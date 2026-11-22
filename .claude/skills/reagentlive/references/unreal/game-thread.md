# Getting onto the game thread

Trigger: `host_thread` is not `engine`, or a verb is refused `not on the
game thread`. Which verbs need it:
off the engine's thread (`docs/commands/unreal.md#off-the-engines-thread`).
`game-thread` arguments and modes: `help game-thread`.

## Before asking

- `status` shows `host_thread: engine`: already on it; nothing here.
- `game-thread` answers unsupported: the host does not link
  `reagentlive::ue-draw`. Use the host's own switch if its notes name one;
  else stay at reads and tell the user.
- Can reads answer the question (`get`, `dump`, `fields`, `entries`, a
  capture)? Then skip the switch.

## Confirm Draw, read-only

The tick runs through the game viewport's Draw: entry
`ViewportDrawIndex` of the table at `ViewportInterfaceOffset` in the
viewport client (`src/reagentlive/packs/ue/engine_layout.h`). Confirm it
in every new game before the first `on`:

1. `find --cls GameViewportClient --live` (games subclass it); read the
   table pointer at `<ref>+ViewportInterfaceOffset` (0x28 in `StockUE5`)
   and its first entries.
2. Every entry, and the table itself, lies in the game's image; entry 2 is
   often an empty function (`RedrawRequested`'s default body).
3. Decompile entry `ViewportDrawIndex` (4): a large function taking `(this, Viewport, Canvas)`
   that names `CanvasObject` and `DebugCanvasObject` is
   `UGameViewportClient::Draw`. Anything else: do not switch; report what
   that entry is.

## Switch on (approval)

- Ask: `game-thread on` writes one pointer in the viewport object (its
  table swapped for a private copy). State the viewport address, the table
  pointer now, and the effect.
- After `on`: `game-thread` shows the mode and the hooked Draw's RVA;
  `status` shows `host_thread: engine` once it ticks. Waiting: armed,
  installs on a later host tick (any Unreal verb locates the viewport).
  A conflict: not installed; report its text, do not retry. Modes:
  `help game-thread`.
- **Known-answer call**, the first call on a new host or in a new game:
  ask the user first (SKILL.md rules), then `call-pure
  Default__KismetMathLibrary Add_IntInt A=2 B=3` returns 5. Only then
  trust the game-thread verbs; later pure getters need no approval.
- `game-thread off` puts the table back. Before ending the session, ask
  the user to test the host's stop: it must put back what the switch
  replaced.
