# Hosting the agent

*Home of:* putting the agent into a process: starting from an example, the agent tooling in a project, the host checklist, CMake targets and options, the `Tick` call, the host thread, the game-thread tick host API (`reagentlive::ue-draw`) and host verbs.

**TL;DR:** The agent only runs when something inside the target calls its
`Tick`: that code is the host. Start from an example DLL (any process, or
any UE5 game); write your own host when you want an SDK's layout and your
own commands. The one thing not to get wrong: say truthfully which thread
you tick on, because Unreal calls are only safe on the game thread.

Code rules the library keeps for injected images:
[contributing](contributing.md#code-rules). Which tier each host gives:
[tiers](tiers.md).

## Unreal example

`examples/unreal/host.cpp` ticks the whole Unreal pack on its own thread
(`reagentlive::ue-base`, the stock UE5 layout, `Context::offThreadReads` on)
and links `reagentlive::ue-draw`, so `game-thread` with `action` `on` moves
the tick to the game viewport's Draw ([game-thread tick](#game-thread-tick)).
Its console is `reagentlive-ue.exe`, section `Local\reagentlive-ue-<pid>`
(the generic `reagentlive.exe` cannot see it). Build, use and what it
refuses: its [README](../examples/unreal/README.md). It is a starting
point, loaded with any DLL loader, never unloaded.

## Own-thread example

`examples/own_thread/host.cpp`: an ordinary DLL that starts one thread on
load and ticks the core every couple of milliseconds, with the generic
console `reagentlive.exe`. It has no Unreal pack (`host_thread: own`,
`packs: []`; reads can be torn and a write can race the game). Its
`CMakeLists.txt` consumes the library the way any host project would. Build
and try it: its [README](../examples/own_thread/README.md). A starting
point for a real host, not a supported one.

Each example uses its own section prefix, which keeps it apart from
another host's agent in the same process.

## Starting from an example

Either example works as a starting point outside this tree: copy its
`CMakeLists.txt`, `host.cpp` and `console.cpp` into a project of your own
that carries ReAgentLive as its `reagentlive/` submodule; the copied
`CMakeLists.txt` finds the library there (elsewhere: set
`REAGENTLIVE_DIR`). Change the section prefix
(`SectionPrefix` in both files) to your own, so its console never attaches
to another host's agent; rename the targets as you like. From then on the
files are yours: an update of ReAgentLive never touches them, and the
CHANGELOG's breaking entries say when the host API changes.

## An agent in your project

The agent tooling (the MCP server, the skill, the docs it reads) runs from
the library's checkout, so a project that wants it carries ReAgentLive at
a fixed path: a git submodule, anywhere in the project (with `FetchContent`
or CPM the checkout lives in the build tree and moves). For any host,
copied from an example or not:

- **`tools/agent_link.py --console <exe> --dll <dll>`** wires the project
  to that checkout: the project record `reagentlive.json` (where the
  checkout is, the clients, `console`, `dll`, an optional `build` command,
  `check`), the ReAgentLive server in each client's project MCP file (the
  project's own when it has one), a ReAgentLive block in `AGENTS.md` and
  pointer skills; a skill folder the project tracks stays. Commit them
  all: a clone of the project then works with no setup. It writes nothing
  outside the project. Run again, it updates all of it in place.
- **`tools/build_project.ps1`** loads Visual Studio's x64 environment and
  runs the record's `build`, or CMake on the project into `build/`. With
  `-Check` it loads the DLL into a throwaway Python process and checks its
  `status`, which fits a host that ticks on its own thread (`check` false
  for one that ticks only inside the game).
- **`tools/update.ps1`** previews an update (what is coming, and whether
  the headers a host includes changed), then with `-Apply` moves the
  submodule, rebuilds and checks; `-Rollback` goes back.

Each project pins its own commit, so updating one never breaks another;
nothing is installed machine-wide. Why there is no global install or
prebuilt package: [design](design/principles.md#not-now).

## Host checklist

A host is whatever code inside the target calls `Tick`. It provides:

- **A section name**, including the process id so a client can find it
  (`Local\mygame-dev-1234`).
- **The layout sum:** `LayoutCore` plus each pack's `LayoutPart`. The client
  must use the same sum ([handshake](protocol.md#handshake)).
- **A `readable` check** for memory the agent may copy.
- **A command table:** the ops it offers. An op left out is answered
  `unsupported op`; a new core op appears only when the host adds it. Host
  ops start at `reagentlive::op::GameFirst`.
- **Its thread kind** (`Host::thread`: `Engine`, `Borrowed` or `Own`; no
  default, the agent will not open without it) and a call to `Tick` from
  that thread ([host thread](#host-thread)).
- **For the Unreal pack:** an object check (`Context::object`; a host
  without its own can pass `reagentlive::ue::ObjectInTable`) and the
  engine-thread check (`Context::gameThread`). Both are required.
  Optional: starting guesses for the object table and name pool (usually
  the SDK's offsets), the image to scan, `Context::offThreadReads` (lookups
  and reads off the game thread, [which
  commands](commands/unreal.md#off-the-engines-thread)) and
  `Context::scanMicroseconds` (how long each tick an object-table scan may
  run: more is fewer ticks per scan and a longer stall in each). The pack
  finds and checks the globals itself
  ([before the first command](commands/unreal.md#before-the-first-command)).
  `reagentlive::ue::FirstLiveInstance(className)` walks the whole object
  table on the calling thread: call it rarely, from the thread that ticks
  the agent.
- **Optional:** a log callback (below), hold budgets
  ([protocol](protocol.md#exchange)), `Host::backgroundSearch`, and named
  roots for `watch-start`'s `follow`
  ([memory commands](commands/memory.md#following-a-replaced-object)).

**Logging.** With `Host::log` set, the agent logs a listening line and a
record per command; refusals log at warning level. Set `Command::logSuccess`
false for read-only ops when the log writes synchronously: logging runs
inside the tick and counts against the hold budget, so a slow log line
splits held commands over ticks.

**Names files.** `ConsoleConfig::namesFile` and `labelsFile` place the
console's names and confirmed labels; give a path under version control so
they survive a clean build ([labels](commands/labels.md#labels-files)).

## Using it in your project

Windows x64 with MSVC or clang-cl, CMake 3.25 or newer, C++23. On any other
platform the library configures with a warning and defines no targets, so
a cross-platform parent still configures.

Add it as a submodule (the agent tooling then sits at a fixed path in the
project: [an agent in your project](#an-agent-in-your-project)), or fetch
it pinned to a release tag when the project only links the library:

```cmake
add_subdirectory(reagentlive)
# or
include(FetchContent)
FetchContent_Declare(reagentlive GIT_REPOSITORY <repository> GIT_TAG <release tag>)
FetchContent_MakeAvailable(reagentlive)
# or, with CPM.cmake (CPM_SOURCE_CACHE shares one download per version between projects)
CPMAddPackage("gh:Krixx1337/ReAgentLive@<version>")

target_link_libraries(my-mod PRIVATE reagentlive::ue-base)    # agent: core + the whole Unreal pack, no SDK
# or reagentlive::ue: the same, with your Dumper-7 SDK as the layout;
# add reagentlive::ue-draw for the game-thread tick through Draw
target_link_libraries(my-console PRIVATE reagentlive::console) # client + Unreal calls + CLI
```

| Option or variable | Effect |
|---|---|
| `REAGENTLIVE_CONSOLE` (on) | Defines `reagentlive::console`, which needs nlohmann/json and Zydis. Off: an agent-only host configures with no downloads |
| `REAGENTLIVE_PACKS` (`ue`) | The engine packs to define, by folder under `src/reagentlive/packs/`; one at most. Empty: no pack targets, and the verb table (so `help`, the generated functions and MCP tools) holds core and host rows only. For a target that is not Unreal |
| `REAGENTLIVE_BUILD_TESTS` (on when top-level) | Builds the library's tests and the examples they test |
| `REAGENTLIVE_HOST_SOURCES`, `REAGENTLIVE_FORBIDDEN_NAMES`, `REAGENTLIVE_AGENT_TARGETS` | A host's own files, forbidden words and agent targets for the library's checks; see the comments in `CMakeLists.txt` |

- **Dependencies:** a parent's `nlohmann_json::nlohmann_json` or `Zydis`
  target (or an installed package) is used first, otherwise a pinned
  release is fetched. Nothing touches the parent's cache.
- **Includes:** only `src/` is on the include path, as
  `"reagentlive/<layer>/<file>.h"` and a pack's as
  `"reagentlive/packs/<id>/<file>.h"`.
- **Not supported:** `install()` and `find_package(reagentlive)`: the
  targets carry their sources so each consumer compiles them with its own
  flags.
- **Version:** `src/reagentlive/version.h`; see the [changelog](../CHANGELOG.md).

## CMake targets

| Target | Purpose |
|---|---|
| `reagentlive::core` | the core agent |
| `reagentlive::ue-base` | the whole Unreal pack agent, no SDK: the stock UE5 layout, each group checked on known objects before its ops run ([without an SDK](commands/unreal.md#without-an-sdk)) |
| `reagentlive::ue-draw` | opt-in: a game-thread tick through the game viewport's Draw and the `game-thread` op (`packs/ue/host_draw.h`) |
| `reagentlive::ue` | `ue-base` built with the host's SDK as the layout (`REAGENTLIVE_UE_SDK`, every group on without checks); the consumer provides its Dumper-7 SDK on the include path as `SDK/...` |
| `reagentlive::client`, `reagentlive::ue-client` | the C++ client, and the Unreal calls (no SDK needed) |
| `reagentlive::cli` | the call parser, without nlohmann/json |
| `reagentlive::console` | the console; links the Zydis decoder, which no agent target may (a configure check fails if one does) |

All targets are INTERFACE libraries that carry their sources, so each consumer
compiles them with its own flags. Injected images often need special ones
(static CRT, no thread-safe statics, no static TLS), and a prebuilt archive
could disagree with them.

A host's console includes only `frontends/cli/console/console.h`
(`RunConsole`, `ConsoleConfig`), `call/build.h`, `call/verb.h` (a row per
verb of its own) and `results/schema.h` (typed results for those rows), and
lists the files that include them in `REAGENTLIVE_HOST_SOURCES` so the
layout test checks them.

## The tick

An SDK host (T4) ticking on the game thread. `examples/unreal/host.cpp` has
the full Unreal command table and an own-thread tick beside a Draw tick.

```cpp
#include "reagentlive/core/agent.h"
#include "reagentlive/packs/ue/agent_ue.h"

constexpr reagentlive::Command Commands[] = {
    {reagentlive::op::Read, "read", &reagentlive::Read},
    {reagentlive::op::Watch, "watch", &reagentlive::Watch, false},
    {reagentlive::ue::op::Find, "find", &reagentlive::ue::Find},
    // ... one row per op the host offers
};

// Called by the host from a thread where engine access is safe (UE: the game thread).
void OnGameTick() {
    const uintptr_t base = SDK::InSDKUtils::GetImageBase(); // The SDK's addresses: a guess the pack checks.
    static reagentlive::ue::Context pack{&IsValidObject, &IsGameThread, // Both required.
                                     base + SDK::Offsets::GObjects, base + SDK::Offsets::GNames};
    const reagentlive::Host host{L"Local\\mygame-dev-1234", reagentlive::LayoutCore + reagentlive::ue::LayoutPart,
                             &IsReadable, &Log, &pack, reagentlive::HostThread::Engine};
    reagentlive::Tick(host, Commands);
}
```

Ops and numbers: [protocol](protocol.md#ops).

- **The agent never hooks, and starts no threads unless the host opts in.**
  Where `Tick` runs is the host's decision and its responsibility. A host
  that sets `Host::backgroundSearch` lets search jobs, the console's
  internal scans and image copies run on one agent-owned worker thread;
  without it they run in slices of each host tick. The two opt-in swaps are
  the call recorder and `reagentlive::ue-draw`; neither writes code or the
  game's tables.
- **Held requests** make the agent spin briefly on the host's thread for the
  client's next request and run it in the same tick, bounded by the host's
  budgets ([protocol](protocol.md#exchange)).
- **Injecting again into the same process** is handled: the newest agent
  takes the section over ([takeover](protocol.md#section)). An agent its
  host has stopped no longer ticks, so it restores nothing: clear `undo`
  writes (`write` with `clear`) before stopping it. This holds only for a
  build with the same protocol: after a protocol change the section keeps
  the old layout, so restart the process instead
  ([the update procedure](agents/update.md#6-running-targets)).
- Idle cost per tick: one atomic increment and one compare.

## Host thread

A host states which thread it calls `Tick` on (`Host::thread`; the agent
refuses to open without it). `status` prints it as `host_thread`:

| `host_thread` | Memory commands | Unreal pack (reads and calls) | `same_tick` means |
|---|---|---|---|
| `engine` | yes | yes | the same moment of the game |
| `borrowed` (a thread the game runs for something else, such as the frame present) or `own` | yes | reads with `Context::offThreadReads`, else refused; calls refused ([which](commands/unreal.md#off-the-engines-thread)) | the same host tick, nothing more |

A host off the game thread that links `reagentlive::ue-draw` moves its
tick onto the game thread when `game-thread` arms it and states `engine` for
those Draw ticks ([game-thread tick](#game-thread-tick)).

The kind is the host's statement; the core does not verify it. The pack's
required thread check ([Unreal pack](commands/unreal.md)) remains the guard
for engine calls, and nothing that works today is refused because of the
kind. Why it is stated rather than detected:
[design](design/unreal.md#decisions).

## Game-thread tick

`reagentlive::ue-draw` (`packs/ue/host_draw.h`, namespace
`reagentlive::ue::draw`) gives a host that ticks elsewhere a tick on the
game thread with no per-build address: it finds the game viewport client
with `FirstLiveInstance` and points its Draw interface at a private copy of
the vtable with Draw swapped. What the command does and refuses:
[`game-thread`](commands/unreal.md); why this way:
[design](design/unreal.md#a-game-thread-tick-through-the-viewports-draw).

The host API (`host_draw.h`, used by `examples/unreal/host.cpp`):
`Configure(tick, log)` once; `Poll()` every host tick before the agent
ticks (installs once armed and the viewport is found); `Live()` says a Draw
tick ran recently, so skip the host's own tick; `OnGameThread()` is the
host's `Context::gameThread` inside the Draw tick; `Arm()` arms without the
command; `Disable()` restores the viewport's table (the copy stays
allocated, since a Draw inside the hook keeps forwarding); `Shutdown()` is
`Disable` for good, called in the host's stop path; `GameThread` is the
command-table entry behind the verb.

Two threads then tick the same agent (the host's own and Draw), so the
host lets one tick run at a time (the example uses an interlocked flag)
and builds its `Context` and `Host` per tick: `OnGameThread` and
`HostThread::Engine` in the Draw tick, its own check and kind elsewhere.

## Host verbs

A host's console (`RunConsole`) can add verbs of its own. Each is a row
of the console's verb table (`ConsoleConfig::verbs`, row type in
`call/verb.h`): name, origin `host`, tool pack (`modify` for one that
writes; [tool packs](tool-interface.md#tool-packs)), args and a result type
from a struct with `Describe`. Mark `writes` on a verb that writes the
target or calls into it and `blocks` on one that holds the console for
seconds. The console checks a call's args against the row and passes them
to `ConsoleConfig::hostCommand`, which answers an envelope. The row gives
the verb what the library's have: `help`, a generated Python method
(`c.host.<verb>`), an MCP tool (`host_<verb>`), strict results and a check
that the result matches its declared type. Why rows:
[design](tool-interface.md#self-describing-console).
