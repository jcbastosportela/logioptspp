#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <iostream>
#include <cstdio>
#include <windows.h>
#include <detours.h>

DWORD GetProcessIdByName(const wchar_t* processName) {
    DWORD processId = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W processEntry;  // Note the 'W' suffix for the wide-character version
        processEntry.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32FirstW(hSnapshot, &processEntry)) {
            do {
                if (wcscmp(processEntry.szExeFile, processName) == 0) {  // Using wcscmp for wide-character strings
                    processId = processEntry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(hSnapshot, &processEntry));
        }

        CloseHandle(hSnapshot);
    }

    return processId;
}


int _main()
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(STARTUPINFO));
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    si.cb = sizeof(STARTUPINFO);
    /*
    char* DirPath = new char[MAX_PATH];
    char* DLLPath = new char[MAX_PATH]; //testdll.dll
    char* DetourPath = new char[MAX_PATH]; //detoured.dll
    GetCurrentDirectory(MAX_PATH, DirPath);
    sprintf_s(DLLPath, MAX_PATH, "%s\\testdll.dll", DirPath);
    sprintf_s(DetourPath, MAX_PATH, "%s\\detoured.dll", DirPath);
    */

    WCHAR la[] = TEXT("C:\\Program Files\\LogiOptionsPlus\\logioptionsplus_agent.exe");
    //WCHAR la[] = TEXT("C:\\windows\\notepad.exe");
    //CHAR dllPath[] = "C:\\Users\\jcbas\\Projects\\logioptionspp\\logioptionspp\\x64\\Debug\\logioptionspp.dll";  // Replace with your DLL's path
    CHAR dllPath[] = "C:\\Program Files\\LogiOptionsPlus\\logioptionspp.dll";  // Replace with your DLL's path
    LPCSTR paths[] = {dllPath};
    //const char* dllDetouredPath = "C:\\Windows\\System32\\KernelBase.dll";  // Replace with your DLL's path
    BOOL ret = DetourCreateProcessWithDlls(
        NULL,
        la,
        NULL,
        NULL, 
        TRUE,
        CREATE_DEFAULT_ERROR_MODE ,
        NULL,
        NULL,
        &si,
        &pi,
        1,
        paths,
        NULL);

    std::cout << (ret ? "Ok" : "Failed") << std::endl;
    return 0;
}

int main() {
    const wchar_t* targetProcessName = L"logioptionsplus_agent.exe";  // Note the 'L' prefix for wide-character string
    DWORD processId = GetProcessIdByName(targetProcessName);
    std::cout << "The " << targetProcessName << " process id is " << (int)processId << std::endl;
    //char dllPath[] = "C:\\Users\\jcbas\\Projects\\logioptionspp\\logioptionspp\\x64\\Debug\\logioptionspp.dll";  // Replace with your DLL's path
    char dllPath[] = "C:\\Program Files\\LogiOptionsPlus\\logioptionspp.dll";  // Replace with your DLL's path

    /*
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE, FALSE, processId);
    LPVOID LoadLibraryAddr = (LPVOID)GetProcAddress(GetModuleHandle(L"kernel32.dll"),
        "LoadLibraryA");
    LPVOID LLParam = (LPVOID)VirtualAllocEx(hProcess, NULL, strlen(dllPath),
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    WriteProcessMemory(hProcess, LLParam, dllPath, strlen(dllPath), NULL);
    CreateRemoteThread(hProcess, NULL, NULL, (LPTHREAD_START_ROUTINE)LoadLibraryAddr,
        LLParam, NULL, NULL);
    CloseHandle(hProcess);
    */
    
    // Open a handle to the target process (logioptionsplus_agent.exe)
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);

    if (hProcess != NULL) {
        LPVOID remoteMemory = VirtualAllocEx(hProcess, NULL, sizeof(dllPath), MEM_COMMIT, PAGE_READWRITE);

        if (remoteMemory != NULL) {
            auto ret = WriteProcessMemory(hProcess, remoteMemory, dllPath, sizeof(dllPath), NULL);
            if (!ret)
            {
                std::cerr << "Failed WriteProcessMemory" << std::endl;
                return ret;
            }

            HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
                (LPTHREAD_START_ROUTINE)LoadLibraryW, remoteMemory, 0, NULL);

            if (hThread != NULL) {
                WaitForSingleObject(hThread, INFINITE);
                CloseHandle(hThread);
            }

            VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
        }

        CloseHandle(hProcess);
    }
    else
    {
        std::cout << "failed" << std::endl;
    }
    /*
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (hProcess != NULL) {
        // Allocate memory for the DLL path in the target process
        LPVOID remoteMemory = VirtualAllocEx(hProcess, NULL, strlen(dllPath) + 1, MEM_COMMIT, PAGE_READWRITE);

        if (remoteMemory != NULL) {
            // Write the DLL path into the allocated memory
            WriteProcessMemory(hProcess, remoteMemory, dllPath, strlen(dllPath) + 1, NULL);

            // Get the address of LoadLibraryA function from kernel32.dll
            //LPTHREAD_START_ROUTINE pLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
            LPTHREAD_START_ROUTINE pLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");

            // Create a remote thread in the target process to load the DLL
            HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pLoadLibrary, remoteMemory, 0, NULL);

            if (hThread != NULL) {
                // Wait for the remote thread to finish
                WaitForSingleObject(hThread, INFINITE);
                CloseHandle(hThread);
            }

            // Free the allocated memory in the target process
            VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
        }

        // Close the handle to the target process
        CloseHandle(hProcess);
    }*/

    return 0;
}
