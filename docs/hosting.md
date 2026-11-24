# Hosting the agent

*Home of:* putting the agent into a process: the two examples' internals, the host checklist, CMake targets and options, the `Tick` call, the host thread, the game-thread tick host API (`reagentlive::ue-draw`), host verbs and logging.

**TL;DR:** The agent only runs when something inside the target calls its
`Tick`: that code is the host. Start from an example DLL (any process, or
any UE5 game); write your own host when you want an SDK's layout and your
own commands. The one thing not to get wrong: say truthfully which thread
you tick on, because Unreal calls are only safe on the game thread.

Code rules the library keeps for injected images:
[contributing](contributing.md#code-rules). Which tier each host gives:
[tiers](tiers.md).

## Unreal example

The same shape for any UE5 game with no dump: `examples/unreal/host.cpp`
ticks the whole Unreal pack on its own thread (`reagentlive::ue-base`, the
stock UE5 layout checked on known objects, `Context::offThreadReads` on)
and links `reagentlive::ue-draw`, so `game-thread` with `action` `on` moves the tick to the
game viewport's Draw, where the game-thread commands work
([game-thread tick](#game-thread-tick)). Its console is
`reagentlive-ue.exe`, section `Local\reagentlive-ue-<pid>` (the generic
`reagentlive.exe` cannot see it); its
[README](../examples/unreal/README.md) is the quick start. Like the
own-thread example it is a starting point, loaded with any DLL loader.
Which tier it gives: [tiers](tiers.md#the-ladder).

## Own-thread example

The fast way in, for any x64 process with no mod and no SDK:
`examples/own_thread/host.cpp`, an ordinary DLL that starts one thread on
load and ticks the core every couple of milliseconds, and the generic
console `reagentlive.exe` (`examples/own_thread/console.cpp`). Its
`CMakeLists.txt` consumes the library the way any host project would; its
[README](../examples/own_thread/README.md) is the quick start.

```powershell
# From a VS x64 developer shell, in the ReAgentLive folder; the example builds on its own:
cmake -S examples/own_thread -B build/own-thread -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/own-thread
# Load reagentlive-own-thread.dll into the target with any DLL loader, then:
reagentlive -c '[{"verb": "status"}, {"verb": "modules"}, {"verb": "read", "args": {"addr": {"module": "target.exe", "rva": "0x1000"}, "len": 16}}]'
```

- With no pid, the console attaches to the one process that has the
  example's section (`Local\reagentlive-<pid>`). With none or several (it
  lists them on stderr) it answers `help` and `usage` only, so the MCP
  server still starts; pass a pid for the rest.
- **Works:** [memory commands](commands/memory.md), the
  [fast lane](commands/fast-lane.md) and
  [native objects](commands/native-objects.md).
- **Not included:** the Unreal pack: the host refuses its commands with a
  message saying it has no Unreal pack. For a UE5 game, use the
  [Unreal example](#unreal-example). Which tier it gives:
  [tiers](tiers.md#the-ladder). `status` says `host_thread: own` and
  `packs: []`; reads can be torn and a write can race the game. Configure
  with `-DREAGENTLIVE_PACKS=` for a console (and tools) without the
  pack's verbs.
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
  engine-thread check (`Context::gameThread`). Both are required. A host
  without its own object check can pass `reagentlive::ue::ObjectInTable`.
  Optional: starting guesses for the object table and name pool
  (`Context::objectsGuess`, `namesGuess`, usually the SDK's offsets) and
  the image to scan (`Context::image`, default the process's exe); the pack
  finds and checks both globals itself
  ([before the first command](commands/unreal.md#before-the-first-command)).
  `Context::offThreadReads` (off by default; `examples/unreal` turns it on)
  lets lookups and reads run off the game thread
  ([which commands](commands/unreal.md#off-the-engines-thread)).
  `Context::scanMicroseconds` (default 1000, at most 50 000) is how long
  each tick an object-table scan (`find` by `cls`, `census`, `each`, `name-refs`,
  `labels-export`) may run: more is fewer ticks per scan and a longer stall
  in each. A host that ticks at 100 Hz and can spare 4 ms finishes a
  370 000-object scan about four times sooner.
- **Unreal helpers for hosts:** `reagentlive::ue::FirstLiveInstance(className)`
  (`packs/ue/agent_ue.h`) returns the first live instance (no class default
  or archetype object) of a class or a subclass, by name; null until the
  pack has located the object table (its first command does). It walks the
  whole table on the calling thread: call it rarely, from the thread that
  ticks the agent. `reagentlive::ue-draw` finds the viewport with it; for a
  game-thread tick without a hook of your own:
  [game-thread tick](#game-thread-tick).
- **Optional:** a log callback ([logging](#logging)), hold budgets
  ([protocol](protocol.md#exchange)), `Host::backgroundSearch`, and named
  roots for `watch-start`'s `follow` ([memory commands](commands/memory.md#following-a-replaced-object)).

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

target_link_libraries(my-mod PRIVATE reagentlive::ue-base)    # agent: core + the whole Unreal pack, no SDK
# or reagentlive::ue: the same, with your Dumper-7 SDK as the layout;
# add reagentlive::ue-draw for the game-thread tick through Draw
target_link_libraries(my-console PRIVATE reagentlive::console) # client + Unreal calls + CLI
```

| Option or variable | Default | Effect |
|---|---|---|
| `REAGENTLIVE_CONSOLE` | `ON` | Defines `reagentlive::console`, which needs nlohmann/json and Zydis. Off: an agent-only host configures with no downloads |
| `REAGENTLIVE_PACKS` | `ue` | The engine packs to define, by folder under `src/reagentlive/packs/`; each defines its targets, and the console and `reagentlive::cli` carry the listed one (one at most: configure fails on two, or on an unknown id). Empty: no pack targets, and the console's verb table, and so `help`, the generated functions and the MCP tools, hold core and host rows only; `write`'s `undo` is refused (its owner check is the pack's), a `hold` or watch on an object is refused, `peek` names no objects. For a target that is not Unreal |
| `REAGENTLIVE_BUILD_TESTS` | on when top-level | Builds the library's tests (label `reagentlive`) and the examples they test (`examples/unreal` with the Unreal pack); needs the console with the pack (they build the pack-less one beside it) |
| `REAGENTLIVE_HOST_SOURCES` | empty | The host's files that include the console; the layout test checks them |
| `REAGENTLIVE_FORBIDDEN_NAMES` | empty | Words the docs test refuses anywhere in the library (a host's and its game's names) |
| `REAGENTLIVE_AGENT_TARGETS` (global property) | the agent targets | Append the host's agent-carrying targets; configure fails if one links the decoder |

- **Dependencies:** a parent's `nlohmann_json::nlohmann_json` or an installed
  package is used first, otherwise a pinned release is fetched. A parent's
  `Zydis` target is used as is; otherwise Zydis v4.1.1 is fetched and
  built static. Nothing touches the parent's cache.
- **Includes:** only `src/` is on the include path, as
  `"reagentlive/<layer>/<file>.h"` and a pack's as
  `"reagentlive/packs/<id>/<file>.h"`, whatever the folder is called. The
  `reagentlive.consumer` test builds an agent-only host this way.
- **Not supported:** `install()` and `find_package(reagentlive)`. The targets
  carry their sources so each consumer compiles them with its own flags
  (below); a prebuilt package could not.
- **Version:** `src/reagentlive/version.h`, also `PROJECT_VERSION` in CMake;
  see the [changelog](../CHANGELOG.md).

## CMake targets

| Target | Carries |
|---|---|
| `reagentlive::core` | `core/agent.cpp`, `core/agent_search.cpp`, include root, C++23 |
| `reagentlive::ue-base` | every Unreal pack agent file, `packs/ue/agent_ue*.cpp` (defined with `ue` in `REAGENTLIVE_PACKS`), links `core`; no SDK: the stock UE5 layout, each group checked on known objects before its ops run ([without an SDK](commands/unreal.md#without-an-sdk)) |
| `reagentlive::ue-draw` | opt-in host helper, links `ue-base`: a game-thread tick through the game viewport's Draw and the `game-thread` op (`packs/ue/host_draw.h`) |
| `reagentlive::ue` | links `ue-base` and builds it with the host's SDK as the layout (`REAGENTLIVE_UE_SDK`, every group on without checks); it adds no op. The consumer provides its Dumper-7 SDK on the include path as `SDK/...` |
| `reagentlive::client` | `core/client.cpp` |
| `reagentlive::ue-client` | `packs/ue/client_ue.cpp`, links `client`; no SDK needed |
| `reagentlive::cli` | `frontends/cli/call/`: the parser, without nlohmann/json |
| `reagentlive::console` | `frontends/cli/console/*.cpp` with `packs/ue/cli/console/` (or `console/no_pack.cpp` with `REAGENTLIVE_PACKS` empty), links `ue-client` (or `client`), `cli`, nlohmann/json and the Zydis decoder (console only: a configure check fails if an agent target in `REAGENTLIVE_AGENT_TARGETS` links it; hosts add theirs) |

All targets are INTERFACE libraries that carry their sources, so each consumer
compiles them with its own flags. Injected images often need special ones
(static CRT, no thread-safe statics, no static TLS), and a prebuilt archive
could disagree with them.

A host's console includes only these headers, and lists the files that
include them in `REAGENTLIVE_HOST_SOURCES` so the layout test checks them
([options](#using-it-in-your-project)):

- `frontends/cli/console/console.h`: `RunConsole` and `ConsoleConfig`;
- `frontends/cli/call/build.h`: `Build` and `BuildCall`;
- `frontends/cli/call/verb.h`: a row for each verb of its own;
- `frontends/cli/results/schema.h`: typed results for those rows.

## The tick

An SDK host (T4) ticking on the game thread, with a short command table;
`examples/unreal/host.cpp` has the full Unreal table and an own-thread
tick beside a Draw tick.

```cpp
#include "reagentlive/core/agent.h"
#include "reagentlive/packs/ue/agent_ue.h"

constexpr reagentlive::Command Commands[] = {
    {reagentlive::op::Read, "read", &reagentlive::Read},
    {reagentlive::op::ReadBatch, "read-batch", &reagentlive::ReadBatch, false},
    {reagentlive::op::Write, "write", &reagentlive::Write},
    {reagentlive::op::Modules, "modules", &reagentlive::Modules},
    {reagentlive::op::Search, "search", &reagentlive::Search},
    {reagentlive::op::Xrefs, "xrefs", &reagentlive::Xrefs},
    {reagentlive::op::Region, "region", &reagentlive::Region},
    {reagentlive::op::ImageCopy, "image copy", &reagentlive::ImageCopy},
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
    {reagentlive::ue::op::Owner, "owner", &reagentlive::ue::Owner, false},
    {reagentlive::ue::op::Route, "route", &reagentlive::ue::Route, false},
    {reagentlive::ue::op::Function, "func", &reagentlive::ue::Function},
    {reagentlive::ue::op::PreviewArray, "preview array", &reagentlive::ue::PreviewArray, false},
    {reagentlive::ue::op::Functions, "funcs", &reagentlive::ue::Functions},
    {reagentlive::ue::op::Parameters, "params", &reagentlive::ue::Fields},
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

The full op list with numbers is in [protocol](protocol.md#ops).

- **The agent never hooks, and starts no threads unless the host opts in.**
  Where `Tick` runs is the host's decision and its responsibility. A host that
  sets `Host::backgroundSearch` lets search jobs, the console's internal
  scans and image copies (`op::ImageCopy`, the
  [image window](protocol.md#handshake)) run on one agent-owned worker
  thread (started on first use, kept for the process); without it a job
  scans, and an image copy copies, in slices of each host tick. The two opt-in swaps are the call recorder (a
  host that lists its ops) and `reagentlive::ue-draw` (a host that links it,
  when `game-thread` arms it); neither writes code or the game's tables.
- **Held requests** make the agent spin briefly on the host's thread for the
  client's next request and run it in the same tick, bounded by the host's
  budgets ([protocol](protocol.md#exchange)).
- **Injecting again into the same process** is handled: the newest agent
  takes the section over ([takeover](protocol.md#section)). `status`
  reports the answering agent's build time and how many it replaced. An
  agent its host has stopped no longer ticks, so it restores nothing:
  clear `undo` writes (`write` with `clear`) before stopping it. This holds only for a build with the same protocol:
  after a protocol change the section keeps the old layout, so restart the
  process instead ([the update procedure](agents/update.md#7-running-targets)).
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

Off the engine's thread, `status` adds `tick_note` saying so once, and the
results that carry `same_tick` (`batch`, `get`) add `tick_accurate:
false`. Timed writes and captures act every host tick, as they always do.
The kind is the host's statement; the core does not verify it. The pack's
required thread check ([Unreal pack](commands/unreal.md)) remains the guard for
engine calls. Nothing that works today is refused because of the kind.
Why the kind is stated rather than detected: [design](design/unreal.md#decisions).

## Game-thread tick

`reagentlive::ue-draw` (`packs/ue/host_draw.h`, namespace
`reagentlive::ue::draw`) gives a host that ticks elsewhere (its own thread,
a frame hook) a tick on the game thread with no per-build address: it finds
the game viewport client with `FirstLiveInstance` and points its Draw
interface at a private copy of the vtable with Draw swapped. What the
command does and refuses: [`game-thread`](commands/unreal.md); why this
way: [design](design/unreal.md#a-game-thread-tick-through-the-viewports-draw).
The host API, as `examples/unreal/host.cpp` uses it:

| Call | When | Does |
|---|---|---|
| `Configure(tick, log)` | once, before `Poll` | the host's tick (run from Draw on the game thread, at most once per frame) and where state changes are logged (may be null) |
| `Arm()` | the host wants it without the command | arms the install, as `game-thread` with `on` does; clears a refused install so it is tried again |
| `Poll()` | every host tick, before the agent ticks, on the host's thread | installs once armed and the viewport is found (the pack must have located the object table: any Unreal command does); cheap otherwise |
| `Live()` | every host tick | a Draw tick ran in the last 500 ms: skip the host's own tick |
| `OnGameThread()` | inside the Draw tick | the host's `Context::gameThread` for that tick (with `HostThread::Engine`) |
| `Disable()` | to stop ticking on Draw | puts the viewport's table back if it still holds the copy (`Arm` arms again); the copy stays allocated, since a Draw inside the hook keeps forwarding |
| `Shutdown()` | the host is stopping | `Disable`, for good |
| `State()` | any time | the `GameThreadState` the command reports (mode, thread, viewport, table, Draw) |
| `GameThread` | the host's command table | the op behind `game-thread` (`ue::op::GameThread`) |

Two threads then tick the same agent (the host's own and Draw), so the
host lets one tick run at a time (the example uses an interlocked flag)
and builds its `Context` and `Host` per tick: `OnGameThread` and
`HostThread::Engine` in the Draw tick, its own check and kind elsewhere.
A host with a stop path calls `Shutdown` in it.

## Logging

When the host sets `Host::log`, the agent logs `Dev agent: listening on
<section>` once, then command records with the name, address or object, length
and result. Hosts can suppress successful reads and lookups (`Command::logSuccess`
false) to keep bulk commands out of the log. Do so for every read-only op when
the log writes synchronously: logging runs inside the tick and counts against
the hold budget, so a slow log line splits held commands over ticks (one
request per tick in practice). Refusals log at warning level.

## Host verbs

A host's console (`RunConsole`) can add verbs of its own. Each is a row
of the console's verb table (`ConsoleConfig::verbs`, the row type in
`call/verb.h`): name, origin `host`, its tool pack
([tool packs](tool-interface.md#tool-packs); `modify` for one that writes),
a summary, args (key, type, help, default, required, list), and a result
type from a struct with `Describe` (`results/schema.h`); mark `writes` on
a verb that writes the target or calls into it and `blocks` on one that
holds the console for seconds. `build` stays null: the console checks a
call's args against the row (`call/check.h`) and passes them to
`ConsoleConfig::hostCommand(client, verb, args, envelope)`, which reads
them (`args.Int("seconds")`, `args.Str("file")`) and answers an envelope
with `ok` and `result` or `error`; a range it refuses itself can answer
`RefusalJson(verb, args.values, {message, key})` as the error. The row
gives the verb what the library's have: `help` with an example call,
schema refusals before the host sees the call, a generated
Python method (`c.host.<verb>`) and an MCP tool (`host_<verb>`), strict
results, and a check that the result matches its declared type (a
mismatch is an `internal` error). Why rows:
[design](tool-interface.md#self-describing-console).

## Names files

`ConsoleConfig::namesFile` names readable field names (names.h format)
beside the console. `ConsoleConfig::labelsFile` is where names people
confirm go, in labels format: `peek` with `label` writes there by default and a
static tool's `export_names` should; `disasm`'s `save` reads it with the
console's derived `<console>.labels.jsonl`. Give a path under version
control (absolute, or relative to the console exe) so names survive a clean
build and travel with the project; leave it empty to keep everything in the
console's file ([labels](commands/labels.md#labels-files)).

## AI agents

The `reagentlive` skill (`.claude/skills/reagentlive/`: `SKILL.md` and its
`references/`) teaches an AI agent the investigation method; installing it
is a step of [agent setup](agents/setup.md). In a host project, point the
agent at it from the host's own instructions, and keep what only that host
adds (console name, host commands, build and approval rules) in a host
skill or doc of its own, which the `reagentlive` skill tells the agent to
read first.
