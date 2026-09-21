//MultiplayerPanel.cpp
#include "MultiplayerPanel.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include <SDL_keycode.h>

#include "NetworkSession.h"
#include "audio/Audio.h"
#include "text/Clipboard.h"
#include "Color.h"
#include "Command.h"
#include "text/DisplayText.h"
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

namespace
{
	// Map any conceivable numeric keypad keys to their ASCII values. Most of
	// these will presumably only exist on special programming keyboards.
	const std::map<SDL_Keycode, char> KEY_MAP = {
		{SDLK_KP_0, '0'},
		{SDLK_KP_1, '1'},
		{SDLK_KP_2, '2'},
		{SDLK_KP_3, '3'},
		{SDLK_KP_4, '4'},
		{SDLK_KP_5, '5'},
		{SDLK_KP_6, '6'},
		{SDLK_KP_7, '7'},
		{SDLK_KP_8, '8'},
		{SDLK_KP_9, '9'},
		{SDLK_KP_PERIOD, '.'},
		{SDLK_KP_MINUS, '-'},
		{SDLK_KP_PLUS, '+'},
		{SDLK_KP_COLON, ':'},
		{SDLK_KP_DIVIDE, '/'},
		{SDLK_KP_EQUALS, '='},
		{SDLK_KP_SPACE, ' '}
	};

	// The height of the address/port input fields, in pixels.
	constexpr double INPUT_HEIGHT = 20;
	// The width of the port field, in pixels.
	constexpr double PORT_FIELD_WIDTH = 80;
	// The gap between the address and port fields, in pixels.
	constexpr double FIELD_GAP = 10;
}

MultiplayerPanel::~MultiplayerPanel()
{
	Audio::Resume();
}

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

// The multiplayer connection panel. It is a normal dialog whose "OK" button is
// labelled "Connect", plus two text fields for the server address and port.
MultiplayerPanel::MultiplayerPanel(NetworkSession &session)
	: MultiplayerPanel([]() -> MultiplayerInit
		{
			MultiplayerInit init;
			init.message = "Connect to server";
			init.canCancel = true;
			init.activeButton = 1;
			return init;
		}())
{
	networkSession = &session;

	okText = "Connect";
	cancelText = "Cancel";
	numButtons = 2;
	canCancel = true;

	addressFocused = true;
	portFocused = false;

	Resize();
}

MultiplayerPanel::MultiplayerPanel(MultiplayerInit init)
	: voidFun(std::move(init.voidFun)),
	boolFun(std::move(init.boolFun)),
	stringFun(std::move(init.stringFun)),
	validateStringFun(std::move(init.validateStringFun)),
	filterCharFun(std::move(init.filterCharFun)),
	canCancel(init.canCancel),
	activeButton(init.activeButton),
	allowsFastForward(init.allowsFastForward),
	input(std::move(init.initialValue)),
	buttonOne(init.buttonOne),
	buttonThree(init.buttonThree),
	system(init.system)
{
	Audio::Pause();
	SetInterruptible(false);

	isWide = false;
	numButtons = canCancel ? (!buttonThree.buttonLabel.empty() ? 3 : 2) : 1;

	if(buttonOne.buttonLabel.empty())
		okText = "OK";
	else
	{
		okText = buttonOne.buttonLabel;
		stringFun = buttonOne.buttonAction;
	}
	cancelText = "Cancel";

	text = std::make_shared<TextArea>();
	text->SetAlignment(Preferences::GetTextAlignment());
	text->SetFont(FontSet::Get(Preferences::GetFontSize()));
	text->SetTruncate(init.truncate);
	text->SetText(init.message);
	extensionCount = 0;
	AddChild(text);

	isOkDisabled = !ValidateInput();
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

	if(networkSession)
	{
		// Draw the address and port input fields.
		LayoutInputFields();

		auto drawField = [&](const Rectangle &rect, const std::string &value, bool focused)
		{
			FillShader::Fill(rect.Center(), rect.Dimensions(), focused ? dim : back);

			const auto fieldText = DisplayText(value, {static_cast<int>(rect.Width() - 10), Truncate::FRONT});
			Point stringPos(rect.Left() + 5, rect.Center().Y() - .5 * font.Height());
			font.Draw(fieldText, stringPos, bright);

			if(focused)
			{
				Point barPos(stringPos.X() + font.FormattedWidth(fieldText) + 2, rect.Center().Y());
				FillShader::Fill(barPos, Point(1., INPUT_HEIGHT - 4), dim);
			}
		};

		drawField(addressRect, address, addressFocused);
		drawField(portRect, port, portFocused);
	}
	else if(AcceptsInput())
	{
		// Draw the generic single input field.
		FillShader::Fill(inputPos, Point(Width() - 20, INPUT_HEIGHT), (flickerTime % 6 > 3) ? dim : back);

		if(flickerTime)
			--flickerTime;

		Point stringPos(inputPos.X() - (Width() - 20) * .5 + 5, inputPos.Y() - .5 * font.Height());
		const auto inputText = DisplayText(input, {static_cast<int>(Width() - 20 - 10), Truncate::FRONT});
		font.Draw(inputText, stringPos, bright);

		Point barPos(stringPos.X() + font.FormattedWidth(inputText) + 2, inputPos.Y());
		FillShader::Fill(barPos, Point(1., INPUT_HEIGHT - 4), dim);
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
	const Sprite *cancel = SpriteSet::Get("ui/connect cancel");
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

	// Keep the address/port field rectangles in sync with the dialog layout.
	LayoutInputFields();
}

void MultiplayerPanel::LayoutInputFields()
{
	if(!networkSession)
		return;

	const Sprite *top = SpriteSet::Get(isWide ? "ui/dialog top wide" : "ui/dialog top");
	const Sprite *middle = SpriteSet::Get(isWide ? "ui/dialog middle wide" : "ui/dialog middle");
	const Sprite *bottom = SpriteSet::Get(isWide ? "ui/dialog bottom wide" : "ui/dialog bottom");
	const Sprite *cancel = SpriteSet::Get("ui/connect cancel");

	Point pos(0., (top->Height() + extensionCount * middle->Height() + bottom->Height()) * -.5);
	Point inputPos = Point(0., -(cancel->Height() + INPUT_HEIGHT)) - pos;

	const double totalWidth = Width() - 20;
	const double addressWidth = totalWidth - PORT_FIELD_WIDTH - FIELD_GAP;
	const double left = inputPos.X() - totalWidth * .5;

	addressRect = Rectangle(
		Point(left + addressWidth * .5, inputPos.Y()),
		Point(addressWidth, INPUT_HEIGHT));
	portRect = Rectangle(
		Point(left + addressWidth + FIELD_GAP + PORT_FIELD_WIDTH * .5, inputPos.Y()),
		Point(PORT_FIELD_WIDTH, INPUT_HEIGHT));
}

bool MultiplayerPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	// The multiplayer connection panel handles its own keys: text is entered
	// into the address/port fields, tab switches fields, and enter connects.
	if(networkSession)
	{
		if(key == SDLK_BACKSPACE || key == SDLK_DELETE)
		{
			std::string *field = FocusedField();
			if(field && !field->empty())
				field->pop_back();
			return true;
		}
		if(key == SDLK_TAB)
		{
			addressFocused = !addressFocused;
			portFocused = !addressFocused;
			return true;
		}
		if(key == SDLK_RETURN || key == SDLK_KP_ENTER)
		{
			Connect();
			return true;
		}
		if(key == SDLK_ESCAPE)
		{
			GetUI().Pop(this);
			return true;
		}
		// All other keys are ignored; printable text arrives via TextInput().
		return true;
	}

	auto Invalid = [this]() {
		flickerTime = 18;
	};

	auto it = KEY_MAP.find(key);
	bool isCloseRequest = key == SDLK_ESCAPE || (key == 'w' && (mod & (KMOD_CTRL | KMOD_GUI)));
	if(stringFun && Clipboard::KeyDown(input, key, mod))
	{
		// Input handled by Clipboard.
	}
	else if((it != KEY_MAP.end() || (key >= ' ' && key <= '~')) && AcceptsInput() && !isCloseRequest)
	{
		int ascii = (it != KEY_MAP.end()) ? it->second : key;
		char c = ((mod & KMOD_SHIFT) ? SHIFT[ascii] : ascii);
		// Caps lock should shift letters, but not any other keys.
		if((mod & KMOD_CAPS) && c >= 'a' && c <= 'z')
			c += 'A' - 'a';

		if(stringFun)
		{
			if(!filterCharFun || filterCharFun(input, c))
				input += c;
			else
				Invalid();
		}

		isOkDisabled = !ValidateInput();
	}
	else if((key == SDLK_DELETE || key == SDLK_BACKSPACE) && !input.empty())
	{
		input.erase(input.length() - 1);
		isOkDisabled = !ValidateInput();
	}
	else if(key == SDLK_TAB)
		// Round-robin to the right, 3->2->1->3
		activeButton = activeButton == 1 ? numButtons : activeButton - 1;
	else if(key == SDLK_LEFT)
	{
		// To the left, 1->2->3->3
		if(activeButton < numButtons)
			activeButton++;
	}
	else if(key == SDLK_RIGHT)
	{
		// To the right, 3->2->1->1
		if(activeButton > 1)
			activeButton--;
	}
	else if(key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || isCloseRequest
			|| (numButtons == 3 && key == buttonThree.buttonKey))
	{
		if(!canCancel && isCloseRequest)
			activeButton = 1;
		if(canCancel && isCloseRequest)
			activeButton = 2;
		if(key == buttonThree.buttonKey && numButtons == 3)
			activeButton = 3;

		// Now that we know what button was selected, process the button press.
		if(boolFun)
		{
			DoCallback(activeButton == 1);
			GetUI().Pop(this);
		}
		else if(activeButton == 1)
		{
			// If the OK button is disabled (because the input failed the
			// validation), don't execute the callback.
			if(!isOkDisabled)
			{
				DoCallback();
				GetUI().Pop(this);
			}
			else
				Invalid();
		}
		else if(activeButton == 3)
		{
			// Do third button callback. If this returns true, also close the dialog.
			if(buttonThree.buttonAction && buttonThree.buttonAction(input))
				GetUI().Pop(this);
		}
		else
			GetUI().Pop(this);
	}
	else
		return false;

	return true;
}

bool MultiplayerPanel::Click(int x, int y, MouseButton button, int clicks)
{
	if(button != MouseButton::LEFT)
		return false;

	Point clickPos(x, y);

	// Focus the address or port field if it was clicked.
	if(networkSession)
	{
		if(addressRect.Contains(clickPos))
		{
			addressFocused = true;
			portFocused = false;
			return true;
		}
		if(portRect.Contains(clickPos))
		{
			addressFocused = false;
			portFocused = true;
			return true;
		}
	}

	const Sprite *sprite = SpriteSet::Get("ui/dialog cancel");
	double toleranceX = (sprite->Width() - 20) / 2.;
	double toleranceY = (sprite->Height() - 20) / 2.;

	Point ok = clickPos - okPos;
	if(std::fabs(ok.X()) < toleranceX && std::fabs(ok.Y()) < toleranceY)
	{
		activeButton = 1;
		return DoKey(SDLK_RETURN);
	}

	if(canCancel)
	{
		Point cancel = clickPos - cancelPos;
		if(std::fabs(cancel.X()) < toleranceX && std::fabs(cancel.Y()) < toleranceY)
		{
			activeButton = 2;
			return DoKey(SDLK_RETURN);
		}
	}

	if(numButtons == 3)
	{
		const Sprite *sprite3 = SpriteSet::Get("ui/wide button");
		toleranceX = (sprite3->Width() - 20) / 2.;
		toleranceY = (sprite3->Height() - 20) / 2.;

		Point third = clickPos - thirdPos;
		if(std::fabs(third.X()) < toleranceX && std::fabs(third.Y()) < toleranceY)
		{
			activeButton = 3;
			return DoKey(SDLK_RETURN);
		}
	}

	return true;
}

bool MultiplayerPanel::TextInput(const std::string &text)
{
	if(!networkSession)
		return false;

	std::string *field = FocusedField();
	if(!field)
		return false;

	for(unsigned char character : text)
	{
		if(addressFocused)
		{
			// Accept hostname, IPv4, IPv6, and localhost characters.
			if(std::isalnum(character) || character == '.' || character == ':' || character == '-' || character == '_')
				field->push_back(static_cast<char>(character));
		}
		else
		{
			// The port field only accepts digits.
			if(std::isdigit(character))
				field->push_back(static_cast<char>(character));
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

bool MultiplayerPanel::AcceptsInput() const
{
	if(networkSession)
		return true;

	return static_cast<bool>(stringFun);
}

bool MultiplayerPanel::ValidateInput() const
{
	if(validateStringFun)
		return validateStringFun(input);

	return true;
}

std::string *MultiplayerPanel::FocusedField()
{
	if(addressFocused)
		return &address;
	if(portFocused)
		return &port;

	return nullptr;
}

void MultiplayerPanel::Connect()
{
	if(!networkSession)
		return;

	if(address.empty())
	{
		ShowError("Enter a server address.");
		return;
	}

	if(port.empty())
	{
		ShowError("Enter a valid port.");
		return;
	}

	unsigned long parsedPort = 0;

	try
	{
		std::size_t charactersRead = 0;
		parsedPort = std::stoul(port, &charactersRead);

		if(charactersRead != port.size())
			throw std::invalid_argument("port contains non-numeric characters");
	}
	catch(const std::exception &)
	{
		ShowError("Enter a valid port.");
		return;
	}

	if(parsedPort < 1 || parsedPort > std::numeric_limits<uint16_t>::max())
	{
		ShowError("Port must be between 1 and 65535.");
		return;
	}

	connecting = true;
	const bool connected = networkSession->Connect(address, static_cast<uint16_t>(parsedPort));
	connecting = false;

	if(connected)
	{
		// The session is connected; close this dialog so the game can continue.
		GetUI().Pop(this);
	}
	else
		ShowError("Could not connect to the server.");
}

void MultiplayerPanel::ShowError(const std::string &message)
{
	GetUI().Push(MultiplayerPanel::Info(message, Truncate::NONE, false));
}
