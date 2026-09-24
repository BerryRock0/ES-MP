/* server_main.cpp
Copyright (c) 2026 by BerryRock0

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

// server_main.cpp
//
// A standalone dedicated server for Endless Sky multiplayer. This is the
// "headless" counterpart to the in-game "Start LAN World" host mode: it runs
// the same NetworkServer without any graphics, so a machine can host a game
// for others without launching the client.
//
// Usage:
//   endless-sky-server [--port <port>] [--password <password>] [--name <name>]
//                      [--world-system <system>] [--world-planet <planet>]
//                      [--world-date "<day> <month> <year>"]
//                      [--spawn-x <x>] [--spawn-y <y>]
//                      [--save-dir <directory>]
//                      [--plugins-dir <directory>] [--world-model <model>]
//
// If no port is given, NetworkProtocol::DEFAULT_PORT is used. An empty password
// means the server accepts anyone (still requires a nickname).
//
// The server owns its world: the start system, planet, date, and spawn point
// are broadcast to every client at login, so no client's local save defines
// the shared world. The standalone server cannot load game data itself, so its
// world comes from the flags above; without them it defaults to the canonical
// first start scenario (Rutilicus / New Boston / 16 Nov 3013 at the origin).
//
// The server also persists its world into its own folder (default: ./server,
// or the directory given with --save-dir), completely separate from the
// client-side saves and pilots. Keep --save-dir outside the client's
// --config/saves directory. On startup it restores that world unless
// --world-* / --spawn-* flags are given (explicit flags win), and it writes
// the world file back every 30 seconds (but only when the world has changed)
// and again on shutdown, so the world -- including each player's last
// reported position -- survives a restart. The write is atomic (a temp file
// is renamed over world.txt), so a crash can never leave a half-written
// world. Use --save-dir "" to disable saving.
//
// The server is fully split from the clients: it never reads any folder the
// clients use. Its fleet comes from its own plugin folder (default:
// <save-dir>/plugins, or --plugins-dir) and only ships it knows are relayed
// between clients. A client whose ship is not in the server's fleet appears
// to everyone else in the server's default ship (optionally --world-model),
// instead of smuggled in by that client's local plugins.
#include "NetworkProtocol.h"
#include "NetworkServer.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace
{
	std::atomic<bool> running{true};

	void HandleSignal(int)
	{
		running = false;
	}

	void PrintUsage(const char *program)
	{
		std::cout << "Usage: " << program
			<< " [--port <port>] [--password <password>] [--name <name>\n"
			<< "                [--world-system <system>] [--world-planet <planet>]\n"
			<< "                [--world-date \"<day> <month> <year>\"]\n"
			<< "                [--spawn-x <x>] [--spawn-y <y>]\n"
			<< "                [--save-dir <directory>]\n"
			<< "                [--plugins-dir <directory>] [--world-model <model>]\n"
			<< "\n"
			<< "The server owns its world (system, planet, date, spawn point) and\n"
			<< "broadcasts it to every client at login. Explicit world/spawn flags\n"
			<< "override the saved world. The world is persisted to --save-dir\n"
			<< "(default: ./server, a folder separate from client saves) every 30\n"
			<< "seconds (but only when something changed) and on shutdown. Keep it\n"
			<< "outside the client's config/saves directory; it is restored on\n"
			<< "the next startup.\n"
			<< "The server is fully split from the clients: it never reads any\n"
			<< "folder the clients use. Its fleet comes from its own plugin folder\n"
			<< "(<save-dir>/plugins or --plugins-dir), and only ships it knows are\n"
			<< "relayed; unknown ships fall back to --world-model (default: the\n"
			<< "first ship in the server's fleet). Pass --save-dir \"\" to disable\n"
			<< "saving.\n";
	}
}

int main(int argc, char *argv[])
{
	uint16_t port = NetworkProtocol::DEFAULT_PORT;
	std::string password;
	std::string serverName = "Endless Sky LAN Server";
	// The server keeps its own world in this folder, separate from the client
	// side saves and pilots. An empty string disables persistence.
	std::string saveDir = "server";
	// Whether any --world-* / --spawn-* flag was given; explicit flags always
	// take precedence over a previously saved world.
	bool worldFlags = false;
	// The folder the server reads its own plugin data (ship definitions) from.
	// Defaults to <save-dir>/plugins so the server's folder holds both its
	// world and its data, fully split from the clients.
	std::string pluginsDir;
	bool pluginsDirSet = false;
	// The fallback ship model for clients whose ship is not in the server's
	// plugin data (default: the first ship the server loads).
	std::string worldModel;

	// The world the server owns. Defaults mirror the canonical first start
	// scenario so a dedicated server always offers a concrete shared world.
	NetworkProtocol::WorldInfo world;
	NetworkProtocol::WriteFixedString(world.system, sizeof(world.system), "Rutilicus");
	NetworkProtocol::WriteFixedString(world.planet, sizeof(world.planet), "New Boston");
	world.day = 16;
	world.month = 11;
	world.year = 3013;

	for(int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if(arg == "--port" && i + 1 < argc)
		{
			const long parsed = std::strtol(argv[++i], nullptr, 10);
			if(parsed < 1 || parsed > 65535)
			{
				std::cerr << "Invalid port: " << argv[i] << "\n";
				return 1;
			}
			port = static_cast<uint16_t>(parsed);
		}
		else if(arg == "--password" && i + 1 < argc)
			password = argv[++i];
		else if(arg == "--name" && i + 1 < argc)
			serverName = argv[++i];
		else if(arg == "--save-dir" && i + 1 < argc)
			saveDir = argv[++i];
		else if(arg == "--plugins-dir" && i + 1 < argc)
		{
			pluginsDir = argv[++i];
			pluginsDirSet = true;
		}
		else if(arg == "--world-model" && i + 1 < argc)
			worldModel = argv[++i];
		else if(arg == "--world-system" && i + 1 < argc)
		{
			NetworkProtocol::WriteFixedString(world.system, sizeof(world.system), argv[++i]);
			worldFlags = true;
		}
		else if(arg == "--world-planet" && i + 1 < argc)
		{
			NetworkProtocol::WriteFixedString(world.planet, sizeof(world.planet), argv[++i]);
			worldFlags = true;
		}
		else if(arg == "--world-date" && i + 1 < argc)
		{
			std::istringstream stream(argv[++i]);
			int day = 0, month = 0, year = 0;
			if(!(stream >> day >> month >> year) || day < 1 || day > 31
				|| month < 1 || month > 12 || year < 1)
			{
				std::cerr << "Invalid world date: " << argv[i]
					<< " (expected \"<day> <month> <year>\").\n";
				return 1;
			}
			world.day = static_cast<uint16_t>(day);
			world.month = static_cast<uint16_t>(month);
			world.year = static_cast<uint16_t>(year);
			worldFlags = true;
		}
		else if(arg == "--spawn-x" && i + 1 < argc)
		{
			world.spawnX = std::strtod(argv[++i], nullptr);
			worldFlags = true;
		}
		else if(arg == "--spawn-y" && i + 1 < argc)
		{
			world.spawnY = std::strtod(argv[++i], nullptr);
			worldFlags = true;
		}
		else if(arg == "--help" || arg == "-h")
		{
			PrintUsage(argv[0]);
			return 0;
		}
		else
		{
			std::cerr << "Unknown argument: " << arg << "\n";
			PrintUsage(argv[0]);
			return 1;
		}
	}

	std::signal(SIGINT, HandleSignal);
	std::signal(SIGTERM, HandleSignal);

	NetworkServer server;
	server.SetLogHandler([](const std::string &message)
	{
		std::cout << "[server] " << message << std::endl;
	});

	// The server owns its world and keeps it in a folder of its own, separate
	// from any client's saves. Explicit --world-*/--spawn-* flags win; without
	// them, a world saved by a previous run is restored so the server's world
	// survives restarts.
	server.SetWorldSaveDirectory(saveDir);
	if(worldFlags)
		server.SetWorld(world);
	else
	{
		const std::string loadError = server.LoadWorld();
		if(!loadError.empty())
			std::cerr << "[server] Warning: could not load a saved world: " << loadError << "\n";
		if(!server.HasWorld())
			server.SetWorld(world);
	}

	// The server loads its own plugin data (ship definitions) entirely from a
	// folder of its own (<save-dir>/plugins, or --plugins-dir) and only relays
	// ships it knows, so a client's local plugins cannot smuggle ships into the
	// shared world. The server never reads any folder the clients use.
	if(!pluginsDirSet)
		pluginsDir = (saveDir.empty() ? "server" : saveDir) + "/plugins";
	server.SetPluginsDirectory(pluginsDir);
	server.LoadPlugins();
	if(!worldModel.empty())
		server.SetDefaultShipModel(worldModel);

	if(!server.Start(port, password, serverName))
	{
		// Start() deliberately does not save while resetting the listener. Do
		// not let the destructor save a new, unstarted world over an existing
		// server file after a bind failure.
		server.SetWorldSaveDirectory("");
		std::cerr << "Failed to start the server on port " << port << ".\n";
		return 1;
	}

	const NetworkProtocol::WorldInfo &activeWorld = server.World();
	std::cout << "Server \"" << serverName << "\" listening on port " << port << ".\n";
	std::cout << "World: " << NetworkProtocol::ReadFixedString(activeWorld.system, sizeof(activeWorld.system))
		<< " / " << NetworkProtocol::ReadFixedString(activeWorld.planet, sizeof(activeWorld.planet))
		<< " (" << activeWorld.day << " " << activeWorld.month << " " << activeWorld.year << ").\n";
	if(saveDir.empty())
		std::cout << "World persistence is disabled (--save-dir \"\").\n";
	else
		std::cout << "The world is saved to \"" << saveDir
			<< "\" every 30 seconds (when something changed) and on shutdown.\n";
	if(!server.DefaultShipModel().empty())
	{
		std::cout << "Server fleet: \""
			<< server.PluginsDirectory() << "\"; ship enforcement is on, "
			<< "unknown ships fall back to \"" << server.DefaultShipModel() << "\".\n";
	}
	else
		std::cout << "Server fleet: none; ship enforcement is off.\n";
	std::cout << "Press Ctrl+C to stop.\n";

	// A fixed-step loop: poll for network activity, then advance the world.
	auto last = std::chrono::steady_clock::now();
	auto lastSave = std::chrono::steady_clock::now();

	while(running)
	{
		server.Poll();

		const auto now = std::chrono::steady_clock::now();
		const double elapsed = std::chrono::duration<double>(now - last).count();
		last = now;

		server.Update(elapsed > 0.25 ? 0.25 : elapsed);

		// Periodically persist the world so a crash cannot lose much. SaveWorld
		// skips the write entirely when nothing changed since the last
		// successful save, so an idle server does not keep rewriting its file.
		if(!saveDir.empty() && now - lastSave >= std::chrono::seconds(30))
		{
			lastSave = now;
			const std::string saveError = server.SaveWorld();
			if(!saveError.empty())
				std::cerr << "[server] Warning: could not save the world: " << saveError << "\n";
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	std::cout << "\nShutting down...\n";
	server.Stop();
	return 0;
}
