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
| Python, MCP, IDA | session client, the generator of typed functions, the MCP adapter over them; label importer | `frontends/python/`, `frontends/mcp/`, `frontends/ida/` | the console's JSON calls and `help --json` (MCP: the MCP Python SDK), its label file |
| Host glue | section name, checks, logging, command table, where `Tick` runs, optional roots for `watch --follow` | the host's code ([hosting](hosting.md)) | core, packs |

Dependencies point one way: host → pack → core. Frontends use the client API,
never the section directly. ReAgentLive never includes host code.

## Adding a command

1. **Agent op** (only when the console cannot build it from existing ops):
   the handler in `core/` or the pack, its op number in the right range
   ([protocol](protocol.md#ops)), a loopback or pack test, and the op's row
   in [protocol](protocol.md).
2. **Its result:** a struct with `Describe` (`results/schema.h`) and its
   builder in the matching middle folder ([source layout](#source-layout)),
   unit-tested in `tests/cli/`. A list is `rows`; follow the conventions
   at the top of `results/result_json.h`. One verb has one result type: a
   mode with another shape is another verb.
3. **Its row** in the verb table (`results/verbs.h`): name, summary,
   positionals (key, usage name, arity, type, help, default) and flags
   (name, takes, type, placeholder, help, default), the result type, the
   parser, `writes` when it writes the target or calls into it, `blocks`
   when it holds the console for seconds. Types come from the vocabulary
   in `syntax/verb.h`; a struct several results share names itself with
   `TypeName` (a pack's with its prefix).
4. **Its parser:** `frontends/cli/syntax/parse_<area>.cpp`, declared in
   `parse_internal.h`. It gets the arguments already sorted by the row
   (`Args`), from a typed line or a JSON call alike; unknown flags or keys,
   missing values, arity and `int` and enum values are refused before it
   runs. Parse tests.
5. **Running it:** `frontends/cli/console/<area>.cpp`, ending in `Emit`
   with the result struct.
   A pack command puts steps 2 to 5 in the layers' `ue/` folders (its row
   in `results/ue/verbs_ue.h`), and `PackCommand` (`console/ue/pack.cpp`)
   claims it.
6. **Python and MCP:** nothing to write. The generated module
   (`reagentlive_gen.py`) and the MCP tools read the row; regenerate a
   module you keep.
7. **Docs:** the command's page under `docs/commands/` and its line in the
   [INDEX](INDEX.md#commands) lookup table. Regenerate
   [result shapes](commands/results.md):
   `reagentlive-table --write docs/commands/results.md` (the
   `reagentlive.results-doc` test fails while it is stale).

## Source layout

`frontends/cli/` is grouped by layer, and each layer is split into the
core and the Unreal pack's part. `tests/python/test_cli_layout.py` (ctest
`reagentlive.cli-layout`) reads the `#include` lines and fails on a broken
rule.

| Folder | Holds | Rule |
|---|---|---|
| `syntax/` | `reagentlive::cli`: the parsed `Command`, `Parse` (`parse.h`), tokens, layouts, byte patterns (`pattern.h`) | includes only `syntax/` and `core/`; no nlohmann/json, no decoder, no I/O (the loopback tests build it without JSON) |
| `values/` | how a value prints: `format.h`, `json.h`, `layout_json.h` | pure; never `console/` |
| `state/` | files that outlive a run: `cache.h`, `names.h`, `usage.h` (the usage log's line and trim) | pure; never `console/` |
| `results/` | result builders: `result_json.h`, `labels.h`, `stats.h` | pure; never `console/` |
| `code/` | the fast lane and native objects: decoder, unwind, image, xrefs, touches, `sig` checker | pure; never `console/` |
| `console/` | `reagentlive::console`: the only folder with I/O (agent, files, stdout) | `internal.h` and `pack.h` are for `console/` only |

**The pack boundary.** Each layer keeps the Unreal pack's part in its own
`ue/` folder (`syntax/ue/`, `values/ue/`, `state/ue/`, `results/ue/`,
`console/ue/`), under the same layer rule. Outside a `ue/` folder no file
includes `reagentlive/ue/` or a `ue/` header, or names `ue::`. The core
console reaches the pack only through `console/pack.h`, which
`console/ue/pack.cpp` implements (and `console/no_pack.cpp`, empty, for a
console built without the pack): the pack's state for a run, its cache
block, its rows of the verb table and `status` line, which commands are its own, and what core
commands borrow (object guards, the trial owner check, `watch --then`,
`peek`'s objects and names). `console/ue/internal_ue.h` is for
`console/ue/` only. Why seams and not a pack interface:
[design](design.md#source-layout).

- The middle folders (`values/` to `code/`) may include each other and
  `syntax/`. Their headers are unit-tested in `tests/cli/`.
- Hosts include only `console/console.h`, `syntax/parse.h`,
  `syntax/verb.h` (a row for each verb of their own) and
  `results/schema.h` (typed results); a host lists its sources in
  `REAGENTLIVE_HOST_SOURCES` so the layout test checks them.
- `code/` is the one feature-shaped folder: the decoders form a closed group
  with their own dependency (Zydis). Why layers and not feature folders:
  [design](design.md#source-layout).
- A new area gets `syntax/parse_<area>.cpp` and `console/<area>.cpp` (in
  the `ue/` folders when it belongs to the pack). It gets a new middle
  folder only once it has three headers of its own; until then they go in
  the closest one.
- Namespaces do not follow folders (`reagentlive::cli`, `cli::detail`,
  `cli::sig`): folders are for finding files.

Within a folder, files are split by subject so a change to one command loads
one file:

| Files | Holds |
|---|---|
| `syntax/verb.h`, `results/verbs.h`, `results/ue/verbs_ue.h` | the verb table: the row type with the tokeniser and usage, the core's rows, the pack's rows |
| `results/schema.h`, `results/docs.h` | typed result structs (one field list writes and describes the JSON), and the results page generated from the table |
| `console/console.cpp` | `help`, `stats` and the usage log's lines, the `Run` dispatcher for core ops, sessions, `batch`, the cache file, `RunConsole` and its no-target mode |
| `console/output.cpp` | JSON records, the check of a result against its row, errors, `--timing`, batch capture, each line's output size for the usage log |
| `console/memory.cpp` | `status`, read decoding, `pin`, trials, `watch` (blocking and detached), `peek` |
| `console/code.cpp` | `disasm`, `xrefs`, `touches` and function bounds (the fast lane) |
| `console/sig.cpp` | `sig-check`, `sig-file`, `sig-bytes`: byte signatures against the live image |
| `console/native.cpp` | `vtable` and `region` (native objects) |
| `console/pack.h`, `console/ue/pack.cpp` | the seam: what the core asks of the pack, and the pack's dispatcher (`find`, `call`, `census`...) |
| `console/ue/members.cpp`, `state/ue/native_names.h` | native members: `label`, the native file and its checks |
| `syntax/layout.h`, `values/layout_json.h` | typed layouts (`read --type "<layout>"`, native member types) |
| `console/ue/dump.cpp` | field tables, struct layouts, `fields`, `dump` |
| `console/ue/labels.cpp` | `export-labels` |
| `console/ue/capture.cpp` | snapshots, `snap`, `record`, replay |
| `console/ue/calls.cpp` | `func`, `params`, typed calls |
| `console/ue/query.cpp` | class lookup, `each`, `get`, `entries` |
| `state/ue/cache_ue.h`, `values/ue/format_ue.h`, `values/ue/json_output.h`, `results/ue/` | the pack's cache block, property values as text and JSON, its result builders |
| `syntax/command.h`, `syntax/text.h` | the parsed `Command` and its limits; token, number, ref and address parsing |
| `syntax/parse.cpp` | `Parse` (declared in `parse.h`): help, status, quit, `batch`, then the areas below |
| `syntax/parse_memory.cpp`, `parse_code.cpp`, `ue/parse_unreal.cpp` | the verbs of each area; each answers nothing for a verb not its own |
| `ue/agent_ue.cpp` | object lookup (`find`, `describe`, object reads, array previews, `name`) and the shared helpers with their caches |
| `agent_ue_reflect.cpp` | `call`, `func`, `funcs`, `fields` and parameters |
| `agent_ue_scan.cpp` | the cached table and pool scans: `census`, `name-find`, `each` |

- Shared helpers live in a `detail` namespace (console) or the pack's
  namespace, declared in an internal header (`console/internal.h`,
  `console/ue/internal_ue.h`, `agent_ue_internal.h`); a helper used by one
  file stays anonymous there.
- Agent files keep the mapped-image limits: constant-initialised globals,
  each defined in one file and declared `extern` in the internal header.
- New files join `target_sources`; hosts need no change.
- No file past 1000 lines: split by subject before it gets there.
- The console keeps no file-level state: everything one run keeps (refs,
  names, modules, output capture and timing) is a member of `Session`
  (`console/internal.h`), and the pack's (field tables, class refs, census,
  snapshots) of its `PackState` (`console/ue/internal_ue.h`), passed to
  every command, so each file can be tested alone.

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
| `reagentlive-cli-tests` (`tests/cli/`) | the verb table (every row, the tokeniser, usage errors, help), the parsers, typed results (each checked against its own type), value formatting, the state files (cache, names), signatures and the fast lane |
| `reagentlive.results-doc` (`tests/table/`) | [result shapes](commands/results.md) is what the table generates; `reagentlive-table` also prints `help --json` without a console |
| `reagentlive-ue-tests` (`tests/ue/`) | the Unreal pack's request bounds, ref packing, op ranges, the name-pool walk, the plain rule, kept results, container layouts, script scans; its own executable with no CLI headers |
| `reagentlive.python-session`, `reagentlive.python-mcp`, `reagentlive.ida-labels` (`tests/python/`) | the session client and a module generated from the real table, against a scripted fake console (`fake_console.py`) that serves it (strict results, a method per verb, JSON calls, canned replies checked against their types); the MCP tools made from that module, through an in-memory client (skipped without the MCP SDK); and the IDA label importer |
| `reagentlive-example-tests` (`tests/example/`) | the own-thread host DLL loaded into the test process: attach by pid, `own` thread kind, reads, expected-bytes writes, a pin that expires, no engine pack |
| `reagentlive.consumer` (`tests/consumer/`) | an agent-only host project built in its own tree: no downloads with the console off, and nothing past `src/` reachable through the include path |
| `reagentlive.core-console` (`tests/python/`) | the console built without the pack (`reagentlive-core-console`, so the core console compiles alone): its `help --json` has core rows only, none marked `needsPack`, each the same as in the full table, and the module generated from it no pack verb |
| `reagentlive.cli-layout` (`tests/python/`) | the `frontends/cli` layering rules and the pack boundary, from `#include` lines, `ue::` names and the pack's id as a string; also `core/` never including the pack and the generator and the MCP adapter never naming it |
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
| What a command takes and its result keys | the verb table in code; [commands/results.md](commands/results.md) is generated from it, never edited |
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
