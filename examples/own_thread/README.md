# Own-thread example

ReAgentLive in any x64 process, with no mod and no SDK: a DLL that ticks
the agent on its own thread, and the console that talks to it. Memory
commands and the fast lane work; object commands and engine calls need a
real host. What it can and cannot do, and why:
[hosting](../../docs/hosting.md#own-thread-example).

| File | What |
|---|---|
| `host.cpp` | `reagentlive-own-thread.dll`: on load, starts one thread that calls `Tick` every 2 ms, with every core op |
| `console.cpp` | `reagentlive.exe`: the generic console; with no pid it finds the one process that has the agent |
| `CMakeLists.txt` | builds both on their own, consuming the library as any host project would |

## Build

From a Visual Studio x64 developer shell, in the ReAgentLive folder:

```powershell
cmake -S examples/own_thread -B build/own-thread -G Ninja -DCMAKE_BUILD_TYPE=Release -DREAGENTLIVE_CONSOLE_UE=OFF
cmake --build build/own-thread
```

`-DREAGENTLIVE_CONSOLE_UE=OFF` leaves out the Unreal pack's verbs, which
this host cannot run.

## Try it

Load the DLL into a process; any DLL loader works. For a quick test, a
Python process that loads it and waits a minute (Python needs the DLL's
full path):

```powershell
$dll = (Resolve-Path build\own-thread\reagentlive-own-thread.dll).Path
Start-Process python -ArgumentList '-c', "`"import ctypes,time; ctypes.WinDLL(r'$dll'); time.sleep(60)`"" -WindowStyle Hidden
build\own-thread\reagentlive.exe -c "status; modules; region base+0x1000"
```

With no pid the console finds the one process that has the agent; with
several it lists their pids on stderr and needs one. `status` shows
`host_thread: own` and `packs: []`. Then:

- **Commands:** [console](../../docs/commands/console.md) and the pages
  it links to.
- **MCP tools for an AI agent:** [agent setup](../../docs/agent-setup.md)
  (it builds this example and registers the server).
- **Your own host,** on the engine's thread and with a pack:
  [hosting](../../docs/hosting.md#host-checklist). This example is a
  starting point, not a supported host.
