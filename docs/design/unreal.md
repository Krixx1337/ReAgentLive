# Design: Unreal pack

*Home of:* why the Unreal pack is built this way: layouts from an SDK or the stock layout with addresses found at run time, thread kinds stated by the host, off-thread reads, calls and their guards, the class index, the stock layout's group checks and the Draw tick; with the rejected alternatives.

**TL;DR:** The pack takes layouts, never addresses, from an SDK, checks the stock
UE5 layout on known objects when there is none, and calls engine code
only on the thread the host says is the game's. Each decision with what
lost. Behaviour: [Unreal pack](../commands/unreal.md). Part of
[design](README.md).

## Decisions

- **Compile model:** compiled inside the host, from its sources. Every
  handler reads engine types through one layout (`EngineLayout`,
  `packs/ue/engine_layout.h`, the one home of engine offsets): `StockUE5`
  checked at run time (`reagentlive::ue-base`), or a host's SDK read with
  `offsetof` (`reagentlive::ue`). Only that header and `agent_ue_globals.cpp`
  see `SDK/`, so there is no abstract engine interface and no second copy of
  the handlers.
- **Layouts from the SDK or the stock layout, addresses found at run
  time.** A game patch moves the object table and the name pool but rarely
  the engine's types, and right after a patch is when the tool is needed
  most. So the pack never takes addresses from an SDK (at most a guess it
  checks), locates both globals itself (host guess, then a shape scan of the
  image's writable sections) and checks them before its first command
  ([before the first command](../commands/unreal.md#before-the-first-command)).
  A failed check turns the pack off with its reason; core commands stay.
  Names are decoded from the located pool, not through the SDK's
  `FName::ToString`, whose engine call sits at a baked address. Rejected:
  trusting the SDK's offsets (stale after every patch, and a stale name
  function is a call into random code); code signatures for the globals
  (they break on patches too); a host-supplied name decoder (it carries the
  host's stale address).
- **Lookups without an SDK.** The table, the object header and two UStruct
  offsets are all `find`, `census` and names need; `ue-base` tries the known
  UE5 shapes and probes the rest from the engine's own class tree, under the
  same checks ([without an SDK](../commands/unreal.md#without-an-sdk)).
  Every other op reads the stock layout once its group passes
  ([decision](#without-an-sdk-the-stock-layout)).
- **Three thread kinds, stated by the host.** `Host::thread` is `engine`,
  `borrowed` (a thread the game runs for another purpose, such as the frame
  present) or `own`, with no default: the agent will not open until the
  host says. The core publishes it (`status`, `tick_accurate`) and the pack
  enforces the engine-call rule. Off the engine's thread, memory commands
  work, pack reads answer when the host opts in, and everything that runs
  engine code is refused ([the list](../commands/unreal.md#off-the-engines-thread)).
  A host that wants calls from there moves its tick onto the game thread
  with `reagentlive::ue-draw` ([decision](#a-game-thread-tick-through-the-viewports-draw)).
  Rejected: detecting the kind (a wrong guess is worse than a required
  statement); engine calls off the engine's thread behind a flag (unsafe in
  a way no guard fixes); queuing requests from a borrowed thread to the
  engine's (needs a second tick source, which is what the Draw tick is).
- **The engine-thread check is required.** Every pack handler asks
  `Context::gameThread` first; a host that leaves it out is refused, not
  trusted.
- **Best-effort pack reads off the engine's thread, behind a host opt-in**
  (`Context::offThreadReads`). Lookups and reads call none of the engine's
  code, so they can answer under a fault guard, checking the object's
  identity before and after each read; values may tear between two fields,
  and the console marks such results `tick_accurate: false`. A function's
  parameters stay on the game thread with the calls. Rejected: on by default
  (a host must accept torn values knowingly); answering calls the same way.
- **Readable names live in the client, not the agent:** a host-supplied names
  file keyed by owner, offset, size and bit
  ([readable names](../commands/unreal-fields.md#readable-names)). Rejected:
  a `displayName` hook in the agent, which would cost the game thread and
  the injected image for display-only data.
- **Static-tool names: borrow generator mappings where they exist.** A host
  whose SDK generator writes IDA mappings imports those first;
  `labels-export` with `kinds` interface and global then adds only what
  needs the running process. ReAgentLive never requires or reads those
  mappings and keeps every kind so it can name a database alone. Rejected:
  dropping the `exec` and `vtable` kinds (another tool would become a
  requirement).
- **Calls run under a fault guard.** An access fault inside `ProcessEvent`
  is answered `access faulted`, saying the target may be in a bad state,
  instead of ending the game. It is no safety net: a fault can leave a lock
  held or state half-changed. Rejected: catching everything (a C++
  exception or a debug break belongs to the engine) and no guard (one wrong
  block ended the process and the investigation).
- **`call-pure` is a read the agent checks.** A const or blueprint-pure
  function whose outputs own nothing is what `call-each` calls on every
  instance; the verb makes the claim checkable (refused before calling) and
  gives it a read-only tool. The flag is the engine's convention, not a
  proof. Rejected: marking `call` read-only when the function is pure (a
  verb's mark cannot depend on its arguments).
- **A class index for `find` by `cls` and `each`.** A class walk cost about
  six times the name walk per object, each class's parent chain checked
  with a `VirtualQuery` per step. The agent now keeps per slot the object
  last seen and its class; a query compares slot pointers and reads only
  changed headers, and parent chains go through a cache of the last readable
  regions. Class names resolve by an exact find that stops at the first
  match. Rejected: rebuilding only when the table's count changes or after
  N seconds (slots are reused, so the count barely moves while objects come
  and go); a thread of its own (the pass fits the existing scan budget, and
  a pack thread would need a host opt-in); a name-to-class index built by
  one scan (thousands of pages for a large game).
- **One object's `ProcessEvent` through a private vtable copy.** "What is
  this object being told to do" is one object's events, so `calls-start`
  without a function points that object at the core's vtable copy
  (`core/vtable_copy.h`) and swaps `ProcessEvent` there: only that object
  pays, nothing shared is written, and other owners of the game's table are
  untouched. Rejected: the shared `ProcessEvent` entry or code (read-only
  data, every object's calls on the engine's hottest path, a clash with
  other hooks); swapping every `UFunction::Func` of the class (many
  pointers for one question, and Blueprint functions share one entry).
- **Known debt: the interface stub pattern decodes bytes in the agent.**
  `func` on an interface function matches one stub shape
  (`StubVtableSlot`) inside the agent, against the rule that the agent reads
  bytes and the console decodes them. It stays because resolving a live
  object's implementation needs it in one request; the console's decoder
  ([fast lane](native.md#fast-lane)) is where it would move.

## Without an SDK: the stock layout

**Decision.** `reagentlive::ue-base` reads every engine type at the offsets
of `StockUE5` and checks them in groups (class, struct, field, property,
typed, function, enum, call) on objects every UE5 game has. The first op
that needs a group runs its check once; a failed group refuses its ops with
the check that failed, and the others still run
([groups](../commands/unreal.md#without-an-sdk)). With `reagentlive::ue` a
host's SDK is the layout and no group is checked.

**Why.** One prebuilt agent then works in any stock UE5 game with no dump,
including right after a patch, and a non-stock engine refuses instead of
reading garbage.

**Rejected.**
- *SDK only:* a dump per game and per patch before the first lookup.
- *A probe framework* searching each member's offset per engine version:
  more code to keep right, and a search finds a wrong but plausible offset
  as readily as the right one; a known answer on a known object either
  holds or it does not.
- *An in-agent probe* guessing a non-stock layout at run time: the same
  risk inside the target, with no one to check. Finding a non-stock layout
  stays the skill's investigation, read-only and checked.

**Consequences.** A non-stock engine gets the groups that pass and refusals
for the rest; filling the gap needs a layout the agent can load at run time
([ideas](../ideas.md#33--layout-profile-loading)). Refs carry serial 0
without an SDK.

## A game-thread tick through the viewport's Draw

**Decision.** `reagentlive::ue-draw` gives a host that ticks off the game
thread a tick on it, with no per-build address: when `game-thread` arms it,
it finds the first live `GameViewportClient`, copies the vtable of its Draw
interface (offset and index in `engine_layout.h`, confirmed per game),
swaps Draw in the copy and writes one pointer, that interface's vtable
pointer inside the viewport object, to the copy. The copy is the core's
(`core/vtable_copy.h`), shared with any other entry swapped on that
interface. The hook forwards every
call first, then ticks the host only for that viewport and only on the
process's first thread. `off` restores the pointer only if it still holds
the copy. Host API: [hosting](../hosting.md#game-thread-tick).

**Why.** Calls, `each`, `script`, `name-refs` and the recorder need the game
thread, and a host on its own thread has none. Draw runs once per frame on
the game thread in every UE5 game, and a private table copy touches nothing
shared.

**Rejected.**
- *Writing the game's vtable entry:* read-only data shared by every
  instance, and a clash with any other owner hooking it.
- *A per-build profile* of a game-thread function to hook: a signature or
  RVA per game and patch.
- *Hooking `ProcessEvent`:* code or a shared vtable entry again, on the
  engine's hottest path, ticking far more than once a frame.
- *An inline code hook on Draw* or a present/graphics hook: patches code,
  or ticks on a render thread with a graphics-API dependency.

**Consequences.** `game-thread` is marked `writes` (one pointer in a heap
object), so a client asks before it; the host must allow only one tick at
a time, since its own thread and Draw both tick the agent.
