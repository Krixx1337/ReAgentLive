# Sampling over time

## Choosing the tool

- **Memory or a reflected field, seconds to minutes:** `watch-start`, then
  `watch-read` while it runs and `watch-stop` to end it. It returns at
  once; the agent keeps sampling while you work, and every interface has
  it. Minutes-long sampling and following a respawn: `help` on `watch-start`
  (capture that does not hold the console: `docs/commands/memory.md#watch`).
  Named fields: `{"object": "<ref>", "field": ["Health"]}` ranges. Every
  sample to a file: `file`.
  A script that should wait: the Python session's `watch(seconds, ranges)`.
- **Named fields before and after:** `get` with `mode` `snap`, the user
  acts, `get` with `mode` `changed`:
  `{"verb": "get", "args": {"object": "0x1f2e3d4c50#123:4", "mode": "changed"}}`.
- **A short action that resets at once (a jump, a shot):** `snap` then
  `changed` misses it once the state is back. Capture across it:
  `{"verb": "watch-start", "args": {"duration": 5, "ranges": [{"object": "0x1f2e3d4c50#123:4", "field": ["Velocity"]}]}}`
  on the owning component (movement, not the pawn), tell the user to act
  now, then `watch-read`. Else `snap` at rest and `changed` while the user
  holds the state mid-action.
- **Getters or mixed sources, sampled over time:** a `run` plan with one
  `batch` step (a `call-pure` and the `read`s, one tick per sample) and
  `repeat` (`{"every": 0.5, "for": 10}`), not a Python loop: the plan file
  replays the measurement (`docs/commands/console.md#plans`). Run it from
  the console `-c` or Python; MCP leaves `run` out. Repeated calls get at
  most one sample per host tick and miss ticks.
- **Data that lives about one tick** (replicated entries, events) slips
  past repeated calls: `watch-start` samples every tick by default, so a
  range on its container catches it. A C++ client probe in a scratch
  folder only for data whose address moves every tick.

## What the game passed to a function

- `{"verb": "calls-start", "args": {"owner": "0x1f2e3d4c50#123:4", "function": "Owner:Function", "duration": 10}}`
  records each call with its
  parameters (approval: it swaps one `UFunction` pointer). Use it when the
  question is an event ("did the hit register, for how much, on which
  bone?"); a watch only sees state afterwards.
- Pick the function with `funcs` on the class that receives the event.
  What the recorder sees and how it prints values:
  call recorder (`docs/commands/unreal-calls.md#call-recorder`).
- Zero rows can mean the wrong function, not no event: record the native
  function a Blueprint calls, or the event that enters it.
- A container parameter that prints only its header: `call` the function
  yourself while recording, or `get` the state it left.
- `calls-read` while it runs; `calls-stop` restores at once.

## Questions and techniques

- "Why is this transition slow?": sample the progress getter and the
  visible result together while the user repeats the action. A gap that
  shrinks by a fixed fraction per sample is an interp toward a target; then
  hunt for two floats where one jumps and one follows, often in unreflected
  padding of a native camera modifier (`read` the object's tail).
- When a fix measures right but the user still sees the problem, ask what
  exactly moves before probing further: the visible thing may be another
  system than the value you fixed (a pose, not the field of view).
- "Which part of a big object moves?": snapshot the raw object in chunks at
  rest and at least twice in the steady state, keep floats that differ from
  rest but agree across steady snapshots, map each offset to the reflected
  field that contains it (anim graph nodes are fields too), then watch only
  those offsets.
- Past what `get` reads (`docs/commands/unreal-fields.md#get`):
  `read` the rest in chunks.

## Analysis

- "Still moving after X ms": comparing the last change time flags every
  double (low bits jitter). Compare the value at X ms with the value before
  the action and the final value instead.
- Watch a reflected value by its path (a `{"object": "<ref>", "field": ["Health"]}`
  range), not
  by an address and a guessed type: Blueprint floats are doubles, and an
  `f32` read returns the low half (0 for whole numbers) for the whole
  capture.
- Results are the changes with their times: read the transitions. Ask for
  every sample (`all`; `help` on `watch-read`) only when the sample spacing itself is the
  question.
