"""reagentlive_session and a module reagentlive_gen made, against a scripted fake console (no agent or game).

The fake (fake_console.py) answers `help --json` with the real verb table
(printed by the reagentlive-table tool: pass it as --table <exe>, or set
REAGENTLIVE_TABLE) and every call with a canned reply. A test checks each
canned result against its verb's type in that table, so the replies cannot
drift from the console's real shapes; the module under test is generated
from the same table.
"""
import importlib.util
import inspect
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
PYTHON = os.path.join(HERE, '..', '..', 'src', 'reagentlive', 'frontends', 'python')
sys.path.insert(0, PYTHON)
import reagentlive_gen  # noqa: E402
import reagentlive_session as dl  # noqa: E402

TABLE_TOOL = os.environ.get('REAGENTLIVE_TABLE') and os.path.abspath(os.environ['REAGENTLIVE_TABLE'])
FAKE = os.path.join(HERE, 'fake_console.py')

OBJECT = {'ref': '0x10#1:1', 'name': 'A', 'class': 'Pawn'}
FIELD = {'name': 'Health', 'owner': 'Pawn', 'type': 'FloatProperty', 'offset': 256, 'size': 4, 'label': 'Health'}
RANGE = {'address': '0x9000', 'length': 4, 'guarded': False}
SIG = {'pattern': '48 8b', 'list': '0x48, 0x8B', 'module': 'Game.exe', 'unique': True, 'total': 1,
       'rows': [{'address': '0x140001000', 'module': 'Game.exe', 'rva': '0x1000'}], 'length': 2, 'fixed_bytes': 2,
       'anchor': {'offset': 0, 'length': 2, 'hits': 1}, 'warnings': [], 'resolves': True}
CALL = {'ref': '0x10#1:1', 'function': 'Pawn:Fn', 'mode': 'raw', 'block_size': 2, 'hex': '01 02', 'kept': False}
PIN = {'id': 1, 'address': '0x10', 'bytes': '00 00 80 3f', 'guarded': True, 'state': 'active', 'writes': 1, 'remaining_ms': 2500}
TRIAL = {'id': 1, 'address': '0x38', 'bytes': '00', 'original': '01', 'owner': '0x10', 'state': 'active', 'changed_tick': 5,
         'remaining_ms': 5000}
REF = '0x10#1:1'


def ok(result):
    return {'ok': True, 'result': result}


def error(**fields):
    return {'ok': False, 'error': fields}


def C(verb, args=None):
    return {'verb': verb, 'args': args or {}}


# Replies by call: [call as the client sends it, reply].
CALLS = [
    [C('status'), ok({'pid': 4, 'build': 'b', 'version': '0.1.0', 'replaced_agents': 0, 'ticks': 9, 'ticks_per_second': 60,
                      'interval_ms': 100, 'ticking': True, 'host_thread': 'engine', 'packs': ['ue'], 'table_hash': 'abc'})],
    [C('region', {'addr': 'base+0x10'}), ok({'address': '0x10', 'kind': 'code', 'section': '.text', 'base': '0x0',
                                            'size': '0x1000', 'allocation_base': '0x0', 'state': 'commit', 'type': 'image',
                                            'protect': 'r-x', 'allocation_protect': 'rwx'})],
    [C('read', {'addr': 4096, 'len': 4}), ok({'address': '0x1000', 'size': 4, 'hex': '00 00 80 3f'})],
    [C('read', {'addr': '0x10#1:1->0x30->0x8', 'len': 2}), ok({'address': '0x18', 'size': 2, 'hex': '01 02',
                                                             'chain': {'base': '0x10', 'hops': [], 'address': '0x18'}})],
    [C('read', {'addr': 16, 'as': 'cstr'}), ok({'address': '0x10', 'size': 2, 'hex': '41 00', 'text': 'A', 'length': 1,
                                              'terminated': True})],
    [C('read', {'addr': '0x10#1:1+0x538', 'as': 'u8 phase, pad 1'}), ok({'address': '0x538', 'size': 2, 'hex': '01 00',
                                                                         'layout': {'phase': 1}})],
    [C('read-batch', {'spans': [[4096, 2], ['base+0x10', 4]]}), ok({'tick': 7, 'rows': [
        {'address': '0x1000', 'size': 2, 'ok': True, 'hex': '01 02'},
        {'address': '0x10', 'size': 4, 'ok': False, 'error': {'status': 3, 'message': 'unreadable'}}]})],
    [C('peek', {'addr': 'base+0x10', 'count': 1, 'label': 'labels.jsonl'}), ok({'address': '0x10', 'rows': [
        {'address': '0x10', 'value': '0x5', 'label': 'value'},
        {'address': '0x18', 'value': '0x10', 'label': 'object', 'object': OBJECT}]})],
    [C('write', {'addr': 4096, 'data': '0000803f'}), ok({'address': '0x1000', 'size': 4})],
    [C('write', {'addr': 4096, 'data': '0102', 'expect': 'ffff'}), error(kind='agent', message='memory differs', found='00 00')],
    [C('search', {'type': 'ptr', 'value': '0x7ff600001234', 'heap': True, 'module': 'Game.exe', 'max': 512}),
     ok({'rows': [{'address': '0x10'}, {'address': '0x20', 'module': 'Game.exe', 'rva': '0x20'}], 'from': '0x0',
         'next_cursor': '0x21'})],
    [C('search', {'type': 'text', 'value': 'Fatal error', 'max': 1}), ok({'rows': [], 'from': '0x0'})],
    [C('batch', {'calls': [C('read', {'addr': 16, 'len': 1}), C('get', {'object': REF, 'paths': ['X']})]}),
     ok({'all_ok': False, 'same_tick': True, 'first_tick': 4, 'last_tick': 4, 'spans_ticks': [], 'rows': [
         {'command': 'read', 'ok': True, 'result': {'address': '0x10', 'size': 1, 'hex': '01'}, 'tick': 4},
         {'command': 'get', 'ok': False, 'error': {'message': 'no field X'}}]})],
    [C('modules'), ok({'rows': [{'name': 'Game.exe', 'base': '0x140000000', 'size': 4096}]})],
    [C('sig-check', {'pattern': '48 8b ?? 89 05', 'expect': 'Game.exe+0x10', 'offset': 4}), ok(SIG)],
    [C('sig-file', {'file': 'my sigs.jsonl'}), ok({'file': 'my sigs.jsonl', 'winner': 2, 'searches': 1, 'rows': [
        {'line': 1, 'ok': False, 'error': 'no pattern'}, {'line': 2, 'ok': True, 'result': SIG}]})],
    [C('sig-bytes', {'addr': 'base+0x10', 'len': 32}), ok({'address': '0x10', 'length': 2, 'pattern': '48 8b',
                                                          'list': '0x48, 0x8B', 'suggested': {
        'pattern': '48 8b', 'list': '0x48, 0x8B', 'fixed_bytes': 2, 'fields': []}})],
    [C('disasm', {'addr': 'Game.exe+0x10', 'count': 8, 'function': True, 'follow': True}), ok({
        'address': '0x10', 'size': 1, 'stop': 'count', 'rows': [{'address': '0x10', 'bytes': 'c3', 'text': 'ret', 'ends': True}]})],
    [C('xrefs', {'addr': 'base+0x10', 'module': 'engine.dll', 'all': True}), ok({
        'address': '0x10', 'scanned': 'engine.dll', 'kinds': {'call': 1}, 'rows': [{'kind': 'call', 'field': '0x21',
                                                                                    'instruction': '0x20'}],
        'capped': False, 'scan_ms': 3})],
    [C('touches', {'addr': 'Game.exe+0x10', 'offsets': [0x538, 0x540], 'range': 0x1000}), ok({
        'address': '0x10', 'offsets': ['0x538', '0x540'], 'range': 4096,
        'scanned': {'from': '0x0', 'to': '0x1010', 'functions': 2, 'bytes': 64}, 'total': 1, 'capped': False, 'scan_ms': 1,
        'rows': [{'function': {'start': '0x20', 'end': '0x40'}, 'distinct': 1, 'writes': False, 'total': 0, 'uses': []}]})],
    [C('pin', {'addr': '0x10#1:1+0x3e4', 'data': '0000803f', 'ms': 2500, 'expect': '00000000'}), ok(PIN)],
    [C('pin-list'), ok({'rows': [PIN]})],
    [C('pin-clear', {'id': 1}), ok({'cleared': 1})],
    [C('watch-start', {'duration': 600, 'ranges': ['0x538:16'], 'every': 10, 'follow': 'pawn'}), ok({
        'capacity': 9000, 'ms': 600000, 'interval_ms': 10000, 'root': 'pawn',
        'ranges': [{'offset': '0x538', 'length': 16, 'guarded': True}]})],
    [C('watch-read'), ok({'running': True, 'capacity': 9000, 'ms': 600000, 'interval_ms': 10000, 'root': 'pawn',
                          'ranges': [{'offset': '0x538', 'length': 16, 'guarded': True}],
                          'rows': [{'tick': 5, 'us': 0, 'values': ['01 00'], 'root': '0x10'}, {'tick': 9, 'us': 10, 'lost': True},
                                   {'tick': 12, 'us': 20, 'values': ['02 00'], 'resumed': True, 'root': '0x20'}],
                          'gaps': [{'from_us': 10, 'to_us': 20}]})],
    [C('watch', {'duration': 1.5, 'ranges': ['0x10#1:1+0x3e4:4', '0x9000:1'], 'then': 'call 0x10#1:1 Fn 01'}), ok({
        'capacity': 100, 'complete': True, 'ranges': [RANGE], 'then': {'ok': True, 'hex': '01'},
        'rows': [{'tick': 5, 'us': 0, 'values': ['01 00', None]}, {'tick': 6, 'us': 9000, 'values': ['02 00', 'ff']}]})],
    [C('trial', {'addr': '0x10#1:1+0x28', 'data': '0000000000000040', 'owner': REF, 'expect': '000000000000f03f',
                 'object': 0x2000, 'seconds': 5}), ok(TRIAL)],
    [C('trial-clear', {'id': 1}), ok({'cleared': 1, 'rows': [dict(TRIAL, state='restored')]})],
    [C('find', {'text': 'A', 'class': 'Pawn'}), ok({'rows': [OBJECT], 'total': 2, 'from': 0, 'next_cursor': 24})],
    [C('find', {'text': 'A', 'class': 'Pawn', 'from': 24}), ok({'rows': [dict(OBJECT, name='B')], 'from': 24})],
    [C('dump', {'target': REF, 'changed': True}), ok({'ref': REF, 'rows': [dict(FIELD, name='Ammo', value=4, before=5,
                                                                                truncated=False)],
                                                      'truncated': False, 'capped': False, 'object_lookup_limited': False})],
    [C('dump', {'target': REF, 'match': 'Ammo', 'changed': 'ctrl.snap'}), ok({
        'ref': REF, 'rows': [], 'truncated': False, 'capped': False, 'object_lookup_limited': False})],
    [C('census-diff', {'class': 'Actor'}), ok({'classes': {'before': 3, 'after': 4}, 'instances': {'before': 9, 'after': 11},
                                               'ms': 4, 'total': 1, 'from': 0, 'filter': 'Actor', 'rows': [
        {'class': 'Proj', 'ref': '0x30#3:1', 'before': 0, 'after': 2, 'samples': ['0x90#9:1']}]})],
    [C('describe', {'object': REF}), ok(dict(OBJECT, class_size=1264))],
    [C('get', {'object': REF, 'paths': ['A.B']}), ok({'ref': REF, 'same_tick': True, 'rows': [
        {'path': 'A.B', 'ok': True, 'value': 1.5, 'address': '0x18', 'object': REF, 'truncated': False}]})],
    [C('get', {'object': REF, 'paths': ['A.B', 'C']}), ok({'ref': REF, 'same_tick': True, 'rows': [
        {'path': 'A.B', 'ok': True, 'value': 1.5, 'address': '0x18', 'object': REF, 'truncated': False},
        {'path': 'C', 'ok': False, 'error': 'no field C'}]})],
    [C('name', {'indices': [5, 6]}), ok({'rows': [{'index': 5, 'name': 'Five'}, {'index': 6}]})],
    [C('refs', {'name': 'Missing'}), error(kind='validation', message='no pooled name is exactly Missing')],
    [C('call', {'object': REF, 'function': 'Pawn:Fn', 'params': ['01']}), ok(CALL)],
    [C('call', {'object': REF, 'function': 'Pawn:Get', 'params': ['Name=#5', 'Mode=2']}), ok(dict(
        CALL, mode='typed', kept=True, kept_reason='ReturnValue is a String', truncated=False, outputs=[
            {'name': 'ReturnValue', 'direction': 'return', 'value': 'x', 'truncated': False}]))],
    [C('call', {'object': 16, 'function': 'Pawn:Get', 'typed': True}), ok(dict(CALL, mode='typed', outputs=[], truncated=False))],
    [C('help', {'verb': 'region'}), ok({'rows': [{'verb': 'region', 'origin': 'core', 'usage': 'region <addr>', 'summary': 's',
                                                  'flags': [], 'result': '{address: addr}'}]})],
]
LINES = {'region base+0x10': CALLS[1][1]}


def conforms(value, kind, where, named):
    """Problems of a canned value against a type from help --json (the console's own check, in small)."""
    if not isinstance(kind, dict):
        return []
    if 'named' in kind:
        return conforms(value, named[kind['named']]['type'], where, named)
    if 'struct' in kind:
        if not isinstance(value, dict):
            return [f'{where}: not an object']
        problems = [f"{where}: unknown key '{key}'" for key in value if key not in kind['struct']]
        problems += [f"{where}: missing key '{key}'" for key, spec in kind['struct'].items()
                     if not spec.get('optional') and key not in value]
        for key, item in value.items():
            if key in kind['struct']:
                problems += conforms(item, kind['struct'][key]['type'], f'{where}.{key}', named)
        return problems
    if 'list' in kind:
        if not isinstance(value, list):
            return [f'{where}: not a list']
        return [p for i, item in enumerate(value) if item is not None
                for p in conforms(item, kind['list'], f'{where}[{i}]', named)]
    if 'enum' in kind:
        return [] if value in kind['enum'] or '...' in kind['enum'] else [f"{where}: '{value}' is not one of {kind['enum']}"]
    return []


def load(path):
    spec = importlib.util.spec_from_file_location('generated_console', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@unittest.skipUnless(TABLE_TOOL, 'needs the verb table: --table <reagentlive-table.exe> or REAGENTLIVE_TABLE')
class SessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.table = json.loads(subprocess.run([TABLE_TOOL], capture_output=True, text=True, check=True).stdout)
        cls.rows = {row['verb']: row for row in cls.table['rows']}
        cls.dir = tempfile.TemporaryDirectory()
        cls.module_path = os.path.join(cls.dir.name, 'generated_console.py')
        with open(cls.module_path, 'w', encoding='utf-8') as file:
            file.write(reagentlive_gen.generate(cls.table, 'reagentlive-table'))
        cls.gen = load(cls.module_path)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        replies = os.path.join(self.work.name, 'replies.json')
        with open(replies, 'w') as f:
            json.dump({'table': self.table, 'calls': CALLS, 'lines': LINES}, f)
        self.log = os.path.join(self.work.name, 'lines.txt')
        os.environ['REAGENTLIVE_FAKE_LOG'] = self.log
        os.environ['REAGENTLIVE_FAKE_REPLIES'] = replies
        self.session = dl.Session(sys.executable, ['-I', FAKE])
        self.c = self.gen.Console(self.session)

    def tearDown(self):
        self.session.close()
        self.work.cleanup()

    def sent(self):
        """The calls the client sent, as dicts (typed lines as text)."""
        with open(self.log) as f:
            return [json.loads(line) if line.startswith('{') else line.strip() for line in f if line.strip() != 'quit']

    def test_canned_replies_match_the_table(self):
        problems = []
        for call, reply in CALLS:
            self.assertIn(call['verb'], self.rows, call)
            if reply['ok']:
                problems += conforms(reply['result'], self.rows[call['verb']]['type'], call['verb'], self.table['types'])
        self.assertEqual(problems, [])

    def test_every_verb_has_a_generated_method_named_by_origin(self):
        for verb, row in self.rows.items():
            if verb == 'quit':
                continue
            owner = self.c if row['origin'] == 'core' else getattr(self.c, row['origin'])
            self.assertTrue(callable(getattr(owner, verb.replace('-', '_'), None)), verb)
        marks = {item['verb']: (item['writes'], item['blocks']) for item in self.gen.VERBS}
        self.assertEqual(marks['write'], (True, False))
        self.assertEqual(marks['watch'], (True, True))
        self.assertEqual(marks['read'], (False, False))
        self.assertNotIn('quit', marks)

    def test_generated_signatures_follow_the_rows(self):
        read = inspect.signature(self.gen.Console.read)
        self.assertEqual(list(read.parameters), ['self', 'addr', 'len', 'as_'])
        self.assertEqual(read.parameters['as_'].kind, inspect.Parameter.KEYWORD_ONLY)
        self.assertEqual(read.return_annotation.__name__, 'ReadResult')
        trial = inspect.signature(self.gen.Console.trial)
        self.assertIs(trial.parameters['owner'].default, inspect.Parameter.empty)   # A required flag.
        self.assertEqual(trial.parameters['owner'].kind, inspect.Parameter.KEYWORD_ONLY)
        find = inspect.signature(self.gen.UeVerbs.find)
        self.assertEqual(list(find.parameters), ['self', 'text', 'class_', 'from_'])
        self.assertIn('Literal', str(inspect.signature(self.gen.Console.search).parameters['type'].annotation))
        self.assertIn('class_size', self.gen.Object.__annotations__)              # Named types are classes.
        self.assertIn('Chain', str(self.gen.ReadResult.__annotations__['chain']))
        with self.assertRaises(TypeError):
            self.c.read(16, as_='cstr', length=4)                                  # A guessed argument fails at once.

    def test_a_stale_module_is_refused(self):
        hash_was = self.session.table_hash
        self.session.table_hash = 'different'
        with self.assertRaises(dl.StaleTableError):
            self.gen.Console(self.session)
        self.session.table_hash = hash_was

    def test_a_wrong_key_raises_and_names_the_valid_ones(self):
        region = self.c.region('base+0x10')
        self.assertEqual(region['kind'], 'code')
        self.assertIsNone(region['module'])              # Optional and absent: None.
        self.assertIsNone(region.get('chain'))
        self.assertEqual(region.get('module', 'none'), 'none')
        with self.assertRaises(dl.ResultKeyError) as caught:
            region['protection']
        self.assertIn("region has no key 'protection'; keys: address, allocation_base, allocation_protect, base, chain?, kind, module?, protect,", str(caught.exception))
        with self.assertRaises(dl.ResultKeyError):
            region.get('protection')                     # .get is no way around it.
        with self.assertRaises(dl.ResultKeyError):
            'protection' in region
        self.assertIn('kind', region)
        self.assertNotIn('module', region)
        self.assertEqual(self.session.table_hash, self.table['table_hash'])
        self.assertEqual(self.session.ok('region base+0x10')['kind'], 'code')   # A typed line, as strict.

    def test_nested_and_named_rows_are_strict_too(self):
        chained = self.c.read('0x10#1:1->0x30->0x8', 2)
        self.assertEqual(chained['chain']['address'], '0x18')
        with self.assertRaises(dl.ResultKeyError) as caught:
            chained['hops']                              # hops is inside chain.
        self.assertIn('chain?', str(caught.exception))
        with self.assertRaises(dl.ResultKeyError):
            chained['chain']['hop']                      # Chain is a named type, read as strictly.
        peeked = self.c.peek('base+0x10', 1, label='labels.jsonl')
        self.assertEqual(peeked['rows'][1]['object']['class'], 'Pawn')
        with self.assertRaises(dl.ResultKeyError):
            peeked['rows'][1]['object']['type']
        row = self.c.sig_bytes('base+0x10', 32)
        with self.assertRaises(dl.ResultKeyError) as caught:
            row['suggested']['text']
        self.assertIn("sig-bytes.suggested has no key 'text'; keys: decoded?, fields, fixed_bytes, list, pattern", str(caught.exception))
        hit = self.c.sig_check('48 8b ?? 89 05', expect='Game.exe+0x10', offset=4)['rows'][0]
        self.assertEqual(hit['rva'], '0x1000')
        with self.assertRaises(dl.ResultKeyError):
            hit['offset']
        self.assertEqual(self.c.help('region')['rows'][0]['result'], '{address: addr}')

    def test_calls_go_as_json_with_named_arguments(self):
        self.c.read(0x10, as_='cstr')
        self.c.read('0x10#1:1+0x538', as_='u8 phase, pad 1')
        self.c.write(0x1000, dl.pack('f32', 1.0))
        self.c.search('text', 'Fatal error', max=1)
        self.c.sig_file('my sigs.jsonl')                 # A space needs no quoting.
        self.c.disasm('Game.exe+0x10', 8, function=True, follow=True)
        self.c.xrefs('base+0x10', module='engine.dll', all=True)
        self.c.touches('Game.exe+0x10', [0x538, 0x540], range=0x1000)
        self.c.ue.find('A', class_='Pawn', from_=24)
        self.assertEqual(self.sent(), [
            C('read', {'addr': 16, 'as': 'cstr'}), C('read', {'addr': '0x10#1:1+0x538', 'as': 'u8 phase, pad 1'}),
            C('write', {'addr': 4096, 'data': '0000803f'}), C('search', {'type': 'text', 'value': 'Fatal error', 'max': 1}),
            C('sig-file', {'file': 'my sigs.jsonl'}),
            C('disasm', {'addr': 'Game.exe+0x10', 'count': 8, 'function': True, 'follow': True}),
            C('xrefs', {'addr': 'base+0x10', 'module': 'engine.dll', 'all': True}),
            C('touches', {'addr': 'Game.exe+0x10', 'offsets': [0x538, 0x540], 'range': 0x1000}),
            C('find', {'text': 'A', 'class': 'Pawn', 'from': 24})])

    def test_helpers_convert_and_say_so(self):
        self.assertEqual(dl.f32(self.session.read_bytes(0x1000, 4)), 1.0)
        self.assertEqual(self.session.read_struct(0x1000, 'f'), (1.0,))
        tick, items = self.session.read_batch_bytes([(0x1000, 2), ('base+0x10', 4)])
        self.assertEqual((tick, items), (7, [b'\x01\x02', None]))
        self.assertEqual(self.session.get_values(REF, 'A.B'), 1.5)
        with self.assertRaises(dl.ReAgentLiveError):
            self.session.get_values(REF, 'A.B', 'C')
        self.assertEqual(self.session.name_map([5, 6]), {5: 'Five', 6: None})
        self.assertEqual(self.session.call_bytes(REF, 'Pawn:Fn', b'\x01'), b'\x01\x02')
        items, same_tick = self.session.batch_results([('read', {'addr': 16, 'len': 1}),
                                                       {'verb': 'get', 'args': {'object': REF, 'paths': ['X']}}])
        self.assertTrue(same_tick)
        self.assertEqual(items[0]['hex'], '01')
        with self.assertRaises(dl.ResultKeyError):
            items[0]['bytes']                            # A batch item is as strict as a single command's result.
        self.assertIsInstance(items[1], dl.ReAgentLiveError)

    def test_write_expect_failure_raises_with_found_bytes(self):
        with self.assertRaises(dl.ReAgentLiveError) as caught:
            self.c.write(0x1000, b'\x01\x02', expect=b'\xff\xff')
        self.assertEqual(caught.exception.error['found'], '00 00')

    def test_pins_trials_and_watches(self):
        self.assertEqual(self.c.pin('0x10#1:1+0x3e4', dl.pack('f32', 1.0), ms=2500, expect=bytes(4))['id'], 1)
        self.assertEqual(self.c.pin_list()['rows'][0]['state'], 'active')
        self.assertEqual(self.c.pin_clear(1)['cleared'], 1)
        row = self.c.trial('0x10#1:1+0x28', dl.pack('f64', 2.0), owner=REF, expect=dl.pack('f64', 1.0), seconds=5,
                           object=0x2000)
        self.assertIsNone(row['guard'])
        self.assertEqual(self.c.trial_clear(1)['rows'][0]['state'], 'restored')
        result = self.c.watch(1.5, ['0x10#1:1+0x3e4:4', '0x9000:1'], then='call 0x10#1:1 Fn 01')
        rows = dl.samples(result)
        self.assertEqual(rows[1]['values'], [bytes([2, 0]), bytes([255])])
        self.assertEqual(self.c.watch_start(600, ['0x538:16'], every=10, follow='pawn')['capacity'], 9000)
        rows = dl.samples(self.c.watch_read())
        self.assertEqual(rows[2], {'tick': 12, 'us': 20, 'values': [b'\x02\x00'], 'resumed': True, 'root': '0x20'})

    def test_pack_verbs(self):
        self.assertEqual(self.c.ue.find('A', class_='Pawn')['total'], 2)
        self.assertEqual(self.c.ue.dump(REF, changed=True)['rows'][0]['before'], 5)
        self.c.ue.dump(REF, match='Ammo', changed='ctrl.snap')
        self.assertEqual(self.c.ue.census_diff(class_='Actor')['rows'][0]['after'], 2)
        self.assertEqual(self.c.ue.describe(REF)['class_size'], 1264)
        typed = self.c.ue.call(REF, 'Pawn:Get', ['Name=#5', 'Mode=2'])
        self.assertTrue(typed['kept'])
        self.assertEqual(self.c.ue.call(16, 'Pawn:Get', typed=True)['mode'], 'typed')
        with self.assertRaises(dl.ReAgentLiveError) as caught:
            self.c.ue.refs('Missing')
        self.assertEqual(caught.exception.error['kind'], 'validation')

    def test_errors_raise(self):
        with self.assertRaises(dl.ReAgentLiveError):
            self.session.ok('bogus')
        with self.assertRaises(dl.ReAgentLiveError):
            self.session.call('bogus')
        self.assertEqual(self.c.status()['packs'], ['ue'])

    def test_the_generator_runs_from_a_console(self):
        replies = os.environ['REAGENTLIVE_FAKE_REPLIES']
        out = os.path.join(self.work.name, 'from_console.py')
        console = os.path.join(self.work.name, 'console.cmd')
        with open(console, 'w') as f:
            f.write(f'@"{sys.executable}" -I "{FAKE}" %*\n')
        self.assertEqual(os.environ['REAGENTLIVE_FAKE_REPLIES'], replies)
        reagentlive_gen.main(['--console', console, '-o', out])
        self.assertEqual(load(out).TABLE_HASH, self.table['table_hash'])


if __name__ == '__main__':
    args = sys.argv[1:]
    if args[:1] == ['--table']:
        TABLE_TOOL = os.path.abspath(args[1])
        args = args[2:]
        # The class decorator read TABLE_TOOL before this ran.
        SessionTests.__unittest_skip__ = False
    unittest.main(argv=[sys.argv[0], *args])
