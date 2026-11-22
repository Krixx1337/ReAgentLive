"""ReAgentLive's IDA side, one file: a loader for saves and the names round trip.

Copy this file into IDA's user loaders folder (Windows:
%APPDATA%\\Hex-Rays\\IDA Pro\\loaders); IDA's headless library reads the
same folder. Then:

- **Open a save** (`disasm <addr> --function --save <file> [--callees]`):
  a small x64 database of the saved functions, their chained chunks
  (function tails) and the read-only data they reference, at their live
  addresses, with the file's names. A name outside the saved bytes (a call
  or tail target not saved, a labelled global) gets a one-byte STUB segment
  so the decompiler shows the name, not a bare address. Names the file gives
  are auto names: export_names never mistakes them for yours.
- `add_saved(path)`: add another save of the same build to the open
  database (only bytes no segment holds yet; stubs replaced by real code).
- `import_labels(path)`: name a database from a labels file (the console's
  `<console>.labels.jsonl`, export-labels, peek --label): a full database of
  the build or a save database. Your own names stay; a label becomes a
  comment there.
- `export_names(path)`: write the names you gave in this database into a
  labels file as `user` rows, so later saves and imports show them.
- `decompile_all()`: every function's pseudocode, one string.

Run as a script (File > Script file...) it imports labels, asking for the
file. The planning below is plain Python, tested without IDA
(tests/python/test_ida_function.py, tests/python/test_ida_labels.py).
"""
import json
import re

_ALLOWED = re.compile(r'[^A-Za-z0-9_:$?@.]')
_BUILD = ('module', 'module_size', 'timestamp', 'base')
_STALE = "this loader is older than the file: copy the new reagentlive_ida.py into IDA's loaders folder"


def sanitize(name):
    """A name IDA accepts: other characters become '_'."""
    return _ALLOWED.sub('_', name)


def parse(lines):
    """The saved file as a plan, or a string saying why it cannot be loaded.

    Plan: {'build': {module, module_size, timestamp, base}, 'functions': [(start, end)]
    in file order, 'chunks': [(start, end, function)], 'segments': [(start, bytes, 'CODE' | 'DATA')] sorted,
    'names': [(address, name, what)], 'stubs': [(address, what)] for names no
    segment holds}.
    """
    build, functions, chunks, segments, names = None, [], [], [], []
    for number, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        try:
            row = json.loads(line)
            kind = row['row']
            if kind == 'function':
                this = {k: row[k] for k in ('module', 'module_size', 'timestamp')}
                this['base'] = int(row['base'], 16)
                if build is None:
                    build = this
                elif this != build:
                    return f'line {number}: a function of another build'
                functions.append((int(row['start'], 16), int(row['end'], 16)))
            elif kind == 'chunk':
                chunks.append((int(row['start'], 16), int(row['end'], 16), int(row['function'], 16)))
            elif kind in ('code', 'data'):
                data = bytes.fromhex(row['bytes'])
                if data:
                    segments.append((int(row['address'], 16), data, 'CODE' if kind == 'code' else 'DATA'))
            elif kind == 'name':
                names.append((int(row['address'], 16), sanitize(row['name']), row.get('what', '')))
            else:
                # A row kind added since: skipping it would load the file
                # silently with less in it (no chunks, no names).
                return f'line {number}: unknown row "{kind}"; {_STALE}'
        except (ValueError, KeyError, TypeError) as error:
            return f'line {number}: {error}'
    if build is None:
        return 'no function row: not a disasm --save file'
    if not any(kind == 'CODE' for _, _, kind in segments):
        return 'no code rows'
    segments.sort(key=lambda s: s[0])
    for (a, data, _), (b, _, _) in zip(segments, segments[1:]):
        if a + len(data) > b:
            return f'segments overlap at {b:#x}'
    held = [(start, start + len(data)) for start, data, _ in segments]
    return {'build': build, 'functions': functions, 'chunks': chunks, 'segments': segments, 'names': names,
            'stubs': stubs(names, held)}


def stubs(names, held):
    """The names no range in `held` [(start, end)] covers: (address, what), each once."""
    out, seen = [], set()
    for address, _, what in names:
        if address in seen or any(start <= address < end for start, end in held):
            continue
        seen.add(address)
        out.append((address, what))
    return out


def additions(segments, occupied):
    """Each segment cut to the bytes no range in `occupied` [(start, end)] holds."""
    out = []
    for start, data, kind in segments:
        cuts = [(start, start + len(data))]
        for low, high in occupied:
            cuts = [piece for a, b in cuts
                    for piece in ((a, min(b, low)), (max(a, high), b)) if piece[0] < piece[1]]
        out.extend((a, data[a - start:b - start], kind) for a, b in cuts)
    return sorted(out, key=lambda s: s[0])


FORMAT = 'ReAgentLive saved function'
ACCEPT_FIRST = 0x8000  # ida_loader.ACCEPT_FIRST: offered before IDA's own "Binary file".
_NODE = '$ reagentlive build'  # The database's build, so add_saved refuses another one.


def _auto_name():
    """IDA: set_name flags for a name from a file, not the user's (export_names skips it)."""
    import ida_name
    return ida_name.SN_AUTO | ida_name.SN_NOCHECK | ida_name.SN_NOWARN


def accept_file(li, filename):
    """IDA: claim the file when its first line is a function row."""
    li.seek(0)
    first = li.read(512)
    if isinstance(first, bytes):
        first = first.decode('utf-8', 'replace')
    if first.lstrip().startswith('{') and ('"row":"function"' in first or '"row": "function"' in first):
        return {'format': FORMAT, 'processor': 'metapc', 'options': 1 | ACCEPT_FIRST}
    return 0


def _apply(plan, segments):
    """IDA: add `segments`, replacing stubs they cover; make the functions; name; stub what stays unheld."""
    import ida_bytes
    import ida_funcs
    import ida_loader
    import ida_name
    import ida_segment

    for start, data, kind in segments:
        end = start + len(data)
        covered = []
        for index in range(ida_segment.get_segm_qty()):
            stub = ida_segment.getnseg(index)
            if ida_segment.get_segm_class(stub) == 'STUB' and start <= stub.start_ea < end:
                covered.append(stub.start_ea)
        for at in covered:
            ida_segment.del_segm(at, ida_segment.SEGMOD_KILL)
        segment = ida_segment.segment_t()
        segment.start_ea, segment.end_ea, segment.bitness = start, end, 2
        segment.perm = ida_segment.SEGPERM_READ | (ida_segment.SEGPERM_EXEC if kind == 'CODE' else 0)
        if not ida_segment.add_segm_ex(segment, f'{kind.lower()}_{start:x}', kind, ida_segment.ADDSEG_NOSREG):
            return f'no segment at {start:#x}'
        ida_loader.mem2base(data, start, -1)  # -1: no position in the input file.
    build = plan['build']
    for start, _ in plan['functions']:
        if not ida_funcs.get_func(start):
            ida_funcs.add_func(start)
        ida_bytes.set_cmt(start, f"reagentlive: {build['module']}+{start - build['base']:#x} "
                          f"(size {build['module_size']}, timestamp {build['timestamp']})", False)
    for start, end, owner in plan['chunks']:
        function = ida_funcs.get_func(owner)
        if function and not ida_funcs.get_func(start):
            ida_funcs.append_func_tail(function, start, end)
    for address, what in plan['stubs']:
        if ida_segment.getseg(address):
            continue
        segment = ida_segment.segment_t()
        segment.start_ea, segment.end_ea, segment.bitness = address, address + 1, 2
        segment.perm = ida_segment.SEGPERM_READ | (ida_segment.SEGPERM_EXEC if what in ('call', 'tail', 'function') else 0)
        ida_segment.add_segm_ex(segment, f'stub_{address:x}', 'STUB', ida_segment.ADDSEG_NOSREG)
    for address, name, _ in plan['names']:
        ida_name.set_name(address, name, _auto_name())
    return None


def load_file(li, neflags, format):
    """IDA: build the database from the plan; 1 on success, 0 on failure."""
    import ida_entry
    import ida_ida
    import ida_idp
    import ida_kernwin
    import ida_loader
    import ida_netnode
    import ida_typeinf

    if format != FORMAT:
        return 0
    ida_idp.set_processor_type('metapc', ida_idp.SETPROC_LOADER)
    ida_ida.inf_set_app_bitness(64)
    # Windows x64 code: without this IDA assumes GNU C++ and the System V
    # calling convention, and the decompiler garbles the parameters.
    ida_typeinf.set_compiler_string('vc', True)
    if neflags & ida_loader.NEF_RELOAD:
        return 1  # Reloading the same bytes into an existing database: nothing to add.
    li.seek(0)
    text = li.read(li.size())
    if isinstance(text, bytes):
        text = text.decode('utf-8', 'replace')
    plan = parse(text.splitlines())
    if isinstance(plan, str):
        ida_kernwin.warning(f'ReAgentLive function not loaded: {plan}')
        return 0
    failed = _apply(plan, plan['segments'])
    if failed:
        ida_kernwin.warning(f'ReAgentLive function not loaded: {failed}')
        return 0
    first = plan['functions'][0][0]
    named = dict((address, name) for address, name, _ in plan['names'])
    ida_entry.add_entry(first, first, named.get(first, 'start'), True)
    if first in named:
        import ida_name
        ida_name.set_name(first, named[first], _auto_name())  # The entry's name is the file's too.
    ida_netnode.netnode(_NODE, 0, True).supset(0, json.dumps(plan['build']))
    return 1


def add_saved(path):
    """IDA: add another save of the same build to the open database; None, or why not."""
    import ida_netnode
    import ida_segment

    with open(path, encoding='utf-8') as file:
        plan = parse(file.read().splitlines())
    if isinstance(plan, str):
        return plan
    known = ida_netnode.netnode(_NODE, 0, False)
    if known.index() == ida_netnode.BADNODE or json.loads(known.supstr(0) or 'null') != plan['build']:
        return 'not a save of this database\'s build'
    occupied = []
    for index in range(ida_segment.get_segm_qty()):
        segment = ida_segment.getnseg(index)
        if ida_segment.get_segm_class(segment) != 'STUB':
            occupied.append((segment.start_ea, segment.end_ea))
    plan['stubs'] = stubs(plan['names'], occupied + [(s, s + len(d)) for s, d, _ in plan['segments']])
    return _apply(plan, additions(plan['segments'], occupied))


# --- Names round trip: labels files in, your names out ---------------------

# Names IDA or the loader make up: never exported as yours.
_DEFAULT_NAME = re.compile(r'^(fn|sub|stub|loc|locret|j|nullsub|unknown|off|byte|word|dword|qword|unk|asc)_[0-9A-Fa-f]+(_\d+)?$')


def load(lines):
    """Rows {module, module_size, timestamp, rva (int), name, kind} from JSON lines; malformed ones skipped."""
    rows = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        try:
            row = json.loads(line)
            row['rva'] = int(row['rva'], 16)
            if row['name'] and row['module'] and row['module_size'] and 'timestamp' in row:
                rows.append(row)
        except (ValueError, KeyError, TypeError):
            continue
    return rows


def of_build(rows, image_size, timestamp):
    """The rows of this build (one file holds several builds), or why there are none."""
    mine = [row for row in rows if (row['module_size'], row['timestamp']) == (image_size, timestamp)]
    if not rows:
        return 'no labels in the file'
    if not mine:
        builds = sorted({(row['module_size'], row['timestamp']) for row in rows})
        return f'no labels for this build ({image_size}, {timestamp}); the file has {builds}'
    return mine


_RANK = {'user': 0, 'global': 1, 'exec': 2, 'interface': 3}


def best(rows):
    """One row per RVA: the highest-ranked kind (user, global, exec, interface, the rest), as the console merges."""
    out = {}
    for row in rows:
        kept = out.get(row['rva'])
        if kept is None or _RANK.get(row['kind'], 4) < _RANK.get(kept['kind'], 4):
            out[row['rva']] = row
    return [out[rva] for rva in sorted(out)]


def plan(rows, existing):
    """Actions for the rows: ('name', rva, name, kind), ('comment', rva, text) or ('skip', rva).

    existing(rva) returns (current name or '', set by the user). A user's name
    stays and the label becomes a comment. A name already taken at another
    address also ends as a comment, when IDA refuses it at apply time.
    """
    actions = []
    for row in rows:
        name = sanitize(row['name'])
        current, user = existing(row['rva'])
        if current == name:
            actions.append(('skip', row['rva']))  # Already named so.
            continue
        if user:
            actions.append(('comment', row['rva'], f"reagentlive: {row['name']}"))
        else:
            actions.append(('name', row['rva'], name, row['kind']))
    return actions


def user_rows(names, build):
    """Label rows ('user') for the names a person gave: [(rva, name)] minus made-up names."""
    return [{'kind': 'user', 'module': build['module'], 'module_size': build['module_size'], 'name': name,
             'rva': hex(rva), 'timestamp': build['timestamp']}
            for rva, name in sorted(names) if name and not _DEFAULT_NAME.match(name)]


def merge_user_rows(lines, rows, build):
    """The labels file's lines with `rows` in: a user row of this build at an address `rows` names is
    replaced, every other line is kept (other databases' names, other builds, reflected names, globals).
    Adds and updates only: a database never deletes names it does not hold."""
    named = {row['rva'] for row in rows}
    out = []
    for line in lines:
        if not line.strip():
            continue
        try:
            row = json.loads(line)
            if (row.get('kind') == 'user' and row.get('module_size') == build['module_size']
                    and row.get('timestamp') == build['timestamp'] and row.get('rva') in named):
                continue
        except ValueError:
            continue  # Malformed: dropped.
        out.append(line.rstrip('\n'))
    out.extend(json.dumps(row, sort_keys=True, separators=(',', ':')) for row in rows)
    return out


def build_of(header):
    """(SizeOfImage, TimeDateStamp) from PE header bytes that start at 'PE\\0\\0', or None."""
    if not header or len(header) < 0x54 or header[:4] != b'PE\0\0':
        return None
    return int.from_bytes(header[0x50:0x54], 'little'), int.from_bytes(header[8:12], 'little')


def _database_build():
    """The open database's build {module, module_size, timestamp, base}: a save's (kept by the
    loader), else a full database's from the PE header IDA keeps. None when neither is there."""
    import ida_bytes
    import ida_nalt
    import ida_netnode
    import idautils

    known = ida_netnode.netnode(_NODE, 0, False)
    if known.index() != ida_netnode.BADNODE and known.supstr(0):
        return json.loads(known.supstr(0))
    build = build_of(idautils.peutils_t().header())
    base = ida_nalt.get_imagebase()
    if not build and ida_bytes.is_loaded(base + 0x3C):  # A manual load with the header bytes in the database.
        pe = base + ida_bytes.get_dword(base + 0x3C)
        build = build_of(ida_bytes.get_bytes(pe, 0x54))
    if not build:
        return None
    return {'module': ida_nalt.get_root_filename(), 'module_size': build[0], 'timestamp': build[1], 'base': base}


def import_labels(path=None):
    """Name the open database (a full one or a save) from a labels file (asked for when None).
    Returns (named, commented, skipped), or None when nothing was imported."""
    import ida_bytes
    import ida_funcs
    import ida_kernwin
    import ida_name
    import ida_undo

    if path is None:
        path = ida_kernwin.ask_file(0, '*.jsonl', 'ReAgentLive labels file')
        if not path:
            return None
    with open(path, encoding='utf-8') as handle:
        rows = load(handle)
    build = _database_build()
    if not build:
        ida_kernwin.warning('ReAgentLive labels not imported: this database has no PE header and is no save')
        return None
    rows = of_build(rows, build['module_size'], build['timestamp'])
    if isinstance(rows, str):
        ida_kernwin.warning(f'ReAgentLive labels not imported: {rows}')
        return None
    base = build['base']

    def existing(rva):
        ea = base + rva
        return ida_name.get_name(ea) or '', ida_bytes.has_user_name(ida_bytes.get_flags(ea))

    ida_undo.create_undo_point('ReAgentLive labels', 'ReAgentLive labels')  # One Undo reverts the import.
    actions = plan(best(rows), existing)
    named = commented = skipped = 0
    ida_kernwin.show_wait_box('ReAgentLive labels: importing')
    try:
        for index, action in enumerate(actions):
            if index % 500 == 0:
                if ida_kernwin.user_cancelled():
                    break
                ida_kernwin.replace_wait_box(f'ReAgentLive labels: {index} of {len(actions)}')
            ea = base + action[1]
            if action[0] == 'skip':
                skipped += 1
                continue
            if action[0] == 'name':
                if not ida_bytes.is_loaded(ea):
                    skipped += 1  # Outside a save's bytes.
                    continue
                if action[3] == 'exec' and not ida_funcs.get_func(ea):
                    ida_funcs.add_func(ea)
                if ida_name.set_name(ea, action[2], _auto_name()):
                    named += 1
                    continue
                text = f'reagentlive: {action[2]}'  # Taken at another address: keep it as a comment.
            else:
                text = action[2]
            old = ida_bytes.get_cmt(ea, False) or ''
            if text in old:
                skipped += 1  # Imported before.
                continue
            ida_bytes.set_cmt(ea, f'{old}\n{text}' if old else text, False)
            commented += 1
    finally:
        ida_kernwin.hide_wait_box()
    print(f'ReAgentLive labels: {named} named, {commented} commented, {skipped} skipped, from {len(rows)} rows')
    return named, commented, skipped


def export_names(path=None):
    """Write the names a person gave in this database into a labels file (asked for when None) as
    `user` rows of its build, adding or updating them; names it does not hold stay. Returns how many."""
    import ida_bytes
    import ida_kernwin
    import idautils

    if path is None:
        path = ida_kernwin.ask_file(1, '*.jsonl', 'ReAgentLive labels file to write your names into')
        if not path:
            return None
    build = _database_build()
    if not build:
        ida_kernwin.warning('ReAgentLive names not exported: this database has no PE header and is no save')
        return None
    names = [(ea - build['base'], name) for ea, name in idautils.Names()
             if ida_bytes.has_user_name(ida_bytes.get_flags(ea)) and ea >= build['base']]
    rows = user_rows(names, build)
    try:
        with open(path, encoding='utf-8') as handle:
            lines = handle.read().splitlines()
    except FileNotFoundError:
        lines = []
    with open(path, 'w', encoding='utf-8', newline='\n') as handle:
        handle.write('\n'.join(merge_user_rows(lines, rows, build)) + '\n')
    print(f'ReAgentLive names: {len(rows)} user names written to {path}')
    return len(rows)


def decompile_all():
    """Every function's pseudocode, each under its name, as one string."""
    import ida_hexrays
    import ida_name
    import idautils

    parts = []
    for ea in idautils.Functions():
        try:
            text = str(ida_hexrays.decompile(ea))
        except Exception as error:  # noqa: BLE001 - one function's failure must not hide the rest.
            text = f'// decompile failed: {error}'
        parts.append(f'// {ida_name.get_name(ea)}\n{text}')
    return '\n\n'.join(parts)


if __name__ == '__main__':
    try:
        import idaapi  # noqa: F401
    except ImportError:
        raise SystemExit('Run this inside IDA (File > Script file...).')
    import_labels()
