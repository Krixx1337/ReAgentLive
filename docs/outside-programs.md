# Outside programs

Using ReAgentLive's core as a lean in-process proxy: one small DLL in the
game that only reads, writes and calls on request, and a program outside
that holds all the logic and any GUI. The console is one such program; this
page is for the others.

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
| Guarded writes; pins that hold a value every tick and expire on their own; trial writes that restore themselves | [memory commands](commands/memory.md) |
| References that refuse a replaced object; engine calls | the [Unreal pack](commands/unreal.md) |
| No hooks, no threads of its own, no drawing | [design](design.md#principles) |
| A client library the program links | [clients](client.md) |
| A host that ticks the core | [hosting](hosting.md); the DLL, its entry and its tick source are the host author's |

A program that wants `dump`, `get`, `batch` or the cache drives the console's
JSON mode instead of linking the client, as the Python client does.

## Limits

- **Throughput.** About one request per host tick, with short held bursts,
  each response up to 4 KB ([limits](protocol.md#limits)). A program that
  reads many values every frame is unmeasured; measure before planning one.
- **One client at a time.** A second one is refused as `busy`; the console
  and an outside program cannot run together.
- **No drawing.** Visuals are drawn in the program's own window, never over
  the game.
- **Engine access needs the engine's thread.** A host ticking elsewhere
  offers memory commands only ([host thread](hosting.md#host-thread)).
- **Same build.** Host and program are built from one revision; the layout
  check refuses a mismatched pair ([handshake](protocol.md#handshake)).

## What fits

| Fits | Does not fit |
|---|---|
| Values that change over seconds: settings, economy, inventory, timers | Anything that tracks fast movement frame by frame |
| Tools and dashboards in their own window | Overlays drawn over the game |
| Holding a value with a pin, or one guarded write | Logic that must run inside a frame (it needs the frame hook a mod owns) |
| Prototyping logic before it moves into a mod | |

A mod that already works in-process gains little by moving out: "inject
once, iterate outside" is what the console already gives for
investigation. If a mod does move, move scalar features first and judge
frame-critical ones last, by measurement.

## Not built yet

Each waits for a real program that needs it:

- a minimal client example ([backlog](ideas.md#20--example-client-and-sdk-guide)); the example host exists
  ([own-thread example](hosting.md#own-thread-example));
- throughput measurements with the example client, then, if needed,
  registered per-tick reads: the client names ranges once, the agent
  samples them every tick (`watch` is the start), and they expire when the
  client's heartbeat stops;
- a second channel, so the console and a program run together: the core's
  state moves into a per-channel context. The one case so far (a long
  capture holding the console) was solved by `watch-start`;
- one Unreal pack binary for every Unreal game, decoupled from SDK headers;
- a "bring your own SDK" guide for using the Unreal pack in another game
  (the same backlog entry).
