# Design: console costs

*Home of:* why the console works the way it does for an agent that pays per call and per character: holds and round trips, output weight, the usage log; with their rejected alternatives.

**TL;DR:** AI agents pay for every tool call and every character of output.
These are the decisions that cut both, the evidence from live sessions
behind them, and what was rejected. Part of [design](../design.md).

## Round trips

AI agents drive ReAgentLive one tool call at a time and pay for every call and
every byte of output; inside a command, each request waits for a host tick.
So the order of work is fewer tool calls (state kept between runs, commands
that do a whole lookup), then less output (filters), then fewer and cheaper
requests inside a command.

- **Hold, not a packed batch op.** A request may say more follow; the agent
  waits a bounded time on the host's thread and runs the next one in the same
  tick. A packed op was rejected: sub-requests need the 128-byte `text`
  field, all results would share one 4 KB response, and it cannot do
  dependent reads. Hold needs no packing format and lets compound commands
  chain reads. Same tick is best effort; every response carries its tick so a
  miss is visible. The per-tick budget bounds the stall the host pays.
- **No fast lane for memory reads on a worker thread.** Reads off the tick
  would be torn against the game; a second request slot helps no one while
  every frontend is synchronous; `watch-start` already samples every tick.
- **No `ReadProcessMemory` from the client.** Fastest for raw reads, but the
  client would open the target process, which the design avoids.
- **One client at a time, kept short** ([protocol](../protocol.md#handshake)).
  A console that finds the agent busy asks again for up to 2 s, then
  refuses as `busy`, so a script beside the MCP tools waits for the other
  call instead of failing at once.
- **No session daemon or several clients.** The console's cache file gives
  one-shot runs their state (class refs, field tables, exe module),
  tied to one agent instance by `Section::owner`. Field tables are trusted
  only when page 0 of the live table matches, so a cache never shows a stale
  layout.
- **Client waits spin, then sleep at 1 ms** with the timer resolution raised
  in the client process only. Polling with `Sleep(1)` at the default 15.6 ms
  timer made every round trip cost 15.5 ms against a 10 ms host tick; now a
  read costs one tick. Held requests spin up to 50 ms at raised thread
  priority: a client that slept through one response missed the agent's
  short wait for every later request too, so held batches ran either in one
  tick or one tick per request.
- **Not built, measured unneeded:** larger responses (a large `get`'s
  requests are pointer lookups and array previews, not 4 KB paging) and a
  wake-up event (a read already costs one host tick).
- **Host logging counts against the hold budget.** A host that logs every
  successful lookup synchronously gets one held request per tick; suppress
  success logs for read-only ops.

## Output weight and dead ends

From one long live session (an unreflected block found, decoded and proved
on live samples), where time and context went for no gain.

- **Compact values by default.** A struct prints as `{Name: value}`; full
  member metadata made up most of nearly every command's output and was
  flattened by hand per call. One rule for every struct, vectors included:
  a shorter special case for vectors was rejected. `meta` restores the
  layout; `get` with `mode` `types` keeps it, being the layout mode.
- **Calls explain themselves.** An outputs-only call gets a zeroed block;
  refusals name the parameter and the reason in terms of it. A message that
  reads as true of the input it refuses ("parameters are plain outputs:
  ReturnValue", for a function whose only parameter is ReturnValue) cost a
  detour.
- **Plain is "nothing to destroy, zero is a valid start"** (`NoDestructor`
  and `ZeroConstructor`), on the property or, where it lacks a flag, on
  every member of its struct. The earlier `IsPlainOldData && NoDestructor`
  refused gameplay tags, which own nothing; a hand-kept list of struct
  names was rejected.
- **Census pages slice the stored census.** A rescan per page would shift
  rows between pages as counts change; the last census (with samples) and
  the one before it are kept in the session and cache file instead, so the
  pages of one filter join. A `view.limit` of 0 stays for all rows at once.
- **`get` reads its values after resolving every path.** Reading each value
  as its path resolved put console work (field table pages, pointer hops)
  between held requests; past the agent's short wait for the next request,
  the values split over ticks. Resolving first and then reading the values
  back to back keeps the reads under that wait, so `same_tick` holds without
  raising the host's budgets. Warm field tables are shared, not copied, for
  the same reason.
- **A batch says which command moved its tick.** `same_tick` alone did not
  stop a skewed reading; per-command ticks and `spans_ticks` come from the
  client's hold, no agent change.
- **Native names in their own file.** The readable-names file is generated
  by the host and would wipe appended lines, so `label` writes a hand-owned
  `<console>.native.txt`. Reflected names follow their field across patches;
  native offsets do not, so each line carries the main image's PE
  timestamp and shows `stale` under another one. Typed layouts share one
  syntax with `read`'s `type`, so a decoded layout pastes into `label`.
- **A capture that does not hold the console**, without a second channel:
  the capture belongs to the agent instance (`watch-start`, `watch-read`,
  `watch-stop`), bounded by the existing buffer, so a long capture and other
  questions overlap. Following a replaced object asks the host: the core
  knows no names, a host offers roots through one optional callback.

From one autonomous read-only agent session through the MCP tools only,
where every question was answered and the cost was output and turns:

- **Wrong-looking answers first, then output, then turns.** A one-hop chain
  (offset 0 after an address) answered the bytes at that address with
  `hops: []`: right by the rule that the last offset only adds, and read as
  a followed pointer. It is
  refused with the form that follows one. A misspelt JSON key (`class_`)
  was reported as a left-out argument; unknown keys are now checked first
  and named with the verb's own. Refusals state their limit's number.
- **One name per argument, never a Python keyword** (`cls`, `start`,
  `type` for `class`, `from`, `as`), so a JSON call, a `batch` entry, the
  generated function and the tool spell it the same.
  Keeping the keywords and letting the generator append `_` was the cause:
  the tool said `class_`, `batch` wanted `class`. A table test refuses a
  keyword.
- **Compact rows, layout on request**, the rule values already followed.
  A `get` row (every field) is its name, offset and value (about 25 KB for one actor
  before); the offset stays because it is the bridge to raw reads, and the
  recovered readable name is the row's name, the reflected one beside it.
  A `get` row is its value and address; `path` and `ok` went because rows
  keep the order asked and a failed path keeps its place. The `batch`
  envelope kept its shape: one envelope for every command beats the bytes.
- **`each` became a table** because most questions were "these fields over
  every instance": four values over 17 objects had taken 17 `get` calls.
  The scan dominates its cost and a column adds none, so columns share
  one scan; a function stays a single column (calls are the slow case and
  their outputs do not fit one). An agent op for several spans was not
  needed: passes of 64 bytes over the cached instance list do it.
- **Typed watches** decode and summarise in the console, so every client
  has the arithmetic. The type rides in a field the agent already kept
  verbatim (`WatchRange::note`), so a later run decodes a
  capture with no agent change.
- **Not built:** a tolerance search (narrowing by change over one range is
  [`mem-diff`](../commands/memory.md#mem-snap-and-mem-diff); the rest is a
  memory scanner's) and a mark on pooled instances
  (game knowledge, a host's verb). A bool's `view.nonzero` already tested its
  bit; readable names already worked in `get` paths.

## Usage log

The console records each call, and `stats` reads it back
([console](../commands/console.md#usage-log-and-stats)), for two readers: the
agent in the session (before a long step, and for its final report) and
whoever improves the tool or a skill across sessions.

- **The console records, because every path ends there:** JSON calls
  from a shell, the generated functions and the MCP tools all run a console
  command. Nothing is recorded in the target or the adapter.
- **One file beside the console, one line per call.** Per-session and
  all-time figures come from the same lines, so neither had to be chosen.
- **Facts, never values.** No addresses, names, paths or results: they are
  large, mean nothing later and would keep the target's data in a file
  that outlives the session. A hash of the input finds the same call made
  again.
- **The caller says who it is** (`--caller`): only a tool's output lands in
  an agent's context, so context cost is its own number.
- **`stats` is a core verb**, a tool and a function like any other, and it
  answers with no target, so an agent can reflect after the target has
  gone. For that, a console with no target answers `help` and `stats` and
  ends on any other verb (kind `startup`), so a client starts it again.
- **Marks, not advice.** `unfiltered`, `repeated` and `refused` state what
  happened; the skill says what to do. `unfiltered` uses a fixed list of
  filter keys (`view`'s `match`, `nonzero` and `limit`, `own`, `cls`)
  rather than a mark on each key's row: the same names mean
  the same thing on every verb, and a row mark would change `help`'s `json`
  and every generated module for no caller's gain.
- **A batch's commands get their own lines**, under their own verbs, and
  the batch one for its wrapper alone, so per-verb figures stay true and
  totals count nothing twice.
- **The caller's own calls are not recorded:** `help` with `json` (every
  session client reads the table at start) and `quit`.

| Rejected | Why |
|---|---|
| Recording in the MCP adapter | misses scripts and shell calls, and a second log would be wanted for them |
| A stats service or a database | the console keeps state in files; there is no daemon ([round trips](#round-trips)) |
| Storing arguments or results | size, and the target's data in a file that outlives the session |
| A warning inside a large result | more output to cure output; the agent asks `stats` when it wants to know |
| Counting tokens | depends on the model; characters rank the same verbs |
| Advice in the result ("use `view.match`") | a rule wrong for one call misleads; the marks state what happened |
| Per-session files | all-time figures would read many files; one file with a session on each line gives both |
| A `caller` key on each JSON call (to leave out a client's table read) | another key on every call for one line; leaving `help` with `json` out covers it |
