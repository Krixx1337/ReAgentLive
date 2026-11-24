# Protocol

*Home of:* the wire protocol's rules between agent and client: the shared section and its takeover, exchange and hold, the handshake, op ranges.

**TL;DR:** The agent and its client talk through one named shared-memory
block in the target process: one request, one response, no sockets and no
process handle. You need this page only to write a client or change an op;
everyone else uses the console. Agent and client must come from the same
revision: the layout check refuses a mismatched pair.

The code is the source of truth for structs, op numbers, statuses and
limits; this page does not mirror them. Core: `core/protocol.h`. Unreal
pack: `packs/ue/protocol_ue.h` (request bounds: `packs/ue/request_ue.h`).
Each op's fields are documented in the comment beside its constant.

## Section

One named, fixed-size shared-memory section (`Section`), created by the agent
on its first `Tick` and kept for the life of the process. The client opens it
by name and never opens the target process itself.

The agent writes `magic` last, after the header (layout sum, op list, host
thread, build) is valid. Sequence, heartbeat, owner and takeover fields are
accessed only through `std::atomic_ref`. The `ops` list is the op numbers
the host's command table handles.

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
  answering it, the agent spins on `requestSeq` for a short wait
  (`Host::holdWaitMicroseconds`) and runs the next request in the same
  tick, until the tick budget (`Host::holdTickMicroseconds`) is spent; then
  the next request waits for the next tick. A handler that returns
  `Continue` ends the hold for that tick; zero in either budget turns hold
  off. The spin runs on the host's thread, so the tick budget bounds the
  stall it adds.
- **Multi-tick handlers** return `Progress::Continue`; the agent calls them
  again each tick until done. A newer `requestSeq` abandons them.
- **A handler that throws** a C++ exception is answered `refused` with the
  reason `handler exception` and no data; the exception never leaves `Tick`.
  Access faults are not exceptions here: each handler guards its own copies.
- **No client:** `requestSeq` never changes; the agent pays one compare.
- **Timeouts are the client's.** While the host stops calling `Tick`, nothing
  is answered.
- **Statuses:** packs reuse the core codes and explain in `reason`; they
  never add codes.

## Handshake

The client refuses a section whose `magic` is not set or whose `layout`
differs from its own sum. The sum takes in the size of every struct that
crosses the section (requests, responses and every row or spec packed into
`data`) and `ProtocolRevision`, which is bumped when a field's meaning
changes but no size does. So a client built before a change is refused
instead of misreading it; a new wire struct is added to `LayoutCore` or the
pack's `LayoutPart`. Build agent and client from the same source.

**One request at a time, clients in turns.** Any number of clients
attach; one request is in flight. The slot is the named mutex
`<section>-client` every client opens: a request takes it for its own
length, a `Client::Hold` for its life (a held command stays in one host
tick), an image copy until it has read the window. Two clients sending at
once would overwrite each other's request. A long verb gives the slot up
for its console-side work (`Client::Turn`), so another client's call waits
about one block, not the whole verb. A request that finds the slot taken
waits up to the client's busy wait (`--busy-wait` in the console), then is
refused as `Busy`, naming the holder. A client that dies holding it
abandons the mutex, and the next waiter takes it. Whoever takes the slot
writes who it is (pid, caller kind, since when) to the mapping
`<section>-client-info`, client to client: the agent never reads it.

**The image window.** `ImageCopy` answers with the bytes of a loaded image
in a second mapping, `<section>-image`, so a console scan that needs a
whole image (signatures, displacements) moves megabytes a request instead
of 4 KB. The agent creates it on the first copy and keeps it; the client
maps it read-only. Only committed, readable image pages are copied; the
response's bitmap marks them, and the rest reads zero. Copies run on the
search worker when the host allows it (`Host::backgroundSearch`) and it is
idle, else in short slices per tick. An image's data pages can change while
copied: the window is for code and constants, never for values. No struct
crosses it, so the layout sum is unchanged; a console checks the `ops` list
and falls back to reads without it.

The `ops` list tells a client which commands this agent has, so frontends
can refuse the rest locally.

## Ops

| Range | Owner |
|---|---|
| 1-255 | core |
| 256-4095 | engine packs |
| 4096 and up | games (the host's own ops, from `op::GameFirst`) |

Each pack owns one slot of 256 ops (`op::PackSlot(n)`), so packs never
collide in one host; a pack's protocol header asserts its last op stays
inside its slot. Slot 0 (256-511) is the Unreal pack (`ue`).

Ops not in the host's command table are answered with `unsupported op`.
Adding an op does not change the layout sum: the `ops` list advertises it.

Semantics the constants' comments cannot carry:

- **Paged scans** (`find`, `census`, `each`, exec entries) pack the total
  and the next cursor into `detail`. Only the first page scans the whole
  table; later pages come from the cursor or the agent's kept result.
- **Named calls** pack the parameter block on the client and send it with
  the inspected UFunction; the agent refuses if resolution gives another
  function. `CallPure` refuses, before calling, a function that is neither
  const nor blueprint-pure or whose outputs own memory. A fault inside
  `ProcessEvent` answers `access faulted`.
- **`owner` and `route`** run over ticks like the paged scans but answer in
  one reply; the console names the fields, the agent only finds objects and
  offsets.

## Limits

Every limit (read and write sizes, pin, undo and watch budgets, page sizes,
text and data lengths, the Unreal pack's bounds) is a `constexpr` in
`core/protocol.h` or `packs/ue/protocol_ue.h`. Those constants are the
limits; they are not repeated here.
