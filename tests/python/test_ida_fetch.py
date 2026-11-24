"""The IDA loader's fetch: the console calls it sends, its replies, and the one-call loop over a stand-in
console (a small Python script that writes synthetic saves), with the IDA steps stubbed."""
import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'reagentlive', 'frontends', 'ida'))
import reagentlive_ida as rl  # noqa: E402

# A console stand-in: reads `-c @<file>`, logs its arguments, writes a one-function save per disasm call
# (a callee too when callees is given) and prints one envelope per call; REFUSE=1 refuses the second.
FAKE = r'''
import json, os, sys
args = sys.argv[1:]
calls = json.load(open(args[args.index('-c') + 1][1:]))
with open(os.environ['FAKE_LOG'], 'a') as log:
    log.write(json.dumps(args) + '\n')
for index, call in enumerate(calls):
    if call['verb'] == 'labels-sign':
        rows = [{'name': 'Mine', 'kind': 'user', 'rva': '0x10', 'status': 'signed'},
                {'name': 'Short', 'kind': 'user', 'rva': '0x20', 'status': 'unsigned', 'reason': 'no unique bytes'}]
        print(json.dumps({'ok': True, 'command': 'labels-sign', 'result': {
            'path': call['args']['file'], 'signed': 1, 'unsigned': 1, 'skipped': 0, 'failed_before': 0, 'rows': rows}}))
        continue
    save = call['args']['save']
    if os.environ.get('REFUSE') and index == 1:
        print(json.dumps({'ok': False, 'command': 'disasm', 'error': {'kind': 'start', 'message': 'nothing to save'}}))
        continue
    start = int(call['args']['addr'], 16)
    starts = [start] + ([start + 0x100] if 'callees' in save else [])
    with open(save['file'], 'w') as out:
        for at in starts:
            out.write(json.dumps({'row': 'function', 'module': 'Game.exe', 'module_size': 4096, 'timestamp': 7,
                                  'base': '0x140000000', 'start': hex(at), 'end': hex(at + 1)}) + '\n')
        for at in starts:
            out.write(json.dumps({'row': 'code', 'address': hex(at), 'rva': hex(at - 0x140000000), 'bytes': 'c3'}) + '\n')
    result = {'address': hex(start), 'saved': save['file'], 'saved_rows': len(starts)}
    if 'callees' in save:
        result['functions'] = [{'start': hex(at), 'end': hex(at + 1), 'rows': 1, 'depth': i} for i, at in enumerate(starts)]
        result['skipped'] = [{'address': hex(start + 0x200), 'why': 'depth'}]
    print(json.dumps({'ok': True, 'command': 'disasm', 'result': result}))
'''


class Calls(unittest.TestCase):
    def test_one_save_per_address_with_the_depth(self):
        calls, paths = rl.fetch_calls([0x140001000, '140002000'], 'dir', depth=2)
        self.assertEqual(len(paths), 2)
        self.assertEqual(calls[0], {'verb': 'disasm', 'args': {'addr': '0x140001000', 'function': True,
                                                               'save': {'file': paths[0], 'callees': 2}}})
        self.assertEqual(calls[1]['args']['addr'], '0x140002000')
        alone, _ = rl.fetch_calls('0x140001000', 'dir', callees=False)
        self.assertNotIn('callees', alone[0]['args']['save'])
        zero, _ = rl.fetch_calls('0x140001000', 'dir', depth=0)   # Depth 0: the function alone.
        self.assertNotIn('callees', zero[0]['args']['save'])
        with self.assertRaises(ValueError):
            rl.fetch_calls(['nowhere'], 'dir')

    def test_replies_raise_the_first_refusal_with_it_attached(self):
        ok = json.dumps({'ok': True, 'result': {'saved': 'a'}})
        refused = json.dumps({'ok': False, 'command': 'disasm', 'error': {'kind': 'start', 'message': 'no'}})
        self.assertEqual(rl.console_results(f'noise\n{ok}\n', 1), [{'saved': 'a'}])
        with self.assertRaises(rl.FetchError) as caught:
            rl.console_results(f'{ok}\n{refused}\n', 2)
        self.assertEqual(caught.exception.refusal['kind'], 'start')
        with self.assertRaises(rl.FetchError):
            rl.console_results(ok, 2)   # A reply missing.


class Fetch(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        script = os.path.join(self.dir, 'console.py')
        with open(script, 'w') as handle:
            handle.write(FAKE)
        self.console = [sys.executable, '-I', script]
        self.log = os.path.join(self.dir, 'log')
        os.environ['FAKE_LOG'] = self.log
        os.environ.pop('REFUSE', None)
        self.added, self.decompiled = [], []
        self.saved = (rl.session.add_saved, rl.session.decompile_to)

        def add_saved(path):
            with open(path) as handle:
                plan = rl.parse(handle.read().splitlines())
            self.added.append([start for start, _ in plan['functions']])
            return {'added': [f'{start:#x}-{start + 1:#x}' for start, _ in plan['functions']], 'joined': [], 'cut': [],
                    'stubs_replaced': [], 'types': 1, 'types_failed': [], 'this_typed': 1, 'this_skipped': []}

        def decompile_to(targets, out_dir, grep=None):
            self.decompiled.append((targets, out_dir, grep))
            return [{'address': hex(t), 'path': os.path.join(out_dir, f'fn_{t:x}.c')} for t in targets]

        rl.session.add_saved, rl.session.decompile_to = add_saved, decompile_to

    def tearDown(self):
        rl.session.add_saved, rl.session.decompile_to = self.saved
        os.environ.pop('REFUSE', None)
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_addresses_to_a_loaded_and_decompiled_summary(self):
        out = rl.fetch([0x140001000, '0x140002000'], depth=2, decompile_dir='out', console=self.console, grep='this')
        with open(self.log) as handle:
            args = json.loads(handle.read().splitlines()[0])
        self.assertEqual(args[:4], ['--caller', 'script', '--packs', 'native'])
        self.assertEqual(self.added, [[0x140001000, 0x140001100], [0x140002000, 0x140002100]])
        self.assertEqual(out['functions'], ['0x140001000', '0x140001100', '0x140002000', '0x140002100'])
        self.assertEqual(len(out['skipped']), 2)
        self.assertEqual(out['types'], 2)
        self.assertEqual(out['this_typed'], 2)
        self.assertEqual(len(out['added']), 4)
        self.assertEqual(self.decompiled, [([0x140001000, 0x140001100, 0x140002000, 0x140002100], 'out', 'this')])
        self.assertEqual(len(out['decompiled']), 4)
        self.assertEqual(set(out['seconds']), {'console', 'load', 'decompile'})

    def test_without_callees_one_function_each_and_no_decompile(self):
        out = rl.fetch(0x140001000, callees=False, console=self.console)
        self.assertEqual(out['functions'], ['0x140001000'])
        self.assertNotIn('decompiled', out)
        self.assertEqual(self.decompiled, [])

    def test_depth_zero_is_the_function_alone(self):
        out = rl.fetch(0x140001000, depth=0, console=self.console)
        self.assertEqual(out['functions'], ['0x140001000'])
        with self.assertRaises(ValueError):
            rl.fetch(0x140001000, depth=-1, console=self.console)

    def test_the_recorded_console_is_the_default(self):
        saved = os.environ.pop('REAGENTLIVE_CONSOLE', None)
        recorded = rl.session._recorded_or_none
        try:
            rl.session._recorded_or_none = lambda: {'path': self.console, 'labels': 'a', 'names': 'b'}
            self.assertEqual(rl.fetch(0x140001000, depth=0)['functions'], ['0x140001000'])
        finally:
            rl.session._recorded_or_none = recorded
            if saved is not None:
                os.environ['REAGENTLIVE_CONSOLE'] = saved

    def test_an_argument_then_the_environment_override_the_recorded_console(self):
        saved = os.environ.pop('REAGENTLIVE_CONSOLE', None)
        try:
            recorded = {'path': 'recorded.exe'}
            self.assertEqual(rl.console_command(None, recorded), ['recorded.exe'])
            os.environ['REAGENTLIVE_CONSOLE'] = 'env.exe'
            self.assertEqual(rl.console_command(None, recorded), ['env.exe'])
            self.assertEqual(rl.console_command(['py', 'c.py'], recorded), ['py', 'c.py'])
            os.environ.pop('REAGENTLIVE_CONSOLE')
            with self.assertRaises(rl.FetchError) as caught:
                rl.console_command(None, None)
            self.assertIn('open a save database', str(caught.exception))
        finally:
            os.environ.pop('REAGENTLIVE_CONSOLE', None)
            if saved is not None:
                os.environ['REAGENTLIVE_CONSOLE'] = saved

    def test_a_refusal_raises_before_anything_loads(self):
        os.environ['REFUSE'] = '1'
        with self.assertRaises(rl.FetchError) as caught:
            rl.fetch([0x140001000, 0x140002000], console=self.console)
        self.assertEqual(caught.exception.refusal, {'kind': 'start', 'message': 'nothing to save'})
        self.assertEqual(self.added, [])

    def test_the_console_comes_from_the_environment(self):
        saved = os.environ.pop('REAGENTLIVE_CONSOLE', None)
        try:
            with self.assertRaises(rl.FetchError):
                rl.fetch(0x140001000)
            os.environ['REAGENTLIVE_CONSOLE'] = os.path.join(self.dir, 'no-such-console.exe')
            with self.assertRaises(rl.FetchError):   # It does not run: an error, not a hang.
                rl.fetch(0x140001000)
        finally:
            os.environ.pop('REAGENTLIVE_CONSOLE', None)
            if saved is not None:
                os.environ['REAGENTLIVE_CONSOLE'] = saved
        with self.assertRaises(ValueError):
            rl.fetch(0x140001000, depth=4, console=self.console)


class StartAndFinish(unittest.TestCase):
    """workspace and finish over the recorded console row, with the IDA steps stubbed."""

    def setUp(self):
        self.dir = tempfile.mkdtemp()
        script = os.path.join(self.dir, 'console.py')
        with open(script, 'w') as handle:
            handle.write(FAKE)
        os.environ['FAKE_LOG'] = os.path.join(self.dir, 'log')
        self.recorded = {'path': [sys.executable, '-I', script], 'labels': 'console.labels.jsonl', 'names': 'names.jsonl'}
        self.calls = []
        self.saved = (rl.session.recorded_console, rl.session.import_labels, rl.session.export_names)
        rl.session.recorded_console = lambda: self.recorded

        def import_labels(files):
            self.calls.append(('import', files))
            return {'named': 3, 'commented': 0, 'outside': 9, 'already': 0, 'rows': 12, 'cancelled': False}

        def export_names(path):
            self.calls.append(('export', path))
            return 2

        rl.session.import_labels, rl.session.export_names = import_labels, export_names

    def tearDown(self):
        rl.session.recorded_console, rl.session.import_labels, rl.session.export_names = self.saved
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_workspace_imports_the_recorded_labels_files(self):
        out = rl.workspace()
        self.assertEqual(self.calls, [('import', ['console.labels.jsonl', 'names.jsonl'])])
        self.assertEqual(out['named'], 3)
        self.assertEqual(out['files'], ['console.labels.jsonl', 'names.jsonl'])
        self.assertEqual(rl.workspace_files({'labels': 'a', 'names': 'a'}), ['a'])   # Each once.
        self.recorded = None
        with self.assertRaises(ValueError):
            rl.workspace()
        rl.workspace('mine.jsonl')
        self.assertEqual(self.calls[-1], ('import', ['mine.jsonl']))

    def test_finish_exports_then_signs_the_names_file(self):
        with contextlib.redirect_stdout(io.StringIO()):
            out = rl.finish()
        self.assertEqual(self.calls, [('export', 'names.jsonl')])
        self.assertEqual(out['exported'], 2)
        self.assertEqual((out['signed'], out['unsigned'], out['path']), (1, 1, 'names.jsonl'))
        self.assertEqual(out['unsigned_rows'], ['Short: no unique bytes'])


class Timing(unittest.TestCase):
    def test_opens_counts_the_databases_an_ida_session_opened(self):
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
        import fetch_timing
        events = [
            {'ts': '2026-01-01T00:00', 'event': 'tool_call', 'tool': 'open_database', 'call_id': 'a', 'input': {'path': 'one.json'}},
            {'ts': '2026-01-01T00:01', 'event': 'tool_result', 'tool': 'open_database', 'call_id': 'a', 'duration_ms': 1500},
            {'ts': '2026-01-02T00:00', 'event': 'tool_call', 'tool': 'open_database', 'call_id': 'b', 'input': {'path': 'two.json'}},
            {'ts': '2026-01-02T00:01', 'event': 'tool_result', 'tool': 'open_database', 'call_id': 'b', 'duration_ms': 500},
            {'ts': '2026-01-02T00:02', 'event': 'tool_result', 'tool': 'execute_python', 'call_id': 'c'},
        ]
        lines = [json.dumps(event) for event in events] + ['not json']
        self.assertEqual(fetch_timing.opens(lines), {'opened': 2, 'seconds': 2.0, 'paths': ['one.json', 'two.json']})
        self.assertEqual(fetch_timing.opens(lines, since='2026-01-02')['opened'], 1)



class SetupTest(unittest.TestCase):
    """tools/ida_setup.py: the stub and the package, current, stale or missing; install into a folder."""

    def setUp(self):
        import importlib.util
        tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'ida_setup.py')
        spec = importlib.util.spec_from_file_location('ida_setup', tool)
        self.setup = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.setup)
        self.folder = tempfile.mkdtemp()
        self.package = os.path.dirname(os.path.abspath(rl.__file__))

    def tearDown(self):
        shutil.rmtree(self.folder, ignore_errors=True)

    def write(self, name, text):
        with open(os.path.join(self.folder, name), 'w') as handle:
            handle.write(text)

    def test_states(self):
        check = self.setup.check
        self.assertEqual(check(self.folder, self.package)['state'], 'missing')
        self.write('other.py', '# another loader' + chr(10))
        shutil.copyfile(self.setup.STUB, os.path.join(self.folder, 'reagentlive_loader.py'))
        self.assertEqual(check(self.folder, None)['state'], 'missing')   # No package: the stub loads nothing.
        report = check(self.folder, self.package)
        self.assertEqual((report['state'], report['why']), ('current', []))
        self.assertEqual(len(report['installed']), 1)   # Only ReAgentLive files.
        self.assertEqual(check(self.folder, self.folder)['state'], 'stale')   # The package from elsewhere.
        self.write('reagentlive_ida.py', '"""ReAgentLive' + chr(39) + 's IDA side, one file' + chr(10))   # The old loader.
        self.assertEqual(check(self.folder, self.package)['state'], 'stale')
        os.remove(os.path.join(self.folder, 'reagentlive_loader.py'))   # The old loader alone: not set up.
        self.assertEqual(check(self.folder, self.package)['state'], 'missing')

    def test_install_writes_the_stub_and_removes_the_old_loader(self):
        self.write('reagentlive_ida.py', '"""ReAgentLive' + chr(39) + 's IDA side, one file' + chr(10))
        site = os.path.join(self.folder, 'site-packages')
        os.makedirs(site)
        self.setup.site_packages = lambda python: site
        loaders = os.path.join(self.folder)
        done = self.setup.install(loaders, 'python')
        self.assertEqual(done['removed'], [os.path.join(loaders, 'reagentlive_ida.py')])
        with open(done['path_file']) as handle:
            self.assertEqual(handle.read().strip(), self.setup.REPO_IDA)
        self.assertEqual(self.setup.check(loaders, self.package)['state'], 'current')


if __name__ == '__main__':
    unittest.main()
