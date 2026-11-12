# ReAgentLive positioning

What ReAgentLive is for, what makes it different, and how to describe it.
The README is the short, benefits-first pitch built from it; the detail
lives here. It states the
direction; [design](design.md) holds the decisions and
the [command pages](INDEX.md#commands) the behaviour. Update it when the role changes, not
when a feature ships.

## One line

ReAgentLive is a live, in-process probe that lets an AI agent investigate a
running game safely and in real time.

## Short description

ReAgentLive is a small agent that runs inside the game process: built into
your own mod, or loaded by a small host of its own beside a tool that works
from outside. An AI drives it through a console to find objects, read and follow live data, test changes
that undo themselves, and check its conclusions against the running game.
Every answer comes from one moment in time, every write is guarded, and
nothing waits for a disassembler to finish analysing. It pairs with static
tools rather than replacing them: ReAgentLive answers "what is happening right
now", they answer "what does this code do".

Three words: **live, safe, immediate.**

## End goal

ReAgentLive is the live, in-process leg of an AI's reversing kit. It answers
"what is true in the running game right now, and is it safe to try this?",
and hands everything else to a neighbouring tool in a form that tool can
use. It is a good neighbour, not a replacement: it does not grow into a
second scanner, debugger or instrumentation framework.

## What is unique

No single one of these is new. The combination is.

| ReAgentLive | Typical alternatives |
|---|---|
| **Runs inside the process**, on a thread its host trusts, with no debugger attached and no hooks of its own | A debugger, a hooking framework, or reading from outside the process |
| **Tick-accurate:** everything in one answer comes from the same moment | Torn reads from outside, or polling |
| **Safe by construction:** references that go stale safely, guarded writes, trial writes that restore themselves | Raw write and code-injection tools handed to an agent |
| **Shaped for an AI's costs:** few commands, whole answers, filtered output, state kept between runs | Large tool surfaces that fill an agent's context before doing anything |
| **Immediate:** no database, no hours of analysis | Static tools need their analysis first |
| **Reflection and raw memory in one tool** | One or the other |

## Role among other tools

ReAgentLive is one tool in a kit, and says so.

- **It builds** what needs the running process, and local one-address
  questions that should not wait for whole-program analysis.
- **It borrows** everything else: whole-function logic, types, call graphs
  and decompiling from a static tool; narrowing scans from a memory
  scanner; tracing from an instrumentation tool.
- **It hands over** in a neutral form: `module` and `rva` both ways, and a
  label file a static tool imports.

The rule and its table are in
[design](design.md#place-among-other-tools).

### Pairings

| Neighbour | It does | ReAgentLive gives it | ReAgentLive takes from it |
|---|---|---|---|
| A static tool (the main pairing) | whole-function logic, types, decompiling | live values, `module` and `rva`, exported labels, signature checks | names, what a function means |
| An engine dumper | a full SDK and offsets | a live check that an offset is right | names for data the engine does not reflect |
| An instrumentation tool | call tracing, hooks | exact addresses, live state before and after | who calls what, with which arguments |
| A memory scanner | narrowing scans, pointer scans | a consistent snapshot to confirm a find | candidate addresses |

### Gaps ReAgentLive fills

What the neighbours do badly or not at all. New work is judged against
this list.

- **Verification:** checking a static conclusion against the live process
  cheaply: a signature is unique, an offset is right, a label matches.
- **Safe experiments:** guarded writes and trials that undo themselves.
- **Tick-accurate state:** same-moment reads on the engine's thread.
- **Handoffs:** addresses and names in a neutral form another tool loads.
- **Nothing to install:** no debugger attached, no driver, no outside
  process handle. Fewer moving parts. This is a statement about setup, not
  about protected software; see "What it is not".

Candidates, because they need the running process anyway: a minimal
narrowing scan (changed, unchanged, increased, aligned to the tick) and a
log-only call probe. Neither is planned; each waits for a case the
neighbours cannot cover.

## Ways to host it

ReAgentLive never injects itself and never hooks. Something inside the process
calls its tick; that something is the host, and it can be small.

| Setup | Host | Notes |
|---|---|---|
| An internal mod | the mod itself | the mod already runs on the game's thread; ReAgentLive reuses its thread and object checks |
| An external tool or mod | a small DLL its author writes, that only ticks the core | the external tool keeps working from outside and uses the core for what needs the inside |
| No mod at all | the own-thread example host, unchanged ([try it](hosting.md#own-thread-example)) | memory commands and the fast lane in any process; no engine access, not tick-accurate |

What the host's thread allows decides what works:

- **Memory commands** (read, search, watch, guarded writes, the fast lane)
  are safe from any thread the host ticks on.
- **Engine calls and object walks** need the thread the engine expects. A
  host that cannot tick there offers the memory commands and refuses the
  rest; the agent's operation list tells the console which.

The first and last setups exist today. ReAgentLive ships the library and
one example host; a real host's DLL, entry and tick source are its
author's to write ([hosting](hosting.md), [clients](client.md)).

## Two layers

| Layer | What it is | Who uses it |
|---|---|---|
| **The core** (agent, transport, client library, engine pack) with a host | a lean in-process proxy: read, write, same-tick batches, guarded and expiring writes, engine calls. No hooks, no drawing, no feature logic | any outside program that links the client: a tool, a mod that keeps its logic outside the game, or ReAgentLive's own frontends |
| **ReAgentLive the probe** (console, Python client, skill) | the development tools built on the core | an AI or a person investigating |

The pitch on this page is about the probe. The core is deliberately
usable without it ([outside programs](outside-programs.md)), and the probe adds nothing to the process that the core
does not already have. One client connects at a time; a second channel is
deferred until an investigation and an outside program must run together
([outside programs](outside-programs.md#not-built-yet)).

## Low footprint

ReAgentLive answers when asked and otherwise stays out of the process's
way:

- **No hooks of its own.** The core never patches code or swaps a
  pointer; where the tick comes from is the host's business.
- **No threads of its own.** It runs on a thread the host already has.
  The own-thread example adds exactly one, its tick; background search
  adds a worker only when the host allows it.
- **No process handle.** The client never opens the target process: no
  `OpenProcess`, no `ReadProcessMemory`, no debugger. Requests travel
  through one named shared-memory section.
- **Next to nothing while idle:** one atomic increment and one compare
  per tick. Fixed buffers, no allocation at load.

Low footprint means it does not disturb the target. It does not mean
hidden: see [what it is not](#what-it-is-not).

## Safety model

- **Writes check first.** A write can require the bytes you expect to
  still be there; otherwise it is refused and shows what it found.
- **Identity, not just addresses.** An object reference carries its table
  slot and serial, so a replaced object is refused instead of written
  into.
- **Changes that end on their own.** Pins expire; a trial write restores
  the original bytes when it expires or is cleared, and stops writing as
  soon as its owner is replaced.
- **Engine calls only on the engine's thread.** A host that ticks
  elsewhere gets memory commands, and engine commands are refused
  ([host thread](hosting.md#host-thread)).
- **Approval stays the user's.** Anything that can change state is a
  separate, explicit command, and its MCP tool is marked destructive
  ([MCP tools](client.md#mcp-tools)).

## What it is not

- **Not a standalone injector.** It needs a host inside the process that
  calls its tick; how that host gets loaded is the user's business.
- **Not "any game" out of the box.** The core works in any Windows process
  that hosts it. Objects, fields, calls and bytecode need an engine pack;
  today there is one, for Unreal. That pack is compiled against the host's
  generated SDK headers and assumes Unreal Engine 5 layouts, so another
  game needs its own SDK dump and a check of those layouts.
- **Not a renderer, and the probe is not a mod's runtime.** Nothing in
  ReAgentLive draws or hooks a frame. The console, the Python client and the
  skill are development tools: one client, a request at a time. The core
  underneath is a different matter; see "Two layers".
- **Not a debugger, a disassembler suite or a signature maker.** Those
  exist; ReAgentLive checks and reports, and leaves analysis and decisions to
  the agent and the static tool.
- **Not a way around protection.** It is not described or built as a way
  around what stops other tools: it still needs a host loaded into the
  process, and a title that blocks one usually blocks the other.

## The parts, by audience

| If you want | ReAgentLive offers |
|---|---|
| To see what a game is doing right now | find, dump, follow paths, read containers, sample every tick |
| To test an idea without rebuilding | guarded writes, pins, trial writes that undo themselves, engine calls |
| To understand code without waiting | the fast lane: instructions, references and signature checks at one address |
| To work with a static tool | addresses both ways, live values for unnamed globals, exported names |
| To let an AI do the above | one console, JSON in and out, a Python client, a skill that teaches the method |

**Fast lane** is the name for one capability only: one-address questions
answered from live bytes with no database. Keep it for that.

## Status against this description

Shipped: the in-process agent, tick-accurate reads, guarded writes and
trials, reflection commands for Unreal, the address bridge and label
export, the fast lane (`disasm`, `xrefs`, `sig-check`;
[design](design.md#fast-lane)), the AI-facing console and Python client.

Native objects without an engine pack (`vtable`, pointer chains, strings,
`region`; [design](design.md#native-objects)) have shipped too. Planned,
so not yet true: calling native functions
([backlog](ideas.md#17--call-native-caution)). Use the pitch above in public only for
what has shipped.

## Before a public release

- ✅ A licence: MIT, and every dependency is MIT too (README).
- ✅ A benefits-first README with an [agent setup](agent-setup.md) prompt
  an AI agent follows on its own.
- What lets someone else use the core: the host and client guides and the
  own-thread example host exist ([hosting](hosting.md),
  [clients](client.md)); still missing are a minimal client example and a
  "bring your own SDK" guide ([backlog](ideas.md#20--example-client-and-sdk-guide)).
- ✅ A plain statement of the [safety model](#safety-model).
- A plain statement of intended use (a development tool).
- The docs stay free of any one game; the `reagentlive.docs` test checks
  that no host or game name appears.
