/* CheatConsolePanel.cpp
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

#include "CheatConsolePanel.h"

#include "CheatCommand.h"
#include "Color.h"
#include "Command.h"
#include "text/DisplayText.h"
#include "Edit.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "GameData.h"
#include "MainPanel.h"
#include "Point.h"
#include "Preferences.h"
#include "Rectangle.h"
#include "Screen.h"
#include "text/Truncate.h"
#include "UI.h"
#include "UILayout.h"

#include <algorithm>
#include <functional>
#include <utility>

using namespace std;

namespace
{
	class ConsoleEdit : public Edit
	{
	public:
		void SetSubmitCallback(std::function<void()> callback)
		{
			submitCallback = std::move(callback);
		}
		void SetCloseCallback(std::function<void()> callback)
		{
			closeCallback = std::move(callback);
		}

	protected:
		bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) override
		{
			if(key == SDLK_RETURN || key == SDLK_KP_ENTER)
			{
				if(submitCallback)
					submitCallback();
				return true;
			}
			if(key == SDLK_ESCAPE || command.Has(Command::CHEATS)
					|| (key == 'w' && (mod & (KMOD_CTRL | KMOD_GUI))))
			{
				if(closeCallback)
					closeCallback();
				return true;
			}
			return Edit::KeyDown(key, mod, command, isNewPress);
		}

	private:
		std::function<void()> submitCallback;
		std::function<void()> closeCallback;
	};
}



CheatConsolePanel::CheatConsolePanel()
{
	Initialize();
}



CheatConsolePanel::CheatConsolePanel(PlayerInfo &player, UI &gamePanels)
	: player(&player), gamePanels(&gamePanels)
{
	Initialize();
}



void CheatConsolePanel::Initialize()
{
	panelWidth = Screen::Width();
	panelHeight = Screen::Height();
	SetIsFullScreen(true);
	SetTrapAllEvents(true);
	SetInterruptible(false);

	auto consoleInput = make_shared<ConsoleEdit>();
	consoleInput->SetLayoutKey("Console", "input");
	consoleInput->SetSubmitCallback([this]() { Submit(); });
	consoleInput->SetCloseCallback([this]() { GetUI().Pop(this); });
	input = consoleInput;
	input->SetFontSize(Preferences::GetFontSize());
	input->SetBgColor(*GameData::Colors().Get("faint"));
	AddChild(input);
	Resize();
}



void CheatConsolePanel::Step()
{
	if(!input->HasFocus())
		input->SetFocus(true);
}



void CheatConsolePanel::Draw()
{
	DrawBackdrop();

	const Font &font = FontSet::Get(Preferences::GetFontSize());
	const Color &medium = *GameData::Colors().Get("medium");

	const Point outputOrigin = UILayout::Apply("Console", "output", Point(-.5 * panelWidth, -.5 * panelHeight));
	const Point inputCenter = input ? input->Position().Center() : Point(0., .5 * panelHeight - 13.);
	const double lineHeight = font.Height() + 3.;
	const double outputWidth = min(1000., panelWidth);
	const double outputBottom = inputCenter.Y() - 13.;

	const size_t maxLines = static_cast<size_t>(max(1., (outputBottom - outputOrigin.Y()) / lineHeight));
	UILayout::Register("Console", "output", Rectangle(
		Point(outputOrigin.X() + .5 * outputWidth, outputOrigin.Y() + .5 * max(1., outputBottom - outputOrigin.Y())),
		Point(outputWidth, max(1., outputBottom - outputOrigin.Y()))));
	const size_t firstLine = output.size() > maxLines ? output.size() - maxLines : 0;
	double y = outputOrigin.Y();
	for(size_t i = firstLine; i < output.size(); ++i, y += lineHeight)
	{
		const DisplayText line(output[i], {static_cast<int>(outputWidth), Truncate::BACK});
		font.Draw(line, Point(outputOrigin.X(), y), medium);
	}
}



bool CheatConsolePanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	if(command.Has(Command::CHEATS) || (key == 'w' && (mod & (KMOD_CTRL | KMOD_GUI))))
		GetUI().Pop(this);
	else if(key == SDLK_RETURN || key == SDLK_KP_ENTER)
		Submit();
	else
		return false;

	UI::PlaySound(UI::UISound::NORMAL);
	return true;
}



void CheatConsolePanel::Resize()
{
	panelWidth = Screen::Width();
	panelHeight = Screen::Height();
	if(input)
	{
		const double inputWidth = panelWidth;
		const Point inputCenter(0., .5 * panelHeight - 13.);
		input->SetPosition(Rectangle(inputCenter, Point(inputWidth, 26.)));
	}
}



MainPanel *CheatConsolePanel::ActiveMainPanel() const
{
	if(!gamePanels)
		return nullptr;
	MainPanel *main = static_cast<MainPanel *>(gamePanels->Root().get());
	return main && !main->IsMenuBackdrop() ? main : nullptr;
}



void CheatConsolePanel::Submit()
{
	const string command = input->Text();
	input->Clear();
	if(command.empty())
		return;
	if(CheatCommand::IsClearCommand(command))
	{
		output.clear();
		return;
	}

	AddOutput("> " + command);
	if(CheatCommand::IsHelpCommand(command))
	{
		for(const string &line : CheatCommand::HelpLines())
			AddOutput(line);
		return;
	}

	if(!player || !gamePanels)
	{
		AddOutput("This console has no active game context.");
		return;
	}

	// The engine may still be finishing the previous frame when the input
	// event arrives. Wait before touching ships or the flight state, even when
	// the console is opened from the main menu.
	MainPanel *main = ActiveMainPanel();
	if(main)
		main->GetEngine().Wait();
	AddOutput(CheatCommand::Execute(*player, *gamePanels, command, main));
}



void CheatConsolePanel::AddOutput(const string &line)
{
	constexpr size_t MAX_LINES = 100;
	output.push_back(line);
	if(output.size() > MAX_LINES)
		output.erase(output.begin(), output.begin() + output.size() - MAX_LINES);
}
