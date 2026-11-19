# Unreal pack

Requires a host that calls `Tick` on the game thread, and for most
commands a Dumper-7 SDK; `find`, `describe`, `name`, `name-find`,
`census` and object range reads also work without one
([without an SDK](#without-an-sdk)). Each command's flags and result keys:
`help <verb>` or [result shapes](results.md).

| Command | Guard |
|---|---|
| `find [text] [--cls C] [--start N]` | objects whose full name (`Base_N`) contains `text` (case-insensitive) and/or whose class or a parent class is named `C` (case-insensitive; `--cls Character` includes vendors); pages of 24 from the object-table slot `N` and the next page's cursor; at most 1 ms of scanning per tick (`Progress::Continue`; the host may allow more, `Context::scanMicroseconds`, up to 50 ms). The first page scans the whole table and counts the total; a later page starts scanning at its cursor and stops once full, so paging a broad class is linear, not quadratic. While scanning, an object only needs a readable header (one `VirtualQuery` per allocator region per tick) and a matching table index; only matches get the host's full `Context::object` check. Decoded base names are cached by FName index for the process (the name pool never changes an entry), so repeated finds skip almost all name decoding |
| `call <object> <[Class:]Function> [hex]` | object resolved by `Ref` (a stale ref is `not found`; an address-only ref is identified first); function searched up the class chain, optionally restricted to the declaring class; parameter block must equal the function's size exactly (`bad request`, `detail` = expected size); the console sends a zeroed block of that size when none is given and the call fills every parameter (the return value, out parameters not passed by reference), and otherwise names the input it needs; refused off the game thread; called with `ProcessEvent` and the function's own flags; returns the block after the call. **Owned results:** when the return value or a non-const out parameter is not plain data (as `each` defines it: weak handles, strings, arrays), the agent keeps the block per function (up to 8) and the next call starts from its outputs, so the engine releases the old values on assignment; inputs, const references included, always come from the request. `kept` reports it. A full table refuses a new function (`release` one); entries are never evicted, because they still own references. With `kept`, the console adds `kept_reason`, naming the output and what it owns. **Faults:** `ProcessEvent` runs under a fault guard; an access fault inside the engine answers `access faulted` ("call faulted: the target may be in a bad state", `detail` = the object) instead of ending the process, and a fresh kept block is dropped. Nothing else is caught, and a fault can leave engine state half-changed: treat the session as suspect afterwards. **`--dry`** (console only) resolves the function and builds the block, raw or typed, and calls nothing: `mode: dry`, `hex` is the block that would be sent, with `exec` and `pure` (const or blueprint-pure). Show it when asking for approval |
| `call-pure <object> <[Class:]Function> [hex\|Name=value]` | as `call`, with the agent's pure check first (flag `CallPure`): the function must be const or blueprint-pure and no output may own memory (nothing is kept), otherwise it is refused before calling ("not a const or pure function: use call", "an output owns memory: use call"). Unlike `call`, its row is not `writes`, so its tool is read-only. Pure is the engine's flag, not a proof: a pure function can still change state; the skill says which to call freely |
| `release <object> <[Class:]Function> [hex]` | one more call with zeroed inputs (or the given block) that drops the function's kept block, so the engine releases what it held; `not found` when nothing is kept |
| `func <object\|class> <[Owner:]Function>` | resolves a function by exact name on the object's class chain or the supplied class itself; validates the function metadata and checks the exec entry's page protection on the game thread. **Native interfaces:** with `Interface:Function` on an object, a function declared on an interface its class (or a parent) implements is found through the class's `Interfaces` array (UE5 `UClass+0x1E0`, `{UClass*, int32 PointerOffset}` entries, read live). The offset is not in the SDK, so each array is checked first: every entry must be a `UInterface` subclass whose part lies inside the implementing class's size; otherwise `func` refuses with "interfaces array implausible" rather than trusting any entry. `interface` then reports the interface part's offset in the object, the vtable slot its stub calls (decoded from the stub's `mov rax, [reg]; call [rax + disp]`) and the implementation the object's interface vtable holds there, with its RVA, ready to disassemble. `call` refuses these: the stub calls that slot on the object it gets without moving `this` to the interface part, so through `ProcessEvent` it would run another function |
| `funcs <object\|class> [text] [--start N]` | lists owner-qualified function names on the class chain, derived class first, then those declared on each native interface the class (or a parent) implements, each interface once, marked `via: "interface"`, with a case-insensitive substring filter and a next-page cursor. Interface rows use the same plausibility check as `func`; an implausible interfaces array lists none. An interface row is never callable on the object (`call` refuses it); use `func Interface:Function` for its implementation |
| `params <object\|class> <[Owner:]Function>` | resolves the function as for `func`, then reads its reflected parameters, directions and block size |
| `fields <object>` | object resolved as for `call`; a class or struct lists its own properties, anything else its class's; walks the chain's reflected properties (bounded depth and count); bool properties report the byte holding their bit and the bit mask |
| Object range read | used by `snap` and `record`; checks the game thread and resolves the full ref on each request, then uses the core guarded read. Up to 4 KB per request within the first 64 KB from the object's address |
| `describe <address\|$N>` | checks the game thread and object table before returning the object's name, class and full ref. Weak pointers must match the table slot and serial |
| `name <index>...` | up to 1024 indices; the console pages them to the agent. FName comparison indices as base text, including gameplay tags and DataTable row names; checks the game thread; an undecodable index returns empty |
| Array preview | used by `dump`; resolves the instance on the game thread, reads the array header and a bounded prefix of its backing storage on the same tick |
| `census [--cls C]` | one table scan (1 ms per tick) counting live instances per exact class, optionally only classes under `C`; class default and archetype objects are skipped. Each row carries the class ref, its name, the count and up to three sample refs; most instances first. The console reads every page (later pages come from the agent's cached result) and pages its own rows ([census](#census-changes)) |
| `name-find <text> [--start N]` | walks the engine's name pool (base names, including gameplay tags and row names no object carries) for a case-insensitive substring; 64 matches per page with the next comparison index as cursor. The pool and its entry layout are the ones located before the first command ([below](#before-the-first-command)) |
| `each <Class> <Path\|[Owner:]Function>...` | lists the live instances of the class or a subclass (the scan spans ticks; up to 1024, then `capped`), then reads one span (up to 64 bytes) or calls one function for every instance in a single tick on the game thread; the console turns several columns into passes ([each](#each)). A call needs a const or blueprint-pure function whose parameters are all plain outputs or the return value (no inputs, no const refs), block size 1..64 bytes. Plain: nothing to destroy and zero is a valid start (`NoDestructor` and `ZeroConstructor` on the property or, where it lacks one, on every member of its struct: a gameplay tag is plain, a tag container is not); anything else is refused and stays with `call`, the message naming the parameter and why (an input, or what keeps it from being plain data: "`ReturnValue` is a `GameplayTagContainer`, which owns `GameplayTags` (an array)"). Instances gone since the scan are skipped. A repeat with the reuse flag skips the scan |
| `script <object\|class> <[Owner:]Function>` | reads the function's bytecode (`UStruct::Script`, a `TArray<uint8>` at `+0x60` in UE5) and scans it anchored: a value counts only right after its opcode byte. Names follow `EX_NameConst` (0x21, a 12-byte script name), functions follow `EX_FinalFunction` (0x1C), `EX_LocalFinalFunction` (0x46) or `EX_CallMath` (0x68), and properties follow `EX_LocalVariable` (0x00), `EX_InstanceVariable` (0x01) or `EX_LocalOutVariable` (0x48). Every entry is confirmed before it is listed: a name must decode, a function must be a live UFunction in the object table, and a property pointer's field class name must end in `Property`. Repeats are folded into `count`. The opcodes are UE5's public `Script.h` values (the SDK has no `EExprToken`); each was seen live before a value of its kind. A native function has no bytecode (`not found`). Not a disassembler: control flow and operands of other opcodes are not decoded |
| `entries <object> <[Owner:]Field> [--start N]` | one page of a reflected `TArray`, `TSet` or `TMap` field on a live instance, header and elements read in the same tick. Arrays page by index; sets and maps by slot, skipping free slots, so `--start` takes the printed `next_cursor`. Sets and maps need the layout `fields` reports (`stride`): the header is checked first (slot count against capacity, one allocation bit per slot, the free count against the bits) and a failed check is refused with its reason. Keys and values use the field formatter (structs expanded, FNames resolved in one lookup, object pointers named in one describe-many) |
| `trace <start> <end> [--channel C] [--simple] [--ignore A]` | console only, no agent op of its own: `KismetSystemLibrary.LineTraceSingle` through `call` on `Default__KismetSystemLibrary`, its hit decoded by the pure `GameplayStatics.BreakHitResult` (`call-pure`). Both signatures are checked by parameter type and order before anything is written; a mismatch is refused. Parameters are matched by position, never by name (games obfuscate names). `BreakHitResult` has 18 outputs in UE5 and 16 in UE4 (no `BoneName`, `ElementIndex`); both are mapped. `bone` is `HitBoneName`, the bone hit. Ends are a point `x,y,z` in cm, an object's location (`GetCameraLocation`, `K2_GetComponentLocation` or `K2_GetActorLocation`, the first the object has; a camera manager is also an actor, whose own location is not the view point, all through `call-pure`) or `object@Socket` (`SceneComponent.GetSocketLocation`, the name looked up as text). The trace runs in `--ignore`'s world with `bIgnoreSelf` (that actor and its components are skipped), else in an end object's world, skipping nothing; two points need `--ignore`. Complex collision unless `--simple`; `--channel` is the trace type query (0 visibility, 1 camera in a stock project). No debug drawing. The row is not `writes`: a line trace changes no state |
| `refs <name\|#index>` | scans every function's bytecode for `EX_NameConst` with that comparison index, 1 ms per tick like `census`; later pages come from the cached result; up to 512 functions (`capped`). Text must match a pooled name exactly (the console resolves it through `name-find`) |

- **Object identity:** an object-table slot plus its serial. Serial 0 means
  not assigned yet and agrees with a later serial for the same slot and
  address.
- **Host hooks:** `Context::object` validates every object the pack touches;
  `Context::gameThread` is required and must return true before any pack
  handler runs. A context without it is refused by every handler (`host
  gave no engine-thread check`), never treated as being on the right
  thread; one returning false is refused as `not on the game thread`.
- **Faults inside engine calls** (`call`, `call-pure`, `each` with a
  function): an access fault is caught and answered `access faulted`; the
  engine may be left in a bad state. Other exceptions are not caught.
- **Globals:** every command first needs the object table and name pool
  located and checked ([below](#before-the-first-command)); until then it
  is refused with `pack off: <reason>`.
- **`fields` rows:** offset, size (element size × array dimension), property
  type, name and declaring class. Games may obfuscate property names; offsets,
  sizes and types stay real. Single TArray fields include the reflected element
  type and stride when available. Single struct fields and arrays of structs
  carry the struct's ref and name; the console shows the name (`Vector`) in
  place of `StructProperty`. Bool rows carry their bit mask, so bitfield bools
  sharing a byte are told apart.

## Before the first command

The pack takes layouts from the SDK (object, class, field and table types)
but never addresses, so a dump made before a game patch still works once
the globals are found again
([design](../design.md#unreal-pack)). The first pack command, on the
engine's thread, locates them:

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

`reagentlive::ue-base` is the part of the pack that compiles with no SDK
([hosting](../hosting.md#cmake-targets)): one prebuilt agent for any UE5
game, with no dump. Its ops are `find`, `describe` (and its batch form),
object range reads, `name`, `census` and `name-find`; a host lists only
those, so the rest answer `unsupported op`. Fields, calls, containers and
bytecode need the SDK's types.

The layout comes from the engine, checked like the globals:

- **Table and object header:** UE5's chunked table (64K items per chunk)
  and header (flags `+0x08`, index `+0x0C`, class `+0x10`, name `+0x18`,
  outer `+0x20`), with the item in one of three shapes (24 bytes with the
  object first or after an 8-byte field, or 16 bytes). Each is tried at
  every candidate address; the table check above decides, so a wrong shape
  is refused, never used. An engine that moved the header turns the pack
  off with `table: no layout fits`.
- **`UStruct::SuperStruct`:** the first pointer field after the header of
  the class of classes whose chain reads `Struct`, `Field`, `Object`, then
  null. Unknown, `find --cls` and `census --cls` match the exact class only.
- **`UStruct::Size`:** the int32 after `SuperStruct` that holds the header
  size (0x28) in `Object` and one pointer more in `Field`. Unknown,
  `describe` reports no class size.
- **Serials** are not known: every ref carries serial 0, which agrees with
  any serial, so a ref is checked by slot and address only.
- **The first command scans:** there are no address guesses without an
  SDK, so the first pack command scans the image's writable sections for
  both globals, a stall of up to seconds on the ticking thread.

## Off the engine's thread

A host that ticks elsewhere (a borrowed frame thread) may set
`Context::offThreadReads`. Being measured
([backlog 18](../ideas.md#18--best-effort-pack-reads-off-the-engines-thread)):
it stays only if a live run under load is clean.

- **Answered:** `find`, `describe` (and its batch form), `fields`, object
  range reads (`read` of an object ref, `dump`, `get`), array previews,
  `entries`, `names`, `census` and `name-find` (the same header and pool
  reads as `find`). Locating the globals also runs there.
- **Still refused** (`not on the game thread`): `call`, `each`, `script`,
  `refs`, `exec-entries`, `funcs`, `func`, `params`: they run engine code
  or walk tables this has not measured.
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
  `#index[:number]` (the console checks the index);
- bools as `true`/`false`/`1`/`0` (a bitfield bool sets only its bit);
- signed and unsigned integers of every width, decimal or `0x` hex,
  range-checked; bytes and enums as unsigned integers;
- floats and doubles in decimal or exponent form, finite only;
- plain structs as `(Member=value, ...)` or `(value, ...)` in member order,
  nesting up to four levels: `Location=(X=1,Y=2,Z=3)`. Members left out
  stay zero.

Use `--typed` for a call with no inputs. Parameters must be plain single
values. A const reference (`const FVector&`) is an input: the engine also
marks it `OutParm`, so the agent passes its `ConstParm` and the console
counts it as an input, not an output. Other references are refused.
Return and out values are formatted by name, structs member by member.
Host approval rules still apply. Use the raw hex form for other layouts
(strings, arrays, object pointers).

## Call recorder

`calls-start <object|Class> <[Owner:]Function> <duration> [--object R] [--dry]`
records a reflected function's calls; `calls-read` returns them so far,
`calls-stop` ends it early. A host offers it by listing the three ops; the
start is a write the host approves.

- **What is written:** `UFunction::Func` (UE5 `+0xD8`), the function's
  native pointer, swapped by compare-and-swap for one of 8 fixed thunks.
  No code, vtable or import. At the end (time limit, at most 60 s;
  `calls-stop`; host stop) the original goes back only if the pointer
  still holds the thunk. Otherwise something else swapped it since: it is
  left alone (`state: replaced`) and the thunk keeps forwarding, which is
  safe because the host's image stays loaded. `--dry` shows the pointer
  and thunk without writing.
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
- **Layout:** `FFrame::Node` `+0x10`, `::Locals` `+0x28` and
  `UFunction::ReturnValueOffset` `+0xB8` are UE5 values, not in the SDK;
  confirmed in `UObject::ProcessEvent`, the `FFrame` constructor and
  `UFunction::Invoke` of one build. Reads of them are fault-guarded.
- **Bounds:** 8 recordings at once, 256 rows each (older ones are dropped
  and counted), parameter blocks up to 1 KiB. Rows carry the sequence,
  milliseconds since the start, thread id and the calling object (named
  while it lives). `--object` keeps only calls on one object.

## Class names

`fields`, `dump`, `func`, `funcs` and `params` also take a bare class or
struct name (`fields Character`). The console finds the object of
that exact name under `Struct` once per agent instance and reuses its ref
through the cache file. The lookup is an exact `find` (`FindExact`): the
agent stops at the first match instead of scanning and counting the whole
table, so a class costs the slots up to it; only a name that is no class
costs a full scan. An agent built before it treats the name as a substring
and counts, as before: slower, the same answer.

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
`{ref, values}`, one value per column in the order asked. The same shape
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
  class is looked up; a class target is its own).
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
  number. Arrays nested inside structs show their headers only.
- With agent support for `describe`, `dump` names single object, class and
  weak object pointers. It looks up at most 32 distinct pointers in field
  order, and 32 more among array elements: one describe-many request each
  (one `describe` per pointer on an agent without it). Unresolved pointers
  keep their raw values. `describe` and `dump`
  append reusable `$N` refs without renumbering existing refs. The next
  successful `find` replaces the list.
- Array previews show up to eight elements per array and up to 16 arrays
  per dump; sets and maps with a known layout get the same preview (the
  occupied count and the first eight entries, a map's as `key` and `value`),
  through `entries`, for elements up to 512 bytes (fewer when eight would exceed
  4 KB). The agent reads the header and elements on the same tick, so
  short-lived entries are caught consistently. Elements use the usual value
  formatter, structs expanded. Object
  elements can get named refs with a separate budget of 32 distinct lookups.
  Empty arrays and arrays with unavailable metadata keep their header.
- Struct values expand through `fields` on the struct type, as
  [compact values](console.md#fewer-round-trips) (`{"X": -928370, "Y": -861551, "Z":
  -81104}`; `record` files: `(X -928370, Y -861551, Z -81104)`). Nested structs expand up to
  three levels and 16 members per struct; deeper levels, unknown structs and
  members outside the value's bytes fall back to hex. Struct layouts are
  cached for the console session. `dump --changed`, `record` and struct array elements
  use the same expansion.

## Labels for a static tool

`export-labels <file> [--module M]
[--kinds exec,vtable,interface,global]` (default the exe and every kind;
a kind not asked for costs no requests) rewrites the file as JSON lines `{module, module_size,
timestamp, rva, name, kind}`, one per RVA, for one build (image size and
PE timestamp): every native function's exec thunk (`Class::execName`,
`exec`), each class default object's vtable (`vtbl_Class`, named after a
native class where one shares it with Blueprint classes, `vtable`), native
interface implementations found through `func` on native class default
objects (`Class::Interface_Function`, `interface`, at most 4096 lookups,
`capped` past that), and the globals `peek --label` put in the file for
this build (`global`, kept first, whatever `--kinds` says: only
`peek --label` makes them). No blind scan of globals. Another tool's IDA
mappings (Dumper-7's) are optional: imported first, they name exec
thunks, vtables and types, and `--kinds interface,global` adds only what
needs the running process.

`frontends/ida/import_labels.py`, run inside IDA, refuses another build,
names each address, keeps a name the user set (the label becomes a
comment), makes exec thunks functions, and prints how many rows it named,
commented and skipped (already named so). The result counts what was
written: `total` rows, of which `globals`, `exec`, `vtables` and
`interfaces`.

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
