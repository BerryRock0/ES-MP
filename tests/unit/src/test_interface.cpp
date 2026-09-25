/* test_interface.cpp
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

#include "es-test.hpp"

#include "../../../source/DataNode.h"
#include "../../../source/Interface.h"
#include "../../../source/Screen.h"
#include "../../../source/UILayout.h"

#include <string>

using namespace std;

SCENARIO( "Interface layout identities", "[Interface][UILayout]" ) {
	GIVEN( "a named point with an explicit layout ID" ) {
		Screen::ScreenDimensionsGuard screen(800, 600);
		UILayout::Clear();

		DataNode root;
		root.AddToken("interface");
		root.AddToken("test interface");

		DataNode point(&root);
		point.AddToken("point");
		point.AddToken("radar");

		DataNode center(&point);
		center.AddToken("center");
		center.AddToken("100");
		center.AddToken("100");
		point.AddChild(center);

		DataNode layout(&point);
		layout.AddToken("layout");
		layout.AddToken("radar-main");
		point.AddChild(layout);
		root.AddChild(point);

		Interface interface;
		interface.Load(root);

		WHEN( "the semantic style exists" ) {
			UILayout::SetOffset("Interface", "test interface#point/radar-main", Point(50., 0.));
			THEN( "the point uses the semantic style" ) {
				REQUIRE( interface.GetPoint("radar") == Point(150., 100.) );
			}
		}
		WHEN( "only the legacy style exists" ) {
			UILayout::SetOffset("Interface", "test interface#point", Point(20., 0.));
			THEN( "the point keeps the legacy style as a fallback" ) {
				REQUIRE( interface.GetPoint("radar") == Point(120., 100.) );
			}
		}
	}
}

SCENARIO( "UILayout legacy aliases migrate to semantic IDs", "[Interface][UILayout]" ) {
	Screen::ScreenDimensionsGuard screen(800, 600);
	UILayout::Clear();
	UILayout::SetOffset("Interface", "test interface#3", Point(12., 0.));
	UILayout::MigrateAliases({{"Interface", "test interface#element/stable", "test interface#3"}});

	REQUIRE( UILayout::Has("Interface", "test interface#element/stable") );
	REQUIRE_FALSE( UILayout::Has("Interface", "test interface#3") );
	REQUIRE( UILayout::Apply("Interface", "test interface#element/stable", Point()) == Point(12., 0.) );
}
