#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <iostream>
#include <cstdio>
#include <windows.h>
#include <detours.h>

static int inject_with_thread();
static int preload_and_start();

DWORD GetProcessIdByName(const wchar_t *processName)
{
    DWORD processId = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hSnapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W processEntry; // Note the 'W' suffix for the wide-character version
        processEntry.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32FirstW(hSnapshot, &processEntry))
        {
            do
            {
                if (wcscmp(processEntry.szExeFile, processName) == 0)
                { // Using wcscmp for wide-character strings
                    processId = processEntry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(hSnapshot, &processEntry));
        }

        CloseHandle(hSnapshot);
    }

    return processId;
}

int main()
{
    // return preload_and_start();  // Not yet tested
    return inject_with_thread();
}

static int preload_and_start()
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(STARTUPINFO));
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    si.cb = sizeof(STARTUPINFO);

    // TODO: remove hardcoded paths
    WCHAR la[] = TEXT("C:\\Program Files\\LogiOptionsPlus\\logioptionsplus_agent.exe");
    CHAR dllPath[] = "C:\\Users\\jcbas\\Projects\\logioptionspp\\logioptionspp\\x64\\Debug\\logioptionspp.dll"; // Replace with your DLL's path
    LPCSTR paths[] = {dllPath};

    BOOL ret = DetourCreateProcessWithDlls(
        NULL,
        la,
        NULL,
        NULL,
        FALSE,
        CREATE_DEFAULT_ERROR_MODE,
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

static int inject_with_thread()
{
    const wchar_t *targetProcessName = L"logioptionsplus_agent.exe"; // Note the 'L' prefix for wide-character string
    DWORD processId = GetProcessIdByName(targetProcessName);
    wchar_t dllPath[] = L"C:\\Users\\jcbas\\Projects\\logioptionspp\\logioptionspp\\x64\\Debug\\logioptionspp.dll"; // Replace with your DLL's path

    // Open a handle to the target process (logioptionsplus_agent.exe)
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);

    if (hProcess == NULL)
    {
        std::cerr << "Failed OpenProcess" << std::endl;
        return -1;
    }

    LPVOID remoteMemory = VirtualAllocEx(hProcess, NULL, wcslen(dllPath), MEM_COMMIT, PAGE_READWRITE);
    if (remoteMemory == NULL)
    {
        std::cerr << "Failed VirtualAllocEx" << std::endl;
        return -2;
    }

    if (!WriteProcessMemory(hProcess, remoteMemory, dllPath, sizeof(dllPath), NULL))
    {
        std::cerr << "Failed WriteProcessMemory" << std::endl;
        return -3;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)LoadLibraryW, remoteMemory, 0, NULL);
    if (hThread != NULL)
    {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }

    if (!VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE))
    {
        std::cerr << "Failed VirtualFreeEx" << std::endl;
        return -4;
    }

    if (!CloseHandle(hProcess))
    {
        std::cerr << "Failed CloseHandle" << std::endl;
        return -4;
    }

    return 0;
}
