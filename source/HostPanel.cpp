/* HostPanel.cpp
Copyright (c) 2026 by BerryRock0

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "HostPanel.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include <SDL_keycode.h>

#include "audio/Audio.h"
#include "Color.h"
#include "Command.h"
#include "DialogPanel.h"
#include "text/DisplayText.h"
#include "Files.h"
#include "shader/FillShader.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "GameData.h"
#include "Logger.h"
#include "MainPanel.h"
#include "NetworkProtocol.h"
#include "NetworkServer.h"
#include "NetworkSession.h"
#include "Planet.h"
#include "PlayerInfo.h"
#include "Point.h"
#include "Preferences.h"
#include "Screen.h"
#include "shift.h"
#include "image/Sprite.h"
#include "image/SpriteSet.h"
#include "shader/SpriteShader.h"
#include "StartConditions.h"
#include "System.h"
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

	// The in-process host is still a client executable, so keep its server
	// data in a dedicated subdirectory instead of allowing the normal player
	// save path (config/saves) to stand in for the shared world.
	std::filesystem::path ServerDataDirectory()
	{
		return Files::Config() / "server";
	}
}

HostPanel::HostPanel(PlayerInfo &player, UI &gamePanels, NetworkServer &server, NetworkSession &session)
	: player(&player), gamePanels(&gamePanels), server(&server), session(&session)
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
	// A host panel can also be opened to inspect/stop a server that was started
	// by an earlier panel. In that case the server already owns its lifetime and
	// closing this dialog must not tear it down.
	if(this->server && this->server->IsRunning())
	{
		hosting = true;
		port = std::to_string(this->server->Port());
		text->SetText("Hosting LAN World");
	}
	else
		text->SetText("Start LAN World");
	AddChild(text);

	Resize();
}

HostPanel::~HostPanel()
{
	// A successful start transfers ownership of the server to the game loop. Do
	// not stop it just because the setup panel is removed. Panels opened later
	// to inspect that server do not own it either; only an untransferred, failed
	// start needs cleanup here.
	if(hosting && ownsServer && !leaveServerRunning)
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
	const Sprite *cancel = SpriteSet::Get("ui/dialog cancel");

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
	const Sprite *cancel = SpriteSet::Get("ui/dialog cancel");
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
	textRectSize.Y() = (top->Height() + realBottomHeight - 20) + extensionCount * middle->Height()
		- ((realBottomHeight - 10) + (INPUT_HEIGHT + FIELD_GAP) * !hosting);
	Rectangle textRect = Rectangle::FromCorner(textPos, textRectSize);
	text->SetRect(textRect);

	LayoutInputFields();
}

void HostPanel::LayoutInputFields()
{
	const Sprite *top = SpriteSet::Get(isWide ? "ui/dialog top wide" : "ui/dialog top");
	const Sprite *middle = SpriteSet::Get(isWide ? "ui/dialog middle wide" : "ui/dialog middle");
	const Sprite *bottom = SpriteSet::Get(isWide ? "ui/dialog bottom wide" : "ui/dialog bottom");
	const Sprite *cancel = SpriteSet::Get("ui/dialog cancel");

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
				// LAN play is trusted; accept any printable character.
				valid = (c >= ' ' && c <= '~')
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
				case Field::ServerName:
					focusedField = Field::Port;
					break;
				case Field::Port:
					focusedField = Field::Password;
					break;
				case Field::Password:
					focusedField = Field::ServerName;
					break;
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
				// LAN play is trusted; accept any printable character.
				if(character >= ' ' && character <= '\177'
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
	try {
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

	const bool couldSave = gamePanels && gamePanels->CanSave();

	// The host executable is also a game client, so give the in-process server
	// its own storage root. It must not fall back to config/saves or
	// config/pilots: those directories belong exclusively to the local game.
	const std::filesystem::path serverDataDirectory = ServerDataDirectory();
	server->SetWorldSaveDirectory(serverDataDirectory.string());
	server->SetPluginsDirectory((serverDataDirectory / "plugins").string());
	server->LoadPlugins();
	const std::string worldLoadError = server->LoadWorld();
	if(!worldLoadError.empty())
		Logger::Log("Could not load the host server's saved world: " + worldLoadError,
			Logger::Level::WARNING);

	if(!server->Start(static_cast<uint16_t>(parsedPort), password, serverName))
	{
		server->SetWorldSaveDirectory("");
		ShowError("Could not start the server (is the port already in use?).");
		return;
	}
	ownsServer = true;
	if(gamePanels)
		gamePanels->CanSave(false);

	// The server owns the shared world, not any client's save file. Restore its
	// own world when one exists; otherwise use the first available start
	// scenario only as a one-time fallback. The spawn point defaults to the
	// origin of that start system (this fork does not expose the planet's
	// position); the server spreads new players around it until they report
	// their own authoritative positions.
	if(!server->HasWorld())
	{
		NetworkProtocol::WorldInfo world;
		const auto &options = GameData::StartOptions();
		for(auto it = options.begin(); it != options.end(); ++it)
			if(it->Visible())
			{
				NetworkProtocol::WriteFixedString(world.system, sizeof(world.system),
					it->GetSystem().TrueName());
				NetworkProtocol::WriteFixedString(world.planet, sizeof(world.planet),
					it->GetPlanet().TrueName());
				const Date &startDate = it->GetDate();
				world.day = startDate.Day();
				world.month = startDate.Month();
				world.year = startDate.Year();
				break;
			}
		server->SetWorld(world);
	}
	const NetworkProtocol::WorldInfo &activeWorld = server->World();

	// Connect the local player to our own server over the loopback interface.
	// The host is just another client as far as the server is concerned, so
	// the same nickname/password validation applies to them too.
	if(!session->Connect("127.0.0.1", static_cast<uint16_t>(parsedPort), serverName, password))
	{
		server->Stop();
		ownsServer = false;
		if(gamePanels)
			gamePanels->CanSave(couldSave);
		ShowError("Could not connect to the local server.");
		return;
	}

	// The host enters the server's own world just like everyone else: with an
	// in-memory network pilot built from that world (and never written to
	// disk), so the shared world never depends on the host's save file. If the
	// world already holds a stored pilot save for the host's nickname, resume
	// that progress synchronously -- the loopback login is asynchronous, and
	// the game must begin immediately.
	bool entered = false;
	const std::string storedPilot = server->PilotSaveFor(serverName);
	if(!storedPilot.empty())
		entered = player->LoadNetworkPilot(storedPilot);
	if(!entered)
		entered = player->StartNetworkGame(activeWorld);
	if(!entered)
	{
		server->Stop();
		session->Disconnect();
		ownsServer = false;
		if(gamePanels)
			gamePanels->CanSave(couldSave);
		ShowError("Could not start a network game (no start scenario available).");
		return;
	}

	// Remember which nickname this flight was created for, so the loopback
	// login (which completes asynchronously) recognizes the host's own flight
	// and does not replace it with a second pilot.
	player->SetNetworkNickname(serverName);

	hosting = true;
	activeButton = 1;

	// Once the local loopback client is accepted (it happens during
	// StartHosting above), enter the game as the host of the shared world. The
	// fresh network pilot gets its own MainPanel; any earlier flight (for
	// example a loaded single-player pilot) is replaced in memory only.
	if(session->IsConnected() && player && gamePanels)
	{
		gamePanels->Reset();
		gamePanels->Push(new MainPanel(*player, &session->Game(), session));
		// It takes one step to figure out the planet panel should be
		// created, and another step to actually place it.
		gamePanels->StepAll();
		gamePanels->StepAll();
		// The server now belongs to the game loop, not this setup panel. Clear
		// the complete menu stack, including panels queued during this frame,
		// so the start/setup screen cannot reappear over the running game.
		leaveServerRunning = true;
		GetUI().Reset();
		return;
	}
}

void HostPanel::StopHosting()
{
	if(session)
		session->ForgetServer();
	if(server)
		server->Stop();

	hosting = false;
	ownsServer = false;
	leaveServerRunning = false;
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
