#pragma once

#include "Panel.h"

#include "text/Format.h"
#include "Point.h"
#include "text/Truncate.h"

#include <functional>
#include <optional>
#include <string>

class TextArea;
class NetworkSession;

class MultiplayerPanel
{
public:
    MultiplayerPanel(NetworkSession& session);

    void Draw();
    void HandleKey(int key);
    void HandleTextInput(const std::string &text);
    void HandleClick(int x, int y);

private:
	NetworkSession& networkSession;

    std::string address = "127.0.0.1";
    std::string port = "4242";

    bool addressFocused = true;
    bool portFocused = false;
    bool connecting = false;
	bool isMission = false;

    void Connect();
    void ShowError(const std::string &message);


protected:
	class MultiplayerInit {
	public:
		std::string message;
		std::string initialValue;
		Truncate truncate = Truncate::NONE;

		std::function<void()> voidFun;
		std::function<void(bool)> boolFun;
		std::function<void(int)> intFun;
		std::function<void(double)> doubleFun;
		std::function<void(const std::string &)> stringFun;

		std::function<bool(int)> validateIntFun;
		std::function<bool(double)> validateDoubleFun;
		std::function<bool(const std::string &)> validateStringFun;
		std::function<bool(const std::string &, char)> filterCharFun;

		bool canCancel = true;
		int activeButton = 1;
		bool isMission = false;
		bool allowsFastForward = false;

		DialogPanel::FunctionButton buttonOne;
		DialogPanel::FunctionButton buttonThree;

		const System *system = nullptr;
	};


protected:
	std::shared_ptr<TextArea> text;
	// The number of extra segments in this dialog.
	int extensionCount;

	bool canCancel;
	int activeButton;
	bool isMission;
	bool isOkDisabled;
	bool allowsFastForward;
	bool isWide;
	int flickerTime = 0;

	std::string input;

	std::string okText;
	std::string cancelText;

	Point okPos;
	Point cancelPos;
	Point thirdPos;

	MultiplayerPanel::FunctionButton buttonOne;
	MultiplayerPanel::FunctionButton buttonThree;

	int numButtons;

	const System *system = nullptr;
};


template<class T>
MultiplayerPanel::FunctionButton::FunctionButton(T *panel, const std::string &buttonLabel, SDL_Keycode buttonKey, bool(T::*buttonAction)(const std::string &)) : buttonLabel(buttonLabel), buttonKey(buttonKey), buttonAction(std::bind(buttonAction, panel, std::placeholders::_1))
{}



template<class T>
MultiplayerPanel *DialogPanel::CallFunctionOnExit(T *t, void (T::*fun)(bool), std::string message, Truncate truncate, bool allowsFastForward)
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
DialogPanel *DialogPanel::RequestInteger(T *t, void (T::*fun)(int), std::string message, std::optional<int> initialValue, Truncate truncate, bool allowsFastForward)
{
	DialogInit init;
	init.message = std::move(message);
	if(initialValue.has_value())
		init.initialValue = std::to_string(initialValue.value());
	init.intFun = std::bind(fun, t, std::placeholders::_1);
	init.truncate = truncate;
	init.allowsFastForward = allowsFastForward;
	return new MultiplayerPanel(init);
}



template<class T>
MultiplayerPanel *MultiplayerPanel::RequestDouble(T *t, void (T::*fun)(double), std::string message, std::optional<double> initialValue, Truncate truncate, bool allowsFastForward)
{
	MultiplayerInit init;
	init.message = std::move(message);
	if(initialValue.has_value())
		init.initialValue = Format::StripCommas(Format::Number(initialValue.value(), 5));
	init.doubleFun = std::bind(fun, t, std::placeholders::_1);
	init.truncate = truncate;
	init.allowsFastForward = allowsFastForward;
	return new MultiplayerPanel(init);
}



template<class T>
MultiplayerPanel *MultiplayerPanel::RequestStringWithValidation(T *t, void (T::*fun)(const std::string &), std::function<bool(const std::string &)> validate, std::string message, std::string initialValue, Truncate truncate, bool allowsFastForward)
{
	DialogInit init;
	init.message = std::move(message);
	init.initialValue = std::move(initialValue);
	init.stringFun = std::bind(fun, t, std::placeholders::_1);
	init.validateStringFun = std::move(validate);
	init.truncate = truncate;
	init.allowsFastForward = allowsFastForward;
	return new MultiplayerPanel(init);
}



template<class T>
MultiplayerPanel *DialogPanel::RequestStringWithCharFilter(T *t, void (T::*fun)(const std::string &), std::function<bool(const std::string &, char)> filter, std::string message, std::string initialValue, Truncate truncate, bool allowsFastForward)
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



template<class T>
MultiplayerPanel *DialogPanel::RequestIntegerWithValidation(T *t, void (T::*fun)(int), std::function<bool(int)> validate, std::string message, std::optional<int> initialValue, Truncate truncate, bool allowsFastForward)
{
	MultiplayerInit init;
	init.message = std::move(message);
	if(initialValue.has_value())
		init.initialValue = std::to_string(initialValue.value());
	init.intFun = std::bind(fun, t, std::placeholders::_1);
	init.validateIntFun = std::move(validate);
	init.truncate = truncate;
	init.allowsFastForward = allowsFastForward;
	return new MultiplayerPanel(init);
}



template<class T>
MultiplayerPanel *MultiplayerPanel::RequestDoubleWithValidation(T *t, void (T::*fun)(double), std::function<bool(double)> validate, std::string message, std::optional<double> initialValue, Truncate truncate, bool allowsFastForward)
{
	MultiplayerInit init;
	init.message = std::move(message);
	if(initialValue.has_value())
		init.initialValue = Format::StripCommas(Format::Number(initialValue.value(), 5));
	init.doubleFun = std::bind(fun, t, std::placeholders::_1);
	init.validateDoubleFun = std::move(validate);
	init.truncate = truncate;
	init.allowsFastForward = allowsFastForward;
	return new MultiplayerPanel(init);
}



template<class T>
MultiplayerPanel *MultiplayerPanel::RequestPositiveInteger(T *t, void (T::*fun)(int), std::string message, std::optional<int> initialValue, Truncate truncate, bool allowsFastForward)
{
	return MultiplayerPanel::RequestIntegerWithValidation(t, fun, [](int value) -> bool { return value > 0; }, message, initialValue, truncate, allowsFastForward);
}
