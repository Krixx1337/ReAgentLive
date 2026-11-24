# Outside programs

*Home of:* the core as a lean in-process proxy for a program outside the game: what it gives, its limits, what fits, what is not built yet.

**TL;DR:** You can keep a tool's logic and window outside the game and
load only a small, stable DLL inside it that reads, writes and calls on
request. That works for values that change over seconds, not for anything
that must happen inside a frame. Expect about one request per host tick,
shared in turns with any other client.

The console is one such program; this page is for the others.

## The idea

Load one small, stable DLL into a game once and keep everything else
outside it. The DLL holds no feature logic, no menu and no drawing. The
outside program can be rebuilt and restarted freely while the game keeps
running.

The premise: **frame-perfect timing is not required.** An answer arrives a
tick or two after the question.

## What the core already gives

| The proxy needs | Where |
|---|---|
| Read, write, same-tick batches (held requests) | [protocol](protocol.md#exchange), [memory commands](commands/memory.md) |
| Guarded writes and `write`'s timed forms: `hold` (rewritten every tick until it expires) and `undo` (restores itself) | [memory commands](commands/memory.md) |
| References that refuse a replaced object; engine calls | the [Unreal pack](commands/unreal.md) |
| No code hooks, no threads of its own, no drawing (the opt-in pointer swaps: [design](design.md#principles)) | [design](design.md#principles) |
| A client library the program links | [clients](client.md) |
| A host that ticks the core | [hosting](hosting.md); the DLL, its entry and its tick source are the host author's |

A program that wants `get`, `batch` or the cache drives the console's
JSON mode instead of linking the client, as the Python client does.

## Limits

- **Throughput.** About one request per host tick, with short held bursts,
  each response up to 4 KB ([limits](protocol.md#limits)). A program that
  reads many values every frame is unmeasured; measure before planning one.
- **One request at a time** ([protocol](protocol.md#handshake)): the
  console and an outside program take the slot in turns, so a long held
  command of one delays the other's requests.
- **No drawing.** Visuals are drawn in the program's own window, never over
  the game.
- **Engine calls need the game thread.** A host ticking elsewhere offers
  memory commands and, if it opts in, object reads; calls only after
  `game-thread` (`on`) moves its tick there ([host thread](hosting.md#host-thread)).
- **Same build.** Host and program are built from one revision; the layout
  check refuses a mismatched pair ([handshake](protocol.md#handshake)).

## What fits

| Fits | Does not fit |
|---|---|
| Values that change over seconds: settings, economy, inventory, timers | Anything that tracks fast movement frame by frame |
| Tools and dashboards in their own window | Overlays drawn over the game |
| Holding a value with `hold`, or one guarded write | Logic that must run inside a frame: the Draw tick (`game-thread`) puts requests on the game thread, but each still waits for a tick, so frame-exact logic stays in a mod |
| Prototyping logic before it moves into a mod | |

A mod that already works in-process gains little by moving out: "inject
once, iterate outside" is what the console already gives for
investigation. If a mod does move, move scalar features first and judge
frame-critical ones last, by measurement.

## Not built yet

Each waits for a real program that needs it:

- a minimal client example ([backlog](ideas.md#20--example-client)); both
  example hosts exist ([own-thread](hosting.md#own-thread-example),
  [Unreal](hosting.md#unreal-example));
- throughput measurements with the example client, then, if needed,
  registered per-tick reads: the client names ranges once, the agent
  samples them every tick (`watch-start` is the start), and they expire when the
  client's heartbeat stops;
- a second channel, so two requests are in flight at once: the core's
  state moves into a per-channel context.
