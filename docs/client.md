# Clients

Four ways to drive an agent: the console (a ready-made command line), typed
Python functions generated from the console's own table, MCP tools made
from those functions, or the C++ client library (raw requests). Pick the
MCP tools for an AI agent that picks one call at a time, the generated
functions for scripts, the C++ client for an outside program that needs
raw requests. The console's commands: [command pages](INDEX.md#commands).

Host and client are built from the same revision; the layout check
refuses a mismatched pair ([handshake](protocol.md#handshake)).

## C++ client

```cpp
#include "reagentlive/core/client.h"
#include "reagentlive/ue/client_ue.h"

auto client = reagentlive::Client::Attach(L"Local\mygame-dev-1234", reagentlive::LayoutCore + reagentlive::ue::LayoutPart);
if (!client) return report(client.error().message);
auto found = reagentlive::ue::Find(*client, "PlayerController_C_");
auto fields = reagentlive::ue::Fields(*client, found->objects[0].ref);   // all pages
auto bytes = client->ReadRange(found->objects[0].ref.address, 0x800);
```

- Synchronous, one request in flight per `Client`, not thread-safe.
- Results are `std::expected<T, reagentlive::Error>`; `Error::kind` tells agent
  refusals, timeouts and attach failures apart.
- Pointer chains: `Client::ResolveChain` ([native objects](commands/native-objects.md#pointer-chains)).
- A ready-made console: `reagentlive::cli::RunConsole({prefix, exeName, layout}, argc, argv)`
  ([host verbs](hosting.md#host-verbs) for commands of your own).
- Commands that exist only in the console (`dump`, `get`, `batch`,
  `entries`, the cache) are reached through its JSON calls, as the Python
  client does ([design](design.md#frontends)).

## Generated Python functions

`frontends/python/reagentlive_gen.py` writes a module for one console from
its `help --json` (no target needed), a host's verbs included:

```
python reagentlive_gen.py --console <console.exe> -o my_console.py
```

```python
from reagentlive_session import Session
from my_console import Console
with Session(r'path\to\console.exe') as s:
    c = Console(s)
    pawn = c.ue.find('Pawn', cls='Pawn')['rows'][0]['ref']
    c.ue.get(pawn, ['Health', 'Controller.PlayerState'])
    c.read(0x7ff600001000, 16, type='u32')
```

- **One method per verb, named by origin:** core verbs on `Console`
  (`c.read`, `c.sig_check`), a pack's and the host's under their origin
  (`c.ue.find`, `c.host.world`). Positionals in order, flags as
  keyword-only arguments, named by the console's keys (the table refuses
  a Python keyword: `cls`, `start`, `type`). A positional that takes several values takes
  a list, except bytes, patterns and text, which are one string (or
  `bytes`). Each docstring is the verb's usage, arguments and notes.
- **Typed:** argument types from the row (`addr` is `int | str`, an int
  goes as hex), each result annotated with a `TypedDict` of the console's
  keys (named types are classes: `Chain`, `Object`, `UeName`), so a type
  checker reads a script before it runs. A wrong argument name is a
  `TypeError` at the call.
- **The console's shape:** a method returns its verb's strict `Record`
  (below), key for key what the console printed; an `addr` stays its hex
  text.
- **Literal refs:** calls go as JSON, which takes the `ref` a result
  printed and refuses `$N` ([JSON calls](commands/console.md#json-calls)).
- **A stale module does not run:** it carries the table hash, and
  `Console(s)` raises `StaleTableError` when the console's differs.
  Generate it again after a build that changes any row.

## Python session

`frontends/python/reagentlive_session.py` drives any ReAgentLive console
through its stateful stdin mode, standard library only. The generated
module calls through it; scripts use it for the rest.

- **Results are strict.** At start the session reads the console's verb
  table (`help --json`) and wraps every result in a `Record` typed by its
  verb's row. A key the result type lacks raises `ResultKeyError` naming
  the valid keys; an optional key that is absent gives `None`; nested
  objects, named types and list rows are strict too. A key guessed wrong
  fails on the line that guessed it, never as a quiet `None` or a caught
  `KeyError`. `record.raw` is the plain dict.
- `s.call(verb, args)` sends one JSON call with arguments named as
  `help <verb>` shows them (None and False left out, bytes as hex) and
  returns the strict result. `s.help('<verb>')` shows a verb's arguments
  and result keys; `s.send(line)` is the raw exchange (the envelope as
  plain dicts).
- **Two helpers, for what a generated function cannot do:**
  `s.batch_results(calls)` (`(items, same_tick)`, each item a strict
  result or a `ReAgentLiveError`) and the module's `samples(result)` (a
  watch's rows with values as bytes). Nothing else in the session names a
  verb. Paging, filtering and retrying are the console's: follow
  `next_cursor` with `start`, or use the verb's limit flag
  (`census --limit 0`).
- `s.table_hash` is the console's `table_hash`.
- `Session(console, args, caller='script')` starts the console with
  `--caller`: its [usage log](commands/console.md#usage-log-and-stats)
  counts a script's output apart from an agent's context. Pass
  `caller='tool'` when an agent reads every result. `table=` takes a
  `help --json` result read before: the session then asks only for the
  hash (`help --hash`) and reads the table again only when it changed.
- `addr()` turns an `addr` or `hex` value into an int, `unhex()` a `bytes`
  value into bytes, `f32`/`u64`/... decode those bytes, and `pack` goes
  the other way: `f32(unhex(c.read(a, 4)['hex']))`.

Errors raise `ReAgentLiveError` with the console's error object. Use it
instead of hand-written session loops or parsing console JSON. Separate
calls never share a host tick (each crosses the stdin pipe, about 1 ms);
put what must line up in one `batch` or `get`.
`frontends/python/benchmark.py` measures round trips against a running
host.

## MCP tools

`frontends/mcp/reagentlive_mcp.py` serves the console's verbs as MCP
tools over standard input and output, so an agent picks a tool and fills
in checked, named arguments instead of writing a command line:

```
python reagentlive_mcp.py --console <console.exe> [--output-schemas compact|off]
                          [--families core,ue,host] [--read-only] [-- <pid>]
```

- **One tool per verb:** a core verb by name (`read`), a pack's or the
  host's with its origin (`ue_find`, `host_world`). The adapter reads the
  console's `help --json` (no target needed) and makes each tool from its
  row: the argument schema from the row's arguments (named by the row's
  keys, so a call goes on as the console's JSON call), the result schema
  from its result's top-level keys, the description from its summary,
  usage and notes. Nothing is written per tool and no module file is
  involved.
- **Arguments are checked before a console starts,** against the row of
  the console's current table: a guessed argument name is refused with the
  verb's usage. A null argument counts as left out.
- **Marks:** a verb that writes the target's memory or calls into it is
  `destructiveHint`, every other one `readOnlyHint`, so a client can
  allow the read-only tools and ask first for the rest. The host's own
  approval rules still apply.
- **Only what the console has:** the tools are its table's rows, so a
  console built without the pack ([hosting](hosting.md#using-it-in-your-project)) serves no
  pack tools and no core tool that needs a pack.
- **Left out:** `quit` (the adapter owns the console) and verbs marked
  `blocks` (`watch`, `record`, a host's countdown capture): a call would
  outlast a client's timeout. Use `watch-start`, then `watch-read`.
- **`batch`** takes a list of `{verb, args}` (console verb names) and runs
  them in as few host ticks as possible, one envelope each.
- **State in arguments:** tools take the literal refs results carry and
  the ids of pins, trials and captures; `$N` is refused.
- **A console per call:** each tool call starts the console and ends it,
  one call at a time. Another client on the same agent (a script, a second
  server) makes the console wait up to 2 s, then refuse as `busy`.
  Nothing holds the executable between calls, so the
  console can be rebuilt while the server runs and the next call runs the
  new build. With no target the console answers `help` and `stats` and
  refuses the rest as `startup`.
- **A rebuilt console's tools:** each start reads the table's hash
  (`help --hash`). When it changed, the adapter reads the new table before
  the call runs and tells the client the list changed (the server says it
  sends that notice). A client that asks again gets the new list
  with no reconnect; one that ignores the notice keeps working for verbs
  whose arguments and result did not change, and needs a reconnect for
  the rest ([design](tool-interface.md#tool-interface)).
- **Usage:** every console runs with `--caller tool` and one `--session`
  per server, so `stats` counts every tool result as context and groups
  the server's calls. Each result goes to the client twice, as text and
  as structured content.
- **Cost, and how to cut it:** a client that loads every tool up front
  pays for the whole list before any work. Result schemas were most of
  it, so they are `compact`: each result's top-level keys and their JSON
  types, never narrower than the whole type, so every console result
  still validates (the console checks results strictly either way).
  `--output-schemas off` advertises none. Measured once, as the serialized
  tool list: about 58 KB for the own-thread example's console (core and
  pack verbs) by default; on one host's console with two host verbs,
  before the adapter made schemas from the rows, about 45 KB with `off`
  and 30 KB with `--families core --read-only`.

  `--families` serves only the verbs of the origins named (`help --json`
  lists them; an unknown one stops the server with the list) and
  `--read-only` leaves out every verb marked `writes`; the server's
  instructions then say what it leaves out. `help <verb>` still shows any
  verb's full result in notation.
- Needs the MCP Python SDK (`pip install mcp`; its low-level server). Check a
  build with the MCP Inspector: it lists the tools and calls one with no
  AI client.

| Work | Use | Why |
|---|---|---|
| One lookup, or a few | a tool | checked before it is sent; the result is in view |
| Related reads at one moment | the `batch` tool | one tick |
| A loop, a sweep, a comparison over many objects | a script on the generated functions | one call; only the summary returns |
| A capture that outlasts one call | `watch-start`, then a tool or a script to read it | a tool call should not block for seconds |
