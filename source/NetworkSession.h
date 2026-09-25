/* NetworkSession.h
Copyright (c) 2026 by BerryRock0

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

// NetworkSession.h
//
// Owns the client-side connection to a multiplayer server and bridges it to
// the local GameModel. It performs the login handshake (nickname + password),
// forwards the local player's input, applies incoming snapshots to the game
// model, and surfaces chat lines and errors to the UI.
#pragma once

#include "NetworkClient.h"
#include "NetworkProtocol.h"
#include "NetworkSnapshot.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class GameModel;
class PlayerInfo;

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

	// A single recorded chat line.
	struct ChatLine
	{
		std::string sender;
		std::string text;
	};

	// Called when the server relays a chat line.
	using ChatHandler = std::function<void(const std::string &sender, const std::string &text)>;
	// Called when the connection fails or is lost.
	using ErrorHandler = std::function<void(const std::string &message)>;
	// Called when the login handshake succeeds and the session becomes active.
	using LoggedInHandler = std::function<void()>;

	explicit NetworkSession(GameModel &game, PlayerInfo &player);
	~NetworkSession();

	// Open a connection and immediately attempt to log in with the given
	// nickname and password. Returns false only if the socket could not be
	// opened; the login result arrives asynchronously via the error handler.
	bool Connect(const std::string &address, uint16_t port,
		const std::string &nickname, const std::string &password);

	// Whether the player can return to the last server. A normal Disconnect
	// keeps the server remembered so the player can enter it again; local-game
	// transitions call ForgetServer() when they want to leave it permanently.
	bool HasLastServer() const;
	// Whether a server was connected to before at all. This remains true after
	// a normal Disconnect and is used to pre-fill the connection form.
	bool HasConnectedServer() const;
	const std::string &LastAddress() const;
	uint16_t LastPort() const;
	const std::string &LastNickname() const;
	const std::string &LastPassword() const;
	// Reconnect to the last server using the remembered credentials.
	bool Reconnect();
	// Forget the remembered server when leaving multiplayer for a local game
	// or stopping a host. A normal Disconnect deliberately does not call this:
	// the player must be able to enter the same server again.
	void ForgetServer();

	void Disconnect();
	void Poll();
	bool IsConnected() const;
	bool IsLoggedIn() const;
	// True while the current player belongs to a multiplayer session. This
	// remains true across an unexpected disconnect so reconnect UI can keep
	// the single-player save protected.
	bool IsNetworkMode() const;
	void Update(double deltaTime);
	void SendPlayerInput(uint32_t buttons, float thrust, float turn);
	void SendChat(const std::string &text);
	// Queue a slash command for execution by MainPanel on the game thread.
	// Commands are client-authoritative and never sent as ordinary chat.
	bool QueueCheatCommand(const std::string &text);
	// Remove and return commands submitted through multiplayer chat.
	std::vector<std::string> TakeQueuedCheatCommands();
	// Surface a locally executed command result in the chat overlay/log.
	void ReportCheatResult(const std::string &text);
	// Report the local flagship's model. The session remembers the last
	// reported name and only sends a message when it changes, so this is safe
	// to call every frame.
	void SendShipModel(const std::string &model);
	// Send the authoritative state of the local flagship.
	void SendShipState(const NetworkShipState &state);
	// Upload the local player's full serialized pilot save to the server so
	// it can persist real progress (credits, ships, outfits, missions,
	// conditions) in its world file. Safe to call every frame; the caller
	// decides when the pilot is worth uploading (typically while landed).
	void SendPilotSave(const std::string &text);

	State GetState() const;
	uint32_t PlayerId() const;
	// The nickname used when logging in; useful for labeling the player's own
	// chat lines.
	const std::string &Nickname() const;
	const std::string &LastError() const;

	// The shared world model this session drives.
	GameModel &Game();

	// The world the server owns, if one has been received. This is set before
	// the login handler fires, so a fresh network pilot can adopt the server's
	// start system, planet, date, and spawn point instead of a local save.
	bool HasWorldInfo() const;
	const NetworkProtocol::WorldInfo &WorldInfo() const;
	// Whether the server returned a stored pilot save for this nickname at
	// login (set before the login handler fires). The client resumes that
	// save instead of starting a fresh pilot.
	bool HasSavedPilot() const;
	const std::string &SavedPilot() const;

	void SetChatHandler(ChatHandler handler);
	void SetErrorHandler(ErrorHandler handler);
	void SetLoggedInHandler(LoggedInHandler handler);

	// The rolling log of chat lines seen in this session. Local sends are
	// recorded here as well, so the log can be shown in the chat UI.
	const std::deque<ChatLine> &ChatLog() const;
	// Record a chat line locally (used for the player's own sends).
	void RecordChat(const std::string &sender, const std::string &text);

private:
	void HandleSnapshot(const NetworkSnapshot &snapshot);
	void HandleLogin(bool accepted, const std::string &reason, uint32_t id);
	void HandleChat(const NetworkProtocol::ChatMessage &message);
	void HandleDisconnect(const std::string &reason);

	std::unique_ptr<NetworkClient> client;
	GameModel &game;
	PlayerInfo &player;
	State state = State::Offline;

	uint32_t playerId = 0;
	std::string lastError;
	std::string reportedModel;
	std::string nickname;
	std::deque<ChatLine> chatLog;
	std::deque<std::string> queuedCheatCommands;

	// The last server this session successfully connected to (see
	// HasLastServer()). Credentials are copied here only after login succeeds,
	// so a failed connection cannot create a bogus "Return to server" action.
	std::string lastAddress;
	uint16_t lastPort = 0;
	std::string lastNickname;
	std::string lastPassword;

	// Credentials for the connection currently being attempted. They remain
	// separate from the last successful server until the login is accepted.
	std::string pendingAddress;
	uint16_t pendingPort = 0;
	std::string pendingNickname;
	std::string pendingPassword;

	// True only after ForgetServer(), when a local-game transition explicitly
	// cancels the remembered-server return option.
	bool disconnectedDeliberately = false;

	// The world parameters broadcast by the server at login.
	NetworkProtocol::WorldInfo worldInfo;
	bool hasWorldInfo = false;

	// The stored pilot save the server returned for our nickname, if any.
	std::string savedPilot;
	bool hasSavedPilot = false;

	ChatHandler chatHandler;
	ErrorHandler errorHandler;
	LoggedInHandler loggedInHandler;
};
