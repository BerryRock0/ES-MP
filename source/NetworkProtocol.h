/* NetworkProtocol.h
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

// NetworkProtocol.h
//
// The shared wire protocol used by both the client (NetworkClient) and the
// server (NetworkServer). Every message on the wire is framed as:
//
//     [ length : uint32 little-endian ][ type : uint8 ][ payload ... ]
//
// where "length" counts the type byte plus the payload (i.e. everything that
// follows the length prefix). This mirrors the framing already used by the
// original NetworkClient, but adds a one-byte message type so that the stream
// can carry more than just snapshots (login handshake, chat, input, ...).
//
// All multi-byte integers and floats are little-endian. This protocol is
// deliberately unencrypted, as requested: it is intended for trusted LAN play.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace NetworkProtocol
{
	// Bump this whenever the wire format changes in an incompatible way. The
	// server rejects clients whose version does not match.
	constexpr uint16_t VERSION = 5;

	// The default TCP port used when hosting a LAN world.
	constexpr uint16_t DEFAULT_PORT = 47624;

	// The largest single message we will accept, in bytes. Anything larger is
	// treated as a malformed stream and the connection is dropped.
	constexpr uint32_t MAX_MESSAGE_SIZE = 1u << 20; // 1 MiB

	// Field limits for the login handshake.
	constexpr size_t MAX_NAME_LENGTH = 32;
	constexpr size_t MAX_PASSWORD_LENGTH = 64;
	constexpr size_t MAX_REASON_LENGTH = 128;
	constexpr size_t MAX_CHAT_LENGTH = 256;
	// The number of bytes used to carry a ship model name, both in the
	// ShipModel message and in every snapshot ship entry.
	constexpr size_t MAX_SHIP_MODEL_LENGTH = 32;
	// The number of bytes used to carry a system name in a ship state message
	// and in every snapshot ship entry. Empty for a ship that has not reported
	// its location yet.
	constexpr size_t MAX_SYSTEM_LENGTH = 32;
	// The number of bytes used to carry a planet name in the WorldInfo message.
	constexpr size_t MAX_PLANET_LENGTH = 64;
	// The largest serialized pilot save (the text of an Endless Sky pilot file)
	// this protocol will carry. Pilot saves are uploaded by the client and
	// returned by the server so a player's real progress -- credits, ships,
	// outfits, missions, and conditions -- survives server restarts.
	constexpr size_t MAX_PILOT_SAVE_BYTES = 512 * 1024;

	// The type byte that prefixes every message payload.
	enum class MessageType : uint8_t
	{
		// Client -> server: request to join with a nickname and password.
		LoginRequest = 1,
		// Server -> client: the login succeeded; carries the assigned player id.
		LoginAccepted = 2,
		// Server -> client: the login failed; carries a human-readable reason.
		LoginRejected = 3,
		// Client -> server: the local player's current input state.
		PlayerInput = 4,
		// Server -> client: the authoritative world snapshot.
		Snapshot = 5,
		// Server -> client: a chat line from another player.
		ChatMessage = 6,
		// Client -> server: the local player wants to send a chat line.
		ChatSend = 7,
		// Either direction: a graceful disconnect notice.
		Disconnect = 8,
		// Server -> client: the server is shutting down.
		ServerShutdown = 9,
		// Client -> server: the local player's flagship model name. The server
		// stores it and relays it in every snapshot so other clients can draw
		// a representative sprite for the remote player.
		ShipModel = 10,
		// Client -> server: the local player's flagship, including its absolute
		// position within its current system. The server relays it to everyone,
		// replacing its own simulation of the arena.
		ShipState = 11,
		// Server -> client: the world the server owns (start system, planet,
		// date, and spawn point). Clients adopt it instead of their local save.
		WorldInfo = 12,
		// Client -> server: the local player's full serialized pilot save. The
		// server persists it in its world file so the player's progress (not
		// just a position) survives a restart, and returns it to the same
		// nickname on a later login.
		PilotSave = 13,
		// Server -> client: the stored pilot save for this nickname, if any.
		// Sent before the login is accepted so the client can resume its real
		// progress instead of starting a fresh pilot.
		SavedPilot = 14
	};

	// The login request sent by a client immediately after connecting.
	struct LoginRequest
	{
		uint16_t version = VERSION;
		char nickname[MAX_NAME_LENGTH] = {};
		char password[MAX_PASSWORD_LENGTH] = {};
	};

	// The server's reply to a login request. On success, playerId is the id
	// assigned to the client and reason is empty. On failure, playerId is 0 and
	// reason explains why the login was rejected.
	struct LoginResponse
	{
		uint32_t playerId = 0;
		char reason[MAX_REASON_LENGTH] = {};
	};

	// The input state a client sends each frame.
	struct InputState
	{
		uint32_t buttons = 0;
		float thrust = 0.f;
		float turn = 0.f;
	};

	// The authoritative state of a client's flagship, sent each frame. Angles
	// are in degrees, matching the engine's conventions.
	struct ShipState
	{
		double x = 0.;
		double y = 0.;
		double velocityX = 0.;
		double velocityY = 0.;
		double angle = 0.;
		char system[MAX_SYSTEM_LENGTH] = {};
	};

	// A chat line. senderId is filled in by the server when relaying.
	struct ChatMessage
	{
		uint32_t senderId = 0;
		char sender[MAX_NAME_LENGTH] = {};
		char text[MAX_CHAT_LENGTH] = {};
	};

	// The world that a server owns, broadcast to every client at login so the
	// shared world no longer depends on any client's local save file. Date is
	// sent as day/month/year rather than days-since-epoch so the value stays
	// readable on the wire. The spawn point is where new players should appear
	// in the start system.
	struct WorldInfo
	{
		char system[MAX_SYSTEM_LENGTH] = {};
		char planet[MAX_PLANET_LENGTH] = {};
		uint16_t day = 0;
		uint16_t month = 0;
		uint16_t year = 0;
		double spawnX = 0.;
		double spawnY = 0.;
	};

	// ---- Little-endian serialization helpers -------------------------------

	inline void WriteUint16(std::vector<uint8_t> &out, uint16_t value)
	{
		out.push_back(static_cast<uint8_t>(value & 0xFF));
		out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
	}

	inline void WriteUint32(std::vector<uint8_t> &out, uint32_t value)
	{
		out.push_back(static_cast<uint8_t>(value & 0xFF));
		out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
		out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
		out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
	}

	inline void WriteFloat(std::vector<uint8_t> &out, float value)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		WriteUint32(out, bits);
	}

	inline void WriteUint64(std::vector<uint8_t> &out, uint64_t value)
	{
		for(int i = 0; i < 8; ++i)
			out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
	}

	inline void WriteDouble(std::vector<uint8_t> &out, double value)
	{
		uint64_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		WriteUint64(out, bits);
	}

	inline void WriteBytes(std::vector<uint8_t> &out, const void *data, size_t size)
	{
		const auto *bytes = static_cast<const uint8_t *>(data);
		out.insert(out.end(), bytes, bytes + size);
	}

	inline uint16_t ReadUint16(const uint8_t *data)
	{
		return static_cast<uint16_t>(data[0])
			| (static_cast<uint16_t>(data[1]) << 8);
	}

	inline uint32_t ReadUint32(const uint8_t *data)
	{
		return static_cast<uint32_t>(data[0])
			| (static_cast<uint32_t>(data[1]) << 8)
			| (static_cast<uint32_t>(data[2]) << 16)
			| (static_cast<uint32_t>(data[3]) << 24);
	}

	inline uint64_t ReadUint64(const uint8_t *data)
	{
		uint64_t value = 0;
		for(int i = 0; i < 8; ++i)
			value |= static_cast<uint64_t>(data[i]) << (8 * i);
		return value;
	}

	inline float ReadFloat(const uint8_t *data)
	{
		const uint32_t bits = ReadUint32(data);
		float value = 0.f;
		std::memcpy(&value, &bits, sizeof(value));
		return value;
	}

	inline double ReadDouble(const uint8_t *data)
	{
		const uint64_t bits = ReadUint64(data);
		double value = 0.;
		std::memcpy(&value, &bits, sizeof(value));
		return value;
	}

	// Copy a fixed-size, null-terminated field into a std::string, stopping at
	// the first null byte or the end of the field.
	inline std::string ReadFixedString(const char *field, size_t size)
	{
		size_t length = 0;
		while(length < size && field[length] != '\0')
			++length;
		return std::string(field, length);
	}

	// Copy a std::string into a fixed-size, null-terminated field, truncating
	// if necessary and always leaving room for the terminator.
	inline void WriteFixedString(char *field, size_t size, const std::string &value)
	{
		if(size == 0)
			return;
		const size_t copyLength = value.size() < size - 1 ? value.size() : size - 1;
		std::memcpy(field, value.data(), copyLength);
		field[copyLength] = '\0';
	}

	// ---- Message builders --------------------------------------------------

	inline std::vector<uint8_t> BuildLoginRequest(const std::string &nickname, const std::string &password)
	{
		std::vector<uint8_t> payload;
		WriteUint16(payload, VERSION);
		char name[MAX_NAME_LENGTH] = {};
		char pass[MAX_PASSWORD_LENGTH] = {};
		WriteFixedString(name, sizeof(name), nickname);
		WriteFixedString(pass, sizeof(pass), password);
		WriteBytes(payload, name, sizeof(name));
		WriteBytes(payload, pass, sizeof(pass));
		return payload;
	}

	inline std::vector<uint8_t> BuildLoginResponse(uint32_t playerId, const std::string &reason)
	{
		std::vector<uint8_t> payload;
		WriteUint32(payload, playerId);
		char text[MAX_REASON_LENGTH] = {};
		WriteFixedString(text, sizeof(text), reason);
		WriteBytes(payload, text, sizeof(text));
		return payload;
	}

	inline std::vector<uint8_t> BuildInputState(const InputState &input)
	{
		std::vector<uint8_t> payload;
		WriteUint32(payload, input.buttons);
		WriteFloat(payload, input.thrust);
		WriteFloat(payload, input.turn);
		return payload;
	}

	inline std::vector<uint8_t> BuildShipState(const ShipState &state)
	{
		std::vector<uint8_t> payload;
		WriteDouble(payload, state.x);
		WriteDouble(payload, state.y);
		WriteDouble(payload, state.velocityX);
		WriteDouble(payload, state.velocityY);
		WriteDouble(payload, state.angle);
		WriteBytes(payload, state.system, sizeof(state.system));
		return payload;
	}

	inline std::vector<uint8_t> BuildChatMessage(const ChatMessage &message)
	{
		std::vector<uint8_t> payload;
		WriteUint32(payload, message.senderId);
		char sender[MAX_NAME_LENGTH] = {};
		char text[MAX_CHAT_LENGTH] = {};
		WriteFixedString(sender, sizeof(sender), message.sender);
		WriteFixedString(text, sizeof(text), message.text);
		WriteBytes(payload, sender, sizeof(sender));
		WriteBytes(payload, text, sizeof(text));
		return payload;
	}

	inline std::vector<uint8_t> BuildShipModel(const std::string &model)
	{
		std::vector<uint8_t> payload;
		char field[MAX_SHIP_MODEL_LENGTH] = {};
		WriteFixedString(field, sizeof(field), model);
		WriteBytes(payload, field, sizeof(field));
		return payload;
	}

	inline std::vector<uint8_t> BuildPilotSaveText(const std::string &text)
	{
		std::vector<uint8_t> payload;
		WriteUint32(payload, static_cast<uint32_t>(text.size()));
		WriteBytes(payload, text.data(), text.size());
		return payload;
	}

	inline std::vector<uint8_t> BuildWorldInfo(const WorldInfo &world)
	{
		std::vector<uint8_t> payload;
		WriteBytes(payload, world.system, sizeof(world.system));
		WriteBytes(payload, world.planet, sizeof(world.planet));
		WriteUint16(payload, world.day);
		WriteUint16(payload, world.month);
		WriteUint16(payload, world.year);
		WriteDouble(payload, world.spawnX);
		WriteDouble(payload, world.spawnY);
		return payload;
	}

	// ---- Message parsers ---------------------------------------------------

	inline bool ParseLoginRequest(const uint8_t *data, size_t size, LoginRequest &request)
	{
		const size_t expected = sizeof(uint16_t) + MAX_NAME_LENGTH + MAX_PASSWORD_LENGTH;
		if(size < expected)
			return false;
		request.version = ReadUint16(data);
		std::memcpy(request.nickname, data + sizeof(uint16_t), MAX_NAME_LENGTH);
		std::memcpy(request.password, data + sizeof(uint16_t) + MAX_NAME_LENGTH, MAX_PASSWORD_LENGTH);
		request.nickname[MAX_NAME_LENGTH - 1] = '\0';
		request.password[MAX_PASSWORD_LENGTH - 1] = '\0';
		return true;
	}

	inline bool ParseLoginResponse(const uint8_t *data, size_t size, LoginResponse &response)
	{
		const size_t expected = sizeof(uint32_t) + MAX_REASON_LENGTH;
		if(size < expected)
			return false;
		response.playerId = ReadUint32(data);
		std::memcpy(response.reason, data + sizeof(uint32_t), MAX_REASON_LENGTH);
		response.reason[MAX_REASON_LENGTH - 1] = '\0';
		return true;
	}

	inline bool ParseInputState(const uint8_t *data, size_t size, InputState &input)
	{
		const size_t expected = sizeof(uint32_t) + sizeof(float) * 2;
		if(size < expected)
			return false;
		input.buttons = ReadUint32(data);
		input.thrust = ReadFloat(data + sizeof(uint32_t));
		input.turn = ReadFloat(data + sizeof(uint32_t) + sizeof(float));
		return true;
	}

	inline bool ParseShipState(const uint8_t *data, size_t size, ShipState &state)
	{
		const size_t expected = sizeof(double) * 5 + MAX_SYSTEM_LENGTH;
		if(size < expected)
			return false;
		state.x = ReadDouble(data);
		state.y = ReadDouble(data + sizeof(double));
		state.velocityX = ReadDouble(data + sizeof(double) * 2);
		state.velocityY = ReadDouble(data + sizeof(double) * 3);
		state.angle = ReadDouble(data + sizeof(double) * 4);
		std::memcpy(state.system, data + sizeof(double) * 5, MAX_SYSTEM_LENGTH);
		state.system[MAX_SYSTEM_LENGTH - 1] = '\0';
		return true;
	}

	inline bool ParseChatMessage(const uint8_t *data, size_t size, ChatMessage &message)
	{
		const size_t expected = sizeof(uint32_t) + MAX_NAME_LENGTH + MAX_CHAT_LENGTH;
		if(size < expected)
			return false;
		message.senderId = ReadUint32(data);
		std::memcpy(message.sender, data + sizeof(uint32_t), MAX_NAME_LENGTH);
		std::memcpy(message.text, data + sizeof(uint32_t) + MAX_NAME_LENGTH, MAX_CHAT_LENGTH);
		message.sender[MAX_NAME_LENGTH - 1] = '\0';
		message.text[MAX_CHAT_LENGTH - 1] = '\0';
		return true;
	}

	inline bool ParseShipModel(const uint8_t *data, size_t size, std::string &model)
	{
		if(size < MAX_SHIP_MODEL_LENGTH)
			return false;
		model = ReadFixedString(reinterpret_cast<const char *>(data), MAX_SHIP_MODEL_LENGTH);
		return true;
	}

	inline bool ParsePilotSaveText(const uint8_t *data, size_t size, std::string &text)
	{
		if(size < sizeof(uint32_t))
			return false;
		const size_t length = ReadUint32(data);
		if(length > MAX_PILOT_SAVE_BYTES || size != sizeof(uint32_t) + length)
			return false;
		text.assign(reinterpret_cast<const char *>(data + sizeof(uint32_t)), length);
		// The serialized pilot is plain text; a NUL byte would be a sign of a
		// malformed or malicious message.
		if(text.find('\0') != std::string::npos)
			return false;
		return true;
	}

	inline bool ParseWorldInfo(const uint8_t *data, size_t size, WorldInfo &world)
	{
		const size_t expected = MAX_SYSTEM_LENGTH + MAX_PLANET_LENGTH
			+ sizeof(uint16_t) * 3 + sizeof(double) * 2;
		if(size < expected)
			return false;
		size_t offset = 0;
		std::memcpy(world.system, data + offset, MAX_SYSTEM_LENGTH);
		world.system[MAX_SYSTEM_LENGTH - 1] = '\0';
		offset += MAX_SYSTEM_LENGTH;
		std::memcpy(world.planet, data + offset, MAX_PLANET_LENGTH);
		world.planet[MAX_PLANET_LENGTH - 1] = '\0';
		offset += MAX_PLANET_LENGTH;
		world.day = ReadUint16(data + offset);
		world.month = ReadUint16(data + offset + sizeof(uint16_t));
		world.year = ReadUint16(data + offset + sizeof(uint16_t) * 2);
		world.spawnX = ReadDouble(data + offset + sizeof(uint16_t) * 3);
		world.spawnY = ReadDouble(data + offset + sizeof(uint16_t) * 3 + sizeof(double));
		return true;
	}
}
