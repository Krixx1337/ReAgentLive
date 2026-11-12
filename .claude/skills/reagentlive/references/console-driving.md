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
  with one JSON response. Refs and the snapshot baseline persist until EOF or
  `quit`.
- **Script through the Python session client** (`reagentlive_session.py` in
  ReAgentLive's Python frontend) whenever one step's output feeds the next, even
  a short chain. A one-line `-c` is for independent commands you read by eye.
  Import it with `sys.path.insert(0, r'<reagentlive>/src/reagentlive/frontends/python')`, then
  `from reagentlive_session import Session`, and use
  `with Session(r'<console exe>') as s:`.
  - **One method per verb,** named after it (`s.find`, `s.sig_check`,
    `s.watch_start`, `s.census_diff`), returning that verb's result as the
    console gives it. A host's own verb: `s.ok('<line>')`.
  - **Results are strict.** `result['rows']`, `row['ref']`: a key the
    result does not have raises `ResultKeyError` and lists the valid keys;
    an optional key that is absent gives `None`. So a wrong guess fails at
    once. Never wrap result access in `try`/`except` or use a default to
    paper over a key: a check that cannot run must fail, not pass.
  - **Before writing a script, look the keys up:** `s.help('<verb>')`, or
    `<console> -c "help <verb>"` (no game needed).
  - Helpers that reshape say so in their name: `read_bytes`, `read_struct`,
    `read_batch_bytes`, `search_addresses` (ints, every page), `find_all`,
    `funcs_all`, `census_all`, `entries_all`, `name_find_all`,
    `get_values`, `name_map`, `call_bytes`, `batch_results` (`(items,
    same_tick)`), and `samples(result)` for a watch's rows as bytes.
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
  carries `next_cursor` (and `census` a `hint`): page with `--from`. A key
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
