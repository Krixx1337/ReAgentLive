"""An IDA loader for functions saved by `disasm <addr> --function --save <file> [--callees]`.

Copy this file into IDA's user loaders folder (Windows:
%APPDATA%\\Hex-Rays\\IDA Pro\\loaders), then open the saved .jsonl file in IDA.
It builds a small x64 database: each saved function's code and the
read-only data it references at their live addresses, the functions made,
and every `name` row applied. A name outside the saved bytes (a call target
not saved, a labelled global) gets a one-byte STUB segment so the decompiler
shows the name instead of a bare address.

`add_saved(path)`, run from IDA's Python, adds another save of the same build
to the open database: only bytes no segment holds yet, stubs replaced by real
code, so one database can grow through a whole investigation.

The planning below is plain Python, tested without IDA
(tests/python/test_ida_function.py).
"""
import json
import re

_ALLOWED = re.compile(r'[^A-Za-z0-9_:$?@.]')
_BUILD = ('module', 'module_size', 'timestamp', 'base')


def sanitize(name):
    """A name IDA accepts: other characters become '_'."""
    return _ALLOWED.sub('_', name)


def parse(lines):
    """The saved file as a plan, or a string saying why it cannot be loaded.

    Plan: {'build': {module, module_size, timestamp, base}, 'functions': [(start, end)]
    in file order, 'segments': [(start, bytes, 'CODE' | 'DATA')] sorted,
    'names': [(address, name, what)], 'stubs': [(address, what)] for names no
    segment holds}.
    """
    build, functions, segments, names = None, [], [], []
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
            elif kind in ('code', 'data'):
                data = bytes.fromhex(row['bytes'])
                if data:
                    segments.append((int(row['address'], 16), data, 'CODE' if kind == 'code' else 'DATA'))
            elif kind == 'name':
                names.append((int(row['address'], 16), sanitize(row['name']), row.get('what', '')))
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
    return {'build': build, 'functions': functions, 'segments': segments, 'names': names,
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
    for address, what in plan['stubs']:
        if ida_segment.getseg(address):
            continue
        segment = ida_segment.segment_t()
        segment.start_ea, segment.end_ea, segment.bitness = address, address + 1, 2
        segment.perm = ida_segment.SEGPERM_READ | (ida_segment.SEGPERM_EXEC if what in ('call', 'function') else 0)
        ida_segment.add_segm_ex(segment, f'stub_{address:x}', 'STUB', ida_segment.ADDSEG_NOSREG)
    for address, name, _ in plan['names']:
        ida_name.set_name(address, name, ida_name.SN_NOWARN | ida_name.SN_NOCHECK)
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
