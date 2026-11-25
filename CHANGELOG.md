# Changelog

ReAgentLive follows semantic versioning. Before 1.0 the API and the wire
layout can change in any minor release; agent and client are always built
from the same revision, and the handshake's layout check refuses a
mismatched pair. The version lives in `src/reagentlive/version.h`. Releases
before 0.6.0 are one line each; git history has the detail.

## Writing entries

- **Write for the user, not the code.** Say what someone can now do, what
  got better or what broke for them, not how it was built.
- **One change, one or two lines.** Lead with the effect in bold; link the
  doc page instead of explaining.
- **Group under New, Improved, Fixed, Breaking.** Merge related changes. A
  feature reworked before release updates its entry; it gets no second one.
- **Leave out** refactors, tests, internal file moves, op numbers, struct
  sizes and fixes to bugs that never shipped.
- **Breaking entries say what to change** ("pass `--all`", "rename X to Y").
- **Protocol changes:** an entry that changes the wire protocol (an op, a
  struct or a constant in `core/protocol.h` or a pack's `protocol_*.h`)
  ends with the exact line `Protocol change: rebuild and re-inject.`
  [The update procedure](docs/agents/update.md#2-show-what-is-coming)
  searches for it.
- **A release opens with one sentence** on what it gives the user.

## Unreleased

Smaller, more uniform tool surface: about a third fewer MCP tool-list
tokens, one way to page, one count, one time unit.

### New

- **Wire your agent to any host project:** `tools/agent_link.py --console
  <exe> --dll <dll>` writes the project's MCP server entry, an `AGENTS.md`
  block and the skill links for the ReAgentLive submodule it carries, for
  a mod of your own or a copy of an example
  ([an agent in your project](docs/hosting.md#an-agent-in-your-project)).
  A project wired by hand keeps its own server (name and flags) and its
  own tracked skill folder. No `CLAUDE.md` is made: Claude Code reads
  `AGENTS.md`.
- **Build, check and update a project by script:** `tools/build_project.ps1
  -Check`; `tools/update.ps1` previews what is coming (and whether the
  host headers changed), `-Apply` moves, rebuilds and checks, `-Rollback`
  goes back ([update](docs/agents/update.md)).
- **Later projects ask only about themselves:** the injector, the IDA side
  and your improvement mode are set up once and kept in
  `%LOCALAPPDATA%\reagentlive\machine.json`
  ([setup](docs/agents/setup.md#1-check-the-machine)); the agent writes
  nothing else outside the project. The skill reads your improvement mode
  there, so the committed project record holds nothing personal.

### Improved

- **Smaller MCP tool list:** shorter verb help and refusals; `max_chars` and
  the `view` keys are described once in the server instructions; no
  `Example:` lines (`help` and refusals keep them).
- **`xrefs`, `touches` and `sig-bytes` take a pointer chain** as `addr`
  ([pointer chains](docs/commands/native-objects.md#pointer-chains)).
- **`find` pages reuse the first page's `total`** instead of recounting.
- **`tools/ida_setup.py --install` fails loudly** when a Microsoft Store
  Python redirected the loader where IDA does not look, instead of
  reporting it installed.
- **Codex and Cursor get the skill** through the project's
  `.agents/skills` link ([setup](docs/agents/setup.md#4-wire-the-agent));
  **a failed update rolls back** ([update](docs/agents/update.md#4-update)).

### Breaking

Setup and docs:

- **Setup puts ReAgentLive into a project; the machine-wide install is
  gone:** your mod, or a new folder started from an example, carries it as
  a submodule; no clone of its own, no user-level MCP entry or skill link,
  and the project record `reagentlive.json` replaces
  `%LOCALAPPDATA%\reagentlive\setup.json`.
  An install from before: ask your agent to run the
  [setup](docs/agents/setup.md) again; its last step retires the old one.
  Your lessons stay.
- **The docs index is `docs/README.md`** (was `docs/INDEX.md`): fix the
  `ReAgentLive docs:` line in your agent's instructions file. Commands
  are in `docs/commands/README.md`, design in `docs/design/`.

Verbs and args:

- **Renamed args:** `route` `outer` → `up`; `name-refs` `index` →
  `name_index`; `describe` `index` → `slot`; `disp-scan` `disps` →
  `offsets`; `assets-export` `cls` → `classes`; `each`/`call-each`
  `seconds` → `duration`; `disasm` `function` → `whole`, `follow` → `jumps`;
  `xrefs` `all` → `loose`.
- **`calls-start` takes `object` only** (instance or class); an instance
  records only its own calls. `release` `object` is a string like `call`'s.
- **`watch-stop` and `calls-stop` are gone:** pass `stop` to `watch-read`
  or `calls-read`.
- **`touches` `reads` → `access`** (`write`, `read`, `any`; default `any`),
  as `disp-scan`; `read` now counts read-modify-writes, not `lea`.
- **Time args are seconds as a number:** `duration` and `search` `wait` take
  fractions; `wait` must be above 0.
- **`usage` takes `scope`** (omitted: this session; `"sessions"`; `"all"`;
  a session id) instead of `report` and `session`; advice is
  `only: ["advice"]`.
- **`timed-writes` is in the `modify` pack** (was `observe`).
- **MCP output schemas are opt-in:** `--output-schemas on` (`compact` is
  gone; `off` is the default).

Results:

- **One cursor:** continue with a result's `next_cursor` as `view.cursor`
  (was `view.start`, `search` `scope.start`, `disasm` `next` + `resume`);
  `search` `scope.start` → `scope.from`. `capped` means a cut with no way
  to the rest, so `disp-scan` drops it.
- **One count, `total`** (after filters, before the row cut, every page):
  replaces `matched`, `omitted` and `filtered`; `get` and `script` drop it.
- **`from` is where a page began;** address keys are `scan_from`/`scan_to`
  (`search`, `search-job`, `disp-scan`, `touches`); `disasm` `followed`
  rows say `source`/`target`; `labels-port` `from` → `from_build`; `call`
  arrays' `start` → `from`.
- **`ref` is an id, `object` the `{ref, name, class}` struct:** `calls-read`
  rows' `object` → `ref`; address-valued `object` → `object_addr`
  (`calls-start`, timed writes).
- **Times are milliseconds in `_ms` keys:** `seconds` → `duration_ms`
  (`each`, `call-each`, `calls-start`); watch `ms` → `duration_ms`, rows'
  `us` → `at_ms`, gaps → `start_ms`/`end_ms`; change and `calls-read` rows'
  `ms` → `at_ms`; `census` `ms` → `scan_ms`; `run` and `usage` `ms` →
  `elapsed_ms`; `run_status` `seconds` → `elapsed_ms`. An older `usage`
  baseline is refused: save it again.

## 0.6.0

Ask a live game where its objects live, and keep exploring while long
calls run: `owner` and `route` answer "who holds this and how does the
engine reach it" in a call each, clients share the agent in short turns,
plans run in the background, and IDA names native functions in one step.

### New

- **`owner` and `route`:** which object, field path or container element
  holds an address, and the shortest field paths from a root to an object
  ([owner and route](docs/commands/unreal-objects.md#owner-and-route)).
  Protocol change: rebuild and re-inject.
- **IDA names a UFunction's native function** and types its `this`;
  `workspace()` and `finish()` do IDA start and finish in one call each
  ([IDA loader](docs/commands/ida-loader.md#opening-a-save)).
- **Background runs:** a plan runs in its own console while you keep
  exploring: `run_status`, `run_read`, `run_stop`
  ([client](docs/client.md#python-session)).
- **`vtable` `slots` and `parts`:** pick slots, or list an object's
  interface tables ([native objects](docs/commands/native-objects.md#vtable)).

### Improved

- **"Update ReAgentLive" is enough:** the skill follows the newest update
  procedure from `origin/main`; submodule hosts get their own checked steps
  ([update](docs/agents/update.md#1-find-the-project)).
- **Clients share the agent in short turns:** any number of consoles,
  sessions and MCP calls attach at once
  ([protocol](docs/protocol.md#handshake)).
- **Long verbs print progress** on stderr.
- **The skill learns on its own in `user` mode**
  ([improving](.claude/skills/reagentlive/references/improving.md)).
- **Concurrent consoles keep their files whole**, and MCP agents load the
  skill first.

### Fixed

- **`save.callees` no longer skips a callee behind a one-byte decoy entry.**

### Breaking

- **The IDA side is set up once, not copied:** run
  `python -I tools/ida_setup.py --install`
  ([IDA loader](docs/commands/ida-loader.md#install)). IDA `import_labels`
  returns a dict; read the keys.
- **Local lessons moved** to `%LOCALAPPDATA%\reagentlive\lessons\`; move
  the lines of an old `references/local-lessons.md` into `lessons\general.md`
  ([improving](.claude/skills/reagentlive/references/improving.md#local-lessons)).
- **`Session(hold=...)` is gone:** drop the argument. A busy agent now
  refuses the call as `busy` instead of the console; C++ `Client::Attach`
  no longer returns `Busy`.
- **`vtable` lost `count` and `table`:** pass `view.limit` for the first N
  slots.

## 0.5.0

Plain JSON calls with refusals that show the fix, sessions that load only
the tools they need, Blueprint logic that reads like code, and readable
pseudocode in IDA in one step. Several verbs were renamed or merged; the
old forms are refused with the new name.

## 0.4.0

Any UE5 game without an SDK dump, engine calls from any host, and output
that costs an agent far less.

## 0.3.0

Cheaper agent sessions, engine calls that survive a fault, and an Unreal
pack that keeps working after a game patch.

## 0.2.0

AI agents get MCP tools: one tool per console verb, with checked
arguments and typed results. Modes became verbs (`sig-check`,
`watch-start`, `pin-list`, ...); hosts give verb rows instead of
`hostHelp`.

## 0.1.0

First versioned release: in-process agent, Unreal pack, native work,
console and Python client, IDA label importer, generic host and the
`reagentlive` skill.
