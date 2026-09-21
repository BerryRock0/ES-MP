// NetworkClient.cpp
#include "NetworkClient.h"

#include <cerrno>
#include <cstring>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
	// Every message on the wire is prefixed with a 4-byte, little-endian length.
	constexpr size_t LENGTH_PREFIX_SIZE = sizeof(uint32_t);

	// Set a socket to non-blocking mode. Returns false on failure.
	bool SetNonBlocking(int fd)
	{
		const int flags = fcntl(fd, F_GETFL, 0);
		if(flags < 0)
			return false;
		return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
	}
}

NetworkClient::NetworkClient()
{
}

NetworkClient::~NetworkClient()
{
	Disconnect();
}

void NetworkClient::SetSnapshotHandler(SnapshotHandler handler)
{
	snapshotHandler = std::move(handler);
}

void NetworkClient::SetLoginHandler(LoginHandler handler)
{
	loginHandler = std::move(handler);
}

void NetworkClient::SetChatHandler(ChatHandler handler)
{
	chatHandler = std::move(handler);
}

void NetworkClient::SetDisconnectHandler(DisconnectHandler handler)
{
	disconnectHandler = std::move(handler);
}

bool NetworkClient::Connect(const std::string &host, uint16_t port)
{
	Disconnect();

	if(host.empty())
		return false;

	addrinfo hints{};
	hints.ai_family = AF_UNSPEC;      // Allow IPv4 or IPv6.
	hints.ai_socktype = SOCK_STREAM;  // TCP.
	hints.ai_protocol = IPPROTO_TCP;

	const std::string portString = std::to_string(port);

	addrinfo *results = nullptr;
	if(getaddrinfo(host.c_str(), portString.c_str(), &hints, &results) != 0)
		return false;

	int fd = -1;
	for(addrinfo *entry = results; entry != nullptr; entry = entry->ai_next)
	{
		fd = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
		if(fd < 0)
			continue;

		if(!SetNonBlocking(fd))
		{
			close(fd);
			fd = -1;
			continue;
		}

		const int result = connect(fd, entry->ai_addr, entry->ai_addrlen);
		if(result == 0)
			break;

		if(errno == EINPROGRESS)
		{
			// Wait for the connection to complete (or fail) with a timeout.
			fd_set writeSet;
			FD_ZERO(&writeSet);
			FD_SET(fd, &writeSet);

			timeval timeout{};
			timeout.tv_sec = 5;
			timeout.tv_usec = 0;

			const int selected = select(fd + 1, nullptr, &writeSet, nullptr, &timeout);
			if(selected > 0)
			{
				int socketError = 0;
				socklen_t length = sizeof(socketError);
				if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) == 0 && socketError == 0)
					break; // Connected.
			}
		}

		close(fd);
		fd = -1;
	}

	freeaddrinfo(results);

	if(fd < 0)
		return false;

	// Disable Nagle's algorithm so input packets are sent immediately.
	const int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

	socketFd = fd;
	connected = true;
	loggedIn = false;
	playerId = 0;
	receiveBuffer.clear();
	sendBuffer.clear();
	return true;
}

void NetworkClient::Login(const std::string &nickname, const std::string &password)
{
	if(!connected || socketFd < 0)
		return;

	SendMessage(NetworkProtocol::MessageType::LoginRequest,
		NetworkProtocol::BuildLoginRequest(nickname, password));
}

void NetworkClient::Disconnect()
{
	if(socketFd >= 0)
	{
		// Best-effort graceful shutdown; ignore errors.
		shutdown(socketFd, SHUT_RDWR);
		close(socketFd);
		socketFd = -1;
	}

	connected = false;
	loggedIn = false;
	playerId = 0;
	receiveBuffer.clear();
	sendBuffer.clear();
}

void NetworkClient::Fail(const std::string &reason)
{
	Disconnect();
	if(disconnectHandler)
		disconnectHandler(reason);
}

void NetworkClient::Poll()
{
	if(!connected || socketFd < 0)
		return;

	// First, try to push out anything still queued from a previous frame.
	FlushSend();

	uint8_t buffer[4096];
	for(;;)
	{
		const ssize_t received = recv(socketFd, buffer, sizeof(buffer), 0);
		if(received > 0)
		{
			receiveBuffer.insert(receiveBuffer.end(), buffer, buffer + received);
			continue;
		}

		if(received == 0)
		{
			// The peer closed the connection.
			Fail("The server closed the connection.");
			return;
		}

		// received < 0
		if(errno == EAGAIN || errno == EWOULDBLOCK)
			break; // No more data available right now.

		if(errno == EINTR)
			continue;

		// Any other error means the connection is broken.
		Fail("The connection was lost.");
		return;
	}

	// Extract every complete, length-prefixed message from the buffer.
	for(;;)
	{
		if(receiveBuffer.size() < LENGTH_PREFIX_SIZE)
			break;

		const uint32_t messageLength = NetworkProtocol::ReadUint32(receiveBuffer.data());
		if(messageLength < 1 || messageLength > NetworkProtocol::MAX_MESSAGE_SIZE)
		{
			// Malformed stream; drop the connection rather than trust it.
			Fail("Received a malformed message.");
			return;
		}

		if(receiveBuffer.size() < LENGTH_PREFIX_SIZE + messageLength)
			break; // Wait for the rest of the message.

		const uint8_t *frame = receiveBuffer.data() + LENGTH_PREFIX_SIZE;
		const auto type = static_cast<NetworkProtocol::MessageType>(frame[0]);
		const uint8_t *payload = frame + 1;
		const size_t payloadSize = messageLength - 1;

		HandlePacket(type, payload, payloadSize);

		// HandlePacket may have disconnected us (e.g. on a server shutdown).
		if(!connected)
			return;

		receiveBuffer.erase(
			receiveBuffer.begin(),
			receiveBuffer.begin() + LENGTH_PREFIX_SIZE + messageLength);
	}
}

void NetworkClient::SendInput(uint32_t buttons, float thrust, float turn)
{
	if(!connected || socketFd < 0)
		return;

	NetworkProtocol::InputState input;
	input.buttons = buttons;
	input.thrust = thrust;
	input.turn = turn;

	SendMessage(NetworkProtocol::MessageType::PlayerInput,
		NetworkProtocol::BuildInputState(input));
}

void NetworkClient::SendChat(const std::string &text)
{
	if(!connected || socketFd < 0)
		return;

	NetworkProtocol::ChatMessage message;
	NetworkProtocol::WriteFixedString(message.text, sizeof(message.text), text);

	SendMessage(NetworkProtocol::MessageType::ChatSend,
		NetworkProtocol::BuildChatMessage(message));
}

void NetworkClient::SendMessage(NetworkProtocol::MessageType type, const std::vector<uint8_t> &payload)
{
	const uint32_t messageLength = static_cast<uint32_t>(1 + payload.size());
	if(messageLength > NetworkProtocol::MAX_MESSAGE_SIZE)
		return;

	NetworkProtocol::WriteUint32(sendBuffer, messageLength);
	sendBuffer.push_back(static_cast<uint8_t>(type));
	sendBuffer.insert(sendBuffer.end(), payload.begin(), payload.end());

	FlushSend();
}

void NetworkClient::FlushSend()
{
	if(socketFd < 0)
		return;

	while(!sendBuffer.empty())
	{
		const ssize_t sent = send(socketFd, sendBuffer.data(), sendBuffer.size(), MSG_NOSIGNAL);
		if(sent > 0)
		{
			sendBuffer.erase(sendBuffer.begin(), sendBuffer.begin() + sent);
			continue;
		}

		if(sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return; // Socket buffer is full; try again next Poll().

		if(sent < 0 && errno == EINTR)
			continue;

		// The connection is broken.
		Fail("The connection was lost while sending.");
		return;
	}
}

bool NetworkClient::IsConnected() const
{
	return connected;
}

bool NetworkClient::IsLoggedIn() const
{
	return loggedIn;
}

uint32_t NetworkClient::PlayerId() const
{
	return playerId;
}

void NetworkClient::HandlePacket(NetworkProtocol::MessageType type, const uint8_t *payload, size_t size)
{
	switch(type)
	{
		case NetworkProtocol::MessageType::LoginAccepted:
		{
			NetworkProtocol::LoginResponse response;
			if(NetworkProtocol::ParseLoginResponse(payload, size, response))
			{
				loggedIn = true;
				playerId = response.playerId;
				if(loginHandler)
					loginHandler(true, "", playerId);
			}
			break;
		}
		case NetworkProtocol::MessageType::LoginRejected:
		{
			NetworkProtocol::LoginResponse response;
			std::string reason = "Login rejected.";
			if(NetworkProtocol::ParseLoginResponse(payload, size, response))
				reason = NetworkProtocol::ReadFixedString(response.reason, sizeof(response.reason));

			loggedIn = false;
			if(loginHandler)
				loginHandler(false, reason, 0);
			// The server closes the connection after a rejection.
			Fail(reason);
			break;
		}
		case NetworkProtocol::MessageType::Snapshot:
		{
			NetworkSnapshot snapshot;
			if(NetworkSnapshot::Deserialize(payload, size, snapshot) && snapshotHandler)
				snapshotHandler(snapshot);
			break;
		}
		case NetworkProtocol::MessageType::ChatMessage:
		{
			NetworkProtocol::ChatMessage message;
			if(NetworkProtocol::ParseChatMessage(payload, size, message) && chatHandler)
				chatHandler(message);
			break;
		}
		case NetworkProtocol::MessageType::ServerShutdown:
		{
			Fail("The server shut down.");
			break;
		}
		default:
			break;
	}
}
