# Contributing

Where code goes, how it is tested, the rules the code keeps, and where each
kind of fact is written down. Read it before adding a command or a doc.

## Layers

The code lives in `src/reagentlive/`, and only `src/` is on the include
path (`#include "reagentlive/core/agent.h"`). Code paths in these docs
(`core/`, `ue/`, `frontends/`) are relative to `src/reagentlive/`;
`tests/`, `examples/` and `docs/` sit at the top.

| Layer | What | Where | Depends on |
|---|---|---|---|
| Core agent | wire protocol, shared section, tick and dispatch, `read`/`write`/`modules`/`search`/`xrefs`/`region`/`pin`/`watch` | `core/agent.*`, `core/protocol.h` | Windows, standard library |
| Core client | typed C++ API: attach, read, write, modules, search, xrefs, region, pointer chains, supported ops, raw `Send` | `core/client.*` | protocol |
| Unreal pack | agent handlers for lookup, reflection, guarded object reads and calls over a Dumper-7 SDK | `ue/` | core; agent side also the host's `SDK/` headers |
| CLI | command parsing and JSON Lines over the client API | `frontends/cli/` (by layer: [source layout](#source-layout)) | client, Unreal client calls |
| Python, IDA | session client over the console; label importer | `frontends/python/`, `frontends/ida/` | the console's JSON mode, its label file |
| Host glue | section name, checks, logging, command table, where `Tick` runs, optional roots for `watch --follow` | the host's code ([hosting](hosting.md)) | core, packs |

Dependencies point one way: host → pack → core. Frontends use the client API,
never the section directly. ReAgentLive never includes host code.

## Adding a command

1. **Agent op** (only when the console cannot build it from existing ops):
   the handler in `core/` or the pack, its op number in the right range
   ([protocol](protocol.md#ops)), a loopback or pack test, and the op's row
   in [protocol](protocol.md).
2. **Verbs:** `frontends/cli/syntax/parse_<area>.cpp`, with parse tests.
3. **Pure logic and JSON:** the matching middle folder ([source
   layout](#source-layout)), unit-tested in `tests/cli/`.
4. **Running it:** `frontends/cli/console/<area>.cpp`.
5. **Python:** a wrapper in `frontends/python/reagentlive_session.py`, or
   its row in [result shapes](commands/results.md) says why it has none.
6. **Docs:** the command's page under `docs/commands/`, its row in
   [result shapes](commands/results.md), and its line in the
   [INDEX](INDEX.md#commands) lookup table.

## Source layout

`frontends/cli/` is grouped by layer. Each folder has one rule, and
`tests/python/test_cli_layout.py` (ctest `reagentlive.cli-layout`) reads the
`#include` lines and fails on a broken one:

| Folder | Holds | Rule |
|---|---|---|
| `syntax/` | `reagentlive::cli`: the parsed `Command`, `Parse` (`parse.h`), tokens, layouts, byte patterns (`pattern.h`) | includes only `syntax/`, `core/` and `ue/` client headers; no nlohmann/json, no decoder, no I/O (the loopback tests build it without JSON) |
| `values/` | how a value prints: `format.h`, `json_output.h`, `layout_json.h` | pure; never `console/` |
| `state/` | files that outlive a run: `cache.h`, `names.h`, `native_names.h` | pure; never `console/` |
| `results/` | result builders: `result_json.h`, `census.h`, `call_text.h`, `labels.h` | pure; never `console/` |
| `code/` | the fast lane and native objects: decoder, unwind, image, xrefs, touches, `sig` checker | pure; never `console/` |
| `console/` | `reagentlive::console`: the only folder with I/O (agent, files, stdout) | `internal.h` is for `console/` only |

- The middle folders (`values/` to `code/`) may include each other and
  `syntax/`. Their headers are unit-tested in `tests/cli/`.
- Hosts include only `console/console.h` and `syntax/parse.h`; a host lists
  its sources in `REAGENTLIVE_HOST_SOURCES` so the layout test checks them.
- `code/` is the one feature-shaped folder: the decoders form a closed group
  with their own dependency (Zydis). Why layers and not feature folders:
  [design](design.md#source-layout).
- A new area gets `syntax/parse_<area>.cpp` and `console/<area>.cpp`. It
  gets a new middle folder only once it has three headers of its own;
  until then they go in the closest one.
- Namespaces do not follow folders (`reagentlive::cli`, `cli::detail`,
  `cli::sig`): folders are for finding files.

Within a folder, files are split by subject so a change to one command loads
one file:

| Files | Holds |
|---|---|
| `console/console.cpp` | help, the `Run` dispatcher, sessions, `batch`, the cache file, `RunConsole` |
| `console/output.cpp` | JSON records, errors, `--timing`, batch capture |
| `console/memory.cpp` | `status`, read decoding, `pin`, `watch` (blocking and detached) |
| `console/members.cpp`, `state/native_names.h` | native members: `label`, the native file and its checks |
| `syntax/layout.h`, `values/layout_json.h` | typed layouts (`read --as "<layout>"`, native member types) |
| `console/dump.cpp` | field tables, struct layouts, `fields`, `dump` |
| `console/code.cpp` | `disasm`, `xrefs`, `touches` and function bounds (the fast lane) |
| `console/sig.cpp` | `sig`: byte signatures against the live image |
| `console/native.cpp` | `vtable` and `region` (native objects) |
| `console/labels.cpp` | `export-labels` |
| `console/capture.cpp` | snapshots, `snap`, `record`, replay |
| `console/calls.cpp` | `func`, `params`, typed calls |
| `console/query.cpp` | class lookup, `each`, `get` |
| `syntax/command.h`, `syntax/text.h` | the parsed `Command` and its limits; token, number, ref and address parsing |
| `syntax/parse.cpp` | `Parse` (declared in `parse.h`): help, status, quit, `batch`, then the areas below |
| `syntax/parse_memory.cpp`, `parse_unreal.cpp`, `parse_code.cpp` | the verbs of each area; each answers nothing for a verb not its own |
| `ue/agent_ue.cpp` | object lookup (`find`, `describe`, object reads, array previews, `name`) and the shared helpers with their caches |
| `agent_ue_reflect.cpp` | `call`, `func`, `funcs`, `fields` and parameters |
| `agent_ue_scan.cpp` | the cached table and pool scans: `census`, `name-find`, `each` |

- Shared helpers live in a `detail` namespace (console) or the pack's
  namespace, declared in an internal header (`console/internal.h`,
  `agent_ue_internal.h`); a helper used by one file stays anonymous there.
- Agent files keep the mapped-image limits: constant-initialised globals,
  each defined in one file and declared `extern` in the internal header.
- New files join `target_sources`; hosts need no change.
- No file past 1000 lines: split by subject before it gets there.
- The console keeps no file-level state: everything one run keeps (refs,
  names, field tables, class refs, modules, output capture and timing) is a
  member of `Session` (`console/internal.h`), passed to every command, so
  each file can be tested alone.

## Tests

The test framework lives in `third_party/doctest` (see its `SOURCE.json`).
CMake fetches a pinned nlohmann/json release for the console and CLI tests
when the parent project has not already provided the target.

```powershell
cmake -S . -B build -G Ninja   # from the ReAgentLive folder; tests on by default
cmake --build build
ctest --test-dir build
```

A host that adds reagentlive with `add_subdirectory` sets
`REAGENTLIVE_BUILD_TESTS ON` to run them with its own tests (label
`reagentlive`).

| Suite | Covers |
|---|---|
| `reagentlive-loopback-tests` (`tests/loopback/`) | the real agent ticking on a thread against a real client in one process, with a test `Host` (shared agent in `support.h`, cases by area): attach, busy and layout refusal, build stamp, wait notices, op list, reads, writes and expected-bytes writes, refusals, guarded faults, modules, search pages, filters and the background worker, pins, trials, watch rows and guards, multi-tick handlers, unsupported ops, timeouts and recovery, response ticks, held requests and hold budgets, trip counts and round-trip time |
| `reagentlive-cli-tests` (`tests/cli/`) | the parser, value formatting, the state files (cache, names), result shapes, signatures and the fast lane |
| `reagentlive-ue-tests` (`tests/ue/`) | the Unreal pack's request bounds, ref packing, op ranges, the name-pool walk, the plain rule, kept results, container layouts, script scans; its own executable with no CLI headers |
| `reagentlive.python-session`, `reagentlive.ida-labels` (`tests/python/`) | the Python session client against a scripted fake console, and the IDA label importer |
| `reagentlive-example-tests` (`tests/example/`) | the own-thread host DLL loaded into the test process: attach by pid, `own` thread kind, reads, expected-bytes writes, a pin that expires, no engine pack |
| `reagentlive.consumer` (`tests/consumer/`) | an agent-only host project built in its own tree: no downloads with the console off, and nothing past `src/` reachable through the include path |
| `reagentlive.cli-layout` (`tests/python/`) | the `frontends/cli` layering rules, from `#include` lines |
| `reagentlive.docs` (`tests/python/`) | the docs: every relative link and anchor resolves, and no host or game name appears in the library |

The Unreal agent handlers need a live object table, so hosts check them in
their own live sessions.

## Code rules

- Agent side: no static TLS, no dynamic initialisation: command tables are
  `constexpr` arrays, state is constant-initialised or heap-allocated on the
  first `Tick`. Client side has no such limits.
- No exceptions across the API; fixed-size buffers; no allocation at load.
- Large buffers on the heap, not as globals (mapped images merge `.bss`).
- Windows x64, C++23.
- The library never includes host code and never names a host or a game.

## Docs

Docs say what code cannot: behaviour, decisions and their rejected
alternatives, constraints, evidence. Each fact has one home; others link
to it.

| Fact | Home |
|---|---|
| What a command does, its guards and syntax | its page under `commands/` |
| Result keys | [commands/results.md](commands/results.md) (pinned by `tests/cli/result_tests.cpp`) |
| Wire format, op numbers, statuses, limits | [protocol.md](protocol.md) |
| What a host provides and how it ticks | [hosting.md](hosting.md) |
| Client library and Python client | [client.md](client.md) |
| Why it is built this way, what was rejected | [design.md](design.md) |
| What may come next, and what will not | [ideas.md](ideas.md) |
| How to describe it | [positioning.md](positioning.md) |
| Where code goes, tests, these rules | this page |
| How an AI agent investigates: method, pitfalls, lessons | the [`reagentlive` skill](../.claude/skills/reagentlive/SKILL.md) (its own rules for lessons) |

- Pages describe the current state. Rewrite stale text; no dated
  "update" sections and no history beyond a rejected alternative.
- No counts that rot (test cases, file sizes). Measured evidence is fine
  when it says what was measured.
- A new page gets a line in [INDEX](INDEX.md) (the docs test checks).
  Split a page by subject once it covers two or passes about 500 lines.
- Plans are their own file, deleted when they ship: the decision moves to
  design, the behaviour to its page.
- Examples use placeholders (`game.exe`, `mygame-dev-1234`) and stock
  engine classes, never one game's names.
