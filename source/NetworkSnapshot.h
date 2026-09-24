/* NetworkSnapshot.h
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

#pragma once

#include "NetworkProtocol.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct NetworkShipState
{
	uint32_t id = 0;

	double x = 0.0;
	double y = 0.0;
	double velocityX = 0.0;
	double velocityY = 0.0;
	double angle = 0.0;

	int hull = 0;

	char model[NetworkProtocol::MAX_SHIP_MODEL_LENGTH] = {};
	// The system the ship is currently in. Empty until a client reports its
	// flagship's location for the first time.
	char system[NetworkProtocol::MAX_SYSTEM_LENGTH] = {};
};

class NetworkSnapshot
{
public:
	uint32_t Tick() const;
	const std::vector<NetworkShipState> &Ships() const;

	void SetTick(uint32_t tick);
	void AddShip(const NetworkShipState &ship);

	// Converts a snapshot into bytes for sending or storage.
	std::vector<uint8_t> Serialize() const;

	// Converts bytes into a snapshot.
	static bool Deserialize(
		const uint8_t *data,
		size_t size,
		NetworkSnapshot &snapshot);

private:
	uint32_t tick = 0;
	std::vector<NetworkShipState> ships;
};

