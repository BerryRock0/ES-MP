#pragma once

#include "NetworkSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class NetworkClient
{
public:
	using SnapshotHandler = std::function<void(const NetworkSnapshot &)>;

	explicit NetworkClient(SnapshotHandler handler);
	~NetworkClient();

	// Non-copyable: this class owns a socket.
	NetworkClient(const NetworkClient &) = delete;
	NetworkClient &operator=(const NetworkClient &) = delete;

	bool Connect(const std::string &host, uint16_t port);
	void Poll();
	void SendInput(uint32_t buttons, float thrust, float turn);
	void Disconnect();

	bool IsConnected() const;

private:
	void HandlePacket(const uint8_t *data, size_t size);
	// Queue a length-prefixed message for sending.
	void SendMessage(const std::vector<uint8_t> &payload);
	// Try to write any queued outgoing bytes to the socket.
	void FlushSend();

	SnapshotHandler snapshotHandler;
	bool connected = false;

	// The underlying socket. -1 means "not connected".
	int socketFd = -1;

	// Buffer used to accumulate bytes until a complete, length-prefixed
	// message has been received.
	std::vector<uint8_t> receiveBuffer;
	// Buffer of bytes waiting to be written to the socket.
	std::vector<uint8_t> sendBuffer;
};
