# Unreal objects

*Home of:* how the Unreal pack finds and counts objects: `find` and its class index, `describe`, `name`, `name-find`, `enum`, the object filters, `census` and its baselines, `each`, where objects live (`owner`, `route`), class names as arguments, and `assets-export`.

**TL;DR:** `find` lists objects by name or class, `census` counts live
instances per class (and what changed since the last count), `each` reads
a table of fields over every live instance of a class in one tick, and
`assets-export` writes them to files. Rules every pack command shares:
[Unreal pack](unreal.md); args and results: the [reference](reference.md).

## find

Objects whose full name (`Base_N`) contains `text` (any case) and/or whose
class or a parent class is named `cls` (`Character` includes vendors), in
pages from the object-table slot `view.start`. The first page scans the
whole table and counts the total; a later page starts at its cursor, so
paging a broad class is linear, not quadratic. `live` skips class default
and archetype objects, as `census` does.

- **Class index:** with `cls` (and in `each`) the agent visits only the slots
  its class index names, rebuilt once a minute. A candidate still gets every
  check, so a stale entry can only hide an object, never add one. `text`
  alone walks the whole table: add `cls` when the class is known.
- Only matches get the host's full `Context::object` check; decoded base
  names are cached for the process, so repeated finds skip most name
  decoding.

`find` and `describe` rows name each object's `outer` (`{name, ref}`): a
function's or property's class, a subobject's owner; a package has none.
`describe` checks the thread and object table before returning the object's
name, class and full ref; give `slot` and `serial` for a weak pointer, which
must match the table.

## name and name-find

- **`name`:** FName comparison indices as base text, gameplay tags and
  DataTable row names included. Each index must be where a pool entry
  starts, so an index inside an entry returns empty rather than its text read
  as a header.
- **`name-find`** walks the engine's name pool (base names, including tags
  and row names no object carries) for a substring, any case, paged with the
  next comparison index as cursor. Some entries are obfuscated (fixed-length
  text with the real length in the header); it takes the longer reading.

## enum

An enum's names and values in its order. The agent reads two layouts of
`UEnum::Names` (pairs, or, as UE 5.6 and later keep them, two arrays) and
refuses the enum when the names do not decode. A Blueprint enum's names are
its stored `NewEnumeratorN`, not its display names. Byte and enum properties
print numbers; this says what they mean.

## Object filters

`find`, `each` and `census` take the same filters over the object table,
applied by the agent during its scan, so they cost no round trip per
object:

| Filter | Keeps |
|---|---|
| `cls` | instances of the class or a subclass. A name that is no class is refused as not found, naming classes whose names contain it, so a typo is never read as no instances |
| `outer` | objects with that object up their outer chain. An object when it parses as one (a literal ref, an address; a gone one is refused, never a filter by whatever reuses its address), else text an outer's name contains |
| `live` (`find`) | no class default or archetype objects (`each` and `census` skip them always) |

## census

One table scan counting live instances per exact class, optionally only
classes under `cls` (and `outer`); class default and archetype objects are
skipped. Each row carries the class ref, name, count and sample refs; most
instances first. `view.match` keeps rows whose class name contains the text;
the scan, `classes` and `instances` stay the whole census.

<a id="census-changes"></a>**Pages and changes.** Every census becomes the
baseline for its filter (`cls` and `outer`), in the session and the cache
file. `view.start` is a later page of the last census with the same filter
(and `diff`): the stored rows, sliced, with no new scan, so the pages of one
filter join without gaps. A census without `view.start` scans again.

`census` with `diff` runs a new census and prints only the classes whose
count differs from that baseline, largest change first (`count` now,
`before` then); the new census is the baseline. So, around the action:

```json
{"verb": "census", "args": {"cls": "Actor"}}
{"verb": "census", "args": {"cls": "Actor", "diff": true}}
```

It is refused when there is no baseline for that filter (none taken, another
agent instance, or another filter). Samples are any live instances, not the
new ones: for a class absent before they are necessarily new, otherwise
follow up with `find` with `cls`.

- A census scans across several ticks, so it is not one moment in time, and
  an object that lives a tick or two can fall between two censuses. For
  those, `watch-start` a container or use `each` with `duration`.
- A broad filter shows unrelated churn (effects, audio, UI). Narrow `cls` to
  a base class instead.

## each

`each` lists the live instances of the class or a subclass, then reads field
paths for every instance, as a table: `columns` once, then `rows` of
`{ref, name, values}`, one value per column in the order asked. `each` only
reads: a function column is refused and named to `call-each`
([calls](unreal-calls.md#call-each)), so the `writes` mark is exact per verb.

- **Columns** take paths as `get` takes them (`Owner:Field` or a readable
  name, struct members, object, class and weak object pointers).
- **Passes.** What the paths need inside the instance is grouped into passes
  of at most 64 bytes sharing the scan, each in one tick on the game thread;
  a path's part past a pointer is read per instance. `same_tick` is true when
  one pass with no pointer path read every value.
- **Failed cells.** A cell that cannot be read is null, with an `errors`
  entry. The row stays, so no instance is hidden.
- **`duration`** (one column, 1..60 s) repeats on the cached instance list and prints
  each changed row to stderr as it happens. Not in a `batch`.
- **`where`** (`{"field": ["Name"], "contains": "text"}`) keeps the instances
  whose field's value contains the text, any case; `matched` counts them. Not
  with `duration`.
- **`Outer`, `Name` and `Class`** name the instance itself, as columns and in
  `where`, when no field takes the name. They match text: `where` on
  `["Class"]` containing `Body` is a substring of the class's own name, while
  `cls` takes a class and its subclasses.
- **`view.limit`** caps the rows shown; `omitted` counts the rest.
  Unfiltered output over a whole class is large: locate the object first, or
  filter.

## owner and route

Two questions that otherwise take hundreds of reads: which object holds this
address, and how does the engine reach this object. Both read reflection
only; neither scans raw memory for pointers.

```json
{"verb": "owner", "args": {"addrs": ["0x1f2e3d4c78", "0x2a0b1c0008"], "within": ["0x1f2e3d4c50#123:4"]}}
{"verb": "route", "args": {"object": "0x1f2e3d4c50#123:4", "sources": ["pawn"], "depth": 6}}
```

- **owner** takes addresses as `search` rows give them and finds the nearest
  object at or below each, accepted when the offset is inside its class size.
  It names the slot: `path` into structs, `outer` for the object's `Outer`
  pointer (the usual back-pointer behind many `search` hits), and the offset.
  No index is kept between calls, so it is never stale.
- **Heap storage** (an array's, set's or map's elements) belongs to no
  object. For those addresses the console reads the containers of the
  `within` objects and names the element (`Items[3].Member`). Give the
  searched object's outer, or the object you suspect, as `within`.
- **route** walks breadth-first from the sources (host roots by name, as
  `status` lists them, or objects) over reflected references: object and
  interface fields and containers of them. Class, weak, soft and lazy
  pointers are not followed (they lead into the asset graph); `up` adds a
  step up to an object's `Outer`, as a last resort. It never steps down, so
  an object only its children's `Outer` points to stays out of reach.
- **Bounds:** `depth` and `budget`. It answers up to a few routes of the
  shortest length, or `end` says why there is none, never a partial guess.
  The walk spans ticks, so an object can die between the walk and your next
  call: check a route with `get` before writing it into code.
- **Paths:** each step's `field` is relative to the previous object. An
  indexed step (`Items[3]`) is an `entries` read; a set or map step names
  the element by its key when it is a name or an object, otherwise `[slot N]`
  is the sparse slot, free slots counted, not `entries`' row number.

## Class names

`get` with `mode` `types`, `func` and `funcs` also take a bare class or
struct name (`{"verb": "get", "args": {"object": "Character", "mode": "types"}}`).
The console finds the object of that exact name under `Struct` once per
agent instance and reuses its ref through the cache file. A name starting
with `Default__` is that default object, so `object` `Default__Character`
reads the object. `get` (in other modes), `rows`, `entries`, `call` and
`call-pure` take a `Default__` name in place of an object
(`{"verb": "get", "args": {"object": "Default__GameSettings", "paths": [["Scale"]]}}`),
never a bare class name: `get` on `Character` is refused with that hint.

## Assets to files

`assets-export` writes the live instances of some classes (and their
subclasses; no `Default__` or archetype objects) into `dir`, optionally only
those whose name contains `view.match`, one JSON file per object, so asset
data can be read, searched and compared between builds offline instead of
live again:

- The file is `<dir>/<Class>/<name>.json`, holding `ref`, `name`, `class`,
  the object's `get` (non-zero fields) and, for a data or curve table, its
  `rows`. A read that failed is `{error}` in its place.
- `<dir>/index.json` lists every object; the files are replaced on every
  call, and a capped call says so: narrow with `view.match` or fewer
  classes.
