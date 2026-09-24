// NetworkServer.cpp
#include "NetworkServer.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
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

	constexpr double SPAWN_RADIUS = 200.0;

	// Persisted world fields are line-oriented. Reject control characters at
	// the network boundary so a client cannot inject additional world-file
	// lines, and keep numeric state finite/bounded before it reaches disk.
	constexpr double MAX_WORLD_COORDINATE = 1.e12;
	constexpr double MAX_WORLD_VELOCITY = 1.e9;
	constexpr double MAX_WORLD_ANGLE = 1.e9;

	// Serialize an arbitrary (multi-line) pilot save into a single world-file
	// line. Base64 keeps the stored text free of whitespace and control
	// characters, so a player's save can never inject extra world-file lines.
	// The server only ever round-trips text it received over the network
	// (bounded by NetworkProtocol::MAX_PILOT_SAVE_BYTES), so this needs no
	// streaming: it is only used for the world file, not for network payloads.
	std::string Base64Encode(const std::string &input)
	{
		static const char TABLE[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string output;
		output.reserve(((input.size() + 2) / 3) * 4);
		std::size_t i = 0;
		while(i + 2 < input.size())
		{
			const uint32_t block =
				(static_cast<unsigned char>(input[i]) << 16)
				| (static_cast<unsigned char>(input[i + 1]) << 8)
				| static_cast<unsigned char>(input[i + 2]);
			output.push_back(TABLE[(block >> 18) & 0x3F]);
			output.push_back(TABLE[(block >> 12) & 0x3F]);
			output.push_back(TABLE[(block >> 6) & 0x3F]);
			output.push_back(TABLE[block & 0x3F]);
			i += 3;
		}
		const std::size_t remaining = input.size() - i;
		if(remaining == 1)
		{
			const uint32_t block = static_cast<unsigned char>(input[i]) << 16;
			output.push_back(TABLE[(block >> 18) & 0x3F]);
			output.push_back(TABLE[(block >> 12) & 0x3F]);
			output.push_back('=');
			output.push_back('=');
		}
		else if(remaining == 2)
		{
			const uint32_t block = (static_cast<unsigned char>(input[i]) << 16)
				| (static_cast<unsigned char>(input[i + 1]) << 8);
			output.push_back(TABLE[(block >> 18) & 0x3F]);
			output.push_back(TABLE[(block >> 12) & 0x3F]);
			output.push_back(TABLE[(block >> 6) & 0x3F]);
			output.push_back('=');
		}
		return output;
	}

	// Strict base64 decoder: rejects stray characters and anything after the
	// '=' padding. Returns false on malformed input; the output is otherwise
	// the exact octets that were encoded.
	bool Base64Decode(const std::string &input, std::string &output)
	{
		static const std::string TABLE =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		static int lookup[256] = {};
		static bool initialized = false;
		if(!initialized)
		{
			for(int i = 0; i < 256; ++i)
				lookup[i] = -1;
			for(std::size_t i = 0; i < TABLE.size(); ++i)
				lookup[static_cast<unsigned char>(TABLE[i])] = static_cast<int>(i);
			initialized = true;
		}

		output.clear();
		output.reserve(input.size() * 3 / 4);
		uint32_t value = 0;
		int bits = 0;
		bool inPadding = false;
		for(const unsigned char c : input)
		{
			if(c == '=')
			{
				inPadding = true;
				continue;
			}
			if(inPadding || lookup[c] < 0)
				return false;
			value = (value << 6) | static_cast<uint32_t>(lookup[c]);
			bits += 6;
			if(bits >= 8)
			{
				bits -= 8;
				output.push_back(static_cast<char>((value >> bits) & 0xFF));
				value &= (1u << bits) - 1;
			}
		}
		// A valid encoding's unpadded remainder is never a lone two-bit tail.
		if(bits > 4)
			return false;
		return true;
	}

	bool IsSafeText(const std::string &value, size_t maxLength, bool rejectEdgeWhitespace)
	{
		if(value.size() >= maxLength)
			return false;
		if(rejectEdgeWhitespace && !value.empty()
				&& (std::isspace(static_cast<unsigned char>(value.front()))
					|| std::isspace(static_cast<unsigned char>(value.back()))))
			return false;
		for(unsigned char character : value)
			if(character < 0x20 || character == 0x7f)
				return false;
		return true;
	}

	bool IsSafeName(const std::string &value, size_t maxLength)
	{
		return !value.empty() && IsSafeText(value, maxLength, true);
	}

	bool IsFiniteWithin(double value, double limit)
	{
		return std::isfinite(value) && std::abs(value) <= limit;
	}

	// Strip leading and trailing whitespace from a line of the world file.
	void Trim(std::string &line)
	{
		const size_t first = line.find_first_not_of(" \t\r\n");
		if(first == std::string::npos)
		{
			line.clear();
			return;
		}
		const size_t last = line.find_last_not_of(" \t\r\n");
		line = line.substr(first, last - first + 1);
	}

	// Format a double so that parsing it back loses no precision.
	std::string FormatDouble(double value)
	{
		std::ostringstream stream;
		stream << std::setprecision(17) << value;
		return stream.str();
	}

	// Strip trailing whitespace and carriage returns from a token.
	void TrimRight(std::string &token)
	{
		const size_t last = token.find_last_not_of(" \t\r\n");
		if(last == std::string::npos)
			token.clear();
		else
			token.erase(last + 1);
	}

	// Read a double-quoted, backslash-escaped name token that starts at
	// rest[0]. Returns everything up to the closing quote.
	std::string ParseQuotedName(const std::string &rest)
	{
		std::string name;
		bool escaping = false;
		for(size_t i = 1; i < rest.size(); ++i)
		{
			const char c = rest[i];
			if(escaping)
			{
				name += c;
				escaping = false;
			}
			else if(c == '\\')
				escaping = true;
			else if(c == '"')
				break;
			else
				name += c;
		}
		return name;
	}

	// Collect the top-level `ship "Name"` definitions from one plugin data
	// file. Indented nodes are ignored, so a ship only counts when it is
	// actually defined here, not when it is merely referenced or modified.
	void CollectShipNames(const std::filesystem::path &path, std::set<std::string> &ships)
	{
		std::ifstream in(path);
		std::string line;
		while(std::getline(in, line))
		{
			const size_t first = line.find_first_not_of(" \t");
			if(first == std::string::npos)
				continue;              // blank line
			if(line[first] == '#')
				continue;              // comment
			if(first != 0)
				continue;              // only top-level (indentation-0) nodes

			const size_t end = line.find_first_of(" \t", first);
			if(end == std::string::npos)
				continue;              // a bare `ship` with no name; ignore
			const std::string key = line.substr(first, end - first);
			if(key != "ship")
				continue;

			std::string rest = line.substr(end + 1);
			if(rest.empty())
				continue;
			std::string name;
			if(rest[0] == '"')
				name = ParseQuotedName(rest);
			else
			{
				const size_t hash = rest.find('#');
				if(hash != std::string::npos)
					rest = rest.substr(0, hash);
				std::size_t space = rest.find_first_of(" \t");
				name = rest.substr(0, space == std::string::npos ? std::string::npos : space);
				TrimRight(name);
			}
			if(!name.empty())
				ships.insert(name);
		}
	}

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
	// Starting a listener is not itself a clean shutdown. In particular, a
	// failed bind must not rewrite an existing world before reporting failure.
	Stop(false);

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

void NetworkServer::Stop(bool saveWorld)
{
	// Snapshot connected players before clearing the client list. Otherwise a
	// clean shutdown would save only players who happened to disconnect
	// earlier and would overwrite the last known world with stale positions.
	for(const Client &client : clients)
		playerStates[client.nickname].ship = client.ship;
	if(!clients.empty())
		worldDirty = true;

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

	// The server owns its world, so when it stops it writes the world (and the
	// last known position of every player) into its own folder, separate from
	// the client-side saves and pilots. A dedicated server restores it on the
	// next start, so "his world" survives a restart.
	if(saveWorld && hasWorld && !worldSaveDirectory.empty())
	{
		const std::string error = SaveWorld();
		if(!error.empty())
			Log("Could not save the world: " + error);
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
	// Notice closed authenticated sockets before handling new logins. This
	// lets a returning player reuse their nickname without allowing a live
	// player's nickname to be taken over.
	ReadFromClients();
	ReadFromPending();
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
		const int fd = client.socketFd;

		uint8_t buffer[4096];
		bool closed = false;
		for(;;)
		{
			const ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
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

		// Locate the client that owns `fd`. It may have moved within the vector
		// (or been removed entirely) while previous frames were being handled.
		auto findClient = [&]() -> Client *
		{
			for(Client &candidate : clients)
				if(candidate.socketFd == fd)
					return &candidate;
			return nullptr;
		};

		bool drop = false;
		for(;;)
		{
			Client *current = findClient();
			if(!current)
				// The client disconnected while handling a previous frame.
				break;

			if(current->receiveBuffer.size() < LENGTH_PREFIX_SIZE)
				break;

			const uint32_t messageLength = NetworkProtocol::ReadUint32(current->receiveBuffer.data());
			if(messageLength < 1 || messageLength > NetworkProtocol::MAX_MESSAGE_SIZE)
			{
				drop = true;
				break;
			}
			if(current->receiveBuffer.size() < LENGTH_PREFIX_SIZE + messageLength)
				break;

			const uint8_t *frame = current->receiveBuffer.data() + LENGTH_PREFIX_SIZE;
			const auto type = static_cast<NetworkProtocol::MessageType>(frame[0]);
			const uint8_t *payload = frame + 1;
			const size_t payloadSize = messageLength - 1;

			HandleClientMessage(*current, type, payload, payloadSize);

			// The client may have disconnected while handling the message (and
			// thus been erased from `clients`); re-locate it before consuming
			// the frame from its buffer.
			current = findClient();
			if(current)
				current->receiveBuffer.erase(
					current->receiveBuffer.begin(),
					current->receiveBuffer.begin() + LENGTH_PREFIX_SIZE + messageLength);
		}

		if(drop)
		{
			for(size_t j = 0; j < clients.size(); ++j)
				if(clients[j].socketFd == fd)
				{
					DisconnectClient(j, "malformed message");
					break;
				}
			continue;
		}

		// If the client disappeared while handling a frame, the remaining
		// elements shifted down; process the new `clients[i]` next instead of
		// skipping it.
		Client *current = findClient();
		if(!current)
			continue;

		// Flush anything queued for this client.
		FlushSend(*current);
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

	// Keep the player's last known state so it survives the next world save.
	playerStates[client.nickname].ship = client.ship;
	worldDirty = true;

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

	if(!IsSafeText(nickname, NetworkProtocol::MAX_NAME_LENGTH, true))
	{
		reject("Nickname contains invalid characters.");
		return;
	}
	if(!IsSafeText(givenPassword, NetworkProtocol::MAX_PASSWORD_LENGTH, false))
	{
		reject("Password contains invalid characters.");
		return;
	}

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
	// A returning player may reuse their nickname after their old connection
	// has closed. Poll() services closed authenticated sockets before pending
	// logins, so any nickname still present here belongs to a live player and
	// must not be overwritten.
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
	const int fd = client.socketFd;
	Client newClient;
	newClient.id = nextPlayerId++;
	newClient.nickname = nickname;
	newClient.socketFd = fd;
	newClient.receiveBuffer = std::move(client.receiveBuffer);
	newClient.sendBuffer = std::move(client.sendBuffer);
	SpawnShip(newClient);

	// The socket is now owned by the client list; detach it from the pending
	// entry so DropPending/Stop do not close it twice. Keep the original fd for
	// the lookup below: changing the pending entry to -1 first makes that
	// lookup impossible and leaves a stale pending client behind.
	client.socketFd = -1;
	for(size_t i = 0; i < pending.size(); ++i)
		if(pending[i].socketFd == fd)
		{
			pending.erase(pending.begin() + i);
			break;
		}

	clients.push_back(std::move(newClient));
	Client &added = clients.back();

	// Tell the new client what world this server owns before confirming the
	// login, so that when the login handler fires it already knows which
	// system, planet, date, and spawn point to adopt.
	if(hasWorld)
		SendToClient(added, NetworkProtocol::MessageType::WorldInfo,
			NetworkProtocol::BuildWorldInfo(world));

	// Give a returning player their stored pilot save (credits, ships,
	// outfits, missions, conditions) before the login lands, so the client can
	// resume its real progress instead of starting a fresh pilot.
	const auto remembered = playerStates.find(nickname);
	if(remembered != playerStates.end() && !remembered->second.pilotSave.empty())
		SendToClient(added, NetworkProtocol::MessageType::SavedPilot,
			NetworkProtocol::BuildPilotSaveText(remembered->second.pilotSave));

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
			{
				if(!std::isfinite(input.thrust) || !std::isfinite(input.turn)
						|| std::abs(input.thrust) > 1.e3 || std::abs(input.turn) > 1.e3)
				{
					Log("Rejected invalid player input from " + client.nickname + ".");
					for(size_t i = 0; i < clients.size(); ++i)
						if(clients[i].socketFd == client.socketFd)
						{
							DisconnectClient(i, "invalid player input");
							break;
						}
					return;
				}
				client.input = input;
			}
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
				// The sender already records their own message locally
				// (NetworkSession::RecordChat), so echoing it back to them
				// would show it twice in their chat log.
				if(c.id != client.id)
					SendToClient(c, NetworkProtocol::MessageType::ChatMessage, chatPayload);
			break;
		}
		case NetworkProtocol::MessageType::ShipModel:
		{
			std::string model;
			if(NetworkProtocol::ParseShipModel(payload, size, model))
			{
				if(!IsSafeText(model, NetworkProtocol::MAX_SHIP_MODEL_LENGTH, true))
				{
					Log("Rejected invalid ship model from " + client.nickname + ".");
					for(size_t i = 0; i < clients.size(); ++i)
						if(clients[i].socketFd == client.socketFd)
						{
							DisconnectClient(i, "invalid ship model");
							break;
						}
					return;
				}
				// The server is authoritative about what ships exist: a model
				// not defined in its own plugin data is not relayed to the
				// other clients. The offending client still flies their own
				// ship locally, but everyone else sees the server's default.
				const std::string accepted = NormalizeModel(model);
				if(accepted != model)
					Log(client.nickname + " reported the ship \"" + model
						+ "\", which is not in the server's ship data; using \""
						+ accepted + "\" instead.");
				NetworkProtocol::WriteFixedString(client.ship.model, sizeof(client.ship.model), accepted);
				playerStates[client.nickname].ship = client.ship;
				worldDirty = true;
			}
			break;
		}
		case NetworkProtocol::MessageType::ShipState:
		{
			NetworkProtocol::ShipState state;
			if(NetworkProtocol::ParseShipState(payload, size, state))
			{
				const std::string system = NetworkProtocol::ReadFixedString(state.system, sizeof(state.system));
				const bool valid = IsSafeText(system, NetworkProtocol::MAX_SYSTEM_LENGTH, true)
					&& IsFiniteWithin(state.x, MAX_WORLD_COORDINATE)
					&& IsFiniteWithin(state.y, MAX_WORLD_COORDINATE)
					&& IsFiniteWithin(state.velocityX, MAX_WORLD_VELOCITY)
					&& IsFiniteWithin(state.velocityY, MAX_WORLD_VELOCITY)
					&& IsFiniteWithin(state.angle, MAX_WORLD_ANGLE);
				if(!valid)
				{
					Log("Rejected invalid ship state from " + client.nickname + ".");
					for(size_t i = 0; i < clients.size(); ++i)
						if(clients[i].socketFd == client.socketFd)
						{
							DisconnectClient(i, "invalid ship state");
							break;
						}
					return;
				}
				client.ship.x = state.x;
				client.ship.y = state.y;
				client.ship.velocityX = state.velocityX;
				client.ship.velocityY = state.velocityY;
				client.ship.angle = state.angle;
				std::memcpy(client.ship.system, state.system, sizeof(client.ship.system));
				client.ship.system[NetworkProtocol::MAX_SYSTEM_LENGTH - 1] = '\0';
				playerStates[client.nickname].ship = client.ship;
				worldDirty = true;
			}
			break;
		}
		case NetworkProtocol::MessageType::PilotSave:
		{
			// The client uploads its full serialized pilot save so the server's
			// world file records real progress (credits, ships, outfits,
			// missions, conditions), not just a position. The text is validated
			// and bounded by ParsePilotSaveText before it is persisted.
			std::string pilotText;
			if(NetworkProtocol::ParsePilotSaveText(payload, size, pilotText))
			{
				playerStates[client.nickname].pilotSave = pilotText;
				worldDirty = true;
			}
			else
				Log("Ignored an invalid pilot save upload from " + client.nickname + ".");
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
	// Positions are now supplied by the clients themselves (ShipState
	// messages) and relayed as-is, so the server no longer simulates motion.
	(void)deltaTime;
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
	// A player who was here before (this run, or a previous server session
	// restored from the world file) reappears where they were instead of at
	// the spawn point.
	auto known = playerStates.find(client.nickname);
	if(known != playerStates.end())
	{
		const NetworkShipState &state = known->second.ship;
		client.ship.id = client.id;
		client.ship.x = state.x;
		client.ship.y = state.y;
		client.ship.velocityX = state.velocityX;
		client.ship.velocityY = state.velocityY;
		client.ship.angle = state.angle;
		client.ship.hull = state.hull > 0 ? state.hull : 100;
		// Re-validate the remembered model against the server's current plugin
		// data: a saved world from an earlier loadout (or a model that used to
		// exist) must not smuggle a ship the server no longer knows.
		const std::string restoredModel = NormalizeModel(
			NetworkProtocol::ReadFixedString(state.model, sizeof(state.model)));
		NetworkProtocol::WriteFixedString(client.ship.model, sizeof(client.ship.model), restoredModel);
		std::memcpy(client.ship.system, state.system, sizeof(client.ship.system));
		return;
	}

	// Spread players around the world's spawn point so they do not overlap.
	// Until a client reports its own position, this is where the server places
	// the player; the world's start system is used so the initial snapshot
	// already shows the new player in the right system.
	const double centerX = hasWorld ? world.spawnX : 0.0;
	const double centerY = hasWorld ? world.spawnY : 0.0;
	const double angle = (client.id % 8) * (3.14159265358979323846 / 4.0);
	client.ship.id = client.id;
	client.ship.x = centerX + std::cos(angle) * SPAWN_RADIUS;
	client.ship.y = centerY + std::sin(angle) * SPAWN_RADIUS;
	client.ship.velocityX = 0.0;
	client.ship.velocityY = 0.0;
	client.ship.angle = angle;
	client.ship.hull = 100;
	if(hasWorld)
		NetworkProtocol::WriteFixedString(client.ship.system, sizeof(client.ship.system),
			NetworkProtocol::ReadFixedString(world.system, sizeof(world.system)));
}

void NetworkServer::SetWorld(const NetworkProtocol::WorldInfo &newWorld)
{
	// The host installs its initial fallback immediately after the listener
	// starts, before any client can connect. Only reject changes once the
	// shared world has live participants.
	if(!clients.empty() || !pending.empty())
	{
		Log("Cannot change the server world while players are connected.");
		return;
	}
	const std::string system = NetworkProtocol::ReadFixedString(newWorld.system, sizeof(newWorld.system));
	const std::string planet = NetworkProtocol::ReadFixedString(newWorld.planet, sizeof(newWorld.planet));
	if(!IsSafeName(system, NetworkProtocol::MAX_SYSTEM_LENGTH)
			|| !IsSafeText(planet, NetworkProtocol::MAX_PLANET_LENGTH, true)
			|| !IsFiniteWithin(newWorld.spawnX, MAX_WORLD_COORDINATE)
			|| !IsFiniteWithin(newWorld.spawnY, MAX_WORLD_COORDINATE))
	{
		Log("Rejected an invalid server world configuration.");
		return;
	}
	world = newWorld;
	hasWorld = true;
	// A new server world must not inherit player positions from a previous
	// world. Those positions belong to the old world file and are loaded only
	// by LoadWorld().
	playerStates.clear();
	worldDirty = true;
	Log("World set to " + NetworkProtocol::ReadFixedString(world.system, sizeof(world.system))
		+ " / " + NetworkProtocol::ReadFixedString(world.planet, sizeof(world.planet)) + ".");
}

bool NetworkServer::HasWorld() const
{
	return hasWorld;
}

const NetworkProtocol::WorldInfo &NetworkServer::World() const
{
	return world;
}

std::string NetworkServer::PilotSaveFor(const std::string &nickname) const
{
	const auto known = playerStates.find(nickname);
	if(known == playerStates.end())
		return "";
	return known->second.pilotSave;
}

void NetworkServer::SetWorldSaveDirectory(const std::string &directory)
{
	if(listenFd >= 0)
	{
		Log("Cannot change the server save directory while it is running.");
		return;
	}
	worldSaveDirectory = directory;
}

const std::string &NetworkServer::WorldSaveDirectory() const
{
	return worldSaveDirectory;
}

void NetworkServer::SetPluginsDirectory(const std::string &directory)
{
	if(listenFd >= 0)
	{
		Log("Cannot change the server plugin directory while it is running.");
		return;
	}
	pluginsDirectory = directory;
}

const std::string &NetworkServer::PluginsDirectory() const
{
	return pluginsDirectory;
}

std::string NetworkServer::NormalizeModel(const std::string &model) const
{
	if(model.empty() || knownShipModels.empty())
		return model;
	if(knownShipModels.count(model) > 0)
		return model;
	return defaultShipModel;
}

size_t NetworkServer::CollectDirectory(const std::string &directory)
{
	std::error_code error;
	size_t files = 0;
	for(std::filesystem::recursive_directory_iterator it(directory, error), end;
		it != end && !error; it.increment(error))
	{
		if(error)
		{
			Log("Stopped reading \"" + directory + "\": " + error.message());
			break;
		}
		const auto &entry = *it;
		std::error_code entryError;
		if(!entry.is_regular_file(entryError) || entryError)
			continue;
		if(entry.path().extension() != ".txt")
			continue;
		CollectShipNames(entry.path(), knownShipModels);
		++files;
	}
	return files;
}

void NetworkServer::LoadPlugins()
{
	knownShipModels.clear();
	if(pluginsDirectory.empty())
	{
		Log("Ship enforcement is off (no plugin directory set).");
		return;
	}

	std::error_code error;
	if(std::filesystem::exists(pluginsDirectory, error) && !error)
	{
		if(!std::filesystem::is_directory(pluginsDirectory, error) || error)
		{
			Log(pluginsDirectory + " exists but is not a directory; ship enforcement is off.");
			return;
		}
	}
	else
	{
		Log("No plugin directory at \"" + pluginsDirectory + "\"; ship enforcement is off.");
		return;
	}

	const size_t files = CollectDirectory(pluginsDirectory);
	if(knownShipModels.empty())
	{
		Log("No ship definitions found in " + std::to_string(files) + " data file(s) under \""
			+ pluginsDirectory + "\"; ship enforcement is off.");
		return;
	}

	if(defaultShipModel.empty() || knownShipModels.count(defaultShipModel) == 0)
		defaultShipModel = *knownShipModels.begin();
	Log("Loaded " + std::to_string(knownShipModels.size()) + " ship(s) from "
		+ std::to_string(files) + " data file(s) in \"" + pluginsDirectory + "\". "
		"Ship enforcement is on; unknown ships fall back to \""
		+ defaultShipModel + "\".");
}

void NetworkServer::SetDefaultShipModel(const std::string &model)
{
	if(model.empty())
		return;
	if(!knownShipModels.empty() && knownShipModels.count(model) == 0)
	{
		Log("Default ship \"" + model + "\" is not in the server's ship data; using \""
			+ defaultShipModel + "\" instead.");
		return;
	}
	defaultShipModel = model;
}

const std::string &NetworkServer::DefaultShipModel() const
{
	return defaultShipModel;
}

std::string NetworkServer::SaveWorld()
{
	// Persist only when something actually changed since the last successful
	// save: an idle server must not keep rewriting its world file.
	if(worldSaveDirectory.empty() || !hasWorld || !worldDirty)
		return "";

	const std::string worldSystem = NetworkProtocol::ReadFixedString(world.system, sizeof(world.system));
	const std::string worldPlanet = NetworkProtocol::ReadFixedString(world.planet, sizeof(world.planet));
	if(!IsSafeName(worldSystem, NetworkProtocol::MAX_SYSTEM_LENGTH)
			|| !IsSafeText(worldPlanet, NetworkProtocol::MAX_PLANET_LENGTH, true)
			|| !IsFiniteWithin(world.spawnX, MAX_WORLD_COORDINATE)
			|| !IsFiniteWithin(world.spawnY, MAX_WORLD_COORDINATE))
		return "the server world contains invalid text or coordinates";
	for(const auto &entry : playerStates)
		{
			const std::string model = NetworkProtocol::ReadFixedString(
				entry.second.ship.model, sizeof(entry.second.ship.model));
			const std::string system = NetworkProtocol::ReadFixedString(
				entry.second.ship.system, sizeof(entry.second.ship.system));
			if(!IsSafeName(entry.first, NetworkProtocol::MAX_NAME_LENGTH)
					|| !IsSafeText(model, NetworkProtocol::MAX_SHIP_MODEL_LENGTH, true)
					|| !IsSafeText(system, NetworkProtocol::MAX_SYSTEM_LENGTH, true)
					|| !IsFiniteWithin(entry.second.ship.x, MAX_WORLD_COORDINATE)
					|| !IsFiniteWithin(entry.second.ship.y, MAX_WORLD_COORDINATE)
					|| !IsFiniteWithin(entry.second.ship.velocityX, MAX_WORLD_VELOCITY)
					|| !IsFiniteWithin(entry.second.ship.velocityY, MAX_WORLD_VELOCITY)
					|| !IsFiniteWithin(entry.second.ship.angle, MAX_WORLD_ANGLE)
					|| entry.second.pilotSave.size() > NetworkProtocol::MAX_PILOT_SAVE_BYTES
					|| entry.second.pilotSave.find('\0') != std::string::npos)
				return "a persisted player state contains invalid text or coordinates";
		}

	std::error_code error;
	const std::filesystem::path directory(worldSaveDirectory);
	if(std::filesystem::is_symlink(directory, error))
		return "server save directory must not be a symbolic link: " + directory.string();
	if(error && error != std::errc::no_such_file_or_directory)
		return "could not inspect " + directory.string() + ": " + error.message();
	error.clear();
	std::filesystem::create_directories(directory, error);
	if(error)
		return "could not create " + directory.string() + ": " + error.message();

	// Write to a temporary file in the same directory, then rename it into
	// place, so a crash mid-write can never leave a half-written world file.
	const std::filesystem::path tmpFile = directory / "world.txt.tmp";
	{
		std::ofstream out(tmpFile);
		if(!out)
			return "could not open " + tmpFile.string() + " for writing";

		out << "version 1\n";
		out << "system "
			<< NetworkProtocol::ReadFixedString(world.system, sizeof(world.system)) << "\n";
		out << "planet "
			<< NetworkProtocol::ReadFixedString(world.planet, sizeof(world.planet)) << "\n";
		out << "date " << world.day << " " << world.month << " " << world.year << "\n";
		out << "spawn " << FormatDouble(world.spawnX) << " " << FormatDouble(world.spawnY) << "\n";

		// The server is a relay: positions and ships come from the clients, so
		// the saved world keeps each player's last reported state for a future
		// restart.
		for(const auto &entry : playerStates)
		{
			const std::string &nickname = entry.first;
			const NetworkShipState &ship = entry.second.ship;
			out << "player " << nickname << "\n";
			out << "\tmodel " << NetworkProtocol::ReadFixedString(ship.model, sizeof(ship.model)) << "\n";
			out << "\tsystem " << NetworkProtocol::ReadFixedString(ship.system, sizeof(ship.system)) << "\n";
			out << "\tposition " << FormatDouble(ship.x) << " " << FormatDouble(ship.y) << "\n";
			out << "\tvelocity " << FormatDouble(ship.velocityX) << " " << FormatDouble(ship.velocityY) << "\n";
			out << "\tangle " << FormatDouble(ship.angle) << "\n";
			out << "\thull " << ship.hull << "\n";
			// The full serialized pilot save (credits, ships, outfits,
			// missions, conditions) is stored base64 on a single line so the
			// player's real progress survives a server restart, not just a
			// position.
			if(!entry.second.pilotSave.empty())
				out << "\tpilot " << Base64Encode(entry.second.pilotSave) << "\n";
		}

		out.close();
		if(!out)
		{
			std::error_code removeError;
			std::filesystem::remove(tmpFile, removeError);
			return "could not finish writing " + tmpFile.string();
		}
	}

	const std::filesystem::path file = directory / "world.txt";
	if(std::filesystem::is_symlink(file, error))
	{
		std::error_code removeError;
		std::filesystem::remove(tmpFile, removeError);
		return "server world file must not be a symbolic link: " + file.string();
	}
	if(error && error != std::errc::no_such_file_or_directory)
	{
		std::error_code removeError;
		std::filesystem::remove(tmpFile, removeError);
		return "could not inspect " + file.string() + ": " + error.message();
	}
	error.clear();
	std::filesystem::rename(tmpFile, file, error);
	if(error)
	{
		std::error_code removeError;
		std::filesystem::remove(tmpFile, removeError);
		return "could not move " + tmpFile.string() + " into place: " + error.message();
	}
	worldDirty = false;
	return "";
}

std::string NetworkServer::LoadWorld()
{
	// Loading is transactional from the caller's point of view: a missing,
	// unreadable, or malformed file must not leave an older in-memory world
	// active. The caller can then explicitly install a fresh fallback world.
	playerStates.clear();
	world = NetworkProtocol::WorldInfo();
	hasWorld = false;
	worldDirty = false;
	if(worldSaveDirectory.empty())
		return "";

	const std::filesystem::path file = std::filesystem::path(worldSaveDirectory) / "world.txt";
	std::ifstream in(file);
	if(!in)
		// No saved world yet: not an error, the caller falls back to defaults.
		return "";

	uint16_t day = 0;
	uint16_t month = 0;
	uint16_t year = 0;
	double spawnX = 0.0;
	double spawnY = 0.0;
	std::string systemName;
	std::string planetName;
	bool sawVersion = false;

	std::string playerName;
	NetworkShipState player;
	std::string pendingPilot;
	bool inPlayer = false;

	std::string line;
	while(std::getline(in, line))
	{
		// A player block's sub-fields are indented; everything else is a
		// top-level key on its own line.
		bool subField = (!line.empty() && (line[0] == '\t' || line[0] == ' '));
		std::istringstream stream(line);
		std::string key;
		stream >> key;
		if(key.empty())
			continue;

		std::string rest;
		std::getline(stream, rest);
		Trim(rest);

		if(subField)
		{
			if(!inPlayer)
				return file.string() + " contains an indented field outside a player block";
			if(key == "model")
				NetworkProtocol::WriteFixedString(player.model, sizeof(player.model), rest);
			else if(key == "system")
				NetworkProtocol::WriteFixedString(player.system, sizeof(player.system), rest);
			else if(key == "position")
			{
				std::istringstream numbers(rest);
				if(!(numbers >> player.x >> player.y))
					return file.string() + " contains an invalid player position";
			}
			else if(key == "velocity")
			{
				std::istringstream numbers(rest);
				if(!(numbers >> player.velocityX >> player.velocityY))
					return file.string() + " contains an invalid player velocity";
			}
			else if(key == "angle")
			{
				std::istringstream numbers(rest);
				if(!(numbers >> player.angle))
					return file.string() + " contains an invalid player angle";
			}
			else if(key == "hull")
			{
				std::istringstream numbers(rest);
				int hull = 0;
				if(numbers >> hull)
					player.hull = hull;
			}
			else if(key == "pilot")
			{
				// The stored pilot save is the text of the player's serialized
				// pilot, base64-encoded onto one line. Decode and bound it
				// here so a corrupt world file cannot smuggle arbitrary bytes
				// into the server's memory.
				std::string decoded;
				if(!Base64Decode(rest, decoded)
						|| decoded.size() > NetworkProtocol::MAX_PILOT_SAVE_BYTES
						|| decoded.find('\0') != std::string::npos)
					return file.string() + " contains an invalid stored pilot save";
				pendingPilot = decoded;
			}
			continue;
		}

		if(inPlayer && key != "player")
			return file.string() + " contains a world header after a player block";

		if(key == "player")
		{
			if(inPlayer)
			{
				const std::string model = NetworkProtocol::ReadFixedString(player.model, sizeof(player.model));
				const std::string system = NetworkProtocol::ReadFixedString(player.system, sizeof(player.system));
				if(!IsSafeName(playerName, NetworkProtocol::MAX_NAME_LENGTH)
						|| !IsSafeText(model, NetworkProtocol::MAX_SHIP_MODEL_LENGTH, true)
						|| !IsSafeText(system, NetworkProtocol::MAX_SYSTEM_LENGTH, true)
						|| !IsFiniteWithin(player.x, MAX_WORLD_COORDINATE)
						|| !IsFiniteWithin(player.y, MAX_WORLD_COORDINATE)
						|| !IsFiniteWithin(player.velocityX, MAX_WORLD_VELOCITY)
						|| !IsFiniteWithin(player.velocityY, MAX_WORLD_VELOCITY)
						|| !IsFiniteWithin(player.angle, MAX_WORLD_ANGLE)
						|| playerStates.count(playerName))
					return file.string() + " contains an invalid or duplicate player state";
				playerStates[playerName].ship = player;
				playerStates[playerName].pilotSave = pendingPilot;
			}
			playerName = rest;
			player = NetworkShipState();
			pendingPilot.clear();
			inPlayer = true;
		}
		else if(key == "version")
		{
			if(rest != "1")
				return "unsupported world file version \"" + rest + "\" in " + file.string();
			sawVersion = true;
		}
		else if(key == "system")
			systemName = rest;
		else if(key == "planet")
			planetName = rest;
		else if(key == "date")
		{
			std::istringstream numbers(rest);
			int d = 0, m = 0, y = 0;
			if(!(numbers >> d >> m >> y) || d < 1 || d > 31 || m < 1 || m > 12 || y < 1
					|| y > std::numeric_limits<uint16_t>::max())
				return file.string() + " contains an invalid world date";
			day = static_cast<uint16_t>(d);
			month = static_cast<uint16_t>(m);
			year = static_cast<uint16_t>(y);
		}
		else if(key == "spawn")
		{
			std::istringstream numbers(rest);
			if(!(numbers >> spawnX >> spawnY))
				return file.string() + " contains an invalid spawn point";
		}
	}
	if(inPlayer)
	{
		const std::string model = NetworkProtocol::ReadFixedString(player.model, sizeof(player.model));
		const std::string system = NetworkProtocol::ReadFixedString(player.system, sizeof(player.system));
		if(!IsSafeName(playerName, NetworkProtocol::MAX_NAME_LENGTH)
				|| !IsSafeText(model, NetworkProtocol::MAX_SHIP_MODEL_LENGTH, true)
				|| !IsSafeText(system, NetworkProtocol::MAX_SYSTEM_LENGTH, true)
				|| !IsFiniteWithin(player.x, MAX_WORLD_COORDINATE)
				|| !IsFiniteWithin(player.y, MAX_WORLD_COORDINATE)
				|| !IsFiniteWithin(player.velocityX, MAX_WORLD_VELOCITY)
				|| !IsFiniteWithin(player.velocityY, MAX_WORLD_VELOCITY)
				|| !IsFiniteWithin(player.angle, MAX_WORLD_ANGLE)
				|| playerStates.count(playerName))
			return file.string() + " contains an invalid or duplicate player state";
		playerStates[playerName].ship = player;
		playerStates[playerName].pilotSave = pendingPilot;
	}

	if(!sawVersion)
		return file.string() + " is not a world file";
	if(!IsSafeName(systemName, NetworkProtocol::MAX_SYSTEM_LENGTH)
			|| !IsSafeText(planetName, NetworkProtocol::MAX_PLANET_LENGTH, true)
			|| !IsFiniteWithin(spawnX, MAX_WORLD_COORDINATE)
			|| !IsFiniteWithin(spawnY, MAX_WORLD_COORDINATE))
		return file.string() + " contains invalid world text or coordinates";
	if(day == 0 || month == 0 || year == 0)
		return file.string() + " has an invalid world date";

	NetworkProtocol::WriteFixedString(world.system, sizeof(world.system), systemName);
	NetworkProtocol::WriteFixedString(world.planet, sizeof(world.planet), planetName);
	world.day = day;
	world.month = month;
	world.year = year;
	world.spawnX = spawnX;
	world.spawnY = spawnY;
	hasWorld = true;
	// The restored world is already on disk; nothing new to persist until it
	// (or a player state) actually changes.
	worldDirty = false;
	Log("Restored the server's own world from " + file.string() + ".");
	return "";
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
