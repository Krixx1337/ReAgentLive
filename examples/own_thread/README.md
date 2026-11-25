# Own-thread example

**TL;DR:** ReAgentLive in any Windows x64 process, with no mod and no SDK:
a DLL that ticks the agent on its own thread (`host.cpp`), and the generic
console `reagentlive.exe` (`console.cpp`) that talks to it. Memory, code and
native-object questions work; no Unreal objects or engine calls. For a UE5
game, start from [`examples/unreal`](../unreal/README.md) instead.

Tier: T0 Core ([tiers](../../docs/tiers.md)); why and what it cannot do:
[hosting](../../docs/hosting.md#own-thread-example).

## Start a project from it

This folder is a kit: copy it into a project of your own and build on
`host.cpp` and `console.cpp`
([starting from a kit](../../docs/hosting.md#starting-from-a-kit);
[agent setup](../../docs/agents/setup.md) does it for you). The project's
DLL and console are then named after it; below they keep this example's
names.

To build it here, in ReAgentLive's own tree, from a Visual Studio x64
developer shell in the ReAgentLive folder:

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
  (it makes a project from this kit and registers the server).
- **Unreal objects and engine calls (T1-T3):** the
  [Unreal example](../unreal/README.md).
- **Your own host** (T4 with an SDK): [hosting](../../docs/hosting.md#host-checklist).
