// HostPanel.h
//
// The "Start LAN World" dialog, modelled on Minecraft's "Open to LAN"
// feature. The host chooses a server name (their own nickname), a port, and an
// optional password, then presses "Start". The panel then starts an in-process
// NetworkServer, connects the local NetworkSession to it over the loopback
// interface, and switches to a live status view that lists the players who
// have joined.
//
// The server validates every incoming player's nickname and password before
// letting them into the world; no encryption is used, as requested.
#pragma once

#include "Panel.h"

#include "Point.h"
#include "Rectangle.h"

#include <memory>
#include <string>

class NetworkServer;
class NetworkSession;
class TextArea;

class HostPanel : public Panel
{
public:
	HostPanel(NetworkServer &server, NetworkSession &session);
	virtual ~HostPanel() override;

	virtual void Step() override;
	virtual void Draw() override;

protected:
	virtual bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) override;
	virtual bool Click(int x, int y, MouseButton button, int clicks) override;
	virtual bool TextInput(const std::string &text) override;
	virtual void Resize() override;

private:
	// Which input field currently has keyboard focus.
	enum class Field { ServerName, Port, Password };

	std::string *FocusedField();
	// Recompute the on-screen rectangles of the input fields.
	void LayoutInputFields();
	// The width of the dialog, excluding margins.
	int Width() const;

	// Validate the fields and, if they are valid, start hosting.
	void StartHosting();
	// Stop the server and the local connection, returning to the setup view.
	void StopHosting();
	// Refresh the status line shown while hosting.
	void RefreshStatus();

	NetworkServer *server = nullptr;
	NetworkSession *session = nullptr;

	std::string serverName;
	std::string port;
	std::string password;

	Field focusedField = Field::ServerName;

	// True once the server has been started and we are showing the live view.
	bool hosting = false;
	// A human-readable status line (player count, errors, ...).
	std::string status;

	Rectangle nameRect;
	Rectangle portRect;
	Rectangle passwordRect;

	std::shared_ptr<TextArea> text;

	Point okPos;
	Point cancelPos;
	int activeButton = 1;
	int extensionCount = 0;
	bool isWide = false;
};
