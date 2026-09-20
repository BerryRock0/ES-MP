// NetworkSession.cpp
#include "NetworkSession.h"
#include "GameModel.h"

#include "NetworkClient.h"

#include <memory>
#include <utility>
#include <string>

NetworkSession::NetworkSession(GameModel &game) : game(game)
{}

NetworkSession::~NetworkSession()
{    
	Disconnect();
}

bool NetworkSession::Connect(const std::string &host, uint16_t port)
{
    state = State::Connecting;

    client = std::make_unique<NetworkClient>(
        [this](const NetworkSnapshot &snapshot)
        {
            HandleSnapshot(snapshot);
        });

    if(!client->Connect(host, port))
    {
		client.reset();
		state = State::Offline;
        return false;
    }

    state = State::Connected;
    return true;
}

void NetworkSession::Disconnect()
{
	state = State::Disconnecting;

	if(client)
		client->Disconnect();
    client.reset();

	state = State::Offline;
}

NetworkSession::State NetworkSession::GetState() const
{    
	return state;
}

void NetworkSession::Poll()
{
	if(client)
		client->Poll();
}

bool NetworkSession::IsConnected() const
{
	return state == State::Connected && client && client->IsConnected();
}

void NetworkSession::Update(double deltaTime)
{
    Poll();
    game.UpdateNetworkInterpolation(deltaTime);
}

void NetworkSession::SendPlayerInput(uint32_t buttons, float thrust, float turn)
{
    if(state != State::Connected || !client)
        return;

    client->SendInput(buttons, thrust, turn);
}

void NetworkSession::HandleSnapshot(const NetworkSnapshot &snapshot)
{
    game.ApplyNetworkSnapshot(snapshot);
}
