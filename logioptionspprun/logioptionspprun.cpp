/********************************************************************
 * \file   logioptionspprun.cpp
 * \brief  Injects or Preloads "logioptionspp.dll" in the
 *          logioptionsplus_agent.exe
 * 
 * \author jcbastosportela
 * \date   September 2023
 *********************************************************************/
#include <windows.h>
#include <detours.h>    // to implement preloadAndStart
#include <tlhelp32.h>   // to implement getProcessIdByName
#include <iostream>
#include <filesystem>   // handle paths

// Some constants
constexpr wchar_t DEFAULT_INI[]{L"conf.ini"};
constexpr wchar_t DEFAULT_LOGI_PATH[]{ L"C:\\Program Files\\LogiOptionsPlus\\logioptionsplus_agent.exe" };
constexpr wchar_t DLL_NAME[]{ L"logioptionspp.dll" };

// HELPERS for arguments
template <typename T>
using OptArg = std::pair<bool, T>;      // an optional argument is present (true/false) and has a value of a certain type
using Arguments = std::tuple<
    bool,                   // HELP
    OptArg<std::string>,    // MODE
    OptArg<std::string>,    // LOGI_PATH
    OptArg<std::string>     // INI_CONF
>;

// arguments positions on the tuple
enum ArgIdx : size_t
{
    HELP,
    MODE,
    LOGI_PATH,
    INI_CONF,
};

/**
 * Get the process ID by process name
 *
 * \param processName   The name of the process to look for
 * \return process id of the process of the given `processName`; 0 if not found
 */
static DWORD getProcessIdByName(const wchar_t* processName);

/**
 * Parse the input arguments
 *
 * \param argc
 * \param argv
 * \return
 */
static Arguments parseArgs(int argc, const char* const argv[]);

/**
 * Inject the DLL by starting a thread on the target process.
 *
 * \param processId         PID of the agent
 * \param dllPath           Path to the DLL to preload
 * \return 0 if good
 */
static int injectWithThread(DWORD processId, const wchar_t* dllPath);

/**
 * Preload the process and start it
 *
 * \param cmd Command line as defined for CreateProcess API.
 * \param dllPath           Path to the DLL to preload
 * \return 0 if good
 */
static int preloadAndStart(const wchar_t* cmd, const char* dllPath);


// --------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    std::filesystem::path executablePath = std::filesystem::absolute(std::filesystem::path(argv[0])).parent_path();
    auto isPreload = false;
    auto logioptsagentCmd = std::wstring(DEFAULT_LOGI_PATH);
    std::filesystem::path iniFilePath = executablePath / std::filesystem::path(DEFAULT_INI);
    std::filesystem::path dllPath = executablePath / std::filesystem::path(DLL_NAME);

    DWORD logioptionsagentProcessId = getProcessIdByName(
        std::filesystem::path(logioptsagentCmd).filename().c_str()
    );

    auto args = parseArgs(argc, argv);

    if (std::get<ArgIdx::HELP>(args))
    {
        // do not do anything else when we are asked to show the help
        return 0;
    }

    // if an alternative INI is given
    if (std::get<ArgIdx::INI_CONF>(args).first)
    {
        // use the given path
        iniFilePath = std::filesystem::absolute(std::wstring(
            std::get<ArgIdx::INI_CONF>(args).second.begin(),
            std::get<ArgIdx::INI_CONF>(args).second.end()
        ));
    }

    wchar_t buffer[1000];
    if (0 != GetPrivateProfileString(L"General", L"MODE", L"", buffer, sizeof(buffer) / sizeof(wchar_t), iniFilePath.c_str()))
    {
        auto mode = std::wstring(buffer);
        isPreload = (mode.rfind(L"PRE") == 0);
    }
    else
    {
        // when not defined in the INI we will guess what is best, basically, if agent not running preload
        isPreload = (0 == logioptionsagentProcessId);
    }

    if (0 != GetPrivateProfileString(L"General", L"LOGI_PATH", L"", buffer, sizeof(buffer) / sizeof(wchar_t), iniFilePath.c_str()))
    {
        logioptsagentCmd = std::wstring(buffer);
    }
    else
    {
        WritePrivateProfileString(L"General", L"LOGI_PATH ", logioptsagentCmd.c_str(), iniFilePath.c_str());
    }

    if (std::get<ArgIdx::MODE>(args).first) // are we forcing a start method?
    {
        // only match 1st 3 letters
        isPreload = (std::get<ArgIdx::MODE>(args).second.rfind("PRE") == 0);
        WritePrivateProfileString(L"General", L"MODE", isPreload ? L"PRELOAD" : L"INJECT", iniFilePath.c_str());
    }

    if (std::get<ArgIdx::LOGI_PATH>(args).first)
    {
        logioptsagentCmd = std::wstring(
            std::get<ArgIdx::LOGI_PATH>(args).second.begin(),
            std::get<ArgIdx::LOGI_PATH>(args).second.end()
            );
        WritePrivateProfileString(L"General", L"LOGI_PATH ", logioptsagentCmd.c_str(), iniFilePath.c_str());
    }

    if (isPreload)
    {
        return preloadAndStart(logioptsagentCmd.c_str(), dllPath.string().c_str());
    }
    else // in any other case INJECT
    {
        return injectWithThread(logioptionsagentProcessId, dllPath.c_str());
    }
}

// --------------------------------------------------------------------------------------
static Arguments parseArgs(int argc, const char* const argv[])
{
    Arguments args;

    auto print_help = [&]() {
        std::cerr << "Usage: " << argv[0] << " [-m {PRELOAD|INJECT=INJECT}] [-p PATH] [-c CONF] [-h]" << std::endl;
    };

    // Iterate through the arguments and parse them
    for (int i = 1; i < argc; i += 1) {
        std::string arg = argv[i];

        if (arg == "-h")
        {
            print_help();
            std::get<ArgIdx::HELP>(args) = true;
        }
        else if (arg == "-m") {
            std::get<ArgIdx::MODE>(args).first = true;
            std::get<ArgIdx::MODE>(args).second = argv[i + 1];
            i++;
        }
        else if (arg == "-p") {
            std::get<ArgIdx::LOGI_PATH>(args).first = true;
            std::get<ArgIdx::LOGI_PATH>(args).second = argv[i + 1];
            i++;
        }
        else if (arg == "-c") {
            std::get<ArgIdx::INI_CONF>(args).first = true;
            std::get<ArgIdx::INI_CONF>(args).second = argv[i + 1];
            i++;
        }
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_help();
        }
    }
    return args;
}

// --------------------------------------------------------------------------------------
static DWORD getProcessIdByName(const wchar_t* processName)
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

// --------------------------------------------------------------------------------------
static int injectWithThread(DWORD processId, const wchar_t* dllPath)
{
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

    if (!WriteProcessMemory(hProcess, remoteMemory, dllPath, (wcslen(dllPath)+1)*sizeof(decltype(dllPath[0])), NULL))
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
        return -5;
    }

    return 0;
}


// --------------------------------------------------------------------------------------
static int preloadAndStart(const wchar_t* cmd, const char* dllPath)
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(STARTUPINFO));
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    si.cb = sizeof(STARTUPINFO);

    auto s = wcslen(cmd)+1;
    wchar_t* l_cmd = new wchar_t[s];  // doing this because DetourCreateProcessWithDlls takes NON const. Why?
    wcscpy_s(l_cmd, s, cmd);

    const char* paths[] = { dllPath };

    BOOL ret = DetourCreateProcessWithDlls(
        NULL,
        l_cmd,    // why does this need to be NON const?
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

    delete[] l_cmd;

    std::cout << (ret ? "Ok" : "Failed") << std::endl;
    return ret ? 0 : -1;
}
