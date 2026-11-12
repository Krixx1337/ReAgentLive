# Memory commands

Core commands: they work in any host, with or without an engine pack.
Wire details: [protocol](../protocol.md). Each command's flags and result
keys: `help <verb>` or [result shapes](results.md).

| Command | Guard |
|---|---|
| `read <addr> [len] [--as T]` | 1..4096 bytes; the whole range must pass the host's `readable` check. `--as i8\|u8\|i16\|u16\|i32\|u32\|i64\|u64\|f32\|f64\|ptr` adds `values`, the bytes decoded as that type (NaN and null pointers as null); the length must be a multiple of its size. `--as "<layout>"`: a block of mixed types (below). `--as cstr\|wstr` and pointer chains: [native objects](native-objects.md) |
| `read-batch <addr> <len> [<addr> <len> ...]` | 1–8 ranges in one host tick. The 4096-byte response budget includes 4 status bytes per range |
| `peek <addr> [count] [--label <file>] [--name <name>]` | console only, held: what 1..64 qwords hold (default 8), for globals and fields a static tool shows only as `qword_…`. One `read` (per-qword `read-batch` when part of the range is unreadable), one `describe` per aligned pointer-shaped value, 1-byte readability probes, one `region` per readable pointer and one `name` lookup. Each row gets one label, the first that holds: `object` (with `object`), `module` (`value_module`, `value_rva`), `heap` (readable pointer to private memory; another region kind, such as `mapped`, by name), `value` (`f32` pair, `f64`); an unreadable qword is `unreadable`. **A name is only a hint:** a `value` row whose low 32 bits decode gets `name_hint`, nothing more, since almost any small number decodes to some name. `--label` (one qword inside a module) appends the reader-confirmed label as a JSON line `{module, module_size, timestamp, rva, name, kind: "global"}` to the file; `--name` names it, the default is `FName_<hint>`. Size and PE timestamp tie it to one build |
| `write <addr> <hex> [--expect <hex>]` | 1..256 bytes; committed, writable pages; page protection is never changed. `--expect`: written only while memory still holds those bytes (same length); otherwise refused and `error.found` shows what is there |
| `modules` | loaded modules with base, image size and name, the process exe first; 64 per page, the client pages |
| `search <type> <value> [--from A] [--to A] [--module M] [--align N] [--writable] [--heap] [--paged] [--max N]` | committed, readable, cacheable pages in the working set; at most 2 ms of scanning per host tick; 1..512 hits per page |
| `pin <addr> <hex> [--seconds N\|--ms N] [--expect <hex>]` | up to 8 pins of 1..64 bytes, each 1 ms..60 s (default 10 s); written at the start of every host tick, before requests |
| `pin-list` / `pin-clear [id]` | each pin's state (`active`, `expired`, `object changed`, `not writable`, `memory differed from --expect`, `access faulted`), writes and time left; clear one or all |
| `trial <addr> <hex> --owner <ref> --expect <hex> [--object <addr>] [--seconds N\|--ms N]` | up to 8 trials of 1..64 bytes, 1 ms..60 s (default 10 s). One write when memory holds `--expect`; the agent saves those bytes and restores them on expiry, on clear, or when a newer agent in the process takes over while this one still ticks. An agent its host has stopped no longer ticks: clear trials before stopping it. Checked at the start of every host tick: a changed owner identity or `--object` vtable ends it `abandoned` (nothing written); memory that no longer holds the trial bytes ends it `conflict` (left alone). The target lies inside the owner (its class size as the agent reports it, so native members past the last reflected field count), or within 0x1000 bytes after `--object`, whose first 8 bytes must point into a loaded module. No target inside a loaded module. The console refuses owners that are not actors or components (class default objects, archetypes, assets), so a trial needs a host with the Unreal pack; the agent does not check classes |
| `trial-list` / `trial-clear [id]` | each trial's state (`active`, `restored`, `conflict`, `abandoned`, with `guard` when abandoned), `original` bytes, `changed_tick` and time left; finished trials stay listed until cleared. Clear restores active ones and returns every cleared row |
| `watch <seconds\|Nms> <addr>:<len>... [--then call ...]` | 1..8 ranges of up to 512 bytes, rows up to 2048 bytes, up to 30 s; sampled at the start of every host tick into a 4 MB agent buffer |
| `watch-start <duration> <addr\|offset>:<len>... [--every <duration>] [--follow <root>]` / `watch-read` / `watch-stop` | the same ranges, returning at once; with `--every` (at least one host tick apart, at most once per tick) up to 60 min, as long as one row per interval fits the buffer (otherwise refused with the longest duration that would); one capture per agent |

## Faults

Read and write copies catch access faults and report `access faulted`; a
first-chance fault is still visible to any exception observer in the host.

## read-batch

`read-batch base 16 base+0x40 8` returns `tick` (the agent heartbeat
at execution) and ordered `rows`. Each row has `address`, `size`,
`ok`, and either `hex` or an `error` with status and message. The command
envelope's `ok` reports whether the batch ran. Check each row's `ok` for
read success. Malformed or over-budget batches are rejected before copying.
Addresses must be known when the request is sent. Like `read`, this checks
memory accessibility. A reference used as an address does not check object
identity. The copies run sequentially inside one host tick.

## Typed layouts

`read <addr> --as "u8 phase, pad 3, f32 duration, f32
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
to pass as `--from` for the next page, absent once the range is done.

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

## pin

`pin` freezes memory: the agent writes the same bytes every tick until the pin
expires or stops. A target given relative to an object (`$N+0x3E4`, a literal
ref plus offset) is guarded on that object's identity (Unreal: object-table
index and class, read when the pin starts), so the pin stops for good when the
object is freed or replaced instead of writing into reused memory. `--expect`
makes the first write conditional. A value the game recomputes from scratch
within each frame still reads the game's value for that frame; integrated
state (a timer, an eased value) stays pinned.

## watch

`watch` samples ranges every host tick inside the agent and prints the
`rows`: each with `tick` (agent heartbeat), `us` (since start) and
`values` (bytes per range, null while unreadable). Object-relative ranges are
guarded like pins: when the object is freed or replaced the capture stops,
`stopped: "object replaced"`, and the rows so far stay. `--then call <object>
<[Class:]Function> [hex]` runs that call right after sampling starts (a setter
that starts the action, such as aiming); its result is `then`. Rows
before it show the state before the action. `stopped` says why a capture
ended: `deadline`, `stopped`, `buffer full`, `object replaced` (absent while
it runs).

### A capture that does not hold the console

`watch-start` returns at
once; durations take `ms`, `s` (the default) or `m`, and `--every 10s`
samples on that interval instead of every tick (a slow capture over
minutes). The capture belongs to the agent instance: it survives the
console exiting, `watch-read` from any later run returns the rows so far
(with `running`, `stopped`, `capacity`, `ms`, `interval_ms`, `root`,
`ranges`), `watch-stop` ends it and returns them. A new start replaces it;
a newer agent taking over ends it. Only raw ranges, as `watch` always
sampled: resolve reflected paths to addresses (`get` prints them) first.
No file is written inside the target.

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
