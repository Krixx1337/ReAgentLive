"""Pseudocode out: one file per function (decompile_to), or one string (decompile_all)."""
import re

from .save import sanitize

_FILE_UNSAFE = re.compile(r'[^A-Za-z0-9_.-]')
_MATCHES = 40  # Matching lines listed per function; `matches_more` counts the rest.


def file_names(functions):
    """A `.c` file name per function [(address, name)], in order: the name with characters a file
    name cannot hold made '_', cut to 120; a name already taken (Windows ignores case) gets the
    address appended, so every function has its own file."""
    out, taken = [], set()
    for address, name in functions:
        stem = _FILE_UNSAFE.sub('_', name or '').strip('._')[:120] or f'fn_{address:x}'
        if stem.lower() in taken:
            stem = f'{stem}_{address:x}'
        taken.add(stem.lower())
        out.append(stem + '.c')
    return out


def matcher(grep):
    """A function telling whether a line matches `grep`: a regex, or a plain substring when it is
    no valid regex. None matches nothing."""
    if not grep:
        return None
    try:
        pattern = re.compile(grep)
    except re.error:
        pattern = re.compile(re.escape(grep))
    return lambda line: pattern.search(line) is not None


def summary(address, name, path, text, matches=None, error=None):
    """One function's entry: address, name, path, line count, the matching lines as
    'number: text' (1-based, as an editor counts the file) when `matches` is a matcher, and error."""
    lines = text.splitlines()
    entry = {'address': f'{address:#x}', 'name': name, 'path': path, 'lines': len(lines)}
    if matches:
        hits = [f'{number}: {line.strip()}' for number, line in enumerate(lines, 1) if matches(line)]
        entry['matches'] = hits[:_MATCHES]
        if len(hits) > _MATCHES:
            entry['matches_more'] = len(hits) - _MATCHES
    if error:
        entry['error'] = error
    return entry


def _target(target):
    """IDA: the start of the function holding `target` (an address, '0x..' text or a name), or why not."""
    import ida_funcs
    import ida_idaapi
    import ida_name

    ea = target
    if isinstance(target, str):
        try:
            ea = int(target, 16) if target.lower().startswith('0x') else None
        except ValueError:
            ea = None
        if ea is None:
            ea = ida_name.get_name_ea(ida_idaapi.BADADDR, target)
            if ea == ida_idaapi.BADADDR:
                ea = ida_name.get_name_ea(ida_idaapi.BADADDR, sanitize(target))  # As the loaders store it.
            if ea == ida_idaapi.BADADDR:
                return None, f'no name {target!r}'
    function = ida_funcs.get_func(ea)
    if not function:
        return None, f'no function at {ea:#x}'
    return function.start_ea, None


def _pseudocode(ea):
    """IDA: (pseudocode, None) or (comment text, error)."""
    import ida_hexrays

    try:
        return str(ida_hexrays.decompile(ea)), None
    except Exception as error:  # noqa: BLE001 - one function's failure must not hide the rest.
        return f'// decompile failed: {error}\n', str(error)


def decompile_to(targets, out_dir, grep=None):
    """IDA: decompile each target (an address, '0x..' text or a name; an address inside a function means
    that function) into `<out_dir>/<name>.c`, one file per function, and return a short summary per
    target: {address, name, path, lines, matches (when `grep`, a regex or substring, is given)} and
    `error` for a failed decompile (its file holds the error) or {target, error} for a target that
    names no function. A function asked twice is written once."""
    import os

    import ida_auto
    import ida_name

    # Analysis still queued (right after a load or add_saved, or in a script that loads and decompiles at
    # once) would leave frames and blocks half done: drain it first. Milliseconds on a save.
    ida_auto.auto_wait()
    if isinstance(targets, (str, int)):
        targets = [targets]
    os.makedirs(out_dir, exist_ok=True)
    found, out = [], []
    for target in targets:
        ea, why = _target(target)
        if why:
            out.append({'target': str(target), 'error': why})
        elif ea not in found:
            found.append(ea)
    names = [ida_name.get_name(ea) or f'fn_{ea:x}' for ea in found]
    matches = matcher(grep)
    for ea, name, file in zip(found, names, file_names(list(zip(found, names)))):
        text, error = _pseudocode(ea)
        path = os.path.join(out_dir, file)
        with open(path, 'w', encoding='utf-8', newline='\n') as handle:
            handle.write(text if text.endswith('\n') else text + '\n')
        out.append(summary(ea, name, path, text, matches, error))
    return out


def decompile_all():
    """IDA: every function's pseudocode, each under its name, as one string (decompile_to for files)."""
    import ida_auto
    import ida_name
    import idautils

    ida_auto.auto_wait()
    return '\n\n'.join(f'// {ida_name.get_name(ea)}\n{_pseudocode(ea)[0]}' for ea in idautils.Functions())
