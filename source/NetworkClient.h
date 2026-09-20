#pragma once

#include "NetworkSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

class NetworkClient
{
public:
	using SnapshotHandler =	std::function<void(const NetworkSnapshot &)>;

	explicit NetworkClient(SnapshotHandler handler);

    bool Connect(const std::string &host, uint16_t port);
	void Poll();
	void SendInput(uint32_t buttons, float thrust, float turn);
    void Disconnect();

    bool IsConnected() const;

private:
	void HandlePacket(const uint8_t *data, size_t size);
	SnapshotHandler snapshotHandler;
	bool connected = false;
};
