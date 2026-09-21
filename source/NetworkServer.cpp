// NetworkServer.cpp
#include "NetworkServer.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
	// The size of the length prefix that precedes every framed message.
	constexpr size_t LENGTH_PREFIX_SIZE = sizeof(uint32_t);

	// How many ships the world simulation supports. Keeps snapshots bounded.
	constexpr size_t MAX_PLAYERS = 64;

	// Physics constants for the placeholder world simulation.
	constexpr double TURN_RATE = 3.0;      // radians per second
	constexpr double ACCELERATION = 200.0; // units per second squared
	constexpr double SPAWN_RADIUS = 200.0;

	// Set a socket to non-blocking mode. Returns false on failure.
	bool SetNonBlocking(int fd)
	{
		const int flags = fcntl(fd, F_GETFL, 0);
		if(flags < 0)
			return false;
		return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
	}

	// Disable Nagle's algorithm so small input packets go out immediately.
	void SetNoDelay(int fd)
	{
		const int one = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	}
}

NetworkServer::NetworkServer()
{
}

NetworkServer::~NetworkServer()
{
	Stop();
}

bool NetworkServer::Start(uint16_t newPort, const std::string &newPassword, const std::string &newServerName)
{
	Stop();

	port = newPort;
	password = newPassword;
	serverName = newServerName;

	addrinfo hints{};
	hints.ai_family = AF_INET;        // LAN play: IPv4 is sufficient.
	hints.ai_socktype = SOCK_STREAM;  // TCP.
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;      // Bind to all interfaces.

	const std::string portString = std::to_string(port);

	addrinfo *results = nullptr;
	if(getaddrinfo(nullptr, portString.c_str(), &hints, &results) != 0)
		return false;

	int fd = -1;
	for(addrinfo *entry = results; entry != nullptr; entry = entry->ai_next)
	{
		fd = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
		if(fd < 0)
			continue;

		const int one = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

		if(bind(fd, entry->ai_addr, entry->ai_addrlen) == 0 && listen(fd, 16) == 0)
			break;

		close(fd);
		fd = -1;
	}

	freeaddrinfo(results);

	if(fd < 0)
		return false;

	if(!SetNonBlocking(fd))
	{
		close(fd);
		return false;
	}

	listenFd = fd;
	nextPlayerId = 1;
	tick = 0;

	Log("Server started on port " + std::to_string(port)
		+ (password.empty() ? " (no password)." : " (password protected)."));
	return true;
}

void NetworkServer::Stop()
{
	for(Client &client : clients)
	{
		if(client.socketFd >= 0)
		{
			shutdown(client.socketFd, SHUT_RDWR);
			close(client.socketFd);
		}
	}
	clients.clear();

	for(PendingClient &client : pending)
	{
		if(client.socketFd >= 0)
		{
			shutdown(client.socketFd, SHUT_RDWR);
			close(client.socketFd);
		}
	}
	pending.clear();

	if(listenFd >= 0)
	{
		shutdown(listenFd, SHUT_RDWR);
		close(listenFd);
		listenFd = -1;
	}
}

bool NetworkServer::IsRunning() const
{
	return listenFd >= 0;
}

uint16_t NetworkServer::Port() const
{
	return port;
}

void NetworkServer::SetLogHandler(LogHandler handler)
{
	logHandler = std::move(handler);
}

std::vector<NetworkServer::Player> NetworkServer::Players() const
{
	std::vector<Player> result;
	result.reserve(clients.size());
	for(const Client &client : clients)
		result.push_back(Player{client.id, client.nickname});
	return result;
}

size_t NetworkServer::PlayerCount() const
{
	return clients.size();
}

void NetworkServer::Poll()
{
	if(listenFd < 0)
		return;

	AcceptNewClients();
	ReadFromPending();
	ReadFromClients();
}

void NetworkServer::AcceptNewClients()
{
	for(;;)
	{
		sockaddr_storage address{};
		socklen_t addressLength = sizeof(address);

		const int fd = accept(listenFd, reinterpret_cast<sockaddr *>(&address), &addressLength);
		if(fd < 0)
		{
			// No more pending connections (or a transient error).
			break;
		}

		if(!SetNonBlocking(fd))
		{
			close(fd);
			continue;
		}
		SetNoDelay(fd);

		PendingClient client;
		client.socketFd = fd;
		pending.push_back(std::move(client));
	}
}

void NetworkServer::ReadFromPending()
{
	// Iterate by index because HandlePendingMessage may erase the current
	// element (on a successful login) or drop it (on a malformed message).
	for(size_t i = 0; i < pending.size(); )
	{
		const int fd = pending[i].socketFd;

		// Read everything currently available into this client's buffer.
		uint8_t buffer[4096];
		bool closed = false;
		for(;;)
		{
			const ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
			if(received > 0)
			{
				pending[i].receiveBuffer.insert(pending[i].receiveBuffer.end(), buffer, buffer + received);
				continue;
			}
			if(received == 0)
			{
				closed = true;
				break;
			}
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			if(errno == EINTR)
				continue;
			closed = true;
			break;
		}

		if(closed)
		{
			DropPending(i, "connection closed before login");
			continue;
		}

		// Extract complete frames, one at a time. After each frame we re-find
		// the pending entry by socket fd, because handling a login promotes the
		// client (erasing it from `pending`).
		bool drop = false;
		bool gone = false;
		for(;;)
		{
			// Locate the current pending entry (it may have moved).
			size_t index = pending.size();
			for(size_t j = 0; j < pending.size(); ++j)
				if(pending[j].socketFd == fd)
				{
					index = j;
					break;
				}
			if(index == pending.size())
			{
				gone = true;
				break;
			}

			std::vector<uint8_t> &receiveBuffer = pending[index].receiveBuffer;
			if(receiveBuffer.size() < LENGTH_PREFIX_SIZE)
				break;

			const uint32_t messageLength = NetworkProtocol::ReadUint32(receiveBuffer.data());
			if(messageLength < 1 || messageLength > NetworkProtocol::MAX_MESSAGE_SIZE)
			{
				drop = true;
				break;
			}
			if(receiveBuffer.size() < LENGTH_PREFIX_SIZE + messageLength)
				break;

			const uint8_t *frame = receiveBuffer.data() + LENGTH_PREFIX_SIZE;
			const auto type = static_cast<NetworkProtocol::MessageType>(frame[0]);
			const size_t payloadSize = messageLength - 1;

			// Copy the frame out before handling, since handling may erase the
			// entry (and thus invalidate `receiveBuffer`).
			std::vector<uint8_t> frameCopy(frame, frame + messageLength);

			HandlePendingMessage(pending[index], type, frameCopy.data() + 1, payloadSize);

			// Re-find the entry and consume the frame.
			for(size_t j = 0; j < pending.size(); ++j)
				if(pending[j].socketFd == fd)
				{
					pending[j].receiveBuffer.erase(
						pending[j].receiveBuffer.begin(),
						pending[j].receiveBuffer.begin() + LENGTH_PREFIX_SIZE + messageLength);
					break;
				}
		}

		if(gone)
		{
			// The client was promoted to a player; nothing more to do here.
			continue;
		}

		if(drop)
		{
			for(size_t j = 0; j < pending.size(); ++j)
				if(pending[j].socketFd == fd)
				{
					DropPending(j, "malformed message");
					break;
				}
			continue;
		}

		++i;
	}
}

void NetworkServer::ReadFromClients()
{
	for(size_t i = 0; i < clients.size(); )
	{
		Client &client = clients[i];

		uint8_t buffer[4096];
		bool closed = false;
		for(;;)
		{
			const ssize_t received = recv(client.socketFd, buffer, sizeof(buffer), 0);
			if(received > 0)
			{
				client.receiveBuffer.insert(client.receiveBuffer.end(), buffer, buffer + received);
				continue;
			}
			if(received == 0)
			{
				closed = true;
				break;
			}
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			if(errno == EINTR)
				continue;
			closed = true;
			break;
		}

		if(closed)
		{
			DisconnectClient(i, "connection closed");
			continue;
		}

		bool drop = false;
		for(;;)
		{
			if(client.receiveBuffer.size() < LENGTH_PREFIX_SIZE)
				break;

			const uint32_t messageLength = NetworkProtocol::ReadUint32(client.receiveBuffer.data());
			if(messageLength < 1 || messageLength > NetworkProtocol::MAX_MESSAGE_SIZE)
			{
				drop = true;
				break;
			}
			if(client.receiveBuffer.size() < LENGTH_PREFIX_SIZE + messageLength)
				break;

			const uint8_t *frame = client.receiveBuffer.data() + LENGTH_PREFIX_SIZE;
			const auto type = static_cast<NetworkProtocol::MessageType>(frame[0]);
			const uint8_t *payload = frame + 1;
			const size_t payloadSize = messageLength - 1;

			HandleClientMessage(client, type, payload, payloadSize);

			client.receiveBuffer.erase(
				client.receiveBuffer.begin(),
				client.receiveBuffer.begin() + LENGTH_PREFIX_SIZE + messageLength);
		}

		if(drop)
		{
			DisconnectClient(i, "malformed message");
			continue;
		}

		// Flush anything queued for this client.
		FlushSend(client);
		++i;
	}
}

void NetworkServer::FlushSend(PendingClient &client)
{
	if(client.socketFd < 0)
		return;

	while(!client.sendBuffer.empty())
	{
		const ssize_t sent = send(client.socketFd, client.sendBuffer.data(), client.sendBuffer.size(), MSG_NOSIGNAL);
		if(sent > 0)
		{
			client.sendBuffer.erase(client.sendBuffer.begin(), client.sendBuffer.begin() + sent);
			continue;
		}
		if(sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		if(sent < 0 && errno == EINTR)
			continue;
		// Broken connection; the read loop will notice and clean up.
		return;
	}
}

void NetworkServer::FlushSend(Client &client)
{
	if(client.socketFd < 0)
		return;

	while(!client.sendBuffer.empty())
	{
		const ssize_t sent = send(client.socketFd, client.sendBuffer.data(), client.sendBuffer.size(), MSG_NOSIGNAL);
		if(sent > 0)
		{
			client.sendBuffer.erase(client.sendBuffer.begin(), client.sendBuffer.begin() + sent);
			continue;
		}
		if(sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		if(sent < 0 && errno == EINTR)
			continue;
		return;
	}
}

void NetworkServer::DropPending(size_t index, const std::string &reason)
{
	if(index >= pending.size())
		return;

	PendingClient &client = pending[index];
	if(client.socketFd >= 0)
	{
		shutdown(client.socketFd, SHUT_RDWR);
		close(client.socketFd);
	}
	pending.erase(pending.begin() + index);

	(void)reason;
}

void NetworkServer::DisconnectClient(size_t index, const std::string &reason)
{
	if(index >= clients.size())
		return;

	Client &client = clients[index];
	Log(client.nickname + " left the game (" + reason + ").");

	if(client.socketFd >= 0)
	{
		shutdown(client.socketFd, SHUT_RDWR);
		close(client.socketFd);
	}
	clients.erase(clients.begin() + index);
}

void NetworkServer::HandlePendingMessage(PendingClient &client, NetworkProtocol::MessageType type,
	const uint8_t *payload, size_t size)
{
	if(type != NetworkProtocol::MessageType::LoginRequest)
	{
		// The only message a not-yet-authenticated client may send is a login.
		NetworkProtocol::LoginResponse response;
		response.playerId = 0;
		NetworkProtocol::WriteFixedString(response.reason, sizeof(response.reason), "Expected a login request.");
		SendToPending(client, NetworkProtocol::MessageType::LoginRejected,
			NetworkProtocol::BuildLoginResponse(response.playerId, response.reason));
		return;
	}

	NetworkProtocol::LoginRequest request;
	if(!NetworkProtocol::ParseLoginRequest(payload, size, request))
	{
		SendToPending(client, NetworkProtocol::MessageType::LoginRejected,
			NetworkProtocol::BuildLoginResponse(0, "Malformed login request."));
		return;
	}

	const std::string nickname = NetworkProtocol::ReadFixedString(request.nickname, sizeof(request.nickname));
	const std::string givenPassword = NetworkProtocol::ReadFixedString(request.password, sizeof(request.password));

	auto reject = [&](const std::string &reason)
	{
		SendToPending(client, NetworkProtocol::MessageType::LoginRejected,
			NetworkProtocol::BuildLoginResponse(0, reason));
		Log("Rejected login from a client: " + reason);
	};

	if(request.version != NetworkProtocol::VERSION)
	{
		reject("Protocol version mismatch (server is v" + std::to_string(NetworkProtocol::VERSION) + ").");
		return;
	}
	if(nickname.empty())
	{
		reject("Nickname must not be empty.");
		return;
	}
	if(nickname.size() >= NetworkProtocol::MAX_NAME_LENGTH)
	{
		reject("Nickname is too long.");
		return;
	}
	if(givenPassword != password)
	{
		reject("Incorrect password.");
		return;
	}
	if(IsNicknameTaken(nickname))
	{
		reject("That nickname is already in use.");
		return;
	}
	if(clients.size() >= MAX_PLAYERS)
	{
		reject("The server is full.");
		return;
	}

	// Promote the pending client to a full player.
	Client newClient;
	newClient.id = nextPlayerId++;
	newClient.nickname = nickname;
	newClient.socketFd = client.socketFd;
	newClient.receiveBuffer = std::move(client.receiveBuffer);
	newClient.sendBuffer = std::move(client.sendBuffer);
	SpawnShip(newClient);

	// The socket is now owned by the client list; detach it from the pending
	// entry so DropPending/Stop do not close it twice.
	client.socketFd = -1;

	// Remove the pending entry (by socket identity, since the reference may be
	// invalidated by the push_back below).
	const int fd = newClient.socketFd;
	for(size_t i = 0; i < pending.size(); ++i)
		if(pending[i].socketFd == fd)
		{
			pending.erase(pending.begin() + i);
			break;
		}

	clients.push_back(std::move(newClient));
	Client &added = clients.back();

	SendToClient(added, NetworkProtocol::MessageType::LoginAccepted,
		NetworkProtocol::BuildLoginResponse(added.id, ""));

	Log(nickname + " joined the game (id " + std::to_string(added.id) + ").");

	// Announce the new player to everyone via chat.
	NetworkProtocol::ChatMessage announcement;
	announcement.senderId = 0;
	NetworkProtocol::WriteFixedString(announcement.sender, sizeof(announcement.sender), "Server");
	NetworkProtocol::WriteFixedString(announcement.text, sizeof(announcement.text), nickname + " joined the game.");
	const std::vector<uint8_t> chatPayload = NetworkProtocol::BuildChatMessage(announcement);
	for(Client &c : clients)
		SendToClient(c, NetworkProtocol::MessageType::ChatMessage, chatPayload);
}

void NetworkServer::HandleClientMessage(Client &client, NetworkProtocol::MessageType type,
	const uint8_t *payload, size_t size)
{
	switch(type)
	{
		case NetworkProtocol::MessageType::PlayerInput:
		{
			NetworkProtocol::InputState input;
			if(NetworkProtocol::ParseInputState(payload, size, input))
				client.input = input;
			break;
		}
		case NetworkProtocol::MessageType::ChatSend:
		{
			NetworkProtocol::ChatMessage message;
			if(!NetworkProtocol::ParseChatMessage(payload, size, message))
				break;

			// The server is authoritative about who sent the message.
			message.senderId = client.id;
			NetworkProtocol::WriteFixedString(message.sender, sizeof(message.sender), client.nickname);

			const std::vector<uint8_t> chatPayload = NetworkProtocol::BuildChatMessage(message);
			for(Client &c : clients)
				SendToClient(c, NetworkProtocol::MessageType::ChatMessage, chatPayload);
			break;
		}
		case NetworkProtocol::MessageType::Disconnect:
		{
			for(size_t i = 0; i < clients.size(); ++i)
				if(clients[i].socketFd == client.socketFd)
				{
					DisconnectClient(i, "client disconnected");
					break;
				}
			break;
		}
		default:
			break;
	}
}

void NetworkServer::SendToPending(PendingClient &client, NetworkProtocol::MessageType type,
	const std::vector<uint8_t> &payload)
{
	QueueMessage(client.sendBuffer, type, payload);
	FlushSend(client);
}

void NetworkServer::SendToClient(Client &client, NetworkProtocol::MessageType type,
	const std::vector<uint8_t> &payload)
{
	QueueMessage(client.sendBuffer, type, payload);
	FlushSend(client);
}

void NetworkServer::QueueMessage(std::vector<uint8_t> &buffer, NetworkProtocol::MessageType type,
	const std::vector<uint8_t> &payload)
{
	const uint32_t messageLength = static_cast<uint32_t>(1 + payload.size());
	if(messageLength > NetworkProtocol::MAX_MESSAGE_SIZE)
		return;

	NetworkProtocol::WriteUint32(buffer, messageLength);
	buffer.push_back(static_cast<uint8_t>(type));
	buffer.insert(buffer.end(), payload.begin(), payload.end());
}

void NetworkServer::Update(double deltaTime)
{
	if(listenFd < 0)
		return;

	StepWorld(deltaTime);
	BroadcastSnapshot();
}

void NetworkServer::StepWorld(double deltaTime)
{
	for(Client &client : clients)
	{
		NetworkShipState &ship = client.ship;

		// Turn.
		ship.angle += client.input.turn * TURN_RATE * deltaTime;

		// Thrust along the facing direction.
		const double thrust = client.input.thrust * ACCELERATION * deltaTime;
		ship.velocityX += std::cos(ship.angle) * thrust;
		ship.velocityY += std::sin(ship.angle) * thrust;

		// Simple drag so ships do not accelerate forever.
		const double drag = std::pow(0.5, deltaTime);
		ship.velocityX *= drag;
		ship.velocityY *= drag;

		// Integrate position.
		ship.x += ship.velocityX * deltaTime;
		ship.y += ship.velocityY * deltaTime;
	}
}

void NetworkServer::BroadcastSnapshot()
{
	++tick;

	NetworkSnapshot snapshot;
	snapshot.SetTick(tick);
	for(const Client &client : clients)
		snapshot.AddShip(client.ship);

	const std::vector<uint8_t> payload = snapshot.Serialize();
	for(Client &client : clients)
		SendToClient(client, NetworkProtocol::MessageType::Snapshot, payload);
}

void NetworkServer::SpawnShip(Client &client)
{
	// Spread players evenly around a circle so they do not overlap at spawn.
	const double angle = (client.id % 8) * (3.14159265358979323846 / 4.0);
	client.ship.id = client.id;
	client.ship.x = std::cos(angle) * SPAWN_RADIUS;
	client.ship.y = std::sin(angle) * SPAWN_RADIUS;
	client.ship.velocityX = 0.0;
	client.ship.velocityY = 0.0;
	client.ship.angle = angle;
	client.ship.hull = 100;
}

void NetworkServer::BroadcastChat(const std::string &text)
{
	NetworkProtocol::ChatMessage message;
	message.senderId = 0;
	NetworkProtocol::WriteFixedString(message.sender, sizeof(message.sender), "Server");
	NetworkProtocol::WriteFixedString(message.text, sizeof(message.text), text);

	const std::vector<uint8_t> payload = NetworkProtocol::BuildChatMessage(message);
	for(Client &client : clients)
		SendToClient(client, NetworkProtocol::MessageType::ChatMessage, payload);
}

bool NetworkServer::IsNicknameTaken(const std::string &nickname) const
{
	for(const Client &client : clients)
		if(client.nickname == nickname)
			return true;
	return false;
}

void NetworkServer::Log(const std::string &message) const
{
	if(logHandler)
		logHandler(message);
}
