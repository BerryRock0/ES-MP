/* GameModel.h
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
