# Driving the console from scripts

## Running commands

- PowerShell: single-quote `-c` commands so `$1` needs no backtick
  (`& $console -c 'find X; dump $1'`). In double quotes write `` `$1 ``.
- Stdout is one JSON record per command: pipe it through `ConvertFrom-Json`
  (PowerShell) or `json.loads` per line. Keep stderr separate: it carries
  wait notices and capture countdowns, and breaks a parser that expects JSON
  on every line.
- Pass a `find` row's `ref` (`result.rows[].ref`) or a dump row's
  `object.ref` verbatim to the next command.
- For stateful automation, keep stdin open and alternate one command line
  with one JSON response. `$N` lasts until EOF or `quit`; the last `snap`
  is also in the cache file, so a later run can `dump --changed`.
- **MCP tools, when the client has them** (`reagentlive_mcp.py`, see the
  library's client docs): pick the tool, fill its named arguments, pass
  the literal `ref` a result carries to the next tool. Read-only tools
  are marked; destructive ones (writes, calls) still need the host's
  approval. `batch` for reads that must share a tick. Each call is its
  own console run, so the console can be rebuilt between calls; a tool
  that says it changed since the rebuild needs the client to reconnect.
- **Otherwise script through the generated functions** whenever one step's
  output feeds the next, even a short chain. A one-line `-c` is for
  independent commands you read by eye. Generate the module once per
  console build (no game needed), then import it beside the session:

  ```
  python <reagentlive>/src/reagentlive/frontends/python/reagentlive_gen.py --console <console exe> -o <scratch>/console_api.py
  ```

  `sys.path[:0] = [r'<reagentlive>/src/reagentlive/frontends/python', r'<scratch>']`,
  `from reagentlive_session import Session`, `from console_api import Console`,
  then `with Session(r'<console exe>') as s: c = Console(s)`.
  - **One method per verb, named by origin:** core on `c` (`c.read`,
    `c.sig_check`, `c.watch_start`), the pack's and the host's under
    their origin (`c.ue.find`, `c.ue.census_diff`, `c.host.<verb>`).
    Flags are keyword arguments; a Python keyword gets `_` (`cls`,
    `start`, `type`). A wrong argument name is a `TypeError` at once.
  - **Literal refs, never `$N`:** calls go as JSON, which refuses `$1`.
    Take `rows[0]['ref']` from the result and pass it on.
  - `StaleTableError` at `Console(s)`: the console was rebuilt; generate
    the module again.
  - `s.call('<verb>', {...})` sends any verb with the keys `help <verb>`
    shows.
  - **Results are strict.** `result['rows']`, `row['ref']`: a key the
    result does not have raises `ResultKeyError` and lists the valid keys;
    an optional key that is absent gives `None`. So a wrong guess fails at
    once. Never wrap result access in `try`/`except` or use a default to
    paper over a key: a check that cannot run must fail, not pass.
  - **Before writing a script, look the keys up:** `s.help('<verb>')`, or
    `<console> -c "help <verb>"` (no game needed).
  - Helpers that convert say so in their name (on `s`): `read_bytes`,
    `read_struct`, `read_batch_bytes`, `get_values`, `name_map`,
    `call_bytes`, `batch_results` (`(items, same_tick)`), and
    `samples(result)` for a watch's rows as bytes. Paging is yours:
    follow `next_cursor` with `start` in a loop, or a limit flag
    (`census --limit 0`).
  - `addr` and `hex` values are the text the console prints (`"0x7ff6..."`):
    `addr(text)` gives the int, `unhex(text)` the bytes of a `bytes` value.
  - Failures raise `ReAgentLiveError`. Check `capped` on `each`, `refs`,
    `xrefs` and `touches` yourself: the list was cut.

  Do not parse console JSON by hand or write session loops. Hand parsers
  crashed six times in one session (list shapes, errors without `result`,
  stderr in stdout).
- If you must parse raw lines, read only `result`, check `ok` first, and take
  key names from `help <verb>`, never from memory.
- One console per agent; a second is refused as `busy`. Stderr lines during
  long waits say whether the agent is ticking (request running) or not.

## Reading output

- Struct values are plain objects keyed by member name (`{"X": 1, "Y": 2,
  "Z": 3}`), readable names where the names file has them; nothing to
  flatten. Add `--meta` only when the member layout itself is the question.

- Read one-shot results by the keys `help <verb>` shows, never by guess.
  Every result's list is `rows` (`find`, `census`, `name-find`, `funcs`,
  `get`, `each`, `batch`, `search`, `disasm`...); a page that has more
  carries `next_cursor` (and `census` a `hint`): page with `--start`. A key
  that does not apply is absent, so test for the key, not for null.
- `name` takes many indices on one line (40+ verified); the console pages
  them to the agent.
- Some `name-find` searches return binary junk beside real names; ignore rows
  with control characters and confirm with `name <index>`.
- `dump` resolves names in expanded structs and top-level array previews. A
  nested array still shows its header, including arrays inside gameplay-tag
  containers. For names in struct array elements, dump
  `Default__GameplayTagsSettings` and inspect populated tag redirects or tag
  rows.
- A bare class name as an argument costs one object-table scan (seconds) the
  first time per agent instance; the console's cache file keeps the ref for
  later runs. A literal ref never scans.

## Structs and arrays

- For an instanced struct array, resolve the script struct by name and match
  its type pointer. Confirm the header, the matching entry and the
  pointed-to value together in one `batch` or `read-batch` before relying on
  a saved data address.
