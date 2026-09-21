// NetworkClient.cpp
#include "NetworkClient.h"

#include <cerrno>
#include <cstring>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
// Every message on the wire is prefixed with a 4-byte, little-endian length.
constexpr size_t LENGTH_PREFIX_SIZE = sizeof(uint32_t);
// Reject absurdly large frames so a malicious peer cannot exhaust memory.
constexpr uint32_t MAX_MESSAGE_SIZE = 1u << 20; // 1 MiB

void WriteUint32LE(std::vector<uint8_t> &output, uint32_t value)
{
	output.push_back(static_cast<uint8_t>(value & 0xFF));
	output.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
	output.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
	output.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

uint32_t ReadUint32LE(const uint8_t *data)
{
	return static_cast<uint32_t>(data[0])
		| (static_cast<uint32_t>(data[1]) << 8)
		| (static_cast<uint32_t>(data[2]) << 16)
		| (static_cast<uint32_t>(data[3]) << 24);
}

// Set a socket to non-blocking mode. Returns false on failure.
bool SetNonBlocking(int fd)
{
	const int flags = fcntl(fd, F_GETFL, 0);
	if(flags < 0)
		return false;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
}

NetworkClient::NetworkClient(SnapshotHandler handler) : snapshotHandler(std::move(handler))
{
}

NetworkClient::~NetworkClient()
{
	Disconnect();
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

	socketFd = fd;
	connected = true;
	receiveBuffer.clear();
	sendBuffer.clear();
	return true;
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
	receiveBuffer.clear();
	sendBuffer.clear();
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
			Disconnect();
			return;
		}

		// received < 0
		if(errno == EAGAIN || errno == EWOULDBLOCK)
			break; // No more data available right now.

		if(errno == EINTR)
			continue;

		// Any other error means the connection is broken.
		Disconnect();
		return;
	}

	// Extract every complete, length-prefixed message from the buffer.
	for(;;)
	{
		if(receiveBuffer.size() < LENGTH_PREFIX_SIZE)
			break;

		const uint32_t messageLength = ReadUint32LE(receiveBuffer.data());
		if(messageLength > MAX_MESSAGE_SIZE)
		{
			// Malformed stream; drop the connection rather than trust it.
			Disconnect();
			return;
		}

		if(receiveBuffer.size() < LENGTH_PREFIX_SIZE + messageLength)
			break; // Wait for the rest of the message.

		const uint8_t *payload = receiveBuffer.data() + LENGTH_PREFIX_SIZE;
		HandlePacket(payload, messageLength);

		receiveBuffer.erase(
			receiveBuffer.begin(),
			receiveBuffer.begin() + LENGTH_PREFIX_SIZE + messageLength);
	}
}

void NetworkClient::SendInput(uint32_t buttons, float thrust, float turn)
{
	if(!connected || socketFd < 0)
		return;

	// Input message layout: [buttons: u32][thrust: f32][turn: f32].
	std::vector<uint8_t> payload;
	payload.reserve(sizeof(uint32_t) + sizeof(float) * 2);

	const auto *buttonBytes = reinterpret_cast<const uint8_t *>(&buttons);
	payload.insert(payload.end(), buttonBytes, buttonBytes + sizeof(buttons));

	const auto *thrustBytes = reinterpret_cast<const uint8_t *>(&thrust);
	payload.insert(payload.end(), thrustBytes, thrustBytes + sizeof(thrust));

	const auto *turnBytes = reinterpret_cast<const uint8_t *>(&turn);
	payload.insert(payload.end(), turnBytes, turnBytes + sizeof(turn));

	SendMessage(payload);
}

void NetworkClient::SendMessage(const std::vector<uint8_t> &payload)
{
	if(payload.size() > MAX_MESSAGE_SIZE)
		return;

	WriteUint32LE(sendBuffer, static_cast<uint32_t>(payload.size()));
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
		Disconnect();
		return;
	}
}

bool NetworkClient::IsConnected() const
{
	return connected;
}

void NetworkClient::HandlePacket(const uint8_t *data, size_t size)
{
	NetworkSnapshot snapshot;

	if(!NetworkSnapshot::Deserialize(data, size, snapshot))
		return;

	if(snapshotHandler)
		snapshotHandler(snapshot);
}
