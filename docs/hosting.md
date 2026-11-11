# Hosting the agent

How to put the agent into a process: adding the library to a project, build
targets, the host checklist, the `Tick` call, which thread to tick on, and
logging. Code rules the library
keeps for injected images: [contributing](contributing.md#code-rules).

## Own-thread example

The fast way in, for any x64 process with no mod and no SDK:
`examples/own_thread/host.cpp`, an ordinary DLL that starts one thread on
load and ticks the core every couple of milliseconds, and the generic
console `reagentlive.exe` (`examples/own_thread/console.cpp`). Its
`CMakeLists.txt` consumes the library the way any host project would.

```powershell
# From a VS x64 developer shell, in the ReAgentLive folder; the example builds on its own:
cmake -S examples/own_thread -B build/own-thread -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/own-thread
# Load reagentlive-own-thread.dll into the target with any DLL loader, then:
reagentlive -c "status; modules; region <module>+0x1000"
```

- With no pid, the console attaches to the one process that has the
  example's section (`Local\reagentlive-<pid>`); with several it lists
  them.
- **Works:** [memory commands](commands/memory.md), the
  [fast lane](commands/fast-lane.md) and
  [native objects](commands/native-objects.md).
- **Not included:** object commands and engine calls: they need a pack and a
  host on the engine's thread. `status` says `host_thread: own`; reads can be
  torn and a write can race the game.
- Its own section prefix keeps it apart from another host's agent in the
  same process. It is never unloaded. It is a starting point for a real
  host, not a supported one; how it gets into a process is the user's
  business.

## Host checklist

A host is whatever code inside the target calls `Tick`. It provides:

- **A section name**, including the process id so a client can find it
  (`Local\mygame-dev-1234`).
- **The layout sum:** `LayoutCore` plus each pack's `LayoutPart`. The client
  must use the same sum ([handshake](protocol.md#handshake)).
- **A `readable` check** for memory the agent may copy.
- **A command table:** the ops it offers. An op left out is answered
  `unsupported op`; a new core op appears only when the host adds it. Host
  ops start at `reagentlive::op::GameFirst` (4096).
- **Its thread kind** (`Host::thread`: `Engine`, `Borrowed` or `Own`; no
  default, the agent will not open without it) and a call to `Tick` from
  that thread ([host thread](#host-thread)).
- **For the Unreal pack:** an object check (`Context::object`) and the
  engine-thread check (`Context::gameThread`). Both are required.
- **Optional:** a log callback ([logging](#logging)), hold budgets
  ([protocol](protocol.md#exchange)), `Host::backgroundSearch`, and named
  roots for `watch --follow` ([memory commands](commands/memory.md#following-a-replaced-object)).

## Using it in your project

Windows x64 with MSVC or clang-cl, CMake 3.25 or newer, C++23. On any other
platform the library configures with a warning and defines no targets, so
a cross-platform parent still configures.

Add the folder, or fetch it pinned to a release tag:

```cmake
add_subdirectory(reagentlive)
# or
include(FetchContent)
FetchContent_Declare(reagentlive GIT_REPOSITORY <repository> GIT_TAG <release tag>)
FetchContent_MakeAvailable(reagentlive)

target_link_libraries(my-mod PRIVATE reagentlive::ue)         # agent: core + Unreal pack
target_link_libraries(my-console PRIVATE reagentlive::console) # client + Unreal calls + CLI
```

| Option or variable | Default | Effect |
|---|---|---|
| `REAGENTLIVE_CONSOLE` | `ON` | Defines `reagentlive::console`, which needs nlohmann/json and Zydis. Off: an agent-only host configures with no downloads |
| `REAGENTLIVE_BUILD_TESTS` | on when top-level | Builds the library's tests (label `reagentlive`); needs the console |
| `REAGENTLIVE_HOST_SOURCES` | empty | The host's files that include the console; the layout test checks them |
| `REAGENTLIVE_FORBIDDEN_NAMES` | empty | Words the docs test refuses anywhere in the library (a host's and its game's names) |
| `REAGENTLIVE_AGENT_TARGETS` (global property) | the agent targets | Append the host's agent-carrying targets; configure fails if one links the decoder |

- **Dependencies:** a parent's `nlohmann_json::nlohmann_json` or an installed
  package is used first, otherwise a pinned release is fetched. A parent's
  `Zydis` target is used as is; otherwise Zydis v4.1.1 is fetched and
  built static. Nothing touches the parent's cache.
- **Includes:** only `src/` is on the include path, as
  `"reagentlive/<layer>/<file>.h"`, whatever the folder is called. The
  `reagentlive.consumer` test builds an agent-only host this way.
- **Not supported:** `install()` and `find_package(reagentlive)`. The targets
  carry their sources so each consumer compiles them with its own flags
  (below); a prebuilt package could not.
- **Version:** `src/reagentlive/version.h`, also `PROJECT_VERSION` in CMake;
  see the [changelog](../CHANGELOG.md).

## CMake targets

| Target | Carries |
|---|---|
| `reagentlive::core` | `core/agent.cpp`, include root, C++23 |
| `reagentlive::ue` | `ue/agent_ue*.cpp`, links `core`; the consumer provides its Dumper-7 SDK on the include path as `SDK/...` |
| `reagentlive::client` | `core/client.cpp` |
| `reagentlive::ue-client` | `ue/client_ue.cpp`, links `client`; no SDK needed |
| `reagentlive::cli` | `frontends/cli/syntax/`: the parser, without nlohmann/json |
| `reagentlive::console` | `frontends/cli/console/*.cpp`, links `ue-client`, `cli`, nlohmann/json and the Zydis decoder (console only: a configure check fails if an agent target in `REAGENTLIVE_AGENT_TARGETS` links it; hosts add theirs) |

All targets are INTERFACE libraries that carry their sources, so each consumer
compiles them with its own flags. Injected images often need special ones
(static CRT, no thread-safe statics, no static TLS), and a prebuilt archive
could disagree with them.

A host's console includes only `frontends/cli/console/console.h` and
`frontends/cli/syntax/parse.h`; list those files in
`REAGENTLIVE_HOST_SOURCES` ([options](#using-it-in-your-project)).

## The tick

```cpp
#include "reagentlive/core/agent.h"
#include "reagentlive/ue/agent_ue.h"

constexpr reagentlive::Command Commands[] = {
    {reagentlive::op::Read, "read", &reagentlive::Read},
    {reagentlive::op::ReadBatch, "read-batch", &reagentlive::ReadBatch, false},
    {reagentlive::op::Write, "write", &reagentlive::Write},
    {reagentlive::op::Modules, "modules", &reagentlive::Modules},
    {reagentlive::op::Search, "search", &reagentlive::Search},
    {reagentlive::op::Xrefs, "xrefs", &reagentlive::Xrefs},
    {reagentlive::op::Region, "region", &reagentlive::Region},
    {reagentlive::op::Pin, "pin", &reagentlive::Pin},
    {reagentlive::op::Trial, "trial", &reagentlive::Trial},
    {reagentlive::op::Watch, "watch", &reagentlive::Watch, false},
    {reagentlive::ue::op::Find, "find", &reagentlive::ue::Find},
    {reagentlive::ue::op::Call, "call", &reagentlive::ue::Call},
    {reagentlive::ue::op::Fields, "fields", &reagentlive::ue::Fields},
    {reagentlive::ue::op::ReadObject, "read object", &reagentlive::ue::ReadObject, false},
    {reagentlive::ue::op::Describe, "describe", &reagentlive::ue::Describe, false},
    {reagentlive::ue::op::DescribeMany, "describe many", &reagentlive::ue::DescribeMany, false},
    {reagentlive::ue::op::ExecEntries, "exec entries", &reagentlive::ue::ExecEntries, false},
    {reagentlive::ue::op::Entries, "entries", &reagentlive::ue::Entries, false},
    {reagentlive::ue::op::Function, "func", &reagentlive::ue::Function},
    {reagentlive::ue::op::PreviewArray, "preview array", &reagentlive::ue::PreviewArray, false},
    {reagentlive::ue::op::Functions, "funcs", &reagentlive::ue::Functions},
    {reagentlive::ue::op::Parameters, "params", &reagentlive::ue::Fields},
};

// Called by the host from a thread where engine access is safe (UE: the game thread).
void OnGameTick() {
    static reagentlive::ue::Context pack{&IsValidObject, &IsGameThread}; // Both required.
    const reagentlive::Host host{L"Local\\mygame-dev-1234", reagentlive::LayoutCore + reagentlive::ue::LayoutPart,
                             &IsReadable, &Log, &pack, reagentlive::HostThread::Engine};
    reagentlive::Tick(host, Commands);
}
```

The full op list with numbers is in [protocol](protocol.md#ops).

- **The agent never hooks, and starts no threads unless the host opts in.**
  Where `Tick` runs is the host's decision and its responsibility. A host that
  sets `Host::backgroundSearch` lets `search --background` run on one
  agent-owned worker thread (started on first use, kept for the process);
  each search still opts in.
- **Held requests** make the agent spin briefly on the host's thread for the
  client's next request and run it in the same tick, bounded by the host's
  budgets ([protocol](protocol.md#exchange)).
- **Injecting again into the same process** is handled: the newest agent
  takes the section over (`Section::owner`) and earlier ones stop answering.
  `status` reports the answering agent's build time and how many it
  replaced. An agent its host has stopped no longer ticks: clear trials
  before stopping it.
- Idle cost per tick: one atomic increment and one compare.

## Host thread

A host states which thread it calls `Tick` on (`Host::thread`; the agent
refuses to open without it). `status` prints it as `host_thread`:

| `host_thread` | Memory commands | Unreal pack (reads and calls) | `same_tick` means |
|---|---|---|---|
| `engine` | yes | yes | the same moment of the game |
| `borrowed` (a thread the game runs for something else, such as the frame present) or `own` | yes | refused: the pack's thread check says no | the same host tick, nothing more |

Off the engine's thread, `status` adds `tick_note` saying so once, and the
results that carry `same_tick` (`batch`, `get`) add `tick_accurate:
false`. `pin`, `watch` and trials act every host tick, as they always do.
The kind is the host's statement; the core does not verify it. The pack's
required thread check ([Unreal pack](commands/unreal.md)) remains the guard for
engine calls. Nothing that works today is refused because of the kind.
Why the kind is stated rather than detected: [design](design.md#unreal-pack).

## Logging

When the host sets `Host::log`, the agent logs `Dev agent: listening on
<section>` once, then command records with the name, address or object, length
and result. Hosts can suppress successful reads and lookups (`Command::logSuccess`
false) to keep bulk commands out of the log. Do so for every read-only op when
the log writes synchronously: logging runs inside the tick and counts against
the hold budget, so a slow log line splits held commands over ticks (one
request per tick in practice). Refusals log at warning level.

## AI agents

The `reagentlive` skill (`.claude/skills/reagentlive/` in this project)
teaches an AI agent the investigation method. It is a plain `SKILL.md`, so
any agent that reads `.claude/skills` picks it up when started in this
project. In a host project, point the agent at it from the host's own
instructions, and keep what only that host adds (console name, host
commands, build and approval rules) in a host skill or doc of its own,
which the `reagentlive` skill tells the agent to read first.
