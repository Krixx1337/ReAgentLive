# Scripts: console and Python

Session and generated-function API: `docs/client.md`; console arguments,
one-shot use and the cache file: `docs/commands/console.md#one-shot-use`.

## Console

- One-shot: `<console> --caller tool --packs explore,native -c '{"verb": "find", "args": {"cls": "Pawn", "view": {"limit": 5}}}'`
  (or a JSON list of calls, or `-c @calls.jsonl`, one call per line);
  exit code 1 on any error. Use it for independent calls you read; a
  chain where one step's output feeds the next goes to Python.
- **`--caller tool`** whenever you read the output: it gives you the
  output budget and digests, and `stats` counts it as context.
- **`--packs`:** `-c` runs all four packs by default (a person's
  ceiling). Pass `--packs explore,native` (add `observe` as needed) for
  read-only work; add `modify` only for an approved write.
- `-c` takes ASCII only: escape other characters as `\u` in the JSON, or
  use `-c @<file>`.
- PowerShell: single-quote the JSON (`& $console -c '{"verb": "status"}'`);
  Windows PowerShell 5 strips the inner `"` from native arguments: write
  the calls to a file and pass `-c @<file>`. `@(@(a,b))` flattens a
  single pair; write `@(,@(a,b))`. Names are case-insensitive: `$early`
  overwrites an `[int]$Early` parameter.
- Stdout is one JSON record per call; stderr carries wait notices and
  capture countdowns. Parse stdout per line (`ConvertFrom-Json`,
  `json.loads`), check `ok` first, read only `result`, and take key names
  from `help` with `verb`, never from memory.
- Stateful without Python: keep stdin open, one JSON call in, one JSON
  response out, until EOF or `{"verb": "quit"}`.

## Python

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
with Session(r'<console exe>', packs=['explore', 'native', 'observe']) as s:
    c = Console(s)
```

- **Strict results:** a wrong key raises `ResultKeyError` at once, naming
  the valid keys. Never wrap result access in `try`/`except` or a
  default: a check that cannot run must fail, not pass. Look the keys up
  first: `s.help('<verb>')`.
- `StaleTableError` at `Console(s)`: the console was rebuilt; generate
  again.
- `caller='tool'` when you read every result yourself (it gives the
  output budget; a result past it is a `Digest`, also in `s.digest`); the
  default `script` keeps a script's output apart in `stats`.
- Page with the console's cursor: pass `next_cursor` back as `view.start`,
  or raise `view.limit`; check a list a verb can cut before treating it as
  complete.
- `Session` has no timeout: a call waits until the console answers or
  exits. Blocking verbs (`run`, a host's) block the script.
- Stop after three identical refusals and fix the call.
- Never parse console JSON by hand or write your own session loop: hand
  parsers crashed six times in one session.
