# ReAgentLive improvement plan

Status: items 1 to 6 built 2026-11-15, not yet checked live; 7 to 12
open. Items that are accepted move to the [backlog](ideas.md) or get
built; decisions that reverse a recorded one go to [design](design.md)
first.

| Item | Built | Left out |
|---|---|---|
| 1 | compact result schemas by default, `--output-schemas`, `--families`, `--read-only` | the `discover` tool; the text and structured double of each result |
| 2 | exact `find` for class names (`FindExact`), `Context::scanMicroseconds` | the class index; the `--timing` check is still owed live |
| 3 | fault guard on `ProcessEvent`, `call-pure`, `call --dry` | first instructions in the dry output (`disasm` on `exec`) |
| 4 | `layout-export`, `layout-check` | `--match` over every class (needs item 2's index) |
| 5 | `rtti`, `import` on `disasm` rows | names on `peek` and `vtable` rows |
| 6 | `mem-snap`, `mem-diff` with `--keep` | tick-aligned snapshots (a diff is one read of the range) |

The plan rests on the code, the docs, one host's usage log and measured
MCP schema sizes. A survey of a large static reverse-engineering MCP
server supplied the ideas marked *borrowed*.

## Summary

The tool pays most of its cost in two places: MCP schema weight and
Unreal class lookups that take seconds. Both are cheap to fix. The
largest capability gain is an Unreal pack that needs no SDK and no mod.

## Evidence

**Usage log** (one host, 809 calls, 18 sessions, 293 s in all):

| Verb | Calls | Time | Output |
|---|---|---|---|
| `fields` | 173 | 185 s (63%) | 1.43 MB (87%) |
| `params` | 113 | 25 s | 51 KB |
| `find --cls` | 12 | 61 s (2.7 to 14 s each) | 10 KB |
| `describe` | 131, 58 repeated | 0.8 s | 17 KB |

**MCP schemas** (measured from the adapter with the Unreal pack and two
host verbs, 50 tools, 102 KB):

| Part | Size |
|---|---|
| Output schemas | 72 KB |
| Descriptions | 16 KB |
| Input schemas | 15 KB |

Every tool result in those 18 sessions together came to 82 KB. A client
that loads schemas up front pays more for them than for all answers.

**Code:**

- `call` runs `ProcessEvent` with no fault guard
  (`ue/agent_ue_reflect.cpp`, `Call`; `each` in `ue/agent_ue_scan.cpp`).
- Object-table scans get 1 ms per tick (`FindBudgetMicroseconds`), and
  the class cache is cleared on every search.
- The Unreal pack names SDK types about 150 times, so it is compiled per
  game.
- No CI, and the Unreal agent handlers have no automated tests.
- The fast lane names no class from run-time type information and no
  import.

## Priorities

| # | Item | Impact | Effort |
|---|---|---|---|
| 1 | MCP schema diet | high | 1 to 2 days |
| 2 | Class index and scan budget | high | 3 to 4 days |
| 3 | Fault-guarded calls, read-only pure calls | high | 2 days |
| 4 | Bulk layout export and check | high | 3 days |
| 5 | Type names and import names | high off Unreal | 3 to 4 days |
| 6 | Raw memory snapshot and diff | high | 4 to 5 days |
| 7 | Triggered capture | medium | 3 days |
| 8 | Function facts in one call | medium | 3 days |
| 9 | CI and a fake-engine test target | medium | 1 week |
| 10 | SDK-free Unreal pack | very high | 3 to 4 weeks |
| 11 | Opt-in probe: who writes, who calls | very high | 2 to 3 weeks, risky |
| 12 | Cross-build function matching | medium | 1 to 2 weeks |

## Tier 1: quick wins

### 1 · MCP schema diet

- An adapter flag for output schemas: `full`, `compact` or `off`. `off`
  removes 70% of the catalog; the console still checks every result.
- A verb-family filter (`core`, `ue`, `host`, `write`). A hidden tool
  answers "disabled, add family X". *Borrowed.*
- Only if still needed: a small hot set and one `discover` tool that
  returns input schemas a few at a time. *Borrowed*; its source measured
  no gain when the agent needed the cold tools anyway.
- Check whether sending each result as text and as structured content
  doubles the context cost in common clients.
- Meets the trigger of backlog idea 11. Where:
  `frontends/mcp/reagentlive_mcp.py`.

### 2 · Class index and scan budget

- Build a name-to-class index once per agent instance and keep it in the
  cache file, so class-name lookups in `fields`, `params`, `func`, `each`
  and `find --cls` stop rescanning the object table.
- Make the scan budget a host setting in place of the fixed 1 ms.
- The diagnosis is an inference (`fields` has a 925 ms median, and a bare
  class name scans the table). Confirm with `--timing` before building.
- Backlog idea 23 (`each` over the last instance list) follows from it.

### 3 · Safer calls

- A fault guard around `ProcessEvent`: a fault is answered `access
  faulted`, saying the target may be in a bad state, instead of ending
  the process.
- A `call-pure` verb that refuses anything not const or pure, as `each`
  does. It is read-only in the table, so clients stop asking approval for
  getters.
- `call --dry`: resolve everything, call nothing, print the text for the
  approval request.

### 4 · Bulk layout export and check

- `export-layout <file> [--match]`: reflected layouts of many classes
  into a file, in one command.
- `layout-check <file>`: expected `Class.Field offset size` lines in,
  only the mismatches out; the pattern `sig-file` has.
- Replaces the 286 `fields` and `params` calls of the patch-update
  sessions, and covers backlog idea 19.

## Tier 2: new capabilities

### 5 · Type names and import names

- `rtti <addr>`: class name and bases from the virtual table's locator,
  console only. `peek`, `vtable` and `search --heap` rows then carry
  names.
- Import-table targets named in `disasm` rows.
- Design declined type information because one Unreal host had none for
  its game classes. Most native targets built with MSVC have it, and
  they are the "any x64 process" audience.

### 6 · Raw memory snapshot and diff

- `mem-snap <addr> <len>`, then `mem-diff --changed|--inc|--dec --type
  f32`. Console only: `touches` already reads 1 MB in 0.5 s.
- `dump --changed` sees reflected fields only; the skill has agents hunt
  hidden state by hand with 512-byte watch ranges.
- A bounded form of backlog idea 16. An AI agent cannot drive an outside
  memory scanner, so borrowing does not cover this case.

### 7 · Triggered capture

- `watch-start --when <range> changes`, with a short ring buffer of rows
  from before the trigger.
- Fixes a pitfall the skill names: a timed capture expires before the
  user reads the steps.
- Stop reasons that name the limit to raise. *Borrowed.*

### 8 · Function facts in one call

- One verb: bounds, callers, callees, strings, imports and the member
  offsets used. Console only, from existing pieces.
- Replaces four to six calls. It stays inside the fast-lane fence: facts
  about one address, no summary of what the function does.

### 9 · CI and a fake-engine target

- A build and `ctest` run on Windows for every push.
- A small test process with a synthetic object table and name pool, so
  `find`, `fields`, `dump` and `each` run end to end.
- A test that each verb's schema stays under a byte budget. *Borrowed.*
- Prebuilt release binaries of the console and the own-thread host.

## Tier 3: strategic

### 10 · SDK-free Unreal pack

- Replace compiled SDK types with a layout table found and checked at
  start, as the object table and name pool already are.
- One prebuilt DLL then gives `find`, `fields`, `dump`, `get` and
  `entries` in any Unreal game, with no SDK dump and no mod. Calls still
  need a host on the engine's thread.
- Depends on off-thread reads (backlog idea 18) measuring clean, and on
  item 9 for tests.
- [Outside programs](outside-programs.md) already lists it as not built.

### 11 · Opt-in probe

- "What writes this address" and "who calls this, with what" are the two
  questions the tool cannot answer; `touches` is a static guess.
- Reverses a recorded rule (no debug registers, no hooks), so it needs a
  design decision first.
- The rules of backlog idea 15 hold: its own optional part, host opt-in,
  record only, a deadline, an exact restore. Try that idea's host-op
  route first.

### 12 · Cross-build function matching

- After a patch, find a function again by body hash, then by a hash of
  its calls and strings. *Borrowed.*
- It needs a sweep over functions, which the fast-lane fence excludes:
  build it as a script over the Python client, not a verb.

## Not recommended

- **A second channel or several clients:** the log shows no overlapping
  sessions.
- **Larger responses:** the only evidence is `fields`, which item 4
  covers.
- **Evidence ids and bundles** as the surveyed server has them: live
  memory is not reproducible, so content digests add weight for nothing.
- **A findings store in the tool:** a project's own docs hold findings.

## Order

1. Items 1 and 3: small and independent.
2. Item 2 after a `--timing` check, then item 4.
3. Items 5, 6 and 8, console only, in any order.
4. Item 9 before item 10.
5. Item 11 only after its design decision.
