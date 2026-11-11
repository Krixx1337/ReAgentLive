---
name: reagentlive
description: Investigate a running game with ReAgentLive, an in-process agent driven by a console - find objects and UObject instances, read reflected and native fields, decode FName and gameplay tag indices, call engine getters, capture runtime behaviour, read live code (disasm, xrefs, signatures) and turn findings into evidence-backed changes. Use for any runtime question about game data, engine objects or a mod's behaviour, before guessing from an SDK dump.
---

# ReAgentLive investigations

ReAgentLive is a small agent inside the game process, run by a host (a mod
or a DLL), that executes commands on the thread the host ticks it on. A
console exe sends them over shared memory. Nothing touches the game except
through the agent. Reference docs: `docs/INDEX.md` in the ReAgentLive
project (every command, its page and a glossary).

Before the first command, read the project's host notes if it has any (a
host skill or doc): the console's name, host commands, build and approval
rules. They win over this skill.

## Rules

- Reads need no approval: `find`, `read`, `fields`, `dump`, `get`,
  `describe`, `name`, `func`, `snap`, `record`, `watch`, `census`,
  `name-find`, `each` (it refuses anything but pure getters), and pure engine
  getters such as `GetSocketTransform` (say what you call).
- Ask the user first for any `write`, `pin`, and any `call` that could change
  game state or crash: give the address, current bytes, new bytes and expected
  effect. One approval covers one write.
- Start a session with `status` and read `host_thread`. On a host that is
  not `engine`, a tick is a frame, not a game moment: confirm a value with
  a second read before relying on two values lining up, and expect Unreal
  pack commands to be refused.
- Only the user's live session is evidence; synthetic tests are not. Record
  live results where the project keeps them, stating what is and is not
  established. If the project's own rules differ from these, theirs win.

## Commands

Full syntax: `<console> -c help`, or the command pages in `docs/`.

| Command | For |
|---|---|
| `find [text] [--class C]` | Objects by name and/or class; 24 per page |
| `census [--class C] [--match T] [--changed] [--limit N] [--from N]` | Live instances per class, with sample refs; `--changed`: only what differs since the last census. Pages of 100: `--from <next_cursor>` for the next, no rescan; `--limit 0` for all |
| `each <Class> <Field\|Owner:Fn>` | One field or pure getter for every instance, one tick |
| `dump <obj> [--match T] [--nonzero] [--changed]` | Every reflected field with its value; filters cut output |
| `get <obj> <Field.Path>...` | Typed values along pointer and struct paths |
| `batch { cmd; cmd; ... }` | Many commands in as few ticks as possible |
| `fields <obj\|class>` / `describe <ref>` | Field layout / an object's name, class and ref |
| `name <index>...` / `name-find <text>` | FName index to text / text to index |
| `read` / `read-batch` / `search` | Raw memory; same-tick ranges; addresses holding a value |
| `entries <obj> <Field> [--from N]` | Every element of an array, set or map field, paged; maps as key and value |
| `peek <addr> [count]` | What qwords hold: object, `module`+`rva`, heap pointer or value (with a name hint) |
| `disasm <addr> [n] [--function] [--follow]` | Instructions from live bytes: targets, string hints, address fields; `--function` uses the exception table's bounds |
| `xrefs <addr> [--module M]` | What points here: calls, jumps, RIP-relative lea/load/store, stored pointers, each with its function |
| `search text "<s>" [--wide]` | Where a string lives, quoted for spaces (then `xrefs` on it) |
| `vtable <obj> [n] [--offset N] [--resolve]` | A native object's virtual table, slots until one is not code |
| `region <addr>` | What memory is: `code`, `rodata`, `data` (with section), `mapped`, `private`, `free` |
| `read <addr>->0x10->0x8 [n] [--as cstr\|wstr]` | Read through a pointer chain (each `->off` but the last reads a pointer); C or UTF-16 strings |
| `sig --check` / `sig --bytes` | Is a byte signature unique and does it land there / live bytes with address fields masked |
| `func` / `funcs <obj> [text]` / `params` | A UFunction's entry / functions matching text / its parameter block |
| `call <obj> Owner:Fn <hex\|Name=value>` | ProcessEvent; prints the block after the call |
| `write` / `pin` | Change memory once / every tick (approval) |
| `watch <s> <addr>:<len>... [--then call ...]` | Sample ranges every tick inside the agent |
| `watch --start <dur> [--every <dur>] [--follow root]` / `--read` / `--stop` | A capture the agent keeps while the console is free |
| `read <addr> --as "<layout>"` / `label <obj> <off> <type> <Name>` | Decode a block of mixed types / name an unreflected member for good |
| `snap` / `dump --changed` / `record` | Baseline, changed fields, named fields over time |
| `status` / `base` / `modules` | Agent build and tick rate; exe and module ranges |

Hosts add their own commands (see the host notes).

## Refs

`$N` comes from the last `find`, `census`, `each`, `dump` or `describe`. The
printed literal `0x<address>#<index>:<serial>` survives between runs and is
refused once the object is replaced; prefer it for anything you rely on. A
raw `read` of an old address still returns bytes: it checks memory, not
identity.

Code and static data: addresses inside a module print `module` and `rva`,
and `<module>+<rva>` (`game.exe+0x1a2b3c`) works as an address. Use the RVA
to cross to the static tool (native-code reference).

## Workflow

1. **Question first.** Write the runtime question and what would answer it
   ("which field drops when I fire?", "what spread does the gun really have?").
   Read what the project's docs already record about it.
2. **Locate.** `find --class <Class>` lists instances; add name text to narrow.
   Name text is a substring match anywhere, so short text floods the page; use
   a distinctive suffix or a class.
3. **Navigate.** `dump` an owner object and follow the printed refs instead of
   computing offsets.
4. **Decode.** Run `name` on every `FName #N` that matters. Readable names for
   obfuscated fields are hints; trust offsets, sizes and types.
5. **Observe change.** `snap`, the user acts, `dump --changed`; or `record`
   for sequences. Pick the object that owns the data: no changed rows usually means
   the wrong object (position lives on a scene component, not the pawn).
6. **Cross-check.** Confirm data with a second, independent source (asset
   value vs measured behaviour, computed point vs bone transform). One source
   is a hypothesis.
7. **Ship and verify.** When a finding changes code: pure logic with unit
   tests, then a live capture that proves the new code path ran (a case
   only the new logic explains), then the evidence record.

## Efficiency

- One-shot runs: `<console> -c 'find X; dump $1'`; exit code 1 on any error.
  A cache file keeps class refs, field tables and the last `$N` list for the
  same agent instance; a re-injection or restart empties it.
- Fewer tool calls beat faster ones: put everything for the next decision in
  one `batch { ... }` or one `get` with several paths.
- Filter `dump` (`--match`, `--nonzero`, `--changed`) instead of reading all
  of it.
- Values read in separate commands come from different ticks; put what must
  line up in one `batch`, `get` or `read-batch`. A batch with `each`,
  `census`, `search`, `refs`, `xrefs` or `touches` spans ticks: read
  `spans_ticks`.
- Add `--timing` to a line to see its round trips.
- One console per agent: a second one is refused as `busy`.

## Pitfalls that bite any session

- **Refs go stale.** Respawns replace objects and ref commands refuse them.
- **Native members may be absent from reflection.** `dump` can show a
  DataTable's row struct while omitting its native `RowMap`.
- **UI names differ from internal names.** Map through item tags, not by
  guessing.
- **Settings confound captures.** Ask the user to turn off host features that
  touch what you measure, and to keep still. Before blaming the code, read
  the saved settings file: a test may have run with the feature off.
- **Interactive tests.** Give the user exact steps and wait for one "ready";
  a timed background capture expires before they read your message.
- **Locale.** Formatted numbers may use commas; parse values from raw bytes.
- **Empty session.** With no second player, behaviour that needs one cannot
  be observed; verify on the local character and record what stays unmeasured.

## Read when

| Before you... | Read |
|---|---|
| write a script or loop, compare or intersect results, parse output, or use PowerShell or Python | `references/console-driving.md` |
| sample a value over time, or see what changes across an action (`watch`, `record`, `snap` then `dump --changed`) | `references/capture-and-timing.md` |
| send any `call` (pure getters too: block layout, leaks), `write` or `pin` | `references/calls-and-writes.md` |
| ask about tags, attributes, effects, "why can't I do X", data tables, maps or traces (Unreal) | `references/unreal/gameplay-systems.md` |
| ask about poses, anim layers or nodes, aiming down sights, lean, camera or bones (Unreal) | `references/unreal/animation-and-camera.md` |
| follow a native getter, read a Blueprint function's bytecode, disassemble, find the code using an offset, or make or check a byte signature | `references/native-code.md` |
| hunt unreflected state or padding, read an object with no reflection, or use `search` | `references/hidden-state.md` |
| rebuild, reinject, or see an attach or layout error | `references/reinjecting.md` |

## When ReAgentLive hits a limit

Add the missing piece instead of working around it twice; `name`,
`find --class`, larger array previews and `describe #index:serial` all came
from real investigations. Keep ReAgentLive engine- and game-agnostic, test it,
document it in ReAgentLive's docs, and re-rank its backlog (`docs/ideas.md`)
with the evidence.

## Keeping this skill current

At the end of every ReAgentLive session, ask what would have saved time:

- **Scope first.** A lesson that would hold in another game goes here,
  worded without the game (an Unreal-only one under `references/unreal/`).
  One about driving one host's console goes to that project's host notes.
  Something learned about the game is a finding: it goes to the project's
  docs, not a skill. This skill never names a game or host; the
  `reagentlive.docs` test checks.
- A new lesson goes to the matching reference, symptom or question first. It
  goes in this file only if every session needs it before its first command.
- This file stays under about 180 lines; a reference past about 150 lines
  splits by sub-topic, with a new row in the read-when table.
- A lesson that fits no reference starts a new one only when there are three
  or more such lessons; until then it goes to the closest file.
- References do not link to each other.
- Code written twice goes into the ReAgentLive Python session client with a
  test, not into the skill.
- Rewrite or delete stale lessons; no dated logs.
