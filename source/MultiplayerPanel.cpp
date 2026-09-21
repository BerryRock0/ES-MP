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

MultiplayerPanel *MultiplayerPanel::Info(std::string message, Truncate truncate, bool allowsFastForward)
{
	MultiplayerInit init;
	init.message = std::move(message);
	init.canCancel = false;
	init.truncate = truncate;
	init.allowsFastForward = allowsFastForward;
	return new MultiplayerPanel(init);
}

MultiplayerPanel *MultiplayerPanel::CallFunctionIfOk(std::function<void()> okFunction, std::string message, int activeButton, Truncate truncate, bool allowsFastForward)
{
	MultiplayerInit init;
	init.voidFun = std::move(okFunction);
	init.message = std::move(message);
	init.activeButton = activeButton;
	init.truncate = truncate;
	init.allowsFastForward = allowsFastForward;
	return new MultiplayerPanel(init);
}

void MultiplayerPanel::Draw()
{
	DrawBackdrop();

	const Sprite *top = SpriteSet::Get(isWide ? "ui/dialog top wide" : "ui/dialog top");
	const Sprite *middle = SpriteSet::Get(isWide ? "ui/dialog middle wide" : "ui/dialog middle");
	const Sprite *bottom = SpriteSet::Get(isWide ? "ui/dialog bottom wide" : "ui/dialog bottom");
	const Sprite *cancel = SpriteSet::Get("ui/connect cancel");
	const Sprite *thirdButtonSprite = SpriteSet::Get("ui/wide button");

	// Get the position of the top of this dialog, and of the input.
	Point pos(0., (top->Height() + extensionCount * middle->Height() + bottom->Height()) * -.5);
	Point inputPos = Point(0., -(cancel->Height() + 20)) - pos;
	
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
	okPos = pos + Point((top->Width() - 20 - cancel->Width()) * .5, 0.);
	Point labelPos(okPos.X() - .5 * font.Width(okText), okPos.Y() - .5 * font.Height());
	font.Draw(okText, labelPos, isOkDisabled ? inactive : (activeButton == 1 ? bright : dim));
	if(canCancel)
	{
		cancelPos = pos + Point(okPos.X() - cancel->Width() + 10, 0.);
		SpriteShader::Draw(cancel, cancelPos);
		labelPos = {cancelPos.X() - .5 * font.Width(cancelText), cancelPos.Y() - .5 * font.Height()};
		font.Draw(cancelText, labelPos, activeButton == 2 ? bright : dim);

		if(numButtons == 3)
		{
			// Third button, always the left-most button:
			thirdPos = pos + Point(cancelPos.X() - (thirdButtonSprite->Width() + cancel->Width()) / 2 + 10, 0.);
			SpriteShader::Draw(thirdButtonSprite, thirdPos);
			labelPos = {thirdPos.X() - .5 * font.Width(buttonThree.buttonLabel), thirdPos.Y() - .5 * font.Height()};
			font.Draw(buttonThree.buttonLabel, labelPos, activeButton == 3 ? bright : dim);
		}
	}

	// Draw the input, if any.
	if(AcceptsInput())
	{
		FillShader::Fill(inputPos, Point(Width() - 20, 20), (flickerTime % 6 > 3) ? dim : back);
		
		if(flickerTime)
			--flickerTime;

		Point stringPos(inputPos.X() - (Width() - 20) * .5 + 5, inputPos.Y() - .5 * font.Height());
		const auto inputText = DisplayText(input, {static_cast<int>(Width() - 20 - 10), Truncate::FRONT});
		font.Draw(inputText, stringPos, bright);

		Point barPos(stringPos.X() + font.FormattedWidth(inputText) + 2, inputPos.Y());
		FillShader::Fill(barPos, Point(1., 20 - 4), dim);
	}
}

void MultiplayerPanel::Resize()
{
	isWide = false;
	Point textRectSize(Width() - 20, 0);
	text->SetRect(Rectangle(Point(), textRectSize));
	const Sprite *top = SpriteSet::Get("ui/dialog top");
	// If the dialog is too tall, then switch to wide mode.
	int maxHeight = Screen::Height() * 3 / 4;
	if(text->GetTextHeight(false) > maxHeight)
	{
		textRectSize.Y() = maxHeight;
		isWide = true;
		// Re-wrap with the new width
		textRectSize.X() = Width() - 20;
		text->SetRect(Rectangle(Point{}, textRectSize));

		if(text->GetLongestLineWidth() <= top->Width() - 40 - 20)
		{
			// Formatted text is long and skinny (e.g. scan result dialog). Go back
			// to using the default width, since the wide width doesn't help.
			isWide = false;
			textRectSize.X() = Width() - 20;
			text->SetRect(Rectangle(Point{}, textRectSize));
		}
	}
	else
		textRectSize.Y() = text->GetTextHeight(false);

	top = SpriteSet::Get(isWide ? "ui/dialog top wide" : "ui/dialog top");
	const Sprite *middle = SpriteSet::Get(isWide ? "ui/dialog middle wide" : "ui/dialog middle");
	const Sprite *bottom = SpriteSet::Get(isWide ? "ui/dialog bottom wide" : "ui/dialog bottom");
	const Sprite *cancel = SpriteSet::Get("ui/dialog cancel");
	// The height of the bottom sprite without the included button's height.
	const int realBottomHeight = bottom->Height() - cancel->Height();

	int height = 10 + textRectSize.Y() + 10 + (realBottomHeight - 10) * AcceptsInput();
	// Determine how many extension panels we need.
	if(height <= realBottomHeight + top->Height())
		extensionCount = 0;
	else
		extensionCount = (height - middle->Height()) / middle->Height();

	// Now that we know how big we want to render the text, position the text
	// area and add it to the UI.

	// Get the position of the top of this dialog, and of the text and input.
	Point pos(0., (top->Height() + extensionCount * middle->Height() + bottom->Height()) * -.5f);
	Point textPos(Width() * -.5 + 10, pos.Y() + 20);
	// Resize textRectSize to match the visual height of the dialog, which will
	// be rounded up from the actual text height by the number of panels that
	// were added. This helps correctly position the TextArea scroll buttons.
	textRectSize.Y() = (top->Height() + realBottomHeight - 20) + extensionCount * middle->Height() - (realBottomHeight - 10) * AcceptsInput();

	Rectangle textRect = Rectangle::FromCorner(textPos, textRectSize);
	text->SetRect(textRect);
}

bool MultiplayerPanel::Click(int x, int y, MouseButton button, int clicks)
{
	if(button != MouseButton::LEFT)
		return false;
	Point clickPos(x, y);

	const Sprite *sprite = SpriteSet::Get("ui/dialog cancel");
	double toleranceX = (sprite->Width() - 20) / 2.;
	double toleranceY = (sprite->Height() - 20) / 2.;

	Point ok = clickPos - okPos;
	if(fabs(ok.X()) < toleranceX && fabs(ok.Y()) < toleranceY)
	{
		activeButton = 1;
		return DoKey(SDLK_RETURN);
	}

	if(canCancel)
	{
		Point cancel = clickPos - cancelPos;
		if(fabs(cancel.X()) < toleranceX && fabs(cancel.Y()) < toleranceY)
		{
			activeButton = 2;
			return DoKey(SDLK_RETURN);
		}
	}

	if(numButtons == 3)
	{
		Point cancel = clickPos - thirdPos;
		const Sprite *sprite3 = SpriteSet::Get("ui/wide button");
		toleranceX = (sprite3->Width() - 20) / 2.;
		toleranceY = (sprite3->Height() - 20) / 2.;
		if(fabs(cancel.X()) < toleranceX && fabs(cancel.Y()) < toleranceY)
		{
			activeButton = 3;
			return DoKey(SDLK_RETURN);
		}
	}

	return true;
}

void MultiplayerPanel::DoCallback(const bool isOk) const
{
	if(stringFun)
		stringFun(input);

	if(voidFun)
		voidFun();

	if(boolFun)
		boolFun(isOk);
}

int MultiplayerPanel::Width() const
{
	const Sprite *top = SpriteSet::Get(isWide ? "ui/dialog top wide" : "ui/dialog top");
	return top->Width() - 20;
}

bool MultiplayerPanel::AllowsFastForward() const noexcept
{
	return allowsFastForward;
}

void MultiplayerPanel::UpdateTextDisplay()
{
	text->SetAlignment(Preferences::GetTextAlignment());
	text->SetFont(FontSet::Get(Preferences::GetFontSize()));
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

    if(networkSession.Connect(address, static_cast<uint16_t>(numericPort)))
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

bool MultiplayerPanel::AcceptsInput() const
{
	return true;
}

void MultiplayerPanel::ShowError(const std::string &message)
{}
