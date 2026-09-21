// HostPanel.cpp
#include "HostPanel.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include <SDL_keycode.h>
#include "shift.h"

#include "NetworkProtocol.h"
#include "NetworkServer.h"
#include "NetworkSession.h"
#include "audio/Audio.h"
#include "Color.h"
#include "Command.h"
#include "DialogPanel.h"
#include "text/DisplayText.h"
#include "shader/FillShader.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "GameData.h"
#include "Point.h"
#include "Preferences.h"
#include "Screen.h"
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


	// The height of the input fields, in pixels.
	constexpr double INPUT_HEIGHT = 20;
	// The width of the right-hand field (port / password), in pixels.
	constexpr double PORT_FIELD_WIDTH = 120;
	// The gap between the two columns of fields, in pixels.
	constexpr double FIELD_GAP = 10;
}

HostPanel::HostPanel(NetworkServer &server, NetworkSession &session)
	: server(&server), session(&session)
{
	Audio::Pause();
	SetInterruptible(false);

	// Sensible defaults: the host's nickname is the server name, and the port
	// defaults to the shared LAN port.
	focusedField = Field::ServerName;
	port = std::to_string(NetworkProtocol::DEFAULT_PORT);

	text = std::make_shared<TextArea>();
	text->SetAlignment(Preferences::GetTextAlignment());
	text->SetFont(FontSet::Get(Preferences::GetFontSize()));
	text->SetText("Start LAN World");
	AddChild(text);

	Resize();
}

HostPanel::~HostPanel()
{
	// If the panel is closed while still hosting, tear the server down so we
	// do not leak the listening socket.
	if(hosting)
		StopHosting();

	Audio::Resume();
}

void HostPanel::Step()
{
	if(hosting)
		RefreshStatus();
}

void HostPanel::Draw()
{
	DrawBackdrop();

	const Sprite *top = SpriteSet::Get(isWide ? "ui/dialog top wide" : "ui/dialog top");
	const Sprite *middle = SpriteSet::Get(isWide ? "ui/dialog middle wide" : "ui/dialog middle");
	const Sprite *bottom = SpriteSet::Get(isWide ? "ui/dialog bottom wide" : "ui/dialog bottom");
	const Sprite *cancel = SpriteSet::Get("ui/connect cancel");

	Point pos(0., (top->Height() + extensionCount * middle->Height() + bottom->Height()) * -.5);
	Point inputPos = Point(0., -(cancel->Height() + INPUT_HEIGHT)) - pos;

	pos.Y() += top->Height() * .5;
	SpriteShader::Draw(top, pos);
	pos.Y() += top->Height() * .5;

	for(int i = 0; i < extensionCount; ++i)
	{
		pos.Y() += middle->Height() * .5;
		SpriteShader::Draw(middle, pos);
		pos.Y() += middle->Height() * .5;
	}

	const Font &font = FontSet::Get(Preferences::GetFontSize());
	pos.Y() += bottom->Height() * .5;
	SpriteShader::Draw(bottom, pos);
	pos.Y() += (bottom->Height() - cancel->Height()) * .5;

	const Color &bright = *GameData::Colors().Get("bright");
	const Color &dim = *GameData::Colors().Get("medium");
	const Color &back = *GameData::Colors().Get("faint");

	const std::string okText = hosting ? "Stop" : "Start";
	const std::string cancelText = hosting ? "Close" : "Cancel";

	okPos = pos + Point((top->Width() - 20 - cancel->Width()) * .5, 0.);
	Point labelPos(okPos.X() - .5 * font.Width(okText), okPos.Y() - .5 * font.Height());
	font.Draw(okText, labelPos, activeButton == 1 ? bright : dim);

	cancelPos = pos + Point(okPos.X() - cancel->Width() + 10, 0.);
	SpriteShader::Draw(cancel, cancelPos);
	labelPos = {cancelPos.X() - .5 * font.Width(cancelText), cancelPos.Y() - .5 * font.Height()};
	font.Draw(cancelText, labelPos, activeButton == 2 ? bright : dim);

	if(hosting)
	{
		// Show the live status (player count and names) in place of the fields.
		const auto statusText = DisplayText(status, {static_cast<int>(Width() - 20), Truncate::NONE});
		Point stringPos(inputPos.X() - (Width() - 20) * .5 + 5, inputPos.Y() - .5 * font.Height());
		font.Draw(statusText, stringPos, bright);
		return;
	}

	// Draw the server name, port, and password input fields.
	LayoutInputFields();

	auto drawField = [&](const Rectangle &rect, const std::string &value,
			const std::string &placeholder, bool focused, bool masked)
	{
		FillShader::Fill(rect.Center(), rect.Dimensions(), focused ? dim : back);

		const bool empty = value.empty();
		const std::string shown = empty ? placeholder : (masked ? std::string(value.size(), '*') : value);
		const auto fieldText = DisplayText(shown, {static_cast<int>(rect.Width() - 10), Truncate::FRONT});
		Point stringPos(rect.Left() + 5, rect.Center().Y() - .5 * font.Height());
		font.Draw(fieldText, stringPos, empty ? dim : bright);

		if(focused)
		{
			const double caretX = empty ? 2. : font.FormattedWidth(fieldText) + 2;
			Point barPos(stringPos.X() + caretX, rect.Center().Y());
			FillShader::Fill(barPos, Point(1., INPUT_HEIGHT - 4), dim);
		}
	};

	drawField(nameRect, serverName, "Server name", focusedField == Field::ServerName, false);
	drawField(portRect, port, "Port", focusedField == Field::Port, false);
	drawField(passwordRect, password, "Password (optional)", focusedField == Field::Password, true);
}

void HostPanel::Resize()
{
	isWide = false;
	Point textRectSize(Width() - 20, 0);
	text->SetRect(Rectangle(Point(), textRectSize));
	const Sprite *top = SpriteSet::Get("ui/dialog top");
	int maxHeight = Screen::Height() * 3 / 4;
	if(text->GetTextHeight(false) > maxHeight)
	{
		textRectSize.Y() = maxHeight;
		isWide = true;
		textRectSize.X() = Width() - 20;
		text->SetRect(Rectangle(Point{}, textRectSize));
	}
	else
		textRectSize.Y() = text->GetTextHeight(false);

	top = SpriteSet::Get(isWide ? "ui/dialog top wide" : "ui/dialog top");
	const Sprite *middle = SpriteSet::Get(isWide ? "ui/dialog middle wide" : "ui/dialog middle");
	const Sprite *bottom = SpriteSet::Get(isWide ? "ui/dialog bottom wide" : "ui/dialog bottom");
	const Sprite *cancel = SpriteSet::Get("ui/connect cancel");
	const int realBottomHeight = bottom->Height() - cancel->Height();

	int height = 10 + textRectSize.Y() + 10 + (realBottomHeight - 10);
	// Reserve room for the two rows of input fields.
	height += INPUT_HEIGHT + FIELD_GAP;
	if(height <= realBottomHeight + top->Height())
		extensionCount = 0;
	else
		extensionCount = (height - middle->Height()) / middle->Height();

	Point pos(0., (top->Height() + extensionCount * middle->Height() + bottom->Height()) * -.5f);
	Point textPos(Width() * -.5 + 10, pos.Y() + 20);
	textRectSize.Y() = (top->Height() + realBottomHeight - 20) + extensionCount * middle->Height() - (realBottomHeight - 10);
	Rectangle textRect = Rectangle::FromCorner(textPos, textRectSize);
	text->SetRect(textRect);

	LayoutInputFields();
}

void HostPanel::LayoutInputFields()
{
	const Sprite *top = SpriteSet::Get(isWide ? "ui/dialog top wide" : "ui/dialog top");
	const Sprite *middle = SpriteSet::Get(isWide ? "ui/dialog middle wide" : "ui/dialog middle");
	const Sprite *bottom = SpriteSet::Get(isWide ? "ui/dialog bottom wide" : "ui/dialog bottom");
	const Sprite *cancel = SpriteSet::Get("ui/connect cancel");

	Point pos(0., (top->Height() + extensionCount * middle->Height() + bottom->Height()) * -.5);
	Point inputPos = Point(0., -(cancel->Height() + INPUT_HEIGHT)) - pos;

	const double totalWidth = Width() - 20;
	const double nameWidth = totalWidth - PORT_FIELD_WIDTH - FIELD_GAP;
	const double left = inputPos.X() - totalWidth * .5;

	const double topRowY = inputPos.Y() - (INPUT_HEIGHT + FIELD_GAP);
	const double bottomRowY = inputPos.Y();

	nameRect = Rectangle(
		Point(left + nameWidth * .5, topRowY),
		Point(nameWidth, INPUT_HEIGHT));
	portRect = Rectangle(
		Point(left + nameWidth + FIELD_GAP + PORT_FIELD_WIDTH * .5, topRowY),
		Point(PORT_FIELD_WIDTH, INPUT_HEIGHT));
	passwordRect = Rectangle(
		Point(left + totalWidth * .5, bottomRowY),
		Point(totalWidth, INPUT_HEIGHT));
}

bool HostPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	if(!hosting)
	{
		std::string *field = FocusedField();
		// SDL text input is not active for this panel, so printable characters
		// must be read from the key events themselves rather than from TextInput().
		auto it = KEY_MAP.find(key);
		if(field && (it != KEY_MAP.end() || (key >= ' ' && key <= '~')))
		{
			int ascii = (it != KEY_MAP.end()) ? it->second : key;
			char c = ((mod & KMOD_SHIFT) ? SHIFT[ascii] : ascii);
			// Caps lock should shift letters, but not any other keys.
			if((mod & KMOD_CAPS) && c >= 'a' && c <= 'z')
				c += 'A' - 'a';

			bool valid = false;
			switch(focusedField)
			{
				case Field::ServerName:
					valid = (std::isalnum(c) || c == '_' || c == '-')
						&& field->size() < NetworkProtocol::MAX_NAME_LENGTH - 1;
					break;
				case Field::Port:
					valid = std::isdigit(c);
					break;
				case Field::Password:
					valid = (c >= ' ' && c <= '~')
						&& field->size() < NetworkProtocol::MAX_PASSWORD_LENGTH - 1;
					break;
			}
			if(valid)
				field->push_back(c);
			return true;
		}

		if(key == SDLK_BACKSPACE || key == SDLK_DELETE)
		{
			if(field && !field->empty())
				field->pop_back();
			return true;
		}
		if(key == SDLK_TAB)
		{
			switch(focusedField)
			{
				case Field::ServerName: focusedField = Field::Port; break;
				case Field::Port: focusedField = Field::Password; break;
				case Field::Password: focusedField = Field::ServerName; break;
			}
			return true;
		}
	}

	if(key == SDLK_RETURN || key == SDLK_KP_ENTER)
	{
		if(activeButton == 1)
		{
			if(hosting)
				StopHosting();
			else
				StartHosting();
		}
		else
			GetUI().Pop(this);
		return true;
	}
	if(key == SDLK_ESCAPE)
	{
		GetUI().Pop(this);
		return true;
	}
	if(key == SDLK_TAB)
	{
		activeButton = activeButton == 1 ? 2 : 1;
		return true;
	}
	if(key == SDLK_LEFT || key == SDLK_RIGHT)
	{
		activeButton = activeButton == 1 ? 2 : 1;
		return true;
	}

	return true;
}

bool HostPanel::Click(int x, int y, MouseButton button, int clicks)
{
	if(button != MouseButton::LEFT)
		return false;

	Point clickPos(x, y);

	if(!hosting)
	{
		if(nameRect.Contains(clickPos))
		{
			focusedField = Field::ServerName;
			return true;
		}
		if(portRect.Contains(clickPos))
		{
			focusedField = Field::Port;
			return true;
		}
		if(passwordRect.Contains(clickPos))
		{
			focusedField = Field::Password;
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

	Point cancel = clickPos - cancelPos;
	if(std::fabs(cancel.X()) < toleranceX && std::fabs(cancel.Y()) < toleranceY)
	{
		activeButton = 2;
		return DoKey(SDLK_RETURN);
	}

	return true;
}

bool HostPanel::TextInput(const std::string &text)
{
	if(hosting)
		return false;

	std::string *field = FocusedField();
	if(!field)
		return false;

	for(unsigned char character : text)
	{
		switch(focusedField)
		{
			case Field::ServerName:
				if((std::isalnum(character) || character == '_' || character == '-')
					&& field->size() < NetworkProtocol::MAX_NAME_LENGTH - 1)
					field->push_back(static_cast<char>(character));
				break;
			case Field::Port:
				if(std::isdigit(character))
					field->push_back(static_cast<char>(character));
				break;
			case Field::Password:
				if(character >= ' ' && character <= '~'
					&& field->size() < NetworkProtocol::MAX_PASSWORD_LENGTH - 1)
					field->push_back(static_cast<char>(character));
				break;
		}
	}

	return true;
}

int HostPanel::Width() const
{
	const Sprite *top = SpriteSet::Get(isWide ? "ui/dialog top wide" : "ui/dialog top");
	return top->Width() - 20;
}

std::string *HostPanel::FocusedField()
{
	switch(focusedField)
	{
		case Field::ServerName: return &serverName;
		case Field::Port: return &port;
		case Field::Password: return &password;
	}
	return nullptr;
}

void HostPanel::StartHosting()
{
	if(!server || !session)
		return;

	if(serverName.empty())
	{
		ShowError("Enter a server name.");
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

	if(!server->Start(static_cast<uint16_t>(parsedPort), password, serverName))
	{
		ShowError("Could not start the server (is the port already in use?).");
		return;
	}

	// Connect the local player to our own server over the loopback interface.
	// The host is just another client as far as the server is concerned, so
	// the same nickname/password validation applies to them too.
	if(!session->Connect("127.0.0.1", static_cast<uint16_t>(parsedPort), serverName, password))
	{
		server->Stop();
		ShowError("Could not connect to the local server.");
		return;
	}
	
	hosting = true;
	activeButton = 1;
	status.clear();
	text->SetText("Hosting LAN world");
	RefreshStatus();
	Resize();
}

void HostPanel::StopHosting()
{
	if(session)
		session->Disconnect();
	if(server)
		server->Stop();

	hosting = false;
	activeButton = 1;
	status.clear();
	text->SetText("Start LAN World");
	Resize();
}

void HostPanel::RefreshStatus()
{
	if(!server)
		return;

	const auto players = server->Players();
	std::string line = "Players (" + std::to_string(players.size()) + "):";
	if(players.empty())
		line += " waiting for players to join...";
	else
	{
		line += " ";
		for(std::size_t i = 0; i < players.size(); ++i)
		{
			if(i)
				line += ", ";
			line += players[i].nickname;
		}
	}

	line += "\nPort: " + std::to_string(server->Port());
	line += "\nShare your address, the port, and the password with other players.";
	status = line;
}

void HostPanel::ShowError(const std::string &message)
{
	GetUI().Push(DialogPanel::Info(message));
}
