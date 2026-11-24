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
   ([protocol](protocol.md#ops)), a loopback or pack test.
2. **Its result:** a struct with `Describe` (`results/schema.h`) and its
   builder in the matching middle folder ([source layout](#source-layout)),
   unit-tested in `tests/cli/`. A list is `rows`; follow the conventions
   at the top of `results/result_json.h`. One verb has one result type: a
   mode with another shape is another verb.
3. **Its row** in the verb table (`results/verbs.h`; a pack's in
   `packs/ue/cli/results/verbs_ue.h`): name, origin, its
   [tool pack](tool-interface.md#tool-packs) (`modify` when it writes or
   calls), summary, args, the result type, the builder, `writes` when it
   writes the target or calls into it, `blocks` when it holds the console
   for seconds. Types come from the vocabulary in `call/verb.h`.
4. **Its builder** (`frontends/cli/call/build_<area>.cpp`): it gets the
   args already checked against the row, reads the typed values, checks
   ranges and combinations, and starts an error about one key with that
   key. Call tests.
5. **Running it** (`frontends/cli/console/<area>.cpp`, ending in `Emit`
   with the result struct). A pack command puts steps 2 to 5 in its pack's
   `packs/<id>/cli/<layer>/` folders and `PackCommand` claims it.
6. **Python and MCP:** nothing to write; the generated module and the MCP
   tools read the row. A new argument or result type also needs its entry
   in both files' type maps (the Python tests fail until it has one).
7. **Docs:** the row is the only place the verb's name, args, notes and
   example are written. Regenerate [result shapes](commands/results.md) and
   the [verb reference](commands/reference.md) with the commands in
   [AGENTS.md](../AGENTS.md#build-and-test). The command's page under
   `docs/commands/` keeps its rationale, workflows and gotchas; its line
   goes in the [commands](commands/README.md) lookup table. A new error
   kind joins `ErrorKinds` (`call/refusals.h`), never a string at the call
   site.

## Source layout

`frontends/cli/` is grouped by layer, and each layer is split into the
core and the Unreal pack's part. `tests/python/test_cli_layout.py` (ctest
`reagentlive.cli-layout`) reads the `#include` lines and fails on a broken
rule.

| Folder | Holds | Rule |
|---|---|---|
| `call/` | `reagentlive::cli`: the verb row, a call's checks and typed values, `Build` and the built `Command` | includes only `call/` and `core/` (and nlohmann/json: a call is JSON); no decoder, no I/O |
| `values/` | how a value prints | pure; never `console/` |
| `state/` | files that outlive a run: cache, names, usage log | pure; never `console/` |
| `results/` | result builders | pure; never `console/` |
| `code/` | the fast lane and native objects: decoder, unwind, xrefs, signatures | pure; never `console/` |
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
   `static_assert` on its last op ([protocol](protocol.md#ops)).
4. Implement `console/pack.h` in `cli/console/pack.cpp`.
5. Configure with `-DREAGENTLIVE_PACKS=<id>`. A console carries one pack;
   the second pack turns `pack.h` into a table first
   ([design](design/architecture.md#source-layout)).

- The middle folders (`values/` to `code/`) may include each other and
  `call/`. Their headers are unit-tested in `tests/cli/`.
- Hosts include only the four headers [hosting](hosting.md#cmake-targets)
  names.
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

CMake fetches doctest and, for the console and CLI tests, nlohmann/json
when the parent has not provided it. Build and run as in
[AGENTS.md](../AGENTS.md#build-and-test) (Release). A host that adds
reagentlive with `add_subdirectory` sets `REAGENTLIVE_BUILD_TESTS ON` to run
them with its own tests (label `reagentlive`).

Suites, by folder (each test file's header says what it covers):

- `tests/loopback/`: the real agent ticking on a thread against a real client.
- `tests/console/`: the real console against a fake engine (`fake_engine.h`).
  **Add a case here when a verb gains a value or an object argument.**
- `tests/cli/`: the verb table, builders, typed values and results, state files.
- `tests/table/`, `tests/python/`: the generated docs match the table, every
  verb and JSON call the docs show is valid, the Python and MCP frontends,
  the IDA loader, layering and engine-offset rules, and the docs test.
- `tests/ue/`: the Unreal pack's bounds and layouts, with no CLI headers.
- `tests/example/`, `tests/consumer/`: the example DLLs loaded in-process,
  and host projects built in their own tree.

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
- `near` and `far` are Windows macros: never use them as names.
- A working copy may hold CRLF files (`.gitattributes` stores LF): edit
  them byte for byte, so a text tool does not rewrite every line ending.

## Docs

Docs say what code cannot: behaviour, decisions and their rejected
alternatives, constraints, evidence. Each fact has one home; others link
to it. **The code is the source of truth for structs, op numbers, limits,
option lists and API signatures: do not mirror them in docs.** Link the
header or `help`.

Four audiences, one style each (the table is in [AGENTS.md](../AGENTS.md)):

- **Docs:** *what it is and how it works*, for people who host, extend or
  need the reference.
- **The [`reagentlive` skill](../.claude/skills/reagentlive/SKILL.md):**
  *how to use it well* (method, order, approvals, pitfalls). It never
  restates a command's behaviour; the docs give no investigation advice
  beyond a command's guards. It keeps its own maintenance rules.
- **`docs/agents/`:** procedures an agent runs once (install, update):
  imperative numbered steps with exact commands, ending in a report block;
  a one-sentence first line for a human; no TL;DR, no *Home of*.
- **[`AGENTS.md`](../AGENTS.md)** (`CLAUDE.md` imports it): an orchestrator
  for an agent changing the repo, at most 120 lines (the docs test checks).

**Two kinds of page, never mixed.** *Orchestrators* route: one line of
what and why per topic, a link to its home, no fact stated in full (every
`README.md`, the skill's `SKILL.md`). *SSOT pages* own facts: each fact
stated once, in exactly one page.

### Where each fact goes

Check this table before adding a fact; if it has a home, link there.

| Fact | Home | Not here |
|---|---|---|
| The tier ladder, reading your tier from `status` | [tiers.md](tiers.md) | README and skill: a link |
| What the agent does next at each tier (climb, approvals, evidence, method, pitfalls) | the skill's `SKILL.md` and `references/` | docs |
| What each interface offers and its behaviour | [client.md](client.md) | README: one line each |
| Which interface to pick for a task; verb → MCP tool → Python function | the skill's `references/interfaces.md` | docs |
| A verb's name, pack, arguments, notes, example, result keys | the verb table in code (`help` with `verb`); [commands/reference.md](commands/reference.md) and [commands/results.md](commands/results.md) are generated from it, never edited | everything else |
| Error kinds and their message templates | `ErrorKinds` in `call/refusals.h` ([reference](commands/reference.md#error-kinds)) | handlers |
| A verb's behaviour and guards; console mechanics (calls, refs, result cap, cache); labels; the IDA loader; the layout groups and their checks; which Unreal commands need the game thread | its page under [commands/](commands/README.md) | skill and client.md (links only) |
| One client at a time; takeover on re-inject; the handshake | [protocol.md](protocol.md) | elsewhere (links) |
| Wire structs, op numbers, statuses, limits | `core/protocol.h`, `packs/ue/protocol_ue.h` | docs |
| Engine layout offsets | `packs/ue/engine_layout.h` | docs, skill |
| Hosting, CMake targets, the host API | [hosting.md](hosting.md) | README (links) |
| Why it is built this way, what was rejected | [design](design/README.md); the tool interface: [tool-interface.md](tool-interface.md) | elsewhere |
| What may come next, and what will not | [ideas.md](ideas.md) | elsewhere |
| How to describe it | [positioning.md](positioning.md) | README: the short pitch |
| Agent procedures (install, update), the setup record | [agents/](agents/README.md) | README: the prompts only |
| When and how an agent proposes skill lessons | the skill's `references/improving.md`; the user's lessons live outside the repo | anything tracked |
| Commands, hard rules, large changes for an agent changing this repo | [`AGENTS.md`](../AGENTS.md) | this page keeps the details |
| Where code goes, tests, these rules | this page | |

The skill may be loaded apart from the docs, so it carries the minimum an
agent needs before its first command and names every other fact as a
repository path (`docs/tiers.md#...`), resolved by one rule in `SKILL.md`.
The docs test checks every path exists and that no `blob/main` URL is left
in the skill.

**Page header.** Every page reachable from the docs README opens with its
title, then (SSOT pages only) a `*Home of:*` line naming the facts it owns,
then a `**TL;DR:**` of two to four plain sentences for a person new to the
project: what it is, when it matters, the one thing not to get wrong.
Orchestrators carry only the TL;DR; the generated pages, `docs/agents/`
pages and skill files carry neither. The docs test checks both lines within
a page's first 15 lines.

- Pages describe the current state. Rewrite stale text; no dated
  "update" sections and no history beyond a rejected alternative.
- No counts that rot (test cases, file sizes). Measured evidence is fine
  when it says what was measured.
- **A folder's `README.md` is its index:** it routes that folder's pages
  and states no fact in full. A new page gets a line in its folder's
  README; a new folder under `docs/` gets a README and a line in its
  parent's (the docs test checks).
  Split a page by subject once it covers two or passes about 500 lines.
- **CHANGELOG:** follow its [writing rules](../CHANGELOG.md#writing-entries).
- Examples use placeholders (`game.exe`, `mygame-dev-1234`) and stock
  engine classes, never one game's names.
