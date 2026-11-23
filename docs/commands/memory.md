# Memory commands

*Home of:* the core memory commands and their guards: `read`, `peek`, `write` and its timed forms, `timed-writes`, `search`, captures (`watch-start`, `watch-read`, `watch-stop`), `mem-snap`/`mem-diff`, typed layouts.

**TL;DR:** Read, search and change raw memory in any process the agent
runs in, with no engine pack (tier T0). Every write is guarded and can
require the old bytes first; a write with `undo` restores itself, and a
`hold` stops by itself. Writes change the running game: they are separate commands
marked `writes`, so a client asks before running them.

Core commands: they work in any host, with or without an engine pack.
Wire details: [protocol](../protocol.md). Each command's args and result
keys: the [reference](reference.md), `help` with `verb`, or
[result shapes](results.md).

| Command | Guard |
|---|---|
| `read` | 1..4096 bytes; the whole range must pass the host's `readable` check. `type` `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64`, `f32`, `f64` or `ptr` adds `values`, the bytes decoded as that type (NaN and null pointers as null); the length must be a multiple of its size. A layout string as `type`: a block of mixed types (below). `type` `cstr` or `wstr`, pointer chains and the `region` it adds: [native objects](native-objects.md) |
| `peek` | console only, held: what 1..64 qwords hold, for globals and fields a static tool shows only as `qword_…`. One `read` (per-qword batched reads when part of the range is unreadable), one `describe` per aligned pointer-shaped value, 1-byte readability probes, one region query for the address and one per readable pointer, and one `name` lookup. Each row gets one label, the first that holds: `object` (with a pack), `module` (`value_module`, `value_rva`), `heap` (readable pointer to private memory; another region kind, such as `mapped`, by name), `value` (`f32` pair, `f64`); an unreadable qword is `unreadable`. **A name is only a hint:** a `value` row whose low 32 bits decode gets `name_hint`, nothing more, since almost any small number decodes to some name. `label` appends one qword as a confirmed global to a labels file (default: the host's names file, else the console's; [labels](labels.md#peek-with-label)) |
| `write` | 1..256 bytes; committed, writable pages; page protection is never changed. `expect`: written only while memory still holds those bytes (same length); otherwise refused and `error.found` shows what is there. `hold` and `undo`: [timed writes](#timed-writes) |
| `timed-writes` | every timed write as a row (`id`, `mode`, `state`, time left): an `undo` one (`active`, `restored`, `conflict`, `abandoned`, with `guard` when abandoned) adds its `original` bytes and `changed_tick` and stays listed until cleared; a `hold` one (`active`, `expired`, `object changed`, `not writable`, `memory differed from expect`, `access faulted`) adds `guarded` and `writes`. `clear` ends one (`id`: `undo:N` or `hold:N`) or all, restoring active `undo` ones, and returns the rows it ended |
| `modules` | loaded modules with base, image size and name, the process exe first; 64 per page, the client pages |
| `search`, `search-job` | committed, readable, cacheable pages in the working set; a job on the agent's worker thread (host opt-in) or at most 2 ms of scanning per host tick; 1..507 hits per job ([below](#search)) |
| `mem-snap`, `mem-diff` | console only: up to 1 MiB read in held 4 KB requests (refused when part is unreadable), kept in a file beside the cache file; then compared element by element ([below](#mem-snap-and-mem-diff)) |
| `watch-start`, `watch-read`, `watch-stop` | a capture inside the agent, one per agent ([below](#watch)); sizes: [limits](../protocol.md#limits) |

## Faults

Read and write copies catch access faults and report `access faulted`; a
first-chance fault is still visible to any exception observer in the host.

## Reads that share a tick

Several ranges that must come from one moment go in one `batch` of
`read`s (its `same_tick` says whether they did) or one `get`. The wire op
behind it, `read-batch` ([protocol](../protocol.md)), is internal: the
console uses it for `disasm`, `touches`, signatures and labels, and gives
it no verb of its own.

## Typed layouts

```json
{"verb": "read", "args": {"addr": "0x1000", "type": "u8 phase, pad 3, f32 duration, f32 elapsed, f64[3] start"}}
```

decodes a block that reflection does not describe:
fields in memory order, comma-separated, each a type from the list above
with an optional `[n]` count and an optional name (letters, digits, `_`, a
trailing `?` for an unconfirmed one), or `pad n` for a gap. The read length
is the layout's size; an explicit `len` that differs is refused, as are an
unknown type, a bad count or name and a name used twice (the message names
the field). `layout` is `{name: value}` (keys print sorted), `[n]` fields as
lists, unnamed fields by offset (`"+0x8"`); `hex` stays. Nothing is saved:
the same syntax names members for good in the
[native names file](unreal-fields.md#native-members), so a layout that decoded here pastes
into `member-name` unchanged.

## search

`search` finds addresses holding a value: `type` `hex` (bytes, up to 64),
`u32`, `i32`, `u64`, `ptr` (a number or `"0x..."`), `f32`, `f64`, `text`
(the string's bytes with no terminator, so any string holding it matches;
the `value` string is taken exactly, see
[search text](fast-lane.md#search-text)) or `wtext` (the same as UTF-16, up
to 32 characters). Typed values align to their size unless `align` says
otherwise; hex aligns to 1. Aligned 4- and 8-byte values compare whole
words; other patterns jump between first-byte candidates.

```json
{"verb": "search", "args": {"type": "f32", "value": 100, "scope": {"heap": true}, "max": 16}}
```

**Range.** The main module (the process exe) unless `scope` says
otherwise: `scope.module` for another module, `scope.start`/`scope.to` for
an address range (`scope.start` alone ends with the module it lies in, or
the top of memory outside modules), `scope.heap` or `scope.writable` for
all memory.

**A job.** Every search is a job the agent runs while other requests do:
on its worker thread when the host allows it (`Host::backgroundSearch`)
and the worker is idle, else in 2 ms slices at the start of every host
tick. The console starts it and polls it for up to `wait` seconds
(default 10, 0 answers at once), so a long scan never holds the console or
the agent's request slot. The answer is the job's state: `rows` (hits so
far: `address`, with `module` and `rva` inside a module), `job`, `state`
(`running`, `done` or `stopped`), `from` and `to` (the range), `scanned`
(the share of the range's searchable bytes, the committed readable memory
the scope takes, behind the cursor; 0 while the job first measures them)
and `next_cursor`.
`{"verb": "search-job", "args": {"job": 16}}` answers the same for a
running or finished job; with `"stop": true` it ends it first. One job at
a time: a new search stops the last one, and only the last answers. The
job stops once it has `max` hits; then, and after a stop, `next_cursor` is
the address to pass as `scope.start` (with the same range options) to go
on, absent once the range is done.

Filters: `scope.writable` skips read-only and code pages; `scope.heap`
keeps process-private memory (heaps, stacks) and skips images and file
mappings; `scope.module` limits the range to one loaded module.
Write-combined and uncached pages (GPU and device mappings) are always
skipped. Pages outside the working set are skipped unless `scope.paged`:
reading them makes the OS fault each page in, or create it for memory that
was committed but never touched, which stalled the host for tens of
milliseconds per slice. The agent skips its own request, job and section
buffers, and any other view of its section (an agent injected earlier into
the same process maps one too). An earlier agent's own request buffer can
still hold old patterns. A match that spans two regions or reaches a
skipped page is missed. Use it to find where a native struct lives (search
its vtable pointer with `scope.heap`) or which copies of a value exist.

## mem-snap and mem-diff

`get` with `mode` `changed` compares reflected fields; these compare raw
bytes, for state no reflection names (a native block, a struct found by
`search`, an object with no engine pack). `mem-snap` keeps the range
(`%TEMP%\reagentlive-<pid>.memsnap`, tied to the agent instance like the
cache file; a new snap replaces it). `mem-diff` reads the range again and
tests every `type`-sized element from the snap's start: `changed` and
`unchanged` compare bytes, `increased` and `decreased` compare numbers
(NaN never passes). It answers `compared`, `matched` and the first
`view.limit` rows (default 64, at most 256; `capped` when cut) of
`address`, `before` and `after`.

`keep` narrows: only the matches stay as candidates, the bytes just read
become the baseline, and later diffs compare the candidates alone (`kept`).
A diff with another type than the candidates were narrowed under is
refused. So "which value fell when I got hit?" is a snap of the owner's
native range, then while nothing happens

```json
{"verb": "mem-diff", "args": {"compare": "unchanged", "type": "f32", "keep": true}}
```

and after the hit the same with `"compare": "decreased"`, repeated until a
few rows remain. Each diff is one read of the range, not one tick: values
may be torn across 4 KB requests, so confirm a candidate with
`watch-start`. This is a bounded narrowing scan over one range the agent
names, not a memory scanner: no whole-process scan, no tolerance, no
pointer scan ([design](../design/architecture.md#place-among-other-tools)).

## Timed writes

Both forms take 1..64 bytes for up to 60 s, at most 8 of each at once,
and run at the start of every host tick, before requests. `timed-writes` lists
them and `clear` ends them.

- **`undo`** writes once, when memory holds `expect` (required), and the
  agent restores the original bytes when `undo.after` is up, on `clear`,
  or when a newer agent in the process takes over while this one still
  ticks ([takeover](../protocol.md#section); stopping a host:
  [hosting](../hosting.md#the-tick)). Each tick it checks its guards: a
  changed owner identity or `undo.object` vtable ends it `abandoned`
  (nothing written); memory that no longer holds the written bytes ends
  it `conflict` (left as the game wrote it). The target lies inside the
  owner (its class size as the agent reports it, so native members past
  the last reflected field count), or within 0x1000 bytes after
  `undo.object`, whose first 8 bytes must point into a loaded module; no
  target inside a loaded module. The console refuses owners that are not
  actors or components (class default objects, archetypes, assets), so
  `undo` needs a host with the Unreal pack; the agent does not check
  classes.
- **`hold`** freezes memory: the agent writes the same bytes every tick
  until its seconds are up or it stops, then leaves them as they are; it
  restores nothing. A target given relative to an object
  (`{"base": "0x1f2e3d4c50#123:4", "offset": "0x3E4"}`, a literal ref plus
  offset) is guarded on that object's identity (Unreal: object-table index
  and class, read when it starts), so it stops for good when the object is
  freed or replaced instead of writing into reused memory. `expect` makes
  the first write conditional. A value the game recomputes from scratch
  within each frame still reads the game's value for that frame;
  integrated state (a timer, an eased value) stays held.

```json
{"verb": "write", "args": {"addr": {"base": "0x1f2e3d4c50#123:4", "offset": "0x3E4"}, "data": "00 00 c8 42", "hold": 5}}
```

## watch

`watch-start` samples up to 8 ranges inside the agent, at the start of
every host tick or every `every` seconds (a slow capture over minutes),
and returns at once. The capture belongs to the agent instance: it
survives the console exiting; `watch-read` from any run returns the rows
so far, and `watch-stop` ends it and returns them. A new start replaces
it; a newer agent taking over ends it. A caller that wants to block
starts, waits and stops: the Python session's `watch(seconds, ranges)`
does that. Each row has `tick` (agent heartbeat), `us` (since start) and
`values` (one per range, null while unreadable: its bytes as hex, or a
[typed range](#typed-ranges)'s numbers). Object-relative ranges are
guarded like a `hold`: when the object is freed or replaced the capture
stops with the rows so far. `stopped` says why a capture ended:
`deadline`, `stopped`, `buffer full`, `object replaced` (absent while it
runs). A capture that cannot fit (one row per interval for its whole
duration) is refused with the longest duration that would.

```json
{"verb": "watch-start", "args": {"duration": 5, "ranges": [{"addr": "0x9000", "len": 16}]}}
```

### Typed ranges

A range may name its type in the [read type](#typed-layouts) element
vocabulary in place of a length: `{"addr": "0x9000", "type": "f64"}`,
`{"addr": {"base": "0x1f2e3d4c50#123:4", "offset": "0x40"}, "type": "i32", "count": 2}`.
Its samples come back as numbers (a list with `count`), its `ranges` entry
carries `type`, and the result adds `summary`: for each value (each
element of a counted range) `first`, `last`, `least`, `greatest`, the
`samples` that held a number, `changes`, how many of those differ from the
one before, and `per_second`, the change from first to last over the time
between them (absent with one sample). The console keeps the type in the
agent's copy of the spec (`WatchRange::note`, [protocol](../protocol.md)),
so `watch-read` and `watch-stop` from any later run decode the capture
too. The arithmetic lives in the console, so a call, a script and a tool
all get it.

### Watching a reflected field

With an engine pack, a range may be a field path instead of an address:
`{"object": "0x1f2e3d4c50#123:4", "field": ["RootComponent", "RelativeLocation"]}`,
the paths `get` takes. The console resolves it before the start through
the same path walk as `get` (object pointers and structs followed then,
not per sample), takes the place and size from reflection and the type
from the property: a Blueprint float is a `DoubleProperty`, so it is
watched as `f64`; a struct of one scalar type laid end to end (a vector)
as that type with a count; anything else as raw bytes. The range is
guarded on the object that holds the field. Not with `follow`, whose
ranges are offsets from the root.

### Rows: the changes, not every sample

`watch-read` and `watch-stop` return the first sample, every
sample that differs from the one before it (each with its `us`), and the
last, so a transition keeps its time while a capture of a value that sits
still for 30 s is a handful of rows. `lost` and `resumed` rows, and the row
after a gap, always stay. `samples` is how many there were. Past 200 rows
(a value that changes every tick) the middle is dropped, half the rows kept
from each end, and `dropped` counts it. The summary is computed from every
sample first. `all` returns every sample.

### Keeping every sample

No file is written inside the target. `file` keeps every sample on the
console's side: the start writes the ranges as the file's first line
(JSON), and each `watch-read` and `watch-stop` rewrites it with every
sample so far, one JSON line each (as `all` prints them), and adds
`file`. The path persists for the agent instance in the cache file, so a
later run's read writes it too; a start without `file` drops it.

### Following a replaced object

A capture tied to an object ends when the
game replaces it (a respawn). `follow` names a root the host can always
find again: the host offers named roots (`status` lists them as `roots`),
the ranges are offsets from the root (`{"offset": "0x538", "len": 16}`),
and each sample first checks the root's identity; when it fails the agent
asks the host for the current one and continues on it. Rows carry `lost`
(one row, when the root went away and no new one existed; nothing is
sampled until it returns) and `resumed` with the new object's `root`
address; `gaps` lists each (`from_us`, `to_us`, absent while still lost),
never filled. A host without roots refuses `follow`, as does an unknown
name.
