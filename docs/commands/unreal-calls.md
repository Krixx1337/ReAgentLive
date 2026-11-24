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

The function is searched up the object's class chain, optionally
restricted to the declaring class (`Owner:Function`). A raw parameter
block must equal the function's size exactly (`bad request`, `detail` =
the expected size); the console sends a zeroed block when none is given
and the call fills every parameter, and otherwise names the input it
needs. Refused off the game thread; called with `ProcessEvent` and the
function's own flags; the block after the call comes back.

- **Owned results:** when the return value or a non-const out parameter
  is not plain data (weak handles, strings, arrays), the agent keeps the
  block per function (up to 8) and the next call starts from its outputs,
  so the engine releases the old values on assignment; inputs, const
  references included, always come from the request. `kept` reports it,
  and the console adds `kept_reason`, naming the output and what it owns.
  A full table refuses a new function (`release` one); entries are never
  evicted, because they still own references.
- **Faults:** `ProcessEvent` runs under a fault guard; an access fault
  inside the engine answers `access faulted` ("call faulted: the target
  may be in a bad state", `detail` = the object) instead of ending the
  process, and a fresh kept block is dropped. Nothing else is caught, and
  a fault can leave engine state half-changed: treat the session as
  suspect afterwards.
- **`dry`** resolves the function and builds the block, raw or typed, and
  calls nothing: `mode: dry`, `hex` is the block that would be sent, with
  `exec` and `pure` (const or blueprint-pure). Show it when asking for
  approval.

`release` is one more call with zeroed inputs (or the given block) that
drops the function's kept block, so the engine releases what it held;
`not found` when nothing is kept.

### call-pure

As `call`, with the agent's pure check first (`CallPure`): the function
must be const or blueprint-pure and no output may own memory (nothing is
kept), otherwise it is refused before calling ("not a const or pure
function: use call", "an output owns memory: use call"). Its row is not
`writes`, so its tool is read-only. With no `params` and no `block` the
call is typed all the same, so its outputs come back decoded, as
`call-each`'s do; inputs are never zeroed for you (a zero can be a valid,
state-changing argument), so a function with inputs is refused until
`params` names them. `block` keeps the raw form. Pure is the engine's flag, not a
proof: a pure function can still change state; the skill's approval rules
say which to call freely (not the first call on a new host or in a new
game, since a wrong engine layout can crash the game).

### call-each

`call-pure`'s check for every live instance of a class, in one tick on the
game thread, answered as [`each`](unreal-objects.md#each)'s table (`mode:
call`). The function's parameters must all be plain outputs or the return
value (no inputs, no const refs), block size 1..64 bytes. Plain: nothing
to destroy and zero is a valid start (`NoDestructor` and `ZeroConstructor`
on the property or, where it lacks one, on every member of its struct: a
gameplay tag is plain, a tag container is not); a function with inputs is
refused with `call-pure` (read-only, one object, `params`) named first and
`call` after it; anything else is refused
and stays with `call`, the message naming the parameter and why ("
`ReturnValue` is a `GameplayTagContainer`, which owns `GameplayTags` (an
array)"); a parameter the names file knows is named by its readable name,
the reflected one in the error's `raw`. Instances gone since the scan are skipped. A verb of its own
because its result is `each`'s table, not `call`'s.

## Named calls

A typed call's `params` names its inputs by the exact reflected parameter
names `func` prints under `params`:

```json
{"verb": "call", "args": {"object": "0x1f2e3d4c50#123:4", "function": "SetTarget", "params": {"Bone": "Head", "Location": {"X": 1, "Y": 2, "Z": 3}}}}
```

Supported values:

- FNames as text (`"Head"`: the pooled name, compared without case,
  instance 0, looked up once per session through `name-find`) or as
  `"#index[:number]"` (the console checks the index). A name the pool lacks
  is refused with its text: "no pooled name is 'X' (parameter P)";
- a struct whose only member is a name (a gameplay tag) as the name alone:
  `"ResourceTag": "Some.Tag.Name"`, also inside a struct
  (`"Entry": {"Count": 2, "Tag": "Some.Tag.Name"}`); `{"TagName": ...}`
  works too;
- bools as `true`/`false` (or `1`/`0`; a bitfield bool sets only its bit);
- signed and unsigned integers of every width, as numbers or `"0x..."`
  text, range-checked; bytes and enums as unsigned integers;
- floats and doubles as numbers, finite only;
- plain structs as an object by member name or a list in member order,
  nesting up to four levels: `{"X": 1, "Y": 2, "Z": 3}` or `[1, 2, 3]`.
  Members left out stay zero;
- object and class pointers as a literal ref or an address (the object is
  described first, so a gone one is refused), or `null` for None.

`params` `{}` makes a typed call with no inputs. Inputs must be plain single
values; outputs may own memory (an array, a string): they start zeroed, as
a raw call with no block sends them, and the agent keeps the block. A
const reference (`const FVector&`) is an input: the engine also marks it
`OutParm`, so the agent passes its `ConstParm` and the console counts it
as an input, not an output. Other references are refused. Return and out
values are [compact values](unreal-fields.md#compact-values). Host
approval rules still apply. Use a raw `block` for other input layouts
(strings, arrays, weak pointers).

**Returned arrays** (the return value or an out parameter) come as rows:
`{count, capacity, address, rows, next_cursor}`, each element read from
the engine's memory and decoded with its reflected struct, nested arrays
included (up to 256 elements each, 3 deep, then `truncated`), tag
containers as lists of tags. The agent keeps the block that owns the
array ([owned results](#call)), so the memory stays valid until the next
call of that function. A `view` pages them: `view.limit` rows per page
(default 100, 0 for all), `view.start` from a page's `next_cursor`, and
`view.fields` keeps only those members, a path going into structs and the
elements of nested arrays (`[["ItemTag"], ["Variations", "Price"]]`); a
path naming no member is refused with the members there; a path into a
map goes through `key` or `value` (`[["Damage", "value"]]`).
`view.nonzero` leaves out the zero members inside the outputs (0, false,
empty, `None`; elements stay, so a list keeps its positions, and one
emptied by the filter prints `{"zeroed": N}`), as it does inside `get`,
`each` and `rows` values. A view makes the call typed. Only `call` takes
one: an array owns memory, so `call-pure` refuses it.

## func and funcs

`func` resolves a function by exact name on the object's class chain or
the supplied class itself, validates the function metadata and checks the
exec entry's page protection. It reports the declaring class, function
name, UFunction address, flags, the current exec entry (with its module
and RVA when it lies in a listed module; a null or nonexecutable entry is
shown as such) and the reflected parameters (`params`: name, type, offset,
direction, flags) with the call block's size (`block_size`). The
parameters need the game thread: off it, `params_error` says so and the
rest stands. An executable entry can be a generated wrapper, so follow it
separately when locating native game logic.

**Native interfaces:** with `Interface:Function` on an object, a function
declared on an interface its class (or a parent) implements is found
through the class's `Interfaces` array (`EngineLayout::interfaces`,
`{UClass*, int32 PointerOffset}` entries, read live). An SDK does not
describe it, so each array is checked first: every entry must be a
`UInterface` subclass whose part lies inside the implementing class's
size; otherwise `func` refuses with "interfaces array implausible" rather
than trusting any entry. `interface` then reports the interface part's
offset in the object, the vtable slot its stub calls (decoded from the
stub's `mov rax, [reg]; call [rax + disp]`) and the implementation the
object's interface vtable holds there, with its RVA, ready to
disassemble. `call` refuses these: the stub calls that slot on the object
it gets without moving `this` to the interface part, so through
`ProcessEvent` it would run another function.

`funcs` lists owner-qualified function names on the class chain, derived
class first, then those declared on each native interface the class (or a
parent) implements, each interface once, marked `via: "interface"`, with
`view.match` and a next-page cursor. Interface rows use the same
plausibility check as `func`; an implausible interfaces array lists none.
An interface row is never callable on the object; use `func` with
`Interface:Function` for its implementation.

## Call recorder

`calls-start` records a reflected function's calls (`owner` an object or
a class, `function`, `duration`, with `object` and `dry` optional);
`calls-read` returns them so far, `calls-stop` ends it early. A host
offers it by listing the three ops; the start is a write the host
approves.

- **What is written:** `UFunction::Func` (`EngineLayout::execFunction`), the function's
  native pointer, swapped by compare-and-swap for one of 8 fixed thunks.
  No code, vtable or import. At the end (time limit, at most 60 s;
  `calls-stop`; host stop) the original goes back only if the pointer
  still holds the thunk. Otherwise something else swapped it since: it is
  left alone (`state: replaced`) and the thunk keeps forwarding, which is
  safe because the host's image stays loaded. `dry` shows the pointer
  and thunk without writing. A stopped slot is reused only a second after
  its restore, oldest first: a caller that loaded the thunk just before
  the restore can still enter it, and must find the old original there.
- **What is seen:** every call that goes through the engine's reflection.
  Through `ProcessEvent` (C++ callers, RPCs received, Blueprint events,
  delegates, `call`), the frame's `Node` is the function and its locals
  are the parameter block: the row holds the parameters after the call
  plus the return value (`ProcessEvent` writes that to the caller's block),
  decoded by name as `in`, `out` and `return`. Inputs are as the function
  left them. A Blueprint graph calling native code passes its own frame;
  the row has the object only (`via: vm`). Direct C++ calls never reach
  the pointer and are not seen. A Blueprint event with an event-graph
  redirect runs the graph function, not the event's pointer.
- **Layout:** `FFrame::Node`, `::Locals` and
  `UFunction::ReturnValueOffset` (`frameNode`, `frameLocals`,
  `returnValueOffset` in `engine_layout.h`) are stock UE5 values an SDK
  does not describe; confirmed in `UObject::ProcessEvent`, the `FFrame`
  constructor and `UFunction::Invoke` of one build. Reads of them are
  fault-guarded.
- **Bounds:** [limits](../protocol.md#limits); older rows are dropped and
  counted. Rows carry the sequence, milliseconds since the start, thread
  id and the calling object (named while it lives). `object` keeps only
  calls on one object.
- **Values** decode through the shared reader, as a copy (as `get`'s
  `before`): FNames and objects are named, but strings, arrays, sets and
  maps print their headers, since the caller may have freed that memory
  after the call; text is not converted.
- **Blueprint functions:** a call from another Blueprint runs the callee's
  bytecode without its pointer, so it is not seen; `calls-start` says so in
  `note` when the function is not native. Record a native function it
  calls, or an event that enters it.

## line-trace

Console only, no agent op of its own: `KismetSystemLibrary.LineTraceSingle`
through `call` on `Default__KismetSystemLibrary`, its hit decoded by the
pure `GameplayStatics.BreakHitResult` (`call-pure`). Both signatures are
checked by parameter type and order before anything is written; a
mismatch is refused. Parameters are matched by position, never by name
(games obfuscate names). `BreakHitResult` has 18 outputs in UE5 and 16 in
UE4 (no `BoneName`, `ElementIndex`); both are mapped. `bone` is
`HitBoneName`, the bone hit.

- **Ends** are a point `[x, y, z]` in cm, an object's location
  (`GetCameraLocation`, `K2_GetComponentLocation` or
  `K2_GetActorLocation`, the first the object has; a camera manager is
  also an actor, whose own location is not the view point, all through
  `call-pure`) or `{"object": <ref>, "socket": "Name"}`
  (`SceneComponent.GetSocketLocation`, the name looked up as text).
- The trace runs in `ignore`'s world with `bIgnoreSelf` (that actor and
  its components are skipped), else in an end object's world, skipping
  nothing; two points need `ignore`. Complex collision unless `simple`;
  `channel` is the trace type query (0 visibility, 1 camera in a stock
  project). No debug drawing. The row is not `writes`: a line trace
  changes no state.

## game-thread

The host's game-thread tick through the game viewport's Draw
(`reagentlive::ue-draw`, `packs/ue/host_draw.h`), for a host that ticks
elsewhere. `on` arms it; the host installs on its next tick once the pack
has located the engine: the first live `GameViewportClient` (found with
`FirstLiveInstance`) has its Draw interface (`ViewportInterfaceOffset` in
`engine_layout.h`) pointed at a private copy of its vtable with the Draw
entry (`ViewportDrawIndex`) swapped: one pointer written in the viewport
object, the game's code and tables untouched. The hook forwards every
call, then ticks the agent only for that viewport on the process's first
thread; `status` then says `host_thread: engine` and the
[game-thread commands](unreal.md#off-the-engines-thread) work.

- A table outside the game's image, or the Draw entry not in its code, is
  refused (`conflict`). `off` puts the viewport's table back if it still
  holds the copy.
- `action` is `on` or `off`; there is no read form. The state (`off`,
  `waiting`: armed, not installed yet, `conflict`, `on`, `stopped`, with
  the game thread, the viewport, its table and the game's Draw as module
  and RVA) is `status`'s `game_thread`, read with no prompt.
- Marked `writes`. Agents whose host does not link `reagentlive::ue-draw`
  answer unsupported. How an agent confirms Draw per game: the skill's
  [game-thread page](../../.claude/skills/reagentlive/references/unreal/game-thread.md);
  host side: [hosting](../hosting.md#game-thread-tick).
