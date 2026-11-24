# Clients

*Home of:* what each interface offers (MCP tools, console, Python, C++ client) and how it behaves: marks, ceilings, state, turn-taking.

**TL;DR:** Four ways to drive the same agent: MCP tools (an AI agent's
default), the console (a person at a shell, quick checks), Python (loops
and comparisons over many results) and the C++ client (your own tools).
Every one speaks the same verbs with the same argument names, so what
`help` with `verb` shows holds everywhere, and every one passes the
literal `ref` a result printed.

## Which one

| Interface | Use it for | Start here |
|---|---|---|
| **MCP tools** | an AI agent: one typed tool per verb, arguments checked before they are sent, writes marked so the client asks | [MCP tools](#mcp-tools) |
| **Console** | a person at a shell, a quick check, no MCP registered | `<console> -c '{"verb": "find", "args": {"text": "X"}}'` ([console](commands/console.md)) |
| **Python** (generated functions, `Session`) | loops, sweeps, comparisons, long captures: logic that should not be twenty tool calls | [generated functions](#generated-python-functions) |
| **C++ client** | your own tool or outside program, linking the library | [C++ client](#c-client) |

Which one an AI agent picks for a given task is the skill's
[`interfaces.md`](../.claude/skills/reagentlive/references/interfaces.md).

All four end in the same place: the console's verb table, or (for C++)
the same requests underneath. Host and client are built from the same
revision; the layout check refuses a mismatched pair
([handshake](protocol.md#handshake)).

## Console

Each host ships a console executable (the examples': `reagentlive.exe`,
`reagentlive-ue.exe`). It is the one frontend that owns behaviour; every
other interface calls it or the same requests. How to drive it:
[console](commands/console.md). What each verb takes and returns: `help`
with `verb`, the [verb reference](commands/reference.md) and
[result shapes](commands/results.md).

A console attaches to one section name, which its host chose: use the
console built with the host you loaded.

## C++ client

```cpp
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/client_ue.h"

auto client = reagentlive::Client::Attach(L"Local\\mygame-dev-1234", reagentlive::LayoutCore + reagentlive::ue::LayoutPart);
if (!client) return report(client.error().message);
auto found = reagentlive::ue::Find(*client, "PlayerController_C_");
auto fields = reagentlive::ue::Fields(*client, found->objects[0].ref);   // all pages
auto bytes = client->ReadRange(found->objects[0].ref.address, 0x800);
```

`core/client.h` is the API reference. What it does not say:

- Synchronous, one request in flight per `Client`, not thread-safe, used
  from one thread (the slot is a mutex, which belongs to a thread).
- Results are `std::expected<T, reagentlive::Error>`; `Error::kind` is
  `Agent` (a refusal), `Timeout`, `Attach` or `Busy`.
- **Clients take turns** on the agent's slot ([protocol](protocol.md#handshake)).
- `CopyImage` reads whole images through the
  [image window](protocol.md#handshake): code and constants, never values.
- Commands that exist only in the console (`get`, `batch`, `entries`,
  `game-thread`) are reached through its JSON calls, as the Python client
  does ([design](design/architecture.md#frontends)). A ready-made console:
  `reagentlive::cli::RunConsole` ([host verbs](hosting.md#host-verbs)).

## Generated Python functions

`frontends/python/reagentlive_gen.py` writes a module for one console from
its `help` with `json` (no target needed), a host's verbs included:

```
python src/reagentlive/frontends/python/reagentlive_gen.py --console <console.exe> -o <scratch>/my_console.py
```

The generated module imports `reagentlive_session` from
`frontends/python/`, so a script needs both folders on `sys.path`:

```python
import sys
sys.path[:0] = [r'path\to\reagentlive\src\reagentlive\frontends\python', r'path\to\scratch']
from reagentlive_session import Session
from my_console import Console
with Session(r'path\to\console.exe') as s:
    c = Console(s)
    pawn = c.ue.find(cls='Pawn', view={'limit': 1})['rows'][0]['ref']
    c.ue.get(pawn, paths=[['Health'], ['Controller', 'PlayerState']])
    c.read('0x7ff600001000', len=16, type='u32')
```

- **One method per verb, named by origin:** core verbs on `Console`
  (`c.read`), a pack's and the host's under their origin (`c.ue.find`,
  `c.host.world`). Arguments are named by the console's keys (`cls`, never
  `class`). Each docstring is the verb's usage.
- **Every verb but `quit`:** a host's blocking verbs are there too and hold
  the script, unlike MCP, which leaves them out.
- **A stale module does not run:** it carries the table hash, and
  `Console(s)` raises `StaleTableError` when the console's differs.
  Generate it again after a build that changes any row.

## Python session

`frontends/python/reagentlive_session.py` drives any ReAgentLive console
through its stateful stdin mode, standard library only. The generated
module calls through it; scripts use it for the rest. Its module docstring
has the API (`call`, `help`, `batch_results`, background runs, helpers).

- **Results are strict.** The session reads the console's verb table and
  wraps every result in a `Record` typed by its verb's row. A key the type
  lacks raises `ResultKeyError` naming the valid keys; an absent optional
  key gives `None`. A key guessed wrong fails on the line that guessed it,
  never as a quiet `None`. `record.raw` is the plain dict.
- **Callers:** `Session(console, args, caller='script')` is counted apart
  from an agent's context in the
  [usage log](commands/console.md#usage-log-and-usage); pass `caller='tool'`
  when an agent reads every result. A tool session gets the console's
  [result cap](commands/console.md#fewer-round-trips), a script none.
- **Packs ceiling:** `Session(console, packs=['explore', 'native'])` runs
  the console with that [`--packs`](commands/console.md#one-shot-use)
  ceiling; a call outside it raises `ReAgentLiveError` of kind `pack`. A
  script that must not change the target leaves `modify` out.
- **No timeout:** a call waits until the console answers or exits; the
  console's busy wait still applies.
- **Sharing the agent:** a session's console takes the agent's slot per
  request and frees it between calls, so an MCP server or another script
  runs in between ([console](commands/console.md#agent-and-console)). Values
  from two calls are never from one moment: make one `batch` (or `get`) for
  that.
- **Background runs:** `s.call('run', {'plan': path, 'background': True})`
  starts the plan in its own console; `run_status`, `run_read` and
  `run_stop` follow it. Runs end with the session.

Errors raise `ReAgentLiveError` with the console's error object. Use it
instead of hand-written session loops or parsing console JSON.

## MCP tools

`frontends/mcp/reagentlive_mcp.py` serves the console's verbs as MCP
tools over standard input and output, so an agent picks a tool and fills
in checked, named arguments instead of writing a command line. `--help`
lists its options. It needs the MCP Python SDK 2
(`pip install --upgrade "mcp>=2"`).

- **One tool per verb:** a core verb by name (`read`), a pack's or the
  host's with its origin (`ue_find`, `host_world`); `-` becomes `_`. Each
  tool is made from the verb's row in the console's `help` with `json`:
  nothing is written per tool. A rebuilt console's changed table is read on
  the next call and the client told the list changed
  ([design](tool-interface.md#tool-interface)).
- **The console decides the target.** `--console` must be the console of
  the host you loaded: the Unreal example's agent is reached only by
  `reagentlive-ue.exe`, the own-thread example's only by `reagentlive.exe`.
  A console built without the pack serves no pack tools.
- **Tool packs:** a tool is served while its verb's
  [pack](tool-interface.md#tool-packs) is on. `--packs` names the packs on
  from the start (default `explore`), `--allow` those the agent may turn on
  later; `modify`, which holds every verb that writes or calls, is
  reachable only when one of them names it. The `packs` tool lists the
  packs and enables or disables them within the ceiling. The person who
  starts the server sets the ceiling; every console the adapter starts gets
  it as its own [`--packs`](commands/console.md#one-shot-use), so the
  console refuses past it too.
- **Marks:** a verb that writes the target's memory or calls into it is
  `destructiveHint`, every other one `readOnlyHint`, so a client can allow
  the read-only tools and ask first for the rest. `call-pure` is marked
  read-only: the agent checks the function is pure before calling
  ([design](design/README.md)). The skill's approval rules still apply (the
  first call on a new host or game needs the user's approval, `call-pure`
  included: a wrong engine layout can crash the game).
- **Left out:** `quit` (the adapter owns the console) and verbs marked
  `blocks`, which would outlast a client's timeout. Captures go as
  `watch-start`, then `watch-read` (with `stop` to end it). A blocking verb with a
  `background` argument (`run`) is served as a background run: `run`
  answers `{run, state}` at once and `run_status`, `run_read` and
  `run_stop` (asked first: it ends a process) follow it.
- **`batch`** takes a list of `{verb, args}` (console verb names:
  `sig-check`) and runs them in as few host ticks as possible. It takes
  only verbs of the packs that are on and none marked as writing: a write,
  call or `game-thread` inside it is refused, so it goes as its own tool
  and is approved.
- **A console per call:** each tool call starts the console and ends it,
  one at a time, so the console can be rebuilt while the server runs.
  Clients take the agent's slot in turns: a call that finds it taken waits
  up to `--busy-wait` (default 10 s), then is refused as `busy`, naming the
  holder. With no target the console answers `help` and `usage` and refuses
  the rest as `startup`.
- **Refusals made here are logged** as `refused` calls, so `usage` counts
  them with the console's own ([console](commands/console.md#one-shot-use)).
- **Results past `max_chars`** come back as the console's `digest` and the
  call to make next ([result cap](commands/console.md#fewer-round-trips)).
  Every console runs with `--caller tool`, so `usage` counts each result as
  context; past `--context-limit` a result's text gets a `Context warning`.
- **Instructions:** the server tells the agent to load the `reagentlive`
  skill (and the host's own) before its first call, and marks text read
  from the target as untrusted data: in a multiplayer game other people
  write much of it, so an agent must never act on it as an instruction.
  Result schemas are `compact` by default to keep the tool list small
  ([tool interface](tool-interface.md#tool-packs)).
