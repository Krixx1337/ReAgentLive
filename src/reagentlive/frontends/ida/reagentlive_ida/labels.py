"""Names round trip: labels files in (import_labels), your names out (export_names)."""
import json
import re

from .database import _NODE, _auto_name
from .save import sanitize

# Names IDA or the loader make up: never exported as yours. An address name
# (sub_, a switch's def_ and jpt_ ...), a jump table, and the compiler's and
# runtime's own names: `__` helpers (__alloca_probe), `?` mangled and `$` names.
_DEFAULT_NAME = re.compile(
    r'^(fn|sub|stub|loc|locret|j|nullsub|unknown|off|seg|byte|word|dword|qword|xmmword|ymmword|tbyte|flt|dbl'
    r'|packreal|unk|asc|stru|algn|def|jpt|jumptable|funcs|str)_[0-9A-Fa-f]+(_\d+)?$'
    r'|^jumptable |^__|^[?$]')


def made_up(name):
    """True for a name IDA, the compiler or the runtime gave: never exported as yours."""
    return not name or bool(_DEFAULT_NAME.match(name))


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
            for rva, name in sorted(names) if not made_up(name)]


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


def labels_message(counts):
    """import_labels' line from its counts {named, commented, outside, already, rows, cancelled}:
    what was skipped and, when nothing was applied, why."""
    head = (f"ReAgentLive labels: {counts['named']} named, {counts['commented']} commented, "
            f"{counts['outside'] + counts['already']} skipped, from {counts['rows']} rows of this build")
    parts = []
    if counts['outside']:
        parts.append(f"{counts['outside']} outside the database's loaded bytes")
    if counts['already']:
        parts.append(f"{counts['already']} named or commented so already")
    if counts.get('cancelled'):
        parts.append('cancelled')
    if not parts:
        return head
    line = f"{head} ({', '.join(parts)})"
    if not counts['named'] and not counts['commented'] and counts['outside']:
        line += ': nothing to name here; a save database holds only its saved functions, so most rows fall outside it'
    return line


def import_labels(path=None):
    """Name the open database (a full one or a save) from a labels file, or a list of them (the
    console's derived names and the host's names file); asked for when None.
    Returns {named, commented, outside (rows at addresses the database holds no bytes for), already
    (named or commented so before), rows (this build's, one per address), cancelled}, or None when
    nothing was imported."""
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
    named = commented = outside = already = 0
    cancelled = False
    ida_kernwin.show_wait_box('ReAgentLive labels: importing')
    try:
        for index, action in enumerate(actions):
            if index % 500 == 0:
                if ida_kernwin.user_cancelled():
                    cancelled = True
                    break
                ida_kernwin.replace_wait_box(f'ReAgentLive labels: {index} of {len(actions)}')
            ea = base + action[1]
            if action[0] == 'skip':
                already += 1
                continue
            if not ida_bytes.is_loaded(ea):
                outside += 1  # Outside a save's bytes.
                continue
            if action[0] == 'name':
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
                already += 1  # Imported before.
                continue
            ida_bytes.set_cmt(ea, f'{old}\n{text}' if old else text, False)
            commented += 1
    finally:
        ida_kernwin.hide_wait_box()
    counts = {'named': named, 'commented': commented, 'outside': outside, 'already': already,
              'rows': len(actions), 'cancelled': cancelled}
    print(labels_message(counts))
    return counts


def export_names(path=None):
    """Write the names a person gave in this database into a labels file (asked for when None) as
    `user` rows of its build, adding or updating them; names it does not hold stay. Only names the
    person set: library names (a FLIRT function, a library item), auto-generated ones (IDA's
    switch and jump-table names, an address name) and names IDA rejects as a user's are IDA's,
    never exported. Returns how many."""
    import ida_bytes
    import ida_funcs
    import ida_kernwin
    import ida_nalt
    import ida_name
    import idautils

    def library(ea):
        function = ida_funcs.get_func(ea)
        return bool(function and function.flags & ida_funcs.FUNC_LIB) or ida_nalt.is_libitem(ea)

    def yours(ea, name):
        flags = ida_bytes.get_flags(ea)
        return (ida_bytes.has_user_name(flags) and not ida_bytes.has_auto_name(flags)
                and ida_name.is_uname(name) and not library(ea))

    if path is None:
        path = ida_kernwin.ask_file(1, '*.jsonl', 'ReAgentLive labels file to write your names into')
        if not path:
            return None
    build = _database_build()
    if not build:
        ida_kernwin.warning('ReAgentLive names not exported: this database has no PE header and is no save')
        return None
    names = [(ea - build['base'], name) for ea, name in idautils.Names() if ea >= build['base'] and yours(ea, name)]
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
