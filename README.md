# !!! Work in progress !!!
This repo does mot yet fulfil it's purpose and the code is a mess, full of hardcoded paths that!

# What does this (will hopefully) do
Will allow users of Logitech mice (mine is an MX Master 3) to enjoy smooth scrolling in all applications.

## The problem
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
- __test_detour__: a simple client user of `QueryFullProcessImageNameW` so I can test if the inteceptor and preloader are working
- __Detours-main__: a copy of the master from https://github.com/microsoft/Detours at some point (I know, I could have used submodules...)

# Tools I am using
- MS Visual Studio 2022 Community (you know here to find this)
- x64dbg (https://help.x64dbg.com/en/latest/index.html)

# What is working
The lib is being preloaded in `logioptionsplus_agent.exe`

# Why isn't this working
The calls are not being intercepted. But when I try intercepting call made by the test application client of `QueryFullProcessImageNameW` I see the calls being intercepted. _Why aren't the calls form `logioptionsplus_agent.exe` being intercepted????_

If you know about libraries preloading, specially using detours and have a clue why this doesn't work please let me know.