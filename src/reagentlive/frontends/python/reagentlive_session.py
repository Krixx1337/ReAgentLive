"""ReAgentLive console session for scripts and agents.

Drives a ReAgentLive console executable through its stateful stdin mode: one
JSON call in ({"verb": ..., "args": {...}}), one JSON line out, so nothing is
quoted or escaped. Calls carry literal refs (the `ref` a result printed).
Every call raises ReAgentLiveError when the console reports `ok: false`; a
refused argument is kind 'schema', with `error['example']`, a corrected call.

The functions to call are generated per console, from its own verb table,
by reagentlive_gen.py: one function per verb with typed arguments and a
typed result, a host's verbs included.

    python reagentlive_gen.py --console <console.exe> -o my_console.py

    from reagentlive_session import Session, f32, unhex
    from my_console import Console
    with Session(r'path\\to\\console.exe') as s:
        c = Console(s)                       # Refuses a module generated from another table.
        read = c.read('0x7ff600001000', len=4)  # A Record: read['hex'], read['module'] (None outside a module)
        read['bytes']                        # ResultKeyError: read has no key 'bytes'; keys: address, ..., hex, ...
        speed = f32(unhex(read['hex']))
        items, same_tick = s.batch_results([('read', {'addr': '0x1000', 'len': 8}), ('modules', {})])

Results are strict. At start the session reads the console's verb table
(`help` with `json`) and wraps every result in a Record typed by its verb's row:
a key the result type lacks raises ResultKeyError and names the valid keys,
and an optional key that is absent gives None. So a key guessed wrong fails
on the line that guessed it, never as a quiet None or a caught KeyError.
`s.help(verb)` shows a verb's arguments and result keys; `s.table_hash`
tells a generated module or cached script its table is stale.

A result past the call's output budget (`budget=N` characters; a session
started with caller='tool' has one by default) comes back as a Digest
instead: its size, its largest parts and `next`, the call narrowed, or
`file`, where the console kept the whole result of a call that must not run
again.

`Session(console, packs=['explore', 'native'])` runs the console with that
ceiling (`--packs`): a verb of another tool pack, through a generated
function, `call` or a batch item, raises ReAgentLiveError with kind 'pack'.
The generated module keeps every verb; the console decides.

`s.call(verb, args)` sends any verb by name; a generated function does the
same with checked argument names. Three helpers do what a generated function
cannot: `s.batch_results` (a strict result per batch item), `s.watch` (a
capture that blocks: watch-start, wait, watch-stop) and `samples` (a
capture's rows as bytes). Nothing else here names a verb; the converters
(`unhex`, `addr`, `pack`, `f32`...) name none. Paging, filtering and
retrying are the console's (arguments and cursors), not Python's.

Standard library only.
"""
import json
import os
import shutil
import struct
import subprocess
import tempfile
import time
from collections.abc import Mapping

__all__ = ['ReAgentLiveError', 'ResultKeyError', 'StaleTableError', 'Record', 'Digest', 'Session', 'samples', 'addr', 'unhex',
           'f32', 'f64', 'u32', 'u64', 'i32', 'pack']


class ReAgentLiveError(RuntimeError):
    """The console answered `ok: false`; `.error` holds its error object and `.kind` its kind ('pack': a verb
    outside the session's tool packs)."""

    def __init__(self, command, error):
        self.command, self.error = command, error or {}
        self.kind = self.error.get('kind')
        super().__init__(f'{command}: {self.error.get("message", "failed")}')


class Digest(dict):
    """A result past its output budget, not printed: chars, budget, largest, maybe rows, narrow, and `next` (the
    call narrowed: a JSON call's text) or `file` (the whole envelope, for a call that must not run again)."""

    def next_call(self):
        """`next` as {'verb', 'args'}, or None."""
        return json.loads(self['next']) if 'next' in self else None


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
    """A call argument as JSON: bytes as hex pairs, tuples as lists, None keys left out."""
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


def console_name(process):
    """The console's file name, for messages."""
    args = process.args if isinstance(process.args, (list, tuple)) else [process.args]
    return str(args[0]).replace('\\', '/').rsplit('/', 1)[-1] if args else 'the console'


def _line(verb, args=None):
    """One JSON call as a console line."""
    return json.dumps({'verb': verb, 'args': args or {}}, separators=(',', ':'))


class Session:
    """One console process; not thread-safe. Use as a context manager.

    `caller` tells the console's usage log who reads the results: 'script'
    (the default: a script's output never reaches an agent's context) or
    'tool' (an agent reads every result, as through the MCP adapter).
    `table`: a `help` with `json` result already read from this console; the
    session then asks only for its hash (`help` with `hash`) and reads the whole
    table again only when it changed. `console` is a path (relative to the
    current directory) or a name on PATH. `packs`: the tool packs the console
    runs (its --packs; explore always); None runs every pack.
    """

    def __init__(self, console, args=(), caller='script', table=None, packs=None):
        console = os.fspath(console)
        found = shutil.which(console) if not os.path.dirname(console) else None
        path = os.path.abspath(found or console)
        if not os.path.isfile(path):
            raise ReAgentLiveError('start', {'message': f'no console at {path}', 'kind': 'startup'})
        console = path
        # stderr goes to a file, read only when the start fails: a pipe nobody reads could fill and stall the console.
        self._stderr = tempfile.TemporaryFile('w+', encoding='utf-8', errors='replace')
        if isinstance(packs, str):
            packs = [packs]
        self.packs = list(packs) if packs is not None else None
        ceiling = ['--packs', ','.join(self.packs)] if self.packs is not None else []
        self._process = subprocess.Popen([console, *args, *ceiling, '--caller', caller], stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE, stderr=self._stderr, text=True, bufsize=1)
        self._types = {}
        self.digest = None  # The last call's Digest (its result was past the budget), or None.
        try:
            if table is not None:
                current = self.send(_line('help', {'hash': True}))
                if not (current.get('ok') and current.get('result', {}).get('table_hash') == table.get('table_hash')):
                    table = None
            if table is None:
                reply = self.send(_line('help', {'json': True}))
                if not reply.get('ok'):
                    raise ReAgentLiveError(reply.get('command', 'help'), reply.get('error'))
                if 'result' not in reply:
                    raise ReAgentLiveError('help', {'message': f'no table in the reply (keys: {", ".join(reply)})'})
                table = reply['result']
        except ReAgentLiveError as error:
            self.close()
            raise ReAgentLiveError('start', dict(error.error, message=self._started_badly(error))) from None
        except BaseException:
            self.close()
            raise
        self.table = table
        self.table_hash = self.table.get('table_hash')
        self._named = self.table.get('types', {})
        for row in self.table['rows']:
            self._types[row['verb']] = row['type']

    def _started_badly(self, error):
        """Why the console did not start: the error, its exit code and the end of its stderr."""
        text = f'{console_name(self._process)} did not start: {error}'
        if self._process.returncode is not None:
            text += f'; exit code {self._process.returncode}'
        try:
            self._stderr.seek(0)
            if tail := self._stderr.read()[-800:].strip():
                text += f'; stderr: {tail}'
        except (OSError, ValueError):
            pass
        return text

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        if self._process.poll() is not None:
            return
        try:
            self._process.stdin.write(_line('quit') + '\n')
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
        """The full JSON envelope for one line (a JSON call, or a JSON list of calls), as plain dicts."""
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

    def call(self, verb, args=None, meta=False, timing=False, budget=None):
        """One verb by name with named arguments (keys as `help` with `verb` shows them); its strict result, or a
        Digest when it was past the output budget.

        None arguments are left out (the console applies the default); bytes go as hex pairs. budget:
        characters (0: none; None: the caller's default).
        """
        call = {'verb': verb, 'args': {key: _value(value) for key, value in (args or {}).items() if value is not None}}
        if meta:
            call['meta'] = True
        if timing:
            call['timing'] = True
        if budget is not None:
            call['budget'] = budget
        envelope = self.send(json.dumps(call, separators=(',', ':')))
        self.digest = Digest(envelope['digest']) if 'digest' in envelope else None
        if not envelope.get('ok'):
            raise ReAgentLiveError(verb, envelope.get('error'))
        return self.digest if self.digest is not None else self.wrap(verb, envelope.get('result', {}))

    def help(self, verb=None):
        """Every verb in one line each, or one verb's arguments, result notation, named types and notes."""
        return self.call('help', {'verb': verb})

    def batch_results(self, calls):
        """(items, same_tick) of a batch: calls are {'verb', 'args'} dicts or (verb, args) pairs.

        Each item is that call's strict result, a Digest, or a ReAgentLiveError (not raised).
        """
        calls = [call if isinstance(call, Mapping) else {'verb': call[0], 'args': call[1] if len(call) > 1 else {}}
                 for call in calls]
        result = self.call('batch', {'calls': calls})
        items = [Digest(row['digest']) if row.get('ok') and 'digest' in row
                 else self.wrap(row.get('command', '?'), row.get('result', {})) if row.get('ok')
                 else ReAgentLiveError(row.get('command', '?'), row.get('error')) for row in result['rows']]
        return items, result['same_tick']

    def watch(self, seconds, ranges, every=None, follow=None, file=None, all=None):
        """A capture that blocks: watch-start for `seconds`, wait it out, then watch-stop's strict result.

        ranges: watch-start's range objects: {'addr': <addr>, 'len': n} for bytes, {'addr': <addr>, 'type':
        'f32', 'count': n} for decoded values (count optional), {'offset': n, 'len' or 'type': ...} with
        follow (offsets from the root), or {'object': <ref>, 'field': ['Field', ...]} for a reflected field.
        The wait ends early once the agent says the capture stopped (an object replaced, the buffer full).
        """
        self.call('watch-start', {'duration': seconds, 'ranges': list(ranges), 'every': every, 'follow': follow,
                                  'file': file})
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            time.sleep(min(0.25, max(0.0, deadline - time.monotonic())))
            if not self.call('watch-read')['running']:
                break
        return self.call('watch-stop', {'all': all})
