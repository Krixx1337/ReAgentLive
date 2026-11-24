# Memory commands

*Home of:* the core memory commands and their guards: `read`, `peek`, `write` and its timed forms, `timed-writes`, `search`, captures (`watch-start`, `watch-read`), `mem-snap`/`mem-diff`, typed layouts.

**TL;DR:** Read, search and change raw memory in any process the agent
runs in, with no engine pack (tier T0). Every write is guarded and can
require the old bytes first; a write with `undo` restores itself, and a
`hold` stops by itself. Writes change the running game: they are separate commands
marked `writes`, so a client asks before running them.

Core commands: they work in any host, with or without an engine pack.
Wire details: [protocol](../protocol.md). Each command's args, limits and
result keys: the [reference](reference.md), `help` with `verb`, or
[result shapes](results.md).

- **Faults.** Read and write copies catch access faults and report `access
  faulted`; a first-chance fault is still visible to any exception observer
  in the host.
- **`read`** needs the whole range to pass the host's `readable` check. A
  `type` decodes the bytes (NaN and null pointers as null); a layout string
  decodes a block of mixed types ([below](#typed-layouts)); `cstr`, `wstr`,
  pointer chains and `region`: [native objects](native-objects.md).
- **`peek`** (console only) shows what a few qwords hold, for globals and
  fields a static tool shows only as `qword_...`. Each row gets one label:
  `object` (with a pack), `module`, `heap`, `value` or `unreadable`. **A
  name is only a hint:** a `value` row gets `name_hint` and nothing more,
  since almost any small number decodes to some name. `label` confirms a
  global ([labels](labels.md#peek-with-label)).
- **`write`** touches committed, writable pages and never changes page
  protection. `expect`: written only while memory still holds those bytes;
  otherwise refused and `error.found` shows what is there. `hold`, `undo`
  and `clear`: [timed writes](#timed-writes).
- **`modules`** lists loaded modules, the exe first.
- **Reads that share a tick** go in one `batch` of `read`s (`same_tick` says
  whether they did) or one `get`. The wire op `read-batch`
  ([protocol](../protocol.md)) has no verb of its own: the console uses it
  for `disasm`, `touches`, signatures and labels.

## Typed layouts

```json
{"verb": "read", "args": {"addr": "0x1000", "type": "u8 phase, pad 3, f32 duration, f32 elapsed, f64[3] start"}}
```

decodes a block that reflection does not describe: fields in memory order,
comma-separated, each a type with an optional `[n]` count and an optional
name (a trailing `?` for an unconfirmed one), or `pad n` for a gap. The read
length is the layout's size; an `len` that differs is refused, as is any
malformed field (the message names it). Nothing is saved: the same syntax
names members for good in the
[native names file](unreal-fields.md#native-members), so a layout that
decoded here pastes into `member-name` unchanged.

## search

`search` finds addresses holding a value (`hex` bytes, integers, `ptr`,
floats, `text`, `wtext`; the `value` string is taken exactly, see
[search text](fast-lane.md#search-text)). Typed values align to their size
unless `align` says otherwise.

```json
{"verb": "search", "args": {"type": "f32", "value": 100, "scope": {"heap": true}, "max": 16}}
```

**Range.** The main module unless `scope` says otherwise: another module, an
address range, `scope.heap` (process-private memory) or `scope.writable`.
`scope.start` alone ends with the module it lies in. `search-job` answers
only for the last job; `scanned` is the share of the range's searchable
bytes (the committed, readable memory `scope` takes) behind the cursor, 0
while the job first measures them.

**A job.** Every search is a job the agent runs while other requests do (on
its worker thread when the host allows it, else in small slices at the start
of every host tick), so a long scan never holds the console or the request
slot. The console starts it and polls for up to `wait` seconds. One job at a
time: a new search stops the last one, and `search-job` polls or stops it.
`next_cursor` is the address to pass as `scope.start` to go on.

**What is skipped.** Write-combined and uncached pages (GPU and device
mappings) always. Pages outside the working set unless `scope.paged`:
reading them makes the OS fault each page in, which stalled the host for
tens of milliseconds per slice. The agent's own request, job and section
buffers, and any other view of its section. A match that spans two regions
or reaches a skipped page is missed. Use it to find where a native struct
lives (search its vtable pointer with `scope.heap`) or which copies of a
value exist.

## mem-snap and mem-diff

`get` with `mode` `changed` compares reflected fields; these compare raw
bytes, for state no reflection names (a native block, a struct found by
`search`, an object with no engine pack). `mem-snap` keeps a range in a file
beside the cache file (tied to the agent instance; a new snap replaces it).
`mem-diff` reads it again and tests every `type`-sized element:
`changed`, `unchanged`, `increased` or `decreased`.

`keep` narrows: only the matches stay as candidates and the bytes just read
become the baseline. So "which value fell when I got hit?" is a snap of the
owner's native range, then while nothing happens

```json
{"verb": "mem-diff", "args": {"compare": "unchanged", "type": "f32", "keep": true}}
```

and after the hit the same with `"compare": "decreased"`, repeated until a
few rows remain. Each diff is one read of the range, not one tick: values
may be torn across requests, so confirm a candidate with `watch-start`. This
is a bounded narrowing scan over one range the agent names, not a memory
scanner: no whole-process scan, no tolerance, no pointer scan
([design](../design/architecture.md#place-among-other-tools)).

## Timed writes

`undo` and `hold` run at the start of every host tick, before requests,
within the limits `help` shows. `timed-writes` lists them (read-only, but
in the `modify` pack with `write`) and `write` with `clear` ends one or `"all"`.

- **`undo`** writes once, when memory holds `expect` (required), and the
  agent restores the original bytes when `undo.after` is up, on `clear`, or
  when a newer agent in the process takes over while this one still ticks
  ([takeover](../protocol.md#section); stopping a host:
  [hosting](../hosting.md#the-tick)). Each tick it checks its guards: a
  changed owner identity or `undo.object` vtable ends it `abandoned`
  (nothing written); memory that no longer holds the written bytes ends it
  `conflict` (left as the game wrote it). The target must lie inside the
  owner (native members past the last reflected field count) or within 0x1000
  bytes after `undo.object`, never inside a loaded module. The console
  refuses owners that are not actors or components, so `undo` needs a host
  with the Unreal pack.
- **`hold`** freezes memory: the agent writes the same bytes every tick
  until its seconds are up or it stops, then leaves them as they are; it
  restores nothing. A target given relative to an object
  (`{"base": "0x1f2e3d4c50#123:4", "offset": "0x3E4"}`) is guarded on that
  object's identity, so it stops for good when the object is freed or
  replaced instead of writing into reused memory. A value the game
  recomputes from scratch within each frame still reads the game's value for
  that frame; integrated state (a timer, an eased value) stays held.

```json
{"verb": "write", "args": {"addr": {"base": "0x1f2e3d4c50#123:4", "offset": "0x3E4"}, "data": "00 00 c8 42", "hold": 5}}
```

## watch

`watch-start` samples ranges inside the agent, every host tick or every
`every` seconds, and returns at once. The capture belongs to the agent
instance: it survives the console exiting; `watch-read` from any run
returns the rows so far, and with `stop` ends it and returns them. A new
start replaces it. A caller that wants to block starts, waits and stops: the
Python session's `watch(seconds, ranges)` does that. The agent samples every
tick, so no change between ticks is missed, unlike polling from the console.
Object-relative ranges are guarded like a `hold`: when the object is freed
or replaced the capture stops with the rows so far (`stopped` says why).

```json
{"verb": "watch-start", "args": {"duration": 5, "ranges": [{"addr": "0x9000", "len": 16}]}}
```

- **Rows are the changes, not every sample:** the first sample, every
  sample that differs from the one before it (with its time) and the last,
  so a transition keeps its time and a value that sits still is a handful of
  rows. A long run drops the middle (`dropped` counts it); `summary` is
  computed from every sample first.
- **Typed ranges.** A range may name a type from the
  [read vocabulary](#typed-layouts) in place of a length:
  `{"addr": "0x9000", "type": "f64"}`. Samples come back as numbers, and
  `summary` gives first, last, least, greatest, changes and rate per value.
  The type is kept in the agent's copy of the spec, so a later run decodes
  the capture too.
- **Keeping every sample.** `file` keeps every sample on the console's side
  (nothing is written inside the target); the path persists for the agent
  instance in the cache file.

### Watching a reflected field

With an engine pack, a range may be a field path instead of an address:
`{"object": "0x1f2e3d4c50#123:4", "field": ["RootComponent", "RelativeLocation"]}`,
the paths `get` takes. The console resolves it once before the start through
the same path walk as `get`, takes the place and size from reflection and
the type from the property (a Blueprint float is a `DoubleProperty`, so
`f64`; a vector as its scalar type with a count; anything else as raw
bytes). `get` with `mode` `types` shows the names; `Owner:Field` where one
is ambiguous. The range is guarded on the object that holds the field.

### Following a replaced object

A capture tied to an object ends when the game replaces it (a respawn).
`follow` names a root the host can always find again (`status` lists the
host's named roots as `roots`); the ranges are then offsets from the root,
and each sample first checks the root's identity. When it fails the agent
asks the host for the current one and continues. Rows carry `lost` (the root
went away and no new one existed) and `resumed` with the new root's address;
`gaps` lists each, never filled. A host without roots refuses `follow`.
