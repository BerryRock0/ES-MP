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

	// Stop listening and disconnect every client.
	void Stop();

	bool IsRunning() const;
	uint16_t Port() const;

	// Accept new connections and read any pending messages. Call every frame.
	void Poll();

	// Advance the world simulation by deltaTime seconds and broadcast a
	// snapshot to all players. Call every frame while running.
	void Update(double deltaTime);

	// Send a chat line to every connected player.
	void BroadcastChat(const std::string &text);

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

	LogHandler logHandler;
};
