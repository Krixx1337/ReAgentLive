"""A save (what `disasm`'s `save` wrote) read as a plan, and the segment arithmetic of adding one to a
database. Plain Python: no IDA."""
import json
import re

_ALLOWED = re.compile(r'[^A-Za-z0-9_:$?@.]')
_STALE = 'this ReAgentLive is older than the file: update the repo, then rl.reload() or restart IDA'


def sanitize(name):
    """A name IDA accepts: other characters become '_'."""
    return _ALLOWED.sub('_', name)


def parse(lines):
    """The saved file as a plan, or a string saying why it cannot be loaded.

    Plan: {'build': {module, module_size, timestamp, base}, 'functions': [(start, end)]
    in file order, 'chunks': [(start, end, function)], 'segments': [(start, bytes, 'CODE' | 'DATA')] sorted,
    'names': [(address, name, what)], 'stubs': [(address, what)] for names no
    segment holds, 'layouts': the layout rows as layout-export lines (JSON text),
    'console': {path, labels, names} (the console that wrote it), or None}.
    """
    build, functions, chunks, segments, names, layout_rows, console = None, [], [], [], [], [], None
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
            elif kind == 'console':
                console = {key: row[key] for key in ('path', 'labels', 'names')}
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
            'stubs': stubs(names, held), 'layouts': layout_rows, 'console': console}


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
