/* MenuPanel.cpp
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

#include "MenuPanel.h"

#include "audio/Audio.h"
#include "Command.h"
#include "Files.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "text/Format.h"
#include "GameData.h"
#include "GamerulesPanel.h"
#include "HostPanel.h"
#include "Information.h"
#include "Interface.h"
#include "LoadPanel.h"
#include "Logger.h"
#include "MainPanel.h"
#include "Messages.h"
#include "MultiplayerPanel.h"
#include "NetworkServer.h"
#include "NetworkSession.h"
#include "pi.h"
#include "PilotProfile.h"
#include "Planet.h"
#include "PlayerInfo.h"
#include "Point.h"
#include "PreferencesPanel.h"
#include "Ship.h"
#include "image/Sprite.h"
#include "shader/StarField.h"
#include "StartConditionsPanel.h"
#include "System.h"
#include "UI.h"

#include "opengl.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

using namespace std;

namespace
{
	const int SCROLL_MOD = 2;
	int scrollSpeed = 1;
	bool showCreditsWarning = true;
	// A clean, worldless player used only for the main menu's backdrop flight.
	// When the player leaves a multiplayer session the real pilot stays loaded
	// (so the menu keeps showing name, credits, and "Save World Locally"), but
	// the visible flight must not keep rendering the departed server's world.
	// The backdrop never needs anything but an empty starfield, and nothing
	// ever writes to this player.
	PlayerInfo menuBackdropPlayer;
}



MenuPanel::MenuPanel(PlayerInfo &player, UI &gamePanels, NetworkSession &session, NetworkServer &server)
	: player(player), gamePanels(gamePanels), session(session), server(server),
	mainMenuUi(GameData::Interfaces().Get("main menu"))
{
	assert(GameData::IsLoaded() && "MenuPanel should only be created after all data is fully loaded");
	SetIsFullScreen(true);

	if(mainMenuUi->GetBox("credits").Dimensions())
	{
		for(const auto &source : GameData::Sources())
		{
			auto credit = Format::Split(Files::Read(source / "credits.txt"), "\n");
			if((credit.size() > 1) || !credit.front().empty())
			{
				credits.insert(credits.end(), credit.begin(), credit.end());
				credits.insert(credits.end(), 15, "");
			}
		}
		// Remove the last 15 lines, as there is already a gap at the beginning of the credits.
		credits.resize(credits.size() - 15);
	}
	else if(showCreditsWarning)
	{
		Logger::Log("Interface \"main menu\" does not contain a box for \"credits\".", Logger::Level::WARNING);
		showCreditsWarning = false;
	}

	if(gamePanels.IsEmpty())
	{
		// This backdrop flight (the ship shown behind the menu) is not the
		// player's game; mark it so a multiplayer login knows to replace it.
		MainPanel *backdrop = new MainPanel(player);
		backdrop->SetIsMenuBackdrop(true);
		gamePanels.Push(backdrop);
		// It takes one step to figure out the planet panel should be created, and
		// another step to actually place it. So, take two steps to avoid a flicker.
		gamePanels.StepAll();
		gamePanels.StepAll();
	}

	if(!scrollSpeed)
		scrollSpeed = 1;

	xSpeed = mainMenuUi->GetValue("x speed");
	ySpeed = mainMenuUi->GetValue("y speed");
	yAmplitude = mainMenuUi->GetValue("y amplitude");
	returnPos = GameData::GetBackgroundPosition();
	GameData::SetBackgroundPosition(Point());

	// When the player is in the menu, pause the game sounds.
	Audio::Pause();
}

MenuPanel::~MenuPanel()
{
	Audio::Resume();
	GameData::SetBackgroundPosition(returnPos);
}

void MenuPanel::Step()
{
	if(Preferences::Has("Animate main menu background"))
	{
		GameData::StepBackground(Point(xSpeed, yAmplitude * sin(animation * TO_RAD)));
		animation += ySpeed;
	}
	else
		GameData::StepBackground(Point());
	if(GetUI().IsTop(this) && !scrollingPaused)
	{
		scroll += scrollSpeed;
		if(scroll < 0)
			scroll = (20 * static_cast<long long int>(credits.size()) + 299) * SCROLL_MOD;
		if(scroll >= (20 * static_cast<long long int>(credits.size()) + 300) * SCROLL_MOD)
			scroll = 0;
	}
}



void MenuPanel::Draw()
{
	glClear(GL_COLOR_BUFFER_BIT);
	// The cleared frame also hides any flight underneath the menu, so neither a
	// connected server world nor a lingering disconnected one is ever visible
	// behind the main menu -- the menu always draws over a clean starfield.
	GameData::Background().Draw(Point());

	Information info;
	if(player.IsLoaded() && !player.IsDead())
	{
		info.SetCondition("pilot loaded");
		info.SetString("pilot", player.FirstName() + " " + player.LastName());
		if(player.Flagship())
		{
			const Ship &flagship = *player.Flagship();
			info.SetSprite("ship sprite", flagship.GetSprite());
			info.SetString("ship", flagship.GivenName());
		}
		if(player.GetSystem())
			info.SetString("system", player.GetSystem()->DisplayName());
		if(player.GetPlanet())
			info.SetString("planet", player.GetPlanet()->DisplayName());
		info.SetString("credits", Format::AbbreviatedNumber(player.Accounts().Credits()));
		info.SetString("date", player.GetDate().ToString());
		info.SetString("playtime", Format::PlayTime(player.GetPlayTime()));
	}
	else if(player.IsLoaded())
	{
		info.SetCondition("pilot dead");
		info.SetString("pilot", player.FirstName() + " " + player.LastName());
		info.SetString("ship", "You have died.");
	}
	else
	{
		info.SetCondition("no pilot loaded");
		info.SetString("pilot", "No Pilot Loaded");
	}
	if(player.Pilot() && !player.Pilot()->GetGamerules().LockGamerules())
		info.SetCondition("gamerules unlocked");
	if(session.IsNetworkMode())
		info.SetCondition("server connected");
	// A network pilot's world lives only in memory (and on the server). Offer
	// the explicit "keep this world" button whenever one is loaded, including
	// after the session has dropped, so its progress can be written to the
	// local saves folder.
	if(player.IsLoaded() && player.IsNetworkPilot())
		info.SetCondition("network pilot loaded");

	GameData::Interfaces().Get("menu background")->Draw(info, this);
	mainMenuUi->Draw(info, this);
	if(session.IsNetworkMode() && mainMenuUi->GetBox("disconnect").Dimensions())
	{
		// Keep the action independent of the interface button's keyboard
		// shortcut so a mouse click always reaches the session disconnect.
		AddZone(mainMenuUi->GetBox("disconnect"), [this]() { DisconnectFromServer(); });
	}
	GameData::Interfaces().Get("menu player info")->Draw(info, this);

	if(!credits.empty())
		DrawCredits();
}



bool MenuPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	if(player.IsLoaded() && (key == 'e' || command.Has(Command::MENU)))
	{
		// A multiplayer flight may still be attached to a local pilot. Do not
		// re-enable the normal save path while returning to the menu; the
		// network session owns the save protection until it is deliberately
		// disconnected or a local save is loaded.
		if(!session.IsNetworkMode() && !player.IsNetworkPilot() && !player.IsNetworkAttached())
			gamePanels.CanSave(true);
		GetUI().PopThrough(this);
		return true;
	}
	else if(key == 'r' && player.IsLoaded() && player.IsDead() && !session.IsNetworkMode())
	{
		// Reloading a local save must not leave a network session attached to
		// the replacement pilot.
		if(session.IsNetworkMode() || session.IsConnected() || session.HasLastServer())
			session.ForgetServer();

		// First, make sure the previous MainPanel has been deleted.
		gamePanels.Reset();
		gamePanels.CanSave(true);

		player.Reload();

		GetUI().PopThrough(GetUI().Root().get());
		gamePanels.Push(new MainPanel(player));
		// It takes one step to figure out the planet panel should be created, and
		// another step to actually place it. So, take two steps to avoid a flicker.
		gamePanels.StepAll();
		gamePanels.StepAll();
	}
	else if(key == 'p')
		GetUI().Push(new PreferencesPanel(player));
	else if(key == 'l' || key == 'm')
		GetUI().Push(new LoadPanel(player, gamePanels, session));
	else if(key == 'c')
		GetUI().Push(new MultiplayerPanel(session, gamePanels));
	else if(key == 'd' && (session.IsNetworkMode() || session.IsConnected()))
		DisconnectFromServer();
	else if(key == 's' && player.IsLoaded() && player.IsNetworkPilot())
		SaveWorldLocally();
	else if(key == 'h')
		GetUI().Push(new HostPanel(player, gamePanels, server, session));
	else if(key == 'n' && !player.IsLoaded())
	{
		// If no player is loaded, the "Enter Ship" button becomes "New Pilot."
		// Request that the player chooses a start scenario.
		// StartConditionsPanel also handles the case where there's no scenarios.
		GetUI().Push(new StartConditionsPanel(player, gamePanels, session, GameData::StartOptions()));
	}
	else if(key == 'g' && player.Pilot() && !player.Pilot()->GetGamerules().LockGamerules())
	{
		GamerulesPanel *panel = new GamerulesPanel(player.Pilot()->GetGamerules(), true);
		panel->SetCallback(&player, &PlayerInfo::SavePilot);
		GetUI().Push(panel);
	}
	else if(key == 'q')
	{
		GetUI().Quit();
		return true;
	}
	else if(key == ' ')
		scrollingPaused = !scrollingPaused;
	else if(key == SDLK_DOWN)
		scrollSpeed += 1;
	else if(key == SDLK_UP)
		scrollSpeed -= 1;
	else
		return false;

	UI::PlaySound(UI::UISound::NORMAL);
	return true;
}



bool MenuPanel::Click(int x, int y, MouseButton button, int clicks)
{
	if(button != MouseButton::LEFT)
		return false;

	// Double clicking on the credits pauses/resumes the credits scroll.
	if(clicks == 2 && mainMenuUi->GetBox("credits").Contains(Point(x, y)))
	{
		scrollingPaused = !scrollingPaused;
		return true;
	}

	return false;
}



void MenuPanel::DisconnectFromServer()
{
	// This action is deliberately unconditional: a stale click zone or a
	// state transition between drawing and clicking must still tear down the
	// client rather than leaving the player attached to the server.
	session.Disconnect();
	// A hosted LAN server was started by this process; leaving the server must
	// close it too, or the loaded server keeps running behind the menu.
	if(server.IsRunning())
		server.Stop();
	ClearNetworkWorld();
	gamePanels.CanSave(true);
	Messages::Add({"Disconnected from the server.", GameData::MessageCategories().Get("info")});
}



void MenuPanel::SaveWorldLocally()
{
	// Ordinary local pilots already save normally; this button is for the
	// in-memory network pilot whose world would otherwise be lost.
	if(!player.IsLoaded() || !player.IsNetworkPilot())
		return;

	if(player.SaveToLocalFolder())
	{
		if(session.IsConnected())
			session.Disconnect();
		// Leave everything behind: close an in-process hosted server and swap
		// the visible flight for the clean menu backdrop, so neither the server
		// nor its world remains on screen behind the menu. The preserved world
		// is safe in the local save and can be reopened from the pilot load
		// menu; the pilot itself stays loaded so the menu keeps showing it.
		if(server.IsRunning())
			server.Stop();
		ClearNetworkWorld();
		gamePanels.CanSave(true);
		Messages::Add({"World saved to your pilot folder; the server was closed.",
			GameData::MessageCategories().Get("info")});
	}
	else
		Messages::Add({"Could not save the network world locally.",
			GameData::MessageCategories().Get("info")});
	UI::PlaySound(UI::UISound::NORMAL);
}



void MenuPanel::ClearNetworkWorld()
{
	// The session and any in-process server are already down; only the visible
	// flight is left. Replace it with the main menu's clean backdrop so nothing
	// of the departed server's world renders behind or beyond the menu. The
	// real pilot is deliberately left untouched: it must stay loaded (the menu
	// keeps showing it, and "Save World Locally" can still preserve its world
	// after a plain Disconnect).
	gamePanels.Reset();
	MainPanel *backdrop = new MainPanel(menuBackdropPlayer);
	backdrop->SetIsMenuBackdrop(true);
	gamePanels.Push(backdrop);
	// It takes one step to figure out the planet panel should be created, and
	// another step to actually place it. So, take two steps to avoid a flicker.
	gamePanels.StepAll();
	gamePanels.StepAll();
	gamePanels.CanSave(true);
}



void MenuPanel::DrawCredits() const
{
	const Font &font = FontSet::Get(14);
	const auto creditsRect = mainMenuUi->GetBox("credits");
	const int top = static_cast<int>(creditsRect.Top());
	const int bottom = static_cast<int>(creditsRect.Bottom());
	int y = bottom + 5 - scroll / SCROLL_MOD;
	for(const string &line : credits)
	{
		float fade = 1.f;
		if(y < top + 20)
			fade = max(0.f, (y - top) / 20.f);
		else if(y > bottom - 20)
			fade = max(0.f, (bottom - y) / 20.f);
		if(fade)
		{
			Color color(((line.empty() || line[0] == ' ') ? .2f : .4f) * fade, 0.f);
			font.Draw(line, Point(creditsRect.Left(), y), color);
		}
		y += 20;
	}
}
