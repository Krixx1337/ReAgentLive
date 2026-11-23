# Unreal pack

*Home of:* what each Unreal pack command does and its guards, which commands answer off the game thread and which need it, the layout groups checked without an SDK, and locating the engine's globals.

**TL;DR:** The Unreal pack finds objects, reads their fields by name and
calls the game's own functions in any UE5 game, with or without a dump.
Lookups and reads work from any thread the host ticks on; calls and
everything that runs engine code need the game thread ([the list](#off-the-engines-thread)),
which the host gives by ticking there or after `game-thread on`. Without a
dump, each part of the engine layout is checked on objects every UE5 game
has before its commands run, so a non-stock engine refuses instead of
guessing. Where this sits in the ladder: [tiers](../tiers.md).

Every op works without an SDK ([without an SDK](#without-an-sdk)); a host's
SDK, when it has one, only supplies the layout. Each command's flags and
result keys: `help <verb>` or [result shapes](results.md).

| Command | Guard |
|---|---|
| `find [text] [--cls C] [--live] [--start N] [--limit N]` | objects whose full name (`Base_N`) contains `text` (case-insensitive) and/or whose class or a parent class is named `C` (case-insensitive; `--cls Character` includes vendors); `--live` skips class default and archetype objects as `census` does (`FindLive`); pages of 24 from the object-table slot `N` and the next page's cursor (`--limit N`, 1..1000: the console asks for pages until N rows or the end, and the cursor goes on after the last row kept); at most 1 ms of scanning per tick (`Progress::Continue`; the host may allow more, `Context::scanMicroseconds`, up to 50 ms). The first page scans the whole table and counts the total; a later page starts scanning at its cursor and stops once full, so paging a broad class is linear, not quadratic. While scanning, an object only needs a readable header (one `VirtualQuery` per allocator region per tick) and a matching table index; only matches get the host's full `Context::object` check. Decoded base names are cached by FName index for the process (the name pool never changes an entry), so repeated finds skip almost all name decoding |
| `call <object> <[Class:]Function> [hex]` | object resolved by `Ref` (a stale ref is `not found`; an address-only ref is identified first); function searched up the class chain, optionally restricted to the declaring class; parameter block must equal the function's size exactly (`bad request`, `detail` = expected size); the console sends a zeroed block of that size when none is given and the call fills every parameter (the return value, out parameters not passed by reference), and otherwise names the input it needs; refused off the game thread; called with `ProcessEvent` and the function's own flags; returns the block after the call. **Owned results:** when the return value or a non-const out parameter is not plain data (as `each` defines it: weak handles, strings, arrays), the agent keeps the block per function (up to 8) and the next call starts from its outputs, so the engine releases the old values on assignment; inputs, const references included, always come from the request. `kept` reports it. A full table refuses a new function (`release` one); entries are never evicted, because they still own references. With `kept`, the console adds `kept_reason`, naming the output and what it owns. **Faults:** `ProcessEvent` runs under a fault guard; an access fault inside the engine answers `access faulted` ("call faulted: the target may be in a bad state", `detail` = the object) instead of ending the process, and a fresh kept block is dropped. Nothing else is caught, and a fault can leave engine state half-changed: treat the session as suspect afterwards. **`--dry`** (console only) resolves the function and builds the block, raw or typed, and calls nothing: `mode: dry`, `hex` is the block that would be sent, with `exec` and `pure` (const or blueprint-pure). Show it when asking for approval |
| `call-pure <object> <[Class:]Function> [hex\|Name=value]` | as `call`, with the agent's pure check first (flag `CallPure`): the function must be const or blueprint-pure and no output may own memory (nothing is kept), otherwise it is refused before calling ("not a const or pure function: use call", "an output owns memory: use call"). Unlike `call`, its row is not `writes`, so its tool is read-only. Pure is the engine's flag, not a proof: a pure function can still change state; the skill's approval rules say which to call freely (not the first call on a new host or in a new game, the known-answer call, since a wrong engine layout can crash the game) |
| `release <object> <[Class:]Function> [hex]` | one more call with zeroed inputs (or the given block) that drops the function's kept block, so the engine releases what it held; `not found` when nothing is kept |
| `func <object\|class> <[Owner:]Function>` | resolves a function by exact name on the object's class chain or the supplied class itself; validates the function metadata and checks the exec entry's page protection. **Native interfaces:** with `Interface:Function` on an object, a function declared on an interface its class (or a parent) implements is found through the class's `Interfaces` array (`EngineLayout::interfaces` in `packs/ue/engine_layout.h`, `{UClass*, int32 PointerOffset}` entries, read live). An SDK does not describe it, so each array is checked first: every entry must be a `UInterface` subclass whose part lies inside the implementing class's size; otherwise `func` refuses with "interfaces array implausible" rather than trusting any entry. `interface` then reports the interface part's offset in the object, the vtable slot its stub calls (decoded from the stub's `mov rax, [reg]; call [rax + disp]`) and the implementation the object's interface vtable holds there, with its RVA, ready to disassemble. `call` refuses these: the stub calls that slot on the object it gets without moving `this` to the interface part, so through `ProcessEvent` it would run another function |
| `funcs <object\|class> [text] [--start N]` | lists owner-qualified function names on the class chain, derived class first, then those declared on each native interface the class (or a parent) implements, each interface once, marked `via: "interface"`, with a case-insensitive substring filter and a next-page cursor. Interface rows use the same plausibility check as `func`; an implausible interfaces array lists none. An interface row is never callable on the object (`call` refuses it); use `func Interface:Function` for its implementation |
| `params <object\|class> <[Owner:]Function>` | resolves the function as for `func`, then reads its reflected parameters, directions and block size |
| `fields <object>` | object resolved as for `call`; a class or struct lists its own properties, anything else its class's; walks the chain's reflected properties (bounded depth and count); bool properties report the byte holding their bit and the bit mask |
| Object range read | used by `snap` and `record`; checks the thread ([off-thread](#off-the-engines-thread)) and resolves the full ref on each request, then uses the core guarded read. Up to 4 KB per request within the first 64 KB from the object's address |
| `describe <address\|$N>` | checks the thread and object table before returning the object's name, class and full ref. Weak pointers must match the table slot and serial |
| `name <index>...` | up to 1024 indices; the console pages them to the agent. FName comparison indices as base text, including gameplay tags and DataTable row names; checks the thread; an undecodable index returns empty |
| Array preview | used by `dump`; resolves the instance, reads the array header and a bounded prefix of its backing storage on the same tick |
| `census [--cls C] [--outer O]` | one table scan (1 ms per tick) counting live instances per exact class, optionally only classes under `C`; class default and archetype objects are skipped. Each row carries the class ref, its name, the count and up to three sample refs; most instances first. The console reads every page (later pages come from the agent's cached result) and pages its own rows ([census](#census-changes)) |
| `name-find <text> [--start N]` | walks the engine's name pool (base names, including gameplay tags and row names no object carries) for a case-insensitive substring; 64 matches per page with the next comparison index as cursor. The pool and its entry layout are the ones located before the first command ([below](#before-the-first-command)) |
| `each <Class> <Path\|[Owner:]Function>...` | lists the live instances of the class or a subclass (the scan spans ticks; up to 1024, then `capped`), then reads one span (up to 64 bytes) or calls one function for every instance in a single tick on the game thread; the console turns several columns into passes ([each](#each)). A call needs a const or blueprint-pure function whose parameters are all plain outputs or the return value (no inputs, no const refs), block size 1..64 bytes. Plain: nothing to destroy and zero is a valid start (`NoDestructor` and `ZeroConstructor` on the property or, where it lacks one, on every member of its struct: a gameplay tag is plain, a tag container is not); anything else is refused and stays with `call`, the message naming the parameter and why (an input, or what keeps it from being plain data: "`ReturnValue` is a `GameplayTagContainer`, which owns `GameplayTags` (an array)"). Instances gone since the scan are skipped. A repeat with the reuse flag skips the scan |
| `script <object\|class> <[Owner:]Function>` | reads the function's bytecode (`UStruct::Script`, a `TArray<uint8>`, `EngineLayout::script`) and scans it anchored: a value counts only right after its opcode byte. Names follow `EX_NameConst` (0x21, a 12-byte script name), functions follow `EX_FinalFunction` (0x1C), `EX_LocalFinalFunction` (0x46) or `EX_CallMath` (0x68), and properties follow `EX_LocalVariable` (0x00), `EX_InstanceVariable` (0x01) or `EX_LocalOutVariable` (0x48). Every entry is confirmed before it is listed: a name must decode, a function must be a live UFunction in the object table, and a property pointer's field class name must end in `Property`. Repeats are folded into `count`. The opcodes are UE5's public `Script.h` values (the SDK has no `EExprToken`); each was seen live before a value of its kind. A native function has no bytecode (`not found`). Not a disassembler: control flow and operands of other opcodes are not decoded |
| `entries <object> <[Owner:]Field> [--start N]` | one page of a reflected `TArray`, `TSet` or `TMap` field on a live instance, header and elements read in the same tick. Arrays page by index; sets and maps by slot, skipping free slots, so `--start` takes the printed `next_cursor`. Sets and maps need the layout `fields` reports (`stride`): the header is checked first (slot count against capacity, one allocation bit per slot, the free count against the bits) and a failed check is refused with its reason. Keys and values use the field formatter (structs expanded, FNames resolved in one lookup, object pointers named in one describe-many) |
| `rows <object> [--match T] [--fields a,b.c] [--start N] [--limit N] [--nonzero]` | console only, over object and range reads: a data table's rows ([rows](#rows)) |
| `trace <start> <end> [--channel C] [--simple] [--ignore A]` | console only, no agent op of its own: `KismetSystemLibrary.LineTraceSingle` through `call` on `Default__KismetSystemLibrary`, its hit decoded by the pure `GameplayStatics.BreakHitResult` (`call-pure`). Both signatures are checked by parameter type and order before anything is written; a mismatch is refused. Parameters are matched by position, never by name (games obfuscate names). `BreakHitResult` has 18 outputs in UE5 and 16 in UE4 (no `BoneName`, `ElementIndex`); both are mapped. `bone` is `HitBoneName`, the bone hit. Ends are a point `x,y,z` in cm, an object's location (`GetCameraLocation`, `K2_GetComponentLocation` or `K2_GetActorLocation`, the first the object has; a camera manager is also an actor, whose own location is not the view point, all through `call-pure`) or `object@Socket` (`SceneComponent.GetSocketLocation`, the name looked up as text). The trace runs in `--ignore`'s world with `bIgnoreSelf` (that actor and its components are skipped), else in an end object's world, skipping nothing; two points need `--ignore`. Complex collision unless `--simple`; `--channel` is the trace type query (0 visibility, 1 camera in a stock project). No debug drawing. The row is not `writes`: a line trace changes no state |
| `game-thread [on\|off\|status]` | the host's game-thread tick through the game viewport's Draw (`reagentlive::ue-draw`, `packs/ue/host_draw.h`), for a host that ticks elsewhere. `on` arms it; the host installs on its next tick once the pack has located the engine: the first live `GameViewportClient` (found with `FirstLiveInstance`) has its Draw interface (`ViewportInterfaceOffset` in `engine_layout.h`) pointed at a private copy of its vtable with the Draw entry (`ViewportDrawIndex`) swapped: one pointer written in the viewport object, the game's code and tables untouched. The hook forwards every call, then ticks the agent only for that viewport on the process's first thread; `status` then says `host_thread: engine` and the [game-thread commands](#off-the-engines-thread) work. A table outside the game's image, or the Draw entry not in its code, is refused (`conflict`). `off` puts the viewport's table back if it still holds the copy. Modes: `off`, `waiting` (armed, not installed yet), `conflict`, `on`, `stopped`; also the game thread, the viewport, its table and the game's Draw (module, RVA). How an agent confirms Draw per game: the skill's [game-thread page](../../.claude/skills/reagentlive/references/unreal/game-thread.md). Marked `writes`, `status` included, so an MCP client asks before reading the mode. Agents whose host does not link `reagentlive::ue-draw` answer unsupported. Host side: [hosting](../hosting.md#game-thread-tick) |
| `enum <object\|Enum>` | an enum's names and values in its order, named in one `name` request. The agent reads `UEnum::Names` as the SDK has it (pairs of FName and int64) or, as UE 5.6 and later keep them, as two arrays (FNames and int64 values, bit 0 of each pointer a dynamic-allocation tag, at `Names - 8` and `Names`) and an int32 count at `Names + 8`; a layout counts only when every name decodes, else the enum is refused. A Blueprint enum's names are its stored `NewEnumeratorN`, not its display names. Byte and enum properties print numbers; this says what they mean. An enum by name is an exact find under `Enum` (user-defined enums included) |
| `refs <name\|#index>` | scans every function's bytecode for `EX_NameConst` with that comparison index, 1 ms per tick like `census`; later pages come from the cached result; up to 512 functions (`capped`). Text must match a pooled name exactly (the console resolves it through `name-find`) |

- **Object identity:** an object-table slot plus its serial. Serial 0 means
  not assigned yet and agrees with a later serial for the same slot and
  address.
- **Host hooks:** `Context::object` validates every object the pack touches;
  `Context::gameThread` is required and must return true before any pack
  handler runs, except the reads a host opts into with
  `Context::offThreadReads` ([below](#off-the-engines-thread)). A context
  without it is refused by every handler (`host gave no engine-thread
  check`), never treated as being on the right thread; one returning false
  is refused as `not on the game thread`.
- **Faults inside engine calls** (`call`, `call-pure`, `each` with a
  function): an access fault is caught and answered `access faulted`; the
  engine may be left in a bad state. Other exceptions are not caught.
- **Globals:** every command first needs the object table and name pool
  located and checked ([below](#before-the-first-command)); until then it
  is refused with `pack off: <reason>`.
- **`fields` rows:** offset (hex, as SDK dumps print it), size (element size × array dimension), property
  type, name and declaring class. Games may obfuscate property names; offsets,
  sizes and types stay real. Single TArray fields include the reflected element
  type and stride when available. Single struct fields and arrays of structs
  carry the struct's ref and name; the console shows the name (`Vector`) in
  place of `StructProperty`. Bool rows carry their bit mask, so bitfield bools
  sharing a byte are told apart.

## Before the first command

The pack never takes addresses from an SDK, only (with `reagentlive::ue`)
its layouts, so a dump made before a game patch still works once the
globals are found again ([design](../design.md#unreal-pack)). The first
pack command, on whichever thread ticks the agent, locates them:

1. **Object table:** the host's guess (`Context::objectsGuess`), else every
   8-byte aligned address in the image's writable sections whose bytes look
   like a table header. A candidate passes when its counts and chunks are
   plausible, every chunk pointer reads, and each object in the first 64
   slots carries its own slot as its index and has a class chain ending in
   the same class of classes (the one object that is its own class).
2. **Name pool:** the host's guess (`Context::namesGuess`), else the same
   scan for a plausible pool header. A candidate passes when one entry
   layout decodes name 0 as `None` and the class of classes' name as
   `Class`; that layout is used for every name from then on.

Every read on the way is a preflighted, fault-guarded copy, so a garbage
guess is a refusal, never a crash. The host log gets one line with both
addresses and whether each came from the guess or the scan. A failure is
the refusal `pack off: <reason>` (`table: index is not slot; scan: none`)
and a warning line; it is tried again on a command at least 2 s later,
since an early injection can run before the engine has built its globals.
Core commands work either way.

Built without an SDK, the pack also finds the layout the globals are read
with ([without an SDK](#without-an-sdk)); the log line then says
`layout probed` with the item size and the two UStruct offsets.

Names come from the located pool, not the SDK's `FName::ToString` (which
calls into the engine at an address the patch may have moved): base name,
the path before the last `/` dropped, `_<number - 1>` added for a numbered
name; wide characters above 0x7f read `?`.

## Without an SDK

`reagentlive::ue-base` builds every op with no SDK
([hosting](../hosting.md#cmake-targets)): one prebuilt agent for any UE5
game, with no dump (`examples/unreal` is one). Engine types are read at
the offsets of the engine layout (`packs/ue/engine_layout.h`, the one home
of every engine offset): a host's SDK when it links `reagentlive::ue`,
else `StockUE5`. Members an SDK does not describe (the interfaces array,
container layouts, `Script`, the frame) are `StockUE5` in both.

**Layout groups.** Without an SDK, the first op that needs a group checks
it once on objects every UE5 game has, and the group's ops run only if it
passes; otherwise they are refused with the check that failed (e.g.
"field: Vector members are not X, Y, Z"):

| Group | Checked on | Ops |
|---|---|---|
| class | cast flags of `Class`, `Struct`, `Field`, `Object` | every op below |
| struct | `Class` > `Struct` > `Field` > `Object` > null; `Guid` size 16, `Vector` 12 or 24 | fields, params, func, funcs, script, refs, call |
| field | `Vector`'s properties are `X`, `Y`, `Z`, float or double | fields, params, call |
| property | `Vector` and `Guid` member offsets, sizes, `ArrayDim` 1, plain-data flags; `IntProperty`'s field cast flags | fields, params, call |
| typed | every bool on `Actor` (bytes at the bool base), its struct and array properties | fields, params, call |
| function | `KismetMathLibrary:Add_IntInt`: native, parameters 12 bytes, return at 8, exec in code | func, funcs, exec entries, call |
| enum | `ENetRole` decodes to 5 names, `ROLE_None` first | enum |
| call | `UObject`'s vtable slot tests `FunctionFlags` against `FUNC_Native` | call, call-pure, each with functions, the recorder |

`FFrame` offsets (the recorder) and the set, map and interface layouts are
not checked up front: the recorder needs the call group, and each container
and interface array is validated where it is read. A passing group does not
lift the thread rule: a host off the game thread still gets only the
[off-thread commands](#off-the-engines-thread). Finding a layout for an
engine that is not stock: the skill's [new-game page](../../.claude/skills/reagentlive/references/unreal/new-game.md) (the
agent cannot load a layout profile yet:
[ideas](../ideas.md#33--layout-profile-loading)).

The table, header and two struct offsets come from the engine, checked like the globals:

- **Table and object header:** UE5's chunked table (64K items per chunk)
  and object header (flags, index, class, name, outer: the `object` members
  of `StockUE5`), with the item in one of three shapes (24 bytes with the
  object first or after an 8-byte field, or 16 bytes). Each is tried at
  every candidate address; the table check above decides, so a wrong shape
  is refused, never used. An engine that moved the header turns the pack
  off with `table: no layout fits`.
- **`UStruct::SuperStruct`:** the first pointer field after the header of
  the class of classes whose chain reads `Struct`, `Field`, `Object`, then
  null. Unknown, `find --cls` and `census --cls` match the exact class only.
- **`UStruct::Size`:** the int32 after `SuperStruct` that holds the header
  size in `Object` and one pointer more in `Field`. Unknown,
  `describe` reports no class size.
- **Serials** are not known: every ref carries serial 0, which agrees with
  any serial, so a ref is checked by slot and address only. A ref with a
  real serial is the sign of an SDK host.
- **The first command scans** unless the host gives address guesses
  (`Context::objectsGuess`, `namesGuess`; an SDK host usually takes them
  from its dump). A host that gives none, as `examples/unreal`, scans the
  image's writable sections for both globals on its first pack command, a
  stall of up to seconds on the ticking thread.

## Off the engine's thread

This section is the one list of which commands need the game thread; other
pages link here. A host that ticks elsewhere (its own thread, as
`examples/unreal`, or a borrowed frame thread such as Present) may set
`Context::offThreadReads`; then:

| Answer off the game thread (with `offThreadReads`) | Need the game thread |
|---|---|
| `find`, `describe` (and its batch form), `census`, `name`, `name-find`, object reads (`read` of an object ref, `dump`, `get`, `snap`, array previews), `fields`, `entries`, `enum`, `func`, `funcs` | `call`, `call-pure`, `release`, `params`, `each` (every form, fields included), `script`, `refs`, `exec entries` (and `export-labels`, which uses them), the call recorder (`calls-start`, `calls-read`, `calls-stop`), and what rides `call`: `trace` and text values |

Commands that need the game thread are refused off it with `not on the
game thread`. A host gets onto the game thread by ticking there (an SDK
host on the engine's own tick) or, with `reagentlive::ue-draw`, by
`game-thread on`, after which `status` says `host_thread: engine`.
Locating the globals runs on either thread. Without `offThreadReads`, a
host off the game thread gets no pack command at all.

- **Guards:** each answered handler runs under a fault guard, so memory
  freed mid-read is the refusal `fault during an off-thread read` (also a
  log warning), never a crash. The object's identity (slot and serial) is
  checked before and after the read; a change is `object changed during
  the read` and the data is dropped.
- **Not guarded against:** values torn between two fields of one read;
  the console marks `dump`, `get`, `entries` and batches
  `tick_accurate: false`, and `status` explains the host tick.

## Named calls

Named calls take the exact reflected parameter names printed by `params`.
Supported inputs:

- FNames as text (`Name=Head`: the pooled name, compared without case,
  instance 0, looked up once per session through `name-find`) or as
  `#index[:number]` (the console checks the index). A name the pool lacks
  is refused with its text: "no pooled name is 'X' (parameter P)";
- a struct whose only member is a name (a gameplay tag) as the name alone:
  `ResourceTag=Some.Tag.Name`, also inside a struct
  (`Entry=(Count=2, Tag=Some.Tag.Name)`); `(TagName=...)` works too;
- bools as `true`/`false`/`1`/`0` (a bitfield bool sets only its bit);
- signed and unsigned integers of every width, decimal or `0x` hex,
  range-checked; bytes and enums as unsigned integers;
- floats and doubles in decimal or exponent form, finite only;
- plain structs as `(Member=value, ...)` or `(value, ...)` in member order,
  nesting up to four levels: `Location=(X=1,Y=2,Z=3)`. Members left out
  stay zero;
- object and class pointers as `$N`, a literal ref or an address (the
  object is described first, so a gone one is refused), or `None`.

Use `--typed` for a call with no inputs. Inputs must be plain single
values; outputs may own memory (an array, a string): they start zeroed, as
a raw call with no block sends them, and the agent keeps the block. A const reference (`const FVector&`) is an input: the engine also
marks it `OutParm`, so the agent passes its `ConstParm` and the console
counts it as an input, not an output. Other references are refused.
Return and out values are formatted by name, structs member by member,
FNames resolved. Host approval rules still apply. Use the raw hex form for
other input layouts (strings, arrays, weak pointers).

**Returned arrays** (the return value or an out parameter) come as rows:
`{count, capacity, address, rows, next_cursor}`, each element read from
the engine's memory and decoded with its reflected struct, nested arrays
included (up to 256 elements each, 3 deep, then `truncated`), tag
containers as lists of tags. The agent
keeps the block that owns the array ([owned results](#unreal-pack)), so
the memory stays valid until the next call of that function. `--limit N`
rows per page (default 100, 0 for all), `--start N` from a page's
`next_cursor`, and `--fields a,b.c` keeps only those members, a path
going into structs and the elements of nested arrays
(`--fields ItemTag,Variations.Price`); a path naming no member is refused
with the members there; a path into a map goes through `key` or `value`
(`--fields Damage.value`). `--nonzero` leaves out the zero members inside
the outputs (0, false, empty, `None`; elements stay, so a list keeps its
positions, and one emptied by the filter prints `{"zeroed": N}`), as it
does inside `get`, `each` and `rows` values. Any of the
four makes the call typed. They are `call` flags only: an array owns
memory, so `call-pure` refuses it.

## Call recorder

`calls-start <object|Class> <[Owner:]Function> <duration> [--object R] [--dry]`
records a reflected function's calls; `calls-read` returns them so far,
`calls-stop` ends it early. A host offers it by listing the three ops; the
start is a write the host approves.

- **What is written:** `UFunction::Func` (`EngineLayout::execFunction`), the function's
  native pointer, swapped by compare-and-swap for one of 8 fixed thunks.
  No code, vtable or import. At the end (time limit, at most 60 s;
  `calls-stop`; host stop) the original goes back only if the pointer
  still holds the thunk. Otherwise something else swapped it since: it is
  left alone (`state: replaced`) and the thunk keeps forwarding, which is
  safe because the host's image stays loaded. `--dry` shows the pointer
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
- **Bounds:** 8 recordings at once, 256 rows each (older ones are dropped
  and counted), parameter blocks up to 1 KiB. Rows carry the sequence,
  milliseconds since the start, thread id and the calling object (named
  while it lives). `--object` keeps only calls on one object.
- **Values** decode through the shared reader, as a copy (as `dump`'s
  `before`): FNames and objects are named, but strings, arrays, sets and
  maps print their headers, since the caller may have freed that memory
  after the call; text is not converted.
- **Blueprint functions:** a call from another Blueprint runs the callee's
  bytecode without its pointer, so it is not seen; `calls-start` says so in
  `note` when the function is not native. Record a native function it
  calls, or an event that enters it.

## Class names

`fields`, `dump`, `func`, `funcs` and `params` also take a bare class or
struct name (`fields Character`). The console finds the object of
that exact name under `Struct` once per agent instance and reuses its ref
through the cache file. Names up to 127 characters: the agent compares the
whole name, and the reply's name (cut at 63) is compared that far. A name
starting with `Default__` is that default object (an exact find, any
class), so `dump Default__Character` dumps the object. `get`, `rows`,
`entries`, `call` and `call-pure` take a `Default__` name in place of an
object (`get Default__GameSettings Scale`), never a bare class name. The lookup is an exact `find` (`FindExact`): the
agent stops at the first match instead of scanning and counting the whole
table, so a class costs the slots up to it; only a name that is no class
costs a full scan.

## Layouts in bulk

`layout-export <file> <Class>...` (up to 256 names) writes one JSON line
per field each class itself declares: `owner`, `field`, `offset` (as
`"0x.."`), `size`, `type`; names that are no class or struct come back in
`missing`. `layout-check <file>` reads such lines (written by
`layout-export`, by hand, or by a tool from an SDK; `size` and `type` are
optional) and answers only what the live layout does not hold: rows of
`owner`, `field`, `problem` (`no class`, `no field`, `offset`, `size`,
`type`, checked in that order), `expected` and `found`, at most 200
(`capped`), with `ok` when every line holds. Each class costs one lookup
and one field table, both cached for the agent instance, so re-checking a
mod's offsets after a game patch is one command, not one `fields` per
class.

## each

`each <Class> <path>...` is a table: `columns` once, then `rows` of
`{ref, name, values}`, one value per column in the order asked; `name` is
the instance's object name, described for the rows shown (after
`--where` and `--limit`) in a few requests. The same shape
for one column, so the verb keeps one result type.

- **Columns.** Up to 16 field paths, as `get` takes them (`Owner:Field` or
  a readable name, struct members, object, class and weak object
  pointers), or one function (a call per instance, refused beside other
  columns: calls are the slow case and their outputs do not fit a column).
  A single column is tried as a function first, then as a field.
- **Passes.** The paths are resolved on the class. What they need inside
  the instance (a field, or the pointer a path follows) is grouped into
  passes of at most 64 bytes: one agent `each` per pass, the first with
  the scan, the rest over its cached instance list. A path's part past a
  pointer is resolved and read per instance, as `get` does. `same_tick`
  is true when one pass with no pointer path read every value.
- **Failed cells.** A cell that cannot be read (a null pointer on the way,
  an instance gone by a later pass) is null, with an `errors` entry (`ref`,
  `column`, `message`). The row stays, so no instance is hidden.
- **Cost.** The scan of the object table dominates, and a column adds none:
  measured on a table of about 370 000 objects, an `each` spanned about 315
  host ticks (3.5 s at 90 ticks per second) for a field and for a call
  alike, where a `get` took 2.
- **`--seconds N`** (one field or function) repeats every 100 ms on the
  cached instance list and prints each changed row to stderr as it
  happens.
- **`--where Field~text`** keeps the instances whose field's value
  contains `text`, any case: a string, an FName's or a text's string, else
  the value's JSON. The field is read as one more column and shown only
  when it is one; `matched` counts the instances kept. Not with
  `--seconds` or a function column.
- **`Outer`, `Name` and `Class`** name the instance itself, as columns and
  in `--where`, when no field takes the name: one describe-many of the
  instances, then one of their distinct outers. `Outer` is `{name, ref}`.
  They match text: `--where Class~Body` is a substring of the class's own
  name, while `--cls` takes a class and its subclasses.
- **Containers.** An array, set or map column reads its header in the pass
  and its elements per instance after it (8 shown, `--elements N` up to
  256), so `same_tick` is false; a set or map is not held to the 64 bytes.
- **`--limit N`** caps the rows shown (default 100, 0 for all); `omitted`
  counts the rest. Unfiltered output over a whole class is large: locate
  the object first, or filter.

## Object filters

`find`, `each` and `census` take the same filters over the object table,
applied by the agent during its scan, so they cost no round trip per
object:

| Filter | Keeps |
|---|---|
| `--cls C` | instances of `C` or a subclass |
| `--outer O` | objects with `O` up their outer chain (at most 16 steps). `O` is an object when it parses as one (a literal ref, an address, `$N`), checked like any object argument (a gone one is refused as not found, never a filter by whatever reuses its address), else text an outer's name contains, any case (no match is no rows) |
| `--live` (`find`) | no class default or archetype objects (`each` and `census` skip them always) |

`fields --match T` keeps the fields whose name or readable label contains
`T`, any case, as `dump --match` does.

`find` and `describe` rows name each object's `outer` (`{name, ref}`): a
function's or property's class, a subobject's owner; a package has none.

## census

`--match T` keeps rows whose class name contains `T` (case-insensitive), as `dump
--match` does for fields; the scan, `classes` and `instances` stay the
whole census, and `--limit` applies after the filter.

<a id="census-changes"></a>**Pages and changes.** A result shows `from`, `total`
(rows after `--match`) and, while more rows remain, `next_cursor` with a
`hint` (`--start <cursor> for more, --limit 0 for all`). `--start N` is a
later page of the last census with the same `--cls` (of the same verb):
the stored rows, sliced, with no new scan and
`ms` 0, so the pages of one filter join without gaps; refused when there
is none. A census without `--start` scans again. Every census becomes the
baseline for its `--cls` filter, in the session and the cache file
(which keeps the census before it too, for later pages of `census-diff`).

`census-diff [--cls C]` runs a new census and prints only the classes whose count
differs from that baseline, largest change first, with `before` and
`after`; then the new census is the baseline. So: `census --cls C`, the
action, `census-diff --cls C`. It is refused when there is no
baseline for that filter (none taken, another agent instance, or another
`--cls`). `--match` and `--limit` apply to the changed rows. Classes
match by ref. Samples are any live instances, not the new ones: for a
class absent before they are necessarily new, otherwise follow up with
`find --cls`; a class now gone has none.

- A census scans across several ticks, so it is not one moment in time,
  and an object that lives a tick or two can fall between two censuses.
  For those, `watch` a container or use `each --seconds`.
- A broad filter shows unrelated churn (effects, audio, UI). Narrow
  `--cls` to a base class instead.

## func

`func` reports the declaring class, function name, UFunction address, flags
and current exec entry. When the entry lies in a listed module, it also
reports the module-relative RVA. A null or nonexecutable entry is shown
explicitly. The command only reads metadata. An executable entry can be a
generated wrapper, so follow it separately when locating native game logic.

## dump

- **A row is its name, offset and value**: the readable name where one was
  recovered, with the reflected one beside it as `raw`; `before` with
  `--changed`, `object` for a named pointer, `preview` for a container,
  `truncated` only when true. The layout (owner, type, size, bool mask)
  comes as `meta` with `--meta`; `fields` stays the layout command.
- `--own` keeps the fields the object's own class declares (an instance's
  class is looked up; a class target is its own). An instance's class name
  arrives cut at 47 characters, so owners are compared that far.
- The object is read in `read object` chunks, each checked to be the same
  object: one freed or replaced mid-dump fails the command instead of
  printing mixed bytes.
- `dump` is `fields` plus `read`s of the object (4 KB per tick, up to 64 KB),
  formatted by property type (`frontends/cli/values/format.h`): structs expanded,
  bools by mask,
  integers, floats, object pointers, weak refs, FName index and number,
  `TArray`/`FString` headers, hex for everything else. Use it on instances
  (including `Default__` objects); on a class object the values are
  meaningless.
- With agent support for `name`, `dump` batches distinct FName indices across
  fields, expanded structs and array previews. Resolved names include the
  Unreal instance suffix where present. Unresolved names keep their index and
  number. Compact values read arrays, sets and maps nested inside structs
  (8 elements, `--elements N` up to 256, 3 deep), show a tag container as
  its tags and name pointers ([compact values](console.md#fewer-round-trips));
  with `--meta` they show their headers. An `FString` is its text (UTF-16
  read and printed as UTF-8, up to 4096 characters). A soft object or class
  pointer is `{path, object}`: the asset path, and the loaded object named
  or null. An instanced struct (`FInstancedStruct`) is `{struct, value}`:
  its struct type named, its memory decoded with that struct's members.
- Every value goes through the shared reader, as `get` and `each` decode
  it: a top-level pointer field or element stays an address in `value` and
  is named beside it (`object`, a map's `key_object` and `value_object`);
  pointers inside values are named in place. Up to 112 distinct pointers
  are named per dump, the rest counted in `unnamed`. `dump` appends
  reusable `$N` refs without renumbering existing refs; `describe` makes its
  object `$1`. The next successful `find` replaces the list.
- `before` (`--changed`) decodes the snapshot's bytes the same way, as a
  copy: names and objects are looked up, but nothing the snapshot points
  into is read (arrays, sets, maps and strings print their headers) and no
  text goes to the engine, because that memory may be freed since.
- Arrays, sets and maps with a known layout get a preview beside their
  header: the count and the first eight elements (`--elements N` for more),
  a map's as `key` and `value`, read as pages of `entries` (an agent without
  it: an array's 8-element preview), up to 16 containers per dump. The
  agent reads a page's header and elements on the same tick, so short-lived
  entries are caught consistently. Empty arrays and arrays with unavailable
  metadata keep their header.
- Struct values expand through `fields` on the struct type, as
  [compact values](console.md#fewer-round-trips) (`{"X": -928370, "Y": -861551, "Z":
  -81104}`; `record` files: `(X -928370, Y -861551, Z -81104)`). Nested structs expand up to
  three levels and 16 members per struct; deeper levels, unknown structs and
  members outside the value's bytes fall back to hex. Struct layouts are
  cached for the console session. `dump --changed`, `record` and struct array elements
  use the same expansion.

## Text values

`get`, `each`, `dump` and decoded rows (returned arrays, `rows`) show a
`TextProperty` as its display string, at any depth: `"Assault Rifle"`.
With `--meta` (top-level only) it says how:
`{"text": "Assault Rifle", "via": "KismetTextLibrary.Conv_TextToString"}`.
An `FText` holds a pointer to engine-private text data whose layout
changes between engine builds, and a localized text keeps no inline
source string, so no read-only decode is reliable. The console calls the
engine's own converter instead, on `Default__KismetTextLibrary` through
`call` (an engine call, read-only by contract), once per distinct value.
Its `FString` result is kept by the agent and released by the next
conversion, so a session holds at most one. Where the host cannot call
(no `call` op, or a host off the engine's thread), the value keeps its
bytes with `text_error` saying why.

## rows

`rows <data table>` lists a `DataTable`'s rows: `{name, address, value}`
per row, `value` decoded with the table's `RowStruct` as returned arrays
are ([named calls](#named-calls)), with the same `--fields`, `--start` and
`--limit`; `--match` keeps row names containing text, any case, and
`total` counts what it kept. `RowStruct` is found by its name, its
readable name, or, where names are obfuscated, as the one object field
the `DataTable` class declares. `RowMap` is unreflected (`dump` shows only
`RowStruct`): the console reads it as the stock UE5
`TMap<FName, uint8*>` right after `RowStruct`, 24-byte slots, and checks
the sparse header (slot count against capacity, one allocation bit per
slot, the free count against the bits) before trusting it; a table whose
header fails is refused with "RowMap is not the stock UE5 ...". Row
names keep their instance suffix (`Row_2`).

## Labels for a static tool

Labels files carry what ReAgentLive and a static tool know about a build,
both ways. JSON lines `{module, module_size, timestamp, rva, name, kind}`;
one file holds several builds (image size and PE timestamp tell them
apart, and each command uses only the running build's rows). Per address
the highest rank wins: `user` (a name a person gave in the static tool),
`global` (`peek --label`), `exec`, `interface`, then the rest.

- **Derived names:** `<console>.labels.jsonl` beside the console, where
  `export-labels` writes by default: tens of thousands of rows, rebuilt in
  seconds, so it stays local.
- **Names people confirm:** the host's names file
  (`ConsoleConfig::labelsFile`, absolute or relative to the console), where
  `peek --label` writes by default and the static tool's `export_names`
  should write: a host keeps it under version control, so names survive a
  clean build and are shared. Without one, the console's file holds both.
- `disasm --save` reads both files (or `--labels <file>` alone).

`export-labels [file] [--module M]
[--kinds exec,vtable,interface,global]` (default the console's file, the
exe and every kind; a kind not asked for costs no requests) merges into the
file: every native function's exec thunk (`Class::execName`, `exec`), each
class default object's vtable (`vtbl_Class`, named after a native class
where one shares it with Blueprint classes, `vtable`) and native interface
implementations found through `func` on native class default objects
(`Class::Interface_Function`, `interface`, at most 4096 lookups, `capped`
past that). It keeps `user` and `global` rows, this build's rows of kinds
not derived now and every other build's lines (`other_builds`). No blind
scan of globals. Another tool's IDA mappings (Dumper-7's) are optional:
imported first, they name exec thunks, vtables and types, and
`--kinds interface,global` adds only what needs the running process. The
result counts what was written: `total` rows of this build, of which
`globals`, `user`, `exec`, `vtables` and `interfaces`.

`frontends/ida/reagentlive_ida.py` is the IDA side (also the save loader,
[saving a function](fast-lane.md#saving-a-function-for-a-static-tool)):

- `import_labels(path)` (a path, or a list: the console's file and the
  host's names file; File > Script file runs it, asking for the file)
  names a full database of the build (size and timestamp from the PE header
  IDA keeps, so a default load with no header bytes works) or a save
  database (the build its loader kept); the highest-ranked row per address,
  a name you set stays (the label becomes a comment), exec thunks become
  functions. One undo step, a wait box that can cancel it; it prints how
  many rows it named, commented and skipped.
- `export_names(path)` (path: the host's names file) writes every name
  you gave in that database (not
  `sub_…`, `fn_…` or other made-up names, and not names a file brought in)
  as `user` rows of its build, adding or updating them; names it does not
  hold stay (a save database never wipes what another database named).
  The next save, import or export shows them: a finding named once stays
  named, in saves and in the full database alike. A row's other fields
  (`sig`, `ported_from`) stay when it is renamed: they follow the address.

### Names that survive a patch

A row may carry `sig`, a byte signature in `sig-check`'s terms:
`{pattern, offset}` for a function (it lies `offset` bytes before the
match, usually 0) or `{pattern, resolve}` for a global (a reference
signature at code that loads it). With the row's `rva` as the expected
address it is a `sig-file` line. `export-labels` and `export_names` keep it.

`labels-sign [file] [--module M] [--again]` (default the host's names
file, else the console's) signs this build's `user` and `global` rows that
have none (`--again`: all of them). A function's pattern starts at it, a
global's at the first code that loads it; each is the live bytes with
address fields masked (as `sig-bytes` suggests), cut on whole instructions
and grown from 16 bytes (to `unique_at_length` when `sig-check` knows it)
until exactly one match lands on the row, up to 96. Derived rows are not
signed: `export-labels` remakes them. A row it cannot sign gets
`sig_failed` (`timestamp`: the build tried, `reason`), and later calls on
that build skip it and count it in `failed_before`, so a repeated call
searches only for new rows; `--again` tries it too. Signing the row drops
the mark; `labels-port` never copies it.

A row's signature comes one of two ways:

- **Mass signer:** `labels-sign`, the default after naming. Cheap (about
  a second a row, no judgement) and the same bytes always give the same
  signature. It is frozen as one fixed loop: no later starts, caller
  signatures, ranking or looser masks, and struct offsets stay fixed.
- **By hand:** a row it lists `unsigned` (with its `reason`, kept in its
  `sig_failed`), or a name `labels-port` lists `missing` once found again.
  Make the signature as for any other ([signatures](fast-lane.md#signatures)),
  write it into the row's `sig` (removing `sig_failed`) and check the
  file's signatures with `sig-file`. A failure is signed by hand or left
  unsigned, never a reason to extend the loop.

`labels-port [file] [--module M]`, after a patch: per name the running
build lacks, the newest signed row of another build of the module is
matched in the image (one shared search per anchor). One match appends a
row for this build with the same `sig` and `ported_from` (the old
timestamp); `missing` (no match: the code changed) and `ambiguous` (several)
are listed, never written. Old rows stay, `present` counts names this build
already has and `unsigned` names with no signature to port. Re-find the
missing ones, name them, then `labels-sign` the new rows.

## Readable names

Games may obfuscate reflected property names. A host can supply a names file
(format in `frontends/cli/state/names.h`: owner, offset, size, bool mask, name), loaded
from `ConsoleConfig::namesFile` beside the console or `--names <file>`.
`fields` then shows `ControlRotation (_qkzvxwmpltrnhdbgf)` as a row's
`label`, and a `dump` row is named `ControlRotation` with `raw`
`_qkzvxwmpltrnhdbgf`; struct members show the readable name alone. `record` and `Owner:Field` accept
either name. A size or bit mismatch keeps the raw name, so a stale file never
labels a moved field. Snapshot files keep raw names.

### Native members

Members reflection does not list (a block of state inside a `Pad_` array)
get names too, so a finding stays found. They live in their own hand-owned
file beside the console, `<console>.native.txt` (format in
`frontends/cli/state/native_names.h`: owner, offset, a [typed layout](memory.md#typed-layouts)
field's type and count, name, the main image's PE timestamp), loaded with
the names file. The host generates the names file and would wipe appended
lines on its next regeneration, so nothing writes into it.

- **`label <object|Class> <offset> <type>[n] <Name>`** appends one line
  (`label $1 0x538 f64[3] Start`), so naming a member is one command.
  It is filed under the class that declares the reflected field before the
  offset, or under `Class` when that class declares one of the fields.
  Refused when it overlaps a reflected field or ends past the class size
  the agent reports. A later line for the same owner and offset replaces
  an earlier one; a trailing `?` marks an unconfirmed name. The result is
  the member as the file now holds it.
- **`fields`, `dump`, `get`** show the members of every class whose fields
  they list as field rows marked `native: true`, `type` as written
  (`f64[3]`), decoded by that type; `dump` filters and `--changed` apply,
  and `get` takes the name as the last step of a path on the object
  (`get $1 Start`). Rows sort by offset. A line that overlaps a reflected
  field or lies outside the class size is not shown: `native_refused`
  lists it with its `line` and `reason` (`get` fails with the same).
- **Stale.** Patches move native offsets, and reflected names follow their
  field while native lines do not. Each line keeps the main image's PE
  timestamp from when it was labelled; under another one the member still
  shows, with `stale: true` on its row and value, until it is labelled
  again.
- It names and decodes scalars and fixed arrays; no nested structs, no
  inheritance. Real layouts stay with the static tool and the host's SDK.
