/* CheatConsolePanel.h
Copyright (c) 2026 by the Endless Sky developers

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

#include <memory>
#include <string>
#include <vector>

class Edit;
class MainPanel;
class PlayerInfo;
class UI;



// A local console for entering slash commands without opening multiplayer chat.
class CheatConsolePanel : public Panel
{
public:
	CheatConsolePanel();
	CheatConsolePanel(PlayerInfo &player, UI &gamePanels);

	void Step() override;
	void Draw() override;

protected:
	bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) override;
	void Resize() override;

private:
	void Initialize();
	void Submit();
	void AddOutput(const std::string &line);
	MainPanel *ActiveMainPanel() const;

	PlayerInfo *player = nullptr;
	UI *gamePanels = nullptr;
	std::shared_ptr<Edit> input;
	std::vector<std::string> output;
	double panelWidth = 800.;
	double panelHeight = 500.;
};
