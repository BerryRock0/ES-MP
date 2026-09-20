// NetworkSession.h
#pragma once


#include <cstdint>
#include <functional>
#include <memory>
#include <string>


#include "NetworkSnapshot.h"
#include "NetworkClient.h"

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
	void Poll();
	bool IsConnected() const;
    void Update(double deltaTime);
    void SendPlayerInput(uint32_t buttons, float thrust, float turn);

    State GetState() const;

private:
    std::unique_ptr<NetworkClient> client;
    GameModel &game;
    State state = State::Offline;

    void HandleSnapshot(const NetworkSnapshot &snapshot);
};
