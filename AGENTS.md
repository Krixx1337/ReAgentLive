# Working on ReAgentLive

An in-process agent library for live game memory and runtime inspection
(Windows x64, C++23), with its console, Python and MCP frontends. Read this
before changing the repo; details live in [docs/contributing.md](docs/contributing.md).

## Four audiences

Each file set has one reader and one style. Write for the reader of the
file you touch.

| Files | Reader | Style |
|---|---|---|
| [README.md](README.md), [docs/](docs/INDEX.md) | humans: what it is, how it works | page header, plain explanations |
| [.claude/skills/reagentlive/](.claude/skills/reagentlive/SKILL.md) | an agent **using** ReAgentLive to investigate | agent-only, imperative |
| [docs/agents/](docs/agents/README.md) | an agent **installing or updating** it | numbered steps, report block |
| `AGENTS.md` (this file) | an agent **changing** ReAgentLive | orchestrator: commands, rules, links |

## Build and test

From an x64 Visual Studio developer shell, in this folder (tests are on by
default when it is the top-level project; Release, since a Debug build of
the CLI tests passes the object file's section limit):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- The docs test alone: `python -I tests/python/test_docs.py`. Add
  `--forbid <word>` once per name: a host passes its own names (CMake
  `REAGENTLIVE_FORBIDDEN_NAMES`), so this repo never has to spell them.
- `reagentlive.python-mcp` skips itself without the MCP Python SDK
  (`python -m pip install mcp`); a skip is not a pass.
- After changing a row of the verb table, regenerate both generated pages:
  `build/reagentlive-table --write docs/commands/results.md` and
  `build/reagentlive-table --write-reference docs/commands/reference.md`
  (`reagentlive.results-doc` and `reagentlive.reference-doc` fail while
  stale; `reagentlive.verb-names` fails on a doc naming a verb or arg the
  table lacks).
- A change is done only when every test passes. What each suite covers:
  [contributing: tests](docs/contributing.md#tests).

## Hard rules

- **Never name a game, mod or host** in code, docs, tests or the skill;
  examples use placeholders ([code rules](docs/contributing.md#code-rules),
  checked by `reagentlive.docs`).
- **No backward compatibility:** rename and remove freely; no stubs, aliases
  or old names. A breaking change gets a CHANGELOG entry saying what to change.
- **One home per fact:** look it up in
  [where each fact goes](docs/contributing.md#where-each-fact-goes) before
  writing it; link instead of repeating.
- **The verb table is the source of truth** for a verb's arguments and result
  keys; `docs/commands/results.md` is generated, never edited
  ([adding a command](docs/contributing.md#adding-a-command)).
- **Engine offsets** only in `src/reagentlive/packs/ue/engine_layout.h` and
  their other homes (`reagentlive.engine-layout`).
- **Protocol change:** a struct that crosses the section joins the layout sum
  (`LayoutCore` in `src/reagentlive/core/protocol.h`, a pack's `LayoutPart`), and its
  CHANGELOG entry ends with `Protocol change: rebuild and re-inject.`
  ([writing entries](CHANGELOG.md#writing-entries)).
- **Writes are marked:** a verb that writes the target or calls into it
  carries `writes` in its row; read-only MCP tools never carry a write
  ([the `batch` rule](docs/tool-interface.md#tool-interface)).
- **Layers and agent-side limits:** code goes by layer, and agent code keeps
  the mapped-image limits ([layers](docs/contributing.md#layers),
  [source layout](docs/contributing.md#source-layout)).
- **Docs style:** listed pages carry their header; `docs/agents/` pages and
  skill files follow their own rules ([docs](docs/contributing.md#docs)).
  Skill edits follow
  [maintaining](.claude/skills/reagentlive/references/maintaining.md).

## Working on it

- Work on a feature branch. Never commit to `main` or push unless asked.
- Conventional commits (`feat:`, `fix:`, `docs:`, `refactor:`, `test:`).
  Stage files by name; never `git add -A`.
- Update docs and the CHANGELOG's `Unreleased` section in the same commit as
  the code.
- Live tests are the user's. Synthetic tests (loopback, fake engine, example
  DLL) never count as live results.

## Large changes

A change across several files or layers:

1. **Plan first.** Write the plan to a file, list the open decisions, and
   wait for the user's answers before writing code.
2. **Orchestrate.** Give every subagent the whole plan plus a contract:
   the settled decisions, the vocabulary (names, terms, phrasing), the files
   it owns and the hand-off file it writes when done.
3. **Writers on disjoint files.** No two writers edit the same file; a shared
   file has one owner, and the others send it their text through hand-offs.
4. **Verify with fresh eyes.** A verifier that did not write the change reads
   the diff against the plan and the hard rules above.
5. **Tests last:** the full build and `ctest`, then fix and rerun until green.

## Read next

- [contributing](docs/contributing.md): layers, source layout, adding a
  command or a pack, tests, code rules, docs rules.
- [design](docs/design.md): why it is built this way, what was rejected.
- [protocol](docs/protocol.md): the wire format, ops, statuses, limits.
- [tool interface](docs/tool-interface.md): the verb table, `help` with `json`,
  generated functions and MCP tools.

## Collaboration note

We are building this together. If you notice something important, a useful
guardrail or a workflow improvement that should live in this file, add it
proactively: one line in the right section, details linked to their home,
and the file kept under 120 lines.
