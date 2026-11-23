# Driving the console and Python by hand

API of the session and the generated functions:
clients (`docs/client.md`); console arguments, one-shot use and
the cache file: console (`docs/commands/console.md#one-shot-use`).

## Console lines

- One-shot: `<console> -c '{"verb": "find", "args": {"cls": "Pawn", "view": {"limit": 5}}}'`
  (or a JSON list of calls, or `-c @calls.jsonl`, one call per line); exit
  code 1 on any error. Use it for independent calls you read by eye; no
  `$N`: a chain where one step's output feeds the next goes to Python.
- PowerShell: single-quote the JSON (`& $console -c '{"verb": "status"}'`);
  Windows PowerShell 5 strips the inner `"` from native arguments: write
  the calls to a file and pass `-c @<file>` there.
- PowerShell `@(@(a,b))` flattens a single pair; write `@(,@(a,b))`.
- PowerShell names are case-insensitive: `$early` overwrites an
  `[int]$Early` parameter; give every variable a distinct name.
- Stdout is one JSON record per command; stderr carries wait notices and
  capture countdowns. Keep them apart, parse stdout per line
  (`ConvertFrom-Json`, `json.loads`).
- If you must parse raw lines: read only `result`, check `ok` first, take
  key names from `help` with `verb`, never from memory.
- Stateful automation without Python: keep stdin open, one JSON call in,
  one JSON response out, until EOF or `{"verb": "quit"}`.

## Python scripts

Generate the module once per console build (no target needed), then
import it beside the session:

```
python <reagentlive>/src/reagentlive/frontends/python/reagentlive_gen.py --console <console exe> -o <scratch>/console_api.py
```

```python
import sys
sys.path[:0] = [r'<reagentlive>/src/reagentlive/frontends/python', r'<scratch>']
from reagentlive_session import Session
from console_api import Console
with Session(r'<console exe>') as s:
    c = Console(s)
```

- `StaleTableError` at `Console(s)`: the console was rebuilt; generate again.
- Look the keys up before writing the script: `s.help('<verb>')`.
- Never wrap result access in `try`/`except` or a default: a check that
  cannot run must fail, not pass.
- Page with the console's cursor: pass `next_cursor` back as `view.start`
  (`view={'start': cursor}`), or raise `view.limit`. A list a verb can cut
  says so (`s.help('<verb>')`): check
  it before treating the list as complete.
- Stop after three identical refusals and fix the call; a loop that retries
  the same refusal only spends output.
- Never parse console JSON by hand or write your own session loop: hand
  parsers crashed six times in one session (list shapes, errors without
  `result`, stderr in stdout).
