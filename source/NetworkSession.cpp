// NetworkSession.cpp
#include "NetworkSession.h"
#include "GameModel.h"

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

bool NetworkSession::Connect(const std::string &host, uint16_t port,
	const std::string &nickname, const std::string &password)
{
	// Drop any previous connection before starting a new one.
	if(client)
		Disconnect();

	lastError.clear();
	state = State::Connecting;

	client = std::make_unique<NetworkClient>();
	client->SetSnapshotHandler([this](const NetworkSnapshot &snapshot)
	{
		HandleSnapshot(snapshot);
	});
	client->SetLoginHandler([this](bool accepted, const std::string &reason, uint32_t id)
	{
		HandleLogin(accepted, reason, id);
	});
	client->SetChatHandler([this](const NetworkProtocol::ChatMessage &message)
	{
		HandleChat(message);
	});
	client->SetDisconnectHandler([this](const std::string &reason)
	{
		HandleDisconnect(reason);
	});

	if(!client->Connect(host, port))
	{
		client.reset();
		state = State::Offline;
		lastError = "Could not connect to the server.";
		return false;
	}

	// The socket is open; send the login request and wait for the reply.
	state = State::Authenticating;
	client->Login(nickname, password);
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
	playerId = 0;
}

NetworkSession::State NetworkSession::GetState() const
{
	return state;
}

uint32_t NetworkSession::PlayerId() const
{
	return playerId;
}

const std::string &NetworkSession::LastError() const
{
	return lastError;
}

void NetworkSession::SetChatHandler(ChatHandler handler)
{
	chatHandler = std::move(handler);
}

void NetworkSession::SetErrorHandler(ErrorHandler handler)
{
	errorHandler = std::move(handler);
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
		playerId = 0;
	}
}

bool NetworkSession::IsConnected() const
{
	return (state == State::Connected || state == State::Authenticating)
		&& client && client->IsConnected();
}

bool NetworkSession::IsLoggedIn() const
{
	return state == State::Connected && client && client->IsLoggedIn();
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

void NetworkSession::SendChat(const std::string &text)
{
	if(state != State::Connected || !client)
		return;

	client->SendChat(text);
}

void NetworkSession::HandleSnapshot(const NetworkSnapshot &snapshot)
{
	game.ApplyNetworkSnapshot(snapshot);
}

void NetworkSession::HandleLogin(bool accepted, const std::string &reason, uint32_t id)
{
	if(accepted)
	{
		state = State::Connected;
		playerId = id;
	}
	else
	{
		state = State::Offline;
		playerId = 0;
		lastError = reason.empty() ? "Login rejected." : reason;
		if(errorHandler)
			errorHandler(lastError);
	}
}

void NetworkSession::HandleChat(const NetworkProtocol::ChatMessage &message)
{
	if(!chatHandler)
		return;

	const std::string sender = NetworkProtocol::ReadFixedString(message.sender, sizeof(message.sender));
	const std::string text = NetworkProtocol::ReadFixedString(message.text, sizeof(message.text));
	chatHandler(sender, text);
}

void NetworkSession::HandleDisconnect(const std::string &reason)
{
	// A disconnect during authentication is a login failure; report it.
	if(state == State::Authenticating)
	{
		lastError = reason.empty() ? "Login rejected." : reason;
		if(errorHandler)
			errorHandler(lastError);
	}
	else if(state == State::Connected)
	{
		lastError = reason.empty() ? "Disconnected from the server." : reason;
		if(errorHandler)
			errorHandler(lastError);
	}

	state = State::Offline;
	playerId = 0;
}
