# Clients

Three ways to drive an agent: the console (a ready-made command line), the
Python session client (over the console), or the C++ client library
(raw requests). Pick the Python client for scripts and AI agents, the C++
client for an outside program that needs raw requests. The console's
commands: [command pages](INDEX.md#commands).

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
  `entries`, the cache) are reached through its JSON mode, as the Python
  client does ([design](design.md#frontends)).

## Python session client

`frontends/python/reagentlive_session.py` drives any ReAgentLive console
through its stateful stdin mode, standard library only.

- **Results are strict.** At start the session reads the console's verb
  table (`help --json`) and wraps every result in a `Record` typed by its
  verb's row. A key the result type lacks raises `ResultKeyError` naming
  the valid keys; an optional key that is absent gives `None`; nested
  objects and list rows are strict too. A key guessed wrong fails on the
  line that guessed it, never as a quiet `None` or a caught `KeyError`.
  `record.raw` is the plain dict.
- **One method per verb,** named after it (`sig_check`, `watch_start`,
  `census_diff`, `pin_list`), sending that verb and returning its result
  as the console gave it: the keys are those of
  [result shapes](commands/results.md), or `s.help('<verb>')`. A host's
  verbs go through `s.ok('<line>')` and are strict the same way.
- **A method that reshapes says so in its name:** `read_bytes`,
  `read_struct` (a `struct` format), `read_batch_bytes`,
  `search_addresses` (ints, every page), `find_all`, `funcs_all`,
  `census_all`, `census_diff_all`, `entries_all`, `name_find_all` (rows of
  every page), `get_values` (decoded values; raises when a path fails),
  `name_map` (`{index: text or None}`), `call_bytes` (the block after a
  raw call), `batch_results` (`(items, same_tick)`, each item a strict
  result or a `ReAgentLiveError`), and the module's `samples(result)` (a
  watch's rows with values as bytes).
- `s.table_hash` is the console's `table_hash`: a cached script compares it
  to know its keys are stale.
- `addr` and `hex` values stay the text the console prints; `addr()`
  turns one into an int, `unhex()` a `bytes` value into bytes. It also has
  `f32`/`u64`/... decoders and `pack`.

Errors raise `ReAgentLiveError` with the console's error object. Use it
instead of hand-written session loops or parsing console JSON. Separate
calls never share a host tick (each crosses the stdin pipe, about 1 ms);
put what must line up in one `batch` or `get`.
`frontends/python/benchmark.py` measures round trips against a running
host.
