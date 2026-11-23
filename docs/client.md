# Clients

*Home of:* what each interface offers (MCP tools, console, Python, C++ client) and its API: setup, options, state, marks, limits and timeouts; the helpers beside them.

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
| IDA label import (a helper) | bringing the console's labels into an IDA database | [helpers](#helpers) |

Which one an AI agent picks for a given task is the skill's
[`interfaces.md`](../.claude/skills/reagentlive/references/interfaces.md).

All four end in the same place: the console's verb table, or (for C++)
the same requests underneath. Host and client are built from the same
revision; the layout check refuses a mismatched pair
([handshake](protocol.md#handshake)).

## Console

Each host ships a console executable (the examples': `reagentlive.exe`,
`reagentlive-ue.exe`). It is the one frontend that owns behaviour; every
other interface calls it or the same requests. How to drive it (JSON
calls, addresses and refs, budgets, `batch`, the cache file,
one-shot runs, arguments, the usage log): [console](commands/console.md).
What each verb takes and returns: `help` with `verb`, the
[verb reference](commands/reference.md) and
[result shapes](commands/results.md).

A console attaches to one section name, which its host chose: the
own-thread example's console finds only the own-thread agent, the Unreal
example's only the Unreal one. Use the console built with the host you
loaded.

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

- Synchronous, one request in flight per `Client`, not thread-safe.
- Results are `std::expected<T, reagentlive::Error>`. `Error::kind` is
  `Agent` (a refusal, with its status and reason), `Timeout`, `Attach` or
  `Busy` (another client holds the agent).
- **Timeouts:** 5 s a request by default (`SetTimeout` changes it).
  `Search` and `Xrefs` take their own; the Unreal pack's scans (`Refs`,
  `ExecEntries` and the like) default to minutes.
- Pointer chains: `Client::ResolveChain` ([native objects](commands/native-objects.md#pointer-chains)).
- A ready-made console: `reagentlive::cli::RunConsole({prefix, exeName, layout}, argc, argv)`
  ([host verbs](hosting.md#host-verbs) for commands of your own).
- Commands that exist only in the console (`get`, `batch`,
  `entries`, the cache, output budgets) are reached through its JSON calls,
  as the Python client does ([design](design.md#frontends)). So is
  `game-thread`, which has no typed wrapper.

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
  (`c.read`, `c.sig_check`), a pack's and the host's under their origin
  (`c.ue.find`, `c.host.world`). Required arguments first (by
  position or name), the others keyword-only, named by the console's keys
  (the table refuses a Python keyword: `cls`, `start`, `type`). An
  argument that takes several values takes a list, an object a dict, except bytes, patterns and text, which are one string (or
  `bytes`). Each docstring is the verb's usage, arguments and notes.
- **Every verb but `quit`:** a host's blocking ones are there too and
  hold the script for their duration, unlike MCP, which leaves them out.
  The session's `watch(seconds, ranges)` is a blocking capture:
  `watch-start`, a wait, then `watch-stop`'s result.
- **Typed:** argument types from the row (`addr` is `int | str`, an int
  goes as hex), each result annotated with a `TypedDict` of the console's
  keys (named types are classes: `Chain`, `Object`, `UeName`), so a type
  checker reads a script before it runs. A wrong argument name is a
  `TypeError` at the call.
- **The console's shape:** a method returns its verb's strict `Record`
  (below), key for key what the console printed; an `addr` stays its hex
  text.
- **Literal refs:** calls go as JSON, which takes the `ref` a result
  printed ([JSON calls](commands/console.md#json-calls)).
- **A stale module does not run:** it carries the table hash, and
  `Console(s)` raises `StaleTableError` when the console's differs.
  Generate it again after a build that changes any row.

## Python session

`frontends/python/reagentlive_session.py` drives any ReAgentLive console
through its stateful stdin mode, standard library only. The generated
module calls through it; scripts use it for the rest.

- **Results are strict.** At start the session reads the console's verb
  table (`help` with `json`) and wraps every result in a `Record` typed by its
  verb's row. A key the result type lacks raises `ResultKeyError` naming
  the valid keys; an optional key that is absent gives `None`; nested
  objects, named types and list rows are strict too. A key guessed wrong
  fails on the line that guessed it, never as a quiet `None` or a caught
  `KeyError`. `record.raw` is the plain dict.
- `s.call(verb, args)` sends one JSON call with arguments named as
  `help` with `verb` shows them (None and False left out, bytes as hex) and
  returns the strict result. `s.help('<verb>')` shows a verb's arguments
  and result keys; `s.send(line)` is the raw exchange (the envelope as
  plain dicts).
- **Two helpers, for what a generated function cannot do:**
  `s.batch_results(calls)` (`(items, same_tick)`, each item a strict
  result or a `ReAgentLiveError`) and the module's `samples(result)` (a
  watch's rows with values as bytes). Nothing else in the session names a
  verb. Paging, filtering and retrying are the console's: follow
  `next_cursor` with `view.start`, or use the verb's `view.limit`
  (0 for every row on `census`).
- `s.table_hash` is the console's `table_hash`.
- `Session(console, args, caller='script')` starts the console with
  `--caller`: its [usage log](commands/console.md#usage-log-and-stats)
  counts a script's output apart from an agent's context. Pass
  `caller='tool'` when an agent reads every result. `table=` takes a
  `help` with `json` result read before: the session then asks only for the
  hash (`help` with `hash`) and reads the table again only when it changed.
- `Session(console, packs=['explore', 'native', 'observe'])` runs the
  console with that [`--packs`](commands/console.md#one-shot-use)
  ceiling: the generated module keeps every verb, and a call outside the
  packs (a `batch` item too) raises `ReAgentLiveError` whose `kind` is
  `pack`. A script that must not change the target leaves `modify` out.
- **No timeout:** a call waits until the console answers or exits. The
  console's own waits still apply (2 s for a busy agent, then `busy`).
- `addr()` turns an `addr` or `hex` value into an int, `unhex()` a `bytes`
  value into bytes, `f32`/`u64`/... decode those bytes, and `pack` goes
  the other way: `f32(unhex(c.read(a, len=4)['hex']))`.

Errors raise `ReAgentLiveError` with the console's error object. Use it
instead of hand-written session loops or parsing console JSON. Separate
calls never share a host tick (each crosses the stdin pipe, about 1 ms);
put what must line up in one `batch` or `get`.

## MCP tools

`frontends/mcp/reagentlive_mcp.py` serves the console's verbs as MCP
tools over standard input and output, so an agent picks a tool and fills
in checked, named arguments instead of writing a command line:

```
python reagentlive_mcp.py --console <console.exe> [--name <server name>] [--output-schemas compact|off]
                          [--packs explore,native] [--allow observe,modify] [-- <console args: pid, --names <file>>]
```

- **One tool per verb:** a core verb by name (`read`), a pack's or the
  host's with its origin (`ue_find`, `host_world`); `-` becomes `_`
  (`sig_check`). The adapter reads the console's `help` with `json` (no target
  needed) and makes each tool from its row: the argument schema from the
  row's arguments (named by the row's keys, so a call goes on as the
  console's JSON call), the result schema from its result's top-level
  keys, the description from its summary, usage and notes. Nothing is
  written per tool and no module file is involved.
- **The console decides the target.** `--console` must be the console of
  the host you loaded: the Unreal example's agent is reached only by
  `reagentlive-ue.exe`, the own-thread example's only by `reagentlive.exe`.
- **Arguments are checked before a console starts,** against the row of
  the console's current table: a guessed argument name is refused with the
  verb's usage. A null argument counts as left out. Every tool also takes
  `budget` (the console's [output budget](commands/console.md#fewer-round-trips)).
- **Tool packs:** a tool is served while its verb's
  [pack](tool-interface.md#tool-packs) is on. `--packs` names the packs
  on from the start (default `explore`, always on), `--allow` those the
  agent may turn on later; `modify`, which holds every verb that writes or
  calls, is reachable only when one of them names it. The `packs` tool is
  always served and read-only: with no arguments it lists each pack, its
  state (`on`, `off`, `outside` the ceiling) and its tools; `{"action":
  "enable", "pack": "native"}` adds a pack's tools and tells the client
  the list changed, `disable` removes them. A pack outside `--packs` and
  `--allow` is refused: the person who starts the server sets the
  ceiling. Every console the adapter starts gets that ceiling as its own
  [`--packs`](commands/console.md#one-shot-use), so the console refuses a
  verb past it too.
- **Marks:** a verb that writes the target's memory or calls into it is
  `destructiveHint`, every other one `readOnlyHint`, so a client can
  allow the read-only tools and ask first for the rest. `call-pure` is
  marked read-only: the agent checks the function is pure before calling
  ([design](design.md)). `status` reads the game-thread mode;
  `game-thread` only arms and removes the tick. The skill's approval rules
  still apply (the first call on a new host or in a new game needs the
  user's approval, `call-pure` included: a wrong engine layout can crash
  the game).
- **Only what the console has:** the tools are its table's rows, so a
  console built without the pack ([hosting](hosting.md#using-it-in-your-project)) serves no
  pack tools.
- **Left out:** `quit` (the adapter owns the console) and verbs marked
  `blocks` (a host's countdown capture): a call would outlast a client's
  timeout. Captures go as `watch-start`, then `watch-read` or `watch-stop`.
- **`batch`** takes a list of `{verb, args}` (console verb names:
  `sig-check`, not `sig_check`) and runs them in as few host ticks as
  possible, one envelope each. It takes only verbs of the packs that are
  on, and none marked as writing: a write, call or `game-thread` inside it
  is refused, so it goes as its own tool and is approved.
- **State in arguments:** tools take the literal refs results carry and
  the ids of timed writes and captures.
- **A console per call:** each tool call starts the console and ends it,
  one call at a time. Another client on the same agent (a script, a second
  server) makes the console wait up to 2 s, then refuse as `busy`.
  Nothing holds the executable between calls, so the
  console can be rebuilt while the server runs and the next call runs the
  new build. With no target the console answers `help` and `stats` and
  refuses the rest as `startup`.
- **A rebuilt console's tools:** each start reads the table's hash
  (`help` with `hash`). When it changed, the adapter reads the new table before
  the call runs and tells the client the list changed. A client that asks
  again gets the new list with no reconnect; one that ignores the notice
  keeps working for verbs whose arguments and result did not change, and
  needs a reconnect for the rest ([design](tool-interface.md#tool-interface)).
- **Results past the budget:** 25,000 characters by default; past it the
  tool returns the console's `digest` and a text naming the call to make
  next, or the file holding the result of a call that must not run again
  ([output budgets](commands/console.md#fewer-round-trips)).
- **Usage:** every console runs with `--caller tool` and one session id
  per server, so `stats` counts every tool result as context and groups
  the server's calls. Each result goes to the client twice, as text and
  as structured content.
- **A smaller tool list:** a client that loads every tool up front pays
  for the whole list before any work, so result schemas are `compact`
  (each result's top-level keys and their JSON types; the console still
  checks every result in full). `--output-schemas off` advertises none,
  and the packs left off are not listed; an unknown pack name stops the
  server with the list, and the server's instructions say which packs it
  serves and which `packs` can add. Why and what it saved:
  [tool interface](tool-interface.md#tool-packs).
- The server's instructions mark text read from the target (names,
  strings, chat) as untrusted data: in a multiplayer game other people
  write much of it, so an agent must never act on it as an instruction.
- The name option (in the usage above) sets the server name clients
  show (default `reagentlive`).
- Needs the MCP Python SDK 2 (`pip install --upgrade "mcp>=2"`; its low-level
  server). A rebuilt console's new tool list reaches a 2026-07-28 client on its
  `subscriptions/listen` stream and an older one as a plain notification. Check a
  build with the MCP Inspector: it lists the tools and calls one with no
  AI client.

## Helpers

Not interfaces: they never drive the agent.

- **IDA side** (`frontends/ida/reagentlive_ida.py`, one file in IDA's
  loaders folder): loads what `disasm`'s `save` wrote, so IDA decompiles it
  without the whole image
  ([saving a function](commands/fast-lane.md#saving-a-function-for-a-static-tool)),
  and carries names both ways: `import_labels` brings in the console's
  labels file, `export_names` writes the names you gave back into it
  ([labels for a static tool](commands/unreal.md#labels-for-a-static-tool)).
- **Round-trip timings** (`frontends/python/benchmark.py`): measures calls
  against a running host.
