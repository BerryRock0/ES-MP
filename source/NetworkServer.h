// NetworkServer.h
//
// A small, authoritative LAN game server for Endless Sky multiplayer. It is
// modelled on Minecraft's "Open to LAN" feature: the host starts the server
// in-process (or runs it standalone) and other players join by entering the
// host's address, a nickname, and the shared password.
//
// The server:
//   * listens on a TCP port and accepts multiple clients,
//   * performs a login handshake that validates the protocol version, the
//     nickname (non-empty, within the length limit, not already in use), and
//     the password,
//   * simulates a trivial authoritative world (one ship per player),
//   * broadcasts a NetworkSnapshot to every player each tick, and
//   * relays chat messages.
//
// The protocol is intentionally unencrypted, as requested.
#pragma once

#include "NetworkProtocol.h"
#include "NetworkSnapshot.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

class NetworkServer
{
public:
	// A connected, authenticated player. This is a lightweight view exposed to
	// callers (e.g. the host panel) and does not own any socket state.
	struct Player
	{
		uint32_t id = 0;
		std::string nickname;
	};

	// Called with human-readable status lines (joins, leaves, rejections).
	using LogHandler = std::function<void(const std::string &)>;

	NetworkServer();
	~NetworkServer();

	// Non-copyable: this class owns a listening socket.
	NetworkServer(const NetworkServer &) = delete;
	NetworkServer &operator=(const NetworkServer &) = delete;

	// Begin listening on the given port. An empty password means "no password".
	// Returns false if the socket could not be created or bound.
	bool Start(uint16_t port, const std::string &password, const std::string &serverName);

	// Stop listening and disconnect every client. The optional argument is
	// used internally when restarting so a failed bind cannot rewrite the
	// previous world file.
	void Stop(bool saveWorld = true);

	bool IsRunning() const;
	uint16_t Port() const;

	// Accept new connections and read any pending messages. Call every frame.
	void Poll();

	// Advance the world simulation by deltaTime seconds and broadcast a
	// snapshot to all players. Call every frame while running.
	void Update(double deltaTime);

	// Send a chat line to every connected player.
	void BroadcastChat(const std::string &text);

	// Set the world this server owns (start system, planet, date, and spawn
	// point). It is broadcast to every client at login, so the shared world no
	// longer depends on any client's local save. Call before clients connect.
	void SetWorld(const NetworkProtocol::WorldInfo &world);
	bool HasWorld() const;
	const NetworkProtocol::WorldInfo &World() const;
	// The stored serialized pilot save for a nickname, if one was persisted
	// from an earlier session. Empty when the player has never uploaded one.
	// The in-process host uses this to resume its own progress immediately,
	// without waiting for the asynchronous loopback login handshake.
	std::string PilotSaveFor(const std::string &nickname) const;

	// Where this server persists its own world (the world parameters plus the
	// last known position, velocity, and ship of every player and each
	// player's full serialized pilot save), completely separate from any
	// client's saves or pilots. The world file is written to
	// <directory>/world.txt on Stop() and whenever SaveWorld() is called; the
	// dedicated server also calls it periodically. An empty directory disables
	// persistence. Set before LoadWorld()/Start().
	void SetWorldSaveDirectory(const std::string &directory);
	const std::string &WorldSaveDirectory() const;

	// Write the current world to world.txt in the save directory. The write is
	// atomic (temp file + rename) and is skipped entirely when the world has
	// not changed since the last successful save, so an idle server does not
	// keep rewriting its file and a crash can never leave a half-written
	// world. Returns an error message on failure, or an empty string on
	// success (also returns an empty string when persistence is disabled, no
	// world is set, or the world is clean).
	std::string SaveWorld();
	// Read world.txt back, applying it as the server's world and remembering
	// returning players' last known positions for SpawnShip. Returns an error
	// message on failure, or an empty string on success (the latter also when
	// no saved world exists yet).
	std::string LoadWorld();

	// The folder this server loads its own plugin data from (the dedicated
	// server defaults this to <save-dir>/plugins, a folder of the server's
	// own). The server reads ship definitions from every .txt data file under
	// it. Only ships the server knows are relayed between clients, so the
	// shared world no longer depends on what plugins each client happens to
	// have installed locally. The server never reads any folder the clients
	// use: its world and its ships live entirely inside its own save
	// directory. Call before clients connect.
	void SetPluginsDirectory(const std::string &directory);
	const std::string &PluginsDirectory() const;
	// Scan the plugin directory and load the set of ship models the server
	// knows. A ship a client reports that is not in this set is replaced with
	// the default ship model instead of being relayed. When the folder is
	// missing or contains no ship definitions, enforcement is off and the
	// server relays whatever the clients report, as before.
	void LoadPlugins();
	// The ship model used when a client reports one the server does not know.
	// Only models present in the server's own plugin data are accepted; the
	// server falls back to the first ship it loaded otherwise.
	void SetDefaultShipModel(const std::string &model);
	const std::string &DefaultShipModel() const;

	void SetLogHandler(LogHandler handler);

	// The list of currently connected players.
	std::vector<Player> Players() const;
	size_t PlayerCount() const;

private:
	// A client that has connected but not yet completed the login handshake.
	struct PendingClient
	{
		int socketFd = -1;
		std::vector<uint8_t> receiveBuffer;
		std::vector<uint8_t> sendBuffer;
	};

	// A client that has authenticated and is now a player in the world.
	struct Client
	{
		uint32_t id = 0;
		std::string nickname;
		int socketFd = -1;
		std::vector<uint8_t> receiveBuffer;
		std::vector<uint8_t> sendBuffer;
		NetworkProtocol::InputState input;
		NetworkShipState ship;
	};

	// --- Socket helpers ---
	void AcceptNewClients();
	void ReadFromPending();
	void ReadFromClients();
	void FlushSend(PendingClient &client);
	void FlushSend(Client &client);
	void DropPending(size_t index, const std::string &reason);
	void DisconnectClient(size_t index, const std::string &reason);

	// --- Message handling ---
	void HandlePendingMessage(PendingClient &client, NetworkProtocol::MessageType type,
		const uint8_t *payload, size_t size);
	void HandleClientMessage(Client &client, NetworkProtocol::MessageType type,
		const uint8_t *payload, size_t size);

	// --- Sending ---
	void SendToPending(PendingClient &client, NetworkProtocol::MessageType type,
		const std::vector<uint8_t> &payload);
	void SendToClient(Client &client, NetworkProtocol::MessageType type,
		const std::vector<uint8_t> &payload);
	void QueueMessage(std::vector<uint8_t> &buffer, NetworkProtocol::MessageType type,
		const std::vector<uint8_t> &payload);

	// --- World ---
	void StepWorld(double deltaTime);
	void BroadcastSnapshot();
	void SpawnShip(Client &client);
	// Return model as-is when the server does not enforce ships or knows the
	// model; otherwise the default ship model (possibly empty).
	std::string NormalizeModel(const std::string &model) const;
	// Walk a data directory recursively, collecting every top-level
	// `ship "Name"` definition from its .txt files into knownShipModels.
	// Returns the number of data files read.
	size_t CollectDirectory(const std::string &directory);

	// Check whether a currently connected player already owns this nickname.
	// Poll services closed client sockets before pending logins, so a returning
	// player can reuse a nickname once their old connection has actually gone
	// away, while an active player's nickname cannot be taken over.
	bool IsNicknameTaken(const std::string &nickname) const;
	void Log(const std::string &message) const;

	int listenFd = -1;
	uint16_t port = 0;
	std::string password;
	std::string serverName;

	std::vector<PendingClient> pending;
	std::vector<Client> clients;

	uint32_t nextPlayerId = 1;
	uint32_t tick = 0;

	// The world owned by this server, broadcast to clients at login.
	NetworkProtocol::WorldInfo world;
	bool hasWorld = false;
	// True while the world (or a saved player state) has changed since the
	// last successful save; autosaving skips the write when the world is
	// clean, so an idle server does not keep rewriting its world file.
	bool worldDirty = false;

	// Directory that holds this server's own world file (empty = disabled).
	std::string worldSaveDirectory;
	// Everything the server remembers about one player across sessions.
	struct PlayerState
	{
		// The player's last reported flagship (position, velocity, angle,
		// system, hull), kept so the world file can restore returning players
		// whether they left before the server stopped or joined after restart.
		NetworkShipState ship;
		// The player's full serialized pilot save, uploaded by their client.
		// This is what makes the world file a real save: the player's credits,
		// ships, outfits, missions, and conditions survive a restart, not just
		// a position. Empty when the player never uploaded one.
		std::string pilotSave;
	};
	// Every player's last known state (nickname -> saved state).
	std::map<std::string, PlayerState> playerStates;

	// Folder the server loads its own plugin data from (empty = off).
	std::string pluginsDirectory;
	// The ship models defined in the server's own plugin data. When non-empty,
	// the server enforces that only these ships are relayed between clients.
	std::set<std::string> knownShipModels;
	// The fallback ship model for anything the server does not know.
	std::string defaultShipModel;

	LogHandler logHandler;
};
