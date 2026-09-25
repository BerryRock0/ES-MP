/* test_cheatcommand.cpp
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

#include "../../../source/CheatCommand.h"
#include "../../../source/GameModel.h"
#include "../../../source/NetworkSession.h"
#include "../../../source/PlayerInfo.h"
#include "../../../source/Screen.h"
#include "../../../source/UI.h"
#include "../../../source/UILayout.h"

#include <algorithm>



SCENARIO( "Cheat command recognition", "[CheatCommand][Parser]" ) {
	GIVEN( "known commands with optional local slash" ) {
		REQUIRE( CheatCommand::IsCommand("credits set 1000") );
		REQUIRE( CheatCommand::IsCommand("/god on") );
		REQUIRE( CheatCommand::IsCommand("/travel \"Alpha Centauri\"", true) );
		REQUIRE( CheatCommand::IsClearCommand("/clear") );
		REQUIRE( CheatCommand::IsCommand("/give ship \"My Ship\" 2") );
		REQUIRE( CheatCommand::IsCommand("/layout list") );
		REQUIRE( CheatCommand::IsCommand("/layout assets radar") );
		REQUIRE( CheatCommand::IsCommand("/layout undo") );
		REQUIRE( CheatCommand::IsCommand("/layout redo") );
	}
	GIVEN( "unknown commands and chat-only text" ) {
		REQUIRE( !CheatCommand::IsCommand("/not-a-command") );
		REQUIRE( !CheatCommand::IsClearCommand("/clear now") );
		REQUIRE( !CheatCommand::IsCommand("credits set 1000", true) );
		REQUIRE( !CheatCommand::IsCommand("hello") );
		REQUIRE( !CheatCommand::IsCommand("/condition set \"unterminated") );
	}
}



SCENARIO( "Multiplayer help is not sent as chat", "[CheatCommand][Network]" ) {
	GIVEN( "an offline session used as a parser boundary" ) {
		GameModel game;
		PlayerInfo player;
		NetworkSession session(game, player);

		WHEN( "a help request is submitted" ) {
			session.SendChat("/help");
			THEN( "the local help catalog is recorded" ) {
				REQUIRE( session.ChatLog().size() == CheatCommand::HelpLines().size() );
				REQUIRE( session.ChatLog().back().sender == "Cheats" );
			}
		}
		WHEN( "an unknown slash command is submitted" ) {
			const std::size_t before = session.ChatLog().size();
			session.SendChat("/not-a-command");
			THEN( "it is handled as a command instead of ordinary chat" ) {
				REQUIRE( session.ChatLog().size() == before + 1 );
				REQUIRE( session.ChatLog().back().sender == "Cheats" );
			}
		}
	}
}



SCENARIO( "Executing local economy and condition cheats", "[CheatCommand][Execute]" ) {
	GIVEN( "a loaded pilot and a game UI" ) {
		PlayerInfo player;
		REQUIRE( player.LoadNetworkPilot(
			"pilot \"\" \"\"\n"
			"\"original name\" \"\" \"\"\n"
			"date 16 11 3013\n"
			"start default\n"
			"\tdate 16 11 3013\n"
			"account\n"
			"\tcredits 1000\n"));
		UI gamePanels;

		WHEN( "credits and a primary condition are changed" ) {
			REQUIRE( CheatCommand::Execute(player, gamePanels, "/clear") == "Console cleared." );
			REQUIRE( CheatCommand::Execute(player, gamePanels, "/credits set 5000") == "Credits: 5,000 credits" );
			REQUIRE( CheatCommand::Execute(player, gamePanels, "/condition set test:cheat 7") == "test:cheat = 7" );
			REQUIRE( CheatCommand::Execute(player, gamePanels, "/god on") == "God mode enabled." );
			REQUIRE( player.Accounts().Credits() == 5000 );
			REQUIRE( player.Conditions().Get("test:cheat") == 7 );
			REQUIRE( player.Conditions().Get("cheat: god mode") == 1 );
		}
	}
}



SCENARIO( "UI layout offsets", "[UILayout]" ) {
	GIVEN( "a known screen size and a named element" ) {
		Screen::ScreenDimensionsGuard screen(800, 600);
		UILayout::Clear();
		UILayout::SetOffset("test panel", "input", Point(80., 60.));

		THEN( "the offset is applied to the normal position" ) {
			REQUIRE( UILayout::Has("test panel", "input") );
			REQUIRE( UILayout::Apply("test panel", "input", Point(100., 200.)) == Point(180., 260.) );
		}
		WHEN( "the element is reset" ) {
			UILayout::Reset("test panel", "input");
			THEN( "its normal position is restored" ) {
				REQUIRE( !UILayout::Has("test panel", "input") );
				REQUIRE( UILayout::Apply("test panel", "input", Point(100., 200.)) == Point(100., 200.) );
			}
		}
		WHEN( "a registered region is edited" ) {
			UILayout::Clear();
			UILayout::ClearRegions();
			UILayout::Register("test panel", "input", Rectangle(Point(100., 200.), Point(80., 30.)));
			UILayout::ToggleEditing();
			REQUIRE( UILayout::BeginDrag(Point(120., 210.)) );
			REQUIRE( UILayout::Drag(Point(10., 15.)) );
			UILayout::ToggleEditing();
			THEN( "the new offset is retained" ) {
				REQUIRE( UILayout::Apply("test panel", "input", Point(100., 200.)) == Point(110., 215.) );
			}
		}
		WHEN( "a drag is cancelled after focus loss" ) {
			UILayout::Clear();
			UILayout::ClearRegions();
			UILayout::Register("test panel", "input", Rectangle(Point(100., 200.), Point(80., 30.)));
			UILayout::ToggleEditing();
			REQUIRE( UILayout::BeginDrag(Point(120., 210.)) );
			UILayout::CancelDrag();
			THEN( "the editor is no longer stuck dragging" ) {
				REQUIRE( !UILayout::IsDragging() );
				UILayout::ToggleEditing();
			}
		}
		WHEN( "a completed drag is one undoable operation" ) {
			UILayout::Clear();
			UILayout::ClearRegions();
			UILayout::Register("test panel", "input", Rectangle(Point(100., 200.), Point(80., 30.)));
			UILayout::ToggleEditing();
			REQUIRE( UILayout::BeginDrag(Point(120., 210.)) );
			REQUIRE( UILayout::Drag(Point(10., 15.)) );
			REQUIRE( UILayout::Drag(Point(5., 5.)) );
			REQUIRE( UILayout::EndDrag() );
			REQUIRE( UILayout::CanUndo() );
			REQUIRE( UILayout::Undo() );
			REQUIRE( UILayout::Apply("test panel", "input", Point(100., 200.)) == Point(100., 200.) );
			REQUIRE( UILayout::CanRedo() );
			REQUIRE( UILayout::Redo() );
			REQUIRE( UILayout::Apply("test panel", "input", Point(100., 200.)) == Point(115., 220.) );
			UILayout::ToggleEditing();
		}
		WHEN( "regions are cycled without an initial selection" ) {
			UILayout::Clear();
			UILayout::ClearRegions();
			UILayout::Register("test panel", "first", Rectangle(Point(10., 20.), Point(20., 20.)));
			UILayout::Register("test panel", "second", Rectangle(Point(40., 20.), Point(20., 20.)));
			UILayout::ToggleEditing();
			REQUIRE( UILayout::SelectNext(false) );
			THEN( "the first region is selected" ) {
				REQUIRE( UILayout::IsSelected("test panel", "first") );
				UILayout::ToggleEditing();
			}
		}
	}
}



SCENARIO( "Layout styles and custom elements", "[UILayout]" ) {
	GIVEN( "a known screen size" ) {
		Screen::ScreenDimensionsGuard screen(800, 600);
		UILayout::Clear();

		WHEN( "a style is configured" ) {
			UILayout::SetScale("test panel", "label", Point(1.5, 2.));
			UILayout::SetColor("test panel", "label", Color(.25f, .5f, .75f));
			THEN( "scale and color are applied" ) {
				REQUIRE( UILayout::ApplyScale("test panel", "label", Point(10., 20.)) == Point(15., 40.) );
				const Color color = UILayout::ApplyColor("test panel", "label", Color(1.f));
				REQUIRE( color.Get()[0] == Catch::Approx(.25f) );
				REQUIRE( color.Get()[1] == Catch::Approx(.5f) );
				REQUIRE( color.Get()[2] == Catch::Approx(.75f) );
			}
		}
		WHEN( "a custom text element is added and edited" ) {
			const int id = UILayout::AddText("Custom", Point(100., 80.), Point(200., 40.), Color(1.f), 18);
			REQUIRE( id > 0 );
			REQUIRE( UILayout::GetCustom(id) );
			REQUIRE( UILayout::SetCustomSize(id, Point(300., 60.)) );
			REQUIRE( UILayout::SetCustomFontSize(id, 14) );
			REQUIRE( UILayout::SetCustomValue(id, "Updated") );
			REQUIRE( UILayout::SetCustomOpacity(id, .5) );
			REQUIRE( UILayout::SetCustomColor(id, Color(1.f, 0.f, 0.f)) );
			THEN( "the custom record is updated" ) {
				const UILayout::CustomElement *element = UILayout::GetCustom(id);
				REQUIRE( element );
				REQUIRE( element->value == "Updated" );
				REQUIRE( element->size.X() == Catch::Approx(300. / 800.) );
				REQUIRE( element->size.Y() == Catch::Approx(60. / 600.) );
				REQUIRE( element->color.Get()[0] == Catch::Approx(1.f) );
				REQUIRE( element->color.Get()[1] == Catch::Approx(0.f) );
			}
			const int duplicate = UILayout::DuplicateCustom(id);
			REQUIRE( duplicate > 0 );
			REQUIRE( UILayout::RemoveCustom(duplicate) );
			REQUIRE( UILayout::RemoveCustom(id) );
			REQUIRE( !UILayout::GetCustom(id) );

			REQUIRE( UILayout::Undo() );
			REQUIRE( UILayout::GetCustom(id) );
			const int nextId = UILayout::AddText("Next", Point(100., 80.), Point(200., 40.));
			REQUIRE( nextId > id );
		}
		UILayout::Clear();
	}
}



SCENARIO( "Cheat help command", "[CheatCommand][Help]" ) {
	GIVEN( "help aliases with optional whitespace and slash prefixes" ) {
		REQUIRE( CheatCommand::IsHelpCommand("/help") );
		REQUIRE( CheatCommand::IsHelpCommand(" /COMMANDS ") );
	}
	GIVEN( "a command that merely starts with help" ) {
		REQUIRE( !CheatCommand::IsHelpCommand("/helper") );
	}
	GIVEN( "the printable help catalog" ) {
		const auto lines = CheatCommand::HelpLines();
		REQUIRE( std::any_of(lines.begin(), lines.end(), [](const std::string &line) {
			return line.find("/help") != std::string::npos;
		}));
		REQUIRE( std::any_of(lines.begin(), lines.end(), [](const std::string &line) {
			return line.find("/commands") != std::string::npos;
		}));
		REQUIRE( std::any_of(lines.begin(), lines.end(), [](const std::string &line) {
			return line.find("/clear") != std::string::npos;
		}));
	}
}
