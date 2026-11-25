#include "Logger.hpp"

#include <Windows.h>
#include <detours.h>
#include <iostream>
#include <sstream>
#include <string>
#include <set>
#include <mutex>

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
            constexpr wchar_t TRACE[]{ L"TRACE" };                      // 1 = log each distinct (function, process name) the agent asks about
        }
    }
}

static std::vector<std::wstring>    appsToBlacklist;    //!< keep a list of applications to blacklist from smoothing
static std::vector<std::wstring>    appsToWhitelist;    //!< keep a list of applications to whitelist from smoothing
static bool                         traceEnabled = false;

constexpr wchar_t CHROME_PATH_W[] = L"c:\\bla\\chrome.exe";
constexpr char    CHROME_PATH_A[] = "c:\\bla\\chrome.exe";

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

// Signatures of the functions the agent can use to learn a process's executable name.
typedef BOOL(WINAPI *QueryFullProcessImageNameW_t)(HANDLE hProcess, DWORD dwFlags, LPWSTR lpExeName, PDWORD lpdwSize);
typedef BOOL(WINAPI *QueryFullProcessImageNameA_t)(HANDLE hProcess, DWORD dwFlags, LPSTR lpExeName, PDWORD lpdwSize);
typedef DWORD(WINAPI *GetModuleFileNameExW_t)(HANDLE hProcess, HMODULE hModule, LPWSTR lpFilename, DWORD nSize);

/**
 * Pointers to the real functions. Resolved at runtime with DetourFindFunction because taking
 * the address directly gets the kernel32.dll stub instead of the kernelbase.dll implementation.
 */
static QueryFullProcessImageNameW_t realQueryFullProcessImageNameW = nullptr;
static QueryFullProcessImageNameA_t realQueryFullProcessImageNameA = nullptr;
static GetModuleFileNameExW_t       realGetModuleFileNameExW = nullptr;

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
 * Whether a process with this executable path should be reported as chrome.exe.
 */
static bool shouldSpoof(const std::wstring& exeName)
{
    if (!appsToWhitelist.empty())
    {
        // Whitelist mode: spoof only apps on the list
        return std::any_of(appsToWhitelist.begin(), appsToWhitelist.end(),
            [&](const std::wstring& app) { return endsWithCaseInsensitive(exeName, app); });
    }
    // Blacklist mode: spoof everything except apps on the list
    return std::none_of(appsToBlacklist.begin(), appsToBlacklist.end(),
        [&](const std::wstring& app) { return endsWithCaseInsensitive(exeName, app); });
}

/**
 * Log each distinct (function, name, spoofed) combination once, so the log shows which
 * lookup the agent really uses without growing on every call.
 */
static std::mutex traceMutex;
static std::set<std::wstring> traceSeen;
static void trace(const wchar_t* fn, const std::wstring& name, bool spoofed)
{
    if (!traceEnabled)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(traceMutex);
    if (traceSeen.size() >= 400)
    {
        return;
    }
    std::wstring key = std::wstring(fn) + L" " + name + (spoofed ? L"  => spoofed as chrome.exe" : L"");
    if (!traceSeen.insert(key).second)
    {
        return;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    logger.log(std::wstring(stamp) + key);
}

/**
 * Hooked QueryFullProcessImageNameW.
 */
BOOL WINAPI MyQueryFullProcessImageNameW(HANDLE hProcess, DWORD dwFlags, LPWSTR lpExeName, PDWORD lpdwSize)
{
    // Save original buffer capacity before the real call overwrites it with string length
    DWORD bufferSize = *lpdwSize;

    BOOL ret = realQueryFullProcessImageNameW(hProcess, dwFlags, lpExeName, lpdwSize);
    if (!ret)
    {
        return ret;
    }

    std::wstring exeName(lpExeName);
    bool spoof = shouldSpoof(exeName);
    if (spoof)
    {
        wcsncpy_s(lpExeName, bufferSize, CHROME_PATH_W, _TRUNCATE);
        *lpdwSize = static_cast<DWORD>(wcslen(lpExeName));
    }
    trace(L"QueryFullProcessImageNameW", exeName, spoof);
    return TRUE;
}

/**
 * Hooked QueryFullProcessImageNameA.
 */
BOOL WINAPI MyQueryFullProcessImageNameA(HANDLE hProcess, DWORD dwFlags, LPSTR lpExeName, PDWORD lpdwSize)
{
    DWORD bufferSize = *lpdwSize;

    BOOL ret = realQueryFullProcessImageNameA(hProcess, dwFlags, lpExeName, lpdwSize);
    if (!ret)
    {
        return ret;
    }

    wchar_t wide[MAX_PATH * 2] = {};
    MultiByteToWideChar(CP_ACP, 0, lpExeName, -1, wide, static_cast<int>(std::size(wide)) - 1);
    std::wstring exeName(wide);
    bool spoof = shouldSpoof(exeName);
    if (spoof)
    {
        strncpy_s(lpExeName, bufferSize, CHROME_PATH_A, _TRUNCATE);
        *lpdwSize = static_cast<DWORD>(strlen(lpExeName));
    }
    trace(L"QueryFullProcessImageNameA", exeName, spoof);
    return TRUE;
}

/**
 * Hooked K32GetModuleFileNameExW (GetModuleFileNameExW). Only the main executable can end in
 * a whitelisted name like brave.exe, so module queries for DLLs pass through untouched.
 */
DWORD WINAPI MyGetModuleFileNameExW(HANDLE hProcess, HMODULE hModule, LPWSTR lpFilename, DWORD nSize)
{
    DWORD ret = realGetModuleFileNameExW(hProcess, hModule, lpFilename, nSize);
    if (ret == 0)
    {
        return ret;
    }

    std::wstring exeName(lpFilename, ret);
    bool spoof = (hModule == NULL || endsWithCaseInsensitive(exeName, L".exe")) && shouldSpoof(exeName);
    if (spoof)
    {
        wcsncpy_s(lpFilename, nSize, CHROME_PATH_W, _TRUNCATE);
        ret = static_cast<DWORD>(wcslen(lpFilename));
    }
    trace(L"K32GetModuleFileNameExW", exeName, spoof);
    return ret;
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

    traceEnabled = (0 != GetPrivateProfileInt(Ini::GeneralSection::NAME, Ini::GeneralSection::Key::TRACE, 0, iniPath.c_str()));

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

/**
 * Find an export in kernelbase.dll, falling back to kernel32.dll.
 */
static PVOID findKernelFunction(const char* name)
{
    PVOID fn = DetourFindFunction("KernelBase.dll", name);
    if (fn == nullptr)
    {
        fn = DetourFindFunction("kernel32.dll", name);
    }
    return fn;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
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

        realQueryFullProcessImageNameW = (QueryFullProcessImageNameW_t)findKernelFunction("QueryFullProcessImageNameW");
        realQueryFullProcessImageNameA = (QueryFullProcessImageNameA_t)findKernelFunction("QueryFullProcessImageNameA");
        realGetModuleFileNameExW = (GetModuleFileNameExW_t)findKernelFunction("K32GetModuleFileNameExW");
        RETURN_FALSE_IF_NOT(realQueryFullProcessImageNameW != nullptr, "DetourFindFunction returned null for QueryFullProcessImageNameW");

        auto stt = DetourTransactionBegin();
        RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourTransactionBegin");

        stt = DetourUpdateThread(GetCurrentThread());
        RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourUpdateThread");

        stt = DetourAttach(&(PVOID&)realQueryFullProcessImageNameW, MyQueryFullProcessImageNameW);
        RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourAttach MyQueryFullProcessImageNameW");

        if (realQueryFullProcessImageNameA != nullptr)
        {
            stt = DetourAttach(&(PVOID&)realQueryFullProcessImageNameA, MyQueryFullProcessImageNameA);
            RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourAttach MyQueryFullProcessImageNameA");
        }
        else
        {
            logger.log("QueryFullProcessImageNameA not found; not hooked");
        }

        if (realGetModuleFileNameExW != nullptr)
        {
            stt = DetourAttach(&(PVOID&)realGetModuleFileNameExW, MyGetModuleFileNameExW);
            RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourAttach MyGetModuleFileNameExW");
        }
        else
        {
            logger.log("K32GetModuleFileNameExW not found; not hooked");
        }

        stt = DetourTransactionCommit();
        RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourTransactionCommit");

        logger.log(std::string("DLL_PROCESS_ATTACH is ok (hooked QueryFullProcessImageNameW")
            + (realQueryFullProcessImageNameA ? ", QueryFullProcessImageNameA" : "")
            + (realGetModuleFileNameExW ? ", K32GetModuleFileNameExW" : "")
            + (traceEnabled ? "; trace on)" : ")"));
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

        if (realQueryFullProcessImageNameA != nullptr)
        {
            DetourDetach(&(PVOID&)realQueryFullProcessImageNameA, MyQueryFullProcessImageNameA);
        }
        if (realGetModuleFileNameExW != nullptr)
        {
            DetourDetach(&(PVOID&)realGetModuleFileNameExW, MyGetModuleFileNameExW);
        }

        stt = DetourTransactionCommit();
        RETURN_FALSE_IF_NOT((stt == NO_ERROR ), "DetourTransactionCommit");

        logger.log("DLL_PROCESS_DETACH is ok");
    }

    return TRUE;
}
