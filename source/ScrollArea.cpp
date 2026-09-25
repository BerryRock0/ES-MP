/* ScrollArea.cpp
Copyright (c) 2026 by Amazinite

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "ScrollArea.h"

#include "GameData.h"
#include "Preferences.h"
#include "RenderBuffer.h"
#include "ScrollBar.h"

using namespace std;



ScrollArea::ScrollArea()
{
	SetInterruptible(true);
	SetTrapAllEvents(false);
	SetIsFullScreen(false);
}



ScrollArea::ScrollArea(const Rectangle &r)
	: ScrollArea()
{
	ScrollArea::SetRect(r);
}



// Stub destructor, so that unique_ptrs are destructed in the correct scope.
ScrollArea::~ScrollArea()
{
}



void ScrollArea::SetRect(const Rectangle &r)
{
	normalRect = r;
	const Rectangle layoutRect = LayoutRectangle(r);
	position = layoutRect.Center();
	size = layoutRect.Dimensions();
	buffer.reset();
	scroll.SetDisplaySize(size.Y());
	scrollBar.displaySizeFraction = scroll.MaxValue()
		? scroll.DisplaySize() / scroll.MaxValue() : 0.;
	Invalidate();
}



void ScrollArea::Resize()
{
	SetRect(normalRect);
}



void ScrollArea::SetScrollbarOffset(int offset)
{
	scrollbarOffset = offset;
}



void ScrollArea::SetPointerOffset(int offset)
{
	pointerOffset = offset;
}



void ScrollArea::SnapToTop()
{
	scroll.Set(0., 0);
	SyncScroll(false);
}



void ScrollArea::SnapToBottom()
{
	scroll.Set(scroll.MaxValue(), 0);
	SyncScroll(false);
}



void ScrollArea::Validate(bool trailingBreak)
{
}



void ScrollArea::Draw()
{
	if(!LayoutIsVisible())
	{
		dragging = false;
		hovering = false;
		return;
	}
	RegisterLayoutRegion(Rectangle(position, size));
	if(!buffer)
		buffer = make_unique<RenderBuffer>(size);

	Validate(scrollHeightIncludesTrailingBreak);
	if(!bufferIsValid || !scroll.IsAnimationDone())
	{
		scroll.Step();

		auto target = buffer->SetTarget();
		Point topLeft(buffer->Left(), buffer->Top() - scroll.AnimatedValue());
		DrawText(topLeft);
		target.Deactivate();

		buffer->SetFadePadding(
			scroll.IsScrollAtMin() ? 0 : 20,
			scroll.IsScrollAtMax() ? 0 : 20
		);
		bufferIsValid = true;
	}
	buffer->Draw(position);

	if(scroll.Scrollable())
	{
		SyncScroll();
		scrollBar.Draw();
	}
}



void ScrollArea::DrawText(const Point &topLeft)
{
}



bool ScrollArea::Click(int x, int y, MouseButton button, int clicks)
{
	if(!LayoutIsVisible())
	{
		dragging = false;
		hovering = false;
		return false;
	}
	if(scroll.Scrollable() && scrollBar.SyncClick(scroll, x, y, button, clicks))
	{
		bufferIsValid = false;
		return true;
	}
	if(button != MouseButton::LEFT)
		return false;

	if(!buffer)
		return false;
	Rectangle bounds(position, {buffer->Width(), buffer->Height()});
	dragging = bounds.Contains(Point(x, y));
	return dragging;
}



bool ScrollArea::Drag(double dx, double dy)
{
	if(!LayoutIsVisible())
	{
		dragging = false;
		hovering = false;
		return false;
	}
	if(scrollBar.SyncDrag(scroll, dx, dy))
	{
		bufferIsValid = false;
		return true;
	}
	if(dragging)
	{
		scroll.Scroll(-dy, 0);
		bufferIsValid = false;
		return true;
	}
	return false;
}



bool ScrollArea::Release(int x, int y, MouseButton button)
{
	if(!LayoutIsVisible())
	{
		dragging = false;
		hovering = false;
		return false;
	}
	if(button != MouseButton::LEFT)
		return false;

	bool ret = dragging;
	dragging = false;
	return ret;
}



bool ScrollArea::Hover(int x, int y)
{
	if(!LayoutIsVisible())
	{
		dragging = false;
		hovering = false;
		return false;
	}
	scrollBar.Hover(x, y);

	if(!buffer)
		return false;
	Rectangle bounds(position, {buffer->Width(), buffer->Height()});
	hovering = bounds.Contains(Point(x, y));
	return hovering;
}



bool ScrollArea::Scroll(double dx, double dy)
{
	if(!LayoutIsVisible())
	{
		dragging = false;
		hovering = false;
		return false;
	}
	if(hovering)
	{
		scroll.Scroll(-dy * Preferences::ScrollSpeed());
		bufferIsValid = false;
	}
	return hovering;
}



void ScrollArea::SyncScroll(bool animated)
{
	if(!buffer)
		return;
	Point topRight(position + Point(buffer->Right() + scrollbarOffset, buffer->Top() + pointerOffset));
	Point bottomRight(position + Point(buffer->Right() + scrollbarOffset, buffer->Bottom() - pointerOffset));

	scrollBar.SyncFrom(scroll, topRight, bottomRight, animated);
}



void ScrollArea::Invalidate()
{
	bufferIsValid = false;
	contentsIsValid = false;
}
