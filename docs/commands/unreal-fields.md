# Unreal fields

*Home of:* reading reflected fields: `get` in every mode (paths, filters, layouts), snapshots across runs (`snap`, `changed`), compact values and `meta`, `entries`, `rows` (data and curve tables), text values, readable names, native members (`member-name`), and layouts in bulk (`layout-export`, `layout-check`).

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
properties, anything else its class's, walking the chain. It is read through
object range reads, each checked to be the same object: one freed or
replaced mid-read fails the command instead of printing mixed bytes. Use it
on instances (including `Default__` objects); on a class object the values
are meaningless.

### Paths

`paths` resolves reflected names (or readable names; `"Owner:Field"` as a
step where ambiguous), descends into structs, follows object, class and
weak object pointers, and returns typed values in the order asked. A failed
path keeps its row with an `error` (a name that is no field lists the closest
ones), so positions never shift. All paths are resolved first, then the
values are read back to back, each guarded on its object; `same_tick` says
whether those reads shared one tick. A dependent chain (read a pointer, read
through it) only works inside one call like this, where the console sees
each response before the next request.

### Every field and its filters

With no `paths`, every field: its name (the readable one where recovered,
with the reflected one beside it as `raw`), offset and value. The layout
comes as `meta` with `meta` true.

- **Filters:** `view.match` (field name or readable label), `view.nonzero`
  (a bool tests its own bit, not the byte it shares) and `own` (only the
  fields the object's own class declares). Filtered rows cost no pointer
  lookups or array previews; the result adds `filtered`.
- **`mode` `snap` and `changed`:** to see what changes when something
  happens, take the baseline on a live instance, act, then ask for the
  changes; `changed` prints the fields that differ, each with `before`:

  ```json
  {"verb": "get", "args": {"object": "0x1f2e3d4c50#123:4", "mode": "snap"}}
  {"verb": "get", "args": {"object": "0x1f2e3d4c50#123:4", "mode": "changed"}}
  ```

  It is refused with no baseline, or when the ref or the reflected field
  layout differs from the baseline's. `before` decodes the snapshot's bytes
  as a copy: names and objects are looked up, but nothing the snapshot points
  into is read (arrays, sets, maps and strings print their headers), because
  that memory may be freed since. Fields outside the first 64 KB are skipped.
  The last snapshot is kept in the cache file, so `changed` works in a later
  run against the same agent instance (a snapshot over 1 MB stays in its
  run); a snapshot `file` (on both calls) keeps a baseline past the agent
  instance and beside a later snapshot. Which command answers which question
  about change: the skill's capture-and-timing reference.
- **Containers** with a known layout get a preview beside their header (the
  count and the first elements, `view.elements` for more; a map's as `key`
  and `value`). Pointers: a top-level pointer stays an address in `value`
  and is named beside it; pointers inside values are named in place.

### mode types

`mode` `types` is the layout with no values: per field its offset, size,
property type, name and declaring class. Games may obfuscate property names;
offsets, sizes and types stay real. A struct field carries the struct's ref
(`get` with `mode` `types` on it lists its members), bool rows their byte and
bit mask, `subtype` the class or enum a property points to, and sets and maps
the `stride` that `entries` needs.

## Compact values

Every decoded value (`get` in every mode, `entries`, `each`, `rows`, the
outputs of a `call` with `params`) prints compact:

- **Structs** as `{Name: value, ...}`, with a member's readable name where
  the [names file](#readable-names) has one. Deep levels and unknown structs
  fall back to hex.
- **Names:** an FName is its text; a struct holding only a name (a gameplay
  tag) is that name. An `FString` is its text; a text value is its display
  string ([text values](#text-values)).
- **Soft and instanced:** a soft pointer is `{path, object}`; an instanced
  struct is `{struct, value}`.
- **Containers and pointers** inside `get` and `each` values are read and
  named (`{name, class, ref}`), within caps: what is cut shows as
  `truncated`, pointers left unnamed are counted as `unnamed`. A weak pointer
  that no longer resolves is `gone`, or `unverified` off the engine's
  thread. Named objects carry their literal ref to pass to the next call.

**`"meta": true`** on a call (per item in a `batch`, or on the batch) restores
the layout: structs as `members` with owner, type, offset and size, `type`
and `hex` on path rows. `mode` `types` is the layout mode and has no compact
form.

## entries

One page of a reflected `TArray`, `TSet` or `TMap` field on a live instance,
header and elements read in the same tick. Arrays page by index; sets and
maps by slot, so `view.cursor` takes the printed `next_cursor`. Sets and maps
need the layout `mode` `types` reports (`stride`): the header is checked
first and a failed check is refused with its reason.

## rows

`rows` lists a `DataTable`'s rows: `{name, address, value}` per row, `value`
decoded with the table's `RowStruct`, with the same `view` keys as returned
arrays ([named calls](unreal-calls.md#named-calls)); `view.match` keeps row
names containing text. `RowMap` is unreflected (`get` shows only
`RowStruct`): the console reads it as the stock UE5 `TMap<FName, uint8*>`
right after `RowStruct` and checks the sparse header as `entries` does; a
table whose header fails is refused, never guessed. Nested arrays decode up
to 256 elements and 3 levels deep, names resolved.

On a `CurveTable` the same verb reads the unreflected `RowMap` and
`CurveTableMode` at the offsets of the engine layout's `curvetable` group
(`engine_layout.h`); each row carries its keys, and `at` adds each row's
value at that time, evaluated per key `interp` (constant, linear, cubic; the
end keys' values outside them). A `simple` curve's rows carry `interp`; a
`rich` curve's keys are `[t, v, interp, arrive, leave]`. `view.fields` and
`view.nonzero` apply to data tables only, `at` to curve tables only.

## Text values

`get`, `each` and decoded rows show a `TextProperty` as its display string,
at any depth. An `FText` holds a pointer to engine-private text data whose
layout changes between engine builds, so no read-only decode is reliable:
the console calls the engine's own converter instead
(`Conv_TextToString` through `call`, read-only by contract), once per
distinct value. Where the host cannot call, the value keeps its bytes with
`text_error` saying why.

## Readable names

Games may obfuscate reflected property names. A host can supply a names file
(format in `frontends/cli/state/names.h`), loaded from
`ConsoleConfig::namesFile` beside the console or `--names <file>`. A `get`
row is then named `ControlRotation` with `raw` `_qkzvxwmpltrnhdbgf`; paths
(`get`, `each`, `watch-start`) and `Owner:Field` accept either name. A size
or bit mismatch keeps the raw name, so a stale file never labels a moved
field. Snapshot files keep raw names.

### Native members

Members reflection does not list (a block of state inside a `Pad_` array)
get names too, so a finding stays found. They live in their own hand-owned
file beside the console, `<console>.native.txt` (format in
`frontends/cli/state/native_names.h`), because the host regenerates the
names file and would wipe appended lines
([design](../design/console-costs.md)).

- **`member-name`** appends one line (`{"verb": "member-name", "args":
  {"object": "0x1f2e3d4c50#123:4", "offset": "0x538", "type": "f64[3]",
  "name": "Start"}}`), using the [typed layout](memory.md#typed-layouts)
  syntax. It is refused when it overlaps a reflected field or ends past the
  class size; a later line for the same owner and offset replaces an earlier
  one; a trailing `?` marks an unconfirmed name.
- **`get`** shows the members as rows marked `native: true`, decoded by their
  type; filters, `mode` `changed` and a path's last step apply. A line that
  overlaps a reflected field or lies outside the class is not shown:
  `native_refused` lists it.
- **Stale.** Patches move native offsets, and reflected names follow their
  field while native lines do not. Each line keeps the main image's PE
  timestamp from when it was labelled; under another one the member still
  shows, with `stale: true`, until it is labelled again.
- Scalars and fixed arrays only: no nested structs, no inheritance. Real
  layouts stay with the static tool and the host's SDK.

## Layouts in bulk

`layout-export` (`file`, `classes`, `closure`) writes JSON lines, each with
a `kind` (`class`, `struct`, `field`, `enum`), for `layout-check` and for a
static tool's types (the [IDA loader](ida-loader.md#types)). `closure` also
exports what the named types' fields reference, up to that many steps; names
that are no class, struct or enum come back in `missing`. The line keys are
defined by the writer (`packs/ue/cli/results/layout.h`), not here.

`layout-check` reads the field lines of its `file` (written by
`layout-export`, by hand, or by a tool from an SDK) and answers only what the
live layout does not hold: rows of `owner`, `field`, `problem`, `expected`
and `found`, with `ok` when every line holds. So re-checking a mod's offsets
after a game patch is one command, not one `get` with `mode` `types` per
class.
