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

A result past the call's cap (`max_chars=N` characters; a session
started with caller='tool' has one by default) comes back as a Digest
instead: its size, its largest parts and `next`, the call narrowed, or
`file`, where the console kept the whole result of a call that must not run
again. Past the console's `--context-limit` (a tool caller's session total) every call also sets
`s.context_warning`, one line; else it is None.

`Session(console, packs=['explore', 'native'])` runs the console with that
ceiling (`--packs`): a verb of another tool pack, through a generated
function, `call` or a batch item, raises ReAgentLiveError with kind 'pack'.
The generated module keeps every verb; the console decides.

The agent has one request slot, and clients take it in turns: the
session's console takes it for each request (a held command, such as a
batch, for its whole length) and gives it up between them, so an MCP
server or another script on the same agent runs between this session's
calls, and a long verb gives it up between its blocks. A call that finds
the slot taken waits for it up to `busy_wait` seconds (the console's
--busy-wait, default 10), then raises kind 'busy', naming the client that
holds it.

Background runs: `s.call('run', {'plan': path, 'background': True})`
starts the plan in its own console process and returns {'run': id} at
once; `s.run_status(id)` says how far it is, `s.run_read(id)` gives its
result once done (a Record or Digest, as call does), `s.run_stop(id)` ends
it and deletes its files. A run takes the slot in turns like any client,
under the session's packs. Runs end with the session (a Windows job
object kills them if the process dies).

`s.call(verb, args)` sends any verb by name; a generated function does the
same with checked argument names. Three helpers do what a generated function
cannot: `s.batch_results` (a strict result per batch item), `s.watch` (a
capture that blocks: watch-start, wait, watch-stop) and `samples` (a
capture's rows as bytes). Nothing else here names a verb; the converters
(`unhex`, `addr`, `pack`, `f32`...) name none. Paging, filtering and
retrying are the console's (arguments and cursors), not Python's.

Standard library only.
"""
import atexit
import ctypes
import itertools
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
from collections.abc import Mapping
from functools import wraps

__all__ = ['ReAgentLiveError', 'ResultKeyError', 'StaleTableError', 'Record', 'Digest', 'Session', 'Runs', 'samples', 'addr',
           'unhex', 'f32', 'f64', 'u32', 'u64', 'i32', 'pack']


class ReAgentLiveError(RuntimeError):
    """The console answered `ok: false`; `.error` holds its error object and `.kind` its kind ('pack': a verb
    outside the session's tool packs)."""

    def __init__(self, command, error):
        self.command, self.error = command, error or {}
        self.kind = self.error.get('kind')
        super().__init__(f'{command}: {self.error.get("message", "failed")}')


class Digest(dict):
    """A result past its cap, not printed: chars, max_chars, largest, maybe rows, narrow, and `next` (the
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


class _Job:
    """One process and everything it starts, in a Windows job object that kills them all when `end` is called or
    its last handle closes (this process exits or dies). The process starts suspended and is assigned before it
    runs, so nothing it starts escapes. Elsewhere: the process alone."""

    def __init__(self, command, stdout, stderr):
        self.handle = None
        if sys.platform != 'win32':
            self.process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr)
            return
        from ctypes import wintypes

        class Basic(ctypes.Structure):
            _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64), ('PerJobUserTimeLimit', ctypes.c_int64),
                        ('LimitFlags', wintypes.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t),
                        ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', wintypes.DWORD),
                        ('Affinity', ctypes.c_size_t), ('PriorityClass', wintypes.DWORD),
                        ('SchedulingClass', wintypes.DWORD)]

        class Extended(ctypes.Structure):
            _fields_ = [('BasicLimitInformation', Basic), ('IoInfo', ctypes.c_uint64 * 6),
                        ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                        ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]

        kernel32 = self._kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel32.CreateJobObjectW.restype = wintypes.HANDLE
        kernel32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        kernel32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        kernel32.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
        kernel32.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD,
                                                       ctypes.c_void_p]
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        ntdll = ctypes.WinDLL('ntdll')
        ntdll.NtResumeProcess.argtypes = [wintypes.HANDLE]
        info = Extended()
        info.BasicLimitInformation.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        job = kernel32.CreateJobObjectW(None, None)
        if not job or not kernel32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
            if job:
                kernel32.CloseHandle(job)
            raise OSError(f'cannot make a job object (error {ctypes.get_last_error()})')
        self.process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
                                        creationflags=0x4)  # CREATE_SUSPENDED
        if not kernel32.AssignProcessToJobObject(job, int(self.process._handle)):
            error = ctypes.get_last_error()
            self.process.kill()
            kernel32.CloseHandle(job)
            raise OSError(f'cannot put the run in its job object (error {error})')
        self.handle = job
        ntdll.NtResumeProcess(int(self.process._handle))

    def _active(self):
        """Processes still alive in the job (its basic accounting), 0 when it cannot say."""
        counts = (ctypes.c_uint32 * 12)()  # Four 64-bit times, then page faults, total, active, terminated.
        if not self._kernel32.QueryInformationJobObject(self.handle, 1, counts, ctypes.sizeof(counts), None):
            return 0
        return counts[10]

    def end(self):
        """Kills the process and all it started; waits until they are gone (their files closed)."""
        if self.handle is not None:
            self._kernel32.TerminateJobObject(self.handle, 1)
            deadline = time.monotonic() + 5
            while self._active() and time.monotonic() < deadline:
                time.sleep(0.01)
            self._kernel32.CloseHandle(self.handle)
            self.handle = None
        elif self.process.poll() is None:
            self.process.kill()
        self.process.wait()


def _running(pid):
    """Whether a process with this id runs now."""
    if sys.platform != 'win32':
        try:
            os.kill(pid, 0)
            return True
        except OSError:
            return False
    kernel32 = ctypes.WinDLL('kernel32')
    handle = kernel32.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE
    if not handle:
        return False
    try:
        return kernel32.WaitForSingleObject(handle, 0) == 0x102  # WAIT_TIMEOUT: still running.
    finally:
        kernel32.CloseHandle(handle)


def _locked(method):
    """One call at a time on the object (its `_lock`): a frontend may follow runs from several threads."""
    @wraps(method)
    def call(self, *args, **kwargs):
        with self._lock:
            return method(self, *args, **kwargs)
    return call


_RUN_IDS = itertools.count(1)  # Per process: every Runs of one console shares its folder.


class Runs:
    """A frontend's background runs: each one plan in its own console process (`run` without background).

    Files go in `<console>.runs/` beside the console: per run its call, its stdout (the final envelope) and its
    stderr (the progress lines). A run whose stdout and stderr pass `cap` bytes is stopped and says so when its
    status is read (run_status, run_read). Files
    stay until `stop` or this frontend exits; a frontend starting deletes those of frontends no longer running.
    Each run is a job object of its own, so it ends with the frontend however that ends.
    """
    CAP = 16 << 20

    def __init__(self, console, cap=CAP):
        self.folder = os.path.splitext(os.path.abspath(console))[0] + '.runs'
        self.cap = cap
        self._lock = threading.RLock()
        self._runs = {}
        os.makedirs(self.folder, exist_ok=True)
        for name in os.listdir(self.folder):  # <pid>-<n>.<part>: a frontend gone leaves its files.
            owner = name.split('-', 1)[0]
            if owner.isdigit() and int(owner) != os.getpid() and not _running(int(owner)):
                try:
                    os.remove(os.path.join(self.folder, name))
                except OSError:
                    pass
        atexit.register(self.close)

    def _path(self, run, part):
        return os.path.join(self.folder, f'{run}.{part}')

    @_locked
    def start(self, command, args, max_chars=None):
        """Starts `run` with args (background left out) on `command` (the console and its arguments); the run id."""
        run = f'{os.getpid()}-{next(_RUN_IDS)}'
        call = {'verb': 'run', 'args': {key: value for key, value in args.items() if key != 'background'}}
        if max_chars is not None:
            call['max_chars'] = max_chars
        with open(self._path(run, 'call.json'), 'w', encoding='utf-8') as file:
            json.dump(call, file)
        out = open(self._path(run, 'out'), 'w', encoding='utf-8')
        err = open(self._path(run, 'err'), 'w', encoding='utf-8')
        try:
            job = _Job([*command, '-c', f'@{self._path(run, "call.json")}'], out, err)
        except OSError as error:
            out.close()
            err.close()
            self._remove(run)
            raise ReAgentLiveError('run', {'kind': 'startup', 'message': f'the run did not start: {error}'}) from None
        out.close()
        err.close()
        self._runs[run] = {'job': job, 'process': job.process, 'started': time.monotonic(), 'stopped': None}
        return run

    def _get(self, run):
        entry = self._runs.get(run)
        if entry is None:
            known = ', '.join(self._runs) or 'none'
            raise ReAgentLiveError('run_status', {'kind': 'schema', 'message': f'no run {run!r}; runs: {known}'})
        return entry

    @_locked
    def status(self, run):
        """{run, state (running, done or stopped), progress (the newest progress line, or None), seconds,
        stopped (why, when stopped)}."""
        entry = self._get(run)
        process = entry['process']
        if process.poll() is None and entry['stopped'] is None:
            size = sum(os.path.getsize(self._path(run, part)) for part in ('out', 'err')
                       if os.path.exists(self._path(run, part)))
            if size > self.cap:
                self._end(entry, f'its output passed the cap ({self.cap} bytes)')
        state = 'stopped' if entry['stopped'] else 'running' if process.poll() is None else 'done'
        progress = None
        try:
            with open(self._path(run, 'err'), encoding='utf-8', errors='replace') as file:
                for text in file:
                    if text.strip().startswith('progress:'):
                        progress = text.strip()[len('progress:'):].strip()
        except OSError:
            pass
        out = {'run': run, 'state': state, 'progress': progress,
               'seconds': round(time.monotonic() - entry['started'], 1)}
        if entry['stopped']:
            out['stopped'] = entry['stopped']
        return out

    @_locked
    def read(self, run):
        """The finished run's envelope; ReAgentLiveError while it runs, or once stopped."""
        state = self.status(run)
        if state['state'] == 'running':
            raise ReAgentLiveError('run_read', {'kind': 'running', 'message': f'run {run} is still running '
                                                f'({state["progress"] or "no progress yet"}): run_status shows how far'})
        if state['state'] == 'stopped':
            raise ReAgentLiveError('run_read', {'kind': 'stopped', 'message': f'run {run} was stopped: {state["stopped"]}'})
        try:
            with open(self._path(run, 'out'), encoding='utf-8', errors='replace') as file:
                for text in file:
                    if text.lstrip().startswith('{'):
                        return json.loads(text)
        except OSError:
            pass
        code = self._runs[run]['process'].returncode
        raise ReAgentLiveError('run_read', {'kind': 'internal', 'message': f'run {run} ended ({code}) with no result'})

    def _end(self, entry, why):
        if entry['process'].poll() is None:
            entry['stopped'] = entry['stopped'] or why
        entry['job'].end()  # Also whatever it started, and the job's handle.

    @_locked
    def stop(self, run):
        """Ends the run if it still runs and deletes its files; its last status."""
        entry = self._get(run)
        self._end(entry, 'run_stop')
        state = self.status(run)
        del self._runs[run]
        self._remove(run)
        return state

    def _remove(self, run):
        for part in ('call.json', 'out', 'err'):
            try:
                os.remove(self._path(run, part))
            except OSError:
                pass

    @_locked
    def close(self):
        """Ends every run and deletes their files."""
        for run in list(self._runs):
            self.stop(run)


class Session:
    """One console process; not thread-safe. Use as a context manager.

    `caller` tells the console's usage log who reads the results: 'script'
    (the default: a script's output never reaches an agent's context) or
    'tool' (an agent reads every result, as through the MCP adapter).
    `table`: a `help` with `json` result already read from this console; the
    session then asks only for its hash (`help` with `hash`) and reads the whole
    table again only when it changed. `console` is a path (relative to the
    current directory) or a name on PATH. `packs`: the tool packs the console
    runs (its --packs; explore always); None runs every pack; a background
    run gets the same. `busy_wait`: seconds a call waits for the slot another
    client holds (None: the console's default).
    """

    def __init__(self, console, args=(), caller='script', table=None, packs=None, busy_wait=None):
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
        wait = ['--busy-wait', str(int(busy_wait))] if busy_wait is not None else []
        self._command = [console, *args, *ceiling, *wait, '--caller', caller]
        self._runs = None  # Runs, from the first background run.
        self._process = subprocess.Popen(self._command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=self._stderr, text=True, bufsize=1)
        self._types = {}
        self.digest = None  # The last call's Digest (its result was past max_chars), or None.
        # The last call's context_warning (the session is past the console's --context-limit), or None.
        self.context_warning = None
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
        if self._runs is not None:
            self._runs.close()
        if self._process is None or self._process.poll() is not None:
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

    def call(self, verb, args=None, meta=False, timing=False, max_chars=None):
        """One verb by name with named arguments (keys as `help` with `verb` shows them); its strict result, or a
        Digest when it was past max_chars.

        None arguments are left out (the console applies the default); bytes go as hex pairs. max_chars:
        characters (0: none; None: the caller's default).
        """
        call = {'verb': verb, 'args': {key: _value(value) for key, value in (args or {}).items() if value is not None}}
        if verb == 'run' and call['args'].get('background'):
            return {'run': self.runs.start(self._command, call['args'], max_chars)}
        if meta:
            call['meta'] = True
        if timing:
            call['timing'] = True
        if max_chars is not None:
            call['max_chars'] = max_chars
        return self._result(verb, self.send(json.dumps(call, separators=(',', ':'))))

    def _result(self, verb, envelope):
        """An envelope as call returns it: the strict result or a Digest; ReAgentLiveError when not ok."""
        self.digest = Digest(envelope['digest']) if 'digest' in envelope else None
        self.context_warning = envelope.get('context_warning')
        if not envelope.get('ok'):
            raise ReAgentLiveError(verb, envelope.get('error'))
        return self.digest if self.digest is not None else self.wrap(verb, envelope.get('result', {}))

    @property
    def runs(self):
        """This session's background runs (Runs)."""
        if self._runs is None:
            self._runs = Runs(self._command[0])
        return self._runs

    def run_status(self, run):
        """A background run's state (running, done or stopped), its newest progress line and its seconds."""
        return self.runs.status(run)

    def run_read(self, run):
        """A finished background run's result, as call returns run's; raises while it runs."""
        return self._result('run', self.runs.read(run))

    def run_stop(self, run):
        """Ends a background run and deletes its files; its last status."""
        return self.runs.stop(run)

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
