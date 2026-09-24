/* test_playerinfo.cpp
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

// Include only the tested class's header.
#include "../../../source/PlayerInfo.h"

// ... and any system includes needed for the test file.
#include <string>



namespace { // test namespace

// #region mock data

// Stripped-down pilot save texts. Real network pilots have empty names: the
// multiplayer start scenario assigns none, so PlayerInfo::SaveToString()
// serializes `pilot "" ""`. Restoring such a save must succeed and bring back
// the player's state (credits, ships, missions, conditions), not be discarded
// as if it were a corrupted single-player save.
const std::string NAMELESS_SAVE =
	"pilot \"\" \"\"\n"
	"\"original name\" \"\" \"\"\n"
	"date 16 11 3013\n"
	"start default\n"
	"\tdate 16 11 3013\n"
	"account\n"
	"\tcredits 1000\n";

// #endregion mock data



// #region unit tests
SCENARIO( "Loading an in-memory pilot save", "[PlayerInfo][Load][Network]" ) {
	GIVEN( "a pilot save whose pilot has an empty name" ) {
		PlayerInfo player;
		WHEN( "the pilot is restored from the save" ) {
			const bool restored = player.LoadNetworkPilot(NAMELESS_SAVE);
			THEN( "the restore succeeds" ) {
				REQUIRE( restored );
			}
			THEN( "the player's state is restored despite the empty name" ) {
				REQUIRE( player.Accounts().Credits() == 1000 );
			}
		}
	}
	GIVEN( "an empty pilot save" ) {
		PlayerInfo player;
		WHEN( "empty text is treated as a pilot save" ) {
			const bool restored = player.LoadNetworkPilot("");
			THEN( "the restore fails" ) {
				REQUIRE( !restored );
			}
		}
	}
}



SCENARIO( "Leaving a network session keeps the pilot loaded", "[PlayerInfo][Network][Leave]" ) {
	GIVEN( "an in-memory network pilot" ) {
		PlayerInfo player;
		player.SetNetworkMode(true);
		player.SetNetworkAttached(true);
		player.SetNetworkNickname("star pilot");
		WHEN( "the session ends (disconnect only drops the session link)" ) {
			player.SetNetworkMode(false);
			player.SetNetworkNickname("");
			THEN( "the pilot stays loaded so the menu keeps showing it" ) {
				REQUIRE( !player.IsNetworkMode() );
				REQUIRE( player.IsLoaded() );
				REQUIRE( player.IsNetworkAttached() );
			}
		}
	}
}
// #endregion unit tests

} // test namespace