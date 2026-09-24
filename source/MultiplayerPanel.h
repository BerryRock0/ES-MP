//MultiplayerPanel.h
#pragma once

#include "Panel.h"

#include "text/Format.h"
#include "Point.h"
#include "text/Truncate.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

class NetworkSession;
class System;
class TextArea;
class UI;

// A dialog box that displays a message to the player. It can also be used as
// the multiplayer "connect" panel, in which case it shows an address and a
// port field and connects to a server when the player presses "Connect".
class MultiplayerPanel : public Panel
{
public:
	class FunctionButton
	{
		public:
		FunctionButton() = default;
		~FunctionButton() = default;

		template<class T>
		FunctionButton(T *panel, const std::string &buttonLabel, SDL_Keycode buttonKey = '\0', bool (T::*buttonAction)(const std::string&) = nullptr);

		public:
			std::string buttonLabel;
			SDL_Keycode buttonKey{};
			std::function<bool(const std::string &)> buttonAction;

	};

public:
	virtual ~MultiplayerPanel() override;

	// An OK dialog that has no callback or cancel button. Only used for displaying information.
	static MultiplayerPanel *Info(std::string message, Truncate truncate = Truncate::NONE, bool allowsFastForward = false);

	// The multiplayer connection panel. It shows an address and port field and
	// connects to the given session when the player presses "Connect".
	explicit MultiplayerPanel(NetworkSession &session, UI &gamePanels);

	// Draw this panel.
	virtual void Draw() override;

	// Some dialogs allow fast-forward to stay active.
	bool AllowsFastForward() const noexcept override;

	virtual void UpdateTextDisplay() override;

private:
	NetworkSession *networkSession = nullptr;
	// The in-flight single-player UI stack, so the shared world can be
	// attached to an existing MainPanel when this client connects.
	UI *gamePanels = nullptr;

	std::string address;
	std::string port;
	std::string nickname;
	std::string password;

	// Which input field currently has keyboard focus.
	enum class Field { Address, Port, Nickname, Password };
	Field focusedField = Field::Address;
	bool connecting = false;

	Rectangle addressRect;
	Rectangle portRect;
	Rectangle nicknameRect;
	Rectangle passwordRect;

	void Connect();
	// Update the contextual Connect/Disconnect controls in the connection
	// dialog. The disconnect action is also wired to the optional third button.
	void UpdateConnectionButtons();
	bool DisconnectFromServer(const std::string &);
	void ShowError(const std::string &message);
	// The field that currently has keyboard focus, or nullptr.
	std::string *FocusedField();
	// Recompute the on-screen rectangles of the input fields.
	void LayoutInputFields();


	// OK / Cancel dialog.
	// The callback is always called with the value of what button the user clicked (ok == true, cancel == false).
	template<class T>
	static MultiplayerPanel *CallFunctionOnExit(T *t, void (T::*fun)(bool),
		std::string message,
		Truncate truncate = Truncate::NONE,
		bool allowsFastForward = false);
	// OK / Cancel dialogs.
	// If the user selects "ok", the callback is called with no parameters.
	template<class T>
	static MultiplayerPanel *CallFunctionIfOk(T *t, void (T::*fun)(),
		std::string message,
		Truncate truncate = Truncate::NONE,
		bool allowsFastForward = false);
	static MultiplayerPanel *CallFunctionIfOk(std::function<void()> okFunction,
		std::string message,
		int activeButton,
		Truncate truncate = Truncate::NONE,
		bool allowsFastForward = false);


	template<class T>
	static MultiplayerPanel *RequestString(T *t, void (T::*fun)(const std::string &),
		std::string message,
		std::string initialValue = "",
		Truncate truncate = Truncate::NONE,
		bool allowsFastForward = false);
	template<class T>
	static MultiplayerPanel *RequestStringWithValidation(T *t, void (T::*fun)(const std::string &),
		std::function<bool(const std::string &)> validate,
		std::string message,
		std::string initialValue = "",
		Truncate truncate = Truncate::NONE,
		bool allowsFastForward = false);
	template<class T>
	static MultiplayerPanel *RequestStringWithCharFilter(T *t, void (T::*fun)(const std::string &),
		std::function<bool(const std::string &, char)> filter,
		std::string message,
		std::string initialValue = "",
		Truncate truncate = Truncate::NONE,
		bool allowsFastForward = false);


protected:
	class MultiplayerInit {
	public:
		std::string message;
		std::string initialValue;
		Truncate truncate = Truncate::NONE;

		std::function<void()> voidFun;
		std::function<void(bool)> boolFun;
		std::function<void(const std::string &)> stringFun;

		std::function<bool(const std::string &)> validateStringFun;
		std::function<bool(const std::string &, char)> filterCharFun;

		bool canCancel = true;
		int activeButton = 1;
		bool allowsFastForward = false;

		MultiplayerPanel::FunctionButton buttonOne;
		MultiplayerPanel::FunctionButton buttonThree;

		const System *system = nullptr;
	};

protected:
	explicit MultiplayerPanel(MultiplayerInit init);

	virtual void Resize() override;

	// The user can click "ok" or "cancel", or use the tab key to toggle which
	// button is highlighted and the enter key to select it.
	virtual bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) override;
	virtual bool Click(int x, int y, MouseButton button, int clicks) override;
	virtual bool TextInput(const std::string &text) override;

private:
	void DoCallback(bool isOk = true) const;
	// The width of the dialog, excluding margins.
	int Width() const;
	// Whether this dialog accepts typed input from the player.
	bool AcceptsInput() const;
	// Return true if the validation function passes when given the current input,
	// or if there is no validation function.
	bool ValidateInput() const;


protected:
	std::shared_ptr<TextArea> text;
	// The number of extra segments in this dialog.
	int extensionCount = 0;

	std::function<void()> voidFun;
	std::function<void(bool)> boolFun;
	std::function<void(const std::string &)> stringFun;

	std::function<bool(const std::string &)> validateStringFun;
	std::function<bool(const std::string &, char)> filterCharFun;

	bool canCancel = true;
	int activeButton = 1;
	bool isOkDisabled = false;
	bool allowsFastForward = false;
	bool isWide = false;
	int flickerTime = 0;

	std::string input;

	std::string okText;
	std::string cancelText;

	Point okPos;
	Point cancelPos;
	Point thirdPos;

	MultiplayerPanel::FunctionButton buttonOne;
	MultiplayerPanel::FunctionButton buttonThree;

	int numButtons = 1;

	const System *system = nullptr;
};


template<class T>
MultiplayerPanel::FunctionButton::FunctionButton(T *panel, const std::string &buttonLabel, SDL_Keycode buttonKey, bool(T::*buttonAction)(const std::string &)) : buttonLabel(buttonLabel), buttonKey(buttonKey), buttonAction(std::bind(buttonAction, panel, std::placeholders::_1))
{}

template<class T>
MultiplayerPanel *MultiplayerPanel::CallFunctionOnExit(T *t, void (T::*fun)(bool), std::string message, Truncate truncate, bool allowsFastForward)
{
	MultiplayerInit init;
	init.message = std::move(message);
	init.boolFun = std::bind(fun, t, std::placeholders::_1);
	init.truncate = truncate;
	init.allowsFastForward = allowsFastForward;
	return new MultiplayerPanel(init);
}

template<class T>
MultiplayerPanel *MultiplayerPanel::CallFunctionIfOk(T *t, void (T::*fun)(), std::string message, Truncate truncate, bool allowsFastForward)
{
	MultiplayerInit init;
	init.message = std::move(message);
	init.voidFun = std::bind(fun, t);
	init.truncate = truncate;
	init.allowsFastForward = allowsFastForward;
	return new MultiplayerPanel(init);
}

template<class T>
MultiplayerPanel *MultiplayerPanel::RequestString(T *t, void (T::*fun)(const std::string &), std::string message, std::string initialValue, Truncate truncate, bool allowsFastForward)
{
	MultiplayerInit init;
	init.message = std::move(message);
	init.initialValue = std::move(initialValue);
	init.stringFun = std::bind(fun, t, std::placeholders::_1);
	init.truncate = truncate;
	init.allowsFastForward = allowsFastForward;
	return new MultiplayerPanel(init);
}

template<class T>
MultiplayerPanel *MultiplayerPanel::RequestStringWithValidation(T *t, void (T::*fun)(const std::string &), std::function<bool(const std::string &)> validate, std::string message, std::string initialValue, Truncate truncate, bool allowsFastForward)
{
	MultiplayerInit init;
	init.message = std::move(message);
	init.initialValue = std::move(initialValue);
	init.stringFun = std::bind(fun, t, std::placeholders::_1);
	init.validateStringFun = std::move(validate);
	init.truncate = truncate;
	init.allowsFastForward = allowsFastForward;
	return new MultiplayerPanel(init);
}

template<class T>
MultiplayerPanel *MultiplayerPanel::RequestStringWithCharFilter(T *t, void (T::*fun)(const std::string &), std::function<bool(const std::string &, char)> filter, std::string message, std::string initialValue, Truncate truncate, bool allowsFastForward)
{
	MultiplayerInit init;
	init.message = std::move(message);
	init.initialValue = std::move(initialValue);
	init.stringFun = std::bind(fun, t, std::placeholders::_1);
	init.filterCharFun = std::move(filter);
	init.truncate = truncate;
	init.allowsFastForward = allowsFastForward;
	return new MultiplayerPanel(init);
}
