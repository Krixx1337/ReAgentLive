"""ReAgentLive console session for scripts and agents.

Drives a ReAgentLive console executable through its stateful stdin mode: one
command line in, one JSON line out. Refs and `$N` results persist for the
session. Every method raises ReAgentLiveError when the console reports
`ok: false`.

    from reagentlive_session import Session
    with Session(r'path\\to\\console.exe') as s:
        samples, _ = s.watch(2, [('$1+0x3e4', 4)], then='call $2 Owner:SetAiming 01')
        tick, items = s.read_batch([(0x1000, 8), (0x2000, 4)])
        alpha = s.get('$1', 'Mesh.AnimInstance.AimAlpha')
        items, same_tick = s.batch(['find Pawn', 'get $1 Health', 'read $1+0x3e4 4'])
        hits = s.search('ptr', 0x7ff600001234, heap=True)
        s.write(hits[0] + 8, s.read(hits[0] + 8, 4), expect=...)

Standard library only. Values are returned decoded; addresses are ints.
"""
import json
import struct
import subprocess

__all__ = ['ReAgentLiveError', 'Session', 'f32', 'f64', 'u32', 'u64', 'i32', 'pack']


class ReAgentLiveError(RuntimeError):
    """The console answered `ok: false`; `.error` holds its error object."""

    def __init__(self, command, error):
        self.command, self.error = command, error or {}
        super().__init__(f'{command}: {self.error.get("message", "failed")}')


def f32(data, offset=0): return struct.unpack_from('<f', data, offset)[0]
def f64(data, offset=0): return struct.unpack_from('<d', data, offset)[0]
def u32(data, offset=0): return struct.unpack_from('<I', data, offset)[0]
def i32(data, offset=0): return struct.unpack_from('<i', data, offset)[0]
def u64(data, offset=0): return struct.unpack_from('<Q', data, offset)[0]


def pack(kind, value):
    """Bytes of one value: 'f32', 'f64', 'u32', 'i32', 'u64', 'ptr'."""
    formats = {'f32': '<f', 'f64': '<d', 'u32': '<I', 'i32': '<i', 'u64': '<Q', 'ptr': '<Q'}
    return struct.pack(formats[kind], value)


def _hex(text):
    return bytes.fromhex(text.replace(' ', '')) if text else b''


def _address(value):
    return value if isinstance(value, str) else f'0x{value:x}'


def _samples(result):
    """Watch rows with values as bytes (None while unreadable); lost and resumed rows keep their marks."""
    samples = []
    for row in result['samples']:
        sample = {key: row[key] for key in ('tick', 'us', 'lost', 'resumed', 'root') if key in row}
        sample['values'] = [None if value is None else _hex(value) for value in row.get('values', [])]
        samples.append(sample)
    return samples


class Session:
    """One console process; not thread-safe. Use as a context manager."""

    def __init__(self, console, args=()):
        self._process = subprocess.Popen([console, *args], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=subprocess.DEVNULL, text=True, bufsize=1)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        if self._process.poll() is not None:
            return
        try:
            self._process.stdin.write('quit\n')
            self._process.stdin.flush()
            self._process.wait(2)
        except (OSError, subprocess.TimeoutExpired):
            self._process.kill()
            self._process.wait()
        finally:
            for stream in (self._process.stdin, self._process.stdout):
                try:
                    stream.close()
                except OSError:
                    pass

    # Raw exchange.
    def send(self, line):
        """The full JSON envelope for one command line."""
        self._process.stdin.write(line + '\n')
        self._process.stdin.flush()
        while True:
            text = self._process.stdout.readline()
            if not text:
                raise ReAgentLiveError(line, {'message': 'console exited'})
            if text.lstrip().startswith('{'):
                return json.loads(text)

    def ok(self, line):
        """The `result` object; raises ReAgentLiveError on failure."""
        envelope = self.send(line)
        if not envelope.get('ok'):
            raise ReAgentLiveError(line, envelope.get('error'))
        return envelope.get('result', {})

    # Memory.
    def read(self, address, length):
        return _hex(self.ok(f'read {_address(address)} {length}')['hex'])

    def read_chain(self, address_text, length):
        """(bytes, chain) read at the end of a pointer chain: '$1->0x30->0x10' or a base and offsets.

        chain: {base, hops[{hop, address, value}], address}; a null or unreadable hop raises ReAgentLiveError.
        """
        result = self.ok(f'read {address_text} {length}')
        return _hex(result['hex']), result.get('chain')

    def read_str(self, address, wide=False, length=None):
        """{text, length, terminated}: a C string (or UTF-16 with wide) read at address (default 256 bytes)."""
        line = f'read {_address(address)}' + (f' {length}' if length else '') + (' --as wstr' if wide else ' --as cstr')
        result = self.ok(line)
        return {key: result[key] for key in ('text', 'length', 'terminated')}

    def entries(self, ref, field, pages=None):
        """Rows ({value} or {key, value}, plus *_object) of an array, set or map field, paging to the end."""
        rows, cursor, page = [], 0, 0
        while True:
            result = self.ok(f'entries {_address(ref)} {field}' + (f' --from {cursor}' if cursor else ''))
            rows += result['entries']
            cursor, page = result['next_cursor'], page + 1
            if not cursor or (pages is not None and page >= pages):
                return rows

    def peek(self, address, count=8, label=None, name=None):
        """Rows {address, value, label: object|module|heap|value|unreadable, ...} for count qwords.

        label: a labels file to append this one qword to (count must be 1); name defaults to FName_<hint>.
        """
        line = f'peek {_address(address)} {count}'
        if label:
            line += f' --label {label}' + (f' {name}' if name else '')
        return self.ok(line)['rows']

    def read_layout(self, address, layout):
        """{name: value} of a block read with a typed layout: 'u8 phase, pad 3, f32 duration, f64[3] start'.

        Arrays decode to lists, unnamed fields are keyed by offset ('+0x8'), NaN and null pointers are None.
        """
        return self.ok(f'read {_address(address)} --as "{layout}"')['values']

    def read_struct(self, address, fmt):
        """struct.unpack of `fmt` (little-endian unless it says otherwise) read from address."""
        if fmt[:1] not in '<>!=@':
            fmt = '<' + fmt
        return struct.unpack(fmt, self.read(address, struct.calcsize(fmt)))

    def get(self, ref, *paths):
        """Decoded values of named fields ('Mesh.AnimInstance.AimAlpha'), following object pointers.

        One path returns its value; several return a list. Paths run held in
        as few host ticks as the agent allows. Raises ReAgentLiveError if a path fails.
        """
        result = self.ok(f'get {_address(ref)} ' + ' '.join(paths))
        values = []
        for row in result['values']:
            if not row['ok']:
                raise ReAgentLiveError(f'get {row["path"]}', {'message': row['error']})
            values.append(row['value'])
        return values[0] if len(paths) == 1 else values

    def batch(self, lines):
        """Runs command lines held in as few host ticks as possible.

        Returns (items, same_tick): each item is that command's result dict, or a
        ReAgentLiveError (not raised) when it failed. When same_tick is False,
        send() the batch line and read `spans_ticks` for the command that moved it. A `$N` from an earlier find in
        the batch works; a value read in the batch cannot feed a later line.
        """
        envelope = self.send('batch { ' + '; '.join(lines) + ' }')
        if not envelope.get('ok'):
            raise ReAgentLiveError('batch', envelope.get('error'))
        result = envelope['result']
        items = [item.get('result', {}) if item.get('ok') else ReAgentLiveError(item.get('command', '?'), item.get('error'))
                 for item in result['results']]
        return items, result['same_tick']

    def read_batch(self, spans):
        """(tick, [bytes or None]) for up to 8 (address, length) spans read in one host tick."""
        result = self.ok('read-batch ' + ' '.join(f'{_address(a)} {n}' for a, n in spans))
        return result['tick'], [_hex(item['hex']) if item['ok'] else None for item in result['items']]

    def write(self, address, data, expect=None):
        """Writes bytes; with `expect`, only while memory still holds those bytes."""
        line = f'write {_address(address)} {bytes(data).hex()}'
        if expect is not None:
            line += f' --expect {bytes(expect).hex()}'
        self.ok(line)

    def search(self, kind, value, limit=None, **options):
        """All hit addresses (every page, or the first `limit`).

        kind: hex (value: bytes), u32, i32, u64, ptr, f32, f64. Options:
        heap, writable, paged, background (bools); module, start, end, align.
        """
        if kind == 'hex':
            text = bytes(value).hex()
        elif kind in ('f32', 'f64'):
            text = repr(float(value))
        elif kind in ('ptr', 'u64') and isinstance(value, int):
            text = f'0x{value:x}'
        else:
            text = str(value)
        flags = ''.join(f' --{name}' for name in ('heap', 'writable', 'paged', 'background') if options.get(name))
        for name, flag in (('module', '--module'), ('align', '--align')):
            if options.get(name) is not None:
                flags += f' {flag} {options[name]}'
        if options.get('end') is not None:
            flags += f' --to {_address(options["end"])}'
        hits, start = [], options.get('start')
        while True:
            page = 512 if limit is None else max(1, min(512, limit - len(hits)))
            line = f'search {kind} {text}{flags} --max {page}' + (f' --from {_address(start)}' if start else '')
            result = self.ok(line)
            hits += [int(h['address'], 16) for h in result['hits']]
            if not result['resume'] or (limit is not None and len(hits) >= limit):
                return hits
            start = int(result['resume'], 16)

    # Signatures.
    def sig_check(self, pattern, module=None, expect=None, offset=0, resolve=None):
        """{unique, count, hits, warnings, ...} for a byte signature in a module (default: the exe).

        pattern: "48 8b ?? 89" or "0x48, 0x8B, -1, 0x89" text, or a list of ints with None or -1 for wildcards.
        expect: where it should land (address, or "<module>+<rva>"); offset: the match lies that far past it.
        resolve: (next, field[, size]) for a reference signature; expect is then the target.
        """
        if not isinstance(pattern, str):
            pattern = ' '.join('??' if b is None or b == -1 else f'{b:02x}' for b in pattern)
        line = f'sig --check {pattern}' + (f' --module {module}' if module else '')
        if expect is not None:
            line += f' --expect {_address(expect)}' + (f' --offset {offset}' if offset else '')
        if resolve is not None:
            line += ' --resolve ' + ','.join(str(n) for n in resolve)
        return self.ok(line)

    def sig_check_file(self, path):
        """{count, winner (line number or None), searches, results[{line, ok, result or error}]} for a JSON-lines file."""
        return self.ok(f'sig --check --file {path}')

    def sig_bytes(self, address, length=64):
        """{pattern, list, module, rva, suggested{pattern, list, fields}}: live bytes to start a signature from."""
        return self.ok(f'sig --bytes {_address(address)} {length}')

    # The fast lane: one address, answered from live bytes.
    def disasm(self, address, count=24, function=False, follow=False):
        """Instruction rows {address, module, rva, bytes, text, target, string, fields, ends} from live bytes.

        function: list the exception-table function holding the address, from its start to its end.
        follow: continue through up to 3 unconditional direct jumps (stubs).
        """
        line = f'disasm {_address(address)} {count}' + (' --function' if function else '') + (' --follow' if follow else '')
        return self.ok(line)['instructions']

    def xrefs(self, address, module=None, all=False):
        """{count (per kind), rows[{kind, field, instruction, function, module, rva}], truncated, scan_ms}.

        module: scan another loaded module (default: the one holding the address).
        all: also list unknown hits (most likely chance bytes).
        """
        line = f'xrefs {_address(address)}' + (f' --module {module}' if module else '') + (' --all' if all else '')
        return self.ok(line)

    # Native objects.
    def touches(self, address, *offsets, range=None):
        """{offsets, scanned, count, rows[{function, distinct, writes, count, uses[{offset, instruction, rva, write, text}]}]}.

        Functions within range bytes of address (default 0x80000) whose [reg + disp] operands use the offsets.
        """
        line = f'touches {_address(address)} ' + ' '.join(hex(o) if isinstance(o, int) else str(o) for o in offsets)
        return self.ok(line + (f' --range {hex(range) if isinstance(range, int) else range}' if range else ''))

    def vtable(self, address, count=None, offset=0, table=False, resolve=False):
        """{table, table_module, table_rva, stop, slots[{slot, offset, target, module, rva, body}]} or table None.

        offset: read the table pointer at address + offset (an interface part); table: address is the table.
        """
        line = f'vtable {_address(address)}' + (f' {count}' if count else '') + (f' --offset {offset}' if offset else '')
        line += (' --table' if table else '') + (' --resolve' if resolve else '')
        return self.ok(line)

    def region(self, address):
        """{kind: code|rodata|data|mapped|private|reserved|free, base, size, state, type, protect, module, rva, section}."""
        return self.ok(f'region {_address(address)}')

    # Pins and watches (agent-side, every tick).
    def pin(self, address, data, seconds=10, expect=None):
        """Freezes bytes for `seconds`; `$N+off` targets stop with their object. Returns the pin."""
        line = f'pin {_address(address)} {bytes(data).hex()} --ms {int(seconds * 1000)}'
        if expect is not None:
            line += f' --expect {bytes(expect).hex()}'
        return self.ok(line)

    def pins(self):
        return self.ok('pin list')['pins']

    def unpin(self, pin_id=None):
        """Clears one pin, or all; returns how many."""
        return self.ok('pin clear' + (f' {pin_id}' if pin_id else ''))['cleared']

    # Trials: one write the agent undoes on expiry or clear.
    def trial(self, address, data, owner, expect, seconds=10, object=None):
        """Writes `data` once at `address` inside live object `owner` (a ref); the agent restores
        `expect` (the bytes there now) after `seconds`, unless the game changed the value (conflict)
        or the owner or `object`'s vtable changed (abandoned). Returns the trial row."""
        line = (f'write {_address(address)} {bytes(data).hex()} --trial --owner {owner}'
                + (f' --object {_address(object)}' if object is not None else '')
                + f' --expect {bytes(expect).hex()} --ms {int(seconds * 1000)}')
        return self.ok(line)

    def trials(self):
        """Trial rows with state: active, restored, conflict or abandoned."""
        return self.ok('write --trial list')['trials']

    def end_trial(self, trial_id=None):
        """Clears one trial or all, restoring active ones; returns their final rows."""
        return self.ok('write --trial clear' + (f' {trial_id}' if trial_id else ''))['trials']

    def watch(self, seconds, ranges, then=None):
        """Samples (address, length) ranges every tick; `then` is a call line run right after the start.

        Returns (samples, result): samples are dicts with tick, us and values
        (bytes, or None while unreadable or after the object changed).
        """
        duration = f'{int(seconds * 1000)}ms'
        line = f'watch {duration} ' + ' '.join(f'{_address(a)}:{n}' for a, n in ranges)
        if then:
            line += f' --then {then}'
        result = self.ok(line)
        return _samples(result), result

    def watch_start(self, seconds, ranges, every=None, follow=None):
        """Starts a capture the agent keeps and returns at once: (address, length) ranges for `seconds`.

        every: seconds between samples (default every host tick, then at most 30 s).
        follow: a host root (status lists them); ranges are then (offset, length)
        from it, and the capture continues on its replacement.
        The capture outlives this console; watch_read() from any later session reads it.
        """
        line = f'watch --start {int(seconds * 1000)}ms' + (f' --every {int(every * 1000)}ms' if every else '')
        line += f' --follow {follow}' if follow else ''
        line += ' ' + ' '.join(f'{_address(a)}:{n}' for a, n in ranges)
        return self.ok(line)

    def watch_read(self):
        """(samples, result) of the running or last capture: samples as watch() returns them,
        plus `lost` / `resumed` (with `root`) when following; result has running, stopped, gaps."""
        result = self.ok('watch --read')
        return _samples(result), result

    def watch_stop(self):
        """Ends the capture early; returns (samples, result) like watch_read()."""
        result = self.ok('watch --stop')
        return _samples(result), result

    # Objects. Each returns the documented result rows (docs/commands/results.md,
    # "Result shapes"); refs are the reusable 0x<address>#<index>:<serial> text.
    def find(self, text='', cls=None, pages=1):
        """Objects ({ref, name, class}) whose name contains text and/or of class `cls`.

        Reads `pages` pages of 24 (None: all). Their refs also become $1..$N
        for the last page read.
        """
        objects, start, read = [], 0, 0
        while True:
            line = 'find' + (f' {text}' if text else '') + (f' --class {cls}' if cls else '')
            result = self.ok(line + (f' --from {start}' if start else ''))
            objects += result['objects']
            read += 1
            start = result['next_cursor']
            if not start or (pages is not None and read >= pages):
                return objects

    def census(self, class_name=None, match=None, changed=False, limit=None, pages=None):
        """Census rows: {class, ref, count, samples}, or with `changed` {class, ref, before, after, samples}.

        `changed` compares with the last census under the same `class_name`
        (refused without one). Every census becomes the next baseline.
        `limit` is the rows per page (None: the console's 100, 0: all in one).
        Reads `pages` pages (None: to the end); later pages slice the stored
        census with `--from`, no new scan. The refs of the last page read
        become $1..$N.
        """
        line = ('census' + (f' --class {class_name}' if class_name else '') + (f' --match {match}' if match else '')
                + (' --changed' if changed else '') + (f' --limit {limit}' if limit is not None else ''))
        rows, cursor, read = [], 0, 0
        while True:
            result = self.ok(line + (f' --from {cursor}' if cursor else ''))
            rows += result['rows']
            cursor, read = result.get('next_cursor'), read + 1
            if not cursor or (pages is not None and read >= pages):
                return rows

    def describe(self, ref):
        """{ref, name, class} of an address, $N or literal ref."""
        return self.ok(f'describe {_address(ref)}')

    def fields(self, ref):
        """Field layout rows ({name, owner, type, offset, size, label, ...}) of an object, class or struct."""
        return self.ok(f'fields {_address(ref)}')['fields']

    def dump(self, ref, match=None, nonzero=False, changed=None):
        """Field rows with `value` (and `object`, `preview` where they apply).

        changed: True compares with this session's `snap` baseline, a path with
        a snapshot file from `snap(ref, file)`; only changed rows, each with `before`.
        """
        line = f'dump {_address(ref)}' + (f' --match {match}' if match else '') + (' --nonzero' if nonzero else '')
        if changed:
            line += ' --changed' + ('' if changed is True else f' {changed}')
        return self.ok(line)['fields']

    def label(self, ref, offset, type, name):
        """Names an unreflected member in the console's native file: label('$1', 0x538, 'f64[3]', 'Start').

        fields, dump and get then show it as a native field. Returns {path, line, owner, offset, type, name, ...}.
        """
        offset = hex(offset) if isinstance(offset, int) else offset
        return self.ok(f'label {_address(ref)} {offset} {type} {name}')

    def snap(self, ref, file=None):
        """Takes the baseline `dump(changed=...)` compares with; {ref, size, path}.

        The session baseline lasts until the console exits. A file carries it
        to a later run, which starts without one.
        """
        return self.ok(f'snap {_address(ref)}' + (f' {file}' if file else ''))

    def record(self, ref, file, fields, seconds):
        """Samples 1..8 named fields for 1..10 `seconds` into a new tab-separated file.

        Blocks for a 3 s countdown plus `seconds`. Returns (path, samples).
        """
        result = self.ok(f'record {_address(ref)} {int(seconds)} {file} ' + ' '.join(fields))
        return result['path'], result['samples']

    def name_find(self, text, pages=1):
        """Pooled names ({index, name}) containing text; `pages` pages of 64 (None: all)."""
        rows, cursor, read = [], 0, 0
        while True:
            result = self.ok(f'name-find {text}' + (f' --from {cursor}' if cursor else ''))
            rows += result['names']
            cursor, read = result['next_cursor'], read + 1
            if not cursor or (pages is not None and read >= pages):
                return rows

    def refs(self, name):
        """Functions whose bytecode names `name` (exact pooled text, or an int index).

        Returns the whole result, {index, name, functions[{ref, owner, name, offset, count}], capped},
        since `capped` (more than 512 functions) changes what the list means.
        """
        return self.ok(f'refs #{name}' if isinstance(name, int) else f'refs {name}')

    def script(self, ref, function):
        """{ref, function, size, names, functions, properties} from a Blueprint function's bytecode."""
        return self.ok(f'func {_address(ref)} {function} --script')

    def each(self, cls, target, seconds=None):
        """One value per live instance of `cls`: rows {ref, value} (structs as {Name: value}). The refs become $1..$N.

        target: a field or [Owner:]Function (const or pure, outputs only).
        With `seconds` (1..60) it polls every 100 ms and returns (rows, changes),
        changes being {ms, tick, ref, before, after}.
        Raises ReAgentLiveError when the instance list was capped at 1024 or a poll
        failed; `.error['result']` then holds the whole result, rows included.
        """
        result = self.ok(f'each {cls} {target}' + (f' --seconds {int(seconds)}' if seconds else ''))
        if result['capped'] or result.get('poll_error'):
            message = 'capped at 1024 instances' if result['capped'] else f'poll failed: {result["poll_error"]}'
            raise ReAgentLiveError(f'each {cls} {target}', {'message': message, 'result': result})
        return (result['rows'], result['changes']) if seconds else result['rows']

    def funcs(self, ref, text=None):
        """Every function ({owner, name}) on the class chain, all pages."""
        functions, start = [], 0
        while True:
            line = f'funcs {_address(ref)}' + (f' {text}' if text else '') + (f' --from {start}' if start else '')
            result = self.ok(line)
            functions += result['functions']
            start = result['next_cursor']
            if not start:
                return functions

    def func(self, ref, function):
        """{function, address, flags, exec, executable, module, rva} of 'Owner:Name' or 'Name'."""
        return self.ok(f'func {_address(ref)} {function}')

    def params(self, ref, function):
        """{function, address, block_size, parameters[{name, type, offset, size, direction, flags}]}."""
        return self.ok(f'params {_address(ref)} {function}')

    # Calls.
    def call(self, ref, function, params=b'', args=None):
        """Raw form: the parameter block after the call (outputs included), as bytes.

        No `params` for a function whose parameters are all outputs: the
        console sends a zeroed block of its size.

        With `args` ({Name: value}; FNames as '#index[:number]', bytes and enums as
        ints; {} for a call with no inputs) it sends the typed form and returns the
        result object: {ref, function, mode, block_size, hex, kept, outputs, truncated}.
        """
        if args is None:
            return _hex(self.ok(f'call {_address(ref)} {function} {bytes(params).hex()}'.rstrip())['hex'])
        named = ' '.join(f'{name}={value}' for name, value in args.items()) or '--typed'
        return self.ok(f'call {_address(ref)} {function} {named}')

    def release(self, ref, function, params=b''):
        """Drops the block the agent kept for `function` (a call result shows `kept: true`).

        Needed when a call returns or outputs something that owns memory (a
        string, array or weak handle): the agent keeps up to 8 such blocks and
        refuses a new function once full. Returns the result object.
        """
        return self.ok(f'release {_address(ref)} {function} {bytes(params).hex()}'.rstrip())

    def names(self, indices):
        """{index: text or None} for FName comparison indices."""
        indices = list(indices)
        rows = self.ok('name ' + ' '.join(str(i) for i in indices))['names'] if indices else []
        return {row['index']: row['name'] for row in rows}

    def modules(self):
        """Loaded modules ({name, base, size}), the exe first; also refreshes the console's list."""
        return self.ok('modules')['modules']

    def export_labels(self, file, module=None, kinds=None):
        """Rewrites `file` with labels for a static tool; returns the summary ({path, rows, ...}).

        kinds: any of 'exec', 'vtable', 'interface', 'global' (list or comma text); default all.
        """
        if kinds is not None and not isinstance(kinds, str):
            kinds = ','.join(kinds)
        return self.ok(f'export-labels {file}' + (f' --module {module}' if module else '')
                       + (f' --kinds {kinds}' if kinds else ''))

    def status(self):
        return self.ok('status')
