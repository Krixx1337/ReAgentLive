# Sampling over time

## Choosing the tool

- **Memory or a reflected field, seconds to minutes:** `watch-start`, then
  `watch-read` while it runs and `watch-stop` to end it. It returns at
  once; the agent keeps sampling while you work, and every interface has
  it. `--every` for minutes; `--follow <root>` survives a respawn
  ([capture that does not hold the console](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/commands/memory.md#a-capture-that-does-not-hold-the-console)).
- **Blocking `watch`** (blocking; interfaces reference): every tick for up
  to 30 s, and `--then` starts the action right after sampling starts; send
  the off call afterwards.
- **Named fields before and after:** `snap`, the user acts,
  `dump --changed`. `record` (blocking; interfaces reference) for 1-10 s
  of a few fields.
- **A short action that resets at once (a jump, a shot):** `snap` then
  `dump --changed` misses it once the state is back. Capture across it:
  `watch-start 5s <ref>.<Field>` on the owning component (movement, not
  the pawn), tell the user to act now, then `watch-read`; or `record` for
  a few seconds. Else `snap` at rest and `dump --changed` while the user
  holds the state mid-action.
- **Getters or mixed sources:** one `batch` (a `call-pure` and the `read`s)
  per sample keeps a getter and raw reads in one tick; loop it from Python.
  Polling gets at most one sample per host tick and misses ticks.
- **Data that lives about one tick** (replicated entries, events) slips
  past polling: write a small probe on the C++ client in a scratch folder,
  reading arrays with the same-tick array preview, one request per tick.

## What the game passed to a function

- `calls-start <target> <Owner:Function> 10s` records each call with its
  parameters (approval: it swaps one `UFunction` pointer). Use it when the
  question is an event ("did the hit register, for how much, on which
  bone?"); a watch only sees state afterwards.
- Pick the function with `funcs` on the class that receives the event.
  What the recorder sees and how it prints values:
  [call recorder](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/commands/unreal.md#call-recorder).
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
- Past what `dump` reads ([dump](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/commands/unreal.md#dump)):
  `read` the rest in chunks.

## Analysis

- "Still moving after X ms": comparing the last change time flags every
  double (low bits jitter). Compare the value at X ms with the value before
  the action and the final value instead.
- Watch a reflected value by its path (`watch-start 30s <ref>.Health`), not
  by an address and a guessed type: Blueprint floats are doubles, and an
  `f32` read returns the low half (0 for whole numbers) for the whole
  capture.
- Results are the changes with their times: read the transitions. Ask for
  `--all` only when the sample spacing itself is the question.
