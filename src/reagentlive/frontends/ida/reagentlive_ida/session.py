"""One call from an address to decompiled text (fetch), one to start (workspace), one to finish."""
import json

from .database import recorded_console
from .decompile import _target, decompile_to
from .labels import export_names, import_labels
from .layout_types import _type_this
from .loader import add_saved

_CONSOLE_ENV = 'REAGENTLIVE_CONSOLE'


class FetchError(RuntimeError):
    """fetch could not save what it asked: `refusal` is the console's error object (kind, message, ...)
    when the console refused a call, else None (no console, no reply)."""

    def __init__(self, message, refusal=None):
        super().__init__(message)
        self.refusal = refusal


def _address_text(address):
    """An address as the console takes it: '0x..' text from an int or text."""
    if isinstance(address, int):
        return f'{address:#x}'
    text = str(address).strip()
    int(text, 16)  # ValueError for no address.
    return text if text.lower().startswith('0x') else f'0x{text}'


def fetch_calls(addrs, folder, callees=True, depth=1):
    """The console calls fetch sends, one `disasm` save per address into `folder`, and the files:
    (calls, paths). `addrs` is one address (an int or '0x..' text) or a list of them. Depth 0 or
    `callees` false: the function alone."""
    import os

    if isinstance(addrs, (str, int)):
        addrs = [addrs]
    calls, paths = [], []
    for index, address in enumerate(addrs):
        path = os.path.join(folder, f'fetch_{index}.jsonl')
        save = {'file': path}
        if callees and depth:
            save['callees'] = depth
        calls.append({'verb': 'disasm', 'args': {'addr': _address_text(address), 'whole': True, 'save': save}})
        paths.append(path)
    return calls, paths


def console_results(text, count):
    """The `count` results in a console's output (one JSON envelope per line), or FetchError for the
    first refusal (its error object attached) or a missing reply."""
    replies = []
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith('{'):
            continue
        try:
            replies.append(json.loads(line))
        except ValueError:
            continue
    for reply in replies:
        if not reply.get('ok'):
            error = reply.get('error') or {}
            raise FetchError(f"console refused {reply.get('command', 'a call')}: {error.get('message', reply)}", error)
    if len(replies) < count:
        raise FetchError(f'console answered {len(replies)} of {count} calls')
    return [reply.get('result') or {} for reply in replies[:count]]


def console_command(console, recorded=None):
    """The console to run, as a command list: the argument (a path or a list), else
    REAGENTLIVE_CONSOLE, else the path the saves recorded (`recorded`, a console row)."""
    import os

    console = console or os.environ.get(_CONSOLE_ENV) or (recorded or {}).get('path')
    if not console:
        raise FetchError(f'no console: open a save database (its saves record the console), pass console= '
                         f'or set {_CONSOLE_ENV} to the console executable')
    return list(console) if isinstance(console, (list, tuple)) else [console]


def _recorded_or_none():
    """recorded_console(), or None outside IDA."""
    try:
        return recorded_console()
    except ImportError:
        return None


def run_console(command, calls, timeout=120):
    """Run the console `command` on `calls` ([{verb, args}]) through a JSON call file (`-c @<file>`,
    `--caller script`, `--packs native`): their results, or FetchError."""
    import os
    import shutil
    import subprocess
    import tempfile

    folder = tempfile.mkdtemp(prefix='reagentlive-calls-')
    try:
        calls_file = os.path.join(folder, 'calls.json')
        with open(calls_file, 'w', encoding='utf-8') as handle:
            json.dump(calls, handle)
        try:
            ran = subprocess.run(command + ['--caller', 'script', '--packs', 'native', '-c', '@' + calls_file],
                                 capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=timeout)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise FetchError(f'console {command[0]}: {error}') from error
        try:
            return console_results(ran.stdout, len(calls))
        except FetchError as error:
            if not ran.stdout.strip() and ran.stderr.strip():
                raise FetchError(f'{error}; stderr: {ran.stderr.strip()[-400:]}', error.refusal) from None
            raise
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def fetch(addrs, callees=True, depth=1, decompile_dir=None, console=None, grep=None, timeout=120):
    """IDA: save the functions at `addrs` from the live process and add them to the open database in
    one call: runs the console (`console`, a path or a command list, else REAGENTLIVE_CONSOLE, else
    the one the database's saves recorded) with one `disasm` save per address (`save.callees` =
    `depth`, 0..3; 0 or `callees` false: the function alone) through `-c @<file>`, then add_saved
    for each file, then with `decompile_dir` decompile_to every saved function (`grep` as
    decompile_to takes it).

    Returns {'functions': ['0x..' every saved function], 'skipped': the console's skipped rows,
    'added', 'joined', 'cut', 'stubs_replaced' (add_saved's, over every file), 'types', 'this_typed',
    'this_skipped', 'bodies_named', 'decompiled': decompile_to's summaries, 'seconds': {'console', 'load', 'decompile'}}.
    Raises FetchError with the console's refusal attached, SaveError when a file does not load."""
    import shutil
    import tempfile
    import time

    if not 0 <= depth <= 3:
        raise ValueError('depth is 0..3 (0: the function alone, no callees)')
    command = console_command(console, _recorded_or_none())
    folder = tempfile.mkdtemp(prefix='reagentlive-fetch-')
    try:
        calls, paths = fetch_calls(addrs, folder, callees, depth)
        started = time.perf_counter()
        results = run_console(command, calls, timeout)
        seconds = {'console': round(time.perf_counter() - started, 3)}
        out = {'functions': [], 'skipped': [], 'added': [], 'joined': [], 'cut': [], 'stubs_replaced': [],
               'types': 0, 'this_typed': 0, 'this_skipped': [], 'bodies_named': []}
        started = time.perf_counter()
        for path, result in zip(paths, results):
            loaded = add_saved(path)
            for key in ('added', 'joined', 'cut', 'stubs_replaced', 'this_skipped', 'bodies_named'):
                out[key] += loaded.get(key, [])
            out['types'] += loaded.get('types', 0)
            out['this_typed'] += loaded.get('this_typed', 0)
            saved = [one['start'] for one in result.get('functions', [])] or [result.get('address')]
            out['functions'] += [start for start in saved if start and start not in out['functions']]
            out['skipped'] += result.get('skipped', [])
        seconds['load'] = round(time.perf_counter() - started, 3)
        if decompile_dir:
            started = time.perf_counter()
            out['decompiled'] = decompile_to([int(start, 16) for start in out['functions']], decompile_dir, grep)
            seconds['decompile'] = round(time.perf_counter() - started, 3)
        out['seconds'] = seconds
        return out
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def name(target, new_name):
    """IDA: name the function holding `target` (an address, '0x..' text or a name) `new_name` as
    yours (export_names writes it, finish signs it), then type its first argument `Class *this`
    when the name is `Class::Method` or `Class_Method` of a local struct type (a save's layouts,
    import_layouts): one call for a function found by hand, such as a native function an exec
    thunk reaches through its vtable. Returns {'address', 'name', 'typed', 'why' when not typed}."""
    ea, why = _target(target)
    if why:
        raise ValueError(why)
    import ida_name

    if not ida_name.set_name(ea, new_name, ida_name.SN_NOCHECK | ida_name.SN_NOWARN):
        raise ValueError(f'IDA refused the name {new_name!r} at {ea:#x} (taken at another address?)')
    typed = _type_this([(ea, new_name)])
    out = {'address': f'{ea:#x}', 'name': new_name, 'typed': bool(typed['typed'])}
    if not out['typed']:
        reasons = typed['skipped'] + [f'{label}: no decompiler' for _, label in typed['left']]
        out['why'] = reasons[0] if reasons else f'{new_name}: no local struct type names its class'
    return out


# --- One call to start, one to finish ---------------------------------------

def workspace_files(recorded):
    """The labels files a console row names, each once: its derived names, then the host's names file."""
    files = []
    for key in ('labels', 'names'):
        path = (recorded or {}).get(key)
        if path and path not in files:
            files.append(path)
    return files


def workspace(files=None):
    """IDA: start work in an open save database: import_labels from `files`, by default the labels
    files its saves' console reads (its derived names and the host's names file). Returns
    import_labels' counts plus 'console' (the recorded console, fetch's default) and 'files'."""
    recorded = recorded_console()
    files = [files] if isinstance(files, str) else files or workspace_files(recorded)
    if not files:
        raise ValueError('no labels files: this database has no recorded console (open a save the console '
                         'wrote), so pass files=')
    counts = import_labels(files) or {}
    return {**counts, 'console': (recorded or {}).get('path'), 'files': files}


def sign_summary(result):
    """finish's summary of a labels-sign result: its counts and file, and the rows left unsigned
    as 'name: why'."""
    out = {key: result.get(key, 0) for key in ('signed', 'unsigned', 'skipped', 'failed_before')}
    out['path'] = result.get('path')
    out['unsigned_rows'] = [f"{row['name']}: {row.get('reason', 'no reason given')}"
                            for row in result.get('rows', []) if row.get('status') == 'unsigned']
    return out


def finish(names_file=None, console=None, timeout=300):
    """IDA: finish work: export_names into `names_file` (default: the names file its saves' console
    recorded), then the console's labels-sign on that file (it reads the target and writes only the
    file), so the new names survive a patch. Returns {'exported': n, 'path', 'signed', 'unsigned',
    'skipped', 'failed_before', 'unsigned_rows': ['name: why']}. Close the database afterwards."""
    recorded = recorded_console()
    names_file = names_file or (recorded or {}).get('names')
    if not names_file:
        raise ValueError('no names file: this database has no recorded console, so pass names_file=')
    exported = export_names(names_file)
    if exported is None:
        raise ValueError('names not exported: see the warning')
    command = console_command(console, recorded)
    result = run_console(command, [{'verb': 'labels-sign', 'args': {'file': names_file}}], timeout)[0]
    out = {'exported': exported, **sign_summary(result)}
    print(f"ReAgentLive finish: {exported} names exported, {out['signed']} signed, {out['unsigned']} unsigned "
          f"({out['path']})")
    return out
