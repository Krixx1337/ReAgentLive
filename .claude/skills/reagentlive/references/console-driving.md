# Driving the console from scripts

## Running commands

- PowerShell: single-quote `-c` commands so `$1` needs no backtick
  (`& $console -c 'find X; dump $1'`). In double quotes write `` `$1 ``.
- Stdout is one JSON record per command: pipe it through `ConvertFrom-Json`
  (PowerShell) or `json.loads` per line. Keep stderr separate: it carries
  wait notices and capture countdowns, and breaks a parser that expects JSON
  on every line.
- Pass `result.objects[].ref` or a dump row's `object.ref` verbatim to the
  next command.
- For stateful automation, keep stdin open and alternate one command line
  with one JSON response. Refs and the snapshot baseline persist until EOF or
  `quit`.
- **Script through the Python session client** (`reagentlive_session.py` in
  ReAgentLive's Python frontend) whenever one step's output feeds the next, even
  a short chain. A one-line `-c` is for independent commands you read by eye.
  Import it with `sys.path.insert(0, r'<reagentlive>/src/reagentlive/frontends/python')`, then
  `from reagentlive_session import Session`, and use
  `with Session(r'<console exe>') as s:`. It offers:
  - lookups return documented rows: `find`, `describe`, `fields`, `dump`
    (`changed=True` after `snap`), `funcs`, `func`, `params`, `name_find`,
    `entries`, `census`, `each`, `modules`; `refs` and `script` return the
    whole result;
  - `call` (bytes, or with `args={...}` the typed result with `outputs`
    and `kept`), `release`, `snap`, `record`, `export_labels`;
  - also `batch`, `get`, `read_struct`, `read_batch` and decoders;
  - failures raise `ReAgentLiveError`; `each` also raises when the instance
    list was capped, with the rows in `.error['result']`.

  The commands doc's result-shapes table names the method per command; a
  command without one is the only case for a hand-built `s.send(line)`.

  Do not parse console JSON by hand or write session loops. Hand parsers
  crashed six times in one session (list shapes, errors without `result`,
  stderr in stdout).
- If you must parse raw lines, read only `result`, check `ok` first, and take
  key names from the commands doc's "Result shapes" table.
- One console per agent; a second is refused as `busy`. Stderr lines during
  long waits say whether the agent is ticking (request running) or not.

## Reading output

- Struct values are plain objects keyed by member name (`{"X": 1, "Y": 2,
  "Z": 3}`), readable names where the names file has them; nothing to
  flatten. Add `--meta` only when the member layout itself is the question.

- Read one-shot results by their documented keys, never by guess: `find`
  → `objects`, `census` → `rows` (not `classes`, which is a count),
  `name-find` → `names` (not `matches`), `funcs` → `functions` as `{owner,
  name}`, `get` → `values`, `each` → `rows`, `batch` → `results`. A cut
  `census` carries `next_cursor` and a `hint`: page with `--from`.
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
