# Sampling over time

## Choosing the tool

- **Memory or a reflected field, seconds to minutes:** `watch-start`, then
  `watch-read` while it runs (with `stop` to end it). It returns at
  once; the agent keeps sampling while you work, and every interface has
  it. Minutes-long sampling and following a respawn: `help` on `watch-start`
  (`docs/commands/memory.md#watch`). A script that should wait: the Python
  session's `watch(seconds, ranges)`.
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
  `repeat` (`{"every": 0.5, "for": 10}`: 20 runs), not a Python loop: the
  plan file replays the measurement (`docs/commands/console.md#plans`). Run
  it from the console `-c` or Python; MCP leaves `run` out. Repeated calls
  get at most one sample per host tick and miss ticks.
- **Plan steps are sequential:** two steps never read the same moment.
  Values that must agree (a position and the world it is in) go in one
  `batch` step; its line carries every item's envelope.
- **Data that lives about one tick** (replicated entries, events) slips
  past repeated calls: `watch-start` samples every tick by default, so a
  range on its container catches it. A C++ client probe in a scratch
  folder only for data whose address moves every tick.

## What the game passed to a function

- `{"verb": "calls-start", "args": {"object": "0x1f2e3d4c50#123:4", "function": "Owner:Function", "duration": 10}}`
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
- `calls-read` while it runs; with `stop` it restores at once.

## Pitfalls

- "Why is this transition slow?": sample the progress getter and the
  visible result together while the user repeats the action. A gap that
  shrinks by a fixed fraction per sample is an interp toward a target.
- A fix measures right but the user still sees the problem: ask what
  exactly moves; the visible thing may be another system than the value
  you fixed.
- "Still moving after X ms": comparing the last change time flags every
  double (low bits jitter). Compare the value at X ms with the value
  before the action and the final value.
- Watch a reflected value by its path (a `{"object": "<ref>", "field": ["Health"]}`
  range), not by an address and a guessed type: Blueprint floats are
  doubles, and an `f32` read returns the low half (0 for whole numbers)
  for the whole capture.
- Results are the changes with their times: read the transitions. Ask for
  every sample (`all`) only when the sample spacing is the question.
- Past what `get` reads: `read` the rest in chunks.
