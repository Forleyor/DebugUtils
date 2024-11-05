#include <filesystem>
#include <regex>
#include <vector>
#include <windows.h>
#include <psapi.h>

#include <INIReader.h>
#include <MinHook.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "mem/mem.h"

// Globals
DWORD foregroundWindowId = NULL;
DWORD x4Id = NULL;
std::shared_ptr<spdlog::logger> logger;
std::vector<std::string> excludes;

// Function typedefs
typedef void (*_reloadUI)();
_reloadUI reloadUI;

// Function hook on X4's debug log function
int (*origLogger)(void* a1, const char* format, va_list args, void* a4, int a5, void* a6);
int hookLogger(void* a1, const char* format, va_list args, void* a4, int a5, void* a6)
{
	char buffer[0x2000];
	if (__stdio_common_vsprintf(0, buffer, 0x2000, format, nullptr, args) >= 0)
	{
		// Loops strings in excluded strings vector
		for (const std::string& excludeString : excludes)
		{
			// If the game is trying to print a string containing excluded text return
			if (std::regex_search(buffer, std::regex(excludeString)))
			{
				return 2256;
			}
		}
	}

	// Otherwise log the message to X4's debug log as normal
	return origLogger(a1, format, args, a4, a5, a6);
}

// Splits the keys in DebugUtils.ini ExcludedLogStrings section into a vector of strings
std::vector<std::string> splitKeys(const std::string& str)
{
	std::vector<std::string> keys;

	std::string::size_type pos = 0;
	std::string::size_type prev = 0;
	while ((pos = str.find('\n', prev)) != std::string::npos) {
		keys.push_back(str.substr(prev, pos - prev));
		prev = pos + 1;
	}
	keys.push_back(str.substr(prev));

	return keys;
}

// Used to get the process id of X4
void getProcID(LPCWSTR window_title, DWORD& process_id)
{
	GetWindowThreadProcessId(FindWindow(nullptr, window_title), &process_id);
}

// Function to check if X4 is currently the window in focus
// this is checked for hotkeys so hotkeys dont trigger when x4 is not in focus
bool isActiveWindow()
{
	GetWindowThreadProcessId(GetForegroundWindow(), &foregroundWindowId);

	if (x4Id == foregroundWindowId)
	{
		return true;
	}

	return false;
}

void LogErrorAndExit(std::string errorMessage)
{
	logger->error("Exiting due to error: {}", errorMessage);
	ExitThread(0);
}

// Created thread to setup our logic
DWORD WINAPI setup(LPVOID param)
{
	// Init spdlog
	std::filesystem::path logPath = std::filesystem::current_path() / "plugins" / "DebugUtils" / "DebugUtils.log";
	logger = spdlog::basic_logger_mt("DebugUtils", logPath.string(), true);

	// Set spdlog to flush messages to log file on info level
	logger->flush_on(spdlog::level::info);

	// Get the current working directory, this returns x4 root directory
	std::filesystem::path iniPath = std::filesystem::current_path() / "plugins" / "DebugUtils" / "DebugUtils.ini";

	INIReader config(iniPath.string());

	// Check if config exists
	if (config.ParseError() < 0)
		LogErrorAndExit("X4Bypass.ini not found, Path was " + iniPath.string());

	// Get ini settings from [Config] section
	bool enableLogsFilter = config.GetBoolean("Config", "EnableLogsFilter", false);
	bool enableReloadHotkey = config.GetBoolean("Config", "EnableHotkeys", false);

	if (!enableLogsFilter && !enableReloadHotkey)
		LogErrorAndExit("No config options enabled in DebugUtils.ini! Exiting...");

	// Initialize MinHook
	if (MH_Initialize() != MH_OK)
		LogErrorAndExit("Minhook initialize failed");

	// Get module info for X4.exe, used later in pattern scanning passing X4 base address and size of module
	MODULEINFO info;
	BOOL result = GetModuleInformation(GetCurrentProcess(), GetModuleHandle(nullptr), &info, sizeof(info));

	if (!result)
		LogErrorAndExit("Failed to get module info for X4.exe");

	logger->info("X4 base address: 0x{0:x}", (uintptr_t)info.lpBaseOfDll);

	if (enableLogsFilter)
	{
		logger->info("Config option: Disable Logs Filter setting up...");

		std::string excluded = config.Get("ExcludedLogStrings", "string", "");

		if (excluded.empty())
			LogErrorAndExit("EnableLogsFilter is enabled, but ExcludedLogStrings section contains no strings to filter!");

		// Split the strings in ExcludedLogStrings into a vector of strings
		excludes = splitKeys(excluded);

		// Check if there are actually any excluded strings
		if (excludes.empty())
		{
			LogErrorAndExit("EnableLogsFilter is enabled, but ExcludedLogStrings section contains no strings to filter!");
			return 0;
		}

		// Get the address of X4.exe Debug log logging function
		uintptr_t loggerAddress = (uintptr_t)scan_idastyle(info.lpBaseOfDll, info.SizeOfImage, "48 89 5C 24 18 55 56 41 54 41 55 41 57");

		if (!loggerAddress)
			LogErrorAndExit("Logger address: NULL!");

		// Create the hook in MinHook and enable it
		if (MH_CreateHook((void*)loggerAddress, (void*)hookLogger, reinterpret_cast<void**>(&origLogger)) != MH_OK || MH_EnableHook((void*)loggerAddress) != MH_OK)
			LogErrorAndExit("Failed to hook logger, Exiting...");

		logger->info("Config option: Disable Logs Filter finished setup successfully");
	}

	if (enableReloadHotkey)
	{
		// give the game time to load a bit
		Sleep(5 * 1000);

		logger->info("Config option: Reload UI hotkey setting up...");

		// Get process ID for X4
		// used in isActiveWindow() so hotkey doesnt trigger when X4 is out of focus
		getProcID(L"X4", x4Id);

		// get the address of X4.exe function handles /commands
		reloadUI = (_reloadUI)scan_idastyle(info.lpBaseOfDll, info.SizeOfImage, "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F 29 74 24 ? 48 83 3D");

		if (!reloadUI)
			LogErrorAndExit("ExecuteCommand address: NULL!");

		logger->info("reloadUI address: 0x{0:x}", (uintptr_t)reloadUI);

		// Get the user defined keycode from ini file, defaults to VK_HOME (0x24) if not set in ini file
		UINT reloadUIKeyCode = (UINT)config.GetInteger("Hotkeys", "reloadUI", 0x24);

		bool requireALT = (UINT)config.GetBoolean("HotkeyModifiers", "requireALT", false);
		bool requireCTRL = (UINT)config.GetBoolean("HotkeyModifiers", "requireCTRL", false);
		bool requireSHIFT = (UINT)config.GetBoolean("HotkeyModifiers", "requireSHIFT", false);

		UINT modifiers = MOD_NOREPEAT;

		if (requireALT)
			modifiers |= MOD_ALT;
		if (requireCTRL)
			modifiers |= MOD_CONTROL;
		if (requireSHIFT)
			modifiers |= MOD_SHIFT;

		// Register hotkey for reloading UI
		if (!RegisterHotKey(nullptr, 1, modifiers, reloadUIKeyCode))
			LogErrorAndExit("Failed to register hotkey: Reload UI. Try another key in the ini file.");

		logger->info("Registered hotkey: Reload UI.");
		logger->info("Config option: Reload UI hotkey finished setup successfully");
		logger->info("Hotkeys ready!");

		// When hotkey pressed, reload UI
		MSG msg = { nullptr };
		while (GetMessage(&msg, nullptr, 0, 0) != 0)
		{
			if (msg.message == WM_HOTKEY)
			{
				if (isActiveWindow())
				{
					logger->info("Reloading UI!");
					reloadUI();
				}
			}
		}
	}

	std::string test;

	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		// Create a thread for setup
		CreateThread(nullptr, 0, setup, nullptr, 0, nullptr);
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
