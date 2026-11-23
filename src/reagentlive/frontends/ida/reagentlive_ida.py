"""ReAgentLive's IDA side, one file: a loader for saves and the names round trip.

Copy this file into IDA's user loaders folder (Windows:
%APPDATA%\\Hex-Rays\\IDA Pro\\loaders); IDA's headless library reads the
same folder. Then:

- **Open a save** (`disasm` with `function` and `save`):
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
  `<console>.labels.jsonl`, `export-labels`, `peek` with `label`): a full database of
  the build or a save database. Your own names stay; a label becomes a
  comment there.
- `export_names(path)`: write the names you gave in this database into a
  labels file as `user` rows, so later saves and imports show them (not
  library names: FLIRT functions and library items).
- `decompile_to(targets, out_dir, grep=None)`: one `.c` file per function
  (addresses or names), a short summary each, the lines matching `grep`.
  `decompile_all()`: every function's pseudocode, one string.
- `fetch(addrs, callees=True, depth=1, decompile_dir=None, console=None, grep=None, timeout=120)`:
  the console's save of each address (and its callees, `depth` levels),
  added to the open database, decompiled into `decompile_dir`: one call
  from an address to pseudocode.
- `import_layouts(path)`: local types from a `layout-export` file, a packed
  struct per class or struct and an enum per enum.
- `apply_this(path)`: type the first argument `Class *this` on functions a
  labels file names `Class::Method` or `Class_Method`.

Run as a script (File > Script file...) it imports labels, asking for the
file. The planning below is plain Python, tested without IDA
(tests/python/test_ida_function.py, test_ida_labels.py, test_ida_types.py).
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
    segment holds, 'layouts': the layout rows as layout-export lines (JSON text)}.
    """
    build, functions, chunks, segments, names, layout_rows = None, [], [], [], [], []
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
            elif kind == 'layout':
                del row['row']
                layout_rows.append(json.dumps(row))
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
    segments = join(segments)
    if isinstance(segments, str):
        return segments
    held = [(start, start + len(data)) for start, data, _ in segments]
    return {'build': build, 'functions': functions, 'chunks': chunks, 'segments': segments, 'names': names,
            'stubs': stubs(names, held), 'layouts': layout_rows}


def methods(plan):
    """The saved functions a name row names, [(address, name)] in file order: the ones whose class
    prefix may type `Class *this` (this_class)."""
    named = {address: name for address, name, what in plan['names'] if what == 'function'}
    return [(start, named[start]) for start, _ in plan['functions'] if start in named]


def _groups(items):
    """Items (start, bytes, kind, tag) sorted and grouped where they overlap, or touch with the same kind:
    [(start, bytes, kind, [members])], or a string naming the first address whose bytes differ.
    A group is CODE when any member is (code bytes a data row also read)."""
    groups = []
    for item in sorted(items, key=lambda i: i[0]):
        start, data, kind, _ = item
        if groups:
            low, held, held_kind, members = groups[-1]
            end = low + len(held)
            if start < end or (start == end and kind == held_kind):
                shared = min(end, start + len(data)) - start
                if held[start - low:start - low + shared] != data[:shared]:
                    at = next(i for i in range(shared) if held[start - low + i] != data[i])
                    return f'segments overlap at {start + at:#x} with different bytes'
                groups[-1] = (low, held + data[shared:], 'CODE' if 'CODE' in (held_kind, kind) else held_kind,
                              members + [item])
                continue
        groups.append((start, data, kind, [item]))
    return groups


def join(segments):
    """Segments [(start, bytes, kind)] sorted, those that overlap with identical bytes (or touch with the
    same kind) joined into one; a string when overlapping bytes differ. One segment per run lets IDA
    keep a function whole: a function cannot cross a segment's end."""
    groups = _groups([(start, data, kind, None) for start, data, kind in segments])
    if isinstance(groups, str):
        return groups
    return [(start, data, kind) for start, data, kind, _ in groups]


def stubs(names, held):
    """The names no range in `held` [(start, end)] covers: (address, what), each once."""
    out, seen = [], set()
    for address, _, what in names:
        if address in seen or any(start <= address < end for start, end in held):
            continue
        seen.add(address)
        out.append((address, what))
    return out


def additions(segments, existing):
    """What adding `segments` [(start, bytes, kind)] to a database holding `existing` (its non-stub
    segments, same shape, with their bytes) writes, or a string when they disagree on a byte.

    Returns (writes, report). writes: [(start, bytes, kind, replaced)], one segment each, where
    `replaced` lists the existing segments [(start, end)] the new one joins and so takes the place of.
    report: {'added': new segments, 'joined': existing segments grown, 'cut': new bytes already held},
    each a list of (start, end).
    """
    groups = _groups([(s, d, k, 'old') for s, d, k in existing] + [(s, d, k, 'new') for s, d, k in segments])
    if isinstance(groups, str):
        return groups
    writes, report = [], {'added': [], 'joined': [], 'cut': []}
    for start, data, kind, members in groups:
        old = [(s, s + len(d), k) for s, d, k, tag in members if tag == 'old']
        new = [(s, s + len(d)) for s, d, _, tag in members if tag == 'new']
        cut = [(max(a, s), min(b, e)) for a, b in new for s, e, _ in old if max(a, s) < min(b, e)]
        report['cut'] += cut
        if sum(b - a for a, b in cut) == sum(b - a for a, b in new) and all(k == kind for _, _, k in old):
            continue  # Held already, byte for byte (or no new bytes: existing segments that touch stay).
        writes.append((start, data, kind, [(s, e) for s, e, _ in old]))
        report['joined' if old else 'added'].append((start, start + len(data)))
    return writes, report


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


def _apply(plan, writes):
    """IDA: add `writes` [(start, bytes, kind, replaced)], each in place of the stubs it covers and the
    segments it joins; make the functions; name; stub what stays unheld.
    Returns (stub addresses replaced, None), or (_, why not)."""
    import ida_bytes
    import ida_funcs
    import ida_loader
    import ida_name
    import ida_segment

    replaced_stubs = []
    for start, data, kind, replaced in writes:
        end = start + len(data)
        covered = []
        for index in range(ida_segment.get_segm_qty()):
            stub = ida_segment.getnseg(index)
            if ida_segment.get_segm_class(stub) == 'STUB' and start <= stub.start_ea < end:
                covered.append(stub.start_ea)
        for at in covered:
            ida_segment.del_segm(at, ida_segment.SEGMOD_KILL)
        replaced_stubs += covered
        for at, _ in replaced:
            # KEEP: the joined segment's functions, names and comments stay; its bytes come back below.
            ida_segment.del_segm(at, ida_segment.SEGMOD_KEEP | ida_segment.SEGMOD_SILENT)
        segment = ida_segment.segment_t()
        segment.start_ea, segment.end_ea, segment.bitness = start, end, 2
        segment.perm = ida_segment.SEGPERM_READ | (ida_segment.SEGPERM_EXEC if kind == 'CODE' else 0)
        if not ida_segment.add_segm_ex(segment, f'{kind.lower()}_{start:x}', kind, ida_segment.ADDSEG_NOSREG):
            return replaced_stubs, f'no segment at {start:#x}'
        ida_loader.mem2base(data, start, -1)  # -1: no position in the input file.
    build = plan['build']
    for start, _ in plan['functions']:
        if not ida_funcs.get_func(start):
            ida_funcs.add_func(start)
        if not ida_bytes.get_cmt(start, False):  # A comment there already (yours, or this one) stays.
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
        if not ida_bytes.has_user_name(ida_bytes.get_flags(address)):  # A name you gave stays.
            ida_name.set_name(address, name, _auto_name())
    unloaded = [start for start, _ in plan['functions']] + [start for start, _, _ in plan['chunks']]
    unloaded = [f'{start:#x}' for start in unloaded if not ida_bytes.is_loaded(start)]
    if unloaded:
        return replaced_stubs, f"no bytes at {', '.join(unloaded)} after loading"
    return replaced_stubs, None


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
    _, failed = _apply(plan, [(start, data, kind, []) for start, data, kind in plan['segments']])
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
    if plan['layouts']:
        # The types now; `this` once auto-analysis is done (no decompiler while loading).
        types = saved_types(plan, now=False)
        if types.get('error') or types['types_failed']:
            ida_kernwin.msg(f'ReAgentLive types: {json.dumps(types)}\n')
    return 1


class SaveError(ValueError):
    """add_saved refused the file or could not load all of it; the message says why."""


def _ranges(ranges):
    return [f'{start:#x}-{end:#x}' for start, end in ranges]


def add_saved(path):
    """IDA: add another save of the same build to the open database, so one database gathers every save.

    Bytes the database holds already are cut from the new save (they must be identical: one build);
    a new run that overlaps or touches an existing segment joins it into one segment, so a function
    never ends at a segment's edge. Raises SaveError when the file cannot be loaded, is of another
    build, disagrees with a byte the database holds, or leaves a saved function or chunk start without
    bytes. Returns {'added': new segments, 'joined': existing segments grown, 'cut': new bytes already
    held, as 'start-end' strings; 'stubs_replaced': stub addresses now holding saved bytes}."""
    import ida_bytes
    import ida_netnode
    import ida_segment

    with open(path, encoding='utf-8') as file:
        plan = parse(file.read().splitlines())
    if isinstance(plan, str):
        raise SaveError(f'{path}: {plan}')
    known = ida_netnode.netnode(_NODE, 0, False)
    if known.index() == ida_netnode.BADNODE or json.loads(known.supstr(0) or 'null') != plan['build']:
        raise SaveError(f"{path}: not a save of this database's build")
    existing = []
    for index in range(ida_segment.get_segm_qty()):
        segment = ida_segment.getnseg(index)
        if ida_segment.get_segm_class(segment) != 'STUB':
            data = ida_bytes.get_bytes(segment.start_ea, segment.end_ea - segment.start_ea) or b''
            existing.append((segment.start_ea, data, ida_segment.get_segm_class(segment)))
    planned = additions(plan['segments'], existing)
    if isinstance(planned, str):
        raise SaveError(f'{path}: {planned} (the file against the database)')
    writes, report = planned
    held = [(s, s + len(d)) for s, d, _ in existing] + [(s, s + len(d)) for s, d, _ in plan['segments']]
    plan['stubs'] = stubs(plan['names'], held)
    replaced, failed = _apply(plan, writes)
    if failed:
        raise SaveError(f'{path}: {failed}')
    summary = {'added': _ranges(report['added']), 'joined': _ranges(report['joined']),
               'cut': _ranges(report['cut']), 'stubs_replaced': [f'{at:#x}' for at in replaced]}
    if plan['layouts']:
        types = saved_types(plan)
        if types.get('error'):
            raise SaveError(f"{path}: layout rows: {types['error']}")
        summary.update(types)
    return summary


_PENDING = '$ reagentlive this'  # Functions a loaded save still has to type `Class *this`.


def saved_types(plan, now=True):
    """IDA: a save's layout rows as local types (import_layouts' logic: types of the same name are
    replaced), then `Class *this` on its functions whose names give a class (apply_this' logic),
    with any a load left pending. Returns {'types': types and enums declared, 'types_failed':
    declarations IDA refused, 'this_typed': n, 'this_skipped': ['name: why']}, or {'error': why}
    when the rows are no layout. With `now` false (while loading: no decompiler yet) the
    functions wait in the database and are typed when auto-analysis ends, or by the next
    add_saved; this_typed is then 0."""
    parsed = layouts(plan['layouts'])
    if isinstance(parsed, str):
        return {'error': parsed}
    report = _declare(*parsed)
    out = {'types': len(report.get('types', [])) + len(report.get('enums', [])), 'types_failed': report['failed'],
           'this_typed': 0, 'this_skipped': []}
    if not now:
        _defer_this(methods(plan))
        return out
    typed = _type_this(_take_pending() + methods(plan))
    out.update(this_typed=typed['typed'], this_skipped=typed['skipped'] + [f'{name}: no decompiler' for _, name in typed['left']])
    return out


def _defer_this(named):
    """IDA: keep [(start, name)] in the database and type them when auto-analysis ends."""
    import ida_idp
    import ida_netnode

    ida_netnode.netnode(_PENDING, 0, True).supset(0, json.dumps(named))

    class _AfterAnalysis(ida_idp.IDB_Hooks):
        def auto_empty_finally(self):
            self.unhook()
            ida_idp.reagentlive_this_hook = None
            pending = _take_pending()
            if pending:
                left = _type_this(pending)['left']
                if left:  # No decompiler yet: the next add_saved types them.
                    ida_netnode.netnode(_PENDING, 0, True).supset(0, json.dumps(left))
            return 0

    hook = _AfterAnalysis()
    hook.hook()
    ida_idp.reagentlive_this_hook = hook  # Kept alive past this loader module.


def _take_pending():
    """IDA: the functions a load left to type, removed from the database: [(start, name)]."""
    import ida_netnode

    node = ida_netnode.netnode(_PENDING, 0, False)
    if node.index() == ida_netnode.BADNODE:
        return []
    named = [tuple(item) for item in json.loads(node.supstr(0) or '[]')]
    node.kill()
    return named


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
    A replaced row's other fields (its signature, where it was ported from) stay: they follow the
    address, not the name. Adds and updates only: a database never deletes names it does not hold."""
    named = {row['rva']: dict(row) for row in rows}
    out = []
    for line in lines:
        if not line.strip():
            continue
        try:
            row = json.loads(line)
            if (row.get('kind') == 'user' and row.get('module_size') == build['module_size']
                    and row.get('timestamp') == build['timestamp'] and row.get('rva') in named):
                named[row['rva']] = {**row, **named[row['rva']]}
                continue
        except ValueError:
            continue  # Malformed: dropped.
        out.append(line.rstrip('\n'))
    out.extend(json.dumps(named[row['rva']], sort_keys=True, separators=(',', ':')) for row in rows)
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
    """Name the open database (a full one or a save) from a labels file, or a list of them (the
    console's derived names and the host's names file); asked for when None.
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
    rows = []
    for one in ([path] if isinstance(path, str) else path):
        try:
            with open(one, encoding='utf-8') as handle:
                rows += load(handle)
        except FileNotFoundError:
            continue  # A host file not made yet.
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
    `user` rows of its build, adding or updating them; names it does not hold stay. Library names (a
    FLIRT function, a library item) are IDA's, not the person's: never exported. Returns how many."""
    import ida_bytes
    import ida_funcs
    import ida_kernwin
    import ida_nalt
    import idautils

    def library(ea):
        function = ida_funcs.get_func(ea)
        return bool(function and function.flags & ida_funcs.FUNC_LIB) or ida_nalt.is_libitem(ea)


    if path is None:
        path = ida_kernwin.ask_file(1, '*.jsonl', 'ReAgentLive labels file to write your names into')
        if not path:
            return None
    build = _database_build()
    if not build:
        ida_kernwin.warning('ReAgentLive names not exported: this database has no PE header and is no save')
        return None
    names = [(ea - build['base'], name) for ea, name in idautils.Names()
             if ida_bytes.has_user_name(ida_bytes.get_flags(ea)) and ea >= build['base'] and not library(ea)]
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


# --- Pseudocode out: to files, or one string ---------------------------------

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


# --- Reflected layouts in: local types, and `this` on methods ----------------

_C_UNSAFE = re.compile(r'[^A-Za-z0-9_]')
_KEYWORDS = {'auto', 'bool', 'break', 'case', 'char', 'class', 'const', 'continue', 'default', 'delete', 'do',
             'double', 'else', 'enum', 'extern', 'false', 'float', 'for', 'goto', 'if', 'int', 'long', 'new',
             'private', 'protected', 'public', 'register', 'return', 'short', 'signed', 'sizeof', 'static',
             'struct', 'switch', 'template', 'this', 'true', 'typedef', 'union', 'unsigned', 'virtual', 'void',
             'volatile', 'while'}
# Property kinds with a fixed C type: (type, natural size).
_SCALARS = {
    'FloatProperty': ('float', 4), 'DoubleProperty': ('double', 8), 'BoolProperty': ('unsigned __int8', 1),
    'Int8Property': ('__int8', 1), 'Int16Property': ('__int16', 2), 'IntProperty': ('__int32', 4),
    'Int64Property': ('__int64', 8), 'ByteProperty': ('unsigned __int8', 1),
    'UInt16Property': ('unsigned __int16', 2), 'UInt32Property': ('unsigned __int32', 4),
    'UInt64Property': ('unsigned __int64', 8),
    'NameProperty': ('struct FName', 8), 'StrProperty': ('struct FString', 16),
    'WeakObjectProperty': ('struct FWeakObjectPtr', 8), 'InterfaceProperty': ('struct FScriptInterface', 16),
}
_POINTERS = ('ObjectProperty', 'ClassProperty', 'ObjectPtrProperty', 'ClassPtrProperty')
# Engine types the properties above use. layout-export names types without their C++ prefix, so these
# do not meet an exported name unless a file holds one of that name (then the file's is used).
_ENGINE_TYPES = {
    'FName': 'struct FName { __int32 ComparisonIndex; __int32 Number; };',
    'FString': 'struct FString { unsigned __int16 *Data; __int32 Num; __int32 Max; };',
    'TArray': 'struct TArray { void *Data; __int32 Num; __int32 Max; };',
    'FWeakObjectPtr': 'struct FWeakObjectPtr { __int32 ObjectIndex; __int32 ObjectSerialNumber; };',
    'FScriptInterface': 'struct FScriptInterface { void *Object; void *Interface; };',
}
_INTS = {1: 'unsigned __int8', 2: 'unsigned __int16', 4: 'unsigned __int32', 8: 'unsigned __int64'}


def c_name(name):
    """A C identifier for `name`: other characters '_', a leading digit or a keyword gets a '_'."""
    out = _C_UNSAFE.sub('_', name) or '_'
    return '_' + out if out[0].isdigit() or out in _KEYWORDS else out


def _order(classes):
    """Type names so each comes after the types it holds by value (its super, its struct fields)."""
    done, out = set(), []

    def visit(name, path):
        if name in done or name not in classes or name in path:
            return
        path = path | {name}
        held = [classes[name].get('base')] + [f.get('struct') for f in classes[name]['fields']]
        for other in held:
            if other:
                visit(other, path)
        done.add(name)
        out.append(name)

    for name in classes:
        visit(name, frozenset())
    return out


def _ctype(prop, classes, enums, arrays):
    """(C type, natural size, None) for one property (a field, or a container's element), or
    (None, 0, why). A typed TArray's declaration is added to `arrays` {name: text}."""
    kind = prop.get('type', '')
    if kind in _POINTERS:
        target = prop.get('class')
        return (f'struct {c_name(target)} *' if target in classes else 'void *'), 8, None
    if prop.get('enum') in enums:
        return f'enum {c_name(prop["enum"])}', enums[prop['enum']].get('size') or 1, None
    if kind == 'StructProperty':
        name = prop.get('struct')
        if name not in classes or not classes[name].get('size'):
            return None, 0, f'struct {name} not in the file'
        return f'struct {c_name(name)}', classes[name]['size'], None
    if kind == 'ArrayProperty':
        element, size, why = _ctype(prop.get('element') or {}, classes, enums, arrays)
        if not element or size != (prop.get('element') or {}).get('size'):
            return 'struct TArray', 16, None
        name = 'TArray_' + c_name(element.replace('struct ', '').replace('enum ', '').replace('*', 'ptr').strip())
        arrays[name] = f'struct {name} {{ {element}{"" if element.endswith("*") else " "}*Data; __int32 Num; __int32 Max; }};'
        return f'struct {name}', 16, None
    if kind == 'EnumProperty' and prop.get('size') in _INTS:  # Its enum not in the file: the integer.
        return _INTS[prop['size']], prop['size'], None
    known = _SCALARS.get(kind)
    if known:
        return known[0], known[1], None
    return None, 0, f'{kind or "no type"}: no C type'


def _enum_values(name, enum, taken):
    """An enum's member declarations (names unique across enums, values that fit its width) and the
    values left out."""
    width = enum.get('size') or 1
    low, high = -(1 << (8 * width - 1)), (1 << (8 * width)) - 1
    values, dropped = [], []
    for value_name, value in enum['values']:
        member = c_name(value_name)
        if member in taken:
            member = c_name(f'{name}__{value_name}')
        if member in taken or not low <= value <= high:
            dropped.append(f'{name}.{value_name}: {"name taken" if member in taken else f"{value} does not fit {width} bytes"}')
            continue
        taken.add(member)
        values.append(f'{member} = {value if value < 0 else hex(value)}')
    return values, dropped


def declarations(classes, enums):
    """C declarations for the types and enums of a layout file (as `layouts` reads it), in one
    `#pragma pack(1)` block so every member sits at its reflected offset, and a report.

    A type's super, when the file holds it and its size fits before the first field, is its base
    class (`struct Pawn : Actor`), so the decompiler shows inherited fields as the type's own; else
    that part is padding. Fields follow at their offsets, typed by property kind: numbers, bool as
    uint8, FName, FString, FWeakObjectPtr and FScriptInterface as engine structs, a TArray as
    `TArray_<element>` {Data, Num, Max} (plain `TArray` when its element has no C type), object
    pointers as pointers to the class's type when the file holds it (else `void *`), a struct by
    value when the file holds it, an enum when the file holds it; a static array as an array of
    its element. Fields sharing a byte (bitfield bools) become one member named after all of them.
    Gaps are byte arrays `pad_<offset>`, and every type is padded to its size. A field with no C
    type is a byte array of its size; a field overlapping the one before is left out.
    Returns (text, report): {'types', 'enums', 'arrays': names declared, 'untyped': ['Type.Field:
    why'], 'skipped': ['Type.Field: why', 'Type: why', 'Enum.Value: why']}."""
    report = {'types': [], 'enums': [], 'arrays': [], 'untyped': [], 'skipped': []}
    lines = ['#pragma pack(push, 1)']
    lines += [text for name, text in _ENGINE_TYPES.items() if name not in classes]
    lines += [f'struct {c_name(name)};' for name in classes]
    taken = set()  # Enum members share one namespace.
    for name, enum in enums.items():
        values, dropped = _enum_values(name, enum, taken)
        lines.append(f'enum {c_name(name)} : {_INTS.get(enum.get("size") or 1, "unsigned __int8")} '
                     f'{{ {", ".join(values)} }};')
        report['enums'].append(name)
        report['skipped'] += dropped
    arrays, sizes, bodies = {}, {}, []
    for name in _order(classes):
        cls = classes[name]
        fields = sorted(cls['fields'], key=lambda f: f['offset'])
        members, used, at, head = [], set(), 0, f'struct {c_name(name)}'
        base = cls.get('base')
        if base:
            first = fields[0]['offset'] if fields else cls.get('size')
            if base not in sizes:
                report['skipped'].append(f'{name}: super {base} not in the file, padded')
            elif first is not None and sizes[base] > first:
                report['skipped'].append(f'{name}: super {base} size {sizes[base]:#x} past the first field, padded')
            else:
                head += f' : {c_name(base)}'
                at = sizes[base]
        index = 0
        while index < len(fields):
            field = fields[index]
            group = [field]
            while index + len(group) < len(fields) and fields[index + len(group)]['offset'] == field['offset']:
                group.append(fields[index + len(group)])
            index += len(group)
            offset, size = field['offset'], max(f.get('size', 0) for f in group)
            if offset < at:
                report['skipped'] += [f'{name}.{f["field"]}: overlaps the member before (at {at:#x})' for f in group]
                continue
            if offset > at:
                members.append(f'unsigned __int8 pad_{at:x}[{offset - at:#x}];')
            member = c_name('__'.join(f['field'] for f in group))
            member = member if len(member) <= 96 else member[:96].rstrip('_')  # No dangling separator.
            while member in used:
                member += '_'
            used.add(member)
            if len(group) > 1:
                typed, natural, why = (_INTS.get(size), size, None) if size in _INTS else \
                    (None, 0, f'{len(group)} fields share the offset')
            else:
                typed, natural, why = _ctype(field, classes, enums, arrays)
            if typed and natural and size % natural == 0 and size:
                count = size // natural
                spaced = typed if typed.endswith('*') else typed + ' '
                members.append(f'{spaced}{member}{f"[{count}]" if count > 1 else ""};')
            elif size > 0:
                members.append(f'unsigned __int8 {member}[{size:#x}];')
                report['untyped'].append(f'{name}.{field["field"]}: '
                                         f'{why or f"{size} bytes do not hold {typed} ({natural} bytes)"}')
            else:
                report['skipped'].append(f'{name}.{field["field"]}: size 0')
                continue
            at = offset + size
        total = cls.get('size')
        if total is not None and total > at:
            members.append(f'unsigned __int8 pad_{at:x}[{total - at:#x}];')
            at = total
        if not members and ':' not in head:
            members.append('unsigned __int8 pad_0[1];')  # C has no empty struct.
        sizes[name] = at
        bodies.append(f'{head} {{ ' + ' '.join(members) + ' };')
        report['types'].append(name)
    lines += list(arrays.values()) + bodies
    report['arrays'] = list(arrays)
    lines.append('#pragma pack(pop)')
    return '\n'.join(lines) + '\n', report


def this_class(name, known):
    """The class a method label names: `Class::Method`, or `Class_Method` with the longest prefix
    `known(prefix)` accepts (class names hold '_' too). None when no prefix is known."""
    if '::' in name:
        head = name.rsplit('::', 1)[0]
        return head if known(head) else None
    parts = name.split('_')
    for cut in range(len(parts) - 1, 0, -1):
        head = '_'.join(parts[:cut])
        if known(head):
            return head
    return None


def _number(value):
    """'0x..', decimal text or an int, as layout files write numbers."""
    return value if isinstance(value, int) else int(value, 0)


def layouts(lines):
    """(classes, enums, report) as declarations takes them, from layout-export's JSON lines (a line
    per class or struct, one per field it declares, one per enum; a line without `kind` is a field),
    or a string saying why the file cannot be used. A field's owner with no type line gets one of
    unknown size and super. An enum whose line gives no size takes it from a field that uses it.
    report: {'lines_skipped': ['line N: why']}."""
    classes, enums, skipped = {}, {}, []
    for number, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        try:
            row = json.loads(line)
            kind = row.get('kind', 'field')
            if kind in ('class', 'struct'):
                held = classes.setdefault(row['name'], {'fields': []})
                held.update(base=row.get('super'), size=_number(row['size']))
            elif kind == 'field':
                field = dict(row, offset=_number(row['offset']), size=_number(row.get('size', 0)))
                classes.setdefault(row['owner'], {'base': None, 'size': None, 'fields': []})['fields'].append(field)
            elif kind == 'enum':
                enums[row['name']] = {'size': row.get('size'), 'values': [(n, int(v)) for n, v in row['values']]}
            else:
                skipped.append(f'line {number}: unknown kind "{kind}"')
        except (ValueError, KeyError, TypeError, AttributeError) as error:
            skipped.append(f'line {number}: {error!r}')
    if not classes and not enums:
        return 'no layout lines: not a layout-export file'
    for cls in classes.values():
        for field in cls['fields']:
            for prop in (field, field.get('element'), field.get('key'), field.get('value')):
                if isinstance(prop, dict) and prop.get('enum') in enums and not enums[prop['enum']]['size']:
                    enums[prop['enum']]['size'] = prop.get('size') if prop.get('size') in _INTS else None
    return classes, enums, {'lines_skipped': skipped}


def import_layouts(path):
    """IDA: local types from a layout file (`layout-export`, `--closure` for the types fields name):
    a packed struct per class or struct, an enum per enum (see declarations). Types of the
    same name are replaced. Returns the declarations' report plus 'failed': declarations IDA refused,
    each with its first 80 characters (those types are missing, the rest are in)."""
    with open(path, encoding='utf-8') as handle:
        parsed = layouts(handle)
    if isinstance(parsed, str):
        raise ValueError(f'{path}: {parsed}')
    return _declare(*parsed)


def _declare(classes, enums, report):
    """IDA: declare what layouts() read; import_layouts' report."""
    import ida_typeinf

    text, built = declarations(classes, enums)
    flags = ida_typeinf.HTI_DCL | ida_typeinf.HTI_PAKDEF
    failed = []
    if ida_typeinf.parse_decls(None, text, None, flags):
        # Find which: each declaration alone, in order (forward declarations are in by now).
        for line in text.splitlines()[1:-1]:
            if ida_typeinf.parse_decls(None, f'#pragma pack(push, 1)\n{line}\n#pragma pack(pop)\n', None, flags):
                failed.append(line[:80])
    for key, value in built.items():
        report[key] = report.get(key, []) + value
    report['failed'] = failed
    return report


def _named_struct(name):
    """IDA: the local struct type named `name`, or None."""
    import ida_typeinf

    typed = ida_typeinf.tinfo_t()
    if typed.get_named_type(None, name) and typed.is_struct():
        return typed
    return None


def apply_this(labels_path):
    """IDA: for each label row of this build naming a function `Class::Method` or `Class_Method` whose
    class is a local struct type (import_layouts makes them), type the function's first argument
    `Class *this`. Other arguments and the return type stay as IDA has them (decompiler's guess when
    the function had no type). Returns {'typed': n, 'skipped': ['name: why']}."""
    with open(labels_path, encoding='utf-8') as handle:
        rows = load(handle)
    build = _database_build()
    if not build:
        raise ValueError('this database has no PE header and is no save')
    rows = of_build(rows, build['module_size'], build['timestamp'])
    if isinstance(rows, str):
        raise ValueError(rows)
    typed = _type_this([(build['base'] + row['rva'], row['name']) for row in best(rows)])
    return {'typed': typed['typed'], 'skipped': typed['skipped'] + [f'{name}: no decompiler' for _, name in typed['left']]}


def _type_this(named):
    """IDA: `Class *this` on each function [(start, name)] whose name gives a local struct type.
    Returns {'typed': n, 'skipped': ['name: why'], 'left': [(start, name)] with no function type
    yet (no decompiler)}."""
    import ida_auto
    import ida_funcs
    import ida_hexrays
    import ida_nalt
    import ida_typeinf

    ida_auto.auto_wait()
    structs, typed, skipped, left = {}, 0, [], []
    decompiler = ida_hexrays.init_hexrays_plugin()
    for ea, label in named:
        function = ida_funcs.get_func(ea)
        if not function or function.start_ea != ea:
            continue  # A global, or a function this database does not hold.
        name = this_class(label, lambda head: structs[head] if head in structs else structs.setdefault(head, _named_struct(c_name(head))))
        if name is None:
            continue  # No class part names a type: not a method of an imported class.
        current = ida_typeinf.tinfo_t()
        # A type IDA holds, else its guess when that has arguments (a guess right after
        # analysis can be an empty one), else the decompiler's.
        if not ida_nalt.get_tinfo(current, ea) and ida_typeinf.guess_tinfo(current, ea) != ida_typeinf.GUESS_FUNC_OK:
            if not decompiler:
                left.append((ea, label))
                continue
            try:
                current = ida_hexrays.decompile(ea).type
            except Exception as error:  # noqa: BLE001
                skipped.append(f"{label}: no function type ({error})")
                continue
        details = ida_typeinf.func_type_data_t()
        if not current.get_func_details(details) or details.size() == 0:
            skipped.append(f"{label}: no first argument")
            continue
        pointer = ida_typeinf.tinfo_t()
        pointer.create_ptr(structs[name])
        details[0].type, details[0].name = pointer, 'this'
        changed = ida_typeinf.tinfo_t()
        if changed.create_func(details) and ida_typeinf.apply_tinfo(ea, changed, ida_typeinf.TINFO_DEFINITE):
            typed += 1
        else:
            skipped.append(f"{label}: IDA refused the type")
    return {'typed': typed, 'skipped': skipped, 'left': left}


# --- One call from an address to decompiled text --------------------------

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
    (calls, paths). `addrs` is one address (an int or '0x..' text) or a list of them."""
    import os

    if isinstance(addrs, (str, int)):
        addrs = [addrs]
    calls, paths = [], []
    for index, address in enumerate(addrs):
        path = os.path.join(folder, f'fetch_{index}.jsonl')
        save = {'file': path}
        if callees:
            save['callees'] = depth
        calls.append({'verb': 'disasm', 'args': {'addr': _address_text(address), 'function': True, 'save': save}})
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


def _console_command(console):
    """The console to run, as a command list: the argument (a path or a list), else REAGENTLIVE_CONSOLE."""
    import os

    console = console or os.environ.get(_CONSOLE_ENV)
    if not console:
        raise FetchError(f'no console: pass console= or set {_CONSOLE_ENV} to the console executable')
    return list(console) if isinstance(console, (list, tuple)) else [console]


def fetch(addrs, callees=True, depth=1, decompile_dir=None, console=None, grep=None, timeout=120):
    """IDA: save the functions at `addrs` from the live process and add them to the open database in
    one call: runs the console (`console`, a path or a command list, else REAGENTLIVE_CONSOLE) with one
    `disasm` save per address (`save.callees` = `depth` unless `callees` is false; the native packs
    ceiling) through `-c @<file>`, then add_saved for each file, then with `decompile_dir`
    decompile_to every saved function (`grep` as decompile_to takes it).

    Returns {'functions': ['0x..' every saved function], 'skipped': the console's skipped rows,
    'added', 'joined', 'cut', 'stubs_replaced' (add_saved's, over every file), 'types', 'this_typed',
    'this_skipped', 'decompiled': decompile_to's summaries, 'seconds': {'console', 'load', 'decompile'}}.
    Raises FetchError with the console's refusal attached, SaveError when a file does not load."""
    import os
    import shutil
    import subprocess
    import tempfile
    import time

    if not 1 <= depth <= 3:
        raise ValueError('depth is 1..3')
    command = _console_command(console)
    folder = tempfile.mkdtemp(prefix='reagentlive-fetch-')
    try:
        calls, paths = fetch_calls(addrs, folder, callees, depth)
        calls_file = os.path.join(folder, 'calls.json')
        with open(calls_file, 'w', encoding='utf-8') as handle:
            json.dump(calls, handle)
        started = time.perf_counter()
        try:
            ran = subprocess.run(command + ['--caller', 'script', '--packs', 'native', '-c', '@' + calls_file],
                                 capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=timeout)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise FetchError(f'console {command[0]}: {error}') from error
        try:
            results = console_results(ran.stdout, len(calls))
        except FetchError as error:
            if not ran.stdout.strip() and ran.stderr.strip():
                raise FetchError(f'{error}; stderr: {ran.stderr.strip()[-400:]}', error.refusal) from None
            raise
        seconds = {'console': round(time.perf_counter() - started, 3)}
        out = {'functions': [], 'skipped': [], 'added': [], 'joined': [], 'cut': [], 'stubs_replaced': [],
               'types': 0, 'this_typed': 0, 'this_skipped': []}
        started = time.perf_counter()
        for path, result in zip(paths, results):
            loaded = add_saved(path)
            for key in ('added', 'joined', 'cut', 'stubs_replaced', 'this_skipped'):
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


if __name__ == '__main__':
    try:
        import idaapi  # noqa: F401
    except ImportError:
        raise SystemExit('Run this inside IDA (File > Script file...).')
    import_labels()
