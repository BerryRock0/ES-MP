#include "NetworkSnapshot.h"

#include <cstring>

namespace
{
// The size, in bytes, of a single serialized ship record. Used to sanity-check
// the ship count advertised by a packet before allocating memory for it.
constexpr size_t SHIP_RECORD_SIZE =
    sizeof(uint32_t) +      // id
    sizeof(double) * 5 +    // x, y, velocityX, velocityY, angle
    sizeof(int);            // hull

template <typename T>
void Write(
    std::vector<uint8_t> &output,
    const T &value)
{
    const auto *bytes =
        reinterpret_cast<const uint8_t *>(&value);

    output.insert(
        output.end(),
        bytes,
        bytes + sizeof(T));
}

template <typename T>
bool Read(
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

    Write(output, tick);
    Write(output, shipCount);

    for(const NetworkShipState &ship : ships)
    {
        Write(output, ship.id);
        Write(output, ship.x);
        Write(output, ship.y);
        Write(output, ship.velocityX);
        Write(output, ship.velocityY);
        Write(output, ship.angle);
        Write(output, ship.hull);
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

    if(!Read(data, size, offset, newTick))
        return false;

    if(!Read(data, size, offset, shipCount))
        return false;

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

        if(!Read(data, size, offset, ship.id))
            return false;

        if(!Read(data, size, offset, ship.x))
            return false;

        if(!Read(data, size, offset, ship.y))
            return false;

        if(!Read(data, size, offset, ship.velocityX))
            return false;

        if(!Read(data, size, offset, ship.velocityY))
            return false;

        if(!Read(data, size, offset, ship.angle))
            return false;

        if(!Read(data, size, offset, ship.hull))
            return false;

        result.AddShip(ship);
    }

    snapshot = std::move(result);
    return true;
}
