/* UILayout.h
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

#include "Color.h"
#include "Point.h"
#include "Rectangle.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>



// Persistent, resolution-independent positions for named UI elements. The
// offset is relative to the element's normal position, so existing layouts
// remain unchanged until an element is explicitly moved.
class TaskQueue;

class UILayout {
public:
	static void Load();
	static void Save();

	// Transactional editing history. A transaction groups related mutations
	// (for example, a complete drag) into one undoable operation.
	static bool BeginTransaction();
	static bool CommitTransaction();
	static void AbortTransaction();
	static bool Undo();
	static bool Redo();
	static bool CanUndo();
	static bool CanRedo();
	static void ClearHistory();
	static bool ClearAll();

	struct Alias {
		std::string panel;
		std::string canonical;
		std::string legacy;
	};
	static void MigrateAliases(const std::vector<Alias> &aliases);

	// Apply the stored movement for an element to its normal position.
	static Point Apply(const std::string &panel, const std::string &element, const Point &normalPosition);
	// Apply a persistent scale to an element's normal dimensions.
	static Point ApplyScale(const std::string &panel, const std::string &element, const Point &normalSize);
	// Apply a persistent font/text color, or return the supplied fallback.
	static Color ApplyColor(const std::string &panel, const std::string &element, const Color &fallback);
	static bool IsVisible(const std::string &panel, const std::string &element);

	// Set the movement in current screen coordinates. The value is normalized
	// before it is stored so it scales with the window size.
	static void SetOffset(const std::string &panel, const std::string &element, const Point &offset);
	// Set a scale and an RGBA color. Values are clamped to safe ranges.
	static void SetScale(const std::string &panel, const std::string &element, const Point &scale);
	static void SetColor(const std::string &panel, const std::string &element, const Color &color);
	static void SetVisible(const std::string &panel, const std::string &element, bool visible);
	static void Reset(const std::string &panel, const std::string &element);
	static bool Has(const std::string &panel, const std::string &element);
	static void Clear();

	// Layout editor support. Panels register their movable regions while they
	// draw; the UI routes mouse events here while editing is enabled.
	struct Region {
		std::string panel;
		std::string element;
		Rectangle bounds;
		uint64_t frame = 0;
	};

	// User-created overlay elements. Position and size are stored as
	// fractions of the screen so custom layouts remain resolution-independent.
	struct CustomElement {
		int id = 0;
		std::string type;
		std::string value;
		Point position;
		Point size;
		Color color = Color(.8f, 1.f);
		int fontSize = 14;
	};
	// The optional queue is used to request deferred sprite images for overlays.
	static void DrawCustom(TaskQueue *queue = nullptr);
	static int AddSprite(const std::string &sprite, const Point &position, const Point &size,
		const Color &color = Color(.8f, 1.f));
	static int AddText(const std::string &text, const Point &position, const Point &size,
		const Color &color = Color(.8f, 1.f), int fontSize = 14);
	static const std::vector<CustomElement> &CustomElements();
	static const CustomElement *GetCustom(int id);
	static bool RemoveCustom(int id);
	static bool SetCustomPosition(int id, const Point &position);
	static bool SetCustomSize(int id, const Point &size);
	static bool SetCustomColor(int id, const Color &color);
	static bool SetCustomOpacity(int id, double opacity);
	static bool SetCustomValue(int id, const std::string &value);
	static bool SetCustomFontSize(int id, int fontSize);
	static int DuplicateCustom(int id);

	static void ClearRegions();
	static void Register(const std::string &panel, const std::string &element, const Rectangle &bounds);
	static const std::vector<Region> &Regions();
	static bool IsEditing();
	static bool IsDragging();
	static void ToggleEditing();
	static bool BeginDrag(const Point &point);
	static bool SelectNext(bool reverse);
	static bool Drag(const Point &delta);
	static bool EndDrag();
	// Abort a drag when the window loses focus or the mouse release event is
	// lost. The current offset is still saved.
	static void CancelDrag();
	static bool ResetSelected();
	static bool ScaleSelected(double factor);
	static bool SetSelectedColor(const Color &color);
	static bool ToggleSelectedVisibility();
	static bool RemoveSelected();
	static bool IsSelected(const std::string &panel, const std::string &element);


private:
	static bool IsValidKey(const std::string &panel, const std::string &element);
};
