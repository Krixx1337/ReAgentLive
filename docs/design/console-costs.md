# Design: console costs

*Home of:* why the console works the way it does for an agent that pays per call and per character: holds and round trips, output weight, the usage log; with their rejected alternatives.

**TL;DR:** AI agents pay for every tool call and every character of output.
These are the decisions that cut both and what was rejected. Part of
[design](README.md).

## Round trips

AI agents drive ReAgentLive one tool call at a time; inside a command, each
request waits for a host tick. So the order of work is fewer tool calls
(state kept between runs, commands that do a whole lookup), then less output
(filters), then fewer and cheaper requests inside a command.

- **Hold, not a packed batch op.** A request may say more follow; the agent
  waits a bounded time on the host's thread and runs the next one in the same
  tick. A packed op was rejected: sub-requests need the small `text` field,
  all results would share one response, and it cannot do dependent reads.
  Same tick is best effort; every response carries its tick so a miss is
  visible, and the per-tick budget bounds the stall the host pays.
- **No fast lane for memory reads on a worker thread:** reads off the tick
  would be torn against the game, and `watch-start` already samples every
  tick. The exception is the image window: code and constants of a loaded
  image do not change under a scan ([native code](native.md#fast-lane)).
- **No `ReadProcessMemory` from the client:** the client would open the
  target process, which the design avoids.
- **One request at a time, clients in short turns**
  ([protocol](../protocol.md#handshake)). A client takes the slot per
  request, a held command for its length, and a long verb gives it up for
  each block's or row's console-side work. The cost was long verbs blocking
  their caller, not clients blocking each other, so long work runs as a
  frontend's background run (its own console) and the agent stays at one
  request in flight, with no context per client. A call waits up to
  `--busy-wait`, then refuses as `busy` naming the holder; every call's wait
  is in the usage log (`slot_ms`). Trade-off: one long agent request (a
  sliced `search` of the whole address space) still blocks everyone, so
  background work uses `search-job`.
- **Rejected:** a client holding the slot for its whole life (a session
  blocked every other call, or paid a console start per call); two requests
  in flight (a context per channel: [ideas](../ideas.md#38--two-requests-in-flight-channels),
  only if `slot_ms` shows real waiting); an agent job table (the slow verbs
  are console-side loops); a broker process queueing every client; an
  unbounded wait (a stuck holder would hang every tool call).
- **Every refusal reaches the usage log,** including ones the MCP adapter
  makes itself (pack off, batch item past the packs, schema refusal): it
  hands each to a one-shot console as a call with `refused`. The adapter
  never appends to the log itself: one writer, one format.
- **No session daemon.** The console's cache file gives one-shot runs their
  state, tied to one agent instance by `Section::owner`. Field tables are
  trusted only when page 0 of the live table matches.
- **Client waits spin, then sleep at 1 ms** with the timer resolution raised
  in the client process only; the default 15.6 ms timer made every round
  trip cost more than a host tick. Held requests spin briefly at raised
  priority: a client that slept through one response missed the agent's
  short wait for every later request too.
- **Host logging counts against the hold budget:** a host that logs every
  successful lookup synchronously gets one held request per tick; suppress
  success logs for read-only ops.

## Output weight and dead ends

Decisions taken where time and context went for no gain in live sessions.

- **Compact values by default.** A struct prints as `{Name: value}`; member
  metadata dominated nearly every command's output. One rule for every
  struct, vectors included. `meta` restores the layout; `get` with `mode`
  `types` keeps it.
- **No Python keyword as an argument name** (`cls`, `from`, `start`, `type`),
  so a JSON call, a `batch` entry, the generated function and the tool spell
  it the same. Letting the generator append `_` was the cause of a tool
  saying `class_` where `batch` wanted `class`. A table test refuses a
  keyword.
- **Compact rows, layout on request.** A `get` row is its name, offset and
  value; the offset stays because it is the bridge to raw reads. `path` and
  `ok` went because rows keep the order asked. The `batch` envelope kept its
  shape: one envelope for every command beats the bytes.
- **`each` is a table** because most questions are "these fields over every
  instance"; the scan dominates the cost, so columns share one scan. A
  function stays a single column.
- **Refusals state the form that works,** with their limit's number: a
  one-hop chain whose last offset only adds is refused with the form that
  follows a pointer. Unknown keys are checked first and named with the
  verb's own.
- **Plain means "nothing to destroy, zero is a valid start"**
  (`NoDestructor` and `ZeroConstructor`), on the property or on every member
  of its struct. `IsPlainOldData && NoDestructor` refused gameplay tags; a
  hand-kept list of struct names was rejected.
- **Census pages slice the stored census.** A rescan per page would shift
  rows as counts change; the last two censuses are kept in the session and
  cache file so the pages of one filter join. `view.limit` 0 stays for all
  rows at once.
- **`get` resolves every path, then reads the values back to back.** Reading
  as each path resolved put console work between held requests and split
  the values over ticks; resolving first keeps `same_tick` without raising
  host budgets. A batch also says which command moved its tick
  (`spans_ticks`).
- **Captures belong to the agent instance** (`watch-start`, `watch-read`,
  `watch-stop`), bounded by the existing buffer, so a long capture and other
  questions overlap without a second channel. Typed watches decode in the
  console, the type riding in `WatchRange::note`.
- **Not built:** a tolerance search (a memory scanner's; change over one
  range is [`mem-diff`](../commands/memory.md#mem-snap-and-mem-diff)) and a
  mark on pooled instances (game knowledge, a host's verb).

## Usage log

The console records each call, and `usage` reads it back
([console](../commands/console.md#usage-log-and-usage)), for the agent in
the session and for whoever improves the tool or a skill across sessions.

- **The console records, because every path ends there** (shell, generated
  functions, MCP tools). Nothing is recorded in the target or the adapter.
- **One file beside the console, one line per call;** per-session and
  all-time figures come from the same lines.
- **Facts, never values.** No addresses, names, paths or results: large,
  meaningless later, and the target's data in a file that outlives the
  session. A hash of the input finds a repeated call.
- **The caller says who it is** (`--caller`): only a tool's output lands in
  an agent's context, so context cost is its own number.
- **`usage` answers with no target,** so an agent can reflect after the
  target has gone; a console with no target ends on any other verb (kind
  `startup`).
- **Marks, not advice.** `unfiltered`, `repeated` and `refused` state what
  happened; the skill says what to do. `unfiltered` uses a fixed list of
  filter keys rather than a mark per row, which would change `help`'s `json`
  and every generated module for no caller's gain.
- **A batch's commands get their own lines,** the batch one for its wrapper
  alone, so per-verb figures stay true. `help` with `json` and `quit` are
  not recorded.

| Rejected | Why |
|---|---|
| Recording in the MCP adapter | misses scripts and shell calls |
| A usage service or a database | the console keeps state in files; there is no daemon |
| Storing arguments or results | size, and the target's data outliving the session |
| A warning inside a large result | more output to cure output |
| Advice in the result | a rule wrong for one call misleads; marks state what happened |
| Counting tokens | model-dependent; characters rank the same verbs |
