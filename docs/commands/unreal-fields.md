# Unreal fields

*Home of:* reading reflected fields: `get` in every mode (paths, filters, layouts), compact values and `meta`, `entries`, `rows` (data and curve tables), text values, readable names, native members (`member-name`), and layouts in bulk (`layout-export`, `layout-check`).

**TL;DR:** `get` reads an object's fields by name: every field with its
value, named paths through pointers and structs, the layout with no
values, or a snapshot and then what changed. Values print compact
(`{"X": 1.5, "Y": -2, "Z": 3}`, names as text, objects as refs).
`entries` pages a container, `rows` a data or curve table, and
`layout-export` writes many classes' layouts at once. Rules every pack
command shares: [Unreal pack](unreal.md).

## get

One reader of reflected fields, in modes; one row type
([reference](reference.md#get)):

```json
{"verb": "get", "args": {"object": "0x1f2e3d4c50#123:4", "view": {"nonzero": true}}}
{"verb": "get", "args": {"object": "0x1f2e3d4c50#123:4", "paths": [["Info", "Owner"], ["Health"]]}}
{"verb": "get", "args": {"object": "Character", "mode": "types", "view": {"match": "Move"}}}
{"verb": "get", "args": {"object": "0x1f2e3d4c50#123:4", "mode": "snap"}}
```

Under every mode is the field table: a class or struct lists its own
properties, anything else its class's, walking the chain's reflected
properties (bounded depth and count). It is read through object range
reads, 4 KB per request within the first 64 KB from the object's address,
each checked to be the same object: one freed or replaced mid-read fails
the command instead of printing mixed bytes. Use it on instances
(including `Default__` objects); on a class object the values are
meaningless.

### Paths

`paths` resolves reflected names (or readable names; `"Owner:Field"` as a
step where ambiguous) through the field tables, descends into structs,
follows object, class and weak object pointers, and returns typed values:
`rows` in the order asked, each `path`, `value` and `address`, or `error`
in the value's place (a failed path keeps its row, so positions never
shift; a name that is no field lists the closest ones); `holder` (the
object of the last field) only when the path crossed a pointer to reach
it, `truncated` only when true; plus `same_tick`. Every path is resolved
first (field tables and pointer hops, which may span ticks), then the
values are read back to back, each guarded on its object; `same_tick`
says whether those reads shared one tick. `meta` adds each value's `type`
and `hex`. Up to 16 paths of up to 8 steps. A dependent chain (read a
pointer, read through it) only works inside one call like this, where the
console sees each response before the next request.

### Every field and its filters

With no `paths`, every field:

- **A row is its name, offset and value**: the readable name where one was
  recovered, with the reflected one beside it as `raw`; `before` with
  `mode` `changed`, `object` for a named pointer, `preview` for a
  container, `truncated` only when true. The layout (owner, type, size,
  bool mask) comes as `meta` with `meta` true.
- **Filters:** `view.match` (field name or readable label, any case),
  `view.nonzero` (a bool tests its own bit, not the byte it shares) and
  `own` (only the fields the object's own class declares, none it
  inherits; an instance's class name arrives cut at 47 characters, so
  owners are compared that far). Filtered rows cost no pointer lookups or
  array previews; the result adds `filtered` (`shown`, `total`).
- **`mode` `snap` and `changed`:** `snap` keeps the object's bytes as the
  session's baseline (or in a snapshot `file`); `changed` lists the fields
  that differ from it, each with `before`. Refused with no baseline, or
  when the reflected field layout differs from the baseline's. `before`
  decodes the snapshot's bytes as a copy: names and objects are looked up,
  but nothing the snapshot points into is read (arrays, sets, maps and
  strings print their headers) and no text goes to the engine, because
  that memory may be freed since. Snapshots over time:
  [capture](capture.md).
- **Containers** with a known layout get a preview beside their header:
  the count and the first eight elements (`view.elements` for more), a
  map's as `key` and `value`, read as pages of `entries`, up to 16
  containers per call, header and elements in one tick. Empty arrays and
  arrays with unavailable metadata keep their header.
- **Pointers:** a top-level pointer field or element stays an address in
  `value` and is named beside it (`object`, a map's `key_object` and
  `value_object`); pointers inside values are named in place.

### mode types

`mode` `types` is the layout with no values: per field its offset (hex,
as SDK dumps print it), size (element size × array dimension), property
type, name and declaring class. Games may obfuscate property names;
offsets, sizes and types stay real. Single `TArray` fields include the
reflected element type and stride; single struct fields and arrays of
structs carry the struct's ref and name (the console shows `Vector` in
place of `StructProperty`; `get` with `mode` `types` on that ref lists its
members). Bool rows carry their byte and bit mask, so bitfield bools
sharing a byte are told apart. `subtype` names the class an object-like
property points to or the enum an enum or byte property holds (a
container's element or key; `value_subtype` a map value's). Sets and maps
report the `stride` `entries` needs.

## Compact values

Every decoded value (`get` in every mode, `entries`, `each`, `rows`, the
outputs of a `call` with `params`) prints compact:

- **Structs** as `{Name: value, ...}`, nested the same way
  (`{"X": 1.5, "Y": -2, "Z": 3}`), with a member's readable name where the
  [names file](#readable-names) has one, through the struct type's field
  table, up to three levels and 16 members per struct; the rest counted
  under `"..."`, a name taken twice gets `@<offset>`. Deeper levels,
  unknown structs and members outside the value's bytes fall back to hex.
- **Names:** an FName is its text (`Name_N` for an instance, `#index` when
  the pool has none), resolved in one batch across fields, structs and
  previews; a struct holding only a name (a gameplay tag) is that name.
- **Strings:** an `FString` is its text (UTF-16 printed as UTF-8, up to
  4096 characters); a text value is its display string
  ([text values](#text-values)).
- **Soft and instanced:** a soft object or class pointer is
  `{path, object}` (the asset path, and the loaded object named or null);
  an instanced struct is `{struct, value}`, decoded with its struct's
  members.
- **Inside `get` and `each` values** (as in returned arrays,
  [named calls](unreal-calls.md#named-calls)): arrays, sets and maps are
  read (up to 8 elements each, `view.elements` up to 256, 3 deep, then
  `truncated`; a map as `{count, rows: [{key, value}]}`, its header
  checked as `entries` checks it, else hex with `container_error`), a tag
  container is the list of its tags, and object, class and weak pointers
  are named (`{name, class, ref}`), up to 112 distinct per call: past that
  a pointer stays an address and the result counts `unnamed`. A weak
  pointer that no longer resolves is `gone`, or `unverified` when the
  answer did not come from the engine's thread. Named objects carry their
  literal ref to pass to the next call.

**`"meta": true`** on a call (inside a `batch` per item, or on the batch
for all) restores the layout: a struct as `members` (`name`, `owner`,
`type`, `offset`, `size`, `value`) with `truncated`, plus `meta` (the
field's layout) on every-field `get` rows, `type` and `hex` on path rows,
`hex` on one-column `each` rows and `meta` (the parameter's layout) on the
outputs of a `call` with `params`; containers show their headers. `get`
with `mode` `types` is the layout mode and has no compact form. JSON keys
print sorted.

## entries

One page of a reflected `TArray`, `TSet` or `TMap` field on a live
instance, header and elements read in the same tick (as many as fit
4 KB). Arrays page by index; sets and maps by slot, skipping free slots,
so `view.start` takes the printed `next_cursor`. Sets and maps need the
layout `get` with `mode` `types` reports (`stride`): the header is
checked first (slot count against capacity, one allocation bit per slot,
the free count against the bits) and a failed check is refused with its
reason. Keys and values decode as [compact values](#compact-values).

## rows

`rows` on a data table lists a `DataTable`'s rows: `{name, address, value}`
per row, `value` decoded with the table's `RowStruct` as returned arrays
are ([named calls](unreal-calls.md#named-calls)), with the same
`view.fields`, `view.start` and `view.limit`; `view.match` keeps row names
containing text, any case, and `total` counts what it kept. `RowStruct` is
found by its name, its readable name, or, where names are obfuscated, as
the one object field the `DataTable` class declares. `RowMap` is
unreflected (`get` shows only `RowStruct`): the console reads it as the
stock UE5 `TMap<FName, uint8*>` right after `RowStruct`, 24-byte slots,
and checks the sparse header as `entries` does before trusting it; a
table whose header fails is refused with "RowMap is not the stock UE5
...". Row names keep their instance suffix (`Row_2`).

`rows` on a curve table reads a `CurveTable` (or a subclass) instead: no
`RowStruct`, so the console checks the class and reads the unreflected
`RowMap` (`TMap<FName, FRealCurve*>`) and `CurveTableMode` at the offsets
of the engine layout's `curvetable` group (`engine_layout.h`). The result
says `curve: simple|rich`; each row carries `keys`, `[t, v]` per key for a
simple curve (with its one `interp` mode on the row) and
`[t, v, interp, arrive, leave]` for a rich one (`weighted: true` when its
tangents are weighted). `at` adds each row's `value` at that time,
evaluated as the engine does: the end key's value outside the keys,
otherwise the earlier key's mode (constant holds, linear blends, cubic is
the Bezier of the two keys' values and tangents; weighted tangents are
evaluated unweighted). `view.fields` and `view.nonzero` apply to data
tables only, `at` to curve tables only. An object that is neither is
refused with "neither a data table ... nor a curve table ...".

## Text values

`get`, `each` and decoded rows (returned arrays, `rows`) show a
`TextProperty` as its display string, at any depth: `"Assault Rifle"`.
With `meta` (top-level only) it says how:
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

## Readable names

Games may obfuscate reflected property names. A host can supply a names file
(format in `frontends/cli/state/names.h`: owner, offset, size, bool mask, name), loaded
from `ConsoleConfig::namesFile` beside the console or `--names <file>`.
A `get` row (every field or `mode` `types`) is then named `ControlRotation`
with `raw` `_qkzvxwmpltrnhdbgf`; struct members show the readable name
alone. Paths (`get`, `each`, `watch-start`) and `Owner:Field` accept
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

- **`member-name`** appends one line
  (`{"verb": "member-name", "args": {"object": "0x1f2e3d4c50#123:4", "offset": "0x538", "type": "f64[3]", "name": "Start"}}`),
  so naming a member is one call.
  It is filed under the class that declares the reflected field before the
  offset, or under `Class` when that class declares one of the fields.
  Refused when it overlaps a reflected field or ends past the class size
  the agent reports. A later line for the same owner and offset replaces
  an earlier one; a trailing `?` marks an unconfirmed name. The result is
  the member as the file now holds it.
- **`get`** shows the members of every class whose fields it lists as
  rows marked `native: true` (with `mode` `types`, `type` as written, `f64[3]`),
  decoded by that type; its filters and `mode` `changed` apply,
  and `get` takes the name as the last step of a path on the object
  (`paths` `[["Start"]]`). Rows sort by offset. A line that overlaps a reflected
  field or lies outside the class size is not shown: `native_refused`
  lists it with its `line` and `reason` (`get` fails with the same).
- **Stale.** Patches move native offsets, and reflected names follow their
  field while native lines do not. Each line keeps the main image's PE
  timestamp from when it was labelled; under another one the member still
  shows, with `stale: true` on its row and value, until it is labelled
  again.
- It names and decodes scalars and fixed arrays; no nested structs, no
  inheritance. Real layouts stay with the static tool and the host's SDK.

## Layouts in bulk

`layout-export` (`file`, up to 256 `classes`: classes, structs or enums,
`closure`) writes JSON lines, each with a `kind`, for `layout-check` and
for a static tool's types (the [IDA loader](ida-loader.md#types)):

- `class` or `struct`: `name`, `super` (its parent, when it has one) and
  `size` (`"0x.."`, the inherited part included), then one `field` line
  per field the type itself declares, by offset.
- `field`: `owner`, `field`, `offset` (`"0x.."`; a bitfield bool's byte),
  `size`, `type`, and where they apply `bool_mask`, `struct` (a struct
  property's struct), `class` (the class an object, class, soft or
  interface property points to), `enum` (an enum property's or a byte
  property's enum), `element` (arrays and sets) or `key` and `value`
  (maps) as `{type, size, struct?, class?, enum?}`, and `stride` and
  `value_offset` (`"0x.."`) for a set's or map's slot.
- `enum`: `name`, `size` (the bytes a field gives it, when one does) and
  `values`, `[[name, value], ...]` in the engine's order, names as stored.

Types come in the order found, enums last. `closure` also exports what
the named types' fields reference, up to that many steps (1..8; 1024
types, then `capped`), each with its super chain, and every enum a written
field names; a type past the depth stays a name in its field lines. Names
that are no class, struct or enum come back in `missing`.

`layout-check` reads the field lines of its `file` (written by
`layout-export`, by hand, or by a tool from an SDK; lines with no `kind`
are field lines; `size` and `type` are optional; `class`, `struct` and
`enum` lines are skipped) and answers only what the live layout does not
hold: rows of `owner`, `field`, `problem` (`no class`, `no field`,
`offset`, `size`, `type`, checked in that order), `expected` and `found`,
at most 200 (`capped`), with `ok` when every line holds. Each class costs
one lookup and one field table, both cached for the agent instance, so
re-checking a mod's offsets after a game patch is one command, not one
`get` with `mode` `types` per class.
