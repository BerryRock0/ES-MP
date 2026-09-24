// NetworkSession.cpp
#include "NetworkSession.h"
#include "GameModel.h"
#include "PlayerInfo.h"

#include <memory>
#include <string>
#include <utility>

NetworkSession::NetworkSession(GameModel &game, PlayerInfo &player) : game(game), player(player)
{
}

NetworkSession::~NetworkSession()
{
	Disconnect();
}

bool NetworkSession::Connect(const std::string &host, uint16_t port,
	const std::string &nickname, const std::string &password)
{
	// Drop any previous connection before starting a new one. If the old
	// client has already dropped unexpectedly, preserve the return action so a
	// failed retry does not make the server permanently non-returnable.
	const bool preserveReturn = HasLastServer();
	const bool wasNetworkMode = player.IsNetworkMode();
	const bool oldClientWasConnected = client && client->IsConnected();
	if(client)
		Disconnect();
	if(preserveReturn && !oldClientWasConnected)
		disconnectedDeliberately = false;

	// Keep the attempted credentials separate until login succeeds. This keeps
	// a failed connection from being offered as a "Return to server" target.
	pendingAddress = host;
	pendingPort = port;
	pendingNickname = nickname;
	pendingPassword = password;

	lastError.clear();
	state = State::Connecting;
	this->nickname = nickname;
	worldInfo = NetworkProtocol::WorldInfo();
	hasWorldInfo = false;
	savedPilot.clear();
	hasSavedPilot = false;
	// Protect an existing single-player pilot for the entire handshake, not
	// only after the server has accepted the login. If no pilot is loaded yet,
	// leave it empty until the login handler creates the network pilot.
	const bool protectLocalPlayer = wasNetworkMode || player.IsLoaded();
	if(protectLocalPlayer)
	{
		player.SetNetworkMode(true);
		// Keep a local pilot protected even if the socket or login fails. The
		// game may already have been exposed to the network UI/state by the
		// time the result is known; only New()/Load() should make it saveable
		// again.
		player.SetNetworkAttached(true);
	}

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
	client->SetWorldInfoHandler([this](const NetworkProtocol::WorldInfo &world)
	{
		worldInfo = world;
		hasWorldInfo = true;
	});
	client->SetSavedPilotHandler([this](const std::string &text)
	{
		// Arrives before the login is accepted, so by the time the login
		// handler fires the client already knows whether to resume a stored
		// pilot or start a fresh one.
		savedPilot = text;
		hasSavedPilot = true;
	});
	client->SetDisconnectHandler([this](const std::string &reason)
	{
		HandleDisconnect(reason);
	});

	if(!client->Connect(host, port))
	{
		client.reset();
		pendingAddress.clear();
		pendingPort = 0;
		pendingNickname.clear();
		pendingPassword.clear();
		state = State::Offline;
		if(protectLocalPlayer)
			player.SetNetworkMode(false);
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
	// Before tearing the socket down, hand the server our latest pilot state
	// so a leave (or a reconnect) does not roll the shared world back to the
	// last periodic upload. Only a pilot that currently belongs to a network
	// session uploads, and only while landed, which matches how normal
	// Endless Sky saves work.
	if(state == State::Connected && client && player.IsNetworkMode()
			&& player.IsLoaded() && player.GetPlanet())
		client->SendPilotSave(player.SaveToString());

	state = State::Disconnecting;

	if(client)
	{
		client->Disconnect();
		client.reset();
	}

	state = State::Offline;
	playerId = 0;
	reportedModel.clear();
	game.Clear();
	worldInfo = NetworkProtocol::WorldInfo();
	hasWorldInfo = false;
	savedPilot.clear();
	hasSavedPilot = false;
	pendingAddress.clear();
	pendingPort = 0;
	pendingNickname.clear();
	pendingPassword.clear();

	// Keep the last successful server remembered so the player can enter it
	// again. Local-game transitions call ForgetServer() when they explicitly
	// want to leave multiplayer for good.
	player.SetNetworkMode(false);
	// The flight no longer belongs to any server session. A later login must
	// rebuild its pilot from the server's data, so a remote player's save can
	// never leak into another player's session (or back into single-player).
	player.SetNetworkNickname("");
	disconnectedDeliberately = false;
}

bool NetworkSession::HasLastServer() const
{
	return !lastAddress.empty() && lastPort != 0 && !disconnectedDeliberately;
}

bool NetworkSession::HasConnectedServer() const
{
	return !lastAddress.empty() && lastPort != 0;
}



void NetworkSession::ForgetServer()
{
	Disconnect();

	lastAddress.clear();
	lastPort = 0;
	lastNickname.clear();
	lastPassword.clear();
	pendingAddress.clear();
	pendingPort = 0;
	pendingNickname.clear();
	pendingPassword.clear();
	disconnectedDeliberately = true;
}



const std::string &NetworkSession::LastAddress() const
{
	return lastAddress;
}

uint16_t NetworkSession::LastPort() const
{
	return lastPort;
}

const std::string &NetworkSession::LastNickname() const
{
	return lastNickname;
}

const std::string &NetworkSession::LastPassword() const
{
	return lastPassword;
}

bool NetworkSession::Reconnect()
{
	if(!HasLastServer())
		return false;
	return Connect(lastAddress, lastPort, lastNickname, lastPassword);
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

const std::string &NetworkSession::Nickname() const
{
	return nickname;
}

GameModel &NetworkSession::Game()
{
	return game;
}

bool NetworkSession::HasWorldInfo() const
{
	return hasWorldInfo;
}

const NetworkProtocol::WorldInfo &NetworkSession::WorldInfo() const
{
	return worldInfo;
}

void NetworkSession::SetChatHandler(ChatHandler handler)
{
	chatHandler = std::move(handler);
}

void NetworkSession::SetErrorHandler(ErrorHandler handler)
{
	errorHandler = std::move(handler);
}

void NetworkSession::SetLoggedInHandler(LoggedInHandler handler)
{
	loggedInHandler = std::move(handler);
}

const std::deque<NetworkSession::ChatLine> &NetworkSession::ChatLog() const
{
	return chatLog;
}

void NetworkSession::RecordChat(const std::string &sender, const std::string &text)
{
	chatLog.push_back({sender, text});
	if(chatLog.size() > 64)
		chatLog.pop_front();
}

void NetworkSession::Poll()
{
	if(!client)
		return;

	const bool wasAuthenticating = state == State::Authenticating;
	client->Poll();

	// If the client dropped the connection (peer closed it, or a socket
	// error occurred), reflect that in the session state.
	if(!client->IsConnected())
	{
		client.reset();
		state = State::Offline;
		playerId = 0;
		game.Clear();
		reportedModel.clear();
		worldInfo = NetworkProtocol::WorldInfo();
		hasWorldInfo = false;
		savedPilot.clear();
		hasSavedPilot = false;
		if(wasAuthenticating)
			player.SetNetworkMode(false);
		// The drop ends this session's claim on the active flight, so a later
		// login has to rebuild its pilot from the server.
		player.SetNetworkNickname("");
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

bool NetworkSession::IsNetworkMode() const
{
	return player.IsNetworkMode();
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

	// The server does not echo a sender's own message back, so record it
	// locally and route it through the same handler remote lines use: the
	// writer sees their own message in the HUD log exactly like everyone
	// else does.
	RecordChat(nickname, text);
	if(chatHandler)
		chatHandler(nickname, text);
	client->SendChat(text);
}

void NetworkSession::SendShipModel(const std::string &model)
{
	if(state != State::Connected || !client || model.empty())
		return;

	if(model == reportedModel)
		return;

	reportedModel = model;
	client->SendShipModel(model);
}

void NetworkSession::SendShipState(const NetworkShipState &shipState)
{
	if(this->state != State::Connected || !client)
		return;

	client->SendShipState(shipState);
}

void NetworkSession::SendPilotSave(const std::string &text)
{
	if(state != State::Connected || !client || text.empty())
		return;

	client->SendPilotSave(text);
}

bool NetworkSession::HasSavedPilot() const
{
	return hasSavedPilot;
}

const std::string &NetworkSession::SavedPilot() const
{
	return savedPilot;
}

void NetworkSession::HandleSnapshot(const NetworkSnapshot &snapshot)
{
	game.ApplyNetworkSnapshot(snapshot);
}

void NetworkSession::HandleLogin(bool accepted, const std::string &reason, uint32_t id)
{
	// Login is a terminal handshake. A duplicate or contradictory response
	// must not tear down an already active network session.
	if(state == State::Connected)
		return;

	if(accepted)
	{
		// Commit the credentials only after the server has accepted the login.
		// Until this point a failed socket/login attempt is not a remembered
		// server and must not produce a bogus "Return to server" action.
		lastAddress = std::move(pendingAddress);
		lastPort = pendingPort;
		lastNickname = std::move(pendingNickname);
		lastPassword = std::move(pendingPassword);
		pendingAddress.clear();
		pendingPort = 0;
		pendingNickname.clear();
		pendingPassword.clear();
		disconnectedDeliberately = false;
		state = State::Connected;
		playerId = id;
		if(player.IsLoaded())
			player.SetNetworkMode(true);
		game.SetLocalPlayer(id);
		if(loggedInHandler)
			loggedInHandler();
	}
	else
	{
		pendingAddress.clear();
		pendingPort = 0;
		pendingNickname.clear();
		pendingPassword.clear();
		state = State::Offline;
		playerId = 0;
		player.SetNetworkMode(false);
		game.Clear();
		reportedModel.clear();
		worldInfo = NetworkProtocol::WorldInfo();
		hasWorldInfo = false;
		savedPilot.clear();
		hasSavedPilot = false;
		lastError = reason.empty() ? "Login rejected." : reason;
		if(errorHandler)
			errorHandler(lastError);
		// A rejected login never claims the active flight; the pilot must come
		// from a later successful login.
		player.SetNetworkNickname("");
	}
}

void NetworkSession::HandleChat(const NetworkProtocol::ChatMessage &message)
{
	const std::string sender = NetworkProtocol::ReadFixedString(message.sender, sizeof(message.sender));
	const std::string text = NetworkProtocol::ReadFixedString(message.text, sizeof(message.text));
	RecordChat(sender, text);
	if(chatHandler)
		chatHandler(sender, text);
}

void NetworkSession::HandleDisconnect(const std::string &reason)
{
	const bool wasAuthenticating = state == State::Authenticating;

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
	game.Clear();
	reportedModel.clear();
	worldInfo = NetworkProtocol::WorldInfo();
	hasWorldInfo = false;
	savedPilot.clear();
	hasSavedPilot = false;
	pendingAddress.clear();
	pendingPort = 0;
	pendingNickname.clear();
	pendingPassword.clear();
	if(wasAuthenticating)
		player.SetNetworkMode(false);
	// The disconnect ends this session's claim on the active flight, so a
	// later login has to rebuild its pilot from the server.
	player.SetNetworkNickname("");
}
