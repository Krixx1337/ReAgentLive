# Unreal pack

Requires a Dumper-7 SDK and a host that calls `Tick` on the game thread.
Each command's flags and result keys: `help <verb>` or
[result shapes](results.md).

| Command | Guard |
|---|---|
| `find [text] [--cls C] [--start N]` | objects whose full name (`Base_N`) contains `text` (case-insensitive) and/or whose class or a parent class is named `C` (case-insensitive; `--cls Character` includes vendors); pages of 24 from the object-table slot `N` and the next page's cursor; at most 1 ms of scanning per tick (`Progress::Continue`). The first page scans the whole table and counts the total; a later page starts scanning at its cursor and stops once full, so paging a broad class is linear, not quadratic. While scanning, an object only needs a readable header (one `VirtualQuery` per allocator region per tick) and a matching table index; only matches get the host's full `Context::object` check. Decoded base names are cached by FName index for the process (the name pool never changes an entry), so repeated finds skip almost all name decoding |
| `call <object> <[Class:]Function> [hex]` | object resolved by `Ref` (a stale ref is `not found`; an address-only ref is identified first); function searched up the class chain, optionally restricted to the declaring class; parameter block must equal the function's size exactly (`bad request`, `detail` = expected size); the console sends a zeroed block of that size when none is given and the call fills every parameter (the return value, out parameters not passed by reference), and otherwise names the input it needs; refused off the game thread; called with `ProcessEvent` and the function's own flags; returns the block after the call. **Owned results:** when the return value or a non-const out parameter is not plain data (as `each` defines it: weak handles, strings, arrays), the agent keeps the block per function (up to 8) and the next call starts from its outputs, so the engine releases the old values on assignment; inputs, const references included, always come from the request. `kept` reports it. A full table refuses a new function (`release` one); entries are never evicted, because they still own references. With `kept`, the console adds `kept_reason`, naming the output and what it owns |
| `release <object> <[Class:]Function> [hex]` | one more call with zeroed inputs (or the given block) that drops the function's kept block, so the engine releases what it held; `not found` when nothing is kept |
| `func <object\|class> <[Owner:]Function>` | resolves a function by exact name on the object's class chain or the supplied class itself; validates the function metadata and checks the exec entry's page protection on the game thread. **Native interfaces:** with `Interface:Function` on an object, a function declared on an interface its class (or a parent) implements is found through the class's `Interfaces` array (UE5 `UClass+0x1E0`, `{UClass*, int32 PointerOffset}` entries, read live). The offset is not in the SDK, so each array is checked first: every entry must be a `UInterface` subclass whose part lies inside the implementing class's size; otherwise `func` refuses with "interfaces array implausible" rather than trusting any entry. `interface` then reports the interface part's offset in the object, the vtable slot its stub calls (decoded from the stub's `mov rax, [reg]; call [rax + disp]`) and the implementation the object's interface vtable holds there, with its RVA, ready to disassemble. `call` refuses these: the stub calls that slot on the object it gets without moving `this` to the interface part, so through `ProcessEvent` it would run another function |
| `funcs <object\|class> [text] [--start N]` | lists owner-qualified function names on the class chain, derived class first, with a case-insensitive substring filter and a next-page cursor |
| `params <object\|class> <[Owner:]Function>` | resolves the function as for `func`, then reads its reflected parameters, directions and block size |
| `fields <object>` | object resolved as for `call`; a class or struct lists its own properties, anything else its class's; walks the chain's reflected properties (bounded depth and count); bool properties report the byte holding their bit and the bit mask |
| Object range read | used by `snap` and `record`; checks the game thread and resolves the full ref on each request, then uses the core guarded read. Up to 4 KB per request within the first 64 KB from the object's address |
| `describe <address\|$N>` | checks the game thread and object table before returning the object's name, class and full ref. Weak pointers must match the table slot and serial |
| `name <index>...` | up to 1024 indices; the console pages them to the agent. FName comparison indices as base text, including gameplay tags and DataTable row names; checks the game thread; an undecodable index returns empty |
| Array preview | used by `dump`; resolves the instance on the game thread, reads the array header and a bounded prefix of its backing storage on the same tick |
| `census [--cls C]` | one table scan (1 ms per tick) counting live instances per exact class, optionally only classes under `C`; class default and archetype objects are skipped. Each row carries the class ref, its name, the count and up to three sample refs; most instances first. The console reads every page (later pages come from the agent's cached result) and pages its own rows ([census](#census-changes)) |
| `name-find <text> [--start N]` | walks the engine's name pool (base names, including gameplay tags and row names no object carries) for a case-insensitive substring; 64 matches per page with the next comparison index as cursor; checks the game thread. The pool and its entry layout are the ones located before the first command ([below](#before-the-first-command)) |
| `each <Class> <Path\|[Owner:]Function>...` | lists the live instances of the class or a subclass (the scan spans ticks; up to 1024, then `capped`), then reads one span (up to 64 bytes) or calls one function for every instance in a single tick on the game thread; the console turns several columns into passes ([each](#each)). A call needs a const or blueprint-pure function whose parameters are all plain outputs or the return value (no inputs, no const refs), block size 1..64 bytes. Plain: nothing to destroy and zero is a valid start (`NoDestructor` and `ZeroConstructor` on the property or, where it lacks one, on every member of its struct: a gameplay tag is plain, a tag container is not); anything else is refused and stays with `call`, the message naming the parameter and why (an input, or what keeps it from being plain data: "`ReturnValue` is a `GameplayTagContainer`, which owns `GameplayTags` (an array)"). Instances gone since the scan are skipped. A repeat with the reuse flag skips the scan |
| `script <object\|class> <[Owner:]Function>` | reads the function's bytecode (`UStruct::Script`, a `TArray<uint8>` at `+0x60` in UE5) and scans it anchored: a value counts only right after its opcode byte. Names follow `EX_NameConst` (0x21, a 12-byte script name), functions follow `EX_FinalFunction` (0x1C), `EX_LocalFinalFunction` (0x46) or `EX_CallMath` (0x68), and properties follow `EX_LocalVariable` (0x00), `EX_InstanceVariable` (0x01) or `EX_LocalOutVariable` (0x48). Every entry is confirmed before it is listed: a name must decode, a function must be a live UFunction in the object table, and a property pointer's field class name must end in `Property`. Repeats are folded into `count`. The opcodes are UE5's public `Script.h` values (the SDK has no `EExprToken`); each was seen live before a value of its kind. A native function has no bytecode (`not found`). Not a disassembler: control flow and operands of other opcodes are not decoded |
| `entries <object> <[Owner:]Field> [--start N]` | one page of a reflected `TArray`, `TSet` or `TMap` field on a live instance, header and elements read in the same tick. Arrays page by index; sets and maps by slot, skipping free slots, so `--start` takes the printed `next_cursor`. Sets and maps need the layout `fields` reports (`stride`): the header is checked first (slot count against capacity, one allocation bit per slot, the free count against the bits) and a failed check is refused with its reason. Keys and values use the field formatter (structs expanded, FNames resolved in one lookup, object pointers named in one describe-many) |
| `refs <name\|#index>` | scans every function's bytecode for `EX_NameConst` with that comparison index, 1 ms per tick like `census`; later pages come from the cached result; up to 512 functions (`capped`). Text must match a pooled name exactly (the console resolves it through `name-find`) |

- **Object identity:** an object-table slot plus its serial. Serial 0 means
  not assigned yet and agrees with a later serial for the same slot and
  address.
- **Host hooks:** `Context::object` validates every object the pack touches;
  `Context::gameThread` is required and must return true before any pack
  handler runs. A context without it is refused by every handler (`host
  gave no engine-thread check`), never treated as being on the right
  thread; one returning false is refused as `not on the game thread`.
- **Faults inside engine calls are not caught.**
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

Names come from the located pool, not the SDK's `FName::ToString` (which
calls into the engine at an address the patch may have moved): base name,
the path before the last `/` dropped, `_<number - 1>` added for a numbered
name; wide characters above 0x7f read `?`.

## Named calls

Named calls take the exact reflected parameter names printed by `params`.
Supported inputs are FNames as `#index[:number]` and byte or enum values
as unsigned integers. Use `--typed` for a call with no inputs. The console
validates the name index, requires plain single-value parameters and
refuses references. It formats return and out values by name. Host
approval rules still apply. Use the raw hex form for other layouts.

## Class names

`fields`, `dump`, `func`, `funcs` and `params` also take a bare class or
struct name (`fields Character`). The console finds the object of
that exact name under `Struct` once per agent instance (one table scan)
and reuses its ref through the cache file.

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
