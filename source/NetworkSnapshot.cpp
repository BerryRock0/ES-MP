/* NetworkSnapshot.cpp
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

#include "NetworkSnapshot.h"

#include <cstring>

namespace
{
// The size, in bytes, of a single serialized ship record. Used to sanity-check
// the ship count advertised by a packet before allocating memory for it.
// The record is: id (u32) + x/y/vx/vy/angle (5 doubles) + hull (u32)
// + model (32 bytes) + system (32 bytes). All integers are little-endian,
// matching the rest of NetworkProtocol.
constexpr size_t SHIP_RECORD_SIZE =
    sizeof(uint32_t) +                // id
    sizeof(double) * 5 +              // x, y, velocityX, velocityY, angle
    sizeof(uint32_t) +                // hull
    NetworkProtocol::MAX_SHIP_MODEL_LENGTH + // model
    NetworkProtocol::MAX_SYSTEM_LENGTH;      // system

// Copy a fixed-size field in and out of the buffer. These are raw byte
// copies (for the char arrays); all numeric fields go through the explicit
// little-endian helpers below so the wire format is architecture-independent.
template <typename T>
bool ReadBytes(
    const uint8_t *data,
    size_t size,
    size_t &offset,
    T &value)
{
    if(offset > size || sizeof(T) > size - offset)
        return false;

    std::memcpy(
        &value,
        data + offset,
        sizeof(T));

    offset += sizeof(T);
    return true;
}
}

uint32_t NetworkSnapshot::Tick() const
{
    return tick;
}

const std::vector<NetworkShipState> &
NetworkSnapshot::Ships() const
{
    return ships;
}

void NetworkSnapshot::SetTick(uint32_t newTick)
{
    tick = newTick;
}

void NetworkSnapshot::AddShip(
    const NetworkShipState &ship)
{
    ships.push_back(ship);
}

std::vector<uint8_t>
NetworkSnapshot::Serialize() const
{
    std::vector<uint8_t> output;

    const uint32_t shipCount =
        static_cast<uint32_t>(ships.size());

    NetworkProtocol::WriteUint32(output, tick);
    NetworkProtocol::WriteUint32(output, shipCount);

    for(const NetworkShipState &ship : ships)
    {
        NetworkProtocol::WriteUint32(output, ship.id);
        NetworkProtocol::WriteDouble(output, ship.x);
        NetworkProtocol::WriteDouble(output, ship.y);
        NetworkProtocol::WriteDouble(output, ship.velocityX);
        NetworkProtocol::WriteDouble(output, ship.velocityY);
        NetworkProtocol::WriteDouble(output, ship.angle);
        // Preserve the bit pattern of the (possibly negative) int hull value.
        // Since C++20 signed integers are two's complement, this round-trips.
        NetworkProtocol::WriteUint32(output, static_cast<uint32_t>(ship.hull));
        NetworkProtocol::WriteBytes(output, ship.model, sizeof(ship.model));
        NetworkProtocol::WriteBytes(output, ship.system, sizeof(ship.system));
    }

    return output;
}

bool NetworkSnapshot::Deserialize(
    const uint8_t *data,
    size_t size,
    NetworkSnapshot &snapshot)
{
    if(!data)
        return false;

    size_t offset = 0;
    uint32_t newTick = 0;
    uint32_t shipCount = 0;

    if(offset > size || sizeof(uint32_t) > size - offset)
        return false;
    newTick = NetworkProtocol::ReadUint32(data + offset);
    offset += sizeof(uint32_t);

    if(offset > size || sizeof(uint32_t) > size - offset)
        return false;
    shipCount = NetworkProtocol::ReadUint32(data + offset);
    offset += sizeof(uint32_t);

    // Prevent malformed packets from allocating excessive memory. The count
    // must also be consistent with the number of bytes actually remaining.
    const size_t remaining = size - offset;
    if(shipCount > 10000 || shipCount > remaining / SHIP_RECORD_SIZE)
        return false;

    NetworkSnapshot result;
    result.SetTick(newTick);

    for(uint32_t i = 0; i < shipCount; ++i)
    {
        NetworkShipState ship;

        if(offset > size || sizeof(uint32_t) > size - offset)
            return false;
        ship.id = NetworkProtocol::ReadUint32(data + offset);
        offset += sizeof(uint32_t);

        if(offset > size || sizeof(double) > size - offset)
            return false;
        ship.x = NetworkProtocol::ReadDouble(data + offset);
        offset += sizeof(double);

        if(offset > size || sizeof(double) > size - offset)
            return false;
        ship.y = NetworkProtocol::ReadDouble(data + offset);
        offset += sizeof(double);

        if(offset > size || sizeof(double) > size - offset)
            return false;
        ship.velocityX = NetworkProtocol::ReadDouble(data + offset);
        offset += sizeof(double);

        if(offset > size || sizeof(double) > size - offset)
            return false;
        ship.velocityY = NetworkProtocol::ReadDouble(data + offset);
        offset += sizeof(double);

        if(offset > size || sizeof(double) > size - offset)
            return false;
        ship.angle = NetworkProtocol::ReadDouble(data + offset);
        offset += sizeof(double);

        if(offset > size || sizeof(uint32_t) > size - offset)
            return false;
        ship.hull = static_cast<int>(
            NetworkProtocol::ReadUint32(data + offset));
        offset += sizeof(uint32_t);

        if(!ReadBytes(data, size, offset, ship.model))
            return false;
        ship.model[NetworkProtocol::MAX_SHIP_MODEL_LENGTH - 1] = '\0';

        if(!ReadBytes(data, size, offset, ship.system))
            return false;
        ship.system[NetworkProtocol::MAX_SYSTEM_LENGTH - 1] = '\0';

        result.AddShip(ship);
    }

    snapshot = std::move(result);
    return true;
}
