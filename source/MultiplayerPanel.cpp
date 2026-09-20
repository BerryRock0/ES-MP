#include "MultiplayerPanel.h"

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <cmath>
#include <utility>

#include "NetworkSession.h"
#include "audio/Audio.h"
#include "text/Clipboard.h"
#include "Color.h"
#include "Command.h"
#include "text/DisplayText.h"
#include "Endpoint.h"
#include "shader/FillShader.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "GameData.h"
#include "Point.h"
#include "Preferences.h"
#include "Screen.h"
#include "shift.h"
#include "image/Sprite.h"
#include "image/SpriteSet.h"
#include "shader/SpriteShader.h"
#include "TextArea.h"
#include "UI.h"

MultiplayerPanel::MultiplayerPanel(NetworkSession& session) : networkSession(session)
{}

void MultiplayerPanel::Draw()
{}

void MultiplayerPanel::HandleTextInput(const std::string &text)
{
    std::string *field = nullptr;

    if(addressFocused)
        field = &address;
    else if(portFocused)
        field = &port;

    if(!field)
        return;

    for(char character : text)
    {
        if(addressFocused)
        {
            // Accept hostname, IPv4, IPv6, and localhost characters.
            if(std::isalnum(static_cast<unsigned char>(character)) || character == '.' || character == ':' || character == '-' || character == '_')
            {
                field->push_back(character);
            }
        }
        else
        {
            if(std::isdigit(static_cast<unsigned char>(character)))
            {
                field->push_back(character);
            }
        }
    }
}

void MultiplayerPanel::HandleKey(int key)
{
    // Replace these key values with the project's key constants.

    if(key == /* Backspace */ 8)
    {
        std::string *field = nullptr;

        if(addressFocused)
            field = &address;
        else if(portFocused)
            field = &port;

        if(field && !field->empty())
            field->pop_back();
    }

    if(key == /* Tab */ 9)
    {
        if(addressFocused)
        {
            addressFocused = false;
            portFocused = true;
        }
        else
        {
            addressFocused = true;
            portFocused = false;
        }
    }

    if(key == /* Enter */ 13)
        Connect();
}

void MultiplayerPanel::HandleClick(int x, int y)
{
    // Replace these bounds with the actual panel coordinates.

    if(y >= 220 && y <= 280)
    {
        addressFocused = true;
        portFocused = false;
    }
    else if(y >= 290 && y <= 350)
    {
        addressFocused = false;
        portFocused = true;
    }
    else if(y >= 390 && y <= 460)
    {
        Connect();
    }
}

void MultiplayerPanel::Connect()
{
    if(address.empty())
    {
        ShowError("Enter a server address.");
        return;
    }

    int numericPort = 0;

    try
    {
        numericPort = std::stoi(port);
    }
    catch(const std::exception &)
    {
        ShowError("Enter a valid port.");
        return;
    }

    if(numericPort < 1 || numericPort > 65535)
    {
        ShowError("Port must be between 1 and 65535.");
        return;
    }

    connecting = true;

    if(networkSession::Connect(address, static_cast<uint16_t>(numericPort)))
    {
        connecting = false;

        // Replace this with the game's actual panel transition.
        // GameWindow::SetPanel("multiplayer-game");
    }
    else
    {
        connecting = false;
        ShowError("Could not connect to the server.");
    }
}

void MultiplayerPanel::ShowError(const std::string &message)
{}
