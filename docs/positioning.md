# ReAgentLive positioning

*Home of:* what ReAgentLive is for and how to describe it: what is unique, its role among other tools, how it is hosted, the safety model and what it is not.

**TL;DR:** ReAgentLive is the live, in-process part of a reversing kit:
it answers "what is true in the running game right now, and is it safe to
try this?" and leaves whole-program analysis to static tools. It works in
any Windows process, and in any UE5 game it also knows objects by name,
with no dump. It is not an injector, a renderer, a debugger or a way
around protection.

The README is the short pitch built from this page. [Design](design/README.md)
holds the decisions and the [command pages](commands/README.md) the
behaviour. Update this page when the role changes, not when a feature ships.

## What is unique

No single one of these is new. The combination is.

| ReAgentLive | Typical alternatives |
|---|---|
| **Runs inside the process**, on a thread its host trusts, with no debugger attached and no code hooks | A debugger, a hooking framework, or reading from outside the process |
| **Tick-accurate:** everything in one answer comes from the same moment | Torn reads from outside, or polling |
| **Safe by construction:** references that go stale safely, guarded writes, writes that restore themselves (`undo`) | Raw write and code-injection tools handed to an agent |
| **Shaped for an AI's costs:** few commands, whole answers, filtered output, state kept between runs | Large tool surfaces that fill an agent's context before doing anything |
| **Immediate:** no database, no hours of analysis | Static tools need their analysis first |
| **Reflection and raw memory in one tool** | One or the other |

## Role among other tools

ReAgentLive builds what needs the running process, and borrows the rest
(whole-function logic, types and decompiling from a static tool; narrowing
scans from a memory scanner; tracing from an instrumentation tool). It
hands over addresses as `module` and `rva` and labels a static tool imports.
The rule and its table: [design](design/architecture.md#place-among-other-tools).

### Pairings

| Neighbour | It does | ReAgentLive gives it | ReAgentLive takes from it |
|---|---|---|---|
| A static tool (the main pairing) | whole-function logic, types, decompiling | live values, `module` and `rva`, exported labels, signature checks | names, what a function means |
| An engine dumper | a full SDK and offsets | a live check that an offset is right | names for data the engine does not reflect |
| An instrumentation tool | native call tracing, code hooks | exact addresses, live state before and after, reflected calls with their arguments (`calls-start`) | direct C++ callers and native arguments |
| A memory scanner | narrowing scans, pointer scans | a consistent snapshot to confirm a find | candidate addresses |

## Ways to host it

ReAgentLive never injects itself and never patches code. Something inside
the process calls its tick; that something is the host, and it can be
small: an internal mod, a small DLL beside an external tool, or an example
host unchanged ([own-thread](hosting.md#own-thread-example),
[Unreal](hosting.md#unreal-example)). A real host's DLL, entry and tick
source are its author's to write ([hosting](hosting.md)). What each setup
reaches is the [tier ladder](tiers.md); what the host's thread allows:
memory commands work from any thread, object reads from another thread when
the host opts in, and engine calls only on the game thread
([which commands](commands/unreal.md#off-the-engines-thread)).

The core is deliberately usable without the probe (console, Python client,
skill): a lean in-process proxy for programs outside the game
([outside programs](outside-programs.md)).

## Low footprint

It answers when asked and otherwise stays out of the process's way: no code
hooks (two opt-in heap-pointer swaps, [principles](design/principles.md#principles)),
no threads of its own beyond the host's tick, no process handle (requests
travel through one named shared-memory section), next to nothing while idle.
This means it does not disturb the target, not that it is hidden: see
[what it is not](#what-it-is-not).

## Safety model

- **Writes check first.** A write can require the bytes you expect to still
  be there; otherwise it is refused and shows what it found.
- **Identity, not just addresses.** A reference carries its table slot and
  serial, so a replaced object is refused instead of written into.
- **Changes that end on their own.** `undo` restores the original bytes on
  expiry or clear; `hold` stops when it expires or its object is replaced.
- **Engine calls only on the game thread** ([host thread](hosting.md#host-thread)).
- **Approval stays the user's.** Anything that can change state is a
  separate, explicit command, and its MCP tool is marked destructive
  ([MCP tools](client.md#mcp-tools)).

## What it is not

- **Not a standalone injector.** It needs a host inside the process that
  calls its tick; how that host gets loaded is the user's business.
- **Not every engine out of the box.** The core works in any Windows process
  that hosts it; objects, fields and calls need an engine pack, and today
  there is one, for Unreal ([without an SDK](commands/unreal.md#without-an-sdk)).
- **Not a renderer, and the probe is not a mod's runtime.** Nothing draws;
  the console, Python client and skill are development tools, one client at
  a time.
- **Not a debugger, a disassembler suite or a signature maker:** it checks
  and reports, and leaves analysis to the agent and the static tool.
- **Not a way around protection.** It still needs a host loaded into the
  process, and a title that blocks one usually blocks the other.
