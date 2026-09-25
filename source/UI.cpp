/* UI.cpp
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

#include "UI.h"

#include "audio/Audio.h"
#include "Color.h"
#include "Command.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "GameData.h"
#include "LayoutAssetPickerPanel.h"
#include "shader/LineShader.h"
#include "Panel.h"
#include "Screen.h"
#include "UILayout.h"

#include <SDL2/SDL.h>

#include <algorithm>

using namespace std;

namespace
{
	int layoutColorIndex = 0;
}



// Handle an event. The event is handed to each panel on the stack until one
// of them handles it. If none do, this returns false.
bool UI::Handle(const SDL_Event &event)
{
	const Command keyCommand = event.type == SDL_KEYDOWN
		? Command(event.key.keysym.sym) : Command();
	const bool assetPickerOpen = LayoutAssetPickerPanel::IsOpen();
	if(event.type == SDL_WINDOWEVENT
			&& (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST
				|| event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED))
	{
		UILayout::CancelDrag();
		UILayout::ClearRegions();
	}
	if(!assetPickerOpen && event.type == SDL_KEYDOWN && !event.key.repeat
			&& event.key.keysym.sym == SDLK_ESCAPE && UILayout::IsEditing())
	{
		UILayout::ToggleEditing();
		UILayout::ClearRegions();
		PushOrPop();
		return true;
	}
	const bool toggleLayout = !assetPickerOpen && event.type == SDL_KEYDOWN && !event.key.repeat
		&& (keyCommand.Has(Command::LAYOUT_TOGGLE)
			|| (event.key.keysym.sym == SDLK_l && (event.key.keysym.mod & KMOD_CTRL)));
	if(toggleLayout)
	{
		UILayout::ToggleEditing();
		UILayout::ClearRegions();
		PushOrPop();
		return true;
	}

	if(event.type == SDL_MOUSEMOTION && UILayout::IsDragging())
	{
		const int buttons = SDL_GetMouseState(nullptr, nullptr);
		if(!(buttons & SDL_BUTTON(1)) && !(event.motion.state & SDL_BUTTON(1)))
		{
			UILayout::CancelDrag();
			UILayout::ClearRegions();
		}
	}

	if(UILayout::IsEditing() && !assetPickerOpen)
	{
		auto screenPoint = [](int x, int y) {
			return Point(Screen::Left() + x * 100 / Screen::Zoom(), Screen::Top() + y * 100 / Screen::Zoom());
		};
		if(event.type == SDL_KEYDOWN && !event.key.repeat)
		{
			const SDL_Keycode key = event.key.keysym.sym;
			const Uint16 mod = event.key.keysym.mod;
			if(keyCommand.Has(Command::LAYOUT_ASSETS))
			{
				Push(new LayoutAssetPickerPanel(UI::GetMouse()));
				PushOrPop();
				return true;
			}
			bool changed = false;
			if(key == 'z' && (mod & (KMOD_CTRL | KMOD_GUI)))
				changed = (mod & KMOD_SHIFT) ? UILayout::Redo() : UILayout::Undo();
			else if(key == 'y' && (mod & (KMOD_CTRL | KMOD_GUI)))
				changed = UILayout::Redo();
			else if(key == SDLK_TAB)
				changed = UILayout::SelectNext(mod & KMOD_SHIFT);
			else if(keyCommand.Has(Command::LAYOUT_SCALE_UP)
					|| key == SDLK_PLUS || key == SDLK_KP_PLUS || key == SDLK_EQUALS)
				changed = UILayout::ScaleSelected(1.1);
			else if(keyCommand.Has(Command::LAYOUT_SCALE_DOWN)
					|| key == SDLK_MINUS || key == SDLK_KP_MINUS)
				changed = UILayout::ScaleSelected(1. / 1.1);
			else if(keyCommand.Has(Command::LAYOUT_COLOR) || key == 'c')
			{
				static const Color colors[] = {
					Color(1.f, .2f, .2f), Color(.2f, 1.f, .2f), Color(.2f, .4f, 1.f),
					Color(1.f, .85f, .2f), Color(1.f, 1.f, 1.f)
				};
				++layoutColorIndex %= static_cast<int>(sizeof(colors) / sizeof(colors[0]));
				changed = UILayout::SetSelectedColor(colors[layoutColorIndex]);
			}
			else if(keyCommand.Has(Command::LAYOUT_VISIBILITY) || key == 'h')
				changed = UILayout::ToggleSelectedVisibility();
			else if(keyCommand.Has(Command::LAYOUT_REMOVE) || key == SDLK_BACKSPACE)
				changed = UILayout::RemoveSelected();
			else if(keyCommand.Has(Command::LAYOUT_ADD_TEXT) || key == 'n')
				changed = UILayout::AddText("Custom text", UI::GetMouse(), Point(240., 30.), Color(1.f)) != 0;
			else if(keyCommand.Has(Command::LAYOUT_ADD_SPRITE) || key == 'b')
				changed = UILayout::AddSprite("ui/selected system", UI::GetMouse(), Point(64., 64.), Color(1.f)) != 0;
			if(changed)
			{
				UILayout::ClearRegions();
				AdjustViewport();
				PushOrPop();
				return true;
			}
		}
		if(event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT
				&& UILayout::BeginDrag(screenPoint(event.button.x, event.button.y)))
		{
			AdjustViewport();
			PushOrPop();
			return true;
		}
		if(event.type == SDL_MOUSEMOTION && ((event.motion.state & SDL_BUTTON(1)) || UILayout::IsDragging()))
		{
			Point dragDelta(event.motion.xrel * 100. / Screen::Zoom(),
				event.motion.yrel * 100. / Screen::Zoom());
			if(SDL_GetModState() & KMOD_SHIFT)
				dragDelta *= .25;
			if(UILayout::Drag(dragDelta))
			{
				UILayout::ClearRegions();
				AdjustViewport();
				PushOrPop();
				return true;
			}
		}
		if(event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT && UILayout::EndDrag())
		{
			UILayout::ClearRegions();
			AdjustViewport();
			PushOrPop();
			return true;
		}
		if(event.type == SDL_KEYDOWN && !event.key.repeat
				&& (keyCommand.Has(Command::LAYOUT_RESET) || event.key.keysym.sym == SDLK_DELETE)
				&& UILayout::ResetSelected())
		{
			UILayout::ClearRegions();
			AdjustViewport();
			PushOrPop();
			return true;
		}
	}

	// The layout editor is modal. Do not let clicks, text input, or shortcuts
	// leak into the panel underneath when no editor target handled them.
	if(UILayout::IsEditing() && !assetPickerOpen
			&& (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP
				|| event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEWHEEL
				|| event.type == SDL_KEYDOWN || event.type == SDL_KEYUP
				|| event.type == SDL_TEXTINPUT || event.type == SDL_TEXTEDITING))
	{
		PushOrPop();
		return true;
	}

	bool handled = false;

	vector<shared_ptr<Panel>>::iterator it = stack.end();
	while(it != stack.begin() && !handled)
	{
		--it;
		// Panels that are about to be popped cannot handle any other events.
		if(count(toPop.begin(), toPop.end(), it->get()))
			continue;
		// Keep the panel alive if handling the event resets the UI stack.
		shared_ptr<Panel> panel = *it;

		if(event.type == SDL_MOUSEMOTION)
		{
			if(event.motion.state & SDL_BUTTON(1))
				handled = panel->DoDrag(
					event.motion.xrel * 100. / Screen::Zoom(),
					event.motion.yrel * 100. / Screen::Zoom());
			else
				handled = panel->DoHover(
					Screen::Left() + event.motion.x * 100 / Screen::Zoom(),
					Screen::Top() + event.motion.y * 100 / Screen::Zoom());
		}
		else if(event.type == SDL_MOUSEBUTTONDOWN)
		{
			int x = Screen::Left() + event.button.x * 100 / Screen::Zoom();
			int y = Screen::Top() + event.button.y * 100 / Screen::Zoom();
			if(event.button.button == SDL_BUTTON_LEFT)
				handled = panel->ZoneClick(Point(x, y));
			if(!handled)
				handled = panel->DoClick(x, y, static_cast<MouseButton>(event.button.button), event.button.clicks);
		}
		else if(event.type == SDL_MOUSEBUTTONUP)
		{
			int x = Screen::Left() + event.button.x * 100 / Screen::Zoom();
			int y = Screen::Top() + event.button.y * 100 / Screen::Zoom();
			handled = panel->DoRelease(x, y, static_cast<MouseButton>(event.button.button));
		}
		else if(event.type == SDL_MOUSEWHEEL)
			handled = panel->DoScroll(event.wheel.x, event.wheel.y);
		else if(event.type == SDL_KEYDOWN)
		{
			Command command(event.key.keysym.sym);
			handled = panel->DoKeyDown(event.key.keysym.sym, event.key.keysym.mod, command, !event.key.repeat);
		}
		else if(event.type == SDL_TEXTINPUT)
			handled = panel->DoTextInput(event.text.text);

		// A panel may reset the UI while handling an event. Do not continue
		// with an iterator into the old stack after that happens.
		if(find(stack.begin(), stack.end(), panel) == stack.end()
				|| find(toPop.begin(), toPop.end(), panel.get()) != toPop.end())
			break;

		// If this panel does not want anything below it to receive events, do
		// not let this event trickle further down the stack.
		if(panel->TrapAllEvents())
			break;
	}

	// Handle any queued push or pop commands.
	PushOrPop();

	return handled;
}



// Step all the panels forward (advance animations, move objects, etc.).
void UI::StepAll()
{
	// Handle any queued push or pop commands.
	PushOrPop();

	// Step all the panels.
	for(shared_ptr<Panel> &panel : stack)
		panel->Step();

	// Process any tasks queued up by the panels.
	syncQueue.Wait();
	syncQueue.ProcessSyncTasks();
	asyncQueue.ProcessSyncTasks();
}



// Draw all the panels.
void UI::DrawAll()
{
	UILayout::ClearRegions();

	// First, clear all the clickable zones. New ones will be added in the
	// course of drawing the screen.
	for(const shared_ptr<Panel> &it : stack)
		it->ClearZones();

	// Find the topmost full-screen panel. Nothing below that needs to be drawn.
	vector<shared_ptr<Panel>>::const_iterator it = stack.end();
	while(it != stack.begin())
		if((*--it)->IsFullScreen())
			break;

	for( ; it != stack.end(); ++it)
		(*it)->DoDraw();

	// User-created elements are global overlays. Draw them after the active
	// panel so their editor regions are on top of the built-in regions.
	if(!LayoutAssetPickerPanel::IsOpen())
		UILayout::DrawCustom(&AsyncQueue());

}



void UI::DrawLayoutEditor()
{
	if(LayoutAssetPickerPanel::IsOpen() || !UILayout::IsEditing())
		return;

	const Color &active = *GameData::Colors().Get("active");
	LineShader::Draw(Screen::TopLeft(), Screen::TopRight(), 1.f, active);
	LineShader::Draw(Screen::TopRight(), Screen::BottomRight(), 1.f, active);
	LineShader::Draw(Screen::BottomRight(), Screen::BottomLeft(), 1.f, active);
	LineShader::Draw(Screen::BottomLeft(), Screen::TopLeft(), 1.f, active);
	FontSet::Get(14).Draw("UI layout editor: ON (" + Command::LAYOUT_TOGGLE.KeyName()
			+ " / Esc to exit)", Point(Screen::Left() + 10., Screen::Top() + 10.), active);

	if(UILayout::Regions().empty())
		return;
	const Color &normal = *GameData::Colors().Get("medium");
	const Color &selected = *GameData::Colors().Get("active");
	for(const UILayout::Region &region : UILayout::Regions())
	{
		const bool isSelected = UILayout::IsSelected(region.panel, region.element);
		const Color &color = isSelected ? selected : normal;
		const float width = isSelected ? 1.5f : .75f;
		const Rectangle &bounds = region.bounds;
		LineShader::Draw(bounds.TopLeft(), bounds.TopRight(), width, color);
		LineShader::Draw(bounds.TopRight(), bounds.BottomRight(), width, color);
		LineShader::Draw(bounds.BottomRight(), bounds.BottomLeft(), width, color);
		LineShader::Draw(bounds.BottomLeft(), bounds.TopLeft(), width, color);
	}
}



TaskQueue &UI::SyncQueue()
{
	return syncQueue;
}



TaskQueue &UI::AsyncQueue()
{
	return asyncQueue;
}



const vector<shared_ptr<Panel>> &UI::Stack() const
{
	return stack;
}



// Add the given panel to the stack. UI is responsible for deleting it.
void UI::Push(Panel *panel)
{
	Push(shared_ptr<Panel>(panel));
}



void UI::Push(const shared_ptr<Panel> &panel)
{
	toPush.push_back(panel);
	panel->SetUI(this);
	panel->DoResize();
	panel->DoUpdateTextDisplay();
}



// Remove the given panel from the stack (if it is in it). The panel will be
// deleted at the start of the next time Step() is called, so it is safe for
// a panel to Pop() itself.
void UI::Pop(const Panel *panel)
{
	toPop.push_back(panel);
}



// Remove the given panel and every panel that is higher in the stack.
void UI::PopThrough(const Panel *panel)
{
	// Panels queued with Push() are logically above the current stack. Handle
	// them as part of the same range so a start/menu panel cannot reappear
	// after a transition that happened in the same frame.
	for(auto it = toPush.rbegin(); it != toPush.rend(); ++it)
		if(it->get() == panel)
		{
			for(auto pending = toPush.rbegin(); pending != toPush.rend(); ++pending)
			{
				toPop.push_back(pending->get());
				if(pending->get() == panel)
					break;
			}
			return;
		}

	for(auto it = stack.rbegin(); it != stack.rend(); ++it)
		if(it->get() == panel)
		{
			for(auto pending = toPush.rbegin(); pending != toPush.rend(); ++pending)
				toPop.push_back(pending->get());
			for(auto current = stack.rbegin(); current != stack.rend(); ++current)
			{
				toPop.push_back(current->get());
				if(current->get() == panel)
					break;
			}
			return;
		}
}



// Check whether the given panel is on top of the existing panels, i.e. is the
// active one, on this Step. Any panels that have been pushed this Step are not
// considered.
bool UI::IsTop(const Panel *panel) const
{
	return (!stack.empty() && stack.back().get() == panel);
}



// Get the absolute top panel, even if it is not yet drawn (i.e. was pushed on
// this Step).
shared_ptr<Panel> UI::Top() const
{
	if(!toPush.empty())
		return toPush.back();

	if(!stack.empty())
		return stack.back();

	return shared_ptr<Panel>();
}



// Delete all the panels and clear the "done" flag.
void UI::Reset()
{
	UILayout::CancelDrag();
	UILayout::ClearRegions();
	stack.clear();
	toPush.clear();
	toPop.clear();
	isDone = false;
}



// Get the lower-most panel.
shared_ptr<Panel> UI::Root() const
{
	if(stack.empty())
	{
		if(toPush.empty())
			return shared_ptr<Panel>();

		return toPush.front();
	}

	return stack.front();
}



// If the player enters the game, enable saving the loaded file.
void UI::CanSave(bool canSave)
{
	this->canSave = canSave;
}



bool UI::CanSave() const
{
	return canSave;
}



// Tell the UI to quit.
void UI::Quit()
{
	isDone = true;
}



// Check if it is time to quit.
bool UI::IsDone() const
{
	return isDone;
}



// Check if there are no panels left. No panels left on the gamePanels-
// stack usually means that it is time for the game to quit, while no
// panels left on the menuPanels-stack is a normal state for a running
// game.
bool UI::IsEmpty() const
{
	return stack.empty() && toPush.empty();
}



void UI::AdjustViewport() const
{
	for(auto &it : stack)
		it->DoResize();
}



void UI::AdjustTextDisplay() const
{
	for(auto &it : stack)
		it->DoUpdateTextDisplay();
}



// Get the current mouse position.
Point UI::GetMouse()
{
	int x = 0;
	int y = 0;
	SDL_GetMouseState(&x, &y);
	return Screen::TopLeft() + Point(x, y) * (100. / Screen::Zoom());
}



void UI::PlaySound(UISound sound)
{
	string name;
	switch(sound)
	{
		case UISound::NORMAL:
			name = "ui/click";
			break;
		case UISound::SOFT:
			name = "ui/click soft";
			break;
		case UISound::SOFT_BUZZ:
			name = "ui/buzz soft";
			break;
		case UISound::TARGET:
			name = "ui/target";
			break;
		case UISound::FAILURE:
			name = "ui/fail";
			break;
		default:
			return;
	}
	Audio::Play(Audio::Get(name), SoundCategory::UI);
}



// If a push or pop is queued, apply it.
void UI::PushOrPop()
{
	const bool stackChanged = !toPush.empty() || !toPop.empty();
	// Handle any panels that should be added.
	for(shared_ptr<Panel> &panel : toPush)
		if(panel)
			stack.push_back(panel);
	toPush.clear();

	// These panels should be popped but not deleted (because someone else
	// owns them and is managing their creation and deletion).
	for(const Panel *panel : toPop)
	{
		for(auto it = stack.begin(); it != stack.end(); ++it)
			if(it->get() == panel)
			{
				stack.erase(it);
				break;
			}
	}
	toPop.clear();

	// Each panel potentially has its own children, which could be modified.
	for(auto &panel : stack)
		panel->AddOrRemove();
	if(stackChanged)
		UILayout::ClearRegions();
}
