"""The IDA loader's fetch: the console calls it sends, its replies, and the one-call loop over a stand-in
console (a small Python script that writes synthetic saves), with the IDA steps stubbed."""
import json
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'reagentlive', 'frontends', 'ida'))
import reagentlive_ida as loader  # noqa: E402

# A console stand-in: reads `-c @<file>`, logs its arguments, writes a one-function save per disasm call
# (a callee too when callees is given) and prints one envelope per call; REFUSE=1 refuses the second.
FAKE = r'''
import json, os, sys
args = sys.argv[1:]
calls = json.load(open(args[args.index('-c') + 1][1:]))
with open(os.environ['FAKE_LOG'], 'a') as log:
    log.write(json.dumps(args) + '\n')
for index, call in enumerate(calls):
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
        calls, paths = loader.fetch_calls([0x140001000, '140002000'], 'dir', depth=2)
        self.assertEqual(len(paths), 2)
        self.assertEqual(calls[0], {'verb': 'disasm', 'args': {'addr': '0x140001000', 'function': True,
                                                               'save': {'file': paths[0], 'callees': 2}}})
        self.assertEqual(calls[1]['args']['addr'], '0x140002000')
        alone, _ = loader.fetch_calls('0x140001000', 'dir', callees=False)
        self.assertNotIn('callees', alone[0]['args']['save'])
        with self.assertRaises(ValueError):
            loader.fetch_calls(['nowhere'], 'dir')

    def test_replies_raise_the_first_refusal_with_it_attached(self):
        ok = json.dumps({'ok': True, 'result': {'saved': 'a'}})
        refused = json.dumps({'ok': False, 'command': 'disasm', 'error': {'kind': 'start', 'message': 'no'}})
        self.assertEqual(loader.console_results(f'noise\n{ok}\n', 1), [{'saved': 'a'}])
        with self.assertRaises(loader.FetchError) as caught:
            loader.console_results(f'{ok}\n{refused}\n', 2)
        self.assertEqual(caught.exception.refusal['kind'], 'start')
        with self.assertRaises(loader.FetchError):
            loader.console_results(ok, 2)   # A reply missing.


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
        self.saved = (loader.add_saved, loader.decompile_to)

        def add_saved(path):
            with open(path) as handle:
                plan = loader.parse(handle.read().splitlines())
            self.added.append([start for start, _ in plan['functions']])
            return {'added': [f'{start:#x}-{start + 1:#x}' for start, _ in plan['functions']], 'joined': [], 'cut': [],
                    'stubs_replaced': [], 'types': 1, 'types_failed': [], 'this_typed': 1, 'this_skipped': []}

        def decompile_to(targets, out_dir, grep=None):
            self.decompiled.append((targets, out_dir, grep))
            return [{'address': hex(t), 'path': os.path.join(out_dir, f'fn_{t:x}.c')} for t in targets]

        loader.add_saved, loader.decompile_to = add_saved, decompile_to

    def tearDown(self):
        loader.add_saved, loader.decompile_to = self.saved
        os.environ.pop('REFUSE', None)
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_addresses_to_a_loaded_and_decompiled_summary(self):
        out = loader.fetch([0x140001000, '0x140002000'], depth=2, decompile_dir='out', console=self.console, grep='this')
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
        out = loader.fetch(0x140001000, callees=False, console=self.console)
        self.assertEqual(out['functions'], ['0x140001000'])
        self.assertNotIn('decompiled', out)
        self.assertEqual(self.decompiled, [])

    def test_a_refusal_raises_before_anything_loads(self):
        os.environ['REFUSE'] = '1'
        with self.assertRaises(loader.FetchError) as caught:
            loader.fetch([0x140001000, 0x140002000], console=self.console)
        self.assertEqual(caught.exception.refusal, {'kind': 'start', 'message': 'nothing to save'})
        self.assertEqual(self.added, [])

    def test_the_console_comes_from_the_environment(self):
        saved = os.environ.pop('REAGENTLIVE_CONSOLE', None)
        try:
            with self.assertRaises(loader.FetchError):
                loader.fetch(0x140001000)
            os.environ['REAGENTLIVE_CONSOLE'] = os.path.join(self.dir, 'no-such-console.exe')
            with self.assertRaises(loader.FetchError):   # It does not run: an error, not a hang.
                loader.fetch(0x140001000)
        finally:
            os.environ.pop('REAGENTLIVE_CONSOLE', None)
            if saved is not None:
                os.environ['REAGENTLIVE_CONSOLE'] = saved
        with self.assertRaises(ValueError):
            loader.fetch(0x140001000, depth=4, console=self.console)


if __name__ == '__main__':
    unittest.main()
