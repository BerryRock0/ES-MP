/* GameModel.cpp
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

#include "GameModel.h"

#include <algorithm>
#include <cmath>
#include <utility>

void GameModel::ApplyNetworkSnapshot(const NetworkSnapshot &snapshot)
{
	// Mark every ship we know about as "not present in this snapshot."
	// The server sends a full snapshot of every ship, so any ship that fails
	// to appear has left the world and is removed below.
	for(auto &it : ships)
		it.second.lastSeenTick = 0;

	const std::vector<NetworkShipState> &states = snapshot.Ships();
	for(const NetworkShipState &state : states)
	{
		NetworkShip *ship = nullptr;
		auto it = ships.find(state.id);
		if(it == ships.end())
		{
			// A brand new ship. Spawn it at its authoritative position so there
			// is no interpolation gap to animate through, then let the normal
			// smoothing take over on the next snapshot.
			NetworkShip created;
			created.id = state.id;
			created.target = Point(state.x, state.y);
			created.position = created.target;
			it = ships.emplace(state.id, created).first;
			ship = &it->second;
		}
		else
			ship = &it->second;

		ship->target = Point(state.x, state.y);
		if(state.id == localPlayerId)
		{
			// The server relays our reports back; keep the render position in
			// sync with the authoritative one so the world does not lag.
			ship->position = ship->target;

			// Establish the view anchor at the local ship's first-known
			// position. It is only used for diagnostics; ships are now drawn
			// at their absolute world positions.
			if(!haveAnchor)
			{
				anchor = ship->target;
				haveAnchor = true;
			}
		}
		ship->velocity = Point(state.velocityX, state.velocityY);
		// Angles are carried in degrees, matching the engine's conventions.
		ship->angle = state.angle;
		ship->hull = state.hull;
		ship->model = NetworkProtocol::ReadFixedString(state.model, sizeof(state.model));
		ship->system = NetworkProtocol::ReadFixedString(state.system, sizeof(state.system));
		ship->lastSeenTick = snapshot.Tick();
	}

	// Remove ships that were not present in the latest snapshot.
	for(auto it = ships.begin(); it != ships.end();)
	{
		if(it->second.lastSeenTick != snapshot.Tick())
			it = ships.erase(it);
		else
			++it;
	}

	if(localPlayerId != 0 && ships.count(localPlayerId) == 0)
		localPlayerId = 0;

	connected = true;
}

void GameModel::UpdateNetworkInterpolation(double deltaTime)
{
	if(!connected)
		return;

	for(auto &it : ships)
	{
		NetworkShip &ship = it.second;
		if(ship.id == localPlayerId)
			continue;

		// Exponential smoothing toward the authoritative position. At the
		// the defaults (deltaTime of roughly 1 / 60 against a snapshot rate
		// of ~30 Hz) this covers most of the gap in a few frames while still
		// hiding the server's quantized positions.
		const double factor = 1. - std::exp(-8. * deltaTime);
		ship.position += (ship.target - ship.position) * factor;
	}
}

void GameModel::SetLocalPlayer(uint32_t playerId)
{
	localPlayerId = playerId;
}

void GameModel::Clear()
{
	ships.clear();
	localPlayerId = 0;
	anchor = Point();
	haveAnchor = false;
	connected = false;
}

bool GameModel::IsConnected() const
{
	return connected;
}

uint32_t GameModel::LocalPlayerId() const
{
	return localPlayerId;
}

const NetworkShip *GameModel::LocalShip() const
{
	auto it = ships.find(localPlayerId);
	return (it == ships.end()) ? nullptr : &it->second;
}

const std::map<uint32_t, NetworkShip> &GameModel::Ships() const
{
	return ships;
}

bool GameModel::HasAnchor() const
{
	return haveAnchor;
}

const Point &GameModel::Anchor() const
{
	return anchor;
}
