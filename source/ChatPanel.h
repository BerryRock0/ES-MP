/* ChatPanel.h
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

// ChatPanel.h
//
// The in-game chat overlay for multiplayer. A small, non-fullscreen panel that
// shows the recent chat history from the network session and provides a single
// input line. Enter sends the line, Escape closes the panel. A button in the
// top-right of the box splits and re-joins this client:
//   - while connected it reads "Disconnect" and closes the current client
//     connection: a graceful wire-level Disconnect notice is sent, the socket
//     is closed, and the network game is torn down on this side. The server
//     remains remembered so the button can return to it;
//   - after an unplanned drop (lost or closed connection) it reads "Return to
//     server" and reconnects to the same server using the remembered address,
//     port, nickname, and password, attaching the shared world back onto the
//     ongoing flight.
#pragma once

#include "Panel.h"
#include "Rectangle.h"

#include <string>

class NetworkSession;

class ChatPanel : public Panel {
public:
	explicit ChatPanel(NetworkSession &session);

	virtual void Step() override;
	virtual void Draw() override;

protected:
	virtual bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) override;
	virtual bool Click(int x, int y, MouseButton button, int clicks) override;

private:
	NetworkSession &session;

	std::string input;
	int flickerTime = 0;

	// The clickable area of the Disconnect / Return to server button, updated
	// every Draw.
	Rectangle actionRect;
};
