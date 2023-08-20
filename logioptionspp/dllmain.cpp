
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

QueryFullProcessImageNameW_t funcToDetour = (QueryFullProcessImageNameW_t)(0x00007FFCE2A1A320); //Set it at address to detour in

// Declare the custom function with the same signature
BOOL WINAPI MyQueryFullProcessImageNameW(
    HANDLE hProcess,
    DWORD dwFlags,
    LPWSTR lpExeName,
    PDWORD lpdwSize
)
{
    BOOL ret = TRUE;
    
    // Call the original function using Detours
    QueryFullProcessImageNameW_t originalFunction =
        (QueryFullProcessImageNameW_t)DetourFindFunction("KernelBase.dll", "QueryFullProcessImageNameW");

    if (originalFunction) {

        ret = originalFunction(hProcess, dwFlags, lpExeName, lpdwSize);
    }

    wcsncpy_s(lpExeName, *lpdwSize, L"c:\\bla\\chrome.exe\0", *lpdwSize);
    *lpdwSize = wcslen(lpExeName);
    
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

        auto ret = DetourRestoreAfterWith();
        ASSERT_GOOD(ret, "DetourRestoreAfterWith");
        ret = DisableThreadLibraryCalls(hModule);
        ASSERT_GOOD(ret, "DisableThreadLibraryCalls");
        // Initialize Detours
        auto stt = DetourTransactionBegin();
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourTransactionBegin");
        stt = DetourUpdateThread(GetCurrentThread());
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourUpdateThread");
        funcToDetour = QueryFullProcessImageNameW;
        stt = DetourAttach(&(PVOID&)funcToDetour, MyQueryFullProcessImageNameW);
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourAttach");
        stt = DetourTransactionCommit();
        ASSERT_GOOD((stt == STATUS_SUCCESS), "DetourTransactionCommit");

        logger.log("DLL_PROCESS_ATTACH is ok");
    }

    if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        // Uninitialize Detours
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)QueryFullProcessImageNameW, MyQueryFullProcessImageNameW);
        //DetourDetach(&(PVOID&)funcToDetour, MyQueryFullProcessImageNameW);
        DetourTransactionCommit();
    }

    return TRUE;
}


