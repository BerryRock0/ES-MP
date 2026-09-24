/* ChatPanel.cpp
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

#include "ChatPanel.h"

#include <cstddef>
#include <map>

#include <SDL_keycode.h>

#include "Color.h"
#include "Command.h"
#include "shader/FillShader.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "GameData.h"
#include "MainPanel.h"
#include "Messages.h"
#include "NetworkProtocol.h"
#include "NetworkSession.h"
#include "Point.h"
#include "Screen.h"
#include "shift.h"
#include "UI.h"

namespace
{
	// Map any conceivable numeric keypad keys to their ASCII values.
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

	constexpr int MAX_LINES = 6;
	constexpr double BOX_MARGIN = 16.;
	constexpr double BOX_WIDTH = 640.;
	constexpr double BOX_PADDING = 6.;
}



ChatPanel::ChatPanel(NetworkSession &session)
	: session(session)
{
	SetIsFullScreen(false);
	SetTrapAllEvents(true);
	SetInterruptible(false);
}



void ChatPanel::Step()
{
	if(flickerTime)
		--flickerTime;
}



void ChatPanel::Draw()
{
	const Font &font = FontSet::Get(14);
	const Color &bright = *GameData::Colors().Get("bright");
	const Color &dim = *GameData::Colors().Get("medium");
	const Color &back = *GameData::Colors().Get("faint");

	const double lineHeight = font.Height() + 2.;
	// One row for the button, MAX_LINES for the chat history, and one row for
	// the input line.
	const double boxHeight = (MAX_LINES + 2) * lineHeight + 2. * BOX_PADDING;
	const Point boxCenter(Screen::Width() * .5 - BOX_MARGIN - BOX_WIDTH * .5,
		Screen::Height() - BOX_MARGIN - boxHeight * .5);
	FillShader::Fill(boxCenter, Point(BOX_WIDTH, boxHeight), back);

	// The button, top-right inside the box. It only exists when there is
	// something to do: "Disconnect" while connected to a server, or "Return
	// to server" after an unplanned drop. A deliberate disconnect is final
	// (the remembered server is gone), so no reconnect button lingers after
	// the player chose to leave.
	if(session.IsLoggedIn() || session.HasLastServer())
	{
		const std::string label = session.IsLoggedIn() ? "Disconnect" : "Return to server";
		const double buttonWidth = font.Width(label) + 2. * BOX_PADDING;
		const double buttonHeight = lineHeight;
		const Point buttonCenter(boxCenter.X() + BOX_WIDTH * .5 - BOX_PADDING - buttonWidth * .5,
			boxCenter.Y() - boxHeight * .5 + BOX_PADDING + buttonHeight * .5);
		actionRect = Rectangle(buttonCenter, Point(buttonWidth, buttonHeight));
		FillShader::Fill(buttonCenter, Point(buttonWidth, buttonHeight), dim);
		font.Draw(label, buttonCenter - Point(font.Width(label) * .5, font.Height() * .5), bright);
	}
	else
		actionRect = Rectangle();

	// Draw the most recent chat lines, oldest first, below the button row.
	const auto &log = session.ChatLog();
	const std::size_t start = (log.size() > MAX_LINES) ? log.size() - MAX_LINES : 0;
	Point linePos(boxCenter.X() - BOX_WIDTH * .5 + BOX_PADDING,
		boxCenter.Y() - boxHeight * .5 + BOX_PADDING + lineHeight);
	for(std::size_t i = start; i < log.size(); ++i)
	{
		const auto &entry = log[i];
		font.Draw(entry.sender + ": " + entry.text, linePos, dim);
		linePos.Y() += lineHeight;
	}

	// The input line, with a blinking caret.
	std::string shown = "> " + input;
	const bool caretVisible = (flickerTime == 0 || (flickerTime % 60) < 30);
	if(caretVisible)
		shown += "_";
	font.Draw(shown, linePos, bright);
}



bool ChatPanel::Click(int x, int y, MouseButton button, int clicks)
{
	if(button != MouseButton::LEFT)
		return true;

	// The button in the top-right of the box is contextual:
	//  * "Disconnect" while connected closes the current client connection:
	//    it sends a graceful wire-level Disconnect notice (which makes the
	//    server drop the player), closes the socket, and clears the network
	//    game on this side. The server remains remembered for a later return.
	//  * "Return to server" after a disconnect or an unplanned drop reconnects
	//    to the same server with the remembered
	//    address, port, nickname, and password, and reattaches the shared
	//    world to the ongoing flight so remote ships reappear once the login
	//    succeeds.
	if(actionRect.Contains(Point(x, y)))
	{
		if(session.IsLoggedIn())
		{
			session.Disconnect();
			// The client is now back in single-player mode. Re-enable the
			// normal save path for a loaded local pilot; a network-created pilot
			// still has no save path and remains protected by PlayerInfo.
			GetUI().CanSave(true);
			Messages::Add({"Disconnected from the server.", GameData::MessageCategories().Get("info")});
		}
		else if(session.HasLastServer())
		{
			Messages::Add({"Returning to " + session.LastAddress() + ":"
				+ std::to_string(session.LastPort()) + "...", GameData::MessageCategories().Get("info")});
			if(session.Reconnect())
			{
				// Attach the shared world to this client's active flight (the
				// same attach the connect panel performs), so the session's
				// game model drives the remote ships once it is populated.
				MainPanel *active = static_cast<MainPanel *>(GetUI().Root().get());
				if(active && !active->IsMenuBackdrop())
				{
					active->SetGameModel(&session.Game());
					active->SetSession(&session);
				}
			}
			else
				Messages::Add({"Could not reconnect to the server.", GameData::MessageCategories().Get("info")});
		}
		GetUI().Pop(this);
		return true;
	}

	// The chat overlay traps all events, so nothing below the panel receives
	// the click either way.
	return true;
}



bool ChatPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool /* isNewPress */)
{
	auto it = KEY_MAP.find(key);
	if(it != KEY_MAP.end() || (key >= ' ' && key <= '~'))
	{
		int ascii = (it != KEY_MAP.end()) ? it->second : key;
		char c = ((mod & KMOD_SHIFT) ? SHIFT[ascii] : ascii);
		// Caps lock should shift letters, but not any other keys.
		if((mod & KMOD_CAPS) && c >= 'a' && c <= 'z')
			c += 'A' - 'a';

		if(c >= ' ' && c <= '~' && input.size() < NetworkProtocol::MAX_CHAT_LENGTH - 1)
		{
			input += c;
			flickerTime = 60;
		}
		return true;
	}

	if(key == SDLK_BACKSPACE || key == SDLK_DELETE)
	{
		if(!input.empty())
		{
			input.pop_back();
			flickerTime = 60;
		}
		return true;
	}

	if(key == SDLK_RETURN || key == SDLK_KP_ENTER)
	{
		if(!input.empty())
		{
			const std::string text = input;
			input.clear();
			// SendChat records the line locally (and surfaces it in the HUD
			// log), so the writer sees their own message like everyone else.
			session.SendChat(text);
		}
		GetUI().Pop(this);
		return true;
	}

	if(key == SDLK_ESCAPE)
	{
		GetUI().Pop(this);
		return true;
	}

	return false;
}
