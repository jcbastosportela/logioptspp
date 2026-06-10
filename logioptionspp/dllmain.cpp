#include "Logger.hpp"

#include <Windows.h>
#include <detours.h>
#include <iostream>

#include <vector>
#include <algorithm>
#include <filesystem>

using Logger = utils::Logger;

auto& logger{Logger::getInstance()};

namespace Ini {
    constexpr wchar_t DEFAULT_INI[]{ L".\\conf.ini" };
    namespace GeneralSection {
        constexpr wchar_t NAME[]{ L"General" };
        namespace Key {
            constexpr wchar_t BLACKLIST_APPS[]{ L"BLACKLIST_APPS" };
            constexpr wchar_t WHITELIST_APPS[]{ L"WHITELIST_APPS" };    // only one of WHITELIST_APPS or BLACKLIST_APPS shall be defined; WHITELIST_APPS precedes
            constexpr wchar_t LOG_PATH[]{ L"LOG_PATH" };
        }
    }
}

static std::vector<std::wstring>    appsToBlacklist;    //!< keep a list of applications to blacklist from smoothing
static std::vector<std::wstring>    appsToWhitelist;    //!< keep a list of applications to whitelist from smoothing

/**
 * Helper to make some runtime assertions.
 * \param _c    condition to assert
 * \param _m    text message to print to logger
 * \param _r    return value
 */
#define RETURN_VAL_IF_NOT(_c, _m, _r)                             \
    {                                                   \
        if (!(_c))                                      \
        {                                               \
            std::cerr << "ERROR " << (_m) << std::endl; \
            logger.log(_m);              \
            return (_r);                                \
        }                                               \
    }

 /**
  * Helper to make some runtime assertions.
  * \param _c    condition to assert
  * \param _m    text message to print to logger
  */
#define RETURN_FALSE_IF_NOT(_c, _m)                             \
    {                                                   \
        if (!(_c))                                      \
        {                                               \
            std::cerr << "ERROR " << (_m) << std::endl; \
            logger.log(_m);              \
            return false;                                \
        }                                               \
    }

  /**
   * Helper to make some runtime assertions.
   * \param _c    condition to assert
   * \param _m    text message to print to logger
   */
#define RETURN_FALSE_IF(_c, _m)                             \
    {                                                   \
        if ((_c))                                      \
        {                                               \
            std::cerr << "ERROR " << (_m) << std::endl; \
            logger.log(_m);              \
            return false;                                \
        }                                               \
    }

// NOTE: this is needed in case we use detours to preload.
// reason is: "The new process will fail to start if the target DLL does not contain a exported function with ordinal #1."
// source: https://github.com/microsoft/Detours/wiki/DetourCreateProcessWithDlls
__declspec(dllexport) void __stdcall MyDetoursInitializationFunction(void)
{
    logger.log("MyDetoursInitializationFunction");
}

// Define a typedef for the original function signature
typedef BOOL(WINAPI *QueryFullProcessImageNameW_t)(
    HANDLE hProcess,
    DWORD dwFlags,
    LPWSTR lpExeName,
    PDWORD lpdwSize);

/**
 * function pointer to the function to detour.
 *
 * Needs to be done at runtime using DetourFindFunction because the line below will not
 * work as it will get from kernel32.dll instead of kernelbase.dll
 *
 * realQueryFullProcessImageNameW = QueryFullProcessImageNameW; // gets from kernel32.dll instead of kernelbase.dll
 */
QueryFullProcessImageNameW_t realQueryFullProcessImageNameW = (QueryFullProcessImageNameW_t)(nullptr);

/**
 * Case-insensitive check if fullString ends with ending.
 */
static bool endsWithCaseInsensitive(const std::wstring& fullString, const std::wstring& ending) {
    if (fullString.length() < ending.length()) {
        return false;
    }
    return _wcsicmp(fullString.c_str() + fullString.length() - ending.length(), ending.c_str()) == 0;
}

/**
 * Spoof the process name buffer to look like chrome.exe.
 */
static void spoofAsChrome(LPWSTR lpExeName, DWORD bufferSize, PDWORD lpdwSize)
{
    constexpr wchar_t CHROME_PATH[] = L"c:\\bla\\chrome.exe";
    wcsncpy_s(lpExeName, bufferSize, CHROME_PATH, _TRUNCATE);
    *lpdwSize = static_cast<DWORD>(wcslen(lpExeName));
}

/**
 * Hooked QueryFullProcessImageNameW - intercepts process name queries
 * and spoofs matching apps as chrome.exe for smooth scrolling.
 */
BOOL WINAPI MyQueryFullProcessImageNameW(
    HANDLE hProcess,
    DWORD dwFlags,
    LPWSTR lpExeName,
    PDWORD lpdwSize)
{
    // Save original buffer capacity before the real call overwrites it with string length
    DWORD bufferSize = *lpdwSize;

    BOOL ret = realQueryFullProcessImageNameW(hProcess, dwFlags, lpExeName, lpdwSize);
    if (!ret)
    {
        return ret;
    }

    std::wstring exeName(lpExeName);

    bool shouldSpoof;
    if (!appsToWhitelist.empty())
    {
        // Whitelist mode: spoof only apps on the list
        shouldSpoof = std::any_of(appsToWhitelist.begin(), appsToWhitelist.end(),
            [&](const std::wstring& app) { return endsWithCaseInsensitive(exeName, app); });
    }
    else
    {
        // Blacklist mode: spoof everything except apps on the list
        shouldSpoof = std::none_of(appsToBlacklist.begin(), appsToBlacklist.end(),
            [&](const std::wstring& app) { return endsWithCaseInsensitive(exeName, app); });
    }

    if (shouldSpoof)
    {
        spoofAsChrome(lpExeName, bufferSize, lpdwSize);
    }

    return TRUE;
}

/**
 * Gets the directory of the DLL.
 */
static std::filesystem::path getDllDirectory(HMODULE hModule)
{
    constexpr size_t MAX_FILE_LEN{ 1000 };
    wchar_t dllPath[MAX_FILE_LEN];
    auto ret = GetModuleFileName(hModule, dllPath, MAX_FILE_LEN);
    if(ret == 0 )
    {
        logger.log("Failed GetModuleFileName with return " + std::to_string(ret));
        return std::filesystem::path();
    }
    auto dllDir = std::filesystem::absolute(std::filesystem::path(dllPath)).parent_path();

    return dllDir;
}


/**
 * Loads the configs from the INI file.
 */
static int load_configs(const std::filesystem::path& iniPath)
{
    auto trim = [](const std::wstring& str) -> std::wstring {
        size_t first = str.find_first_not_of(L" \t\r\n");
        size_t last = str.find_last_not_of(L" \t\r\n");
        if (first == std::wstring::npos || last == std::wstring::npos) {
            return L"";
        }
        return str.substr(first, (last - first + 1));
        };

    wchar_t buffer[10000];  // local stack buffer, only called once at startup
    if (0 != GetPrivateProfileString(Ini::GeneralSection::NAME, Ini::GeneralSection::Key::LOG_PATH, L"", buffer, sizeof(buffer) / sizeof(wchar_t), iniPath.c_str()))
    {
        logger.setLogFilePath(std::wstring(buffer));
        logger.enable();
    }

    logger.log("load_configs");

    auto get_app_filter_list = [&](const wchar_t* key, std::vector<std::wstring>& appsFilterList)
    {
        if (0 != GetPrivateProfileString(Ini::GeneralSection::NAME, key, L"", buffer, sizeof(buffer) / sizeof(wchar_t), iniPath.c_str()))
        {
            std::wistringstream wiss(buffer);

            std::wstring token;
            while (std::getline(wiss >> std::ws, token, L','))
            {
                auto app = trim(token);
                if (!app.empty())
                {
                    appsFilterList.push_back(app);
                }
            }

            logger.log(std::wstring(L"Applications in ") + key + L" are: ");
            for (const auto& substring : appsFilterList) {
                logger.log(substring);
            }
        }
    };

    get_app_filter_list(Ini::GeneralSection::Key::BLACKLIST_APPS, appsToBlacklist);
    get_app_filter_list(Ini::GeneralSection::Key::WHITELIST_APPS, appsToWhitelist);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    Logger &logger = Logger::getInstance();

    if (DetourIsHelperProcess())
    {
        return TRUE;
    }
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        auto dllDirPath = getDllDirectory(hModule);
        RETURN_FALSE_IF(dllDirPath.empty(), "getDllDirectory failed");

        load_configs(dllDirPath / Ini::DEFAULT_INI);

        logger.log("DLL_PROCESS_ATTACH started");

        auto stt = DetourTransactionBegin();
        RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourTransactionBegin");

        stt = DetourUpdateThread(GetCurrentThread());
        RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourUpdateThread");

        realQueryFullProcessImageNameW = (QueryFullProcessImageNameW_t)DetourFindFunction("KernelBase.dll", "QueryFullProcessImageNameW");
        RETURN_FALSE_IF_NOT(realQueryFullProcessImageNameW != nullptr, "DetourFindFunction returned null for QueryFullProcessImageNameW");

        stt = DetourAttach(&(PVOID&)realQueryFullProcessImageNameW, MyQueryFullProcessImageNameW);
        RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourAttach MyQueryFullProcessImageNameW");

        stt = DetourTransactionCommit();
        RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourTransactionCommit");

        logger.log("DLL_PROCESS_ATTACH is ok");
    }

    if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        logger.log("DLL_PROCESS_DETACH started");

        auto stt = DetourTransactionBegin();
        RETURN_FALSE_IF_NOT((stt == NO_ERROR ), "DetourTransactionBegin");

        stt = DetourUpdateThread(GetCurrentThread());
        RETURN_FALSE_IF_NOT((stt == NO_ERROR ), "DetourUpdateThread");

        stt = DetourDetach(&(PVOID &)realQueryFullProcessImageNameW, MyQueryFullProcessImageNameW);
        RETURN_FALSE_IF_NOT((stt == NO_ERROR ), "DetourDetach MyQueryFullProcessImageNameW");

        stt = DetourTransactionCommit();
        RETURN_FALSE_IF_NOT((stt == NO_ERROR ), "DetourTransactionCommit");

        logger.log("DLL_PROCESS_DETACH is ok");
    }

    return TRUE;
}
