# Own-thread example

**TL;DR:** ReAgentLive in any Windows x64 process, with no mod and no SDK:
a DLL that ticks the agent on its own thread (`host.cpp`), and the generic
console `reagentlive.exe` (`console.cpp`) that talks to it. Memory, code and
native-object questions work; no Unreal objects or engine calls. For a UE5
game, start from [`examples/unreal`](../unreal/README.md) instead.

Tier: T0 Core ([tiers](../../docs/tiers.md)); why and what it cannot do:
[hosting](../../docs/hosting.md#own-thread-example).

## Build it

Use it here, or copy `CMakeLists.txt`, `host.cpp` and `console.cpp` into a
project of your own as a starting point
([starting from an example](../../docs/hosting.md#starting-from-an-example);
[agent setup](../../docs/agents/setup.md) can do it for you).

To build it here, from a Visual Studio x64 developer shell in the
ReAgentLive folder:

```powershell
cmake -S examples/own_thread -B build/own-thread -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/own-thread
```

Its `CMakeLists.txt` leaves out the Unreal pack's verbs, which this host
cannot run.

## Try it

Load the DLL into a process; any DLL loader works. For a quick test, a
Python process that loads it and waits a minute (Python needs the DLL's
full path):

```powershell
$dll = (Resolve-Path build\own-thread\reagentlive-own-thread.dll).Path
Start-Process python -ArgumentList '-c', "`"import ctypes,time; ctypes.WinDLL(r'$dll'); time.sleep(60)`"" -WindowStyle Hidden
build\own-thread\reagentlive.exe -c '[{"verb": "status"}, {"verb": "modules"}, {"verb": "read", "args": {"addr": {"module": "python.exe", "rva": "0x1000"}, "len": 16}}]'
```

With no pid the console finds the one process that has the agent; with
none or several it lists their pids on stderr, answers `help` and `usage`
only, and needs a pid for the rest. `status` shows `host_thread: own` and
`packs: []`. Then:

- **Commands:** [console](../../docs/commands/console.md).
- **MCP tools for an AI agent:** [agent setup](../../docs/agents/setup.md)
  (it starts a project from this example and registers the server).
- **Unreal objects and engine calls (T1-T3):** the
  [Unreal example](../unreal/README.md).
- **Your own host** (T4 with an SDK): [hosting](../../docs/hosting.md#host-checklist).
