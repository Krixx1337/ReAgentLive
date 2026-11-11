# Sampling over time

## Choosing the tool

- **Memory values, every tick:** `watch <s> <addr>:<len>...` samples up to 8
  ranges inside the agent, one row per tick, no round trips. Use it whenever
  the values are plain memory.
- **Over minutes, or while you keep working:** `watch --start 10m --every
  10s <ranges>` returns at once and the agent keeps sampling; the console
  stays free and may even exit. `watch --read` (any later run) gives the
  rows so far, `watch --stop` ends it (Python: `watch_start`, `watch_read`,
  `watch_stop`). `--follow pawn` (roots in `status`) takes offsets from the
  host's root and continues on its replacement after a respawn: rows mark
  `lost` and `resumed`, `gaps` lists the holes. A refused start names the
  longest duration that fits the buffer.
- **Drive the action yourself:** `watch <s> <ranges> --then call <component>
  <Setter> 01` starts the action right after sampling starts. Send the off
  call afterwards.
- **Getters or mixed sources:** one `batch { call ...; read ... }` per sample
  keeps a getter and raw reads in the same tick; loop it from the Python
  session client (`items, same_tick = s.batch([...])`). Console polling gets
  at most one sample per host tick and misses ticks.
- **Named fields:** `record` for 1-10 s of up to 8 fields; `snap`, then
  `dump --changed` for before and after (`s.snap(ref)`, `s.dump(ref,
  changed=True)`; a snapshot file carries the baseline across runs).
- **Commands that always span ticks** (`each`, `census`, `search`, `refs`,
  `xrefs`, `touches`) break a `batch`'s tick: an `each` ran about 300 ticks
  after a `read` in the same batch. The batch names the culprit in
  `spans_ticks` and gives every result its own `tick`. For values that must
  line up use `get`, `read-batch`, or a batch without those (pair raw reads
  with `get` on the instance, a component's location field).
- Data that lives about one tick (replicated entries, events) slips past
  console polling. Write a small probe on the ReAgentLive C++ client library in a
  scratch folder; read arrays with the same-tick array preview and spend one
  request per tick.

## Questions and techniques

- "Why is this transition slow?": sample the progress getter and the visible
  result together while the user repeats the action. A gap that shrinks by a
  fixed fraction per sample is an interp toward a target; then hunt for two
  floats where one jumps and one follows, often in unreflected padding of a
  native camera modifier (`read` the object's tail).
- When a fix measures right but the user still sees the problem, ask what
  exactly moves before probing further: the visible thing may be another
  system than the value you fixed (a pose, not the field of view).
- "Which part of a big object moves?": snapshot the raw object in chunks at
  rest and at least twice in the steady state, keep floats that differ from
  rest but agree across steady snapshots, and map each offset to the
  reflected field that contains it (anim graph nodes are fields too). Then
  `watch` only those offsets.
- `dump` reads values only in the object's first 64 KB; fields past it list
  their offsets without values (a weapon anim layer had 619 of 1,244). `read`
  those in chunks of up to 4 KB.

## Analysis

- "Still moving after X ms": comparing the last change time flags every
  double (low bits jitter). Compare the value at X ms with the value before
  the action and the final value instead.
