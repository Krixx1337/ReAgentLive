# Protocol

*Home of:* the wire format between agent and client: the shared section, exchange and hold, handshake, op numbers, request and response, statuses, limits.

**TL;DR:** The agent and its client talk through one named shared-memory
block in the target process: one request, one response, no sockets and no
process handle. You need this page only to write a client or change an op;
everyone else uses the console. Agent and client must come from the same
revision: the layout check refuses a mismatched pair.

Definitions: `core/protocol.h`, Unreal additions `packs/ue/protocol_ue.h`.

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
| `requestSeq` | client | `RequestWriting` (0) while it writes `request`, then the next number (never 0) |
| `responseSeq` | agent | set to the handled `requestSeq` after writing `response` |
| `owner` | agent | id of the agent instance that answers (see takeover below) |
| `takeovers` | agent | how many agents in this process were replaced |
| `hostThread` | agent | the thread the host ticks on, as it declared it (`HostThread`: engine, borrowed or own); unset is refused at open |
| `build` | agent | the loaded build as the host names it (`Host::build`, such as the image's link time), else the agent library's compile time, which an incremental build can leave stale (text, up to 47 characters) |
| `request`, `response` | client, agent | one slot each |

Sequence, heartbeat, owner and takeovers fields are accessed only through `std::atomic_ref`.

**Takeover on re-inject.** A second agent in the same process (the host
injected again) opens the same section, writes its own `owner` and
answers from then on. The earlier one sees another owner, restores its
active `undo` writes once under their guards and stops ticking; a capture
it ran ends with it.

## Exchange

```text
client                                   agent (host thread, every Tick)
requestSeq = RequestWriting
write request, requestSeq = next ──────►  requestSeq not 0 and != last handled?
                                           no  → return
                                           yes → copy request; requestSeq changed? drop it
                                                 run handler,
poll responseSeq             ◄─────────        write response, responseSeq = requestSeq
read response                              request held? wait for the next one (budgets)
```

- **One request in flight.** The agent copies the request before running it.
- **A seqlock on the slot.** After a timeout the client reuses the slot
  while the agent may still pick up the old number. The marker before the
  write and the agent's re-read of `requestSeq` after its copy (behind an
  acquire fence) mean a copy that overlapped a write is dropped, never run;
  the newer request runs on the next tick.
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
into `data`) and `ProtocolRevision`, which is bumped when a field's meaning
changes but no size does. So a client built before a change is refused
instead of misreading it; a new wire struct is added to `LayoutCore` or the
pack's `LayoutPart`. Build agent and client from the same source.

**One client at a time.** A client that attaches creates the named mutex
`<section>-client`; while another client holds it, the attach is refused
as `Busy` (two clients would overwrite each other's request). Windows
closes it when the client exits. The holder also writes who it is (pid,
caller kind, when it attached) to the mapping `<section>-client-info`,
client to client: the agent never reads it, and `Client::Holder` names the
holder to a client that finds the slot taken. The console waits for the
slot up to `--busy-wait` (default 10 s) before it reports `busy`, naming
the holder ([console](commands/console.md#agent-and-console)).

**The image window.** `ImageCopy` answers with the bytes of a loaded image
in a second mapping, `<section>-image` (4 MB), so a console scan that needs
a whole image (signatures, displacements) moves 4 MB a request instead of
4 KB. The agent creates it on the first copy and keeps it; the client maps
it read-only. Only image pages (`MEM_IMAGE`, committed and readable) are
copied; the response's bitmap marks them, and the rest reads zero. Copies
run on the search worker when the host allows it
(`Host::backgroundSearch`) and the worker is idle, else in 2 ms slices per
tick. An image's data pages can change while copied: the window is for
code and constants, never for values. No struct crosses it, so the layout
sum is unchanged; a console asks `ops` and falls back to reads without it.

The `ops` list tells a client which commands this agent has, so frontends
can refuse the rest locally.

## Ops

| Range | Owner |
|---|---|
| 1-255 | core |
| 256-4095 | engine packs |
| 4096 and up | games (the host's own ops) |

Each pack owns one slot of 256 ops (`op::PackSlot(n)`, 15 slots), so packs
never collide in one host; a pack's protocol header asserts its last op
stays inside its slot.

| Slot | Ops | Pack |
|---|---|---|
| 0 | 256-511 | Unreal (`ue`) |

| Op | Value | Defined in |
|---|---|---|
| `read` | 1 | core |
| `write` | 2 | core |
| `modules` | 3 | core |
| `read-batch` | 4 | core |
| `search` (and the search job) | 5 | core |
| `pin` (`write`'s `hold`) | 6 | core |
| `watch` (start, read, stop, info, roots) | 7 | core |
| `trial` (`write`'s `undo`) | 8 | core |
| `xrefs` | 9 | core |
| `region` | 10 | core |
| Image copy (the image window) | 11 | core |
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
| `calls-start`, `calls-read`, `calls-stop` | 274, 275, 276 | Unreal pack |
| `enum` | 277 | Unreal pack |
| `game-thread` | 278 | Unreal pack, hosts that link `reagentlive::ue-draw` |

Ops not in the host's command table are answered with `unsupported op`.
Adding an op does not change the layout sum: the `ops` list advertises it.

**Each op's fields** (what `address`, `args`, `length`, `flags`, `text`
and `data` carry, and what `detail` packs) are written once, in the
comment beside the op's constant: `core/protocol.h` for core ops,
`packs/ue/protocol_ue.h` for the Unreal pack's. What the comments do not
show in one place:

- **Paged scans** (`find`, `census`, `each`, Exec entries) pack the total
  and the next cursor into `detail` (`FindDetail`). Only the first page
  scans the whole table; later pages come from the cursor or the agent's
  kept result. `find` takes `FindExact` (1: the whole name, stop at the
  first match) and `FindLive` (2: live instances only, no class default
  or archetype objects); any other flag is a bad request.
- **The object filter** (`find`, `each`, `census`): `data` holds the class
  name, then optionally an outer's name text (under 64 bytes) or an outer
  object; an object passes when an outer up its chain, at most 16 steps,
  is that object or has a name containing the text, any case. An outer
  object that is gone is refused before the scan.
- **Fields pages** (`fields`, function parameters): a `FieldsPageHeader`,
  that many `FieldRecord`s (44 bytes: offset, size, element size, stride,
  a map value's offset and size, bool mask, parameter flags, two struct
  ref numbers and eight string offsets: name, type, owner, element type,
  struct name, value type, value struct name and subtype), the page's
  struct refs (each once), then a table of NUL-terminated strings, each
  once. Names travel whole: a field whose strings no longer fit ends the
  page. `FieldsPageWriter` and `ReadFieldsPage` (`protocol_ue.h`) build
  and check a page; the client refuses a page whose offsets or ref
  numbers fall outside it. A map or set's layout comes from the property
  itself (`FScriptMapLayout`, `FScriptSetLayout` in `engine_layout.h`),
  checked by `ContainerLayoutValid`; a failed check leaves `stride` 0.
- **Named calls** pack the parameter block on the client and send it with
  the inspected UFunction in `args[1]`; the agent refuses if resolution
  gives another function. `CallPure` (2) refuses, before calling, a
  function that is neither const nor blueprint-pure or whose outputs own
  memory. A fault inside `ProcessEvent` answers `access faulted`.
- **`game-thread`** takes its action in `flags` (0 status, 1 on, 2 off)
  and answers a `GameThreadState`.

## Request and response

| Request field | Use |
|---|---|
| `op`, `flags` | operation; op-specific flags |
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

## Limits

The constants are in `protocol.h` and `protocol_ue.h`.

| Limit | Value |
|---|---|
| Read | 4096 bytes; `read-batch` 8 ranges |
| Write | 256 bytes |
| Hold (`pin`) | 8 at once, 64 bytes each, up to 60 s |
| Undo (`trial`) | 8 at once, 64 bytes each, up to 60 s; with a guarded object, the address within 4 KB after it |
| Watch | 8 ranges of up to 512 bytes, a 2 KB row, a 4 MB buffer; every tick up to 30 s, with an interval up to 1 h |
| Search | a 64-byte pattern; 512 hits per page |
| Image copy | 4 MB a request, image pages only |
| Modules | 64 per page |
| Reason | 64 bytes |
| Text | 128 bytes |
| Unreal call parameters | 1024 bytes |
| Unreal find results | 24 per page, plus the total and the next cursor (`FindDetail`); class name under 128 characters |
| Unreal fields | as many as one 4 KB page holds (records, refs and strings; up to 92); page start ≤ 16384 |
| Unreal array preview | up to 8 elements of up to 512 bytes each, at most 4096 bytes in all |
| Unreal describe many | 28 refs |
| Unreal census | 30 classes per page; 3 samples per class |
| Unreal name-find | 64 names per page; names cut at 200 characters |
| Unreal name | 128 indices |
| Unreal each | 1024 instances; values 1..64 bytes |
| Unreal call recorder | 8 recordings, 256 rows each, 1 KiB blocks, up to 60 s |
| Unreal script | 1 MiB of bytecode |
