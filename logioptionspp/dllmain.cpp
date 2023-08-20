
#include <Windows.h>
#include <winnt.h>
#include <detours.h>
#include <iostream>

__declspec(dllexport) void __stdcall MyDetoursInitializationFunction(void)
{
    // This function serves as the entry point for Detours
    // Perform any initialization or setup required for your hooks
    std::cerr << "merda" << std::endl;
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
        (QueryFullProcessImageNameW_t)DetourFindFunction("kernelbase.dll", "QueryFullProcessImageNameW");

    if (originalFunction) {

        ret = originalFunction(hProcess, dwFlags, lpExeName, lpdwSize);
    }

    wcsncpy_s(lpExeName, *lpdwSize, L"c:\\bla\\chrome.exe\0", *lpdwSize);
    *lpdwSize = wcslen(lpExeName);
    
    // Handle if the original function cannot be found
    return ret;
}
/*
void BeginRedirect(LPVOID);

#define SIZE 6
QueryFullProcessImageNameW_t pOrigMBAddress = NULL;
BYTE oldBytes[SIZE] = { 0 };
BYTE JMP[SIZE] = { 0 };
DWORD oldProtect, myProtect = PAGE_EXECUTE_READWRITE;

INT APIENTRY DllMain(HMODULE hDLL, DWORD Reason, LPVOID Reserved)
{
    switch (Reason)
    {
    case DLL_PROCESS_ATTACH:
        pOrigMBAddress = (QueryFullProcessImageNameW_t)
            GetProcAddress(GetModuleHandle(L"kernelbase.dll"),
                "QueryFullProcessImageNameW");
        if (pOrigMBAddress != NULL)
            BeginRedirect(MyQueryFullProcessImageNameW);
        break;
    case DLL_PROCESS_DETACH:
        memcpy(pOrigMBAddress, oldBytes, SIZE);
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}

void BeginRedirect(LPVOID newFunction)
{
    BYTE tempJMP[SIZE] = { 0xE9, 0x90, 0x90, 0x90, 0x90, 0xC3 };
    memcpy(JMP, tempJMP, SIZE);
    DWORD JMPSize = ((DWORD)newFunction - (DWORD)pOrigMBAddress - 5);
    VirtualProtect((LPVOID)pOrigMBAddress, SIZE,
        PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(oldBytes, pOrigMBAddress, SIZE);
    memcpy(&JMP[1], &JMPSize, 4);
    memcpy(pOrigMBAddress, JMP, SIZE);
    VirtualProtect((LPVOID)pOrigMBAddress, SIZE, oldProtect, NULL);
}
*/

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (DetourIsHelperProcess()) {
        return TRUE;
    }
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DetourRestoreAfterWith();
        DisableThreadLibraryCalls(hModule);
        // Initialize Detours
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        //DetourAttach(&(PVOID&)QueryFullProcessImageNameW, MyQueryFullProcessImageNameW);
        funcToDetour = (QueryFullProcessImageNameW_t)
            GetProcAddress(GetModuleHandle(L"kernelbase.dll"),
                "QueryFullProcessImageNameW");
        DetourAttach(&(PVOID&)funcToDetour, MyQueryFullProcessImageNameW);
        auto error = DetourTransactionCommit();

        if (error == NO_ERROR) {
            printf("simple" DETOURS_STRINGIFY(DETOURS_BITS) ".dll:"
                " Detoured SleepEx().\n");
        }
        else {
            printf("simple" DETOURS_STRINGIFY(DETOURS_BITS) ".dll:"
                " Error detouring SleepEx(): %ld\n", error);
        }
    }

    if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        // Uninitialize Detours
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        //DetourDetach(&(PVOID&)QueryFullProcessImageNameW, MyQueryFullProcessImageNameW);
        DetourDetach(&(PVOID&)funcToDetour, MyQueryFullProcessImageNameW);
        DetourTransactionCommit();
    }

    return TRUE;
}


