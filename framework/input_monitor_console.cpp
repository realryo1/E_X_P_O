#include "input_monitor_console.h"
#include <cstdio>
#include "gamepad.h"

#if defined(_DEBUG)
namespace
{
	bool g_InputConsoleEnabled = false;

	const char* BackendToString(Gamepad_InputBackend backend)
	{
		switch (backend)
		{
		case GAMEPAD_INPUT_BACKEND_XINPUT:
			return "XInput";
		case GAMEPAD_INPUT_BACKEND_RAWINPUT:
			return "RawInput";
		case GAMEPAD_INPUT_BACKEND_DIRECTINPUT:
			return "DirectInput";
		default:
			return "None";
		}
	}
}
#endif

void InputMonitorConsole_Initialize(void)
{
#if defined(_DEBUG)
	if (g_InputConsoleEnabled)
	{
		return;
	}

	if (!AllocConsole())
	{
		return;
	}

	FILE* fpOut = nullptr;
	FILE* fpErr = nullptr;
	freopen_s(&fpOut, "CONOUT$", "w", stdout);
	freopen_s(&fpErr, "CONOUT$", "w", stderr);
	SetConsoleTitle(L"expogame Input Monitor");

	printf("[Input Monitor] XInput priority + RawInput/DirectInput fallback\n");
	printf("slot mask backend    A B X Y Start  Back  LT   RT      LS(x,y)      RS(x,y)\n");
	g_InputConsoleEnabled = true;
#endif
}

void InputMonitorConsole_Update(void)
{
#if defined(_DEBUG)
	if (!g_InputConsoleEnabled)
	{
		return;
	}

	static int outputInterval = 0;
	outputInterval++;
	if (outputInterval < 3)
	{
		return;
	}
	outputInterval = 0;

	const int player = Gamepad_FindConnectedPlayer();
	const unsigned int mask = Gamepad_GetConnectedMask();
	const bool connected = Gamepad_IsConnected(player);
	const Gamepad_InputBackend backend = Gamepad_GetActiveBackend();
	const bool aButton = Gamepad_IsButtonDown(player, GPB_A);
	const bool bButton = Gamepad_IsButtonDown(player, GPB_B);
	const bool xButton = Gamepad_IsButtonDown(player, GPB_X);
	const bool yButton = Gamepad_IsButtonDown(player, GPB_Y);
	const bool startButton = Gamepad_IsButtonDown(player, GPB_START);
	const bool backButton = Gamepad_IsButtonDown(player, GPB_BACK);
	const float leftTrigger = Gamepad_GetLeftTrigger(player);
	const float rightTrigger = Gamepad_GetRightTrigger(player);
	const Gamepad_ThumbStick leftStick = Gamepad_GetLeftStick(player);
	const Gamepad_ThumbStick rightStick = Gamepad_GetRightStick(player);

	printf("\r%4d 0x%X %-11s %d %d %d %d   %d     %d   %.2f %.2f (%+.2f,%+.2f) (%+.2f,%+.2f)   ",
		connected ? player : -1,
		mask,
		BackendToString(backend),
		aButton ? 1 : 0,
		bButton ? 1 : 0,
		xButton ? 1 : 0,
		yButton ? 1 : 0,
		startButton ? 1 : 0,
		backButton ? 1 : 0,
		leftTrigger,
		rightTrigger,
		leftStick.x,
		leftStick.y,
		rightStick.x,
		rightStick.y);
	fflush(stdout);
#endif
}

void InputMonitorConsole_Finalize(void)
{
#if defined(_DEBUG)
	if (!g_InputConsoleEnabled)
	{
		return;
	}

	printf("\n[Input Monitor] closed.\n");
	FreeConsole();
	g_InputConsoleEnabled = false;
#endif
}