"""IDA's loader entry points (accept_file, load_file; the stub in IDA's loaders folder calls them) and
add_saved, which grows an open database by another save."""
import json

from .database import _CONSOLE_NODE, _NODE, _auto_name
from .layout_types import _declare, _type_this, layouts
from .save import additions, methods, parse, stubs

FORMAT = 'ReAgentLive saved function'
ACCEPT_FIRST = 0x8000  # ida_loader.ACCEPT_FIRST: offered before IDA's own "Binary file".


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
    _keep_console(plan)
    if plan['layouts']:
        # The types now; `this` once auto-analysis is done (no decompiler while loading).
        types = saved_types(plan, now=False)
        if types.get('error') or types['types_failed']:
            ida_kernwin.msg(f'ReAgentLive types: {json.dumps(types)}\n')
    return 1


def _keep_console(plan):
    """IDA: keep the save's console row in the database (the latest save's wins)."""
    import ida_netnode

    if plan['console']:
        ida_netnode.netnode(_CONSOLE_NODE, 0, True).supset(0, json.dumps(plan['console']))


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
    _keep_console(plan)
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
