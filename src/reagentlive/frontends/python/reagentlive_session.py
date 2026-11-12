"""ReAgentLive console session for scripts and agents.

Drives a ReAgentLive console executable through its stateful stdin mode: one
call in, one JSON line out. Calls go as JSON ({"verb": ..., "args": {...}}),
so nothing is quoted or escaped, and they carry literal refs (the `ref` a
result printed), never `$N`. Every call raises ReAgentLiveError when the
console reports `ok: false`.

The functions to call are generated per console, from its own verb table,
by reagentlive_gen.py: one function per verb with typed arguments and a
typed result, a host's verbs included.

    python reagentlive_gen.py --console <console.exe> -o my_console.py

    from reagentlive_session import Session, samples
    from my_console import Console
    with Session(r'path\\to\\console.exe') as s:
        c = Console(s)                       # Refuses a module generated from another table.
        pawn = c.ue.find('Pawn', cls='Pawn')['rows'][0]['ref']
        region = c.region(0x7ff600001000)    # A Record: region['kind'], region['module'] (None outside a module)
        region['protection']                 # ResultKeyError: region has no key 'protection'; keys: ..., protect, ...
        tick, items = s.read_batch_bytes([(0x1000, 8), (0x2000, 4)])
        alpha = s.get_values(pawn, 'Mesh.AnimInstance.AimAlpha')

Results are strict. At start the session reads the console's verb table
(`help --json`) and wraps every result in a Record typed by its verb's row:
a key the result type lacks raises ResultKeyError and names the valid keys,
and an optional key that is absent gives None. So a key guessed wrong fails
on the line that guessed it, never as a quiet None or a caught KeyError.
`s.help(verb)` shows a verb's arguments and result keys; `s.table_hash`
tells a generated module or cached script its table is stale.

`s.call(verb, args)` sends any verb by name; a generated function does the
same with checked argument names. The helpers here convert a result and
say so in their name (`read_bytes`, `get_values`, `name_map`, ...); paging,
filtering and retrying are the console's (flags and cursors), not Python's.

Standard library only.
"""
import json
import struct
import subprocess
from collections.abc import Mapping

__all__ = ['ReAgentLiveError', 'ResultKeyError', 'StaleTableError', 'Record', 'Session', 'samples', 'addr', 'unhex',
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


class StaleTableError(RuntimeError):
    """A generated module was made from another verb table than the console's: generate it again."""


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


def samples(result):
    """A watch result's rows with values as bytes (None while unreadable); lost and resumed rows keep their marks."""
    out = []
    for row in result['rows']:
        sample = {key: row[key] for key in ('tick', 'us', 'lost', 'resumed', 'root') if row[key] is not None}
        sample['values'] = [None if value is None else unhex(value) for value in row['values'] or []]
        out.append(sample)
    return out


def _value(value):
    """A call argument as JSON: bytes as hex pairs, tuples as lists, None keys left out; ints stay ints (an addr reads them as hex)."""
    if isinstance(value, (bytes, bytearray)):
        return bytes(value).hex()
    if isinstance(value, (list, tuple)):
        return [_value(item) for item in value]
    if isinstance(value, Mapping):
        return {key: _value(item) for key, item in value.items() if item is not None}
    return value


class Record(Mapping):
    """One result object, read strictly against its type.

    record[key]: the value; None for an optional key that is absent; a key
    the type lacks raises ResultKeyError naming the valid ones. Iteration,
    len and `in` cover the keys present. `.raw` is the plain dict.
    """
    __slots__ = ('raw', '_fields', '_where', '_named')

    def __init__(self, raw, fields, where, named=None):
        self.raw, self._fields, self._where, self._named = raw, fields, where, named or {}

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
        return _wrap(self.raw[key], field['type'], f'{self._where}.{key}', self._named)

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


def _wrap(value, kind, where, named):
    if not isinstance(kind, dict) or value is None:
        return value
    if 'named' in kind:
        return _wrap(value, named.get(kind['named'], {}).get('type'), where, named)
    if 'struct' in kind:
        return Record(value, kind['struct'], where, named) if isinstance(value, dict) else value
    if 'list' in kind:
        return [_wrap(item, kind['list'], f'{where}[{i}]', named) for i, item in enumerate(value)]
    if 'map' in kind:
        return {key: _wrap(item, kind['map'], f'{where}.{key}', named) for key, item in value.items()}
    return value


class Session:
    """One console process; not thread-safe. Use as a context manager.

    `caller` tells the console's usage log who reads the results: 'script'
    (the default: a script's output never reaches an agent's context) or
    'tool' (an agent reads every result, as through the MCP adapter).
    `table`: a `help --json` result already read from this console; the
    session then asks only for its hash (`help --hash`) and reads the whole
    table again only when it changed.
    """

    def __init__(self, console, args=(), caller='script', table=None):
        self._process = subprocess.Popen([console, *args, '--caller', caller], stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self._types = {}
        try:
            if table is not None:
                current = self.send('help --hash')
                if not (current.get('ok') and current['result'].get('table_hash') == table.get('table_hash')):
                    table = None
            if table is None:
                reply = self.send('help --json')
                if not reply.get('ok'):
                    raise ReAgentLiveError(reply.get('command', 'help --json'), reply.get('error'))
                table = reply['result']
        except BaseException:
            self.close()
            raise
        self.table = table
        self.table_hash = self.table.get('table_hash')
        self._named = self.table.get('types', {})
        for row in self.table['rows']:
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
        """The full JSON envelope for one line (a typed command or a JSON call), as plain dicts."""
        try:
            self._process.stdin.write(line + '\n')
            self._process.stdin.flush()
        except OSError:
            raise ReAgentLiveError(line, {'message': 'console exited'}) from None
        while True:
            text = self._process.stdout.readline()
            if not text:
                raise ReAgentLiveError(line, {'message': 'console exited'})
            if text.lstrip().startswith('{'):
                return json.loads(text)

    def wrap(self, verb, result):
        """A verb's result object as a strict Record (a plain dict for a verb the table lacks)."""
        kind = self._types.get(verb)
        return _wrap(result, kind, verb, self._named) if kind else result

    def call(self, verb, args=None, meta=False, timing=False):
        """One verb by name with named arguments (keys as `help <verb>` shows them); its strict result.

        None and False arguments are left out; bytes go as hex pairs.
        """
        call = {'verb': verb, 'args': {key: _value(value) for key, value in (args or {}).items()
                                       if value is not None and value is not False}}
        if meta:
            call['meta'] = True
        if timing:
            call['timing'] = True
        envelope = self.send(json.dumps(call, separators=(',', ':')))
        if not envelope.get('ok'):
            raise ReAgentLiveError(verb, envelope.get('error'))
        return self.wrap(verb, envelope.get('result', {}))

    def ok(self, line):
        """A typed line's result as a strict Record; raises ReAgentLiveError on failure."""
        envelope = self.send(line)
        if not envelope.get('ok'):
            raise ReAgentLiveError(line, envelope.get('error'))
        return self.wrap(line.split(None, 1)[0], envelope.get('result', {}))

    def help(self, verb=None):
        """Every verb in one line each, or one verb's arguments, result notation, named types and notes."""
        return self.call('help', {'verb': verb})

    # Helpers that convert a result; each says so in its name.
    def read_bytes(self, address, length):
        return unhex(self.call('read', {'addr': address, 'len': length})['hex'])

    def read_struct(self, address, fmt):
        """struct.unpack of `fmt` (little-endian unless it says otherwise) read from address."""
        if fmt[:1] not in '<>!=@':
            fmt = '<' + fmt
        return struct.unpack(fmt, self.read_bytes(address, struct.calcsize(fmt)))

    def read_batch_bytes(self, spans):
        """(tick, [bytes or None]) for up to 8 (address, length) spans read in one host tick."""
        result = self.call('read-batch', {'spans': [list(span) for span in spans]})
        return result['tick'], [unhex(row['hex']) if row['ok'] else None for row in result['rows']]

    def batch_results(self, calls):
        """(items, same_tick) of a batch: calls are {'verb', 'args'} dicts or (verb, args) pairs.

        Each item is that call's strict result, or a ReAgentLiveError (not raised).
        """
        calls = [call if isinstance(call, Mapping) else {'verb': call[0], 'args': call[1] if len(call) > 1 else {}}
                 for call in calls]
        result = self.call('batch', {'calls': calls})
        items = [self.wrap(row.get('command', '?'), row.get('result', {})) if row.get('ok')
                 else ReAgentLiveError(row.get('command', '?'), row.get('error')) for row in result['rows']]
        return items, result['same_tick']

    def get_values(self, ref, *paths):
        """The decoded values of get: one path returns its value, several a list. Raises if a path fails."""
        values = []
        # Rows come in the order asked; a failed path keeps its place with `error`.
        for path, row in zip(paths, self.call('get', {'object': ref, 'paths': list(paths)})['rows']):
            if row.get('error') is not None:
                raise ReAgentLiveError(f'get {path}', {'message': row['error']})
            values.append(row['value'])
        return values[0] if len(paths) == 1 else values

    def name_map(self, indices):
        """{index: text or None} of name."""
        indices = list(indices)
        return {row['index']: row['name'] for row in self.call('name', {'indices': indices})['rows']} if indices else {}

    def call_bytes(self, ref, function, params=b''):
        """The parameter block after a raw call (outputs included), as bytes."""
        args = {'object': ref, 'function': function, 'params': [bytes(params).hex()] if params else None}
        return unhex(self.call('call', args)['hex'])
