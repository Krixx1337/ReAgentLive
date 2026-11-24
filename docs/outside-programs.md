# Outside programs

*Home of:* the core as a lean in-process proxy for a program outside the game: the premise, its limits and what fits.

**TL;DR:** You can keep a tool's logic and window outside the game and
load only a small, stable DLL inside it that reads, writes and calls on
request. That works for values that change over seconds, not for anything
that must happen inside a frame. Expect about one request per host tick,
shared in turns with any other client.

The console is one such program; this page is for the others.

## The idea

Load one small, stable DLL into a game once and keep everything else
outside it. The DLL holds no feature logic, no menu and no drawing; the
outside program can be rebuilt and restarted freely while the game keeps
running. The premise: **frame-perfect timing is not required.** An answer
arrives a tick or two after the question.

What the proxy rests on: [protocol](protocol.md#exchange) (held same-tick
requests), [memory commands](commands/memory.md) (guarded, `hold` and `undo`
writes), the [Unreal pack](commands/unreal.md) (refs, engine calls),
[clients](client.md) (the library to link) and [hosting](hosting.md) (the
host that ticks the core). A program that wants `get`, `batch` or the cache
drives the console's JSON mode instead, as the Python client does.

## Limits

- **Throughput.** About one request per host tick, with short held bursts,
  each response small ([limits](protocol.md#limits)). A program that reads
  many values every frame is unmeasured; measure before planning one.
- **One request at a time** ([protocol](protocol.md#handshake)): the console
  and an outside program take the slot in turns, so a long held command of
  one delays the other. A second channel waits for a real need
  ([ideas](ideas.md)).
- **No drawing.** Visuals are drawn in the program's own window.
- **Engine calls need the game thread** ([host thread](hosting.md#host-thread)).
- **Same build.** Host and program are built from one revision; the layout
  check refuses a mismatched pair ([handshake](protocol.md#handshake)).

## What fits

| Fits | Does not fit |
|---|---|
| Values that change over seconds: settings, economy, inventory, timers | Anything that tracks fast movement frame by frame |
| Tools and dashboards in their own window | Overlays drawn over the game |
| Holding a value with `hold`, or one guarded write | Logic that must run inside a frame: the Draw tick puts requests on the game thread, but each still waits for a tick, so frame-exact logic stays in a mod |
| Prototyping logic before it moves into a mod | |

A mod that already works in-process gains little by moving out: "inject
once, iterate outside" is what the console already gives for investigation.
If a mod does move, move scalar features first and judge frame-critical
ones last, by measurement.
