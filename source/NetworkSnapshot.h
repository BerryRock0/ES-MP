#pragma once

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

