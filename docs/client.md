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
- A ready-made console: `reagentlive::cli::RunConsole({prefix, exeName, layout, prompt}, argc, argv)`.
- Commands that exist only in the console (`dump`, `get`, `batch`,
  `entries`, the cache) are reached through its JSON mode, as the Python
  client does ([design](design.md#frontends)).

## Python session client

`frontends/python/reagentlive_session.py` drives any ReAgentLive console through its
stateful stdin mode, standard library only: `read`, `read_struct` (a
`struct` format), `read_layout` (a [typed layout](commands/memory.md#typed-layouts)), `label` ([native members](commands/unreal.md#native-members)), `read_batch`, `get` (field paths), `batch` (command lines,
one result or `ReAgentLiveError` each, plus `same_tick`), `write` (with
`expect`), `search` (pages until done or a limit), `peek`, `entries` (pages to the end), `pin`, `watch`,
`watch_start`, `watch_read`, `watch_stop` (a capture that returns at once),
`names`, `status`, `modules`, `export_labels`, `sig_check`, `sig_check_file`, `sig_bytes`
([signatures](commands/fast-lane.md#signatures)), `disasm` (the rows) and `xrefs` (the whole
result) ([fast lane](commands/fast-lane.md)), `vtable`, `region`, `read_chain` and `read_str`
([native objects](commands/native-objects.md)). The lookups return the rows in
[Result shapes](commands/results.md):
- `find` and `name_find` (pages on request), `describe`, `fields`, `dump`
  (`changed=True` for the session baseline, or a snapshot file), `census`
  (`class_name`, `match`, `changed`, `limit` per page, `pages`: None
  reads to the end);
- `funcs` (every page), `func`, `script` (`func --script`), `params`,
  `refs` (the whole result: `capped` changes what the list means);
- `each` returns the rows, with `seconds` `(rows, changes)`; a capped
  instance list or a failed poll raises `ReAgentLiveError` with the whole result
  in `.error['result']`.

Calls and captures: `call` with a hex block returns the block as bytes; with
`args` (`{Name: value}`, `{}` for no inputs) it sends the typed form and
returns the result object (`outputs`, `kept`). `release` drops a kept block.
`snap(ref, file=None)` takes the baseline; `record(ref, file, fields,
seconds)` blocks like the console and returns `(path, samples)`.

It also has `f32`/`u64`/... decoders and `pack`. Errors raise `ReAgentLiveError`
with the console's error object. Use it instead of hand-written session
loops or parsing console JSON. Separate calls never share a host tick (each
crosses the stdin pipe, about 1 ms); put what must line up in one `batch` or
`get`. `frontends/python/benchmark.py` measures round trips against a
running host.
