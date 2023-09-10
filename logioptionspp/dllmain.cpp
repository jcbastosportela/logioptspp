
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

constexpr wchar_t DEFAULT_INI[]{ L".\\conf.ini" };
static std::vector<std::wstring>    appsToMaskW;

#define ASSERT_GOOD(_r, _m)                             \
    {                                                   \
        if (!(_r))                                      \
        {                                               \
            std::cerr << "ERROR " << (_m) << std::endl; \
            logger.log(_m);              \
            return (_r);                                \
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

QueryFullProcessImageNameW_t funcToDetour = (QueryFullProcessImageNameW_t)(nullptr);  // Set it at address to detour in

QueryFullProcessImageNameW_t realQueryFullProcessImageNameW = QueryFullProcessImageNameW;

/**
 * .
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

// Declare the custom function with the same signature
BOOL WINAPI MyQueryFullProcessImageNameW(
    HANDLE hProcess,
    DWORD dwFlags,
    LPWSTR lpExeName,
    PDWORD lpdwSize)
{
    BOOL ret = TRUE;

    // Call the original function using Detours
    ret = realQueryFullProcessImageNameW(hProcess, dwFlags, lpExeName, lpdwSize);

    // check if the active application is to be masked
    if (std::find_if(
            appsToMaskW.begin(),
            appsToMaskW.end(),
            std::bind(endsWith<std::wstring>, std::wstring(lpExeName), std::placeholders::_1)
        ) != appsToMaskW.end())
    {
        wcsncpy_s(lpExeName, *lpdwSize, L"c:\\bla\\chrome.exe\0", *lpdwSize);
        *lpdwSize = static_cast<std::remove_pointer<decltype(lpdwSize)>::type>(wcslen(lpExeName));
        ret = true;
    }

    return ret;
}

static int load_configs(HMODULE hModule)
{
    constexpr size_t MAX_FILE_LEN{ 1000 };
    wchar_t dllPath[MAX_FILE_LEN];
    auto ret = GetModuleFileName(hModule, dllPath, MAX_FILE_LEN);
    ASSERT_GOOD( (ret != 0), "GetModuleFileName");
    auto dllDir = std::filesystem::absolute(std::filesystem::path(dllPath)).parent_path();
    auto iniPath = dllDir / std::filesystem::path(DEFAULT_INI);

    auto trim = [](const std::wstring& str) -> std::wstring {
        size_t first = str.find_first_not_of(L" \t\r\n");
        size_t last = str.find_last_not_of(L" \t\r\n");
        if (first == std::wstring::npos || last == std::wstring::npos) {
            return L"";
        }
        return str.substr(first, (last - first + 1));
        };

    wchar_t buffer[1000];
    if (0 != GetPrivateProfileString(L"General", L"LOG_PATH", L"", buffer, sizeof(buffer) / sizeof(wchar_t), iniPath.c_str()))
    {
        logger.setLogFilePath(std::wstring(buffer));
        logger.enable();
    }

    logger.log("load_configs");

    if (0 != GetPrivateProfileString(L"General", L"APPS_TO_MASK", L"", buffer, sizeof(buffer) / sizeof(wchar_t), iniPath.c_str()))
    {
        // Create a wistringstream from the input wstring
        std::wistringstream wiss(buffer);

        // Split the wstring using ',' as the delimiter and trim spaces
        std::wstring token;
        while (std::getline(wiss >> std::ws, token, L','))
        {
            auto app = trim(token);
            appsToMaskW.push_back(app);
        }

        // Print the split substrings
        for (const auto& substring : appsToMaskW) {
            logger.log(substring);
        }
    }

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
        load_configs(hModule);

        logger.log("DLL_PROCESS_ATTACH started");

        // Initialize Detours
        auto stt = DetourTransactionBegin();
        ASSERT_GOOD((stt == NO_ERROR), "DetourTransactionBegin");

        stt = DetourUpdateThread(GetCurrentThread());
        ASSERT_GOOD((stt == NO_ERROR), "DetourUpdateThread");

        /*
        // NOTE: the line below will not work as it will get from kernel32.dll instead of kernelbase.dll
        // funcToDetour = QueryFullProcessImageNameW;
        */
        realQueryFullProcessImageNameW = (QueryFullProcessImageNameW_t)DetourFindFunction("KernelBase.dll", "QueryFullProcessImageNameW");
        stt = DetourAttach(&(PVOID&)realQueryFullProcessImageNameW, MyQueryFullProcessImageNameW);
        ASSERT_GOOD((stt == NO_ERROR), "DetourAttach MyQueryFullProcessImageNameW");

        stt = DetourTransactionCommit();
        ASSERT_GOOD((stt == NO_ERROR), "DetourTransactionCommit");

        logger.log("DLL_PROCESS_ATTACH is ok");
    }

    if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        logger.log("DLL_PROCESS_DETACH started");
        
        // Uninitialize Detours
        auto stt = DetourTransactionBegin();
        ASSERT_GOOD((stt == NO_ERROR ), "DetourTransactionBegin");
        
        stt = DetourUpdateThread(GetCurrentThread());
        ASSERT_GOOD((stt == NO_ERROR ), "DetourUpdateThread");
        
        funcToDetour = QueryFullProcessImageNameW;
        stt = DetourDetach(&(PVOID &)funcToDetour, MyQueryFullProcessImageNameW);
        ASSERT_GOOD((stt == NO_ERROR ), "DetourAttach MyQueryFullProcessImageNameW");

        stt = DetourTransactionCommit();
        ASSERT_GOOD((stt == NO_ERROR ), "DetourTransactionCommit");

        logger.log("DLL_PROCESS_DETACH is ok");
    }

    return TRUE;
}
