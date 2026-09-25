/* CheatCommand.h
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

#include <string>
#include <vector>

class MainPanel;
class PlayerInfo;
class UI;



// Shared parsing, help, and execution for the local cheat console and
// multiplayer slash commands.
class CheatCommand
{
public:
	// Whether the text is a recognized slash command. A leading slash is
	// optional for the local console, but multiplayer chat requires it.
	static bool IsCommand(const std::string &text, bool requireSlash = false);
	static bool IsHelpCommand(const std::string &text);
	static bool IsClearCommand(const std::string &text);
	static std::vector<std::string> HelpLines();

	// Execute one command on the main game thread. The caller must ensure the
	// engine has been stopped (engine.Wait()) before calling this method.
	static std::string Execute(PlayerInfo &player, UI &gamePanels,
		const std::string &text, MainPanel *mainPanel = nullptr);
};
