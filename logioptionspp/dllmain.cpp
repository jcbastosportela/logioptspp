
#include <Windows.h>
#include <winnt.h>
#include <detours.h>
#include <iostream>
#include <ntstatus.h>  // Include the header for NTSTATUS values
#include <iostream>
#include <fstream>
#include <string>
#include <mutex>

class Logger {
private:
    Logger() {
        // Open the log file for writing
        //logFile.open("log.txt", std::ios::app);
    }

    ~Logger() {
        // Close the log file
        if (logFile.is_open()) {
            logFile.close();
        }
    }

    // Declare private copy constructor and assignment operator to prevent copies
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void log(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex); // Ensure thread safety

        logFile.open("log.txt", std::ios::app);

        if (logFile.is_open()) {
            logFile << message << std::endl;
            logFile.close();
        }
    }

private:
    std::ofstream logFile;
    std::mutex mutex;
};

__declspec(dllexport) void __stdcall MyDetoursInitializationFunction(void)
{
    // This function serves as the entry point for Detours
    // Perform any initialization or setup required for your hooks
    std::cerr << "merda" << std::endl;
    Logger& logger = Logger::getInstance();
    logger.log("MyDetoursInitializationFunction");
}

// Define a typedef for the original function signature
typedef BOOL(WINAPI* QueryFullProcessImageNameW_t)(
    HANDLE hProcess,
    DWORD dwFlags,
    LPWSTR lpExeName,
    PDWORD lpdwSize
    );

// Define a typedef for the original function signature
typedef BOOL(WINAPI* QueryFullProcessImageNameA_t)(
    HANDLE hProcess,
    DWORD dwFlags,
    LPSTR lpExeName,
    PDWORD lpdwSize
    );

QueryFullProcessImageNameW_t funcToDetour = (QueryFullProcessImageNameW_t)(0x00007FFB7F5CA320); //Set it at address to detour in
QueryFullProcessImageNameA_t funcToDetourA = (QueryFullProcessImageNameA_t)(0x00007FFB7F5CA320); //Set it at address to detour in

// Declare the custom function with the same signature
BOOL WINAPI MyQueryFullProcessImageNameW(
    HANDLE hProcess,
    DWORD dwFlags,
    LPWSTR lpExeName,
    PDWORD lpdwSize
)
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
    PDWORD lpdwSize
)
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

#define ASSERT_GOOD(_r, _m) \
{\
if(!(_r)){\
std::cerr << "ERROR " << (_m) << std::endl;\
logger.log(_m);\
return (_r);\
}\
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    Logger& logger = Logger::getInstance();

    if (DetourIsHelperProcess()) {
        return TRUE;
    }
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        logger.log("DLL_PROCESS_ATTACH started");
        bool ret;
        ret = DetourRestoreAfterWith();
        ASSERT_GOOD(ret, "DetourRestoreAfterWith");
        ret = DisableThreadLibraryCalls(hModule);
        ASSERT_GOOD(ret, "DisableThreadLibraryCalls");
        // Initialize Detours
        auto stt = DetourTransactionBegin();
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourTransactionBegin");
        stt = DetourUpdateThread(GetCurrentThread());
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourUpdateThread");
        funcToDetour = QueryFullProcessImageNameW;
        funcToDetourA = QueryFullProcessImageNameA;
        logger.log("Address of QueryFullProcessImageNameW is " + std::to_string((unsigned long int)funcToDetour));
        logger.log("Address of QueryFullProcessImageNameA is " + std::to_string((unsigned long int)funcToDetourA));
        stt = DetourAttach(&(PVOID&)funcToDetour, MyQueryFullProcessImageNameW);
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourAttach MyQueryFullProcessImageNameW");
        stt = DetourAttach(&(PVOID&)funcToDetourA, MyQueryFullProcessImageNameA);
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourAttach MyQueryFullProcessImageNameA");
        stt = DetourTransactionCommit();
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourTransactionCommit");

        logger.log("DLL_PROCESS_ATTACH is ok");
        /*WCHAR name[1000];
        DWORD size = 1000;
        QueryFullProcessImageNameW(0, 0, name, &size);*/
    }

    if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        // Uninitialize Detours
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)funcToDetour, MyQueryFullProcessImageNameW);
        DetourDetach(&(PVOID&)funcToDetourA, MyQueryFullProcessImageNameA);
        DetourTransactionCommit();
    }

    return TRUE;
}


