#pragma once

#include <string>

class NetworkSession;

class MultiplayerPanel
{
public:
    MultiplayerPanel(NetworkSession& session);

    void Draw();
    void HandleKey(int key);
    void HandleTextInput(const std::string &text);
    void HandleClick(int x, int y);

private:
	void ShowError(const std::string& message);
	NetworkSession& networkSession;

    std::string address = "127.0.0.1";
    std::string port = "4242";

    bool addressFocused = true;
    bool portFocused = false;
    bool connecting = false;

    void Connect();
    void ShowError(const std::string &message);
};
