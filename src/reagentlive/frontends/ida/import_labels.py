"""Import a ReAgentLive labels file (export-labels, peek --label) into an IDA database.

Run in IDA (File > Script file...), or call apply(path) from a session. It
asks for the labels file, checks that the database is the same build (image
size and PE timestamp, from the PE header IDA keeps), then names each
address in one undoable step, with a wait box you can cancel. A name the
user set is never replaced: the label becomes a comment. Exec thunks also
become functions. The planning below is plain Python, tested
without IDA (tests/python/test_ida_labels.py).
"""
import json
import re

_ALLOWED = re.compile(r'[^A-Za-z0-9_:$?@.]')


def sanitize(name):
    """A name IDA accepts: other characters become '_'."""
    return _ALLOWED.sub('_', name)


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


def check_build(rows, image_size, timestamp):
    """None when every row belongs to this image, else the reason it does not."""
    builds = {(row['module_size'], row['timestamp']) for row in rows}
    if not rows:
        return 'no labels in the file'
    if builds != {(image_size, timestamp)}:
        return f'labels are for another build {sorted(builds)}; this database is ({image_size}, {timestamp})'
    return None


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


def build_of(header):
    """(SizeOfImage, TimeDateStamp) from PE header bytes that start at 'PE\\0\\0', or None."""
    if not header or len(header) < 0x54 or header[:4] != b'PE\0\0':
        return None
    return int.from_bytes(header[0x50:0x54], 'little'), int.from_bytes(header[8:12], 'little')


def _database_build():
    """The open database's (SizeOfImage, TimeDateStamp). IDA keeps the PE header
    in the database's PE node even when its bytes were not loaded (the default)."""
    import ida_bytes
    import ida_nalt
    import idautils

    build = build_of(idautils.peutils_t().header())
    if build:
        return build
    base = ida_nalt.get_imagebase()  # A manual load with the header bytes in the database.
    if ida_bytes.is_loaded(base + 0x3C):
        pe = base + ida_bytes.get_dword(base + 0x3C)
        return build_of(ida_bytes.get_bytes(pe, 0x54))
    return None


def apply(path=None):
    """Import the labels file at `path` (asked for when None) into the open database.
    Returns (named, commented, skipped), or None when nothing was imported."""
    import ida_bytes
    import ida_funcs
    import ida_kernwin
    import ida_name
    import ida_nalt
    import ida_undo

    if path is None:
        path = ida_kernwin.ask_file(0, '*.jsonl', 'ReAgentLive labels file')
        if not path:
            return None
    with open(path, encoding='utf-8') as handle:
        rows = load(handle)
    build = _database_build()
    if not build:
        ida_kernwin.warning('ReAgentLive labels not imported: this database has no PE header')
        return None
    problem = check_build(rows, *build)
    if problem:
        ida_kernwin.warning(f'ReAgentLive labels not imported: {problem}')
        return None
    base = ida_nalt.get_imagebase()

    def existing(rva):
        ea = base + rva
        return ida_name.get_name(ea) or '', ida_bytes.has_user_name(ida_bytes.get_flags(ea))

    ida_undo.create_undo_point('ReAgentLive labels', 'ReAgentLive labels')  # One Undo reverts the import.
    actions = plan(rows, existing)
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
                if action[3] == 'exec' and not ida_funcs.get_func(ea):
                    ida_funcs.add_func(ea)
                if ida_name.set_name(ea, action[2], ida_name.SN_NOWARN | ida_name.SN_NOCHECK):
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
    print(f'ReAgentLive labels: {named} named, {commented} commented, {skipped} skipped (already there), '
          f'from {len(rows)} rows')
    return named, commented, skipped


if __name__ == '__main__':
    try:
        import idaapi  # noqa: F401
    except ImportError:
        raise SystemExit('Run this inside IDA (File > Script file...).')
    apply()
