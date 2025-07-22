#include <filesystem>
#include <regex>
#include <vector>
#include <windows.h>
#include <psapi.h>

#include <INIReader.h>
#include <MinHook.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "mem/mem.h"
#include "X4RE/Station.h"

// Globals
DWORD foregroundWindowId = NULL;
DWORD x4Id = NULL;
std::shared_ptr<spdlog::logger> logger;
std::vector<std::string> excludes;

// Function typedefs
typedef float (*__GetUIScaleFactor)();
__GetUIScaleFactor _GetUIScaleFactor;

typedef void (*__SetUIScaleFactor)(float factor);
__SetUIScaleFactor _SetUIScaleFactor;

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

void someFunction(X4RE::Station* myStation)
{
    logger->info("My station is in cluster with macro name {}", myStation->zone->sector->cluster->macro->macroName);
    logger->info("My station is named {}", myStation->stationName);
    logger->info("My station is of type {}", myStation->stationTypeName);
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

bool WriteMemory(void* address, void* newBytes, int size)
{
	DWORD oldProtect;

	if (!VirtualProtect((LPVOID)address, size, PAGE_EXECUTE_READWRITE, &oldProtect))
		return false;

	if (memcpy_s(address, size, newBytes, size))
		return false;

	if (!VirtualProtect((LPVOID)address, size, oldProtect, &oldProtect))
		return false;

	return true;
}

bool SetupLogFilter(INIReader& config, void* moduleBase, uintptr_t moduleSize)
{
	logger->info("Config option: Disable Logs Filter setting up...");

	std::string excluded = config.Get("ExcludedLogStrings", "string", "");
	if (excluded.empty())
	{
		logger->error("EnableLogsFilter is enabled, but ExcludedLogStrings section contains no strings to filter!");
		return false;
	}

	excludes = splitKeys(excluded);
	if (excludes.empty())
	{
		logger->error("EnableLogsFilter is enabled, but ExcludedLogStrings section contains no strings to filter!");
		return false;
	}

	uintptr_t loggerAddress = (uintptr_t)scan_idastyle(moduleBase, moduleSize, "48 89 5C 24 18 55 56 41 54 41 55 41 57");
	if (!loggerAddress)
	{
		logger->error("Logger address: NULL!");
		return false;
	}

	if (MH_CreateHook((void*)loggerAddress, (void*)hookLogger, reinterpret_cast<void**>(&origLogger)) != MH_OK || MH_EnableHook((void*)loggerAddress) != MH_OK)
	{
		logger->error("Failed to hook logger, Exiting...");
		return false;
	}

    return true;
}

bool SetupEnableMapEditorExport(void* moduleBase, uintptr_t moduleSize)
{
	uintptr_t exportMapAddress = (uintptr_t)scan_idastyle(moduleBase, moduleSize, "0F 84 ? ? ? ? 48 C7 44 24 ? ? ? ? ? 48 C7 44 24 ? ? ? ? ? 48 C7 44 24 ? ? ? ? ? 0F 1F 40");

    if (!exportMapAddress)
    {
		logger->error("Failed to get address required for Export Map patch!");
        return false;
    }

	char newBytes[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    if (!WriteMemory((void*)exportMapAddress, (void*)newBytes, sizeof(newBytes)))
    {
        logger->error("Failed to patch ExportMap, map exporting will not work!");
        return false;
    }

    return true;
}

bool SetupReloadUIHotkey(INIReader& config)
{
	Sleep(5 * 1000);

	logger->info("Config option: Reload UI hotkey setting up...");

	getProcID(L"X4", x4Id);

	_GetUIScaleFactor = (__GetUIScaleFactor)GetProcAddress(GetModuleHandle(nullptr), "GetUIScaleFactor");
	_SetUIScaleFactor = (__SetUIScaleFactor)GetProcAddress(GetModuleHandle(nullptr), "SetUIScaleFactor");

    if (!_GetUIScaleFactor || !_SetUIScaleFactor)
    {
		logger->error("Failed to resolve function pointers during Reload UI hotkey setup, /reloadui hotkey will not work.");
        return false;
    }

	logger->info("GetUIScaleFactor address: 0x{0:x}", (uintptr_t)_GetUIScaleFactor);
	logger->info("SetUIScaleFactor address: 0x{0:x}", (uintptr_t)_SetUIScaleFactor);

	UINT reloadUIKeyCode = (UINT)config.GetInteger("Hotkeys", "reloadUI", 0x24);

	bool requireALT = config.GetBoolean("HotkeyModifiers", "requireALT", false);
	bool requireCTRL = config.GetBoolean("HotkeyModifiers", "requireCTRL", false);
	bool requireSHIFT = config.GetBoolean("HotkeyModifiers", "requireSHIFT", false);

	UINT modifiers = MOD_NOREPEAT;

	if (requireALT)
		modifiers |= MOD_ALT;
	if (requireCTRL)
		modifiers |= MOD_CONTROL;
	if (requireSHIFT)
		modifiers |= MOD_SHIFT;

    if (!RegisterHotKey(nullptr, 1, modifiers, reloadUIKeyCode))
    {
		logger->error("Failed to register hotkey: Reload UI. Try another key in the ini file.");
        return false;
    }

	logger->info("Registered hotkey: Reload UI.");
	logger->info("Config option: Reload UI hotkey finished setup successfully");
	logger->info("Hotkeys ready!");

	MSG msg = { nullptr };
	while (GetMessage(&msg, nullptr, 0, 0) != 0)
	{
		if (msg.message == WM_HOTKEY)
		{
			if (isActiveWindow())
			{
				logger->info("Reloading UI!");
				_SetUIScaleFactor(_GetUIScaleFactor());
			}
		}
	}

    return true;
}

DWORD WINAPI setup(LPVOID param)
{
    // Init spdlog
    std::filesystem::path logPath = std::filesystem::current_path() / "plugins" / "DebugUtils" / "DebugUtils.log";
    logger = spdlog::basic_logger_mt("DebugUtils", logPath.string(), true);

    logger->flush_on(spdlog::level::info);

    // Init Config
    std::filesystem::path iniPath = std::filesystem::current_path() / "plugins" / "DebugUtils" / "DebugUtils.ini";

    INIReader config(iniPath.string());

    if (config.ParseError() < 0)
    {
        logger->error("X4Bypass.ini not found, Path was " + iniPath.string());
        return 0;
    }

    bool enableLogsFilter = config.GetBoolean("Config", "EnableLogsFilter", false);
    bool enableReloadHotkey = config.GetBoolean("Config", "EnableHotkeys", false);
    bool enableMapEditorExport = config.GetBoolean("Config", "EnableMapEditorExport", false);

    if (!enableLogsFilter && !enableReloadHotkey)
    {
        logger->error("No config options enabled in DebugUtils.ini! Exiting...");
        return 0;
    }

    if (MH_Initialize() != MH_OK)
    {
        logger->error("Minhook initialize failed");
        return 0;
    }

    MODULEINFO info;
    BOOL result = GetModuleInformation(GetCurrentProcess(), GetModuleHandle(nullptr), &info, sizeof(info));

    if (!result)
    {
        logger->error("Failed to get module info for X4.exe, exiting.");
        return 0;
    }

    logger->info("X4 base address: 0x{0:x}", (uintptr_t)info.lpBaseOfDll);

    if (enableLogsFilter)
    {
        if (SetupLogFilter(config, info.lpBaseOfDll, info.SizeOfImage))
			logger->info("Config option: Disable Logs Filter finished setup successfully");
    }

	if (enableMapEditorExport)
	{
        if (SetupEnableMapEditorExport(info.lpBaseOfDll, info.SizeOfImage))
        logger->info("ExportMap patch success");
	}

    if (enableReloadHotkey)
    {
        SetupReloadUIHotkey(config);
    }

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
