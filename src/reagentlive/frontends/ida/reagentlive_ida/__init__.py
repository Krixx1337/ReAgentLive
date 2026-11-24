"""ReAgentLive's IDA side: a loader for saves and the names round trip.

`python -I tools/ida_setup.py --install` puts this package on IDA's Python path and the loader
stub (reagentlive_loader.py) in IDA's user loaders folder; IDA's headless library reads the same
folder. Then `import reagentlive_ida as rl` in IDA, and:

- **Open a save** (`disasm` with `whole` and `save`):
  a small x64 database of the saved functions, their chained chunks
  (function tails) and the read-only data they reference, at their live
  addresses, with the file's names, and its layout rows as local types
  with `Class *this` on the methods they belong to. A name outside the saved bytes (a call
  or tail target not saved, a labelled global) gets a one-byte STUB segment
  so the decompiler shows the name, not a bare address. Names the file gives
  are auto names: export_names never mistakes them for yours. Rows that
  overlap with identical bytes (a callee inside another's listing) load as
  one segment; differing bytes refuse the file.
- `add_saved(path)`: add another save of the same build to the open
  database, so one database gathers a whole investigation: bytes it holds
  are cut, runs that overlap or touch a segment join it, stubs are replaced
  by real code. Returns what it added, joined, cut and which stubs it
  replaced; raises SaveError instead of loading less than the file holds.
- `import_labels(path)`: name a database from a labels file (the console's
  `<console>.labels.jsonl`, `labels-export`, `peek` with `label`): a full database of
  the build or a save database. Your own names stay; a label becomes a
  comment there.
- `export_names(path)`: write the names you gave in this database into a
  labels file as `user` rows, so later saves and imports show them (not
  library names: FLIRT functions and library items).
- `decompile_to(targets, out_dir, grep=None)`: one `.c` file per function
  (addresses or names), a short summary each, the lines matching `grep`.
  `decompile_all()`: every function's pseudocode, one string.
- `fetch(addrs, callees=True, depth=1, decompile_dir=None, console=None, grep=None, timeout=120)`:
  the console's save of each address (and its callees, `depth` levels;
  0: none), added to the open database, decompiled into `decompile_dir`:
  one call from an address to pseudocode. The console defaults to the one
  the saves recorded.
- `workspace(files=None)`: start: import_labels from the labels files the
  saves' console reads. `finish(names_file=None)`: export_names into its
  names file, then the console's labels-sign on it.
- `import_layouts(path)`: local types from a `layout-export` file, a packed
  struct per class or struct and an enum per enum.
- `apply_this(path)`: type the first argument `Class *this` on functions a
  labels file names `Class::Method` or `Class_Method`.
- `name(target, 'Class::Method')`: name a function you found as yours and type its `this`.
- `reload()`: load the repo's code again, so an update runs without restarting IDA.

Modules: save (a save as a plan, plain Python), database (what a database keeps), loader,
labels, decompile, layout_types, session (fetch, workspace, finish). The planning is plain
Python, tested without IDA (tests/python/test_ida_*.py).
"""
from .decompile import decompile_all, decompile_to, file_names, matcher, summary
from .database import recorded_console
from .labels import (best, build_of, export_names, import_labels, labels_message, load, made_up,
                     merge_user_rows, of_build, plan, user_rows)
from .layout_types import (apply_this, c_name, declarations, exec_method, import_layouts, layouts, this_class,
                           thunk_body)
from .loader import FORMAT, SaveError, accept_file, add_saved, load_file, saved_types
from .save import additions, join, methods, parse, sanitize, stubs
from .session import (FetchError, console_command, console_results, fetch, fetch_calls, finish, name, run_console,
                      sign_summary, workspace, workspace_files)

# Dependencies first: reload() reloads them in this order.
_MODULES = ('save', 'database', 'decompile', 'labels', 'layout_types', 'loader', 'session')


def reload():
    """Load every module again from the repo, then this package, so code updated since IDA imported
    it runs (the loader stub follows: it calls through the module). Returns the package."""
    import importlib
    import sys

    for name in _MODULES:
        importlib.reload(sys.modules[f'{__name__}.{name}'])
    return importlib.reload(sys.modules[__name__])
