# Unreal objects

*Home of:* how the Unreal pack finds and counts objects: `find` and its class index, `describe`, `name`, `name-find`, `enum`, the object filters, `census` and its baselines, `each`, class names as arguments, and `export-assets`.

**TL;DR:** `find` lists objects by name or class, `census` counts live
instances per class (and what changed since the last count), `each` reads
a table of fields over every live instance of a class in one tick, and
`export-assets` writes them to files. Rules every pack command shares:
[Unreal pack](unreal.md); args and results: the [reference](reference.md).

## find

Objects whose full name (`Base_N`) contains `text` (any case) and/or whose
class or a parent class is named `cls` (any case; `cls` `Character`
includes vendors), in pages of 24 from the object-table slot `view.start`.
`view.limit` (1..1000) asks for pages until that many rows or the end.
The first page scans the whole table and counts the total; a later page
starts scanning at its cursor and stops once full, so paging a broad class
is linear, not quadratic. `live` skips class default and archetype objects,
as `census` does.

- **Class index:** with `cls` (and in `each`) the agent visits only the
  slots its class index names: per slot, the object last seen and its
  class. Every query first compares each slot's pointer with the kept one
  and reads only changed slots, then works out which classes match the
  name (kept per name); the whole index is rebuilt once a minute (a new
  object at a freed object's address in the same slot is the one change a
  pass misses until then). A candidate still gets every check below, so a
  stale entry can only hide an object, never add one. `text` alone walks
  the whole table: add `cls` when the class is known.
- **Checks:** while scanning, an object only needs a readable header (one
  `VirtualQuery` per allocator region, the last eight regions kept per
  tick) and a matching table index; only matches get the host's full
  `Context::object` check. Decoded base names are cached by FName index
  for the process (the name pool never changes an entry), so repeated
  finds skip almost all name decoding.

`find` and `describe` rows name each object's `outer` (`{name, ref}`): a
function's or property's class, a subobject's owner; a package has none.

## describe

Checks the thread and object table before returning the object's name,
class and full ref. Weak pointers must match the table slot and serial.

## name and name-find

- **`name`:** FName comparison indices as base text, gameplay tags and
  DataTable row names included; up to 1024 indices, which the console
  pages to the agent. Each index must be where a pool entry starts: its
  block is walked from the first entry (one walk per block per request),
  so an index inside an entry returns empty rather than its text read as
  a header.
- **`name-find`** walks the engine's name pool (base names, including tags
  and row names no object carries) for a substring, any case; 64 matches
  per page with the next comparison index as cursor. Where entries carry a
  kind byte, kinds 1 and 2 are obfuscated: their text is always 22
  characters, zero-padded, while the header keeps the original name's
  length, so each entry takes the longer of the two; a kind-2 entry shows
  its 22 characters, a kind-1 entry the header's length of them.

## enum

An enum's names and values in its order, named in one `name` request. The
agent reads `UEnum::Names` as the SDK has it (pairs of FName and int64)
or, as UE 5.6 and later keep them, as two arrays (FNames and int64 values,
bit 0 of each pointer a dynamic-allocation tag, at `Names - 8` and
`Names`) and an int32 count at `Names + 8`; a layout counts only when
every name decodes, else the enum is refused. A Blueprint enum's names are
its stored `NewEnumeratorN`, not its display names. Byte and enum
properties print numbers; this says what they mean. An enum by name is an
exact find under `Enum` (user-defined enums included).

## Object filters

`find`, `each` and `census` take the same filters over the object table,
applied by the agent during its scan, so they cost no round trip per
object:

| Filter | Keeps |
|---|---|
| `cls` | instances of the class or a subclass. A name that is no class (any case) is refused as not found, naming up to five classes whose names contain it, so a typo is never read as no instances; `each` checks it first, `find` and `census` only when nothing matched |
| `outer` | objects with that object up their outer chain (at most 16 steps). It is an object when it parses as one (a literal ref, an address), checked like any object argument (a gone one is refused as not found, never a filter by whatever reuses its address), else text an outer's name contains, any case (no match is no rows) |
| `live` (`find`) | no class default or archetype objects (`each` and `census` skip them always) |

## census

One table scan counting live instances per exact class, optionally only
classes under `cls` (and `outer`); class default and archetype objects are
skipped. Each row carries the class ref, its name, the count and up to
three sample refs; most instances first. `view.match` keeps rows whose
class name contains the text (any case); the scan, `classes` and
`instances` stay the whole census, and `view.limit` applies after the
filter.

<a id="census-changes"></a>**Pages and changes.** A result shows `from`,
`total` (rows after `view.match`) and, while more rows remain,
`next_cursor` with a `hint`. `view.start` is a later page of the last
census with the same filter (and the same `diff`): the stored rows,
sliced, with no new scan and `ms` 0, so the pages of one filter join
without gaps; refused when there is none. A census without `view.start`
scans again. Every census becomes the baseline for its filter (`cls` and
`outer`, either may be absent), in the session and the cache file (which
keeps the census before it too, for later pages of a `diff`).

`census` with `diff` runs a new census and prints only the classes whose
count differs from that baseline, largest change first: each row's
`count` is the count now and `before` the count then, and the result's
`before` holds the baseline's `classes` and `instances`; then the new
census is the baseline. So, around the action:

```json
{"verb": "census", "args": {"cls": "Actor"}}
{"verb": "census", "args": {"cls": "Actor", "diff": true}}
```

It is refused when there is no baseline for that filter (none taken,
another agent instance, or another filter). Classes match by ref. Samples
are any live instances, not the new ones: for a class absent before they
are necessarily new, otherwise follow up with `find` with `cls`; a class
now gone has none.

- A census scans across several ticks, so it is not one moment in time,
  and an object that lives a tick or two can fall between two censuses.
  For those, `watch-start` a container or use `each` with `seconds`.
- A broad filter shows unrelated churn (effects, audio, UI). Narrow
  `cls` to a base class instead.

## each

`each` lists the live instances of the class or a subclass (the scan spans
ticks; up to 1024, then `capped`), then reads up to 16 field paths for
every instance, as a table: `columns` once, then `rows` of
`{ref, name, values}`, one value per column in the order asked. `name` is
the instance's object name, described for the rows shown (after `where`
and `view.limit`) in a few requests. `each` only reads: a function column
is refused and named to `call-each`
([calls](unreal-calls.md#call-each)), so the `writes` mark is exact per
verb.

- **Columns** take paths as `get` takes them (`Owner:Field` or a readable
  name, struct members, object, class and weak object pointers).
- **Passes.** The paths are resolved on the class. What they need inside
  the instance (a field, or the pointer a path follows) is grouped into
  passes of at most 64 bytes: one agent `each` per pass, the first with
  the scan, the rest over its cached instance list, each pass in one tick
  on the game thread. A path's part past a pointer is resolved and read
  per instance, as `get` does. `same_tick` is true when one pass with no
  pointer path read every value.
- **Failed cells.** A cell that cannot be read (a null pointer on the way,
  an instance gone by a later pass) is null, with an `errors` entry (`ref`,
  `column`, `message`). The row stays, so no instance is hidden.
- **Cost.** Listing the instances dominates, and a column adds none. The
  listing goes through the class index ([find](#find)): after the first,
  a pass over the table's changed slots, not a walk of every object.
- **`seconds`** (one column) repeats every 100 ms on the cached instance
  list and prints each changed row to stderr as it happens. Not in a
  `batch`.
- **`where`** (`{"field": ["Name"], "contains": "text"}`) keeps the
  instances whose field's value contains the text, any case: a string, an
  FName's or a text's string, else the value's JSON. The field is read as
  one more column and shown only when it is one; `matched` counts the
  instances kept. Not with `seconds`.
- **`Outer`, `Name` and `Class`** name the instance itself, as columns and
  in `where`, when no field takes the name: one describe-many of the
  instances, then one of their distinct outers. `Outer` is `{name, ref}`.
  They match text: `where` on `["Class"]` containing `Body` is a substring
  of the class's own name, while `cls` takes a class and its subclasses.
- **Containers.** An array, set or map column reads its header in the pass
  and its elements per instance after it (8 shown, `view.elements` up to
  256), so `same_tick` is false; a set or map is not held to the 64 bytes.
- **`view.limit`** caps the rows shown (default 100, 0 for all); `omitted`
  counts the rest. Unfiltered output over a whole class is large: locate
  the object first, or filter.

## Class names

`get` with `mode` `types`, `func` and `funcs` also take a bare class or
struct name (`{"verb": "get", "args": {"object": "Character", "mode": "types"}}`).
The console finds the object of that exact name under `Struct` once per
agent instance and reuses its ref through the cache file. Names up to 127
characters: the agent compares the whole name, and the reply's name (cut
at 63) is compared that far. A name starting with `Default__` is that
default object (an exact find, any class), so `object` `Default__Character`
reads the object. `get` (in other modes), `rows`, `entries`, `call` and
`call-pure` take a `Default__` name in place of an object
(`{"verb": "get", "args": {"object": "Default__GameSettings", "paths": [["Scale"]]}}`),
never a bare class name: `get` on `Character` is refused with that hint.
The lookup is an exact `find` (`FindExact`): the agent stops at the first
match instead of scanning and counting the whole table, so a class costs
the slots up to it; only a name that is no class costs a full scan.

## Assets to files

`export-assets` writes the live instances of up to 16 classes (`cls`,
and their subclasses; no `Default__` or archetype objects) into `dir`,
optionally only those whose name contains `view.match`, one
JSON file per object, so asset data can be read, searched and compared
between builds offline instead of live again:

- The file is `<dir>/<Class>/<name>.json` (characters Windows refuses
  become `_`; a second object of the same name gets `~2`), holding `ref`,
  `name`, `class`, `get` (`get`'s result with `view.nonzero`) and, for a
  data or curve table, `rows` (`rows`' result with `view.limit` 0). A
  table is an object with a `RowStruct` field, or whose class or the class
  asked for names `CurveTable`. A read that failed is `{error}` in its
  place, and the object counts in `failed`.
- `<dir>/index.json` lists every object (`name`, `class`, `ref`, `file`,
  `table_rows`, `error`); the result carries `exported`, `tables`,
  `failed`, the first 100 rows and `omitted`. At most 2000 objects per
  call, then `capped`: narrow with `view.match` or fewer classes.
- Each object costs the reads of its `get` and `rows`, run as batch items
  run (no output budget per item); the files are replaced on every call.
