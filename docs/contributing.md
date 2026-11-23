# Contributing

*Home of:* where code goes (layers, source layout, adding a command or a pack), the tests, the code rules, and where each fact is written down (the docs and skill rules).

**TL;DR:** Read this before adding a command, a file or a doc. Code goes
by layer, every command starts as a row in the verb table, and the tests
check the layering and the docs. Each fact has exactly one home: look it
up in [where each fact goes](#where-each-fact-goes) before writing it, and
link instead of repeating it.

## Layers

The code lives in `src/reagentlive/`, and only `src/` is on the include
path (`#include "reagentlive/core/agent.h"`). Code paths in these docs
(`core/`, `packs/ue/`, `frontends/`) are relative to `src/reagentlive/`;
`tests/`, `examples/` and `docs/` sit at the top.

| Layer | What | Where | Depends on |
|---|---|---|---|
| Core agent | wire protocol, shared section, tick and dispatch, `read`/`write`/`modules`/`search` (and its job)/`xrefs`/`region`/`pin`/`watch` | `core/agent.*`, `core/agent_search.cpp`, `core/protocol.h` | Windows, standard library |
| Core client | typed C++ API: attach, read, write, modules, search, xrefs, region, pointer chains, supported ops, raw `Send` | `core/client.*` | protocol |
| Unreal pack | agent handlers for lookup, reflection, guarded object reads and calls, every one with no SDK (`ue-base`, layouts through `engine_layout.h`); the opt-in game-thread tick for hosts (`host_draw.*`, `ue-draw`) | `packs/ue/` | core; only `reagentlive::ue` also the host's `SDK/` headers (`engine_layout_sdk.h`, `agent_ue_globals.cpp`) |
| CLI | command parsing and JSON Lines over the client API | `frontends/cli/` (by layer: [source layout](#source-layout)) | client, Unreal client calls |
| Python, MCP, IDA | session client, the generator of typed functions, the MCP adapter (tools from the table's rows); label importer | `frontends/python/`, `frontends/mcp/`, `frontends/ida/` | the console's JSON calls and `help` with `json` (MCP: the MCP Python SDK), its label file |
| Host glue | section name, checks, logging, command table, where `Tick` runs, optional roots for `watch-start`'s `follow` | the host's code ([hosting](hosting.md)) | core, packs |

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
3. **Its row** in the verb table (`results/verbs.h`): name, origin, its
   [tool pack](tool-interface.md#tool-packs) (`modify` when it writes or
   calls; a new verb outside `modify` also gets its forms in the
   `call_tests.cpp` forms test), summary,
   args (key, type, help, default, required, list, and an example where
   the type's own would be refused), the result type, the builder,
   `writes` when it writes the target or calls into it, `blocks` when it
   holds the console for seconds. Types come from the vocabulary in
   `call/verb.h`; a struct several results share names itself with
   `TypeName` (a pack's with its prefix).
4. **Its builder:** `frontends/cli/call/build_<area>.cpp`, declared in
   `build_internal.h`. It gets the args already checked against the row
   (`Args`, `call/check.h`): unknown and missing keys and wrong types are
   refused before it runs. It reads the typed values (`call/values.h`),
   checks ranges and combinations, and starts an error about one key
   with that key, so the refusal names it and its example drops or
   replaces it. Call tests.
5. **Running it:** `frontends/cli/console/<area>.cpp`, ending in `Emit`
   with the result struct.
   A pack command puts steps 2 to 5 in its pack's `packs/<id>/cli/<layer>/` folders (its row
   in `packs/ue/cli/results/verbs_ue.h`), and `PackCommand` (`packs/ue/cli/console/pack.cpp`)
   claims it.
6. **Python and MCP:** nothing to write. The generated module
   (`reagentlive_gen.py`) and the MCP tools read the row; regenerate a
   module you keep. A new argument or result type also needs its entry in
   both files' type maps (the Python tests fail until it has one).
7. **Docs:** the row is the only place the verb's name, pack, args (with
   their help), notes and example are written. Regenerate
   [result shapes](commands/results.md) and the
   [verb reference](commands/reference.md):
   `reagentlive-table --write docs/commands/results.md` and
   `reagentlive-table --write-reference docs/commands/reference.md`
   (`reagentlive.results-doc` and `reagentlive.reference-doc` fail while
   either is stale). The command's page under `docs/commands/` keeps its
   rationale, workflows and gotchas and links the reference; its line goes
   in the [INDEX](INDEX.md#commands) lookup table. A new error kind joins
   `ErrorKinds` (`call/refusals.h`), never a string at the call site.

## Source layout

`frontends/cli/` is grouped by layer, and each layer is split into the
core and the Unreal pack's part. `tests/python/test_cli_layout.py` (ctest
`reagentlive.cli-layout`) reads the `#include` lines and fails on a broken
rule.

| Folder | Holds | Rule |
|---|---|---|
| `call/` | `reagentlive::cli`: the verb table's row (`verb.h`), a call's checks (`check.h`), its typed values (`values.h`), `Build` (`build.h`) and the built `Command`, layouts, byte patterns (`pattern.h`) | includes only `call/` and `core/` (and nlohmann/json: a call is JSON); no decoder, no I/O |
| `values/` | how a value prints: `format.h`, `json.h`, `layout_json.h` | pure; never `console/` |
| `state/` | files that outlive a run: `cache.h`, `names.h`, `usage.h` (the usage log's line and trim) | pure; never `console/` |
| `results/` | result builders: `result_json.h`, `labels.h`, `stats.h`, `budget.h` | pure; never `console/` |
| `code/` | the fast lane and native objects: decoder, unwind, image, xrefs, touches, `sig` checker | pure; never `console/` |
| `console/` | `reagentlive::console`: the only folder with I/O (agent, files, stdout) | `internal.h` and `pack.h` are for `console/` only |

**The pack boundary.** Each engine pack lives in `packs/<id>/`: its agent
and client files at the top, its `pack.cmake`, and its part of each layer
in `cli/<layer>/` (`packs/ue/cli/call/` to `packs/ue/cli/console/`),
under that layer's rule. Layers in `frontends/cli/` hold files only.
Outside `packs/` no file includes `reagentlive/packs/`, names `ue::` or
spells the id `"ue"`, and no pack includes another. The core
console reaches the pack only through `console/pack.h`, which
`packs/ue/cli/console/pack.cpp` implements (and `console/no_pack.cpp`, empty, for a
console built without the pack): the pack's state for a run, its cache
block, its rows of the verb table and `status` line, which commands are its own, and what core
commands borrow (object guards, the owner check of `write`'s `undo`, `watch-start`'s field ranges,
`peek`'s objects and names). `packs/ue/cli/console/internal_ue.h` is for
`packs/ue/cli/console/` only. Why seams and not a pack interface:
[design](design/architecture.md#source-layout).

**Adding a pack** (id `<id>`):

1. `packs/<id>/` with its agent and client files, and `cli/<layer>/` for
   its part of each layer.
2. `packs/<id>/pack.cmake`: its agent and client targets (the agent one
   appended to `REAGENTLIVE_AGENT_TARGETS`) and the CLI lists the root
   reads, `reagentlive_pack_call_sources`,
   `reagentlive_pack_console_sources`, `reagentlive_pack_console_link`.
   Copy `packs/ue/pack.cmake`.
3. The next op slot: `op::PackSlot(n)` in its protocol header with a
   `static_assert` on its last op, and a row in
   [protocol](protocol.md#ops).
4. Implement `console/pack.h` in `cli/console/pack.cpp`.
5. Configure with `-DREAGENTLIVE_PACKS=<id>`. A console carries one pack;
   the second pack turns `pack.h` into a table first
   ([design](design/architecture.md#source-layout)).

- The middle folders (`values/` to `code/`) may include each other and
  `call/`. Their headers are unit-tested in `tests/cli/`.
- Hosts include only the four headers [hosting](hosting.md#cmake-targets)
  lists.
- `code/` is the one feature-shaped folder: the decoders form a closed group
  with their own dependency (Zydis). Why layers and not feature folders:
  [design](design/architecture.md#source-layout).
- A new area gets `call/build_<area>.cpp` and `console/<area>.cpp` (in
  `packs/<id>/cli/` when it belongs to a pack). It gets a new middle
  folder only once it has three headers of its own; until then they go in
  the closest one.
- Namespaces do not follow folders (`reagentlive::cli`, `cli::detail`,
  `cli::sig`): folders are for finding files.

Within a folder, files are split by subject so a change to one command loads
one file; each file's top comment says what it holds.

- Shared helpers live in a `detail` namespace (console) or the pack's
  namespace, declared in an internal header (`console/internal.h`,
  `packs/ue/cli/console/internal_ue.h`, `agent_ue_base.h` and
  `agent_ue_internal.h` for the agent files); a helper used by one file
  stays anonymous there. No agent file includes `SDK/` except
  `agent_ue_globals.cpp` under `REAGENTLIVE_UE_SDK` and
  `engine_layout_sdk.h`: the others read objects through `EngineLayout`.
  Engine offsets are defined only in their homes (`reagentlive.engine-layout`).
- Agent files keep the mapped-image limits: constant-initialised globals,
  each defined in one file and declared `extern` in the internal header.
- New files join `target_sources`; hosts need no change.
- No file past 1000 lines: split by subject before it gets there.
- The console keeps no file-level state: everything one run keeps (refs,
  names, modules, output capture and timing) is a member of `Session`
  (`console/internal.h`), and the pack's (field tables, class refs, census,
  snapshots) of its `PackState` (`packs/ue/cli/console/internal_ue.h`), passed to
  every command, so each file can be tested alone.

## Tests

CMake fetches the test framework (doctest, a pinned release archive: its
header and test discovery, none of its targets) and, for the console and
CLI tests, a pinned nlohmann/json release when the parent project has not
already provided the target.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # from the ReAgentLive folder; tests on by default
cmake --build build
ctest --test-dir build
```

A host that adds reagentlive with `add_subdirectory` sets
`REAGENTLIVE_BUILD_TESTS ON` to run them with its own tests (label
`reagentlive`).

| Suite | Covers |
|---|---|
| `reagentlive-loopback-tests` (`tests/loopback/`) | the real agent ticking on a thread against a real client in one process, with a test `Host` (shared agent in `support.h`, cases by area): attach, busy and layout refusal, build stamp, wait notices, op list, reads, writes and expected-bytes writes, refusals, guarded faults, modules, search pages, filters, the background worker and search jobs, pins, trials, watch rows and guards, multi-tick handlers, unsupported ops, timeouts and recovery, response ticks, held requests and hold budgets, trip counts and round-trip time |
| `reagentlive-console-tests` (`tests/console/`) | the real console against a fake engine (`fake_engine.h`): an in-process agent whose Unreal pack ops answer from a synthetic world in test memory (types with fields, objects with bytes, an FName pool), core reads real. One fixture read through `get` (paths, and every field with values and previews), `each` and `entries` must print the same JSON, with and without `view.nonzero` and `meta`; status keys (`unnamed`, digested or not) and stale object arguments (`object` and `outer`) must agree. Add a case here when a verb gains a value or an object argument |
| `reagentlive-cli-tests` (`tests/cli/`) | the verb table (every row, its args schema, schema refusals and their examples, help), the builders and typed values, typed results (each checked against its own type), value formatting, the state files (cache, names), signatures and the fast lane |
| `reagentlive.results-doc`, `reagentlive.reference-doc` (`tests/table/`) | [result shapes](commands/results.md) and the [verb reference](commands/reference.md) are what the table generates; `reagentlive-table` also prints `help` with `json` without a console |
| `reagentlive.verb-names`, `reagentlive.verb-names-check` (`tests/python/`) | every verb and arg the docs and the skill name (JSON calls, plan steps, Python calls, MCP tool names, key paths such as `view.limit`) is in the table and no code span still writes the removed text grammar (`check_verb_names.py`; a host runs it on its own docs with its console), and the checker's own cases |
| `reagentlive-ue-tests` (`tests/ue/`) | the Unreal pack's request bounds, ref packing, op ranges, the name-pool walk, the class index, the plain rule, kept results, container layouts, script scans; its own executable with no CLI headers |
| `reagentlive.python-session`, `reagentlive.python-mcp`, `reagentlive.ida-labels`, `reagentlive.ida-function`, `reagentlive.ida-types`, `reagentlive.ida-fetch` (`tests/python/`) | the session client and a module generated from the real table, against a scripted fake console (`fake_console.py`) that serves it (strict results, a method per verb, JSON calls, canned replies checked against their types); the MCP tools made from that table, through an in-memory client (skipped without the MCP SDK); and the IDA loader's planning without IDA: labels, saves, types, and `fetch` over a stand-in console script with the IDA steps stubbed |
| `reagentlive-example-tests` (`tests/example/`) | the own-thread host DLL loaded into the test process: attach by pid, `own` thread kind, reads, expected-bytes writes, a pin that expires, no engine pack; with the Unreal pack, the Unreal example's DLL the same way (`unreal_tests.cpp`, no engine: the `ue` pack listed, `game-thread` answering) |
| `reagentlive.consumer` (`tests/consumer/`) | host projects built in their own tree: an agent-only host (no downloads with the console off, nothing past `src/` reachable through the include path) and an Unreal host with no SDK (`host_ue_base.cpp`: `ue-base` builds with no `SDK/` on the path) |
| `reagentlive.engine-layout` (`tests/python/`) | engine offsets defined only in their homes (`engine_layout.h`, `engine_layout_sdk.h`, `globals.h`, `name_pool.h`) |
| `reagentlive.core-console` (`tests/python/`) | the console built without the pack (`reagentlive-core-console`, so the core console compiles alone): its `help` with `json` has every core row and no other, each the same as in the full table, and the module generated from it no pack verb |
| `reagentlive.cli-layout` (`tests/python/`) | the `frontends/cli` layering rules and the pack boundary, from `#include` lines, `ue::` names and the pack's id as a string; also `core/` never including the pack and the generator and the MCP adapter never naming it |
| `reagentlive.docs` (`tests/python/`) | the docs: every relative link and anchor resolves, INDEX lists every page, every listed page has its header (**TL;DR**, and *Home of* on SSOT pages), the skill's `SKILL.md` stays within its line budget and links every reference, `AGENTS.md` stays within its own, and no host or game name appears in the library |

The Unreal agent handlers need a live object table, so hosts check them in
their own live sessions; the Unreal example's DLL is tested in-process,
with no engine, for what needs none.

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

**Docs and skill, one job each.** The docs say *what it is and how it
works* (command behaviour, protocol, hosting, design, contributing), for
people who host, extend or need the exact reference. The
[`reagentlive` skill](../.claude/skills/reagentlive/SKILL.md) says *how
to use it well* (method, order, judgement, approvals, pitfalls), for the
AI agent investigating. The skill never restates a command's behaviour (it
links `help` or the docs page); the docs give no investigation advice
beyond a command's guards. The skill keeps its own maintenance rules.

**Agent procedures, a third audience.** Pages under `docs/agents/` are
procedures an AI agent runs once on the user's machine (install, update),
fetched from a URL; the skill is method while investigating. They are
imperative numbered steps with exact commands and end with a report block;
their first line is one sentence for a human who lands there. They carry
no TL;DR and no *Home of* line; `docs/agents/README.md` routes between
them and restates nothing.

**Changing the repo, a fourth audience.** [`AGENTS.md`](../AGENTS.md) is
for an AI agent changing ReAgentLive (`CLAUDE.md` imports it): an
orchestrator of commands, hard rules and how to run large changes, linking
this page for the details. Imperative and dense, no TL;DR and no *Home of*
line, at most 120 lines (the docs test checks).

**Two kinds of page, never mixed.** *Orchestrators* route: one line of
what and why per topic, a link to its home, and when to read it; no fact
stated in full (`README.md`, `docs/INDEX.md`, `docs/agents/README.md`, the
example READMEs, the skill's `SKILL.md`). *SSOT pages* own facts: each
fact stated once, in full, in exactly one page.

### Where each fact goes

Check this table before adding a fact; if it has a home, link there.

| Fact | Home | Not here |
|---|---|---|
| The tier ladder, what stops each rung, which artifact gives it | [tiers.md](tiers.md) | README and skill: picture plus a link |
| Reading your tier from `status` (the full table) | [tiers.md](tiers.md#read-your-tier-from-status) | skill: its operational rule plus a link |
| What the agent does next at each tier (climb, approvals) | the skill's `SKILL.md` | docs |
| What each interface offers (MCP tools, console, Python, C++ client) and its API | [client.md](client.md) | README: one line each |
| Which interface to pick for a task; verb → MCP tool → Python function | the skill's `references/interfaces.md` | docs |
| A verb's name, pack, arguments, notes, example, result keys | the verb table in code (`help` with `verb`); [commands/reference.md](commands/reference.md) and [commands/results.md](commands/results.md) are generated from it, never edited | everything else |
| Error kinds and their message templates | `ErrorKinds` in `call/refusals.h` (listed in [the reference](commands/reference.md#error-kinds)) | handlers (they name an `ErrorKind`) |
| A verb's behaviour and guards | its page under `commands/` | skill (links only) |
| Which Unreal commands need the game thread | [commands/unreal.md](commands/unreal.md#off-the-engines-thread) | everywhere else (links) |
| Console mechanics: calls, refs, budgets (the default budget's size), cache, JSON Lines | [commands/console.md](commands/console.md) | client.md and skill (links) |
| One client at a time; an agent taking over on re-inject | [protocol.md](protocol.md#handshake), [protocol.md](protocol.md#section) | elsewhere (links) |
| The layout groups and their checks | [commands/unreal.md](commands/unreal.md#without-an-sdk) | design and skill (links) |
| A `get` snapshot's baseline across runs | [commands/capture.md](commands/capture.md) | console.md (link) |
| Labels files, their defaults and signatures | [commands/labels.md](commands/labels.md) | fast lane, IDA loader, skill (links) |
| The IDA loader's behaviour | [commands/ida-loader.md](commands/ida-loader.md) | skill: the loop's steps, plus links |
| Approval rules, evidence rules | the skill's `SKILL.md` | docs (links) |
| Investigation method, pitfalls, per-question advice | the skill's `references/` | docs |
| Hosting, CMake targets, the examples' internals, the host API | [hosting.md](hosting.md) | README (links) |
| Wire format, op numbers, statuses, limits | [protocol.md](protocol.md) | elsewhere |
| Why it is built this way, what was rejected | [design.md](design.md) and its pages under `design/` | elsewhere |
| Why the console, generated functions and MCP tools are shaped as they are | [tool-interface.md](tool-interface.md) | elsewhere |
| Engine layout offsets | `packs/ue/engine_layout.h` (code) and [unreal.md](commands/unreal.md#without-an-sdk) | skill (links) |
| What may come next, and what will not | [ideas.md](ideas.md) | elsewhere |
| How to describe it | [positioning.md](positioning.md) | README: the short pitch |
| Agent procedures (install, update) | [agents/](agents/README.md): [setup.md](agents/setup.md), [update.md](agents/update.md) | README: the prompts only |
| The setup record's fields | [agents/setup.md](agents/setup.md#10-write-the-setup-record) | the skill and agents/README.md read it, never restate it |
| When and how an agent reviews a session and proposes skill lessons (modes, scratch file, rubric) | the skill's `references/improving.md` | docs |
| The user's own skill lessons | the skill's `references/local-lessons.md` (git-ignored, the user's) | anything tracked |
| How an agent works on this repo: commands, hard rules, large changes | [`AGENTS.md`](../AGENTS.md) | this page keeps the details; `AGENTS.md` links them |
| Where code goes, tests, these rules | this page | |

The skill may be installed apart from the docs (a link into the clone, or
a copy), so it carries the minimum an agent needs before its first command
(the operational status rule, approval rules) and names every other fact
as a repository path (`docs/tiers.md#...`), resolved by one rule in
`SKILL.md`: the clone the skill folder lives in (through its link, a
submodule or an instructions line), else the setup record's clone, else
the console's commit on GitHub, else `main`. The docs own the full statements; the docs test checks every
path exists and that no `blob/main` URL is left in the skill.

**Page header.** Every page INDEX lists opens with its title, then (SSOT
pages only) a `*Home of:*` line naming the facts it owns, then a
`**TL;DR:**` of two to four plain sentences: what it is, when it matters,
the one thing not to get wrong. Orchestrators carry only the TL;DR;
`commands/results.md` and `commands/reference.md` (generated), `docs/agents/` pages and skill files
carry neither. The
TL;DR is for a person new to the project: plain words, jargon defined or
linked. The docs test checks both lines within a page's first 15 lines.

- Pages describe the current state. Rewrite stale text; no dated
  "update" sections and no history beyond a rejected alternative.
- No counts that rot (test cases, file sizes). Measured evidence is fine
  when it says what was measured.
- A new page gets a line in [INDEX](INDEX.md) (the docs test checks).
  Split a page by subject once it covers two or passes about 500 lines.
- Plans are their own file, deleted when they ship: the decision moves to
  design, the behaviour to its page.
- **CHANGELOG:** follow its [writing rules](../CHANGELOG.md#writing-entries):
  user-facing, grouped, and the exact protocol-change line that
  [the update procedure](agents/update.md#2-show-what-is-coming) searches for.
- Examples use placeholders (`game.exe`, `mygame-dev-1234`) and stock
  engine classes, never one game's names.
