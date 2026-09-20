// NetworkClient.cpp
#include "NetworkClient.h"

#include <utility>

NetworkClient::NetworkClient(SnapshotHandler handler) : snapshotHandler(std::move(handler)){}

bool NetworkClient::Connect(const std::string &host, uint16_t port)
{  
	connected = true;
    return connected;
}

void NetworkClient::Disconnect()
{
	connected = false;
}

void NetworkClient::Poll()
{  
	if(!connected)
		return;

	std::vector<uint8_t> packet;
	
	if (!ReceivePacket(packet))
		return;

	HandlePacket(packet.data(), packet.size());
}

void NetworkClient::SendInput(uint32_t buttons, float thrust, float turn)
{
	if(!connected)        
	return;
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
