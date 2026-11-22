"""An IDA loader for one function saved by `disasm <addr> --function --save <file>`.

Copy this file into IDA's user loaders folder (Windows:
%APPDATA%\\Hex-Rays\\IDA Pro\\loaders), then open the saved .jsonl file in IDA.
It builds a small x64 database: the function's code and the read-only data
it references at their live addresses, the function made, imports named.
IDA then decompiles that one function in seconds, with no analysis of the
whole image. Calls into code that was not saved show as bare addresses;
save a callee into a database of its own.

The planning below is plain Python, tested without IDA
(tests/python/test_ida_function.py).
"""
import json
import re

_ALLOWED = re.compile(r'[^A-Za-z0-9_:$?@.]')


def sanitize(name):
    """A name IDA accepts: other characters become '_'."""
    return _ALLOWED.sub('_', name)


def parse(lines):
    """The saved file as a plan, or a string saying why it cannot be loaded.

    Plan: {'function': {module, module_size, timestamp, base, start, end},
    'segments': [(start, bytes, 'CODE' | 'DATA')] sorted, 'names': [(address, name)]}.
    """
    function, segments, names = None, [], []
    for number, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        try:
            row = json.loads(line)
            kind = row['row']
            if kind == 'function':
                function = {k: row[k] for k in ('module', 'module_size', 'timestamp')}
                for k in ('base', 'start', 'end'):
                    function[k] = int(row[k], 16)
            elif kind in ('code', 'data'):
                data = bytes.fromhex(row['bytes'])
                if data:
                    segments.append((int(row['address'], 16), data, 'CODE' if kind == 'code' else 'DATA'))
            elif kind == 'name':
                names.append((int(row['address'], 16), sanitize(row['name'])))
        except (ValueError, KeyError, TypeError) as error:
            return f'line {number}: {error}'
    if function is None:
        return 'no function row: not a disasm --save file'
    if not any(kind == 'CODE' for _, _, kind in segments):
        return 'no code rows'
    segments.sort(key=lambda s: s[0])
    for (a, data, _), (b, _, _) in zip(segments, segments[1:]):
        if a + len(data) > b:
            return f'segments overlap at {b:#x}'
    return {'function': function, 'segments': segments, 'names': names}


def accept_file(li, filename):
    """IDA: claim the file when its first line is a function row."""
    li.seek(0)
    first = li.read(512)
    if isinstance(first, bytes):
        first = first.decode('utf-8', 'replace')
    if first.lstrip().startswith('{') and ('"row":"function"' in first or '"row": "function"' in first):
        return {'format': 'ReAgentLive saved function', 'processor': 'metapc'}
    return 0


def load_file(li, neflags, format):
    """IDA: build the database from the plan."""
    import ida_bytes
    import ida_funcs
    import ida_ida
    import ida_idp
    import ida_name
    import ida_segment
    import ida_kernwin
    import ida_typeinf

    li.seek(0)
    text = li.read(li.size())
    if isinstance(text, bytes):
        text = text.decode('utf-8', 'replace')
    plan = parse(text.splitlines())
    if isinstance(plan, str):
        ida_kernwin.warning(f'ReAgentLive function not loaded: {plan}')
        return 0
    ida_idp.set_processor_type('metapc', ida_idp.SETPROC_LOADER)
    ida_ida.inf_set_64bit(True)
    # Windows x64 code: without this IDA assumes GNU C++ and the System V
    # calling convention, and the decompiler garbles the parameters.
    ida_typeinf.set_compiler_string('vc', True)
    for index, (start, data, kind) in enumerate(plan['segments']):
        segment = ida_segment.segment_t()
        segment.start_ea = start
        segment.end_ea = start + len(data)
        segment.bitness = 2
        segment.perm = ida_segment.SEGPERM_READ | (ida_segment.SEGPERM_EXEC if kind == 'CODE' else 0)
        ida_segment.add_segm_ex(segment, f'{kind.lower()}{index}', kind, ida_segment.ADDSEG_NOSREG)
        ida_bytes.put_bytes(start, data)
    function = plan['function']
    ida_funcs.add_func(function['start'])
    for address, name in plan['names']:
        ida_name.set_name(address, name, ida_name.SN_NOWARN | ida_name.SN_NOCHECK)
    ida_bytes.set_cmt(function['start'], f"reagentlive: {function['module']}+{function['start'] - function['base']:#x} "
                      f"(size {function['module_size']}, timestamp {function['timestamp']})", False)
    return 1
