// GameModel.h
#pragma once

#include "NetworkSnapshot.h"
#include "Point.h"

#include <cstdint>
#include <map>

// The state of a single ship as seen through the network. The authoritative
// values come from the server snapshot; the render position is interpolated
// locally so ships move smoothly between snapshots.
struct NetworkShip
{
	uint32_t id = 0;

	// Authoritative position coming from the latest snapshot.
	Point target;
	// Render position, interpolated toward the target each frame.
	Point position;
	Point velocity;
	double angle = 0.0;
	int hull = 0;
	uint32_t lastSeenTick = 0;
	// The model name of the ship, used to pick a representative sprite for
	// drawing remote players.
	std::string model;
	// The name of the system the ship is currently flying in.
	std::string system;
};

class GameModel
{
public:
	// Merge the latest authoritative snapshot into the local state.
	void ApplyNetworkSnapshot(const NetworkSnapshot &snapshot);
	// Move every ship's render position toward its target.
	void UpdateNetworkInterpolation(double deltaTime);

	void SetLocalPlayer(uint32_t playerId);
	void Clear();

	bool IsConnected() const;
	uint32_t LocalPlayerId() const;
	const NetworkShip *LocalShip() const;
	const std::map<uint32_t, NetworkShip> &Ships() const;

	// The fixed reference point of the shared world. It is set once to the
	// local player's spawn position and never changes, so the view does not
	// shift whenever the local player moves (only when a remote player does).
	bool HasAnchor() const;
	const Point &Anchor() const;

private:
	std::map<uint32_t, NetworkShip> ships;
	uint32_t localPlayerId = 0;
	Point anchor;
	bool haveAnchor = false;
	bool connected = false;
};