# IDA loader

*Home of:* the IDA side, the `reagentlive_ida` package in `frontends/ida/` and its loader stub: setting it up and checking the setup, reloading it, opening a save, the workspace database (`add_saved`, `workspace`, `finish`), `fetch`, `decompile_to`, types (`import_layouts`, `apply_this`) and names both ways (`import_labels`, `export_names`, `name`).

**TL;DR:** A loader stub in IDA's loaders folder, calling the repo's
`reagentlive_ida` package, opens what `disasm`'s `save` wrote as a small x64 database, so IDA decompiles those functions
without analysing the whole image. One workspace database grows through
an investigation: `fetch(addr)` saves an address live, adds it and
decompiles it in one call. Names travel both ways through the labels file.
What a save holds: [saving a function](fast-lane.md#saving-a-function-for-a-static-tool).
The package's docstrings give every argument and return key.

## Install

- **Set it up once:** `python -I tools/ida_setup.py --install`. It writes a
  path file into the site-packages of IDA's Python (`--python <exe>` for
  another), so IDA imports the package from the repo, and copies the stub
  into IDA's user loaders folder (`--loaders <dir>` for another), removing
  older ReAgentLive loaders there. IDA's headless library reads the same
  folder and Python, so an automated session opens saves too. Run it again
  after switching IDA's Python.
- **Nothing to copy after an update:** IDA runs the repo's code. A running
  IDA session keeps what it imported: `rl.reload()` loads it again, or
  restart IDA. Code older than a save refuses it at the first row kind it
  does not know, so a save never loads with its chunks or names silently
  missing.
- **Is it set up?** `python -I tools/ida_setup.py` (no `--install`: it only
  reads) prints where IDA's Python imports the package from, the loaders
  found, `why` and `state`: `current`, `stale` (an old or edited loader, or
  the package from another folder) or `missing`; the exit code is 0, 1 or 2
  to match. `--install` fixes both.
- `fetch` and `finish` run the console the database's saves recorded (each
  save's `console` row; the latest one loaded wins). `console=` overrides
  it, then `REAGENTLIVE_CONSOLE`; a full database records none, so pass one
  there.

## Opening a save

Open the saved file in IDA. The loader claims it by its first line (a
`function` row), not by its extension, and builds an x64 database (Visual
C++, so the decompiler uses the Windows calling convention): one segment per
run of saved bytes at its live address (overlapping rows must agree byte for
byte, else the file is refused), each function with its chunks attached as
function tails, the file's names, its `layout` rows as local types and
`Class *this` on the methods they belong to once auto-analysis ends (the
decompiler is not there while loading), and one-byte `STUB` segments for
names outside the saved bytes so the decompiler shows `fn_6fa0aa0(...)` or a
label instead of a bare address. A UFunction's exec thunk
(`Class::execMethod`) also names the native function it calls
`Class::Method`: the one call whose first argument is the thunk's own first
parameter.

Writable globals are not saved (their values change). The `rva`s cross back
to a full database of the same build. The loader ran when functions are named
`fn_<rva>` (or their labels) and carry a `reagentlive: <module>+<rva>`
comment; a database with one segment at 0 came from IDA's own binary loader
(the loader is missing or failed: its warning says why).

## The workspace database

`add_saved(<path>)`, on an open database, adds another save of the same
build, so one workspace database grows through a whole investigation: open
the first save, `add_saved` (or `fetch`) every later one, close it at the
end. Bytes the database already holds must match the new save byte for byte
(one build has one set of bytes). New runs join the segments they touch, so a
function never ends at a segment's edge, and stubs are replaced by the code
that arrives. Names you gave and comments already there stay. A function it
could not type yet waits for the next `add_saved`. It raises `SaveError`
instead of loading less than the file holds: a file it cannot parse, another
build, a differing byte, or a saved function start left without bytes.

### Start and finish

`workspace(files=None)` starts work in an open save database: it runs
`import_labels` over the labels files the recorded console reads.
`finish(names_file=None, console=None, timeout=300)` ends it: `export_names`
into the recorded names file, then the console's `labels-sign` on that file,
which reads the target and writes only the file. Close the database after it.

## From an address to pseudocode in one call

`fetch(addrs, callees=True, depth=1, decompile_dir=None, console=None, grep=None, timeout=120)`,
on an open save database, is the whole loop: it runs the console with one
`disasm` save per address, `add_saved`s each file (types and `this`
included), and with `decompile_dir` runs `decompile_to` over every saved
function. A console refusal raises `FetchError` before anything loads; a file
that does not load raises `SaveError`.

```python
import reagentlive_ida as rl
rl.workspace()                                         # the recorded labels files in
out = rl.fetch(0x140001000, depth=2, decompile_dir='<dir>', grep=r'this->\w+')
rl.finish()                                            # your names out, then signed
```

An IDA bridge with Python can do this from an automated session: once set
up, a plain import finds the package in any database, a reopened one
included (IDA runs no loader there).

`decompile_to(targets, out_dir, grep=None)` writes one `<name>.c` per
function (an address inside a function means that function) and returns one
summary per function, with `grep` matches. Read the summary first and open
only the files it points to. The pseudocode already shows the saved
constants' values.

Close the database once read, and before opening a new save under the same
name delete the database files beside it. `export_names` and `import_labels`
without a path open a file dialog, which hangs a headless session
(`workspace` and `finish` never ask).

## Types

`import_layouts(<file>)` turns a `layout-export` file
([layouts in bulk](unreal-fields.md#layouts-in-bulk)) into local types,
replacing types of the same name; a save's `layout` rows go the same way.
Each class or struct becomes a packed struct of its size with every field at
its reflected offset, typed by property; gaps become `pad_<offset>` bytes,
and what has no C type (sets, maps, delegates) becomes a byte blob. Its
return value lists what it skipped or IDA refused; those types are missing,
the rest are in.

`apply_this(<labels file>)` types the first argument `Class *this` on each
function a label names `Class::Method` (or `Class_Method`, by the longest
prefix that is a local struct type), so the decompiler shows `this->Field`.
Run `import_layouts` first.

## Names both ways

- `name(target, new_name)` names the function holding `target` as yours, so
  `export_names` and `finish` carry it, and types `Class *this` when the name
  fits a local struct. Use it for a function found by hand, such as the
  native function an exec thunk reaches through its vtable.
- `import_labels(path)` (a path, or a list) names a full database of the
  build or a save database from the labels files; the highest-ranked row per
  address. A name you set stays (the label becomes a comment), exec thunks
  become functions. One undo step.
- `export_names(path)` writes every name you gave in that database as `user`
  rows of its build: not IDA's or the compiler's automatic names, not names a
  file brought in, not library names. Names it does not hold stay (a save
  database never wipes what another database named). The next save, import
  or export shows them: a finding named once stays named.

Labels files and what each command reads: [labels](labels.md).
