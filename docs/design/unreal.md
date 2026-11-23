# Design: Unreal pack

*Home of:* why the Unreal pack is built this way: layouts from an SDK or the stock layout with addresses found at run time, thread kinds stated by the host, off-thread reads, calls and their guards, the class index, the stock layout's group checks and the Draw tick; with the rejected alternatives.

**TL;DR:** The pack takes layouts, never addresses, from an SDK, checks the stock
UE5 layout on known objects when there is none, and calls engine code
only on the thread the host says is the game's. Each decision with what
lost. Behaviour: [Unreal pack](../commands/unreal.md). Part of
[design](../design.md).

## Decisions


- **Compile model:** compiled inside the host, from its sources. Every
  handler reads engine types through one layout (`EngineLayout`,
  `packs/ue/engine_layout.h`, the one home of engine offsets): `StockUE5`
  checked at run time (`reagentlive::ue-base`), or a host's Dumper-7 SDK
  read with `offsetof` (`reagentlive::ue`, `engine_layout_sdk.h`). Only that
  header and `agent_ue_globals.cpp` see `SDK/`, so no abstract engine
  interface and no second copy of the handlers.
- **Layouts from the SDK or the stock layout, addresses found at run
  time.** A game patch moves the object table and the name pool but rarely
  the engine's types, and right after a patch is when the tool is needed
  most. So the pack never takes addresses from an SDK (at most a guess it
  checks), locates both globals itself (host guess, then a
  shape scan of the image's writable sections) and checks them before its
  first command: index equals slot, one class of classes, `None` and
  `Class` decode ([before the first command](../commands/unreal.md#before-the-first-command)).
  A failed check turns the pack off with its reason; core commands stay.
  Names are decoded from the located pool, not through the SDK's
  `FName::ToString`, whose engine call sits at a baked address. Rejected:
  trusting the SDK's offsets (stale after every patch, and a stale name
  function is a call into random code); code signatures for the globals
  (they break on patches too, and are game-specific work); a host-supplied
  name decoder (it carries the host's stale address with it). An engine
  version change that moves the types fails the check and turns the pack
  off rather than reading garbage.
- **Lookups without an SDK.** The table, the object header and two
  UStruct offsets are all `find`, `census` and names need, and UE5 keeps
  them in a few known shapes, so `ue-base` tries those and probes the
  rest from the engine's own class tree, under the same checks as the
  globals ([without an SDK](../commands/unreal.md#without-an-sdk)). Every
  other op reads the stock layout once its group passes
  ([decision](#without-an-sdk-the-stock-layout-checked-per-group-on-known-objects)).
- **String type:** the pack only uses `.c_str()` and `.size()` on SDK strings,
  so stock `std::string` dumps and adapted string types both compile.
- **Three thread kinds, stated by the host.** `Host::thread` is `engine`,
  `borrowed` (a thread the game runs for another purpose, such as the frame
  present) or `own`, with no default: the agent will not open until the
  host says. The core publishes it (`status`, `tick_accurate`) and the pack
  enforces the engine-call rule; neither verifies the other. On the
  engine's thread everything works and a tick is a moment of the game;
  elsewhere memory commands work, pack reads answer when the host opts in
  (`Context::offThreadReads`), everything that runs engine code is refused
  ([the list](../commands/unreal.md#off-the-engines-thread)), and a tick is a
  host tick. A host that wants calls from there moves its tick onto the
  game thread with `reagentlive::ue-draw`
  ([decision](#a-game-thread-tick-through-the-viewports-draw)). Rejected:
  detecting the kind (the core cannot know which thread is the engine's,
  and a wrong guess is worse than a required statement); running engine
  calls off the engine's thread behind a flag (unsafe in a way no guard
  fixes); queuing requests from a borrowed thread to the engine's (that
  needs a second tick source, which is what the Draw tick is).
- **Best-effort pack reads off the engine's thread, behind a host
  opt-in.** Lookups and reads (`find`, field tables, object reads, `entries`,
  `enum`, `func`, ...) read memory the engine owns but call none of its
  code, so a host ticking on its own thread can answer them under a fault
  guard, checking the object's identity before and after each read; values
  may tear between two fields, and the console marks such results
  `tick_accurate: false`. Shipped on in `examples/unreal` and used by
  library hosts; live-tested on a UE 5.7 game. A function's parameters
  (`func`'s `params`) stay on the game thread with the calls. Rejected: on by default (a host must accept torn
  values knowingly); answering calls the same way (no guard makes an
  engine call off its thread safe).
- **The engine-thread check is required.** Every pack handler reads engine
  state, so each asks `Context::gameThread` first. A host that leaves it
  out is refused, not trusted: a host ticking off the engine's thread with
  no check would otherwise run engine calls on the wrong thread unnoticed.
  A host on the engine's thread passes a check that says so.
- **Readable names live in the client, not the agent:** a host-supplied names
  file keyed by owner, offset, size and bit ([readable names](../commands/unreal-fields.md#readable-names)).
  Rejected: a `displayName` hook in the agent. It would cost the game thread
  and the injected image for display-only data, and offsets already identify
  a field within one build.
- **Static-tool names: borrow mappings where they exist** (seen live: the
  SDK generator's mappings named 17,652 exec functions and 21,613 types;
  `labels-export` with `kinds` interface and global then named 213 more, the
  generator's own static-FName naming having failed on an unanalysed
  database). Not checked: whether a full export after the mappings would
  comment rather than rename the generator's names; the docs say not to run
  one. A host whose SDK
  generator writes IDA mappings (Dumper-7) imports those first for exec
  thunks, vtables, types and engine globals; `labels-export` with `kinds`
  interface and global then adds only what needs the running process. ReAgentLive
  never requires or reads those mappings, and keeps every kind so it can
  name a database alone. Rejected: dropping the `exec` and `vtable` kinds
  (another tool would become a requirement).
- **Calls run under a fault guard.** An access fault inside `ProcessEvent`
  is answered `access faulted`, saying the target may be in a bad state,
  instead of ending the game. It is no safety net: a fault can leave a lock
  held or state half-changed, so the session is suspect afterwards. Only
  access faults are caught, as in the agent's reads. Rejected: catching
  everything (a C++ exception or a debug break belongs to the engine) and
  no guard (one wrong block ended the process and the investigation).
- **`call-pure` is a read the agent checks.** A const or blueprint-pure
  function whose outputs own nothing is what `call-each` calls on every instance,
  and the skill already called such getters without approval; the verb
  makes the claim checkable (`CallPure`, refused before calling) and gives
  it a read-only tool. The flag is the engine's convention, not a proof,
  and the docs say so. Rejected: marking `call` read-only when the
  function is pure (a verb's mark cannot depend on its arguments).
- **Class names by exact find.** `find`'s first page scans and counts
  the whole table even after the match, so a class name resolved that way
  cost a full scan each; `FindExact` stops at the first match. Rejected for now: a
  name-to-class index built by one scan (thousands of pages for the
  classes of a large game; the exact find makes a miss the only full scan).
- **A class index for `find` by `cls` and `each`.** Measured live on a table
  of about 410 000 objects at 55 ticks per second and a 4 ms scan budget:
  `find` by `cls` took 4.4 s and `find` by `text` 0.7 s, both in the agent (two
  round trips). The class walk cost about six times the name walk per
  object: each distinct class's parent chain was checked with a
  `VirtualQuery` per step. Now the agent keeps per slot the object last
  seen and its class; a query compares slot pointers (a sequential read)
  and reads only changed headers, and parent chains go through a cache of
  the last readable regions. Rejected: rebuilding only when the table's
  count changes or after N seconds (slots are reused, so the count barely
  moves while objects come and go; a fixed age either rebuilds on most
  calls or misses new objects for that long); building it on a thread of
  its own (the pass is a few milliseconds, inside the existing scan
  budget, and a pack thread would need a host opt-in).
- **Known debt: the interface stub pattern decodes bytes in the agent.**
  `func` on an interface function matches one stub shape
  (`mov rax, [reg]; call [rax + disp]`, `StubVtableSlot`) inside the agent,
  against the rule that the agent reads bytes and the console decodes them.
  It stays because resolving a live object's implementation needs it in one
  request; the console's decoder ([fast lane](native.md#fast-lane)) is where it
  would move. Any other stub shape reports a null slot.

## Without an SDK: the stock layout, checked per group on known objects

**Decision.** `reagentlive::ue-base` reads every engine type at the offsets
of `StockUE5` (`engine_layout.h`) and checks them in groups (class, struct,
field, property, typed, function, enum, call) on objects every UE5 game
has: the cast flags of `Class`, `Struct`, `Field`, `Object`; the chain
`Class` > `Struct` > `Field` > `Object`; `Vector`'s and `Guid`'s members,
sizes and flags; every bool on `Actor`; `KismetMathLibrary:Add_IntInt`;
`ENetRole`; `UObject`'s `ProcessEvent` slot. The first op that needs a
group runs its check once; a failed group refuses its ops with the check
that failed, and the others still run
([groups](../commands/unreal.md#without-an-sdk)). With `reagentlive::ue` a
host's SDK is the layout and no group is checked.

**Why.** One prebuilt agent then works in any stock UE5 game with no dump,
including right after a patch, and a non-stock engine refuses instead of
reading garbage. Evidence: live-tested on a UE 5.7 game with no dump.

**Rejected.**
- *SDK only:* a dump per game and per patch before the first lookup, and
  nothing right after a patch, when the tool is needed most.
- *A probe framework* searching each member's offset per engine version:
  much more code to keep right per version, and a search finds a wrong but
  plausible offset as readily as the right one; a known answer on a known
  object either holds or it does not.
- *An in-agent probe* that guesses a non-stock layout at run time: the
  same wrong-but-plausible risk inside the target, with no one to check
  the guess. Finding a non-stock layout stays the agent's (the skill's)
  investigation, read-only and checked.

**Consequences.** A non-stock engine gets the groups that pass and refusals
for the rest; filling the gap needs a layout the agent can load at run
time ([ideas](../ideas.md#33--layout-profile-loading)). Refs carry serial 0
without an SDK (serials are not known).

## A game-thread tick through the viewport's Draw

**Decision.** `reagentlive::ue-draw` gives a host that ticks off the game
thread a tick on it, with no per-build address: when `game-thread` arms it, it
finds the first live `GameViewportClient` with `FirstLiveInstance`, copies
the vtable of its Draw interface (`ViewportInterfaceOffset`, entry
`ViewportDrawIndex` in `engine_layout.h`, confirmed per game before the
first `on`), swaps Draw in the copy and writes one pointer, that
interface's vtable pointer inside the viewport object, to the copy. The hook forwards every call first, then
ticks the host only for that viewport and only on the process's first
thread. `off` restores the pointer only if it still holds the copy; the
copy stays allocated. Host API: [hosting](../hosting.md#game-thread-tick).

**Why.** Calls, `each`, `script`, `name-refs` and the recorder need the game
thread, and a host on its own thread (`examples/unreal`) has none. Draw
runs once per frame on the game thread in every UE5 game, and a private
table copy touches nothing shared.

**Rejected.**
- *Writing the game's vtable entry:* read-only data, shared by every
  instance of the class, and a clash with any other owner hooking it.
- *A per-build profile* of a game-thread function to hook: a signature or
  RVA per game and per patch, the work a stock-layout agent avoids.
- *Hooking `ProcessEvent`:* code or a shared vtable entry again, on the
  engine's hottest path, ticking far more often than once a frame.
- *An inline code hook on Draw* or a present/graphics hook: patches code,
  or ticks on a render thread with a graphics-API dependency.

**Consequences.** `game-thread` is marked `writes` (one pointer in a heap
object), so a client asks before it; the host must allow only one tick at
a time, since its own thread and Draw both tick the agent.
