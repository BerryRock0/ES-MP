// NetworkSession.cpp
#include "NetworkSession.h"
#include "GameModel.h"
#include "NetworkClient.h"

NetworkSession::NetworkSession(GameModel &game)
    : game(game)
{
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
        return false;
    }

    state = State::Connected;
    return true;
}

void NetworkSession::Disconnect()
{
	if(client)
		client->Disconnect();
    client.reset();
}

void NetworkSession::Poll()
{
	if(client)
		client->Poll();
}

bool NetworkSession::IsConnected()
{
	return client && client->IsConnected();
}

void NetworkSession::Update(double deltaTime)
{
    if(client)
        client->Poll();

    game.UpdateNetworkInterpolation(deltaTime);
}

void NetworkSession::ApplySnapshot(const NetworkSnapshot &snapshot)
{
    (void)snapshot;
}

void NetworkSession::SendPlayerInput(
    uint32_t buttons,
    float thrust,
    float turn)
{
    if(state != State::Connected || !client)
        return;

    client->SendInput(buttons, thrust, turn);
}

void NetworkSession::HandleSnapshot(
    const NetworkSnapshot &snapshot)
{
    game.ApplyNetworkSnapshot(snapshot);
}
