#include "MultiplayerPanel.h"

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

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <cmath>
#include <utility>

MultiplayerPanel::MultiplayerPanel()
{}

void MultiplayerPanel::Draw()
{
	DrawBackdrop();

	const Sprite *top = SpriteSet::Get(isWide ? "ui/dialog top wide" : "ui/dialog top");
	const Sprite *middle = SpriteSet::Get(isWide ? "ui/dialog middle wide" : "ui/dialog middle");
	const Sprite *bottom = SpriteSet::Get(isWide ? "ui/dialog bottom wide" : "ui/dialog bottom");
	const Sprite *cancel = SpriteSet::Get("ui/dialog cancel");
	const Sprite *thirdButtonSprite = SpriteSet::Get("ui/wide button");

	// Get the position of the top of this dialog, and of the input.
	Point pos(0., (top->Height() + extensionCount * middle->Height() + bottom->Height()) * -.5);
	Point inputPos = Point(0., -(cancel->Height() + INPUT_HEIGHT)) - pos;

	// Draw the top section of the dialog box.
	pos.Y() += top->Height() * .5;
	SpriteShader::Draw(top, pos);
	pos.Y() += top->Height() * .5;

	// The middle section is duplicated depending on how long the text is.
	for(int i = 0; i < extensionCount; ++i)
	{
		pos.Y() += middle->Height() * .5;
		SpriteShader::Draw(middle, pos);
		pos.Y() += middle->Height() * .5;
	}

	// Draw the bottom section.
	const Font &font = FontSet::Get(Preferences::GetFontSize());
	pos.Y() += bottom->Height() * .5;
	SpriteShader::Draw(bottom, pos);
	pos.Y() += (bottom->Height() - cancel->Height()) * .5;

	// Draw the buttons, including optionally the cancel button.
	const Color &bright = *GameData::Colors().Get("bright");
	const Color &dim = *GameData::Colors().Get("medium");
	const Color &back = *GameData::Colors().Get("faint");
	const Color &inactive = *GameData::Colors().Get("inactive");
	okPos = pos + Point((top->Width() - RIGHT_MARGIN - cancel->Width()) * .5, 0.);
	Point labelPos(okPos.X() - .5 * font.Width(okText), okPos.Y() - .5 * font.Height());
	font.Draw(okText, labelPos, isOkDisabled ? inactive : (activeButton == 1 ? bright : dim));
	if(canCancel)
	{
		cancelPos = pos + Point(okPos.X() - cancel->Width() + BUTTON_RIGHT_MARGIN, 0.);
		SpriteShader::Draw(cancel, cancelPos);
		labelPos = {cancelPos.X() - .5 * font.Width(cancelText), cancelPos.Y() - .5 * font.Height()};
		font.Draw(cancelText, labelPos, activeButton == 2 ? bright : dim);

		if(numButtons == 3)
		{
			// Third button, always the left-most button:
			thirdPos = pos + Point(cancelPos.X() - (thirdButtonSprite->Width() + cancel->Width()) / 2 + BUTTON_RIGHT_MARGIN, 0.);
			SpriteShader::Draw(thirdButtonSprite, thirdPos);
			labelPos = {thirdPos.X() - .5 * font.Width(buttonThree.buttonLabel), thirdPos.Y() - .5 * font.Height()};
			font.Draw(buttonThree.buttonLabel, labelPos, activeButton == 3 ? bright : dim);
		}
	}

	// Draw the input, if any.
	if(AcceptsInput())
	{
		FillShader::Fill(inputPos, Point(Width() - HORIZONTAL_PADDING, INPUT_HEIGHT), (flickerTime % 6 > 3) ? dim : back);
		if(flickerTime)
			--flickerTime;

		Point stringPos(inputPos.X() - (Width() - HORIZONTAL_PADDING) * .5 + INPUT_LEFT_PADDING, inputPos.Y() - .5 * font.Height());
		const auto inputText = DisplayText(input, {static_cast<int>(Width() - HORIZONTAL_PADDING - INPUT_HORIZONTAL_PADDING), Truncate::FRONT});
		font.Draw(inputText, stringPos, bright);

		Point barPos(stringPos.X() + font.FormattedWidth(inputText) + INPUT_TOP_PADDING, inputPos.Y());
		FillShader::Fill(barPos, Point(1., INPUT_HEIGHT - INPUT_VERTICAL_PADDING), dim);
	}
}

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

    if(NetworkSession::Connect(address, static_cast<uint16_t>(numericPort)))
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
