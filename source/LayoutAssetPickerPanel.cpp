/* LayoutAssetPickerPanel.cpp
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

#include "LayoutAssetPickerPanel.h"

#include "Color.h"
#include "text/DisplayText.h"
#include "shader/FillShader.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "text/Format.h"
#include "GameData.h"
#include "Screen.h"
#include "image/Sprite.h"
#include "image/SpriteLoadManager.h"
#include "image/SpriteSet.h"
#include "shader/SpriteShader.h"
#include "text/Truncate.h"
#include "UI.h"
#include "UILayout.h"

#include <algorithm>



using namespace std;

namespace {
	constexpr double ROW_HEIGHT = 22.;
	constexpr double PADDING = 16.;
}



bool LayoutAssetPickerPanel::open = false;



LayoutAssetPickerPanel::LayoutAssetPickerPanel(const Point &placement)
	: placement(placement)
{
	SetIsFullScreen(true);
	SetTrapAllEvents(true);
	SetInterruptible(false);
	open = true;
	RefreshAssets();
}



LayoutAssetPickerPanel::~LayoutAssetPickerPanel()
{
	open = false;
}



bool LayoutAssetPickerPanel::IsOpen()
{
	return open;
}



void LayoutAssetPickerPanel::Draw()
{
	DrawBackdrop();

	const Rectangle panel(Point(), Point(min(760., Screen::Width() - 40.),
		min(540., Screen::Height() - 40.)));
	const Color &background = *GameData::Colors().Get("panel background");
	const Color &outline = *GameData::Colors().Get("panel outline");
	const Color &bright = *GameData::Colors().Get("bright");
	const Color &medium = *GameData::Colors().Get("medium");
	const Color &faint = *GameData::Colors().Get("faint");
	const Color &active = *GameData::Colors().Get("active");
	FillShader::Fill(panel.Center(), panel.Dimensions() + Point(2., 2.), outline);
	FillShader::Fill(panel.Center(), panel.Dimensions(), background);

	const Font &font = FontSet::Get(18);
	const Font &smallFont = FontSet::Get(14);
	const Point title = panel.TopLeft() + Point(PADDING, PADDING + font.Height());
	font.Draw("Choose a UI sprite", title, bright);

	const Rectangle search = Rectangle::FromCorner(
		panel.TopLeft() + Point(PADDING, title.Y() + 8.),
		Point(panel.Width() - PADDING * 2., 28.));
	FillShader::Fill(search.Center(), search.Dimensions(), faint);
	const string filterText = filter.empty() ? "Type to filter assets..." : filter;
	smallFont.Draw(DisplayText{filterText, {static_cast<int>(search.Width() - 10.), Truncate::BACK}},
		search.TopLeft() + Point(5., smallFont.Height() * .5), filter.empty() ? medium : bright);

	const Rectangle list = ListBounds();
	const int rows = VisibleRows();
	if(firstVisible > max(0, static_cast<int>(assets.size()) - rows))
		firstVisible = max(0, static_cast<int>(assets.size()) - rows);
	for(int row = 0; row < rows && firstVisible + row < static_cast<int>(assets.size()); ++row)
	{
		int index = firstVisible + row;
		const Point rowTop = list.TopLeft() + Point(0., row * ROW_HEIGHT);
		if(index == selected)
			FillShader::Fill(rowTop + Point(list.Width() * .5, ROW_HEIGHT * .5),
				Point(list.Width(), ROW_HEIGHT), active.Transparent(.25));
		smallFont.Draw(DisplayText{assets[index], {static_cast<int>(list.Width() - 10.), Truncate::MIDDLE}},
			rowTop + Point(5., ROW_HEIGHT * .5), index == selected ? active : medium);
	}
	if(assets.empty())
		smallFont.Draw("No matching sprites", list.TopLeft() + Point(5., ROW_HEIGHT), medium);

	const Rectangle preview = PreviewBounds();
	FillShader::Fill(preview.Center(), preview.Dimensions(), faint);
	if(selected >= 0 && selected < static_cast<int>(assets.size()))
	{
		const string &name = assets[selected];
		smallFont.Draw(DisplayText{name, {static_cast<int>(preview.Width() - 10.), Truncate::MIDDLE}},
			preview.TopLeft() + Point(5., preview.Bottom() - 5. - smallFont.Height()), bright);
		const Sprite *sprite = SpriteSet::Get(name);
		if(sprite && sprite->HasDimensions())
		{
			SpriteLoadManager::LoadDeferred(GetUI().AsyncQueue(), sprite);
			if(sprite->IsLoaded())
			{
				const double scale = min((preview.Width() - 20.) / sprite->Width(),
					(preview.Height() - 45.) / sprite->Height());
				SpriteShader::Draw(sprite, preview.Center() - Point(0., 8.), scale);
			}
			else
				smallFont.Draw("Loading...", preview.Center() - Point(30., 0.), medium);
		}
	}

	const string help = "Enter: add    Up/Down: select    Esc: cancel";
	smallFont.Draw(help, panel.BottomLeft() + Point(PADDING, -PADDING - smallFont.Height()), medium);
}



bool LayoutAssetPickerPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	if(key == SDLK_ESCAPE)
	{
		GetUI().Pop(this);
		return true;
	}
	if(key == SDLK_RETURN || key == SDLK_KP_ENTER)
	{
		AddSelected();
		return true;
	}
	if(key == SDLK_BACKSPACE && !filter.empty())
	{
		filter.pop_back();
		RefreshAssets();
		return true;
	}
	if(key == SDLK_UP)
	{
		Select(selected - 1);
		return true;
	}
	if(key == SDLK_DOWN)
	{
		Select(selected + 1);
		return true;
	}
	if(key == SDLK_PAGEUP)
	{
		Select(selected - VisibleRows());
		return true;
	}
	if(key == SDLK_PAGEDOWN)
	{
		Select(selected + VisibleRows());
		return true;
	}
	return false;
}



bool LayoutAssetPickerPanel::Click(int x, int y, MouseButton button, int clicks)
{
	const Rectangle list = ListBounds();
	if(list.Contains(Point(x, y)))
	{
		int index = firstVisible + static_cast<int>((y - list.Top()) / ROW_HEIGHT);
		if(index >= 0 && index < static_cast<int>(assets.size()))
		{
			Select(index);
			if(clicks > 1)
				AddSelected();
		}
		return true;
	}
	return true;
}



bool LayoutAssetPickerPanel::Scroll(double dx, double dy)
{
	const int direction = dy > 0. ? -1 : 1;
	firstVisible = max(0, firstVisible + direction * 3);
	firstVisible = min(firstVisible, max(0, static_cast<int>(assets.size()) - VisibleRows()));
	return true;
}



bool LayoutAssetPickerPanel::TextInput(const string &text)
{
	for(unsigned char character : text)
		if(character >= 0x20 && character != 0x7f)
			filter += character;
	RefreshAssets();
	return true;
}



void LayoutAssetPickerPanel::RefreshAssets()
{
	const string lowerFilter = Format::LowerCase(filter);
	assets.clear();
	for(const string &name : SpriteSet::Names())
		if(lowerFilter.empty() || Format::LowerCase(name).find(lowerFilter) != string::npos)
			assets.push_back(name);
	selected = 0;
	firstVisible = 0;
}



void LayoutAssetPickerPanel::Select(int index)
{
	if(assets.empty())
	{
		selected = 0;
		firstVisible = 0;
		return;
	}
	selected = max(0, min(index, static_cast<int>(assets.size()) - 1));
	const int rows = VisibleRows();
	if(selected < firstVisible)
		firstVisible = selected;
	else if(selected >= firstVisible + rows)
		firstVisible = selected - rows + 1;
}



void LayoutAssetPickerPanel::AddSelected()
{
	if(selected < 0 || selected >= static_cast<int>(assets.size()))
		return;
	if(UILayout::AddSprite(assets[selected], placement, Point(64., 64.), Color(1.f)))
		GetUI().Pop(this);
}



Rectangle LayoutAssetPickerPanel::ListBounds() const
{
	const Rectangle panel(Point(), Point(min(760., Screen::Width() - 40.),
		min(540., Screen::Height() - 40.)));
	return Rectangle::FromCorner(panel.TopLeft() + Point(PADDING, 78.),
		Point(panel.Width() * .58, panel.Height() - 110.));
}



Rectangle LayoutAssetPickerPanel::PreviewBounds() const
{
	const Rectangle panel(Point(), Point(min(760., Screen::Width() - 40.),
		min(540., Screen::Height() - 40.)));
	return Rectangle::FromCorner(panel.TopLeft() + Point(panel.Width() * .61, 78.),
		Point(panel.Width() * .34, panel.Height() - 110.));
}



int LayoutAssetPickerPanel::VisibleRows() const
{
	return max(1, static_cast<int>(ListBounds().Height() / ROW_HEIGHT));
}
