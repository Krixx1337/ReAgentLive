# Protocol

The wire format between the agent (inside the target process) and a client.
Definitions: `core/protocol.h`, Unreal additions `ue/protocol_ue.h`.

## Section

One named, fixed-size shared-memory section (`Section`), created by the agent
on its first `Tick` and kept for the life of the process. The client opens it
by name and never opens the target process itself.

| Field | Written by | Meaning |
|---|---|---|
| `magic` | agent, last | `Magic` once the header is valid |
| `layout` | agent | `Host::layout`: `LayoutCore` plus each pack's `LayoutPart` |
| `agentPid` | agent | process id of the target |
| `opCount`, `ops` | agent, before `magic` | the op numbers the host's command table handles (up to 64) |
| `heartbeat` | agent | incremented every `Tick`; a client derives the tick rate |
| `requestSeq` | client | bumped after writing `request` |
| `responseSeq` | agent | set to the handled `requestSeq` after writing `response` |
| `owner` | agent | id of the agent instance that answers; a newer agent in the same process takes over and the older one stops ticking, after restoring its active trials once under their guards |
| `takeovers` | agent | how many agents in this process were replaced |
| `build` | agent | the answering agent's compile time (text, up to 23 characters) |
| `request`, `response` | client, agent | one slot each |

Sequence, heartbeat, owner and takeovers fields are accessed only through `std::atomic_ref`.

## Exchange

```text
client                                   agent (host thread, every Tick)
write request, requestSeq++  ─────────►  requestSeq != last handled?
                                           no  → return
                                           yes → copy request, run handler,
poll responseSeq             ◄─────────        write response, responseSeq = requestSeq
read response                              request held? wait for the next one (budgets)
```

- **One request in flight.** The agent copies the request before running it.
- **Hold.** A request with `hold = RequestHold` says more follow. After
  answering it, the agent spins on `requestSeq` for up to
  `Host::holdWaitMicroseconds` (default 200) and runs the next request in
  the same tick, until `Host::holdTickMicroseconds` (default 1000; waits and
  handlers after the tick's first request) are spent; then the next request waits for the next tick.
  A handler that returns `Continue` ends the hold for that tick. Zero in
  either budget turns hold off. The spin runs on the host's thread, so the
  tick budget bounds the stall it adds. A client that stops sending costs
  one wait.
- **Multi-tick handlers** return `Progress::Continue`; the agent calls them
  again each tick until done. A newer `requestSeq` abandons them.
- **A handler that throws** a C++ exception is answered `refused` with the
  reason `handler exception` and no data; the exception never leaves `Tick`.
  Access faults are not exceptions here: each handler guards its own copies.
- **No client:** `requestSeq` never changes; the agent pays one compare.
- **Timeouts are the client's.** While the host stops calling `Tick`, nothing
  is answered.

## Handshake

The client refuses a section whose `magic` (`DVLK`) is not set or whose
`layout` differs from its own sum. The sum takes in the size of every struct
that crosses the section (requests, responses and every row or spec packed
into `data`), so a client built before one changed is refused instead of
misreading it; a new wire struct is added to `LayoutCore` or the pack's
`LayoutPart`. There is no versioning: build agent and client from the same
source. The `ops` list tells a client which commands this
agent has, so frontends can refuse the rest locally.

## Ops

| Range | Owner |
|---|---|
| 1-255 | core |
| 256-4095 | engine packs |
| 4096 and up | games (the host's own ops) |

| Op | Value | Defined in |
|---|---|---|
| `read` | 1 | core |
| `write` | 2 | core |
| `modules` | 3 | core |
| `read-batch` | 4 | core |
| `search` | 5 | core |
| `pin` | 6 | core |
| `watch` (start, read, stop, info, roots; interval and followed root in `WatchSpec`, `protocol.h`) | 7 | core |
| `trial` | 8 | core |
| `xrefs` (relative-reference scan; request fields in `protocol.h`) | 9 | core |
| `region` (one `VirtualQuery`; response `RegionInfo`) | 10 | core |
| `find` | 256 | Unreal pack |
| `fields` | 257 | Unreal pack |
| `call` | 258 | Unreal pack |
| Object range read | 259 | Unreal pack |
| `describe` | 260 | Unreal pack |
| Array preview | 261 | Unreal pack |
| `func` | 262 | Unreal pack |
| `name` | 263 | Unreal pack |
| `funcs` | 264 | Unreal pack |
| Function parameters | 265 | Unreal pack |
| `census` | 266 | Unreal pack |
| `name-find` | 267 | Unreal pack |
| `each` | 268 | Unreal pack |
| `script` | 269 | Unreal pack |
| `refs` | 270 | Unreal pack |
| Describe many | 271 | Unreal pack |
| Exec entries | 272 | Unreal pack |
| `entries` | 273 | Unreal pack |

Ops not in the host's command table are answered with `unsupported op`.
Adding an op does not change the layout sum: the `ops` list advertises it,
and an older agent answers `unsupported op`.

`read-batch` takes the range count in `length` and that many `ReadSpan`
records in `data`. A successful response carries the execution heartbeat
in `detail`. Response data starts with one `Status` per range, followed
by the byte ranges in request order. Failed ranges occupy their requested
length and contain zero bytes.

`funcs` takes an object or class ref, an optional substring in `text`, and
the match offset in `length`. It returns `FunctionNameEntry` rows and a
next-page cursor in `detail` (zero on the last page).

Function parameters takes the UFunction address from `func` and a page
offset in `length`. It returns `FieldEntry` rows for parameters only.
`reserved[0]` carries `ParameterFlag` bits. `detail` packs the total parameter
count in the low 32 bits and the block size in the high 32 bits (`fields`
puts the listed type's object-table index there, so a client cache knows whose
table a page belongs to: an instance lists its class's). Named calls
use the existing `call` op after packing the block on the client, with the
inspected UFunction address in `args[1]`. The agent refuses if resolution
produces a different function. Raw calls leave `args[1]` zero.

## Request and response

| Request field | Use |
|---|---|
| `op`, `flags` | operation; op-specific flags (`each`: reuse) |
| `address` | memory or object address |
| `args[2]` | op-specific numbers (Unreal refs: `args[0]` = table index low, serial high) |
| `length` | byte count, or an op-specific number (page start for `modules` and `fields`) |
| `hold` | `RequestHold`: more requests follow (see Exchange); any op |
| `text` | NUL-terminated name or pattern, 128 bytes |
| `data` | payload, 4096 bytes, 16-byte aligned |

| Response field | Use |
|---|---|
| `status` | see below |
| `length` | bytes used in `data` |
| `detail` | op-specific number (totals, expected size, fault address) |
| `tick` | the agent heartbeat when the request finished, for every op |
| `reason` | short NUL-terminated text explaining a refusal, 64 bytes; may be empty |
| `data` | result, 4096 bytes |

## Statuses

| Status | Meaning |
|---|---|
| `ok` | done |
| `bad request` | malformed request or wrong argument (`reason` says which) |
| `memory not readable`, `memory not writable` | page preflight failed |
| `access faulted` | the copy faulted after the preflight; `detail` = address |
| `not found` | object, function or item does not exist (`reason` says which) |
| `refused` | valid request the agent will not run now (`reason` says why) |
| `unsupported op` | not in the host's command table |

Packs reuse these codes and explain in `reason`; they never add codes.

Object range reads use the full Unreal ref in `address` and `args[0]`,
the byte offset in `args[1]` and the byte count in `length`. The response
contains the copied bytes. The supported-op list advertises the handler.

`find` takes the name substring in `text`, an optional NUL-terminated class
name in `data` (`length` includes the terminator) and the starting slot in
`args[1]`, where scanning starts. `detail` packs the total (low 32 bits) and
the slot of the first match not returned (high 32 bits, 0 when none). Only
the first page (slot 0) scans the whole table and counts the total; later
pages report 0 and stop at the first match past a full page.

`census` takes an optional class name in `data` as for `find`, empty `text`,
and the first row of the page in `args[1]`. The response holds
`CensusEntry` rows (class ref, count, up to three sample refs, name), most
instances first; `detail` = `FindDetail(class count, next row or 0)`. The
agent keeps the last result: a page past row 0 of the same filter comes from
it, row 0 always rescans.

`name-find` takes the substring in `text` and the comparison index to resume
at in `args[1]`. The response packs an int32 index and a NUL-terminated name
(cut at 200 characters) per match; `detail` is the next cursor, 0 when the
pool is done.

`each` takes the class name in `data` and the first row in `args[1]`. Field
mode: empty `text`, `args[0]` = offset | size << 32. Call mode: the function
in `text` and the parameter block size in `args[0]`. `flags` bit 0 reuses the
instance list of the previous `each` of the same class. Rows are a `Ref` and
the value padded to 8 bytes (`EachStride`); `detail` = `FindDetail(rows, next
row or 0)`; `reason` is `capped` when more instances matched than 1024. Pages
past row 0 come from the cached rows of the same query.

`describe` takes a full ref, an address-only ref (index -1), or a weak ref
(address zero, nonnegative index and positive serial). The response contains
one `FindEntry`; `detail` is the size of the object's class (0 when unknown),
which covers native members past the last reflected field.

Describe many takes 1..32 refs of any of those forms in `data` (`length` =
16 per ref) and returns one `FindEntry` per ref in order, zeroed for a ref
that is gone or not an object; `detail` counts the ones found. `dump` uses it
for its field pointers and again for its array elements.

`entries` takes the object ref, `args[1]` = the field offset in the low 32
bits and the first index (arrays) or slot (sets, maps) in the high 32,
`length` = the element stride and `flags` = `ContainerKind` (1 array, 2 set,
3 map). In one tick it reads the header and as many elements as fit 4 KB:
arrays from the index, sets and maps as whole allocated slots from the slot.
A set or map header (`SparseHeader`, UE5's `TSparseArray`: data, slot count
and capacity, a bit array with four inline words, the free list) must pass
`SparseHeaderValid`, and its allocation bits must count the slot count minus
the free count, or it is refused. `detail` is `FindDetail(elements or
occupied slots, next index or slot, 0 when done)`.

Exec entries takes the first row in `args[1]` (address and `length` zero). Its
first page scans the object table, 1 ms per tick like `census`, for native
functions (`FUNC_Native`, with an exec thunk of their own) and caches them;
later pages come from that scan. Rows are `ExecEntry` (`exec`, `owner`,
`name`; zeroed when the function is gone), 30 per page; `detail` is
`FindDetail(rows, next row or 0)`. For labelling code in a static-analysis
tool.

`func` takes an object or class ref and exact function name in `text`, with
an optional declaring class before a colon. `length` is zero. The response
contains one `FunctionEntry` with both addresses, raw function flags and the
exec entry's executable-page check. Module and RVA display use `modules`.

`name` carries up to 128 int32 FName comparison indices in `data` (`length` =
4 x count). The response packs one NUL-terminated base name per index, empty
when the index cannot be decoded (decoding is fault-guarded), and `detail`
counts the names returned, fewer when the response fills.

Array preview uses the object ref, array header offset in `args[1]`, and
element stride in `length`. The response contains the bounded prefix and
reports the current element count in `detail`. The layout hash includes the
size of `FieldEntry`, which carries the reflected element type and stride.
For a single struct field or an array of structs, `FieldEntry` also carries
the struct object's ref and name; `fields` on that ref lists its members.

For a map or set, `FieldEntry` carries the key (a set's element) in the
element members, a map's value type, size, offset and struct, and the
element's `stride` (key, value and the two `int32` hash links). The layout
comes from the property itself: UE5's `FScriptMapLayout` at `MapLayoutOffset`
(0x80) or `FScriptSetLayout` at `SetLayoutOffset` (0x78), pack constants
because the SDK does not describe them. `ContainerLayoutValid` checks every
layout (key at 0, the value after it, adjacent hash links, the stride at
most 512 bytes); a failed check leaves `stride` 0 and the console shows the
raw header.

## Limits

| Limit | Value |
|---|---|
| Read | 4096 bytes |
| Write | 256 bytes |
| Modules | 64 per page |
| Reason | 64 bytes |
| Text | 128 bytes |
| Unreal call parameters | 1024 bytes |
| Unreal find results | 24 per page, plus the total and the next cursor (`FindDetail`); class name under 64 characters |
| Unreal fields | `MaxRead / sizeof(FieldEntry)` per page; page start ≤ 16384 |
| Unreal array preview | up to 8 elements of up to 512 bytes each, at most 4096 bytes in all |
| Unreal census | 30 classes per page; 3 samples per class |
| Unreal name-find | 64 names per page; names cut at 200 characters |
| Unreal each | 1024 instances; values 1..64 bytes |
