# Working in parallel

Several agents, subagents or a background run on one agent. Clients take
its slot in turns, a request at a time: parallel work is safe when nobody
leans on state another client can replace. How a frontend starts and
follows a background run: the console-driving reference.

## Split the work

- Subagents are read-only. One agent (the lead) writes, calls and holds
  approvals; a subagent that finds a write reports it to the lead.
- Before spawning, the lead enables every pack the subagents need: a
  subagent never changes packs, so a verb of a pack that is off is out of
  its reach (it falls back to a weaker verb).
- Brief each subagent with its own question, the literal refs it starts
  from (the host's world refs, objects already found), the read-only rule
  and the skills to load first. Results carry literal refs, so a subagent
  never needs another's session state.
- Values from two calls, or two clients, are never from one moment. For
  values that must line up: one `get` with several `paths`, or a `batch`
  of `read`s. A `batch` holding `get` spans ticks through its lookups
  (2-3 ticks each, other clients or not), and a batch of `read`s often
  spans 2-3. Values that change every frame and must line up (camera and
  projectile, aim and target): `watch-start`, which samples them in one
  tick, every tick.

## Shared resources: one at a time

- **One capture** (`watch-start`) per agent: a second start replaces the
  first, whoever owns it. Only the lead captures; the others wait for its
  result or read its file.
- **One search job** per agent: any client's new `search` stops the
  running one, and only the last job answers `search-job`. Only one client
  searches at a time; others wait for its job to end.
- **`census` scans on every call** (a repeat with the same filter costs the
  same). Let one client census and share the rows, instead of each
  subagent running its own.

## Long work

- A long `search`: start it with a short `wait`
  (`{"verb": "search", "args": {"type": "f32", "value": 100.0, "wait": 1}}`)
  and poll `search-job` between other calls, instead of one call that
  blocks its caller for the whole scan.
- Sweeps and label checks (`disp-scan`, `labels-check`, `labels-sign`) give
  the slot up between blocks: start them as a background run and keep
  exploring.
- Keep `batch` short beside others: it holds the slot for its whole length.

## Busy

- A `busy` refusal names the holder: usually a long held command. Retry
  once; if it repeats, find the holder (another session's batch or run) and
  wait for it or stop that run.
- `usage` shows `slot_ms` per verb. Beside a background run, the run's
  verb carries most of it and interactive calls wait a few milliseconds
  each: expected. Interactive calls waiting long mean a client holds too
  long (a big `batch`, a long verb run directly): move it to a background
  run or split it.
