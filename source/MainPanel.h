/* MainPanel.h
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "Panel.h"

#include "Command.h"
#include "Engine.h"

#include <list>

class GameModel;
class NetworkSession;
class PlayerInfo;
class ShipEvent;



// Class representing the main panel (i.e. the view of your ship moving around).
// The goal is that the Engine class will not need to know about displaying
// panels or handling key presses; it instead focuses just on the calculations
// needed to move the ships around and to figure out where they should be drawn.
class MainPanel : public Panel {
public:
	explicit MainPanel(PlayerInfo &player, GameModel *game = nullptr, NetworkSession *session = nullptr);

	virtual void Step() override;
	virtual void Draw() override;

	// The planet panel calls this when it closes.
	void OnCallback();
	// The hail panel calls this when it closes.
	void OnBribeCallback(const Government *bribed);

	// The main panel allows fast-forward.
	bool AllowsFastForward() const noexcept final;

	// Get the underlying game engine used by the game.
	Engine &GetEngine();
	// Attach (or detach, with nullptr) the shared multiplayer world.
	void SetGameModel(GameModel *sharedWorld);
	// Attach (or detach, with nullptr) the active network session, enabling the
	// in-game chat panel.
	void SetSession(NetworkSession *session);
	// Mark this flight as the main menu's backdrop (the ship shown behind the
	// menu), which is not the player's active game.
	void SetIsMenuBackdrop(bool isMenuBackdrop);
	// Whether this is the main menu's backdrop flight.
	bool IsMenuBackdrop() const noexcept;


protected:
	// Only override the ones you need; the default action is to return false.
	virtual bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) override;
	virtual bool Click(int x, int y, MouseButton button, int clicks) override;
	virtual bool Drag(double dx, double dy) override;
	virtual bool Release(int x, int y, MouseButton button) override;
	virtual bool Scroll(double dx, double dy) override;


private:
	void ShowScanDialog(const ShipEvent &event);
	bool ShowHailPanel();
	bool ShowHelp(bool force);
	void StepEvents(bool &isActive);
	void DrawNetworkPlayers();
	void DrawChatButton();


private:
	PlayerInfo &player;

	Engine engine;
	// True if this panel is the main menu's backdrop flight (not a real game).
	bool isMenuBackdrop = false;
	// The shared multiplayer world, if the player has connected to a server.
	GameModel *game = nullptr;
	// The active network session, if the player is playing multiplayer. Lets
	// the main panel open the in-game chat panel.
	NetworkSession *session = nullptr;
	// The clickable area of the "Chat" button in the bottom-right corner of
	// the screen, updated every Draw. Only meaningful while a session is set.
	Rectangle chatButtonRect;

	// These are the pending ShipEvents that have yet to be processed.
	std::list<ShipEvent> eventQueue;
	bool handledFront = false;

	Command show;

	// Keep track of how long a starting player has spent drifting in deep space.
	int lostness = 0;
	int lostCount = 0;

	Point dragSource;
	Point dragPoint;
	bool isDragging = false;
	bool hasShift = false;
	bool hasControl = false;
	bool canClick = false;
	bool canDrag = false;
};
