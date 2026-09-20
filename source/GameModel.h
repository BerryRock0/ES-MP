// GameModel.h
#pragma once
#include "NetworkSnapshot.h"

class GameModel
{
public:
    void ApplyNetworkSnapshot(const NetworkSnapshot &snapshot);
    void UpdateNetworkInterpolation(double deltaTime);
};
