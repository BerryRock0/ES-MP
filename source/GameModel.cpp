// GameModel.cpp
#include "GameModel.h"

void GameModel::ApplyNetworkSnapshot(const NetworkSnapshot &snapshot)
{
    for(const NetworkShipState &ship : snapshot.Ships())
    {
        // Later:
        // Find the corresponding existing Ship.
        // Store its target position and velocity.
        //
        // Example:
        // Ship *localShip = FindNetworkShip(ship.id);
        // localShip->SetNetworkTarget(...);

        (void)ship;
    }
}

void GameModel::UpdateNetworkInterpolation(double deltaTime)
{
    // Interpolate remote ships toward their latest
    // network positions here.

    (void)deltaTime;
}
