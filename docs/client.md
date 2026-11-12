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
  keyword-only arguments; a Python keyword or `self` gets a trailing `_`
  (`cls`, `start`, `type`). A positional that takes several values takes
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
  `help <verb>` shows them (None and False left out, bytes as hex);
  `s.ok(line)` sends a typed line. Both return the strict result.
  `s.help('<verb>')` shows a verb's arguments and result keys.
- **Helpers convert and say so in their name:** `read_bytes`,
  `read_struct` (a `struct` format), `read_batch_bytes` (`(tick, [bytes
  or None])`), `get_values` (decoded values; raises when a path fails),
  `name_map` (`{index: text or None}`), `call_bytes` (the block after a raw
  call), `batch_results` (`(items, same_tick)`, each item a strict result
  or a `ReAgentLiveError`), and the module's `samples(result)` (a watch's
  rows with values as bytes). Paging, filtering and retrying are the
  console's: follow `next_cursor` with `start`, or use the verb's limit
  flag (`census --limit 0`).
- `s.table_hash` is the console's `table_hash`.
- `addr()` turns an `addr` or `hex` value into an int, `unhex()` a `bytes`
  value into bytes; `f32`/`u64`/... decoders and `pack` go the other way.

Errors raise `ReAgentLiveError` with the console's error object. Use it
instead of hand-written session loops or parsing console JSON. Separate
calls never share a host tick (each crosses the stdin pipe, about 1 ms);
put what must line up in one `batch` or `get`.
`frontends/python/benchmark.py` measures round trips against a running
host.

## MCP tools

`frontends/mcp/reagentlive_mcp.py` serves the generated module's methods
as MCP tools over standard input and output, so an agent picks a tool and
fills in checked, named arguments instead of writing a command line:

```
python reagentlive_mcp.py --console <console.exe> --module my_console.py [-- <pid>]
```

- **One tool per verb:** a core verb by name (`read`), a pack's or the
  host's with its origin (`ue_find`, `host_world`). Argument and result
  schemas come from the functions' types; descriptions from their
  docstrings. Nothing is written per tool.
- **Marks:** a verb that writes the target's memory or calls into it is
  `destructiveHint`, every other one `readOnlyHint`, so a client can
  allow the read-only tools and ask first for the rest. The host's own
  approval rules still apply.
- **Left out:** `quit` (the adapter owns the console) and verbs marked
  `blocks` (`watch`, `record`, a host's countdown capture): a call would
  outlast a client's timeout. Use `watch-start`, then `watch-read`.
- **`batch`** takes a list of `{verb, args}` (console verb names) and runs
  them in as few host ticks as possible, one envelope each.
- **State in arguments:** tools take the literal refs results carry and
  the ids of pins, trials and captures; `$N` is refused.
- **Startup:** the adapter compares the module's table hash with the
  console's (`help --json`, no target) and refuses to start on a
  mismatch. The console itself starts on the first tool call and again
  after it exits, so the tool list is there with no target running.
- **Cost:** with every pack verb the tool list is about 110 KB of schema;
  a client that loads every tool's schema up front pays that before any
  work.
- Needs the MCP Python SDK (`pip install mcp`; its FastMCP). Check a
  build with the MCP Inspector: it lists the tools and calls one with no
  AI client.

| Work | Use | Why |
|---|---|---|
| One lookup, or a few | a tool | checked before it is sent; the result is in view |
| Related reads at one moment | the `batch` tool | one tick |
| A loop, a sweep, a comparison over many objects | a script on the generated functions | one call; only the summary returns |
| A capture that outlasts one call | `watch-start`, then a tool or a script to read it | a tool call should not block for seconds |
