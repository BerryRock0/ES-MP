// NetworkSession.h
#pragma once

#include "NetworkSnapshot.h"

#include <cstdint>
#include <functional>
#include <string>

class GameModel;

class NetworkSession
{
public:
    enum class State
    {
        Offline,
        Connecting,
        Connected,
        Disconnecting
    };

    explicit NetworkSession(GameModel &game);
    ~NetworkSession();

    bool Connect(const std::string &host, uint16_t port);
    void Disconnect();

    void Update(double deltaTime);

    void SendPlayerInput(
        uint32_t buttons,
        float thrust,
        float turn);

    State GetState() const;

private:
    class NetworkClient *client = nullptr;
    GameModel &game;
    State state = State::Offline;

    void HandleSnapshot(const NetworkSnapshot &snapshot);
};
