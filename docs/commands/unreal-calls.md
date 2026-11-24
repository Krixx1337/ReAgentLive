# Unreal calls

*Home of:* calling into the engine: `call` and its kept results, `call-pure`, `call-each`, `release`, `func` and `funcs` (native interfaces included), typed parameters and returned arrays, the call recorder, `line-trace`, and `game-thread`.

**TL;DR:** `func` shows a reflected function's parameters and native
entry; `call` calls it with inputs by name (or a raw block) through
`ProcessEvent` on the game thread; `call-pure` and `call-each` call only
const or pure functions, one object or every instance of a class; the
call recorder watches a function's calls; `line-trace` casts a line trace; and
`game-thread` gives a host that ticks elsewhere the game thread. Rules
every pack command shares: [Unreal pack](unreal.md).

## call

The function is searched up the object's class chain, optionally restricted
to the declaring class (`Owner:Function`). A raw parameter block must equal
the function's size exactly; with none given the console sends a zeroed
block and names any input the call needs. Refused off the game thread.
The result's `hex` is the block after the call; with `params` the outputs
are decoded too, and a returned array comes back as `{count, rows,
next_cursor}`.

- **Owned results:** when the return value or a non-const out parameter is
  not plain data (weak handles, strings, arrays), the agent keeps the block
  per function and the next call starts from its outputs, so the engine
  releases the old values on assignment; inputs always come from the
  request. `kept` reports it. A full table refuses a new function
  (`release` one); entries are never evicted, because they still own
  references.
- **Faults:** `ProcessEvent` runs under a fault guard; an access fault
  inside the engine answers `access faulted` instead of ending the process.
  Nothing else is caught, and a fault can leave engine state half-changed:
  treat the session as suspect afterwards.
- **`dry`** resolves the function and builds the block, and calls nothing:
  it shows the block that would be sent and whether the function is const or
  pure. Show it when asking for approval.

`release` drops the function's kept block, so the engine releases what it
held.

### call-pure

As `call`, with the agent's pure check first: the function must be const or
blueprint-pure and no output may own memory (nothing is kept), otherwise it
is refused before calling. Without `params` its outputs are still
decoded, as `call-each`'s. Its row is not `writes`, so its tool is
read-only. Inputs are never zeroed for you (a zero can be a valid,
state-changing argument), so a function with inputs is refused until
`params` names them. Pure is the engine's flag, not a proof: a pure function
can still change state; the skill's approval rules say which to call freely
(not the first call on a new host or in a new game, since a wrong engine
layout can crash the game).

### call-each

`call-pure`'s check for every live instance of a class, in one tick on the
game thread, answered as [`each`](unreal-objects.md#each)'s table. The
parameters must all be plain outputs or the return value. Plain: nothing to
destroy and zero is a valid start (a gameplay tag is plain, a tag container
is not). Anything else is refused and stays with `call`, the message naming
the parameter and why. A verb of its own because its result is `each`'s
table, not `call`'s.

## Named calls

A typed call's `params` names its inputs by the exact reflected parameter
names `func` prints under `params`:

```json
{"verb": "call", "args": {"object": "0x1f2e3d4c50#123:4", "function": "SetTarget", "params": {"Bone": "Head", "Location": {"X": 1, "Y": 2, "Z": 3}}}}
```

Values: names as text (a name the pool lacks is refused), gameplay tags as
the tag name alone, bools, numbers (range-checked; `"0x..."` text for
integers), plain structs by member name or in member order (members left out
stay zero), and objects and classes as a literal ref or address (`null` for
None). Use a raw `block` for anything else (strings, arrays, weak pointers).
`params` `{}` makes a typed call with no inputs. Inputs must be plain single
values; outputs may own memory: they start zeroed and the agent keeps the
block. A const reference is an input; other references are refused. Return
and out values are [compact values](unreal-fields.md#compact-values).

**Returned arrays** come as rows with `count`, `next_cursor` and a cap that
shows as `truncated`; the agent keeps the block that owns the array
([owned results](#call)), so the memory stays valid until the next call of
that function. A `view` pages and filters them (`view.fields` takes paths
into nested structs (nested arrays decode up to 256 elements, 3 deep), and through `key` or `value` into a map;
`view.nonzero` leaves out zero members), and makes the call typed. Only
`call` takes one: an array owns memory, so `call-pure` refuses it.

## func and funcs

`func` resolves a function by exact name on the object's class chain or the
supplied class, validates the function metadata and reports its flags,
native entry (an executable entry can be a generated wrapper, so follow it
separately when locating native game logic) and reflected parameters. The
parameters need the game thread: off it, `params_error` says so and the rest
stands.

**Native interfaces:** with `Interface:Function` on an object, a function
declared on an interface its class implements is found through the class's
`Interfaces` array. An SDK does not describe it, so each array is validated,
never trusted: an implausible one is refused rather than read. `interface`
then reports the part's offset, the vtable slot its stub calls and the
implementation the object's vtable holds there, ready to disassemble.
`call` refuses these: the stub calls that slot on the object it gets
without moving `this` to the interface part, so through `ProcessEvent` it
would run another function.

`funcs` lists owner-qualified function names on the class chain, derived
class first, then those declared on each native interface, marked
`via: "interface"`. An interface row is never callable on the object; use
`func` with `Interface:Function` for its implementation.

## Call recorder

`calls-start` records a reflected function's calls; `calls-read` returns
them so far, `calls-stop` ends it early. A host offers it by listing the
three ops; the start is a write the host approves.

- **What is written:** one pointer, `UFunction::Func`, swapped by
  compare-and-swap for one of a few fixed thunks. No code, vtable or import.
  At the end the original goes back only if the pointer still holds the
  thunk; otherwise something else swapped it since and it is left alone
  (`state: replaced`) while the thunk keeps forwarding, which is safe because
  the host's image stays loaded. `dry` shows the pointer and thunk without
  writing.
- **Which calls:** `object` is an instance (a ref or address: only its calls
  are recorded) or a class (by name or ref: every instance's).
- **What is seen:** every call that goes through the engine's reflection
  (`ProcessEvent`: C++ callers, RPCs, Blueprint events, delegates, `call`),
  with parameters and return value decoded by name. A Blueprint graph
  calling native code passes its own frame; the row has the object only
  (`via: vm`). Direct C++ calls and a Blueprint calling another Blueprint
  never reach the pointer and are not seen; `calls-start` says so in `note`
  for a function that is not native. Record a native function it calls, or an
  event that enters it.
- **Values** decode as a copy: FNames and objects are named, but strings,
  arrays, sets and maps print their headers, since the caller may have freed
  that memory after the call.
- The frame layout it reads is stock UE5, not described by an SDK (see
  `engine_layout.h`), and fault-guarded. Bounds: [limits](../protocol.md#limits).

## line-trace

Console only, no agent op of its own: `KismetSystemLibrary.LineTraceSingle`
through `call`, its hit decoded by the pure `GameplayStatics.BreakHitResult`.
Both signatures are checked by parameter type and order before anything is
written, and parameters are matched by position, never by name (games
obfuscate names); a mismatch is refused. Ends are a point or an object's
location (or a socket's); the trace runs in `ignore`'s world with
`bIgnoreSelf`, else in an end object's world, so two points need `ignore`.
The row is not `writes`: a line trace changes no state.

## game-thread

The host's game-thread tick through the game viewport's Draw
(`reagentlive::ue-draw`, `packs/ue/host_draw.h`), for a host that ticks
elsewhere. `on` arms it; the host installs on its next tick once the pack
has located the engine: the first live `GameViewportClient` has its Draw
interface pointed at a private copy of its vtable with the Draw entry
swapped: one pointer written in the viewport object, the game's code and
tables untouched. The hook forwards every call, then ticks the agent on the
game thread; `status` then says `host_thread: engine` and the
[game-thread commands](unreal.md#off-the-engines-thread) work.

- A table outside the game's image, or the Draw entry not in its code, is
  refused (`conflict`). `off` puts the viewport's table back if it still
  holds the copy. The state is `status`'s `game_thread`, read with no prompt.
- Marked `writes`. Agents whose host does not link `reagentlive::ue-draw`
  answer unsupported. How an agent confirms Draw per game: the skill's
  [game-thread page](../../.claude/skills/reagentlive/references/unreal/game-thread.md);
  host side: [hosting](../hosting.md#game-thread-tick).
