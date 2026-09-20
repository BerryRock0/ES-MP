#pragma once

#include <cstdint>
#include <vector>

struct NetworkShipState
{
    uint32_t networkId = 0;

    double x = 0.;
    double y = 0.;
    double vx = 0.;
    double vy = 0.;
    double angle = 0.;

    int hull = 0;
};

struct NetworkSnapshot
{
    uint32_t tick = 0;
    std::vector<NetworkShipState> ships;
};
