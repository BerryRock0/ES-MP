/* NetworkClient.cpp
Copyright (c) 2026 by BerryRock0

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "NetworkClient.h"

#include <cerrno>
#include <cstring>
#include <utility>

#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	// Winsock has no SIGPIPE, so a send can never raise it; the flag is 0.
	#ifndef MSG_NOSIGNAL
	#define MSG_NOSIGNAL 0
	#endif
	// Winsock sockets are closed with closesocket() and shut down with the
	// SD_* constants instead of SHUT_RDWR.
	#define SocketClose(fd) closesocket(fd)
	#ifndef SHUT_RDWR
	#define SHUT_RDWR SD_BOTH
	#endif
#else
	#include <fcntl.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <sys/select.h>
	#include <sys/socket.h>
	#include <netinet/tcp.h>
	#include <unistd.h>
	#define SocketClose(fd) close(fd)
#endif

namespace
{
	// Every message on the wire is prefixed with a 4-byte, little-endian length.
	constexpr size_t LENGTH_PREFIX_SIZE = sizeof(uint32_t);

	// Set a socket to non-blocking mode. Returns false on failure.
	bool SetNonBlocking(int fd)
	{
#ifdef _WIN32
		u_long mode = 1;
		return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
		const int flags = fcntl(fd, F_GETFL, 0);
		if(flags < 0)
			return false;
		return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
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

void NetworkClient::SetWorldInfoHandler(WorldInfoHandler handler)
{
	worldInfoHandler = std::move(handler);
}

void NetworkClient::SetSavedPilotHandler(SavedPilotHandler handler)
{
	savedPilotHandler = std::move(handler);
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
			SocketClose(fd);
			fd = -1;
			continue;
		}

		const int result = connect(fd, entry->ai_addr, entry->ai_addrlen);
		if(result == 0)
			break;

		#ifdef _WIN32
		// A non-blocking connect on winsock reports WSAEWOULDBLOCK instead of
		// errno EINPROGRESS.
		if(WSAGetLastError() == WSAEWOULDBLOCK)
#else
		if(errno == EINPROGRESS)
#endif
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
#ifdef _WIN32
				int length = sizeof(socketError);
#else
				socklen_t length = sizeof(socketError);
#endif
				if(getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&socketError), &length) == 0
					&& socketError == 0)
					break; // Connected.
			}
		}

		SocketClose(fd);
		fd = -1;
	}

	freeaddrinfo(results);

	if(fd < 0)
		return false;

	// Disable Nagle's algorithm so input packets are sent immediately.
	const int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof(one));

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
		// Tell the server why the socket is closing before shutting it down.
		// This is best effort: a broken connection may reject the write, but a
		// normal leave should be observed immediately by the server.
		if(connected)
		{
			std::vector<uint8_t> frame;
			NetworkProtocol::WriteUint32(frame, 1);
			frame.push_back(static_cast<uint8_t>(NetworkProtocol::MessageType::Disconnect));
			send(socketFd, reinterpret_cast<const char *>(frame.data()), frame.size(), MSG_NOSIGNAL);
		}
		shutdown(socketFd, SHUT_RDWR);
		SocketClose(socketFd);
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
		const int received = recv(socketFd, reinterpret_cast<char *>(buffer), sizeof(buffer), 0);
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
#ifdef _WIN32
		if(WSAGetLastError() == WSAEWOULDBLOCK)
#else
		if(errno == EAGAIN || errno == EWOULDBLOCK)
#endif
			break; // No more data available right now.

#ifdef _WIN32
		if(WSAGetLastError() == WSAEINTR)
#else
		if(errno == EINTR)
#endif
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

void NetworkClient::SendShipModel(const std::string &model)
{
	if(!connected || socketFd < 0)
		return;

	SendMessage(NetworkProtocol::MessageType::ShipModel,
		NetworkProtocol::BuildShipModel(model));
}

void NetworkClient::SendShipState(const NetworkShipState &state)
{
	if(!connected || socketFd < 0)
		return;

	NetworkProtocol::ShipState message;
	message.x = state.x;
	message.y = state.y;
	message.velocityX = state.velocityX;
	message.velocityY = state.velocityY;
	message.angle = state.angle;
	std::memcpy(message.system, state.system, sizeof(message.system));
	message.system[NetworkProtocol::MAX_SYSTEM_LENGTH - 1] = '\0';

	SendMessage(NetworkProtocol::MessageType::ShipState,
		NetworkProtocol::BuildShipState(message));
}

void NetworkClient::SendPilotSave(const std::string &text)
{
	if(!connected || socketFd < 0 || text.empty())
		return;
	if(text.size() > NetworkProtocol::MAX_PILOT_SAVE_BYTES)
		return;

	SendMessage(NetworkProtocol::MessageType::PilotSave,
		NetworkProtocol::BuildPilotSaveText(text));
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
		const int sent = send(socketFd, reinterpret_cast<const char *>(sendBuffer.data()), sendBuffer.size(), MSG_NOSIGNAL);
		if(sent > 0)
		{
			sendBuffer.erase(sendBuffer.begin(), sendBuffer.begin() + sent);
			continue;
		}

#ifdef _WIN32
		if(sent < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
#else
		if(sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
#endif
			return; // Socket buffer is full; try again next Poll().

#ifdef _WIN32
		if(sent < 0 && WSAGetLastError() == WSAEINTR)
#else
		if(sent < 0 && errno == EINTR)
#endif
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
			// Login is a terminal handshake. Ignore duplicate or contradictory
			// responses after the first successful result.
			if(loggedIn)
				break;
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
			// Never let a late rejection undo an already accepted login and
			// re-enable local persistence.
			if(loggedIn)
				break;
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
		case NetworkProtocol::MessageType::WorldInfo:
		{
			NetworkProtocol::WorldInfo world;
			if(NetworkProtocol::ParseWorldInfo(payload, size, world) && worldInfoHandler)
				worldInfoHandler(world);
			break;
		}
		case NetworkProtocol::MessageType::SavedPilot:
		{
			std::string text;
			if(NetworkProtocol::ParsePilotSaveText(payload, size, text) && savedPilotHandler)
				savedPilotHandler(text);
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
