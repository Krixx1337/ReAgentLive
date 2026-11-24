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

### Improved

- **Setup and update build and check with one script:**
  `tools/build_examples.ps1` builds the examples and, with `-Check`, checks
  each DLL's `status` ([setup](docs/agents/setup.md#3-clone-and-build)).
- **Codex and Cursor get the skill itself:** setup links it into
  `~/.agents/skills` ([skill install](docs/agents/setup.md#6-install-the-skill)).
- **A failed update can roll back** to the previous commit
  ([update](docs/agents/update.md#3-pull-and-rebuild)).
- **Shorter verb help, tool descriptions and refusals:** MCP tool lists are
  smaller; `max_chars` is described once in the server instructions.

### Breaking

- **The setup record's `scratch` key is gone**; the skill keeps no scratch
  file. Delete the key from an existing record.
- **The skill is no longer installed as a copy**, and the setup record has
  no `skill_commit`. A copy install: ask your agent to "update
  ReAgentLive"; it replaces the copy with a link.
- **The docs index is `docs/README.md`** (was `docs/INDEX.md`): change the
  `ReAgentLive docs:` line in your agent's instructions file. The command
  table moved to `docs/commands/README.md`, and `docs/design.md` became
  `docs/design/README.md` plus `docs/design/principles.md`.
- **Verb arguments renamed:** `route` `outer` is `up`; `name-refs` `index`
  is `name_index`; `describe` `index` is `slot`; `disp-scan` `disps` is
  `offsets`; `assets-export` `cls` is `classes`; `each` and `call-each`
  `seconds` is `duration`.
- **`calls-start` takes `object`, not `owner`:** pass the instance or class
  as `object`; an instance now records only its own calls (the old `object`
  filter is gone).
- **`release` `object` is a string** like `call`'s (a ref, `"0x..."` or a
  `Default__` name).
- **`watch-stop` and `calls-stop` are gone:** pass `stop` to `watch-read`
  or `calls-read`; the answer is the same as the old stop verb gave.
- **`touches` `reads` is `access`:** `write`, `read` or `any` as in
  `disp-scan`; `reads: true` is `access: "read"`, the default (all uses) is
  `any`. `write` is new. `read` now counts read-modify-writes and leaves
  `lea` out.
- **`timed-writes` moved from the `observe` pack to `modify`**, beside
  `write` `clear`: add `modify` to `--packs` to list timed writes.

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
  ([update](docs/agents/update.md#host-projects-with-a-submodule)).
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
