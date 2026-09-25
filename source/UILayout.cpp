/* UILayout.cpp
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

#include "UILayout.h"

#include "DataFile.h"
#include "DataNode.h"
#include "DataWriter.h"
#include "text/DisplayText.h"
#include "Files.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "text/Layout.h"
#include "Screen.h"
#include "image/Sprite.h"
#include "image/SpriteLoadManager.h"
#include "image/SpriteSet.h"
#include "shader/SpriteShader.h"
#include "TaskQueue.h"
#include "text/Truncate.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <utility>

using namespace std;

namespace
{
	using LayoutKey = pair<string, string>;

	struct Style {
		Point offset;
		Point scale = Point(1., 1.);
		Color color = Color(.8f, 1.f);
		bool hasColor = false;
		bool visible = true;
	};

	struct State {
		map<LayoutKey, Style> styles;
		vector<UILayout::CustomElement> customElements;
		int nextCustomId = 1;
	};

	struct Change {
		State before;
		State after;
	};

	constexpr size_t MAX_HISTORY = 128;

	map<LayoutKey, Style> styles;
	vector<UILayout::CustomElement> customElements;
	int nextCustomId = 1;
	bool dirty = false;
	vector<UILayout::Region> regions;
	uint64_t frame = 0;
	bool editing = false;
	bool dragging = false;
	bool hasSelected = false;
	LayoutKey selected;
	vector<Change> undoHistory;
	vector<Change> redoHistory;
	bool transactionActive = false;
	bool transactionDirty = false;
	State transactionBefore;
	bool dragOwnsTransaction = false;

	bool SameStyle(const Style &a, const Style &b)
	{
		return a.offset == b.offset && a.scale == b.scale && a.color == b.color
			&& a.hasColor == b.hasColor && a.visible == b.visible;
	}

	bool SameCustom(const UILayout::CustomElement &a, const UILayout::CustomElement &b)
	{
		return a.id == b.id && a.type == b.type && a.value == b.value
			&& a.position == b.position && a.size == b.size && a.color == b.color
			&& a.fontSize == b.fontSize;
	}

	bool SameState(const State &a, const State &b)
	{
		if(a.nextCustomId != b.nextCustomId || a.styles.size() != b.styles.size()
				|| a.customElements.size() != b.customElements.size())
			return false;
		for(auto it = a.styles.begin(), other = b.styles.begin(); it != a.styles.end(); ++it, ++other)
			if(it->first != other->first || !SameStyle(it->second, other->second))
				return false;
		for(size_t i = 0; i < a.customElements.size(); ++i)
			if(!SameCustom(a.customElements[i], b.customElements[i]))
				return false;
		return true;
	}

	State CaptureState()
	{
		return {styles, customElements, nextCustomId};
	}

	string CustomKey(int id)
	{
		return "#" + to_string(id);
	}

	UILayout::CustomElement *FindCustom(int id)
	{
		auto it = find_if(customElements.begin(), customElements.end(),
			[id](const UILayout::CustomElement &element) { return element.id == id; });
		return it == customElements.end() ? nullptr : &*it;
	}

	void ResetEditorState()
	{
		editing = false;
		dragging = false;
		dragOwnsTransaction = false;
		hasSelected = false;
		selected = {};
		UILayout::ClearRegions();
	}

	void RestoreState(const State &state)
	{
		styles = state.styles;
		customElements = state.customElements;
		nextCustomId = state.nextCustomId;
		dirty = true;
		UILayout::ClearRegions();
		if(hasSelected && !styles.contains(selected))
		{
			bool hasCustom = false;
			for(const UILayout::CustomElement &element : customElements)
				if(selected == LayoutKey{"Layout", CustomKey(element.id)})
				{
					hasCustom = true;
					break;
				}
			if(!hasCustom)
			{
				hasSelected = false;
				selected = {};
			}
		}
	}

	void FinishTransaction(bool began, bool success)
	{
		if(!began)
			return;
		if(success)
			UILayout::CommitTransaction();
		else
			UILayout::AbortTransaction();
	}

	void ClearPersistedState()
	{
		styles.clear();
		customElements.clear();
		nextCustomId = 1;
		dirty = true;
	}

	double ClampValue(double value, double minimum, double maximum, double fallback);
	bool IsPrintable(const string &text);

	void LoadNode(const DataNode &node)
	{
		if(node.Token(0) == "element" && node.Size() >= 5
				&& node.IsNumber(3) && node.IsNumber(4))
			styles[{node.Token(1), node.Token(2)}].offset = Point(
				ClampValue(node.Value(3), -1., 1., 0.),
				ClampValue(node.Value(4), -1., 1., 0.));
		else if(node.Token(0) == "scale" && node.Size() >= 5
				&& node.IsNumber(3) && node.IsNumber(4))
			styles[{node.Token(1), node.Token(2)}].scale = Point(
				ClampValue(node.Value(3), .1, 10., 1.),
				ClampValue(node.Value(4), .1, 10., 1.));
		else if(node.Token(0) == "color" && node.Size() >= 7
				&& node.IsNumber(3) && node.IsNumber(4) && node.IsNumber(5) && node.IsNumber(6))
		{
			Style &style = styles[{node.Token(1), node.Token(2)}];
			style.color.Load(
				ClampValue(node.Value(3), 0., 1., 0.),
				ClampValue(node.Value(4), 0., 1., 0.),
				ClampValue(node.Value(5), 0., 1., 0.),
				ClampValue(node.Value(6), 0., 1., 1.));
			style.hasColor = true;
		}
		else if(node.Token(0) == "visible" && node.Size() >= 4 && node.IsBool(3))
			styles[{node.Token(1), node.Token(2)}].visible = node.BoolValue(3);
		else if(node.Token(0) == "custom" && node.Size() >= 12
				&& node.IsNumber(1) && (node.Token(2) == "sprite" || node.Token(2) == "text")
				&& node.IsNumber(4) && node.IsNumber(5) && node.IsNumber(6) && node.IsNumber(7)
				&& node.IsNumber(8) && node.IsNumber(9) && node.IsNumber(10) && node.IsNumber(11))
		{
			const double id = node.Value(1);
			if(std::isfinite(id) && id >= 1. && id <= numeric_limits<int>::max()
					&& floor(id) == id)
			{
				UILayout::CustomElement element;
				element.id = static_cast<int>(id);
				element.type = node.Token(2);
				element.value = node.Token(3);
				element.position = Point(
					ClampValue(node.Value(4), 0., 1., 0.),
					ClampValue(node.Value(5), 0., 1., 0.));
				element.size = Point(
					ClampValue(node.Value(6), .001, 1., .1),
					ClampValue(node.Value(7), .001, 1., .1));
				element.color.Load(
					ClampValue(node.Value(8), 0., 1., 0.),
					ClampValue(node.Value(9), 0., 1., 0.),
					ClampValue(node.Value(10), 0., 1., 0.),
					ClampValue(node.Value(11), 0., 1., 1.));
				if(node.Size() >= 13 && node.IsNumber(12))
					element.fontSize = static_cast<int>(ClampValue(node.Value(12), 1., 72., 14.));
				if(element.id > 0 && IsPrintable(element.value) && !FindCustom(element.id))
					customElements.push_back(std::move(element));
			}
		}

		for(const DataNode &child : node)
			LoadNode(child);
	}

	bool IsPrintable(const string &text)
	{
		if(text.empty() || text.size() > 128)
			return false;
		bool hasQuote = false;
		bool hasBacktick = false;
		for(unsigned char character : text)
		{
			if(character < 0x20 || character == 0x7f)
				return false;
			hasQuote |= character == '"';
			hasBacktick |= character == '`';
		}
		// The data format has no escape sequence for both quote styles.
		return !(hasQuote && hasBacktick);
	}

	double ClampValue(double value, double minimum, double maximum, double fallback)
	{
		return std::isfinite(value) ? max(minimum, min(maximum, value)) : fallback;
	}

	double NormalizePosition(double value, double origin, double extent)
	{
		if(!std::isfinite(value))
			return 0.;
		return max(0., min(1., (value - origin) / max(1., extent)));
	}

	double NormalizeSize(double value, double extent)
	{
		if(!std::isfinite(value))
			return .1;
		return max(.001, min(1., value / max(1., extent)));
	}

	int AddCustom(const string &type, const string &value, const Point &position, const Point &size,
		const Color &color, int fontSize)
	{
		if(customElements.size() >= 128 || !IsPrintable(value)
				|| !std::isfinite(position.X()) || !std::isfinite(position.Y())
				|| !std::isfinite(size.X()) || !std::isfinite(size.Y())
				|| size.X() <= 0. || size.Y() <= 0.
				|| nextCustomId <= 0 || nextCustomId == numeric_limits<int>::max())
			return 0;

		UILayout::CustomElement element;
		element.id = nextCustomId++;
		element.type = type;
		element.value = value;
		element.position = Point(
			NormalizePosition(position.X(), Screen::Left(), Screen::Width()),
			NormalizePosition(position.Y(), Screen::Top(), Screen::Height()));
		element.size = Point(
			NormalizeSize(size.X(), Screen::Width()),
			NormalizeSize(size.Y(), Screen::Height()));
		const float *rgba = color.Get();
		element.color.Load(
			ClampValue(rgba[0], 0., 1., 0.),
			ClampValue(rgba[1], 0., 1., 0.),
			ClampValue(rgba[2], 0., 1., 0.),
			ClampValue(rgba[3], 0., 1., 1.));
		element.fontSize = max(1, min(72, fontSize));
		customElements.push_back(std::move(element));
		UILayout::ClearRegions();
		dirty = true;
		return customElements.back().id;
	}
}



void UILayout::Load()
{
	styles.clear();
	customElements.clear();
	nextCustomId = 1;
	regions.clear();
	frame = 0;
	editing = false;
	dragging = false;
	hasSelected = false;
	selected = {};
	undoHistory.clear();
	redoHistory.clear();
	transactionActive = false;
	transactionDirty = false;
	transactionBefore = State();
	dragOwnsTransaction = false;
	dirty = false;

	DataFile layout(Files::Config() / "ui-layout.txt");
	for(const DataNode &node : layout)
		LoadNode(node);

	for(const CustomElement &element : customElements)
		if(element.id < numeric_limits<int>::max())
			nextCustomId = max(nextCustomId, element.id + 1);
}



void UILayout::Save()
{
	if(!dirty)
		return;
	// Unit tests and tools may use UILayout before Files::Init(). Do not write
	// a relative ui-layout.txt into their working directory in that case.
	if(Files::Config().empty())
	{
		dirty = false;
		return;
	}

	DataWriter out;
	out.Write("ui-layout", 2);
	out.BeginChild();
	for(const auto &[key, style] : styles)
	{
		if(style.offset.X() || style.offset.Y())
			out.Write("element", key.first, key.second, style.offset.X(), style.offset.Y());
		if(style.scale.X() != 1. || style.scale.Y() != 1.)
			out.Write("scale", key.first, key.second, style.scale.X(), style.scale.Y());
		if(style.hasColor)
		{
			const float *color = style.color.Get();
			out.Write("color", key.first, key.second, color[0], color[1], color[2], color[3]);
		}
		if(!style.visible)
			out.Write("visible", key.first, key.second, false);
	}
	for(const CustomElement &element : customElements)
	{
		const float *color = element.color.Get();
		out.Write("custom", element.id, element.type, element.value,
			element.position.X(), element.position.Y(), element.size.X(), element.size.Y(),
			color[0], color[1], color[2], color[3], element.fontSize);
	}
	out.EndChild();

	// Write to a sibling temporary file first. This prevents a failed or
	// interrupted write from truncating the last known-good layout.
	const filesystem::path path = Files::Config() / "ui-layout.txt";
	filesystem::path temporary = path;
	temporary += ".tmp";
	filesystem::remove(temporary);
	shared_ptr<iostream> file = Files::Open(temporary, true);
	if(!file)
		return;
	*file << out.SaveToString();
	file->flush();
	const bool writeSucceeded = !file->fail();
	file.reset();
	if(!writeSucceeded)
	{
		filesystem::remove(temporary);
		return;
	}

	error_code error;
	filesystem::rename(temporary, path, error);
#ifdef _WIN32
	// POSIX replaces an existing destination atomically. Windows does not, so
	// retry after removing the old file when the first rename reports that it
	// already exists.
	if(error)
	{
		error.clear();
		filesystem::remove(path, error);
		error.clear();
		filesystem::rename(temporary, path, error);
	}
#endif
	if(error)
	{
		filesystem::remove(temporary);
		return;
	}
	dirty = false;
}



bool UILayout::BeginTransaction()
{
	if(transactionActive)
		return false;
	transactionBefore = CaptureState();
	transactionDirty = dirty;
	transactionActive = true;
	return true;
}



bool UILayout::CommitTransaction()
{
	if(!transactionActive)
		return false;

	State after = CaptureState();
	const bool changed = !SameState(transactionBefore, after);
	transactionActive = false;
	dragOwnsTransaction = false;
	if(changed)
	{
		if(undoHistory.size() >= MAX_HISTORY)
			undoHistory.erase(undoHistory.begin());
		undoHistory.push_back({transactionBefore, after});
		redoHistory.clear();
		dirty = true;
	}
	transactionBefore = State();
	Save();
	return changed;
}



void UILayout::AbortTransaction()
{
	if(!transactionActive)
		return;

	RestoreState(transactionBefore);
	dirty = transactionDirty;
	transactionActive = false;
	dragOwnsTransaction = false;
	transactionBefore = State();
}



bool UILayout::Undo()
{
	if(transactionActive || undoHistory.empty())
		return false;

	Change change = undoHistory.back();
	undoHistory.pop_back();
	RestoreState(change.before);
	redoHistory.push_back(std::move(change));
	Save();
	return true;
}



bool UILayout::Redo()
{
	if(transactionActive || redoHistory.empty())
		return false;

	Change change = redoHistory.back();
	redoHistory.pop_back();
	RestoreState(change.after);
	undoHistory.push_back(std::move(change));
	Save();
	return true;
}



bool UILayout::CanUndo()
{
	return !transactionActive && !undoHistory.empty();
}



bool UILayout::CanRedo()
{
	return !transactionActive && !redoHistory.empty();
}



void UILayout::ClearHistory()
{
	if(transactionActive)
		AbortTransaction();
	undoHistory.clear();
	redoHistory.clear();
}



Point UILayout::Apply(const string &panel, const string &element, const Point &normalPosition)
{
	auto it = styles.find({panel, element});
	if(it == styles.end())
		return normalPosition;

	const double width = max(1, Screen::Width());
	const double height = max(1, Screen::Height());
	return normalPosition + it->second.offset * Point(width, height);
}



Point UILayout::ApplyScale(const string &panel, const string &element, const Point &normalSize)
{
	auto it = styles.find({panel, element});
	return it == styles.end() ? normalSize : normalSize * it->second.scale;
}



Color UILayout::ApplyColor(const string &panel, const string &element, const Color &fallback)
{
	auto it = styles.find({panel, element});
	return it == styles.end() || !it->second.hasColor ? fallback : it->second.color;
}



bool UILayout::IsVisible(const string &panel, const string &element)
{
	auto it = styles.find({panel, element});
	return it == styles.end() || it->second.visible;
}



void UILayout::SetOffset(const string &panel, const string &element, const Point &offset)
{
	const bool began = BeginTransaction();
	if(!IsValidKey(panel, element))
	{
		FinishTransaction(began, false);
		return;
	}

	const double width = max(1, Screen::Width());
	const double height = max(1, Screen::Height());
	const Point normalized(
		ClampValue(offset.X() / width, -1., 1., 0.),
		ClampValue(offset.Y() / height, -1., 1., 0.));
	styles[{panel, element}].offset = normalized;
	ClearRegions();
	dirty = true;
	FinishTransaction(began, true);
}



void UILayout::SetScale(const string &panel, const string &element, const Point &scale)
{
	const bool began = BeginTransaction();
	if(!IsValidKey(panel, element))
	{
		FinishTransaction(began, false);
		return;
	}

	auto clampScale = [](double value) {
		return std::isfinite(value) ? max(.1, min(10., value)) : 1.;
	};
	styles[{panel, element}].scale = Point(clampScale(scale.X()), clampScale(scale.Y()));
	ClearRegions();
	dirty = true;
	FinishTransaction(began, true);
}



void UILayout::SetColor(const string &panel, const string &element, const Color &color)
{
	const bool began = BeginTransaction();
	if(!IsValidKey(panel, element))
	{
		FinishTransaction(began, false);
		return;
	}

	const float *rgba = color.Get();
	Style &style = styles[{panel, element}];
	style.color.Load(
		ClampValue(rgba[0], 0., 1., 0.),
		ClampValue(rgba[1], 0., 1., 0.),
		ClampValue(rgba[2], 0., 1., 0.),
		ClampValue(rgba[3], 0., 1., 1.));
	style.hasColor = true;
	dirty = true;
	FinishTransaction(began, true);
}



void UILayout::SetVisible(const string &panel, const string &element, bool visible)
{
	const bool began = BeginTransaction();
	if(!IsValidKey(panel, element))
	{
		FinishTransaction(began, false);
		return;
	}

	styles[{panel, element}].visible = visible;
	ClearRegions();
	dirty = true;
	FinishTransaction(began, true);
}



void UILayout::Reset(const string &panel, const string &element)
{
	const bool began = BeginTransaction();
	auto it = styles.find({panel, element});
	if(it == styles.end())
	{
		FinishTransaction(began, false);
		return;
	}

	it->second.offset = Point();
	if(it->second.scale == Point(1., 1.) && !it->second.hasColor && it->second.visible)
		styles.erase(it);
	ClearRegions();
	dirty = true;
	FinishTransaction(began, true);
}



bool UILayout::Has(const string &panel, const string &element)
{
	return styles.contains({panel, element});
}



void UILayout::Clear()
{
	if(transactionActive)
		AbortTransaction();
	const bool changed = !styles.empty() || !customElements.empty() || nextCustomId != 1;
	if(changed)
		ClearPersistedState();
	ResetEditorState();
	undoHistory.clear();
	redoHistory.clear();
}



bool UILayout::ClearAll()
{
	const bool began = BeginTransaction();
	const bool changed = !styles.empty() || !customElements.empty() || nextCustomId != 1;
	if(changed)
		ClearPersistedState();
	ResetEditorState();
	FinishTransaction(began, true);
	return changed;
}



void UILayout::MigrateAliases(const vector<Alias> &aliases)
{
	if(transactionActive || aliases.empty())
		return;

	bool changed = false;
	set<LayoutKey> legacyKeys;
	for(const Alias &alias : aliases)
	{
		if(alias.panel.empty() || alias.canonical.empty() || alias.legacy.empty()
				|| alias.canonical == alias.legacy)
			continue;
		LayoutKey legacyKey{alias.panel, alias.legacy};
		LayoutKey canonicalKey{alias.panel, alias.canonical};
		auto legacy = styles.find(legacyKey);
		if(legacy == styles.end())
			continue;
		legacyKeys.insert(legacyKey);
		if(!styles.contains(canonicalKey))
			styles[canonicalKey] = legacy->second;
	}
	for(const LayoutKey &legacyKey : legacyKeys)
	{
		styles.erase(legacyKey);
		changed = true;
	}
	if(changed)
	{
		dirty = true;
		undoHistory.clear();
		redoHistory.clear();
		UILayout::ClearRegions();
	}
}



void UILayout::DrawCustom(TaskQueue *queue)
{
	const double width = max(1, Screen::Width());
	const double height = max(1, Screen::Height());
	for(const CustomElement &element : customElements)
	{
		const string key = CustomKey(element.id);
		if(!IsVisible("Layout", key))
			continue;

		Point position(
			Screen::Left() + element.position.X() * width,
			Screen::Top() + element.position.Y() * height);
		Point size(element.size.X() * width, element.size.Y() * height);
		position += Apply("Layout", key, Point());
		size = ApplyScale("Layout", key, size);
		if(size.X() <= 0. || size.Y() <= 0.)
			continue;

		Point center = position + .5 * size;
		if(element.type == "sprite")
		{
			const Sprite *sprite = SpriteSet::Get(element.value);
			if(!sprite || !sprite->HasDimensions())
				continue;
			if(queue)
				SpriteLoadManager::LoadDeferred(*queue, sprite);
			Register("Layout", key, Rectangle::FromCorner(position, size));
			if(!sprite->IsLoaded())
				continue;
			const Point scale(size.X() / sprite->Width(), size.Y() / sprite->Height());
			const Color color = ApplyColor("Layout", key, element.color);
			SpriteShader::Bind();
			SpriteShader::Item item = SpriteShader::Prepare(sprite, center, scale, nullptr, 0.f);
			const float *rgba = color.Get();
			item.tint[0] = rgba[0];
			item.tint[1] = rgba[1];
			item.tint[2] = rgba[2];
			item.tint[3] = 1.f;
			item.alpha = rgba[3];
			SpriteShader::Add(item);
			SpriteShader::Unbind();
		}
		else if(element.type == "text")
		{
			Register("Layout", key, Rectangle::FromCorner(position, size));
			const Font &font = FontSet::Get(element.fontSize);
			const Color color = ApplyColor("Layout", key, element.color);
			if(size.X() >= 1.)
				font.Draw(DisplayText{element.value, {static_cast<int>(size.X()), Truncate::BACK}},
					position, color);
			else
				font.Draw(element.value, position, color);
		}
	}
}



int UILayout::AddSprite(const string &sprite, const Point &position, const Point &size, const Color &color)
{
	if(!IsPrintable(sprite) || !SpriteSet::Exists(sprite)
			|| !SpriteSet::Get(sprite)->HasDimensions())
		return 0;
	const bool began = BeginTransaction();
	const int id = AddCustom("sprite", sprite, position, size, color, 14);
	FinishTransaction(began, id != 0);
	return id;
}



int UILayout::AddText(const string &text, const Point &position, const Point &size,
	const Color &color, int fontSize)
{
	const bool began = BeginTransaction();
	const int id = AddCustom("text", text, position, size, color, fontSize);
	FinishTransaction(began, id != 0);
	return id;
}



const vector<UILayout::CustomElement> &UILayout::CustomElements()
{
	return customElements;
}



const UILayout::CustomElement *UILayout::GetCustom(int id)
{
	return const_cast<UILayout::CustomElement *>(FindCustom(id));
}



bool UILayout::RemoveCustom(int id)
{
	const bool began = BeginTransaction();
	auto it = find_if(customElements.begin(), customElements.end(),
		[id](const CustomElement &element) { return element.id == id; });
	if(it == customElements.end())
	{
		FinishTransaction(began, false);
		return false;
	}

	customElements.erase(it);
	ClearRegions();
	const string key = CustomKey(id);
	styles.erase({"Layout", key});
	if(hasSelected && selected == LayoutKey{"Layout", key})
	{
		hasSelected = false;
		selected = {};
	}
	dirty = true;
	FinishTransaction(began, true);
	return true;
}



bool UILayout::SetCustomPosition(int id, const Point &position)
{
	const bool began = BeginTransaction();
	CustomElement *element = FindCustom(id);
	if(!element || !std::isfinite(position.X()) || !std::isfinite(position.Y()))
	{
		FinishTransaction(began, false);
		return false;
	}
	element->position = Point(
		NormalizePosition(position.X(), Screen::Left(), Screen::Width()),
		NormalizePosition(position.Y(), Screen::Top(), Screen::Height()));
	styles[{"Layout", CustomKey(id)}].offset = Point();
	ClearRegions();
	dirty = true;
	FinishTransaction(began, true);
	return true;
}



bool UILayout::SetCustomSize(int id, const Point &size)
{
	const bool began = BeginTransaction();
	CustomElement *element = FindCustom(id);
	if(!element || !std::isfinite(size.X()) || !std::isfinite(size.Y()) || size.X() <= 0. || size.Y() <= 0.)
	{
		FinishTransaction(began, false);
		return false;
	}
	element->size = Point(
		NormalizeSize(size.X(), Screen::Width()),
		NormalizeSize(size.Y(), Screen::Height()));
	styles[{"Layout", CustomKey(id)}].scale = Point(1., 1.);
	ClearRegions();
	dirty = true;
	FinishTransaction(began, true);
	return true;
}



bool UILayout::SetCustomColor(int id, const Color &color)
{
	const bool began = BeginTransaction();
	CustomElement *element = FindCustom(id);
	if(!element)
	{
		FinishTransaction(began, false);
		return false;
	}
	const float *rgba = color.Get();
	element->color.Load(
		ClampValue(rgba[0], 0., 1., 0.),
		ClampValue(rgba[1], 0., 1., 0.),
		ClampValue(rgba[2], 0., 1., 0.),
		ClampValue(rgba[3], 0., 1., 1.));
	styles[{"Layout", CustomKey(id)}].hasColor = false;
	ClearRegions();
	dirty = true;
	FinishTransaction(began, true);
	return true;
}



bool UILayout::SetCustomFontSize(int id, int fontSize)
{
	const bool began = BeginTransaction();
	CustomElement *element = FindCustom(id);
	if(!element || element->type != "text")
	{
		FinishTransaction(began, false);
		return false;
	}
	element->fontSize = max(1, min(72, fontSize));
	ClearRegions();
	dirty = true;
	FinishTransaction(began, true);
	return true;
}



bool UILayout::SetCustomOpacity(int id, double opacity)
{
	const bool began = BeginTransaction();
	CustomElement *element = FindCustom(id);
	if(!element || !std::isfinite(opacity))
	{
		FinishTransaction(began, false);
		return false;
	}
	const float *rgba = element->color.Get();
	element->color.Load(rgba[0], rgba[1], rgba[2], ClampValue(opacity, 0., 1., 1.));
	styles[{"Layout", CustomKey(id)}].hasColor = false;
	ClearRegions();
	dirty = true;
	FinishTransaction(began, true);
	return true;
}



bool UILayout::SetCustomValue(int id, const string &value)
{
	const bool began = BeginTransaction();
	CustomElement *element = FindCustom(id);
	if(!element || !IsPrintable(value)
			|| (element->type == "sprite"
				&& (!SpriteSet::Exists(value) || !SpriteSet::Get(value)->HasDimensions())))
	{
		FinishTransaction(began, false);
		return false;
	}
	element->value = value;
	ClearRegions();
	dirty = true;
	FinishTransaction(began, true);
	return true;
}



int UILayout::DuplicateCustom(int id)
{
	const bool began = BeginTransaction();
	const CustomElement *original = FindCustom(id);
	if(!original || customElements.size() >= 128
			|| nextCustomId <= 0 || nextCustomId == numeric_limits<int>::max())
	{
		FinishTransaction(began, false);
		return 0;
	}

	CustomElement duplicate = *original;
	duplicate.id = nextCustomId++;
	duplicate.position = Point(
		NormalizePosition((original->position.X() * Screen::Width()) + 16. - Screen::Left(),
			Screen::Left(), Screen::Width()),
		NormalizePosition((original->position.Y() * Screen::Height()) + 16. - Screen::Top(),
			Screen::Top(), Screen::Height()));
	customElements.push_back(std::move(duplicate));
	const auto originalStyle = styles.find({"Layout", CustomKey(id)});
	if(originalStyle != styles.end())
		styles[{"Layout", CustomKey(customElements.back().id)}] = originalStyle->second;
	ClearRegions();
	dirty = true;
	FinishTransaction(began, true);
	return customElements.back().id;
}



void UILayout::ClearRegions()
{
	regions.clear();
	if(++frame == 0)
		++frame;
}



void UILayout::Register(const string &panel, const string &element, const Rectangle &bounds)
{
	if(!IsValidKey(panel, element))
		return;

	for(Region &region : regions)
		if(region.frame == frame && region.panel == panel && region.element == element)
		{
			const Point topLeft(
				min(region.bounds.Left(), bounds.Left()),
				min(region.bounds.Top(), bounds.Top()));
			const Point bottomRight(
				max(region.bounds.Right(), bounds.Right()),
				max(region.bounds.Bottom(), bounds.Bottom()));
			region.bounds = Rectangle::WithCorners(topLeft, bottomRight);
			return;
		}
	regions.push_back({panel, element, bounds, frame});
}



const vector<UILayout::Region> &UILayout::Regions()
{
	return regions;
}



bool UILayout::IsEditing()
{
	return editing;
}



bool UILayout::IsDragging()
{
	return editing && dragging;
}



void UILayout::ToggleEditing()
{
	if(dragging)
		CancelDrag();
	editing = !editing;
	dragging = false;
	hasSelected = false;
	selected = {};
}



bool UILayout::BeginDrag(const Point &point)
{
	if(!editing)
		return false;

	auto select = [](const Region &region) {
		selected = {region.panel, region.element};
		hasSelected = true;
		dragging = true;
		dragOwnsTransaction = BeginTransaction();
		return true;
	};
	// Prefer exact hits so nearby dense controls do not steal a selection.
	for(auto it = regions.rbegin(); it != regions.rend(); ++it)
		if(it->frame == frame && it->bounds.Contains(point))
			return select(*it);
	for(auto it = regions.rbegin(); it != regions.rend(); ++it)
	{
		if(it->frame != frame)
			continue;
		const Rectangle &bounds = it->bounds;
		const Rectangle forgivingBounds(bounds.Center(), bounds.Dimensions() + Point(16., 16.));
		if(forgivingBounds.Contains(point))
			return select(*it);
	}
	return false;
}



bool UILayout::SelectNext(bool reverse)
{
	if(!editing)
		return false;

	vector<const Region *> selectable;
	for(const Region &region : regions)
		if(region.frame == frame)
			selectable.push_back(&region);
	if(selectable.empty())
		return false;

	size_t index;
	if(hasSelected)
	{
		index = selectable.size();
		for(size_t i = 0; i < selectable.size(); ++i)
			if(selected == LayoutKey{selectable[i]->panel, selectable[i]->element})
			{
				index = i;
				break;
			}
		if(reverse)
			index = index ? index - 1 : selectable.size() - 1;
		else
			index = (index + 1) % selectable.size();
	}
	else
		index = reverse ? selectable.size() - 1 : 0;
	selected = {selectable[index]->panel, selectable[index]->element};
	hasSelected = true;
	dragging = false;
	return true;
}



bool UILayout::Drag(const Point &delta)
{
	if(!editing || !dragging || !hasSelected)
		return false;

	const Point currentOffset = Apply(selected.first, selected.second, Point());
	SetOffset(selected.first, selected.second, currentOffset + delta);
	ClearRegions();
	return true;
}



bool UILayout::EndDrag()
{
	if(!editing || !dragging)
		return false;

	const bool ownsTransaction = dragOwnsTransaction;
	dragging = false;
	dragOwnsTransaction = false;
	if(ownsTransaction)
		CommitTransaction();
	else
		Save();
	return true;
}



void UILayout::CancelDrag()
{
	if(!dragging)
		return;
	const bool ownsTransaction = dragOwnsTransaction;
	dragging = false;
	dragOwnsTransaction = false;
	if(ownsTransaction)
		CommitTransaction();
	else
		Save();
	ClearRegions();
}



bool UILayout::ResetSelected()
{
	const bool began = BeginTransaction();
	if(!editing || !hasSelected)
	{
		FinishTransaction(began, false);
		return false;
	}

	Reset(selected.first, selected.second);
	ClearRegions();
	hasSelected = false;
	selected = {};
	FinishTransaction(began, true);
	return true;
}



bool UILayout::ScaleSelected(double factor)
{
	const bool began = BeginTransaction();
	if(!editing || !hasSelected || !std::isfinite(factor) || factor <= 0.)
	{
		FinishTransaction(began, false);
		return false;
	}

	auto it = styles.find(selected);
	const Point scale = it == styles.end() ? Point(1., 1.) : it->second.scale;
	SetScale(selected.first, selected.second, scale * factor);
	ClearRegions();
	FinishTransaction(began, true);
	return true;
}



bool UILayout::SetSelectedColor(const Color &color)
{
	const bool began = BeginTransaction();
	if(!editing || !hasSelected)
	{
		FinishTransaction(began, false);
		return false;
	}

	SetColor(selected.first, selected.second, color);
	FinishTransaction(began, true);
	return true;
}



bool UILayout::ToggleSelectedVisibility()
{
	const bool began = BeginTransaction();
	if(!editing || !hasSelected)
	{
		FinishTransaction(began, false);
		return false;
	}

	SetVisible(selected.first, selected.second, !IsVisible(selected.first, selected.second));
	FinishTransaction(began, true);
	return true;
}



bool UILayout::RemoveSelected()
{
	const bool began = BeginTransaction();
	if(!editing || !hasSelected)
	{
		FinishTransaction(began, false);
		return false;
	}

	int id = 0;
	for(const CustomElement &element : customElements)
		if(selected == LayoutKey{"Layout", CustomKey(element.id)})
		{
			id = element.id;
			break;
		}
	if(id)
	{
		RemoveCustom(id);
		ClearRegions();
		hasSelected = false;
		selected = {};
		FinishTransaction(began, true);
		return true;
	}
	FinishTransaction(began, false);
	return false;
}



bool UILayout::IsSelected(const string &panel, const string &element)
{
	return hasSelected && selected == LayoutKey{panel, element};
}



bool UILayout::IsValidKey(const string &panel, const string &element)
{
	return IsPrintable(panel) && IsPrintable(element);
}
