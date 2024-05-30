
#include "Logger.hpp"

#include <Windows.h>
#include <detours.h>
#include <iostream>

#include <vector>
#include <algorithm>
#include <functional>
#include <filesystem>

using Logger = utils::Logger;

auto& logger{Logger::getInstance()};

namespace Ini {
    constexpr wchar_t DEFAULT_INI[]{ L".\\conf.ini" };
    namespace GeneralSection {
        constexpr wchar_t NAME[]{ L"General" };
        namespace Key {
            constexpr wchar_t BLACKLIST_APPS[]{ L"BLACKLIST_APPS" };
            constexpr wchar_t WHITELIST_APPS[]{ L"WHITELIST_APPS" };    // only one of WHITELIST_APPS or BLACKLIST_APPS shall be defined; WHITELIST_APPS preceeds
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
    // This function serves as the entry point for Detours
    // Perform any initialization or setup required for your hooks
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
 * Checks if a T ends with a something.
 * 
 * T needs to support .length() and .compare()
 * 
 * \param exeName
 * \param appToMask
 * \return 
 */
template<class T>
static bool endsWith(const T& fullString, const T& ending) {
    if (fullString.length() >= ending.length()) {
        return (fullString.compare(fullString.length() - ending.length(), ending.length(), ending) == 0);
    }
    return false;
}

/**
 * Declare the custom function with the same signature
 * 
 * \param hProcess
 * \param dwFlags
 * \param lpExeName
 * \param lpdwSize
 * \return \c true if ok, \c false otherwise
 */
BOOL WINAPI MyQueryFullProcessImageNameW(
    HANDLE hProcess,
    DWORD dwFlags,
    LPWSTR lpExeName,
    PDWORD lpdwSize)
{
    BOOL ret = TRUE;

    // Call the original function using Detours
    ret = realQueryFullProcessImageNameW(hProcess, dwFlags, lpExeName, lpdwSize);

    // if the whitelisting is enabled, it preceeds
    if(appsToWhitelist.size() > 0)
    {
        // check if the active application is to be white listed (force apply smooth)
        if (std::find_if(
                appsToWhitelist.begin(),
                appsToWhitelist.end(),
                std::bind(endsWith<std::wstring>, std::wstring(lpExeName), std::placeholders::_1)
            ) != appsToWhitelist.end()) // if is not on the list, so we smooth it
        {
            wcsncpy_s(lpExeName, *lpdwSize, L"c:\\bla\\chrome.exe\0", *lpdwSize);
            *lpdwSize = static_cast<std::remove_pointer<decltype(lpdwSize)>::type>(wcslen(lpExeName));
            ret = true;
        }
    }
    else
    {
        // check if the active application is NOT to be black listed (not force apply smooth)
        if (std::find_if(
                appsToBlacklist.begin(),
                appsToBlacklist.end(),
                std::bind(endsWith<std::wstring>, std::wstring(lpExeName), std::placeholders::_1)
            ) == appsToBlacklist.end()) // if is not on the list, so we smooth it
        {
            wcsncpy_s(lpExeName, *lpdwSize, L"c:\\bla\\chrome.exe\0", *lpdwSize);
            *lpdwSize = static_cast<std::remove_pointer<decltype(lpdwSize)>::type>(wcslen(lpExeName));
            ret = true;
        }
    }

    return ret;
}

/**
 * Gets the directory 
 * 
 * \param hModule   DLL handle
 * \return Directory path of the DLL
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
 * Loads the configs from the INI file
 * 
 * \param iniPath   Path to the INI file
 * \return 0 if good
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

    static wchar_t buffer[10000];   // static so goes to heap (!not thread safe!)
    // TODO: we need to runtime check if the buffer was sufficient by look the return of GetPrivateProfileString
    if (0 != GetPrivateProfileString(Ini::GeneralSection::NAME, Ini::GeneralSection::Key::LOG_PATH, L"", buffer, sizeof(buffer) / sizeof(wchar_t), iniPath.c_str()))
    {
        logger.setLogFilePath(std::wstring(buffer));
        logger.enable();
    }

    logger.log("load_configs");

    // lazy lambda
    auto get_app_filter_list = [&](constexpr wchar_t* key, std::vector<std::wstring>& appsFilterList)
    {
        if (0 != GetPrivateProfileString(Ini::GeneralSection::NAME, key, L"", buffer, sizeof(buffer) / sizeof(wchar_t), iniPath.c_str()))
        {
            // Create a wistringstream from the input wstring
            std::wistringstream wiss(buffer);

            // Split the wstring using ',' as the delimiter and trim spaces
            std::wstring token;
            while (std::getline(wiss >> std::ws, token, L','))
            {
                auto app = trim(token);
                appsFilterList.push_back(app);
            }

            // Print the split substrings
            logger.log("Applications in " + key + " are: ");
            for (const auto& substring : appsFilterList) {
                logger.log(substring);
            }
        }
    };

    // check if there is blacklisting
    get_app_filter_list(Ini::GeneralSection::Key::BLACKLIST_APPS, appsToBlacklist);

    // check if there is whitelisting
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
        RETURN_FALSE_IF(dllDirPath.empty(), "DetourTransactionBegin");

        load_configs(dllDirPath / Ini::DEFAULT_INI);

        logger.log("DLL_PROCESS_ATTACH started");

        // Initialize Detours
        auto stt = DetourTransactionBegin();
        RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourTransactionBegin");

        stt = DetourUpdateThread(GetCurrentThread());
        RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourUpdateThread");

        realQueryFullProcessImageNameW = (QueryFullProcessImageNameW_t)DetourFindFunction("KernelBase.dll", "QueryFullProcessImageNameW");
        stt = DetourAttach(&(PVOID&)realQueryFullProcessImageNameW, MyQueryFullProcessImageNameW);
        RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourAttach MyQueryFullProcessImageNameW");

        stt = DetourTransactionCommit();
        RETURN_FALSE_IF_NOT((stt == NO_ERROR), "DetourTransactionCommit");

        logger.log("DLL_PROCESS_ATTACH is ok");
    }

    if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        logger.log("DLL_PROCESS_DETACH started");
        
        // Uninitialize Detours
        auto stt = DetourTransactionBegin();
        RETURN_FALSE_IF_NOT((stt == NO_ERROR ), "DetourTransactionBegin");
        
        stt = DetourUpdateThread(GetCurrentThread());
        RETURN_FALSE_IF_NOT((stt == NO_ERROR ), "DetourUpdateThread");
        
        stt = DetourDetach(&(PVOID &)realQueryFullProcessImageNameW, MyQueryFullProcessImageNameW);
        RETURN_FALSE_IF_NOT((stt == NO_ERROR ), "DetourAttach MyQueryFullProcessImageNameW");

        stt = DetourTransactionCommit();
        RETURN_FALSE_IF_NOT((stt == NO_ERROR ), "DetourTransactionCommit");

        logger.log("DLL_PROCESS_DETACH is ok");
    }

    return TRUE;
}
