![msbuild workflow](https://github.com/jcbastosportela/logioptspp/actions/workflows/msbuild.yml/badge.svg)

# What does this do
Allow users of Logitech mice (mine is an MX Master 3) to enjoy smooth scrolling in all applications.

# How to
First, in Logi Options+ open your mouse's scroll wheel settings and turn **Smooth scrolling** on.
It is off by default for the main wheel on some mice (the MX Master 3S and MX Master 4, for example),
and without it Options+ never smooths the main wheel, not even in Chrome or Edge, so this tool has nothing to unlock.
The thumb wheel has its own smooth setting, which is on by default.

Compile the solution (or download bianries from the releases), create a `conf.ini` file next to the `logioptionspprun.exe` and `logioptionspp.dll` (the file must be called `conf.ini`; that is the name both the runner and the DLL read), like:
```ini
[General]
LOGI_PATH=C:\Program Files\LogiOptionsPlus\logioptionsplus_agent.exe
BLACKLIST_APPS=explorer.exe, Taskmgr.exe
```
or
```ini
[General]
LOGI_PATH=C:\Program Files\LogiOptionsPlus\logioptionsplus_agent.exe
WHITELIST_APPS=vscode.exe, my_other_app.exe
```

where `LOGI_PATH` is the full path to the `logioptionsplus_agent.exe`

`BLACKLIST_APPS` is a comma "," sperated list of executables that you do NOT want to be treated as "chrome.exe", meaning, NOT to receive smooth scrooling. For example, `explorer.exe` and `Taskmgr.exe` are applications that feel a bit strange with the smooth scrool.

`WHITELIST_APPS` is a comma "," sperated list of executables that you want to be treated as "chrome.exe", meaning, to receive smooth scrooling.

__NOTE__ that defining both `BLACKLIST_APPS` and `WHITELIST_APPS` is contradictory; defining one of them defines the selection mode; when both are defined it will function in whitelist mode.

Optional settings in the same `[General]` section:
- `MODE=INJECT` or `MODE=PRELOAD`: `INJECT` loads the DLL into the running agent; `PRELOAD` starts a new agent with the DLL loaded. When `MODE` is not set the runner injects if the agent is running and preloads otherwise. Set `MODE=INJECT` if you run the runner unattended (for example from a scheduled task), so it never starts a second agent.
- `LOG_PATH=C:\path\to\logioptspp.log`: the DLL writes a short log of what it loaded and hooked.
- `TRACE=1`: also log each distinct executable name the agent looks up, and through which function. Useful when a new Options+ version stops working: it shows which lookup the agent now uses.

Execute `logioptionspprun.exe` by double clicking it.

The hook lives inside the running `logioptionsplus_agent.exe`, so it is lost whenever the agent restarts (log on, Options+ update, crash). Run `logioptionspprun.exe` again after that, or schedule it.

## The problem that made me do this
If you are here you may be irritated with the fact that your Logitech mouse on does smooth scrolling in certain applications.
If you are wondering if it is Logitech's fault or if it is the applications' fault, let me assure you, it is Logitech's fault!

Basically Logitech runs some applications in the backgorund, like the `logioptionsplus_agent.exe` that is continuously evalutating
the application currently in the foregorund; if the foreground application is one of ("magically smoothly scrollable"):
- chrome.exe
- iexplorer.exe
- msedge.exe
- firefox.exe

than it will do smooth scrolling, otherwise it won't, regardless of the settings that you make via LogioOptions+.

See this discussion as an example: https://www.reddit.com/r/logitech/comments/14h34we/systemwide_smooth_scrolling/

## The idea
The `logioptionsplus_agent.exe` is getting the name of the executable in foreground by using MSWindows libraries in `kernelbase.dll`.
Older agents used `QueryFullProcessImageNameW`. Options+ 2.9 uses `QueryFullProcessImageNameW` only to scan the process list and identifies the foreground application with `K32GetModuleFileNameExW` (`GetModuleFileNameExW`).

The idea is to intercept these calls and return one of the "magically smoothly scrollable" names. The DLL hooks `QueryFullProcessImageNameW`, `QueryFullProcessImageNameA` and `K32GetModuleFileNameExW`.

To intercept I am trying to use MS Detours (see: https://github.com/microsoft/Detours)

To make the injection there seem to be two possible approaches:
1. use detours to preload and start `logioptionsplus_agent.exe`
1. inject runtime: start a thread on the `logioptionsplus_agent.exe` process and load the interceptor DLL

Currently I am more focused in making 1. to work.

# Projects on the solution
- __logioptionspp__: "Logitech Options Plus Plus" the interceptor DLL
- __logioptionspprun__: "Logitech Options Plus Plus Runner" the executable responsible for preloading (inject) the library on `logioptionsplus_agent.exe`
- __Detours-main__: submodule of the master from https://github.com/microsoft/Detours at some point

# Tools I am using
- MS Visual Studio 2022 Community (you know where to find this)
- x64dbg (https://help.x64dbg.com/en/latest/index.html)

