/* LayoutAssetPickerPanel.h
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
#include "Point.h"

#include <string>
#include <vector>



// A modal search/preview panel for choosing a sprite to add to the layout.
class LayoutAssetPickerPanel : public Panel {
public:
	explicit LayoutAssetPickerPanel(const Point &placement);
	~LayoutAssetPickerPanel() override;

	static bool IsOpen();


public:
	void Draw() override;


protected:
	bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) override;
	bool Click(int x, int y, MouseButton button, int clicks) override;
	bool Scroll(double dx, double dy) override;
	bool TextInput(const std::string &text) override;


private:
	void RefreshAssets();
	void Select(int index);
	void AddSelected();
	Rectangle ListBounds() const;
	Rectangle PreviewBounds() const;
	int VisibleRows() const;


private:
	Point placement;
	std::string filter;
	std::vector<std::string> assets;
	int selected = 0;
	int firstVisible = 0;
	static bool open;
};
