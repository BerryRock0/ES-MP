// server_main.cpp
//
// A standalone dedicated server for Endless Sky multiplayer. This is the
// "headless" counterpart to the in-game "Start LAN World" host mode: it runs
// the same NetworkServer without any graphics, so a machine can host a game
// for others without launching the client.
//
// Usage:
//   endless-sky-server [--port <port>] [--password <password>] [--name <name>]
//
// If no port is given, NetworkProtocol::DEFAULT_PORT is used. An empty password
// means the server accepts anyone (still requires a nickname).
#include "NetworkProtocol.h"
#include "NetworkServer.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
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
			<< " [--port <port>] [--password <password>] [--name <name>]\n";
	}
}

int main(int argc, char *argv[])
{
	uint16_t port = NetworkProtocol::DEFAULT_PORT;
	std::string password;
	std::string serverName = "Endless Sky LAN Server";

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

	if(!server.Start(port, password, serverName))
	{
		std::cerr << "Failed to start the server on port " << port << ".\n";
		return 1;
	}

	std::cout << "Server \"" << serverName << "\" listening on port " << port << ".\n";
	std::cout << "Press Ctrl+C to stop.\n";

	// A fixed-step loop: poll for network activity, then advance the world.
	auto last = std::chrono::steady_clock::now();

	while(running)
	{
		server.Poll();

		const auto now = std::chrono::steady_clock::now();
		const double elapsed = std::chrono::duration<double>(now - last).count();
		last = now;

		server.Update(elapsed > 0.25 ? 0.25 : elapsed);

		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	std::cout << "\nShutting down...\n";
	server.Stop();
	return 0;
}
