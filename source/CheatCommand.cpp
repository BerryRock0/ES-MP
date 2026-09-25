/* CheatCommand.cpp
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

#include "CheatCommand.h"

#include "Account.h"
#include "CargoHold.h"
#include "Color.h"
#include "ConditionsStore.h"
#include "DataNode.h"
#include "Date.h"
#include "Engine.h"
#include "text/Format.h"
#include "GameData.h"
#include "Government.h"
#include "MainPanel.h"
#include "Mission.h"
#include "Outfit.h"
#include "Planet.h"
#include "PlayerInfo.h"
#include "Port.h"
#include "RoutePlan.h"
#include "Screen.h"
#include "Ship.h"
#include "image/SpriteSet.h"
#include "StellarObject.h"
#include "System.h"
#include "UI.h"
#include "UILayout.h"

#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using namespace std;

namespace
{
	constexpr size_t MAX_COMMAND_LENGTH = 512;
	constexpr int MAX_ITEM_COUNT = 1000;
	constexpr int MAX_DATE_ADVANCE = 3650;
	constexpr double MAX_DAMAGE = 1.e12;
	constexpr int64_t MAX_CREDITS = numeric_limits<int64_t>::max();

	struct ParsedCommand
	{
		string name;
		vector<string> arguments;
	};

	bool IsKnownName(const string &name)
	{
		static const set<string> names = {
			"help", "commands", "info", "where", "world", "players", "date", "credits",
			"money", "give", "buy", "repair", "jump", "land", "travel", "previous",
			"clear", "rep", "reputation", "condition", "missions", "god", "damage", "kill",
			"selfkill", "spawn", "layout"
		};
		return names.contains(name);
	}

	bool Tokenize(const string &text, vector<string> &tokens, string &error)
	{
		tokens.clear();
		string current;
		bool hasToken = false;
		char quote = 0;
		bool escaped = false;
		for(unsigned char character : text)
		{
			if(character < 0x20 || character == 0x7f)
			{
				error = "Command contains a control character.";
				return false;
			}
			if(character == '\\' && quote)
			{
				escaped = !escaped;
				continue;
			}
			if(escaped)
			{
				current.push_back(static_cast<char>(character));
				escaped = false;
				hasToken = true;
				continue;
			}
			if(quote)
			{
				if(character == static_cast<unsigned char>(quote))
					quote = 0;
				else
					current.push_back(static_cast<char>(character));
				continue;
			}
			if(character == '\'' || character == '"')
			{
				quote = static_cast<char>(character);
				hasToken = true;
			}
			else if(isspace(character))
			{
				if(hasToken)
				{
					tokens.push_back(current);
					current.clear();
					hasToken = false;
				}
			}
			else
			{
				current.push_back(static_cast<char>(character));
				hasToken = true;
			}
		}
		if(escaped || quote)
		{
			error = "Command has an unterminated quote or escape.";
			return false;
		}
		if(hasToken)
			tokens.push_back(current);
		return true;
	}

	bool ParseCommand(const string &text, ParsedCommand &command, string &error, bool requireSlash)
	{
		command = ParsedCommand{};
		if(text.size() > MAX_COMMAND_LENGTH)
		{
			error = "Command is too long.";
			return false;
		}
		size_t first = 0;
		while(first < text.size() && isspace(static_cast<unsigned char>(text[first])))
			++first;
		if(requireSlash && (first == text.size() || text[first] != '/'))
		{
			error = "Multiplayer commands must start with '/'.";
			return false;
		}
		if(first < text.size() && text[first] == '/')
			++first;
		while(first < text.size() && isspace(static_cast<unsigned char>(text[first])))
			++first;
		vector<string> tokens;
		if(!Tokenize(text.substr(first), tokens, error) || tokens.empty())
		{
			if(error.empty())
				error = "Command is empty.";
			return false;
		}
		command.name = Format::LowerCase(tokens.front());
		command.arguments.assign(tokens.begin() + 1, tokens.end());
		if(!IsKnownName(command.name))
		{
			error = "Unknown command. Use /help for the command list.";
			return false;
		}
		return true;
	}

	bool ParseInt64(const string &text, int64_t &value)
	{
		if(text.empty())
			return false;
		const char *begin = text.data();
		const char *end = begin + text.size();
		auto result = from_chars(begin, end, value);
		return result.ec == errc{} && result.ptr == end;
	}

	bool ParseInt(const string &text, int &value, int maximum = numeric_limits<int>::max())
	{
		int64_t parsed = 0;
		if(!ParseInt64(text, parsed) || parsed < 0 || parsed > maximum)
			return false;
		value = static_cast<int>(parsed);
		return true;
	}

	bool ParseDouble(const string &text, double &value)
	{
		if(text.empty())
			return false;
		char *end = nullptr;
		errno = 0;
		value = strtod(text.c_str(), &end);
		return end == text.c_str() + text.size() && errno != ERANGE && isfinite(value);
	}

	bool ParseColor(const string &text, Color &color)
	{
		const string name = Format::LowerCase(text);
		if(name == "black")
			color = Color(0.f, 0.f, 0.f);
		else if(name == "white" || name == "bright")
			color = Color(1.f);
		else if(name == "red")
			color = Color(1.f, .2f, .2f);
		else if(name == "green")
			color = Color(.2f, 1.f, .2f);
		else if(name == "blue")
			color = Color(.2f, .4f, 1.f);
		else if(name == "yellow")
			color = Color(1.f, .85f, .2f);
		else if(name == "cyan")
			color = Color(.2f, .9f, 1.f);
		else if(name == "magenta")
			color = Color(1.f, .2f, .9f);
		else if(name == "orange")
			color = Color(1.f, .45f, .1f);
		else if(name == "medium" || name == "dim" || name == "active"
				|| name == "inactive" || name == "hover")
		{
			const Color *namedColor = GameData::Colors().Get(name);
			if(!namedColor)
				return false;
			color = *namedColor;
		}
		else
		{
			string hex = text;
			if(hex.starts_with('#'))
				hex.erase(hex.begin());
			if(hex.size() != 6 && hex.size() != 8)
				return false;
			char *end = nullptr;
			errno = 0;
			unsigned long value = strtoul(hex.c_str(), &end, 16);
			if(errno == ERANGE || !end || *end)
				return false;
			const auto byte = [value](int shift) { return static_cast<float>((value >> shift) & 0xff) / 255.f; };
			if(hex.size() == 6)
				color = Color(byte(16), byte(8), byte(0));
			else
				color = Color(byte(24), byte(16), byte(8), byte(0));
		}
		return true;
	}

	Ship *FindOwnedShip(PlayerInfo &player, const string &name)
	{
		Ship *match = nullptr;
		for(const shared_ptr<Ship> &ship : player.Ships())
		{
			if(ship->GivenName() == name || ship->TrueModelName() == name
					|| ship->DisplayModelName() == name)
			{
				if(match)
					return nullptr;
				match = ship.get();
			}
		}
		return match;
	}

	Ship *ResolveTarget(PlayerInfo &player, const string &name)
	{
		Ship *flagship = player.Flagship();
		if(!flagship)
			return nullptr;
		if(name.empty() || name == "target")
			return flagship->GetTargetShip() ? flagship->GetTargetShip().get() : flagship;
		if(name == "flagship")
			return flagship;
		return FindOwnedShip(player, name);
	}

	string PlayerName(const PlayerInfo &player)
	{
		string name = player.FirstName();
		if(!player.LastName().empty())
			name += " " + player.LastName();
		return name.empty() ? "Unnamed pilot" : name;
	}

	string LocationInfo(const PlayerInfo &player)
	{
		ostringstream out;
		out << "Pilot: " << PlayerName(player) << "; date: " << player.GetDate().ToString()
			<< "; credits: " << Format::CreditString(player.Accounts().Credits());
		if(const System *system = player.GetSystem())
			out << "; system: " << system->DisplayName();
		else
			out << "; system: unknown";
		if(const Planet *planet = player.GetPlanet())
			out << "; planet: " << planet->DisplayName();
		else
			out << "; planet: in flight";
		out << "; ships: " << player.Ships().size();
		if(const Ship *flagship = player.Flagship())
			out << "; flagship: " << flagship->GivenName();
		return out.str();
	}

	string ExecuteCredits(PlayerInfo &player, const vector<string> &arguments)
	{
		int64_t current = player.Accounts().Credits();
		if(arguments.empty())
			return "Credits: " + Format::CreditString(current);

		string mode = Format::LowerCase(arguments[0]);
		const bool explicitMode = mode == "add" || mode == "set";
		if(!explicitMode)
		{
			mode = "add";
			if(arguments.size() != 1)
				return "Usage: /credits [add|set] <amount>";
		}
		else if(arguments.size() != 2)
			return "Usage: /credits [add|set] <amount>";
		const string amountText = arguments[explicitMode ? 1 : 0];
		int64_t amount = 0;
		if(!ParseInt64(amountText, amount))
			return "Usage: /credits [add|set] <amount>";

		int64_t next = 0;
		if(mode == "set")
		{
			if(amount < 0)
				return "Credit amount is outside the supported range.";
			next = amount;
		}
		else if(amount > 0)
		{
			if(current > MAX_CREDITS - amount)
				return "Credit amount is outside the supported range.";
			next = current + amount;
		}
		else
		{
			if(amount == numeric_limits<int64_t>::min() || current < -amount)
				return "Credit amount is outside the supported range.";
			next = current + amount;
		}
		player.Accounts().AddCredits(next - current);
		return "Credits: " + Format::CreditString(player.Accounts().Credits());
	}

	string ExecuteGive(PlayerInfo &player, MainPanel *mainPanel, const vector<string> &arguments)
	{
		if(arguments.size() < 2 || arguments.size() > 3)
			return "Usage: /give ship|outfit <true name> [count]";
		const string kind = Format::LowerCase(arguments[0]);
		int count = 1;
		if(arguments.size() >= 3 && (!ParseInt(arguments[2], count, MAX_ITEM_COUNT) || !count))
			return "Count must be between 1 and 1000.";
		if(kind == "ship")
		{
			const Ship *model = GameData::Ships().Find(arguments[1]);
			if(!model)
				return "Unknown ship: " + arguments[1];
			for(int i = 0; i < count; ++i)
				player.GiftShip(model, "", "");
			if(mainPanel && !player.GetPlanet() && player.GetSystem())
				mainPanel->GetEngine().Place();
			if(player.Conditions().Get("cheat: god mode"))
				for(const shared_ptr<Ship> &ship : player.Ships())
					ship->SetGodMode(true);
			return "Gave " + to_string(count) + " " + model->DisplayModelName() + (count == 1 ? "." : "s.");
		}
		if(kind != "outfit")
			return "Usage: /give ship|outfit <true name> [count]";
		const Outfit *outfit = GameData::Outfits().Find(arguments[1]);
		if(!outfit)
			return "Unknown outfit: " + arguments[1];
		if(Ship *ship = player.Flagship())
		{
			if(ship->Attributes().CanAdd(*outfit, count) < count)
				return "The flagship cannot carry that many outfits.";
			ship->AddOutfit(outfit, count);
		}
		else if(player.GetPlanet())
		{
			if(player.Cargo().Add(outfit, count) < count)
				return "There is not enough cargo space.";
		}
		else
			return "Give outfit requires a flagship or a landed pilot.";
		return "Gave " + to_string(count) + " " + outfit->DisplayName() + (count == 1 ? "." : "s.");
	}

	string ExecuteBuy(PlayerInfo &player, const vector<string> &arguments)
	{
		if(!player.GetPlanet())
			return "Buying requires a landed pilot.";
		if(arguments.size() < 2 || arguments.size() > 3)
			return "Usage: /buy ship|outfit <true name> [count]";
		int count = 1;
		if(arguments.size() >= 3 && (!ParseInt(arguments[2], count, MAX_ITEM_COUNT) || !count))
			return "Count must be between 1 and 1000.";
		const string kind = Format::LowerCase(arguments[0]);
		if(kind == "ship")
		{
			const Ship *model = GameData::Ships().Find(arguments[1]);
			if(!model)
				return "Unknown ship: " + arguments[1];
			for(int i = 0; i < count; ++i)
				if(!player.BuyShip(model, ""))
					return "Not enough credits or stock for " + model->DisplayModelName() + ".";
			if(player.Conditions().Get("cheat: god mode"))
				for(const shared_ptr<Ship> &ship : player.Ships())
					ship->SetGodMode(true);
			return "Bought " + to_string(count) + " " + model->DisplayModelName() + (count == 1 ? "." : "s.");
		}
		if(kind != "outfit")
			return "Usage: /buy ship|outfit <true name> [count]";
		const Outfit *outfit = GameData::Outfits().Find(arguments[1]);
		if(!outfit)
			return "Unknown outfit: " + arguments[1];
		const int64_t unitCost = outfit->Cost();
		if(unitCost < 0 || (unitCost && count > MAX_CREDITS / unitCost))
			return "Outfit price is outside the supported range.";
		int64_t cost = unitCost * count;
		if(player.Accounts().Credits() < cost)
			return "Not enough credits for that outfit.";
		Ship *ship = player.Flagship();
		if(ship)
		{
			if(ship->Attributes().CanAdd(*outfit, count) < count)
				return "The flagship cannot carry that many outfits.";
			ship->AddOutfit(outfit, count);
		}
		else if(player.Cargo().Add(outfit, count) < count)
			return "There is not enough cargo space.";
		player.Accounts().AddCredits(-cost);
		return "Bought " + to_string(count) + " " + outfit->DisplayName() + (count == 1 ? "." : "s.");
	}

	string ExecuteRepair(PlayerInfo &player, const vector<string> &arguments)
	{
		if(arguments.size() > 1 || (!arguments.empty() && Format::LowerCase(arguments[0]) != "all"))
			return "Usage: /repair [all]";
		vector<Ship *> ships;
		if(arguments.empty())
		{
			if(Ship *ship = player.Flagship())
				ships.push_back(ship);
		}
		else
			for(const shared_ptr<Ship> &ship : player.Ships())
				if(!ship->IsDestroyed())
					ships.push_back(ship.get());
		if(ships.empty())
			return "There are no ships to repair.";
		for(Ship *ship : ships)
			ship->Recharge(Port::RechargeType::All, true);
		return "Repaired " + to_string(ships.size()) + " ship(s).";
	}

	string ExecuteJump(PlayerInfo &player, MainPanel *mainPanel, const vector<string> &arguments)
	{
		if(arguments.size() != 1)
			return "Usage: /jump <system>";
		if(!mainPanel || !player.Flagship())
			return "Jump requires an active flight and flagship.";
		const System *target = GameData::Systems().Find(arguments[0]);
		if(!target)
			return "Unknown system: " + arguments[0];
		if(player.GetSystem() == target)
			return "The flagship is already in that system.";
		player.TravelPlan().clear();
		player.SetTravelDestination(nullptr);
		player.SetSystem(*target);
		player.SetPlanet(nullptr);
		for(const shared_ptr<Ship> &ship : player.Ships())
		{
			ship->SetSystem(target);
			ship->SetPlanet(nullptr);
			ship->SetPosition(Point());
			ship->SetVelocity(Point());
			ship->SetTargetSystem(nullptr);
			ship->SetTargetStellar(nullptr);
			ship->SetCommands(Command());
		}
		mainPanel->GetEngine().Place();
		return "Jumped to " + target->DisplayName() + ".";
	}

	string ExecuteTravel(PlayerInfo &player, MainPanel *mainPanel, const vector<string> &arguments)
	{
		if(arguments.size() > 1)
			return "Usage: /travel [system]";
		if(arguments.empty())
		{
			if(!player.HasTravelPlan())
				return "No travel plan is set.";
			string result = "Travel plan:";
			for(const System *system : player.TravelPlan())
				result += " " + system->DisplayName();
			return result + ".";
		}
		if(!mainPanel || !player.Flagship() || !player.GetSystem())
			return "Travel requires an active flight and flagship.";
		const System *target = GameData::Systems().Find(arguments[0]);
		if(!target)
			return "Unknown system: " + arguments[0];
		if(player.GetSystem() == target)
		{
			player.TravelPlan().clear();
			player.SetTravelDestination(nullptr);
			player.Flagship()->SetTargetSystem(nullptr);
			return "Cleared the travel plan.";
		}
		RoutePlan route(*player.Flagship(), *target, &player);
		if(!route.HasRoute())
			return "No navigable route to " + target->DisplayName() + ".";
		player.SetTravelDestination(nullptr);
		player.Flagship()->SetTargetSystem(nullptr);
		player.TravelPlan() = route.Plan();
		string result = "Travel plan:";
		for(const System *system : player.TravelPlan())
			result += " " + system->DisplayName();
		return result + ".";
	}



	string ExecuteLand(PlayerInfo &player, MainPanel *mainPanel, const vector<string> &arguments)
	{
		if(arguments.size() != 1)
			return "Usage: /land <planet>";
		if(!mainPanel || !player.Flagship() || !player.GetSystem())
			return "Land requires an active flight and flagship.";
		const Planet *planet = GameData::Planets().Find(arguments[0]);
		if(!planet)
			return "Unknown planet: " + arguments[0];
		if(planet->IsWormhole())
			return "Cannot land on a wormhole.";
		if(planet->GetSystem() != player.GetSystem())
			return "That planet is not in the current system.";
		const StellarObject *stellar = player.GetSystem()->FindStellar(planet);
		if(!stellar)
			return "That planet cannot be landed on.";
		player.TravelPlan().clear();
		player.SetPlanet(planet);
		player.SetTravelDestination(planet);
		Ship *flagship = player.Flagship();
		flagship->SetTargetSystem(nullptr);
		flagship->SetSystem(player.GetSystem());
		flagship->SetPlanet(planet);
		flagship->SetPosition(stellar->Position());
		flagship->SetVelocity(Point());
		// Do not call Engine::Place() here: Place() is the take-off path and
		// deliberately clears PlayerInfo::planet. MainPanel will run the normal
		// landing callback on its next step, after this console/chat panel closes.
		return "Landing at " + planet->DisplayName() + ".";
	}

	string ExecuteDate(PlayerInfo &player, const vector<string> &arguments)
	{
		if(arguments.empty())
			return "Date: " + player.GetDate().ToString();
		if(arguments.size() != 1)
			return "Usage: /date [YYYY-MM-DD]";
		int day = 0;
		int month = 0;
		int year = 0;
		char extra = 0;
		if(sscanf(arguments[0].c_str(), "%d-%d-%d%c", &day, &month, &year, &extra) != 3
				|| day < 1 || month < 1 || month > 12 || year < 1 || year > 9999)
			return "Date must be a valid YYYY-MM-DD value.";
		Date target(day, month, year);
		if(target.Day() != day || target.Month() != month || target.Year() != year)
			return "Date must be a valid YYYY-MM-DD value.";
		if(target <= player.GetDate())
			return "The date can only be advanced forward.";
		const int days = target - player.GetDate();
		if(days > MAX_DATE_ADVANCE)
			return "The date cannot be advanced more than 10 years at once.";
		player.AdvanceDate(days);
		GameData::SetDate(player.GetDate());
		return "Date advanced to " + player.GetDate().ToString() + ".";
	}

	string ExecuteReputation(const vector<string> &arguments)
	{
		if(arguments.size() < 2)
			return "Usage: /rep get|set|add <government> <value>";
		const string mode = Format::LowerCase(arguments[0]);
		const Government *government = GameData::Governments().Find(arguments[1]);
		if(!government)
			return "Unknown government: " + arguments[1];
		if(mode == "get")
		{
			if(arguments.size() != 2)
				return "Usage: /rep get <government>";
			return "Reputation with " + government->DisplayName() + ": " + Format::Number(government->Reputation());
		}
		if(arguments.size() != 3)
			return "Usage: /rep set|add <government> <value>";
		double value = 0.;
		if(!ParseDouble(arguments[2], value))
			return "Reputation value must be a finite number.";
		if(mode == "set")
			government->SetReputation(value);
		else if(mode == "add")
			government->AddReputation(value);
		else
			return "Usage: /rep get|set|add <government> <value>";
		return "Reputation with " + government->DisplayName() + ": " + Format::Number(government->Reputation());
	}

	string ExecuteCondition(PlayerInfo &player, const vector<string> &arguments)
	{
		if(arguments.size() < 2)
			return "Usage: /condition get|set|add|clear <name> [value]";
		const string mode = Format::LowerCase(arguments[0]);
		const string &name = arguments[1];
		if(!DataNode::IsConditionName(name))
			return "Invalid condition name.";
		if(mode == "get")
		{
			if(arguments.size() != 2)
				return "Usage: /condition get <name>";
			return name + " = " + to_string(player.Conditions().Get(name));
		}
		int64_t value = 0;
		int64_t before = player.Conditions().Get(name);
		if(mode == "clear")
		{
			if(arguments.size() != 2)
				return "Usage: /condition clear <name>";
			player.Conditions().Set(name, 0);
		}
		else
		{
			if(arguments.size() != 3 || !ParseInt64(arguments[2], value))
				return "Usage: /condition set|add <name> <integer>";
			if(mode == "set")
				player.Conditions().Set(name, value);
			else if(mode == "add")
			{
				if((value > 0 && before > numeric_limits<int64_t>::max() - value)
						|| (value < 0 && before < numeric_limits<int64_t>::min() - value))
					return "Condition value is outside the supported range.";
				player.Conditions().Add(name, value);
			}
			else
				return "Usage: /condition get|set|add|clear <name> [value]";
		}
		int64_t actual = player.Conditions().Get(name);
		int64_t expected = mode == "clear" ? 0 : mode == "set" ? value : before + value;
		if(actual != expected)
			return "Condition is read-only or was rejected.";
		return name + " = " + to_string(actual);
	}

	string ExecuteMissions(PlayerInfo &player, UI &gamePanels, const vector<string> &arguments)
	{
		if(arguments.size() != 1 || Format::LowerCase(arguments[0]) != "clear")
			return "Usage: /missions clear";
		while(!player.Missions().empty())
			player.RemoveMission(Mission::ABORT, player.Missions().front(), gamePanels);
		return "Cleared active missions.";
	}

	string ExecuteGod(PlayerInfo &player, const vector<string> &arguments)
	{
		if(arguments.size() > 1)
			return "Usage: /god [on|off]";
		const string mode = arguments.empty() ? "" : Format::LowerCase(arguments[0]);
		int64_t desired = 0;
		if(mode == "on")
			desired = 1;
		else if(mode == "off")
			desired = 0;
		else if(!mode.empty())
			return "God mode must be on or off.";
		else
			desired = player.Conditions().Get("cheat: god mode") ? 0 : 1;
		player.Conditions().Set("cheat: god mode", desired);
		if(player.Conditions().Get("cheat: god mode") != desired)
			return "God mode condition was rejected.";
		const bool enabled = player.Conditions().Get("cheat: god mode") != 0;
		for(const shared_ptr<Ship> &ship : player.Ships())
			ship->SetGodMode(enabled);
		return enabled ? "God mode enabled." : "God mode disabled.";
	}

	string ExecuteDamage(PlayerInfo &player, const vector<string> &arguments)
	{
		if(arguments.empty() || arguments.size() > 2)
			return "Usage: /damage <amount> [target]";
		double amount = 0.;
		if(!ParseDouble(arguments[0], amount) || amount <= 0. || amount > MAX_DAMAGE)
			return "Damage amount must be a positive finite number no greater than 1e12.";
		Ship *target = ResolveTarget(player, arguments.size() == 2 ? arguments[1] : "");
		if(!target || target->IsDestroyed())
			return "No valid target was found.";
		target->ApplyCheatDamage(amount);
		return "Applied " + Format::Number(amount) + " damage to " + target->GivenName() + ".";
	}

	string ExecuteKill(PlayerInfo &player, const vector<string> &arguments)
	{
		if(arguments.size() > 1)
			return "Usage: /kill [target|all]";
		const bool all = arguments.size() == 1 && Format::LowerCase(arguments[0]) == "all";
		if(!all)
		{
			Ship *target = ResolveTarget(player, arguments.empty() ? "" : arguments[0]);
			if(!target || target->IsDestroyed())
				return "No valid target was found.";
			target->Destroy();
			return "Destroyed " + target->GivenName() + ".";
		}
		int count = 0;
		for(const shared_ptr<Ship> &ship : player.Ships())
			if(!ship->IsDestroyed())
			{
				ship->Destroy();
				++count;
			}
		return "Destroyed " + to_string(count) + " owned ship(s).";
	}

	string ExecuteSpawn(PlayerInfo &player, MainPanel *mainPanel, const vector<string> &arguments)
	{
		if(arguments.empty() || arguments.size() > 2)
			return "Usage: /spawn <ship> [count]";
		const Ship *model = GameData::Ships().Find(arguments[0]);
		if(!model)
			return "Unknown ship: " + arguments[0];
		int count = 1;
		if(arguments.size() == 2 && (!ParseInt(arguments[1], count, MAX_ITEM_COUNT) || !count))
			return "Count must be between 1 and 1000.";
		for(int i = 0; i < count; ++i)
			player.GiftShip(model, "", "");
		if(mainPanel && !player.GetPlanet() && player.GetSystem())
			mainPanel->GetEngine().Place();
		if(player.Conditions().Get("cheat: god mode"))
			for(const shared_ptr<Ship> &ship : player.Ships())
				ship->SetGodMode(true);
		return "Spawned " + to_string(count) + " " + model->DisplayModelName() + (count == 1 ? "." : "s.");
	}



	string ExecuteLayout(const vector<string> &arguments)
	{
		if(arguments.empty() || Format::LowerCase(arguments.front()) == "list")
		{
			ostringstream out;
			out << "Layout elements:";
			if(UILayout::CustomElements().empty())
				out << " none.";
			else
				for(const UILayout::CustomElement &element : UILayout::CustomElements())
					out << " #" << element.id << " " << element.type << " \"" << element.value << "\" "
						<< static_cast<int>(element.size.X() * Screen::Width()) << "x"
						<< static_cast<int>(element.size.Y() * Screen::Height());
			return out.str();
		}

		const string mode = Format::LowerCase(arguments.front());
		if((mode == "assets" || mode == "sprites") && arguments.size() <= 2)
		{
			const string filter = arguments.size() == 2 ? Format::LowerCase(arguments[1]) : string();
			vector<string> matches;
			for(const string &name : SpriteSet::Names())
				if(filter.empty() || Format::LowerCase(name).find(filter) != string::npos)
					matches.push_back(name);

			ostringstream out;
			out << "Sprite assets: " << matches.size() << " match" << (matches.size() == 1 ? "" : "es") << ".";
			for(size_t i = 0; i < min<size_t>(100, matches.size()); ++i)
				out << "\n  " << matches[i];
			if(matches.size() > 100)
				out << "\n  ... " << (matches.size() - 100) << " more";
			return out.str();
		}
		if(mode == "add")
		{
			if(arguments.size() < 3)
				return "Usage: /layout add sprite|text <name-or-text> [x y [width height]] [color]";
			const string kind = Format::LowerCase(arguments[1]);
			if(kind != "sprite" && kind != "text")
				return "Layout element type must be sprite or text.";
			if(arguments.size() != 3 && arguments.size() != 5 && arguments.size() != 7 && arguments.size() != 8)
				return "Usage: /layout add sprite|text <name-or-text> [x y [width height]] [color]";

			Point position = Screen::TopLeft() + Point(100., 100.);
			Point size = kind == "text" ? Point(240., 40.) : Point(128., 128.);
			Color color(1.f);
			if(arguments.size() >= 5)
			{
				double x = 0.;
				double y = 0.;
				if(!ParseDouble(arguments[3], x) || !ParseDouble(arguments[4], y))
					return "Layout position must contain numeric x and y values.";
				position = Point(x, y);
			}
			if(arguments.size() >= 7)
			{
				double width = 0.;
				double height = 0.;
				if(!ParseDouble(arguments[5], width) || !ParseDouble(arguments[6], height)
						|| width <= 0. || height <= 0.)
					return "Layout width and height must be positive numbers.";
				size = Point(width, height);
			}
			if(arguments.size() == 8 && !ParseColor(arguments[7], color))
				return "Unknown color. Use a name or #RRGGBB/#RRGGBBAA.";

			int id = kind == "sprite"
				? UILayout::AddSprite(arguments[2], position, size, color)
				: UILayout::AddText(arguments[2], position, size, color);
			if(!id)
				return "Could not add the layout element.";
			return "Added layout element #" + to_string(id) + ".";
		}

		if(mode == "undo" && arguments.size() == 1)
			return UILayout::Undo() ? "Undid the last layout change." : "There is no layout change to undo.";
		if(mode == "redo" && arguments.size() == 1)
			return UILayout::Redo() ? "Redid the last layout change." : "There is no layout change to redo.";
		if(mode == "reset" && arguments.size() == 2 && Format::LowerCase(arguments[1]) == "all")
		{
			UILayout::ClearAll();
			return "Cleared all custom and built-in UI layout changes.";
		}
		if(mode == "remove" && arguments.size() == 2)
		{
			int id = 0;
			if(!ParseInt(arguments[1], id, 1000000) || !UILayout::RemoveCustom(id))
				return "Unknown layout element.";
			return "Removed layout element #" + to_string(id) + ".";
		}
		if(mode == "clear" && arguments.size() == 1)
		{
			const vector<UILayout::CustomElement> elements = UILayout::CustomElements();
			const bool began = UILayout::BeginTransaction();
			bool changed = false;
			for(const UILayout::CustomElement &element : elements)
				changed = UILayout::RemoveCustom(element.id) || changed;
			if(began)
			{
				if(changed)
					UILayout::CommitTransaction();
				else
					UILayout::AbortTransaction();
			}
			return changed ? "Removed all custom layout elements." : "There are no custom layout elements.";
		}
		if((mode == "position" || mode == "size") && arguments.size() == 4)
		{
			int id = 0;
			double x = 0.;
			double y = 0.;
			if(!ParseInt(arguments[1], id, 1000000) || !ParseDouble(arguments[2], x) || !ParseDouble(arguments[3], y)
					|| (mode == "size" && (x <= 0. || y <= 0.)))
				return "Usage: /layout " + mode + " <id> <x> <y>";
			const bool changed = mode == "position"
				? UILayout::SetCustomPosition(id, Point(x, y))
				: UILayout::SetCustomSize(id, Point(x, y));
			if(!changed)
				return "Unknown layout element.";
			return "Updated layout element #" + to_string(id) + ".";
		}
		if(mode == "color" && arguments.size() == 3)
		{
			int id = 0;
			Color color;
			if(!ParseInt(arguments[1], id, 1000000) || !ParseColor(arguments[2], color)
					|| !UILayout::SetCustomColor(id, color))
				return "Usage: /layout color <id> <color>";
			return "Updated layout element #" + to_string(id) + " color.";
		}
		if(mode == "set" && arguments.size() == 3)
		{
			int id = 0;
			if(!ParseInt(arguments[1], id, 1000000) || !UILayout::SetCustomValue(id, arguments[2]))
				return "Usage: /layout set <id> <sprite-or-text>";
			return "Updated layout element #" + to_string(id) + " value.";
		}
		if(mode == "opacity" && arguments.size() == 3)
		{
			int id = 0;
			double opacity = 0.;
			if(!ParseInt(arguments[1], id, 1000000) || !ParseDouble(arguments[2], opacity)
					|| opacity < 0. || opacity > 1. || !UILayout::SetCustomOpacity(id, opacity))
				return "Usage: /layout opacity <id> <0-1>";
			return "Updated layout element #" + to_string(id) + " opacity.";
		}
		if(mode == "duplicate" && arguments.size() == 2)
		{
			int id = 0;
			if(!ParseInt(arguments[1], id, 1000000))
				return "Usage: /layout duplicate <id>";
			const int duplicate = UILayout::DuplicateCustom(id);
			if(!duplicate)
				return "Could not duplicate the layout element.";
			return "Duplicated layout element #" + to_string(id) + " as #" + to_string(duplicate) + ".";
		}
		if(mode == "font" && arguments.size() == 3)
		{
			int id = 0;
			int fontSize = 0;
			if(!ParseInt(arguments[1], id, 1000000) || !ParseInt(arguments[2], fontSize, 72)
					|| !fontSize || !UILayout::SetCustomFontSize(id, fontSize))
				return "Usage: /layout font <id> <size>";
			return "Updated layout element #" + to_string(id) + " font size.";
		}
		if((mode == "show" || mode == "hide") && arguments.size() == 2)
		{
			int id = 0;
			if(!ParseInt(arguments[1], id, 1000000) || !UILayout::GetCustom(id))
				return "Unknown layout element.";
			UILayout::SetVisible("Layout", "#" + to_string(id), mode == "show");
			return (mode == "show" ? "Shown" : "Hidden") + string(" layout element #") + to_string(id) + ".";
		}
		return "Usage: /layout list|assets [filter]|add|remove|duplicate|clear|undo|redo|reset|"
			"position|size|font|color|opacity|set|show|hide ...";
	}
}



bool CheatCommand::IsCommand(const string &text, bool requireSlash)
{
	ParsedCommand command;
	string error;
	return ParseCommand(text, command, error, requireSlash);
}



bool CheatCommand::IsHelpCommand(const string &text)
{
	ParsedCommand command;
	string error;
	return ParseCommand(text, command, error, false)
		&& (command.name == "help" || command.name == "commands");
}



bool CheatCommand::IsClearCommand(const string &text)
{
	ParsedCommand command;
	string error;
	return ParseCommand(text, command, error, false)
		&& command.name == "clear" && command.arguments.empty();
}



vector<string> CheatCommand::HelpLines()
{
	return {
		"Cheat commands:",
		"  Multiplayer commands affect your pilot and are not sent as chat.",
		"  /help, /commands - Show this help.",
		"  /clear - Clear the console output.",
		"  /info, /where - Show pilot and location information.",
		"  /world - Show location, date, and travel plan.",
		"  /previous - Show the previous system and planet.",
		"  /travel [system] - Show or set a route to a system.",
		"  /players - Show the local pilot context.",
		"  /credits [add|set] <amount> - Show or change credits (/money).",
		"  /give ship|outfit <name> [count] - Grant ships or outfits.",
		"  /buy ship|outfit <name> [count] - Buy while landed.",
		"  /repair [all] - Repair the flagship or fleet.",
		"  /jump <system> - Teleport to a system.",
		"  /land <planet> - Land at a planet in the current system.",
		"  /date [YYYY-MM-DD] - Show or advance the date.",
		"  /rep get <government> | set|add <government> <value> - Edit reputation (/reputation).",
		"  /condition get <name> | set|add <name> <value> | clear <name> - Edit conditions.",
		"  /missions clear - Abort active missions.",
		"  /god [on|off] - Toggle persistent god mode.",
		"  /damage <amount> [target] - Apply direct hull damage (even in god mode).",
		"  /kill [target|all] - Destroy a ship or the owned fleet.",
		"  /selfkill - Destroy the flagship.",
		"  /spawn <ship> [count] - Add ships to the active fleet.",
		"  /layout list | assets [filter] | add sprite|text <value> [x y [width height]] [color] - "
		"Manage custom UI elements.",
		"  /layout remove|duplicate|position|size|font|color|opacity|set|show|hide <id> ... - Edit a custom UI element.",
		"  /layout undo|redo - Undo or redo the last layout change.",
		"  /layout reset all - Remove all saved layout changes."
	};
}



string CheatCommand::Execute(PlayerInfo &player, UI &gamePanels, const string &text, MainPanel *mainPanel)
{
	ParsedCommand command;
	string error;
	if(!ParseCommand(text, command, error, false))
		return error;
	if(command.name == "clear")
		return command.arguments.empty() ? "Console cleared." : "Usage: /clear";
	if(command.name == "help" || command.name == "commands")
		return HelpLines().front();
	if(command.name == "info" || command.name == "where")
		return LocationInfo(player);
	if(command.name == "world")
	{
		string result = LocationInfo(player);
		if(player.HasTravelPlan())
		{
			result += "; travel:";
			for(const System *system : player.TravelPlan())
				result += " " + system->TrueName();
		}
		return result;
	}
	if(command.name == "players")
		return "Local pilot: " + PlayerName(player);
	if(command.name == "credits" || command.name == "money")
		return ExecuteCredits(player, command.arguments);
	if(command.name == "give")
		return ExecuteGive(player, mainPanel, command.arguments);
	if(command.name == "buy")
		return ExecuteBuy(player, command.arguments);
	if(command.name == "repair")
		return ExecuteRepair(player, command.arguments);
	if(command.name == "jump")
		return ExecuteJump(player, mainPanel, command.arguments);
	if(command.name == "travel")
		return ExecuteTravel(player, mainPanel, command.arguments);
	if(command.name == "previous")
	{
		string result = "Previous location:";
		if(const System *system = player.GetPreviousSystem())
			result += " " + system->DisplayName();
		else
			result += " unknown";
		if(const Planet *planet = player.GetPreviousPlanet())
			result += ", " + planet->DisplayName();
		return result + ".";
	}
	if(command.name == "land")
		return ExecuteLand(player, mainPanel, command.arguments);
	if(command.name == "date")
		return ExecuteDate(player, command.arguments);
	if(command.name == "rep" || command.name == "reputation")
		return ExecuteReputation(command.arguments);
	if(command.name == "condition")
		return ExecuteCondition(player, command.arguments);
	if(command.name == "missions")
		return ExecuteMissions(player, gamePanels, command.arguments);
	if(command.name == "god")
		return ExecuteGod(player, command.arguments);
	if(command.name == "damage")
		return ExecuteDamage(player, command.arguments);
	if(command.name == "kill")
		return ExecuteKill(player, command.arguments);
	if(command.name == "selfkill")
	{
		Ship *flagship = player.Flagship();
		if(!flagship)
			return "There is no flagship to destroy.";
		flagship->SelfDestruct();
		return "Flagship self-destructed.";
	}
	if(command.name == "spawn")
		return ExecuteSpawn(player, mainPanel, command.arguments);
	if(command.name == "layout")
		return ExecuteLayout(command.arguments);
	return "Unknown command. Use /help for the command list.";
}
