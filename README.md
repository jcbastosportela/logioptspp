![msbuild workflow](https://github.com/jcbastosportela/logioptspp/actions/workflows/msbuild.yml/badge.svg)

# !!! Work in progress !!!
This repo is almost fulfilling it's purpose but the code is a mess, but it is working.

# What does this do
Allow users of Logitech mice (mine is an MX Master 3) to enjoy smooth scrolling in all applications.

# How to
Compile the solution (or download bianries from the releases), create `config.ini` file next to the `logioptionspprun.exe` and `logioptionspp.dll`, like:
```ini
[General]
MODE=PRELOAD
LOGI_PATH=C:\Program Files\LogiOptionsPlus\logioptionsplus_agent.exe
APPS_TO_MASK=devenv.exe, Code.exe
```
where `MODE` can be:
- `PRELOAD` : use if `logioptionsplus_agent.exe` is not running and you want to start it with `logioptionspprun.exe`
- `INJECT` : use if `logioptionsplus_agent.exe` already running and you just want to inject the DLL

`LOGI_PATH` is the full path to the `logioptionsplus_agent.exe`

`APPS_TO_MASK` is a comma "," sperated list of executables that you want to be treated as "chrome.exe", meaning, to receive smooth scrooling.

Execute `logioptionspprun.exe` by double clicking it.

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
The `logioptionsplus_agent.exe` is getting the name of the executable in foreground by using MSWindows libraries, namelly `kernelbase.dll` calling `QueryFullProcessImageNameW`.

The idea is to intercept this call and return one of the "magically smoothly scrollable" names.

To intercept I am trying to use MS Detours (see: https://github.com/microsoft/Detours)

To make the injection there seem to be two possible approaches:
1. use detours to preload and start `logioptionsplus_agent.exe`
1. inject runtime: start a thread on the `logioptionsplus_agent.exe` process and load the interceptor DLL

Currently I am more focused in making 1. to work.

# Projects on the solution
- __logioptionspp__: "Logitech Options Plus Plus" the interceptor DLL
- __logioptionspprun__: "Logitech Options Plus Plus Runner" the executable responsible for preloading (inject) the library on `logioptionsplus_agent.exe`
- __Detours-main__: a copy of the master from https://github.com/microsoft/Detours at some point (I know, I could have used submodules...)

# Tools I am using
- MS Visual Studio 2022 Community (you know here to find this)
- x64dbg (https://help.x64dbg.com/en/latest/index.html)

