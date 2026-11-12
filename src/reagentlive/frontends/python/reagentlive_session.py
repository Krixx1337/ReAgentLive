"""ReAgentLive console session for scripts and agents.

Drives a ReAgentLive console executable through its stateful stdin mode: one
command line in, one JSON line out. Refs and `$N` results persist for the
session. Every method raises ReAgentLiveError when the console reports
`ok: false`.

    from reagentlive_session import Session, samples
    with Session(r'path\\to\\console.exe') as s:
        region = s.region('$1')                  # A Record: region['kind'], region['module'] (None outside a module)
        region['protection']                     # ResultKeyError: region has no key 'protection'; keys: ..., protect, ...
        rows = samples(s.watch(2, [('$1+0x3e4', 4)], then='call $2 Owner:SetAiming 01'))
        tick, items = s.read_batch_bytes([(0x1000, 8), (0x2000, 4)])
        alpha = s.get_values('$1', 'Mesh.AnimInstance.AimAlpha')
        hits = s.search_addresses('ptr', 0x7ff600001234, heap=True)
        s.write(hits[0] + 8, s.read_bytes(hits[0] + 8, 4), expect=...)

Results are strict. At start the session reads the console's verb table
(`help --json`) and wraps every result in a Record typed by its verb's row:
a key the result type lacks raises ResultKeyError and names the valid keys,
and an optional key that is absent gives None. So a key guessed wrong fails
on the line that guessed it, never as a quiet None or a caught KeyError.
`s.help(verb)` shows a verb's usage and result keys; `s.table_hash` tells a
cached script its table is stale.

A method named after a verb sends that verb and returns its result as the
console gave it. A method that reshapes says so in its name: `read_bytes`,
`search_addresses`, `find_all`, `get_values`, `name_map`.

Standard library only.
"""
import json
import struct
import subprocess
from collections.abc import Mapping

__all__ = ['ReAgentLiveError', 'ResultKeyError', 'Record', 'Session', 'samples', 'addr', 'unhex',
           'f32', 'f64', 'u32', 'u64', 'i32', 'pack']


class ReAgentLiveError(RuntimeError):
    """The console answered `ok: false`; `.error` holds its error object."""

    def __init__(self, command, error):
        self.command, self.error = command, error or {}
        super().__init__(f'{command}: {self.error.get("message", "failed")}')


class ResultKeyError(KeyError):
    """A result was asked for a key its type does not have."""

    def __str__(self):
        return self.args[0]


def f32(data, offset=0): return struct.unpack_from('<f', data, offset)[0]
def f64(data, offset=0): return struct.unpack_from('<d', data, offset)[0]
def u32(data, offset=0): return struct.unpack_from('<I', data, offset)[0]
def i32(data, offset=0): return struct.unpack_from('<i', data, offset)[0]
def u64(data, offset=0): return struct.unpack_from('<Q', data, offset)[0]


def pack(kind, value):
    """Bytes of one value: 'f32', 'f64', 'u32', 'i32', 'u64', 'ptr'."""
    formats = {'f32': '<f', 'f64': '<d', 'u32': '<I', 'i32': '<i', 'u64': '<Q', 'ptr': '<Q'}
    return struct.pack(formats[kind], value)


def unhex(text):
    """Bytes of a `bytes` value ("01 02 ff"); b'' for an empty one."""
    return bytes.fromhex(text.replace(' ', '')) if text else b''


def addr(text):
    """The int of an `addr` or `hex` value ("0x7ff6...")."""
    return int(text, 16)


def _address(value):
    return value if isinstance(value, str) else f'0x{value:x}'


def _quoted(text):
    """A token the console takes exactly: quoted when it holds a space, ';' or a quote."""
    text = str(text)
    if text and not any(c in text for c in ' \t;"') and not text.startswith('--'):
        return text
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def samples(result):
    """A watch result's rows with values as bytes (None while unreadable); lost and resumed rows keep their marks."""
    out = []
    for row in result['rows']:
        sample = {key: row[key] for key in ('tick', 'us', 'lost', 'resumed', 'root') if row[key] is not None}
        sample['values'] = [None if value is None else unhex(value) for value in row['values'] or []]
        out.append(sample)
    return out


class Record(Mapping):
    """One result object, read strictly against its type.

    record[key]: the value; None for an optional key that is absent; a key
    the type lacks raises ResultKeyError naming the valid ones. Iteration,
    len and `in` cover the keys present. `.raw` is the plain dict.
    """
    __slots__ = ('raw', '_fields', '_where')

    def __init__(self, raw, fields, where):
        self.raw, self._fields, self._where = raw, fields, where

    def _field(self, key):
        field = self._fields.get(key)
        if field is None:
            keys = ', '.join(name + ('?' if spec.get('optional') else '') for name, spec in self._fields.items())
            raise ResultKeyError(f"{self._where} has no key '{key}'; keys: {keys}")
        return field

    def __getitem__(self, key):
        field = self._field(key)
        if key not in self.raw:
            return None
        return _wrap(self.raw[key], field['type'], f'{self._where}.{key}')

    def get(self, key, default=None):
        value = self[key]
        return default if value is None else value

    def __contains__(self, key):
        self._field(key)
        return key in self.raw

    def __iter__(self):
        return iter(self.raw)

    def __len__(self):
        return len(self.raw)

    def __eq__(self, other):
        return self.raw == (other.raw if isinstance(other, Record) else other)

    def __repr__(self):
        return f'Record({self.raw!r})'


def _wrap(value, kind, where):
    if not isinstance(kind, dict) or value is None:
        return value
    if 'struct' in kind:
        return Record(value, kind['struct'], where) if isinstance(value, dict) else value
    if 'list' in kind:
        return [_wrap(item, kind['list'], f'{where}[{i}]') for i, item in enumerate(value)]
    if 'map' in kind:
        return {key: _wrap(item, kind['map'], f'{where}.{key}') for key, item in value.items()}
    return value


class Session:
    """One console process; not thread-safe. Use as a context manager."""

    def __init__(self, console, args=()):
        self._process = subprocess.Popen([console, *args], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self._types = {}
        table = self.send('help --json')
        if not table.get('ok'):
            raise ReAgentLiveError('help --json', table.get('error'))
        self.table_hash = table['result'].get('table_hash')
        for row in table['result']['rows']:
            for name in (row['verb'], *row.get('aliases', ())):
                self._types[name] = row['type']

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
        """The full JSON envelope for one command line, as plain dicts."""
        self._process.stdin.write(line + '\n')
        self._process.stdin.flush()
        while True:
            text = self._process.stdout.readline()
            if not text:
                raise ReAgentLiveError(line, {'message': 'console exited'})
            if text.lstrip().startswith('{'):
                return json.loads(text)

    def wrap(self, verb, result):
        """A verb's result object as a strict Record (a plain dict for a verb the table lacks)."""
        kind = self._types.get(verb)
        return _wrap(result, kind, verb) if kind else result

    def ok(self, line):
        """The line's result as a strict Record; raises ReAgentLiveError on failure."""
        envelope = self.send(line)
        if not envelope.get('ok'):
            raise ReAgentLiveError(line, envelope.get('error'))
        return self.wrap(line.split(None, 1)[0], envelope.get('result', {}))

    def help(self, verb=None):
        """Every verb in one line each, or one verb's flags, result notation and notes."""
        return self.ok('help' + (f' {verb}' if verb else ''))

    def status(self):
        return self.ok('status')

    # Memory.
    def read(self, address, length=None, as_=None):
        """address may carry a pointer chain ('$1->0x30->0x10'); as_: a type, cstr|wstr, or a layout."""
        line = f'read {_address(address)}' + (f' {length}' if length else '')
        if as_:
            line += ' --as ' + (f'"{as_}"' if ' ' in as_ or ',' in as_ else as_)
        return self.ok(line)

    def read_bytes(self, address, length):
        return unhex(self.read(address, length)['hex'])

    def read_struct(self, address, fmt):
        """struct.unpack of `fmt` (little-endian unless it says otherwise) read from address."""
        if fmt[:1] not in '<>!=@':
            fmt = '<' + fmt
        return struct.unpack(fmt, self.read_bytes(address, struct.calcsize(fmt)))

    def read_batch(self, spans):
        """Up to 8 (address, length) spans read in one host tick."""
        return self.ok('read-batch ' + ' '.join(f'{_address(a)} {n}' for a, n in spans))

    def read_batch_bytes(self, spans):
        """(tick, [bytes or None]) of read_batch."""
        result = self.read_batch(spans)
        return result['tick'], [unhex(row['hex']) if row['ok'] else None for row in result['rows']]

    def peek(self, address, count=8, label=None, name=None):
        """label: a labels file to append this one qword to (count must be 1); name defaults to FName_<hint>."""
        line = f'peek {_address(address)} {count}'
        if label:
            line += f' --label {_quoted(label)}' + (f' --name {name}' if name else '')
        return self.ok(line)

    def write(self, address, data, expect=None):
        """Writes bytes; with `expect`, only while memory still holds those bytes."""
        line = f'write {_address(address)} {bytes(data).hex()}'
        if expect is not None:
            line += f' --expect {bytes(expect).hex()}'
        return self.ok(line)

    def search(self, kind, value, max=None, from_=None, **options):
        """One page of addresses holding a value.

        kind: hex (value: bytes), u32, i32, u64, ptr, f32, f64, text. Options:
        heap, writable, paged, background, wide (bools); module, to, align.
        """
        if kind == 'hex':
            text = bytes(value).hex()
        elif kind in ('f32', 'f64'):
            text = repr(float(value))
        elif kind in ('ptr', 'u64') and isinstance(value, int):
            text = f'0x{value:x}'
        elif kind == 'text':
            text = _quoted(value)
        else:
            text = str(value)
        line = f'search {kind} {text}'
        line += ''.join(f' --{name}' for name in ('heap', 'writable', 'paged', 'background', 'wide') if options.get(name))
        for name in ('module', 'align'):
            if options.get(name) is not None:
                line += f' --{name} {options[name]}'
        if options.get('to') is not None:
            line += f' --to {_address(options["to"])}'
        if max is not None:
            line += f' --max {max}'
        if from_:
            line += f' --from {_address(from_)}'
        return self.ok(line)

    def search_addresses(self, kind, value, limit=None, from_=None, **options):
        """Every hit address as an int (every page, or the first `limit`)."""
        hits = []
        while True:
            page = 512 if limit is None else max(1, min(512, limit - len(hits)))
            result = self.search(kind, value, max=page, from_=from_, **options)
            hits += [addr(row['address']) for row in result['rows']]
            from_ = result['next_cursor']
            if not from_ or (limit is not None and len(hits) >= limit):
                return hits

    def batch(self, lines):
        """Runs command lines held in as few host ticks as possible.

        rows holds one plain envelope per command; batch_results() wraps them.
        A `$N` from an earlier find in the batch works; a value read in the
        batch cannot feed a later line.
        """
        return self.ok('batch { ' + '; '.join(lines) + ' }')

    def batch_results(self, lines):
        """(items, same_tick): each item is that command's strict result, or a ReAgentLiveError (not raised)."""
        result = self.batch(lines)
        items = [self.wrap(row.get('command', '?'), row.get('result', {})) if row.get('ok')
                 else ReAgentLiveError(row.get('command', '?'), row.get('error')) for row in result['rows']]
        return items, result['same_tick']

    def base(self):
        return self.ok('base')

    def modules(self):
        """Loaded modules, the exe first; also refreshes the console's list."""
        return self.ok('modules')

    # Signatures.
    def sig_check(self, pattern, module=None, expect=None, offset=0, resolve=None):
        """A byte signature in a module (default: the exe).

        pattern: "48 8b ?? 89" or "0x48, 0x8B, -1, 0x89" text, or a list of ints with None or -1 for wildcards.
        expect: where it should land (address, or "<module>+<rva>"); offset: the match lies that far past it.
        resolve: (next, field[, size]) for a reference signature; expect is then the target.
        """
        if not isinstance(pattern, str):
            pattern = ' '.join('??' if b is None or b == -1 else f'{b:02x}' for b in pattern)
        line = f'sig-check {pattern}' + (f' --module {module}' if module else '')
        if expect is not None:
            line += f' --expect {_address(expect)}' + (f' --offset {offset}' if offset else '')
        if resolve is not None:
            line += ' --resolve ' + ','.join(str(n) for n in resolve)
        return self.ok(line)

    def sig_file(self, path):
        """Checks a JSON-lines file of signatures; `winner` is the line of the first that holds."""
        return self.ok(f'sig-file {_quoted(path)}')

    def sig_bytes(self, address, length=64):
        """Live bytes to start a signature from, with address fields wildcarded in `suggested`."""
        return self.ok(f'sig-bytes {_address(address)} {length}')

    # The fast lane: one address, answered from live bytes.
    def disasm(self, address, count=24, function=False, follow=False):
        """function: list the exception-table function holding the address. follow: step through direct jumps."""
        line = f'disasm {_address(address)} {count}' + (' --function' if function else '') + (' --follow' if follow else '')
        return self.ok(line)

    def xrefs(self, address, module=None, all=False):
        """module: scan another loaded module. all: also list unknown hits (most likely chance bytes)."""
        return self.ok(f'xrefs {_address(address)}' + (f' --module {module}' if module else '') + (' --all' if all else ''))

    def touches(self, address, *offsets, range=None):
        """Functions within range bytes of address (default 0x80000) whose [reg + disp] operands use the offsets."""
        line = f'touches {_address(address)} ' + ' '.join(hex(o) if isinstance(o, int) else str(o) for o in offsets)
        return self.ok(line + (f' --range {hex(range) if isinstance(range, int) else range}' if range else ''))

    # Native objects.
    def vtable(self, address, count=None, offset=0, table=False, resolve=False):
        """offset: read the table pointer at address + offset (an interface part); table: address is the table."""
        line = f'vtable {_address(address)}' + (f' {count}' if count else '') + (f' --offset {offset}' if offset else '')
        return self.ok(line + (' --table' if table else '') + (' --resolve' if resolve else ''))

    def region(self, address):
        return self.ok(f'region {_address(address)}')

    # Pins and watches (agent-side, every tick).
    def pin(self, address, data, seconds=10, expect=None):
        """Freezes bytes for `seconds`; `$N+off` targets stop with their object."""
        line = f'pin {_address(address)} {bytes(data).hex()} --ms {int(seconds * 1000)}'
        if expect is not None:
            line += f' --expect {bytes(expect).hex()}'
        return self.ok(line)

    def pin_list(self):
        return self.ok('pin-list')

    def pin_clear(self, pin_id=None):
        """Clears one pin, or all; `cleared` says how many."""
        return self.ok('pin-clear' + (f' {pin_id}' if pin_id else ''))

    # Trials: one write the agent undoes on expiry or clear.
    def trial(self, address, data, owner, expect, seconds=10, object=None):
        """Writes `data` once at `address` inside live object `owner` (a ref); the agent restores
        `expect` (the bytes there now) after `seconds`, unless the game changed the value (conflict)
        or the owner or `object`'s vtable changed (abandoned)."""
        line = (f'trial {_address(address)} {bytes(data).hex()} --owner {owner}'
                + (f' --object {_address(object)}' if object is not None else '')
                + f' --expect {bytes(expect).hex()} --ms {int(seconds * 1000)}')
        return self.ok(line)

    def trial_list(self):
        return self.ok('trial-list')

    def trial_clear(self, trial_id=None):
        """Clears one trial or all, restoring active ones; rows holds their final state."""
        return self.ok('trial-clear' + (f' {trial_id}' if trial_id else ''))

    def watch(self, seconds, ranges, then=None):
        """Samples (address, length) ranges every tick; `then` is a call line run right after the start.

        samples(result) gives the rows with values as bytes.
        """
        line = f'watch {int(seconds * 1000)}ms ' + ' '.join(f'{_address(a)}:{n}' for a, n in ranges)
        return self.ok(line + (f' --then {then}' if then else ''))

    def watch_start(self, seconds, ranges, every=None, follow=None):
        """Starts a capture the agent keeps and returns at once: (address, length) ranges for `seconds`.

        every: seconds between samples (default every host tick, then at most 30 s).
        follow: a host root (status lists them); ranges are then (offset, length)
        from it, and the capture continues on its replacement.
        The capture outlives this console; watch_read() from any later session reads it.
        """
        line = f'watch-start {int(seconds * 1000)}ms ' + ' '.join(f'{_address(a)}:{n}' for a, n in ranges)
        line += f' --every {int(every * 1000)}ms' if every else ''
        return self.ok(line + (f' --follow {follow}' if follow else ''))

    def watch_read(self):
        """The running or last capture; samples(result) decodes its rows."""
        return self.ok('watch-read')

    def watch_stop(self):
        """Ends the capture early and returns it like watch_read()."""
        return self.ok('watch-stop')

    # Objects (the Unreal pack).
    def _pages(self, line, pages):
        """The rows of every page of a paged verb (`pages` of them; None: to the end)."""
        rows, cursor, read = [], 0, 0
        while True:
            result = self.ok(line + (f' --from {cursor}' if cursor else ''))
            rows += result['rows']
            cursor, read = result['next_cursor'], read + 1
            if not cursor or (pages is not None and read >= pages):
                return rows

    @staticmethod
    def _find_line(text, cls):
        return 'find' + (f' {text}' if text else '') + (f' --class {cls}' if cls else '')

    def find(self, text='', cls=None, from_=0):
        """One page (24) of objects whose name contains text and/or of class `cls`; its rows become $1..$N."""
        return self.ok(self._find_line(text, cls) + (f' --from {from_}' if from_ else ''))

    def find_all(self, text='', cls=None, pages=None):
        """The object rows of every page (or the first `pages`)."""
        return self._pages(self._find_line(text, cls), pages)

    @staticmethod
    def _census_line(verb, class_name, match, limit):
        return (verb + (f' --class {class_name}' if class_name else '') + (f' --match {match}' if match else '')
                + (f' --limit {limit}' if limit is not None else ''))

    def census(self, class_name=None, match=None, limit=None, from_=0):
        """One page of live instances per class. Every census becomes census_diff's baseline.

        `limit` is the rows per page (None: the console's 100, 0: all in one);
        from_ slices the stored census, no new scan.
        """
        return self.ok(self._census_line('census', class_name, match, limit) + (f' --from {from_}' if from_ else ''))

    def census_all(self, class_name=None, match=None, limit=None, pages=None):
        """The class rows of every page (or the first `pages`); the refs of the last page read become $1..$N."""
        return self._pages(self._census_line('census', class_name, match, limit), pages)

    def census_diff(self, class_name=None, match=None, limit=None, from_=0):
        """One page of the classes whose count differs from the last census under the same `class_name`."""
        return self.ok(self._census_line('census-diff', class_name, match, limit) + (f' --from {from_}' if from_ else ''))

    def census_diff_all(self, class_name=None, match=None, limit=None, pages=None):
        return self._pages(self._census_line('census-diff', class_name, match, limit), pages)

    def describe(self, ref):
        """An address, $N, literal ref or '#index:serial'."""
        return self.ok(f'describe {_address(ref)}')

    def fields(self, ref):
        return self.ok(f'fields {_address(ref)}')

    def dump(self, ref, match=None, nonzero=False, changed=None):
        """changed: True compares with this session's `snap` baseline, a path with a snapshot
        file from `snap(ref, file)`; only changed rows, each with `before`."""
        line = f'dump {_address(ref)}' + (f' --match {match}' if match else '') + (' --nonzero' if nonzero else '')
        if changed:
            line += ' --changed' + ('' if changed is True else f' {_quoted(changed)}')
        return self.ok(line)

    def label(self, ref, offset, type, name):
        """Names an unreflected member in the console's native file: label('$1', 0x538, 'f64[3]', 'Start')."""
        offset = hex(offset) if isinstance(offset, int) else offset
        return self.ok(f'label {_address(ref)} {offset} {type} {name}')

    def snap(self, ref, file=None):
        """Takes the baseline `dump(changed=...)` compares with. A file carries it to a later run."""
        return self.ok(f'snap {_address(ref)}' + (f' {_quoted(file)}' if file else ''))

    def record(self, ref, file, fields, seconds):
        """Samples 1..8 named fields for 1..10 `seconds` into a new tab-separated file; blocks for a 3 s countdown too."""
        return self.ok(f'record {_address(ref)} {int(seconds)} {_quoted(file)} ' + ' '.join(fields))

    def entries(self, ref, field, from_=0):
        """One page of an array, set or map field."""
        return self.ok(f'entries {_address(ref)} {field}' + (f' --from {from_}' if from_ else ''))

    def entries_all(self, ref, field, pages=None):
        return self._pages(f'entries {_address(ref)} {field}', pages)

    def get(self, ref, *paths):
        """Named fields ('Mesh.AnimInstance.AimAlpha'), following object pointers; one row per path."""
        return self.ok(f'get {_address(ref)} ' + ' '.join(paths))

    def get_values(self, ref, *paths):
        """The decoded values of get: one path returns its value, several a list. Raises if a path fails."""
        values = []
        for row in self.get(ref, *paths)['rows']:
            if not row['ok']:
                raise ReAgentLiveError(f'get {row["path"]}', {'message': row['error']})
            values.append(row['value'])
        return values[0] if len(paths) == 1 else values

    def name(self, indices):
        """FName comparison indices as text."""
        return self.ok('name ' + ' '.join(str(i) for i in indices))

    def name_map(self, indices):
        """{index: text or None} of name."""
        indices = list(indices)
        return {row['index']: row['name'] for row in self.name(indices)['rows']} if indices else {}

    def name_find(self, text, from_=0):
        """One page (64) of pooled names containing text."""
        return self.ok(f'name-find {text}' + (f' --from {from_}' if from_ else ''))

    def name_find_all(self, text, pages=None):
        return self._pages(f'name-find {text}', pages)

    def refs(self, name):
        """Functions whose bytecode names `name` (exact pooled text, or an int index)."""
        return self.ok(f'refs #{name}' if isinstance(name, int) else f'refs {name}')

    def script(self, ref, function):
        """A Blueprint function's bytecode: the names, functions and properties it uses."""
        return self.ok(f'script {_address(ref)} {function}')

    def each(self, cls, target, seconds=None):
        """One value per live instance of `cls`; the refs become $1..$N.

        target: a field or [Owner:]Function (const or pure, outputs only). With
        `seconds` (1..60) it polls every 100 ms and adds polls and changes.
        Check `capped` (more than 1024 instances) and `poll_error`.
        """
        return self.ok(f'each {cls} {target}' + (f' --seconds {int(seconds)}' if seconds else ''))

    def funcs(self, ref, text=None, from_=0):
        """One page of functions on the class chain."""
        return self.ok(f'funcs {_address(ref)}' + (f' {text}' if text else '') + (f' --from {from_}' if from_ else ''))

    def funcs_all(self, ref, text=None, pages=None):
        return self._pages(f'funcs {_address(ref)}' + (f' {text}' if text else ''), pages)

    def func(self, ref, function):
        return self.ok(f'func {_address(ref)} {function}')

    def params(self, ref, function):
        return self.ok(f'params {_address(ref)} {function}')

    # Calls.
    def call(self, ref, function, params=b'', args=None):
        """Raw form: `params` is the parameter block; `hex` in the result is the block after the call.

        No `params` for a function whose parameters are all outputs: the
        console sends a zeroed block of its size.

        With `args` ({Name: value}; FNames as '#index[:number]', bytes and enums as
        ints; {} for a call with no inputs) it sends the typed form, which adds `outputs`.
        """
        if args is None:
            return self.ok(f'call {_address(ref)} {function} {bytes(params).hex()}'.rstrip())
        named = ' '.join(f'{name}={value}' for name, value in args.items()) or '--typed'
        return self.ok(f'call {_address(ref)} {function} {named}')

    def call_bytes(self, ref, function, params=b''):
        """The parameter block after a raw call (outputs included), as bytes."""
        return unhex(self.call(ref, function, params)['hex'])

    def release(self, ref, function, params=b''):
        """Drops the block the agent kept for `function` (a call result shows `kept: true`).

        Needed when a call returns or outputs something that owns memory (a
        string, array or weak handle): the agent keeps up to 8 such blocks and
        refuses a new function once full.
        """
        return self.ok(f'release {_address(ref)} {function} {bytes(params).hex()}'.rstrip())

    def export_labels(self, file, module=None, kinds=None):
        """Rewrites `file` with labels for a static tool.

        kinds: any of 'exec', 'vtable', 'interface', 'global' (list or comma text); default all.
        """
        if kinds is not None and not isinstance(kinds, str):
            kinds = ','.join(kinds)
        return self.ok(f'export-labels {_quoted(file)}' + (f' --module {module}' if module else '')
                       + (f' --kinds {kinds}' if kinds else ''))
