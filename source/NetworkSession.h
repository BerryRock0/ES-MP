// NetworkSession.h
//
// Owns the client-side connection to a multiplayer server and bridges it to
// the local GameModel. It performs the login handshake (nickname + password),
// forwards the local player's input, applies incoming snapshots to the game
// model, and surfaces chat lines and errors to the UI.
#pragma once

#include "NetworkProtocol.h"
#include "NetworkSnapshot.h"
#include "NetworkClient.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class GameModel;

class NetworkSession
{
public:
	enum class State
	{
		Offline,
		Connecting,     // Socket is being opened.
		Authenticating, // Socket is open; waiting for the login result.
		Connected,      // Logged in and playing.
		Disconnecting
	};

	// Called when the server relays a chat line.
	using ChatHandler = std::function<void(const std::string &sender, const std::string &text)>;
	// Called when the connection fails or is lost.
	using ErrorHandler = std::function<void(const std::string &message)>;

	explicit NetworkSession(GameModel &game);
	~NetworkSession();

	// Open a connection and immediately attempt to log in with the given
	// nickname and password. Returns false only if the socket could not be
	// opened; the login result arrives asynchronously via the error handler.
	bool Connect(const std::string &address, uint16_t port,
		const std::string &nickname, const std::string &password);

	void Disconnect();
	void Poll();
	bool IsConnected() const;
	bool IsLoggedIn() const;
	void Update(double deltaTime);
	void SendPlayerInput(uint32_t buttons, float thrust, float turn);
	void SendChat(const std::string &text);

	State GetState() const;
	uint32_t PlayerId() const;
	const std::string &LastError() const;

	void SetChatHandler(ChatHandler handler);
	void SetErrorHandler(ErrorHandler handler);

private:
	void HandleSnapshot(const NetworkSnapshot &snapshot);
	void HandleLogin(bool accepted, const std::string &reason, uint32_t id);
	void HandleChat(const NetworkProtocol::ChatMessage &message);
	void HandleDisconnect(const std::string &reason);

	std::unique_ptr<NetworkClient> client;
	GameModel &game;
	State state = State::Offline;

	uint32_t playerId = 0;
	std::string lastError;

	ChatHandler chatHandler;
	ErrorHandler errorHandler;
};
