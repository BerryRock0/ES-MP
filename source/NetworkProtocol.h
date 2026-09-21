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
	constexpr uint16_t VERSION = 1;

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
		ServerShutdown = 9
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

	// A chat line. senderId is filled in by the server when relaying.
	struct ChatMessage
	{
		uint32_t senderId = 0;
		char sender[MAX_NAME_LENGTH] = {};
		char text[MAX_CHAT_LENGTH] = {};
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

	inline float ReadFloat(const uint8_t *data)
	{
		const uint32_t bits = ReadUint32(data);
		float value = 0.f;
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
}
