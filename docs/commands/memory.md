# Memory commands

*Home of:* the core memory commands and their guards: `read`, `peek`, `write` and its timed forms, `writes`, `search`, captures (`watch-start`, `watch-read`, `watch-stop`), `mem-snap`/`mem-diff`, typed layouts.

**TL;DR:** Read, search and change raw memory in any process the agent
runs in, with no engine pack (tier T0). Every write is guarded and can
require the old bytes first; timed writes undo themselves when they
expire. Writes change the running game: they are separate commands
marked `writes`, so a client asks before running them.

Core commands: they work in any host, with or without an engine pack.
Wire details: [protocol](../protocol.md). Each command's flags and result
keys: `help <verb>` or [result shapes](results.md).

| Command | Guard |
|---|---|
| `read <addr> [len] [--type T]` | 1..4096 bytes; the whole range must pass the host's `readable` check. `--type i8\|u8\|i16\|u16\|i32\|u32\|i64\|u64\|f32\|f64\|ptr` adds `values`, the bytes decoded as that type (NaN and null pointers as null); the length must be a multiple of its size. `--type "<layout>"`: a block of mixed types (below). `--type cstr\|wstr`, pointer chains and the `region` it adds: [native objects](native-objects.md) |
| `peek <addr> [count] [--label [file]] [--name <name>]` | console only, held: what 1..64 qwords hold (default 8), for globals and fields a static tool shows only as `qword_…`. One `read` (per-qword batched reads when part of the range is unreadable), one `describe` per aligned pointer-shaped value, 1-byte readability probes, one region query for the address (`region`) and one per readable pointer, and one `name` lookup. Each row gets one label, the first that holds: `object` (with `object`), `module` (`value_module`, `value_rva`), `heap` (readable pointer to private memory; another region kind, such as `mapped`, by name), `value` (`f32` pair, `f64`); an unreadable qword is `unreadable`. **A name is only a hint:** a `value` row whose low 32 bits decode gets `name_hint`, nothing more, since almost any small number decodes to some name. `--label` (one qword inside a module) appends the reader-confirmed label as a JSON line `{module, module_size, timestamp, rva, name, kind: "global"}` to the file (default: the console's labels file); `--name` names it, the default is `FName_<hint>`. Size and PE timestamp tie it to one build |
| `write <addr> <hex> [--expect <hex>]` | 1..256 bytes; committed, writable pages; page protection is never changed. `--expect`: written only while memory still holds those bytes (same length); otherwise refused and `error.found` shows what is there |
| `modules` | loaded modules with base, image size and name, the process exe first; 64 per page, the client pages |
| `search <type> <value> [--start A] [--to A] [--module M] [--align N] [--writable] [--heap] [--paged] [--max N]` | committed, readable, cacheable pages in the working set; at most 2 ms of scanning per host tick; 1..512 hits per page |
| `mem-snap <addr> <len>` / `mem-diff <test> <type> [--keep] [--limit N]` | console only: up to 1 MiB read in held 4 KB requests (refused when part is unreadable), kept in a file beside the cache file; then compared element by element ([below](#mem-snap-and-mem-diff)) |
| `write <addr> <hex> --hold [duration] [--expect <hex>]` | a pin: up to 8 of 1..64 bytes, each 1 ms..60 s (default 10 s); written at the start of every host tick, before requests ([timed writes](#timed-writes)) |
| `write <addr> <hex> --undo <duration> --owner <ref> --expect <hex> [--object <addr>]` | a trial: up to 8 of 1..64 bytes, 1 ms..60 s. One write when memory holds `--expect`; the agent saves those bytes and restores them on expiry, on clear, or when a newer agent in the process takes over while this one still ticks. An agent its host has stopped no longer ticks: clear them (`writes clear`) before stopping it. Checked at the start of every host tick: a changed owner identity or `--object` vtable ends it `abandoned` (nothing written); memory that no longer holds the trial bytes ends it `conflict` (left alone). The target lies inside the owner (its class size as the agent reports it, so native members past the last reflected field count), or within 0x1000 bytes after `--object`, whose first 8 bytes must point into a loaded module. No target inside a loaded module. The console refuses owners that are not actors or components (class default objects, archetypes, assets), so `--undo` needs a host with the Unreal pack; the agent does not check classes |
| `writes [clear [undo:N\|hold:N]]` | every timed write as a row (`id`, `mode`, `state`, time left): an `--undo` one (`active`, `restored`, `conflict`, `abandoned`, with `guard` when abandoned) adds its `original` bytes and `changed_tick` and stays listed until cleared; a `--hold` one (`active`, `expired`, `object changed`, `not writable`, `memory differed from --expect`, `access faulted`) adds `guarded` and `writes`. `clear` ends one or all, restoring active `--undo` ones, and returns the rows it ended |
| `watch-start <duration> <addr\|offset>:<len>\|<object>.<Field>... [--every <duration>] [--follow <root>] [--file <path>]` / `watch-read [--all]` / `watch-stop [--all]` | 1..8 ranges of up to 512 bytes, rows up to 2048 bytes, sampled at the start of every host tick into a 4 MB agent buffer for up to 30 s, returning at once; with `--every` (at least one host tick apart, at most once per tick) up to 60 min, as long as one row per interval fits the buffer (otherwise refused with the longest duration that would); one capture per agent |

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

`read <addr> --type "u8 phase, pad 3, f32 duration, f32
elapsed, f64[3] start"` decodes a block that reflection does not describe:
fields in memory order, comma-separated, each a type from the list above
with an optional `[n]` count and an optional name (letters, digits, `_`, a
trailing `?` for an unconfirmed one), or `pad n` for a gap. The read length
is the layout's size; an explicit length that differs is refused, as are an
unknown type, a bad count or name and a name used twice (the message names
the field). `layout` is `{name: value}` (keys print sorted), `[n]` fields as
lists, unnamed fields by offset (`"+0x8"`); `hex` stays. Nothing is saved:
the same syntax names members for good in the
[native names file](unreal.md#native-members), so a layout that decoded here pastes
into `label` unchanged.

## search

`search` finds addresses holding a value: `hex` bytes (up to 64), `u32`,
`i32`, `u64`, `ptr` (also `$N` and module addresses), `f32`, `f64`, or
`text` (the string's bytes with no terminator, so any string holding it
matches; quoted text is taken exactly, see
[search text](fast-lane.md#search-text); `--wide` for UTF-16, up to 32
characters). Typed
values align to their size unless `--align` says otherwise; hex aligns to 1.
Aligned 4- and 8-byte values compare whole words; other patterns jump
between first-byte candidates. It returns `rows` (`address`, with
`module` and `rva` inside a module), `from` and `next_cursor`: the address
to pass as `--start` for the next page, absent once the range is done.

`--background` runs the scan on the agent's worker thread instead of in host-tick slices, so the host pays only a poll per tick; the host must allow it (`Host::backgroundSearch`), otherwise the search is refused. A newer background search cancels an abandoned one first.

Filters: `--writable` skips read-only and code pages; `--heap` keeps
process-private memory (heaps, stacks) and skips images and file mappings;
`--module <name>` limits the range to one loaded module. Write-combined and
uncached pages (GPU and device mappings) are always skipped. Pages outside
the working set are skipped unless `--paged`: reading them makes the OS fault
each page in, or create it for memory that was committed but never touched,
which stalled the host for tens of milliseconds per slice. The agent skips
its own request, job and section buffers, and any other view of its section
(an agent injected earlier into the same process maps one too). An earlier
agent's own request buffer can still hold old patterns. A match that spans two
regions or reaches a skipped page is missed. Use it to find where a native
struct lives (search its vtable pointer with `--heap`) or which copies of a
value exist.

## mem-snap and mem-diff

`get --changed` compares reflected fields; these compare raw bytes, for
state no reflection names (a native block, a struct found by `search`, an
object with no engine pack). `mem-snap` keeps the range
(`%TEMP%\reagentlive-<pid>.memsnap`, tied to the agent instance like the
cache file; a new snap replaces it). `mem-diff` reads the range again and
tests every `type`-sized element from the snap's start: `changed` and
`unchanged` compare bytes, `increased` and `decreased` compare numbers
(NaN never passes). It answers `compared`, `matched` and the first
`--limit` rows (default 64, at most 256; `capped` when cut) of `address`,
`before` and `after`.

`--keep` narrows: only the matches stay as candidates, the bytes just read
become the baseline, and later diffs compare the candidates alone (`kept`).
A diff with another type than the candidates were narrowed under is
refused. So "which value fell when I got hit?" is a snap of the owner's
native range, `mem-diff unchanged f32 --keep` while nothing happens,
`mem-diff decreased f32 --keep` after the hit, repeated until a few rows
remain. Each diff is one read of the range, not one tick: values may be
torn across 4 KB requests, so confirm a candidate with `watch-start`. This is a
bounded narrowing scan over one range the agent names, not a memory
scanner: no whole-process scan, no tolerance, no pointer scan
([design](../design.md#place-among-other-tools)).

## Timed writes

`write --undo <duration>` writes once and the agent restores the original
bytes when the time is up (a trial, above). `write --hold` freezes memory: the
agent writes the same bytes every tick until the pin
expires (its duration, default 10 s) or stops. A target given relative to an object (`$N+0x3E4`, a literal
ref plus offset) is guarded on that object's identity (Unreal: object-table
index and class, read when the pin starts), so the pin stops for good when the
object is freed or replaced instead of writing into reused memory. `--expect`
makes the first write conditional. A value the game recomputes from scratch
within each frame still reads the game's value for that frame; integrated
state (a timer, an eased value) stays pinned.

## watch

`watch-start` samples ranges every host tick (or `--every`) inside the
agent and returns at once; `watch-read` prints the `rows` so far and
`watch-stop` ends the capture and prints them: each with `tick` (agent
heartbeat), `us` (since start) and `values` (one per range, null while
unreadable: its bytes as hex, or a [typed range](#typed-ranges)'s
numbers). Object-relative ranges are guarded like pins: when the object is
freed or replaced the capture stops, `stopped: "object replaced"`, and the
rows so far stay. `stopped` says why a capture ended: `deadline`,
`stopped`, `buffer full`, `object replaced` (absent while it runs). A
caller that wants to block (a script waiting out the capture) starts,
waits and stops: the Python session's `watch(seconds, ranges)` does that.

### Typed ranges

A range may name its type in the [read --type](#typed-layouts) element
vocabulary in place of a length: `0x9000:f64`, `$1+0x40:i32[2]`. Its
samples come back as numbers (a list for `[n]`), its `ranges` entry carries
`type`, and the result adds `summary`: for each value (each element of an
`[n]` range) `first`, `last`, `least`, `greatest`, the `samples` that held a
number, `changes`, how many of those differ from the one before, and
`per_second`, the change from first to last over the time between them
(absent with one sample). The console keeps the type in the
agent's copy of the spec (`WatchRange::note`, [protocol](../protocol.md)),
so `watch-read` and `watch-stop` from any later run decode the capture
too. The arithmetic lives in the console, so a typed line, a script and a
tool all get it.

### Watching a reflected field

With an engine pack, a range may be a field path instead of an address:
`$1.Health`, `0x...#5:2.RootComponent.RelativeLocation`, the paths `get`
takes. The console resolves it before the start through the same path
walk as `get` (object pointers and structs followed then, not per sample),
takes the place and size from reflection and the type from the property:
a Blueprint float is a `DoubleProperty`, so it is watched as `f64`; a
struct of one scalar type laid end to end (a vector) as that type with a
count; anything else as raw bytes. The range is guarded on the object that
holds the field. Not with `--follow`, whose ranges are offsets from the root.

### Rows: the changes, not every sample

`watch-read` and `watch-stop` return the first sample, every
sample that differs from the one before it (each with its `us`), and the
last, so a transition keeps its time while a capture of a value that sits
still for 30 s is a handful of rows. `lost` and `resumed` rows, and the row
after a gap, always stay. `samples` is how many there were. Past 200 rows
(a value that changes every tick) the middle is dropped, half the rows kept
from each end, and `dropped` counts it. The summary is computed from every
sample first. `--all` returns every sample.

### A capture that does not hold the console

`watch-start` returns at
once; durations take `ms`, `s` (the default) or `m`, and `--every 10s`
samples on that interval instead of every tick (a slow capture over
minutes). The capture belongs to the agent instance: it survives the
console exiting, `watch-read` from any later run returns the rows so far
(with `running`, `stopped`, `capacity`, `ms`, `interval_ms`, `root`,
`ranges`), `watch-stop` ends it and returns them. A new start replaces it;
a newer agent taking over ends it. Reflected paths are resolved once, at
the start ([above](#watching-a-reflected-field)).
No file is written inside the target. `--file <path>` keeps every sample
on the console's side: the start writes the ranges as the file's first
line (JSON), and each `watch-read` and `watch-stop` rewrites it with every
sample so far, one JSON line each (as `--all` prints them), and adds
`file`. The path persists for the agent instance in the cache file, so a
later run's read writes it too; a start without `--file` drops it.

### Following a replaced object

A capture tied to an object ends when the
game replaces it (a respawn). `--follow <root>` follows one the host can
always find again: the host offers named roots (`status` lists them as
`roots`), the ranges are offsets from the root (`+0x538:16`), and each
sample first checks the root's identity; when it fails the agent asks the
host for the current one and continues on it. Rows carry `lost` (one row,
when the root went away and no new one existed; nothing is sampled until it
returns) and `resumed` with the new object's `root` address; `gaps` lists
each (`from_us`, `to_us`, absent while still lost), never filled. A host
without roots refuses `--follow`, as does an unknown name.
