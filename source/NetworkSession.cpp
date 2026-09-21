// NetworkSession.cpp
#include "NetworkSession.h"
#include "GameModel.h"

#include "NetworkClient.h"

#include <memory>
#include <string>
#include <utility>

NetworkSession::NetworkSession(GameModel &game) : game(game)
{
}

NetworkSession::~NetworkSession()
{
	Disconnect();
}

bool NetworkSession::Connect(const std::string &host, uint16_t port)
{
	// Drop any previous connection before starting a new one.
	if(client)
		Disconnect();

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
	{
		client->Disconnect();
		client.reset();
	}

	state = State::Offline;
}

NetworkSession::State NetworkSession::GetState() const
{
	return state;
}

void NetworkSession::Poll()
{
	if(!client)
		return;

	client->Poll();

	// If the client dropped the connection (peer closed it, or a socket
	// error occurred), reflect that in the session state.
	if(!client->IsConnected())
	{
		client.reset();
		state = State::Offline;
	}
}

bool NetworkSession::IsConnected() const
{
	return state == State::Connected && client && client->IsConnected();
}

void NetworkSession::Update(double deltaTime)
{
	Poll();

	if(state == State::Connected)
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
