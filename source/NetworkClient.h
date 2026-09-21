// NetworkClient.h
//
// The client half of the Endless Sky multiplayer protocol. It opens a TCP
// connection to a NetworkServer, performs the login handshake (nickname +
// password), sends the local player's input each frame, and dispatches the
// typed messages the server sends back (snapshots, chat, login results).
//
// Framing matches NetworkProtocol: every message is
//     [ length : uint32 LE ][ type : uint8 ][ payload ... ].
#pragma once

#include "NetworkProtocol.h"
#include "NetworkSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class NetworkClient
{
public:
	// Called when the server sends a world snapshot.
	using SnapshotHandler = std::function<void(const NetworkSnapshot &)>;
	// Called once when the server accepts or rejects the login.
	using LoginHandler = std::function<void(bool accepted, const std::string &reason, uint32_t playerId)>;
	// Called when the server relays a chat line.
	using ChatHandler = std::function<void(const NetworkProtocol::ChatMessage &)>;
	// Called when the connection is lost or the server shuts down.
	using DisconnectHandler = std::function<void(const std::string &reason)>;

	NetworkClient();
	~NetworkClient();

	// Non-copyable: this class owns a socket.
	NetworkClient(const NetworkClient &) = delete;
	NetworkClient &operator=(const NetworkClient &) = delete;

	void SetSnapshotHandler(SnapshotHandler handler);
	void SetLoginHandler(LoginHandler handler);
	void SetChatHandler(ChatHandler handler);
	void SetDisconnectHandler(DisconnectHandler handler);

	// Open a TCP connection to the given host and port. This does not perform
	// the login handshake; call Login() once connected.
	bool Connect(const std::string &host, uint16_t port);

	// Send a login request with the given nickname and password.
	void Login(const std::string &nickname, const std::string &password);

	// Send the local player's input state.
	void SendInput(uint32_t buttons, float thrust, float turn);

	// Send a chat line to the server.
	void SendChat(const std::string &text);

	// Receive and dispatch any pending messages. Call every frame.
	void Poll();

	void Disconnect();

	bool IsConnected() const;
	// True once the server has accepted the login.
	bool IsLoggedIn() const;
	uint32_t PlayerId() const;

private:
	void HandlePacket(NetworkProtocol::MessageType type, const uint8_t *payload, size_t size);
	// Queue a framed message for sending.
	void SendMessage(NetworkProtocol::MessageType type, const std::vector<uint8_t> &payload);
	// Try to write any queued outgoing bytes to the socket.
	void FlushSend();
	// Tear down the connection and notify the disconnect handler.
	void Fail(const std::string &reason);

	SnapshotHandler snapshotHandler;
	LoginHandler loginHandler;
	ChatHandler chatHandler;
	DisconnectHandler disconnectHandler;

	bool connected = false;
	bool loggedIn = false;
	uint32_t playerId = 0;

	// The underlying socket. -1 means "not connected".
	int socketFd = -1;

	// Buffer used to accumulate bytes until a complete, length-prefixed
	// message has been received.
	std::vector<uint8_t> receiveBuffer;
	// Buffer of bytes waiting to be written to the socket.
	std::vector<uint8_t> sendBuffer;
};
