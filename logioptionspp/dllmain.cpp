
#include <Windows.h>
#include <winnt.h>
#include <detours.h>
#include <iostream>
#include <ntstatus.h> // Include the header for NTSTATUS values
#include <iostream>
#include <fstream>
#include <string>
#include <mutex>

// don't keep this enabled
// #define ENABLE_CRAZY_LOGGER

#define ASSERT_GOOD(_r, _m)                             \
    {                                                   \
        if (!(_r))                                      \
        {                                               \
            std::cerr << "ERROR " << (_m) << std::endl; \
            logger.log(_m);                             \
            return (_r);                                \
        }                                               \
    }


class Logger
{
private:
    Logger() = default;

    ~Logger()
    {
        // Close the log file if open
        if (logFile.is_open())
        {
            logFile.close();
        }
    }

    // Declare private copy constructor and assignment operator to prevent copies
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

public:
    static Logger &getInstance()
    {
        static Logger instance;
        return instance;
    }

    void log(const std::string &message)
    {
#ifdef ENABLE_CRAZY_LOGGER
        std::lock_guard<std::mutex> lock(mutex); // Ensure thread safety

        if (!logFile.is_open())
        {
            // TODO: remove hardcoded path
            logFile.open("C:\\Users\\jcbas\\Projects\\logioptionspp\\logioptionspp\\x64\\Debug\\log.txt", std::ios::app);
        }

        if (logFile.is_open())
        {
            logFile << message << std::endl;
            logFile.close();
        }
#endif
    }

private:
    std::ofstream logFile;
    std::mutex mutex;
};

// NOTE: this is needed in case we use detours to preload.
// reason is: "The new process will fail to start if the target DLL does not contain a exported function with ordinal #1."
// source: https://github.com/microsoft/Detours/wiki/DetourCreateProcessWithDlls
__declspec(dllexport) void __stdcall MyDetoursInitializationFunction(void)
{
    // This function serves as the entry point for Detours
    // Perform any initialization or setup required for your hooks
    Logger::getInstance().log("MyDetoursInitializationFunction");
}

// Define a typedef for the original function signature
typedef BOOL(WINAPI *QueryFullProcessImageNameW_t)(
    HANDLE hProcess,
    DWORD dwFlags,
    LPWSTR lpExeName,
    PDWORD lpdwSize);

// Define a typedef for the original function signature
typedef BOOL(WINAPI *QueryFullProcessImageNameA_t)(
    HANDLE hProcess,
    DWORD dwFlags,
    LPSTR lpExeName,
    PDWORD lpdwSize);

QueryFullProcessImageNameW_t funcToDetour = (QueryFullProcessImageNameW_t)(nullptr);  // Set it at address to detour in
QueryFullProcessImageNameA_t funcToDetourA = (QueryFullProcessImageNameA_t)(nullptr); // Set it at address to detour in

// Declare the custom function with the same signature
BOOL WINAPI MyQueryFullProcessImageNameW(
    HANDLE hProcess,
    DWORD dwFlags,
    LPWSTR lpExeName,
    PDWORD lpdwSize)
{
    BOOL ret = TRUE;
    Logger::getInstance().log("MyQueryFullProcessImageNameW");

    // Call the original function using Detours
    /*QueryFullProcessImageNameW_t originalFunction =
        (QueryFullProcessImageNameW_t)DetourFindFunction("KernelBase.dll", "QueryFullProcessImageNameW");

    if (originalFunction) {

        ret = originalFunction(hProcess, dwFlags, lpExeName, lpdwSize);
    }*/

    wcsncpy_s(lpExeName, *lpdwSize, L"c:\\bla\\chrome.exe\0", *lpdwSize);
    *lpdwSize = wcslen(lpExeName);

    // Handle if the original function cannot be found
    return ret;
}

// Declare the custom function with the same signature
BOOL WINAPI MyQueryFullProcessImageNameA(
    HANDLE hProcess,
    DWORD dwFlags,
    LPSTR lpExeName,
    PDWORD lpdwSize)
{
    BOOL ret = TRUE;
    Logger::getInstance().log("MyQueryFullProcessImageNameA");

    // Call the original function using Detours
    /*QueryFullProcessImageNameA_t originalFunction =
        (QueryFullProcessImageNameA_t)DetourFindFunction("KernelBase.dll", "QueryFullProcessImageNameA");

    if (originalFunction) {

        ret = originalFunction(hProcess, dwFlags, lpExeName, lpdwSize);
    }*/

    strncpy_s(lpExeName, *lpdwSize, "c:\\bla\\chrome.exe\0", *lpdwSize);
    *lpdwSize = strlen(lpExeName);

    // Handle if the original function cannot be found
    return ret;
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
        logger.log("DLL_PROCESS_ATTACH started");
        /*bool ret;
        ret = DetourRestoreAfterWith();
        ASSERT_GOOD(ret, "DetourRestoreAfterWith");
        ret = DisableThreadLibraryCalls(hModule);
        ASSERT_GOOD(ret, "DisableThreadLibraryCalls");
        */
        // Initialize Detours
        auto stt = DetourTransactionBegin();
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourTransactionBegin");

        stt = DetourUpdateThread(GetCurrentThread());
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourUpdateThread");

        // NOTE: the line below will not work as it will get from kernel32.dll instead of kernelbase.dll
        // funcToDetour = QueryFullProcessImageNameW;
        // logger.log("Address of QueryFullProcessImageNameW is " + std::to_string((unsigned long int)funcToDetour));

        funcToDetour = (QueryFullProcessImageNameW_t)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "QueryFullProcessImageNameW");
        logger.log("Address of QueryFullProcessImageNameW is " + std::to_string((unsigned long int)funcToDetour));

        // funcToDetourA = QueryFullProcessImageNameA;
        // logger.log("Address of QueryFullProcessImageNameA is " + std::to_string((unsigned long int)funcToDetourA));

        stt = DetourAttach(&(PVOID &)funcToDetour, MyQueryFullProcessImageNameW);
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourAttach MyQueryFullProcessImageNameW");

        // stt = DetourAttach(&(PVOID&)funcToDetourA, MyQueryFullProcessImageNameA);
        // ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourAttach MyQueryFullProcessImageNameA");

        stt = DetourTransactionCommit();
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourTransactionCommit");

        logger.log("DLL_PROCESS_ATTACH is ok");
    }

    if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        logger.log("DLL_PROCESS_DETACH started");
        /*bool ret;
        ret = DetourRestoreAfterWith();
        ASSERT_GOOD(ret, "DetourRestoreAfterWith");
        ret = DisableThreadLibraryCalls(hModule);
        ASSERT_GOOD(ret, "DisableThreadLibraryCalls");
        */
        // Initialize Detours
        // Uninitialize Detours
        auto stt = DetourTransactionBegin();
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourTransactionBegin");
        stt = DetourUpdateThread(GetCurrentThread());
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourUpdateThread");
        funcToDetour = QueryFullProcessImageNameW;
        stt = DetourDetach(&(PVOID &)funcToDetour, MyQueryFullProcessImageNameW);
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourAttach MyQueryFullProcessImageNameW");
        // stt = DetourDetach(&(PVOID&)funcToDetourA, MyQueryFullProcessImageNameA);
        // ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourAttach MyQueryFullProcessImageNameA");
        stt = DetourTransactionCommit();
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourTransactionCommit");

        logger.log("DLL_PROCESS_DETACH is ok");
    }

    return TRUE;
}
