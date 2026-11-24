# IDA loader

*Home of:* the IDA side, `frontends/ida/reagentlive_ida.py`: installing it and checking the installed copy, opening a save, the workspace database (`add_saved`), `fetch`, `decompile_to`, types (`import_layouts`, `apply_this`) and names both ways (`import_labels`, `export_names`).

**TL;DR:** One Python file in IDA's loaders folder opens what `disasm`'s
`save` wrote as a small x64 database, so IDA decompiles those functions
without analysing the whole image. One workspace database grows through
an investigation: `fetch(addr)` saves an address live, adds it and
decompiles it in one call. Names travel both ways through the labels file.
What a save holds: [saving a function](fast-lane.md#saving-a-function-for-a-static-tool).

## Install

- Copy `frontends/ida/reagentlive_ida.py` into IDA's user loaders folder
  (Windows: `%APPDATA%\Hex-Rays\IDA Pro\loaders`). IDA's headless library
  reads the same folder, so an automated session opens saves too. Any file
  name there works.
- **Copy it again after every update.** A loader older than the file
  refuses it at the first row kind it does not know ("this loader is older
  than the file"), so a save never loads with its chunks or names silently
  missing.
- **Is the installed copy current?** `python -I tools/ida_loader_check.py`
  (`--loaders <dir>` for another folder) hashes the repo's file and every
  ReAgentLive loader in the folder (found by its first docstring line, any
  file name) and prints `state`: `current`, `stale` (copy the repo's file
  over it) or `missing`; the exit code is 0, 1 or 2 to match. Inside IDA:
  `rl.loader_digest() == rl.loader_digest('<repo copy>')`.
- For `fetch`, pass the console: `console=` with its absolute path (or a
  command list). `REAGENTLIVE_CONSOLE` is optional, read only when
  `console` is not given.

## Opening a save

Open the saved file in IDA. The loader claims it by its first line (a
`function` row), not by its extension, and builds an x64 database (Visual
C++, so the decompiler uses the Windows calling convention):

- one segment per run of `code` and `data` rows at its live address (rows
  that overlap with identical bytes, or touch, are joined; rows that
  overlap with different bytes refuse the file);
- each function, with its chunks attached as function tails, and the
  file's names (auto names: `export_names` never mistakes them for yours);
- a one-byte `STUB` segment for a name outside the saved bytes, so the
  decompiler shows `fn_6fa0aa0(...)` or a label instead of a bare address;
- the file's `layout` rows as local types, and `Class *this` on the
  methods they belong to once auto-analysis ends (the decompiler is not
  there while loading).

Writable globals are not saved (their values change). The `rva`s cross
back to a full database of the same build. The loader ran when the
functions are named `fn_<rva>` (or their labels) and carry a
`reagentlive: <module>+<rva>` comment; a database with one segment at 0
came from IDA's own binary loader (the loader is missing or failed: its
warning says why).

## The workspace database

`add_saved(<path>)`, on an open database, adds another save of the same
build to it, so one workspace database grows through a whole
investigation: open the first save, `add_saved` (or `fetch`) every later
one, close it at the end. Bytes the database already holds are cut from
the new save and must match byte for byte (one build has one set of
bytes). A new run that overlaps or touches a segment of its kind joins it,
so a function never ends at a segment's edge, and stubs are replaced by
the code that arrives. Names you gave and comments already there stay.

It returns `added` (new segments), `joined` (existing segments grown),
`cut` (new bytes already held), each as `start-end` ranges,
`stubs_replaced`, and for `layout` rows `types` (declared),
`types_failed`, `this_typed` and `this_skipped` (`'<name>: why'`); a
function it could not type yet waits for the next `add_saved`. It raises
`SaveError` instead of loading less than the file holds: a file it cannot
parse, another build, a byte that differs from the database, or a saved
function or chunk start left without bytes.

## From an address to pseudocode in one call

`fetch(addrs, callees=True, depth=1, decompile_dir=None, console=None, grep=None, timeout=120)`,
on an open save database, is the whole loop: it runs the console with one
`disasm` save per address (`save.callees` = `depth` unless `callees` is
false) as a JSON call file (`-c @<file>`, `--caller script`, `--packs
native`), `add_saved`s each file (types and `this` included), and with
`decompile_dir` runs `decompile_to` over every saved function. One address
or a list; each an int or `'0x..'` text.

```python
out = rl.fetch(0x140001000, depth=2, decompile_dir='<dir>', grep=r'this->\w+')
```

It returns `functions` (every saved one), `skipped` (the console's rows),
`add_saved`'s keys summed over the files, `decompiled` (`decompile_to`'s
summaries) and `seconds` (`console`, `load`, `decompile`). A console
refusal raises `FetchError` before anything loads, with the console's
error object in `refusal`; a file that does not load raises `SaveError`.
The scratch files go when it returns.

`decompile_to(targets, out_dir, grep=None)` takes addresses (an int or
`'0x..'` text; an address inside a function means that function) or
names, and writes one `<name>.c` per function into `out_dir`: characters
a file name cannot hold become `_`, the name is cut to 120, and a name
already taken (ignoring case) gets the address appended. A function asked
twice is written once. It returns one summary per function: `address`,
`name`, `path`, `lines` and, with `grep` (a regex, or a plain substring
when it is no valid regex), `matches` as `'<line>: <text>'`, at most 40,
the rest counted in `matches_more`. A failed decompile writes the error
into its file and adds `error`; a target that names no function comes
back as `{target, error}`. Read the summary first and open only the files
it points to. `decompile_all()` returns every function's pseudocode as one
string. The pseudocode already shows the saved constants' values.

`tools/fetch_timing.py` times the loop inside IDA, hop by hop:
`time_fetch` (live) and `time_saves` (offline: saved files, no console);
outside IDA, `python tools/fetch_timing.py opens <session.jsonl>` counts
the databases an IDA MCP session opened.

## From an automated session

An IDA bridge with Python: open the first save, then import the loader
and call it. With the loader installed in IDA's loaders folder, a plain
import finds it:

```python
import reagentlive_ida as rl
rl.fetch('0x<addr>', depth=2, decompile_dir='<dir>')   # save, add, type, decompile
rl.export_names('<names file>')   # before closing: always with a path
```

Only for a copy outside the loaders folder (the repo's, say), load it by path:

```python
import importlib.util
spec = importlib.util.spec_from_file_location('reagentlive_ida', '<path>/reagentlive_ida.py')
rl = importlib.util.module_from_spec(spec); spec.loader.exec_module(rl)
```

Close the database once read, and before opening a new save under the
same name delete the database files beside it. `export_names` and
`import_labels` without a path open a file dialog, which hangs a headless
session.

## Types

`import_layouts(<file>)` turns a `layout-export` file
([layouts in bulk](unreal-fields.md#layouts-in-bulk)) into local types,
replacing types of the same name; a save's `layout` rows go the same way.
Each class or struct becomes a packed struct of its size with every field
at its reflected offset: its `super` is the base class when the file holds
it, else that part is padding. Fields are typed by property: numbers, a
bool as a byte (bitfield bools sharing a byte become one member named
after all of them), `FName`, `FString`, `FWeakObjectPtr` and
`FScriptInterface` as engine structs, an object or class pointer as a
pointer to its class's type when the file holds it (else `void *`), a
struct by value and an enum when the file holds them (else an enum
property is its integer), a `TArray` as `TArray_<element>` {Data, Num,
Max} (plain `TArray` when its element has no C type), a static array as
an array. Gaps become `pad_<offset>` bytes; sets, maps, delegates and soft
pointers are untyped byte blobs of their size. It returns `types`,
`enums`, `arrays` (the `TArray_` types made), `untyped` (fields left as
bytes, with why), `skipped` (overlapping fields, a super not in the file,
enum values that do not fit), `lines_skipped` and `failed` (declarations
IDA refused; those types are missing, the rest are in).

`apply_this(<labels file>)` types the first argument `Class *this` on each
function a label of this build names `Class::Method`, or `Class_Method`
with the longest `Class` prefix that is a local struct type (class names
hold `_` too), so the decompiler shows `this->Field`. Other arguments and
the return type stay as IDA has them. It returns `typed` (a count) and
`skipped` (`'<name>: why'`). Run `import_layouts` first.

## Names both ways

- `import_labels(path)` (a path, or a list: the console's file and the
  host's names file; File > Script file runs it, asking for the file)
  names a full database of the build (size and timestamp from the PE
  header IDA keeps, so a default load with no header bytes works) or a
  save database (the build its loader kept); the highest-ranked row per
  address. A name you set stays (the label becomes a comment), exec thunks
  become functions. One undo step, a wait box that can cancel it; it
  prints how many rows it named, commented and skipped.
- `export_names(path)` (path: the host's names file) writes every name you
  gave in that database as `user` rows of its build: not IDA's names
  (`sub_…`, `fn_…`, a switch's `def_…` and `jpt_…`, a jump table, any
  name IDA marks auto-generated or rejects as a user's), not the compiler's
  or runtime's (`__` helpers such as `__alloca_probe`, `?` mangled and `$`
  names), not names a file brought in, nor library names (FLIRT functions
  and library items), adding or updating them; names it
  does not hold stay (a save database never wipes what another database
  named). The next save, import or export shows them: a finding named
  once stays named, in saves and in the full database alike.

Labels files and what each command reads: [labels](labels.md).
