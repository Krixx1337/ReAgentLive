"""reagentlive_session against a scripted fake console (no agent or game)."""
import os
import sys
import tempfile
import textwrap
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'src', 'reagentlive', 'frontends', 'python'))
import reagentlive_session as dl  # noqa: E402

# Replies by verb; records every line it receives in LOG.
FAKE = textwrap.dedent('''
    import json, os, sys
    log = open(os.environ['REAGENTLIVE_FAKE_LOG'], 'a')
    def reply(ok, result=None, error=None):
        out = {'command': 'x', 'ok': ok}
        out['result' if ok else 'error'] = result if ok else error
        print(json.dumps(out), flush=True)
    for line in sys.stdin:
        line = line.strip(); log.write(line + '\\n'); log.flush()
        verb = line.split(' ')[0]
        if verb == 'quit': break
        if verb == 'read' and '--as "' in line: reply(True, {'hex': '01 00', 'values': {'phase': 1}})
        elif verb == 'read' and '--as' in line: reply(True, {'hex': '41 00', 'text': 'A', 'length': 1, 'terminated': True})
        elif verb == 'read' and '->' in line: reply(True, {'hex': '01 02', 'chain': {'base': '0x10', 'hops': [], 'address': '0x18'}})
        elif verb == 'read': reply(True, {'hex': '00 00 80 3f'})
        elif verb == 'read-batch': reply(True, {'tick': 7, 'items': [{'ok': True, 'hex': '01 02'}, {'ok': False}]})
        elif line == 'write --trial list': reply(True, {'trials': [{'id': 1, 'state': 'active'}]})
        elif line.startswith('write --trial clear'): reply(True, {'cleared': 1, 'trials': [{'id': 1, 'state': 'restored'}]})
        elif verb == 'write' and '--trial' in line: reply(True, {'id': 1, 'state': 'active'})
        elif verb == 'write' and '--expect ffff' in line: reply(False, error={'message': 'memory differs', 'found': '00 00'})
        elif verb == 'write': reply(True, {})
        elif verb == 'search' and '--from' not in line: reply(True, {'hits': [{'address': '0x10'}, {'address': '0x20', 'module': 'Game.exe', 'rva': '0x20'}], 'count': 2, 'resume': '0x21'})
        elif verb == 'search': reply(True, {'hits': [{'address': '0x30'}], 'count': 1, 'resume': None})
        elif verb == 'pin' and line == 'pin list': reply(True, {'pins': [{'id': 1, 'state': 'active'}]})
        elif verb == 'pin' and line.startswith('pin clear'): reply(True, {'cleared': 1})
        elif verb == 'pin': reply(True, {'id': 1, 'state': 'active', 'writes': 1})
        elif line == 'watch --read' or line == 'watch --stop': reply(True, {'running': line.endswith('read'), 'stopped': None, 'samples': [{'tick': 5, 'us': 0, 'values': ['01 00'], 'root': '0x10'}, {'tick': 9, 'us': 10, 'lost': True}, {'tick': 12, 'us': 20, 'values': ['02 00'], 'resumed': True, 'root': '0x20'}], 'gaps': [{'from_us': 10, 'to_us': 20}]})
        elif verb == 'watch' and '--start' in line: reply(True, {'started': True, 'capacity': 9000})
        elif verb == 'watch': reply(True, {'rows': 2, 'complete': True, 'samples': [{'tick': 5, 'us': 0, 'values': ['01 00', None]}, {'tick': 6, 'us': 9000, 'values': ['02 00', 'ff']}]})
        elif verb == 'name': reply(True, {'names': [{'index': 5, 'name': 'Five'}, {'index': 6, 'name': None}]})
        elif verb == 'get': reply(True, {'values': [{'path': 'A.B', 'ok': True, 'value': 1.5}, {'path': 'C', 'ok': False, 'error': 'no field C'}][:len(line.split(' ')) - 2]})
        elif verb == 'find' and '--from 24' in line: reply(True, {'objects': [{'ref': '0x20#2:1', 'name': 'B', 'class': 'Pawn'}], 'total': None, 'from': 24, 'next_cursor': None, 'truncated': True})
        elif verb == 'find': reply(True, {'objects': [{'ref': '0x10#1:1', 'name': 'A', 'class': 'Pawn'}], 'total': 2, 'from': 0, 'next_cursor': 24, 'truncated': True})
        elif verb == 'funcs' and '--from' in line: reply(True, {'ref': '0x10#1:1', 'functions': [{'owner': 'Actor', 'name': 'B'}], 'from': 32, 'next_cursor': None, 'truncated': True})
        elif verb == 'funcs': reply(True, {'ref': '0x10#1:1', 'functions': [{'owner': 'Pawn', 'name': 'A'}], 'from': 0, 'next_cursor': 32, 'truncated': True})
        elif verb == 'name-find' and '--from 12' in line: reply(True, {'names': [{'index': 11, 'name': 'Tag.C'}], 'from': 12, 'next_cursor': None})
        elif verb == 'name-find' and '--from' in line: reply(True, {'names': [{'index': 9, 'name': 'Tag.B'}], 'from': 8, 'next_cursor': 12})
        elif verb == 'name-find': reply(True, {'names': [{'index': 5, 'name': 'Tag.A'}], 'from': 0, 'next_cursor': 8})
        elif verb == 'refs' and 'Missing' in line: reply(False, error={'kind': 'not found', 'message': 'no such name'})
        elif verb == 'refs': reply(True, {'index': 5, 'name': 'Tag.A', 'functions': [{'ref': '0x40#4:1', 'owner': 'BP', 'name': 'Go'}], 'capped': False})
        elif verb == 'modules': reply(True, {'modules': [{'name': 'Game.exe', 'base': '0x140000000', 'size': 4096}]})
        elif verb == 'export-labels': reply(True, {'path': line.split(' ')[1], 'rows': 3, 'capped': False})
        elif verb == 'label': reply(True, {'path': 'c.native.txt', 'line': 1, 'owner': 'Zone', 'name': line.split(' ')[4]})
        elif verb == 'snap': reply(True, {'ref': '0x10#1:1', 'size': 512, 'path': (line.split(' ')[2:] or [None])[0]})
        elif verb == 'record': reply(True, {'ref': '0x10#1:1', 'path': line.split(' ')[3], 'samples': 40})
        elif verb == 'each' and 'Big' in line: reply(True, {'capped': True, 'count': 1024, 'rows': [{'ref': '0x10#1:1', 'value': 1}]})
        elif verb == 'each': reply(True, dict({'capped': False, 'count': 1, 'rows': [{'ref': '0x10#1:1', 'value': 1, 'hex': '01'}]},
                                              **({'polls': 9, 'changes': [{'ref': '0x10#1:1', 'before': 1, 'after': 2}]} if '--seconds' in line else {})))
        elif verb == 'release': reply(True, {'mode': 'release', 'kept': False, 'hex': ''})
        elif verb == 'call' and ('=' in line or line.endswith('--typed')): reply(True, {'mode': 'typed', 'hex': '01', 'kept': True, 'outputs': [{'name': 'ReturnValue', 'value': 'x'}]})
        elif verb == 'call': reply(True, {'mode': 'raw', 'hex': '01 02', 'kept': False})
        elif verb == 'func' and line.endswith('--script'): reply(True, {'ref': '0x10#1:1', 'size': 64, 'names': [], 'functions': [], 'properties': []})
        elif verb == 'dump' and '--changed' in line: reply(True, {'ref': '0x10#1:1', 'fields': [{'name': 'Ammo', 'value': 4, 'before': 5}], 'truncated': False})
        elif verb in ('fields', 'dump'): reply(True, {'ref': '0x10#1:1', 'fields': [{'name': 'Health', 'offset': 256}], 'truncated': False})
        elif verb == 'entries' and '--from' in line: reply(True, {'kind': 'map', 'entries': [{'key': 'B', 'value': 2.0}], 'next_cursor': None})
        elif verb == 'entries': reply(True, {'kind': 'map', 'entries': [{'key': 'A', 'value': 1.0}], 'next_cursor': 7})
        elif verb == 'census' and '--changed' in line: reply(True, {'rows': [{'class': 'Proj', 'ref': '0x30#3:1', 'before': 0, 'after': 2, 'samples': []}]})
        elif verb == 'census' and 'Zone' in line and '--from 1' in line: reply(True, {'rows': [{'class': 'ZoneB', 'ref': '0x50#5:1', 'count': 1, 'samples': []}], 'next_cursor': None})
        elif verb == 'census' and 'Zone' in line: reply(True, {'rows': [{'class': 'ZoneA', 'ref': '0x40#4:1', 'count': 2, 'samples': []}], 'next_cursor': 1})
        elif verb == 'census': reply(True, {'rows': [{'class': 'Pawn', 'ref': '0x20#2:1', 'count': 3, 'samples': []}]})
        elif verb == 'peek': reply(True, {'address': '0x10', 'count': 1, 'rows': [{'address': '0x10', 'value': '0x5', 'label': 'value'}]})
        elif verb in ('describe', 'func', 'params'): reply(True, {'ref': '0x10#1:1', 'verb': verb})
        elif verb == 'sig': reply(True, {'unique': True, 'count': 1, 'resolves': True})
        elif verb == 'disasm': reply(True, {'address': '0x10', 'count': 1, 'stop': 'count', 'function': None, 'instructions': [{'address': '0x10', 'text': 'ret', 'ends': True}]})
        elif verb == 'region': reply(True, {'address': '0x10', 'kind': 'code', 'section': '.text'})
        elif verb == 'vtable': reply(True, {'address': '0x10', 'table': '0x20', 'count': 1, 'stop': 'not code', 'slots': [{'slot': 0, 'target': '0x30'}]})
        elif verb == 'xrefs': reply(True, {'address': '0x10', 'count': {'call': 1}, 'rows': [{'kind': 'call', 'field': '0x21', 'instruction': '0x20', 'function': None}], 'truncated': False})
        elif verb == 'touches': reply(True, {'offsets': ['0x538'], 'count': 1, 'rows': [{'function': {'start': '0x20'}, 'distinct': 1, 'uses': []}], 'truncated': False})
        elif verb == 'batch': reply(True, {'count': 2, 'same_tick': True, 'results': [{'command': 'read', 'ok': True, 'result': {'hex': '01'}}, {'command': 'get', 'ok': False, 'error': {'message': 'no field X'}}]})
        else: reply(False, error={'message': 'unknown'})
''')


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.script = os.path.join(self.dir.name, 'fake_console.py')
        with open(self.script, 'w') as f:
            f.write(FAKE)
        self.log = os.path.join(self.dir.name, 'lines.txt')
        os.environ['REAGENTLIVE_FAKE_LOG'] = self.log
        self.session = dl.Session(sys.executable, ['-I', self.script])

    def tearDown(self):
        self.session.close()
        self.dir.cleanup()

    def lines(self):
        with open(self.log) as f:
            return [line.strip() for line in f]

    def test_sig_check_lines(self):
        self.assertTrue(self.session.sig_check('48 8b ?? 89 05', expect='Game.exe+0x10', offset=4)['resolves'])
        self.session.sig_check([0x48, 0x8b, None, -1, 0x89], module='engine.dll')
        self.session.sig_check('48 8d 0d ?? ?? ?? ??', expect=0x1000, resolve=(7, 3))
        self.session.sig_check_file('sigs.jsonl')
        self.session.sig_bytes('base+0x10', 32)
        self.assertEqual(self.lines(), ['sig --check 48 8b ?? 89 05 --expect Game.exe+0x10 --offset 4',
                                        'sig --check 48 8b ?? ?? 89 --module engine.dll',
                                        'sig --check 48 8d 0d ?? ?? ?? ?? --expect 0x1000 --resolve 7,3',
                                        'sig --check --file sigs.jsonl', 'sig --bytes base+0x10 32'])

    def test_fast_lane_lines(self):
        rows = self.session.disasm('Game.exe+0x10', 8, function=True, follow=True)
        self.assertEqual(rows[0]['text'], 'ret')
        self.session.disasm(0x10)
        result = self.session.xrefs('base+0x10', module='engine.dll', all=True)
        self.assertEqual(result['rows'][0]['kind'], 'call')
        self.session.xrefs(0x10)
        self.assertEqual(self.session.touches('Game.exe+0x10', 0x538, 0x540, range=0x1000)['rows'][0]['distinct'], 1)
        self.assertEqual(self.lines(), ['disasm Game.exe+0x10 8 --function --follow', 'disasm 0x10 24',
                                        'xrefs base+0x10 --module engine.dll --all', 'xrefs 0x10',
                                        'touches Game.exe+0x10 0x538 0x540 --range 0x1000'])

    def test_chains_and_strings(self):
        data, chain = self.session.read_chain('$1->0x30->0x8', 2)
        self.assertEqual((data, chain['address']), (b'\x01\x02', '0x18'))
        self.assertEqual(self.session.read_str(0x10), {'text': 'A', 'length': 1, 'terminated': True})
        self.session.read_str('base+0x10', wide=True, length=64)
        self.assertEqual(self.lines(), ['read $1->0x30->0x8 2', 'read 0x10 --as cstr', 'read base+0x10 64 --as wstr'])

    def test_region_line(self):
        self.assertEqual(self.session.region('base+0x10')['kind'], 'code')
        self.assertEqual(self.lines(), ['region base+0x10'])

    def test_vtable_lines(self):
        self.assertEqual(self.session.vtable('$1', 4, offset=0x620, resolve=True)['slots'][0]['slot'], 0)
        self.session.vtable(0x20, table=True)
        self.assertEqual(self.lines(), ['vtable $1 4 --offset 1568 --resolve', 'vtable 0x20 --table'])

    def test_reads_decode(self):
        self.assertEqual(dl.f32(self.session.read(0x1000, 4)), 1.0)
        tick, items = self.session.read_batch([(0x1000, 2), ('base+0x10', 4)])
        self.assertEqual((tick, items), (7, [b'\x01\x02', None]))
        self.assertIn('read-batch 0x1000 2 base+0x10 4', self.lines())

    def test_search_pages_until_done(self):
        hits = self.session.search('ptr', 0x7ff600001234, heap=True, background=True, module='Game.exe')
        self.assertEqual(hits, [0x10, 0x20, 0x30])
        first, second = [line for line in self.lines() if line.startswith('search')]
        self.assertEqual(first, 'search ptr 0x7ff600001234 --heap --background --module Game.exe --max 512')
        self.assertTrue(second.endswith('--from 0x21'))

    def test_search_limit_stops_early(self):
        self.assertEqual(self.session.search('f32', 100.0, limit=2), [0x10, 0x20])
        self.assertIn('search f32 100.0 --max 2', self.lines())

    def test_write_expect_failure_raises_with_found_bytes(self):
        self.session.write(0x1000, dl.pack('f32', 1.0))
        with self.assertRaises(dl.ReAgentLiveError) as caught:
            self.session.write(0x1000, b'\x01\x02', expect=b'\xff\xff')
        self.assertEqual(caught.exception.error['found'], '00 00')
        self.assertIn('write 0x1000 0000803f', self.lines())

    def test_pin_and_watch(self):
        pin = self.session.pin('$1+0x3e4', dl.pack('f32', 1.0), seconds=2.5, expect=bytes(4))
        self.assertEqual(pin['id'], 1)
        self.assertIn('pin $1+0x3e4 0000803f --ms 2500 --expect 00000000', self.lines())
        self.assertEqual(self.session.pins()[0]['state'], 'active')
        self.assertEqual(self.session.unpin(1), 1)
        self.assertIn('pin clear 1', self.lines())
        samples, result = self.session.watch(1.5, [('$1+0x3e4', 4), (0x9000, 1)], then='call $1 Fn 01')
        self.assertTrue(result['complete'])
        self.assertEqual(samples[0]['values'], [bytes([1, 0]), None])
        self.assertEqual(samples[1]['values'], [bytes([2, 0]), bytes([255])])
        self.assertIn('watch 1500ms $1+0x3e4:4 0x9000:1 --then call $1 Fn 01', self.lines())
        self.assertTrue(self.session.watch_start(600, [(0x538, 16)], every=10, follow='pawn')['started'])
        self.assertIn('watch --start 600000ms --every 10000ms --follow pawn 0x538:16', self.lines())
        samples, result = self.session.watch_read()
        self.assertEqual([s.get('lost', False) for s in samples], [False, True, False])
        self.assertEqual(samples[2]['values'], [b'\x02\x00'])
        self.assertTrue(samples[2]['resumed'])
        self.assertEqual(result['gaps'], [{'from_us': 10, 'to_us': 20}])
        self.assertFalse(self.session.watch_stop()[1]['running'])

    def test_read_struct_get_and_batch(self):
        self.assertEqual(self.session.read_struct(0x1000, 'f'), (1.0,))
        self.assertEqual(self.session.read_layout('$1+0x538', 'u8 phase, pad 1'), {'phase': 1})
        self.assertEqual(self.session.label('$1', 0x538, 'f64[3]', 'Start')['name'], 'Start')
        self.assertIn('label $1 0x538 f64[3] Start', self.lines())
        self.assertIn('read $1+0x538 --as "u8 phase, pad 1"', self.lines())
        self.assertIn('read 0x1000 4', self.lines())
        self.assertEqual(self.session.get('$1', 'A.B'), 1.5)
        self.assertIn('get $1 A.B', self.lines())
        with self.assertRaises(dl.ReAgentLiveError):
            self.session.get('$1', 'A.B', 'C')
        items, same_tick = self.session.batch(['read 0x10 1', 'get $1 X'])
        self.assertTrue(same_tick)
        self.assertEqual(items[0], {'hex': '01'})
        self.assertIsInstance(items[1], dl.ReAgentLiveError)
        self.assertIn('batch { read 0x10 1; get $1 X }', self.lines())

    def test_lookups_return_documented_rows(self):
        self.assertEqual([o['name'] for o in self.session.find('A', cls='Pawn')], ['A'])
        self.assertIn('find A --class Pawn', self.lines())
        self.assertEqual([o['name'] for o in self.session.find(cls='Pawn', pages=None)], ['A', 'B'])
        self.assertIn('find --class Pawn --from 24', self.lines())
        functions = self.session.funcs('0x10#1:1', 'Get')
        self.assertEqual(functions, [{'owner': 'Pawn', 'name': 'A'}, {'owner': 'Actor', 'name': 'B'}])
        self.assertIn('funcs 0x10#1:1 Get --from 32', self.lines())
        self.assertEqual(self.session.fields(0x10)[0]['offset'], 256)
        self.assertEqual(self.session.dump('$1', match='Health', nonzero=True)[0]['name'], 'Health')
        self.assertIn('dump $1 --match Health --nonzero', self.lines())
        self.assertEqual(self.session.census('Actor', match='Pawn', limit=0)[0]['count'], 3)
        self.assertIn('census --class Actor --match Pawn --limit 0', self.lines())
        self.assertEqual(self.session.census('Actor', changed=True)[0]['after'], 2)
        self.assertIn('census --class Actor --changed', self.lines())
        self.assertEqual([r['class'] for r in self.session.census(match='Zone', limit=1)], ['ZoneA', 'ZoneB'])
        self.assertIn('census --match Zone --limit 1 --from 1', self.lines())
        self.assertEqual(len(self.session.census(match='Zone', limit=1, pages=1)), 1)
        self.assertEqual(self.session.describe('$1')['verb'], 'describe')
        self.assertEqual(self.session.func('$1', 'Pawn:A')['verb'], 'func')
        self.assertEqual(self.session.params('$1', 'Pawn:A')['verb'], 'params')
        self.assertIn('params $1 Pawn:A', self.lines())
        self.assertEqual(self.session.peek('base+0x10', 1, label='labels.jsonl')[0]['label'], 'value')
        self.assertEqual([r['key'] for r in self.session.entries('$1', 'Attributes')], ['A', 'B'])
        self.assertIn('entries $1 Attributes --from 7', self.lines())
        self.assertIn('peek base+0x10 1 --label labels.jsonl', self.lines())

    def test_trials(self):
        row = self.session.trial('$1+0x28', dl.pack('f64', 2.0), '0x10#1:1', dl.pack('f64', 1.0), seconds=5, object=0x2000)
        self.assertEqual(row['state'], 'active')
        self.assertIn('write $1+0x28 0000000000000040 --trial --owner 0x10#1:1 --object 0x2000 '
                      '--expect 000000000000f03f --ms 5000', self.lines())
        self.assertEqual(self.session.trials()[0]['state'], 'active')
        self.assertEqual(self.session.end_trial(1)[0]['state'], 'restored')
        self.assertIn('write --trial clear 1', self.lines())

    def test_name_find_pages(self):
        self.assertEqual(self.session.name_find('Tag'), [{'index': 5, 'name': 'Tag.A'}])
        self.assertEqual([r['index'] for r in self.session.name_find('Tag', pages=None)], [5, 9, 11])
        self.assertEqual([r['index'] for r in self.session.name_find('Tag', pages=2)], [5, 9])
        self.assertEqual(self.lines(), ['name-find Tag', 'name-find Tag', 'name-find Tag --from 8', 'name-find Tag --from 12',
                                        'name-find Tag', 'name-find Tag --from 8'])

    def test_refs_modules_script_labels(self):
        self.assertFalse(self.session.refs('Tag.A')['capped'])
        self.assertEqual(self.session.refs(5)['functions'][0]['name'], 'Go')
        self.assertEqual(self.session.modules()[0]['name'], 'Game.exe')
        self.assertEqual(self.session.script('$1', 'BP:Go')['size'], 64)
        self.assertEqual(self.session.export_labels('l.jsonl')['rows'], 3)
        self.session.export_labels('l.jsonl', module='engine.dll', kinds=['exec', 'global'])
        self.session.export_labels('l.jsonl', kinds='vtable')
        self.assertEqual(self.lines(), ['refs Tag.A', 'refs #5', 'modules', 'func $1 BP:Go --script', 'export-labels l.jsonl',
                                        'export-labels l.jsonl --module engine.dll --kinds exec,global',
                                        'export-labels l.jsonl --kinds vtable'])

    def test_snap_dump_changed_and_record(self):
        self.assertIsNone(self.session.snap('$1')['path'])
        self.assertEqual(self.session.snap('0x10#1:1', 'ctrl.snap')['path'], 'ctrl.snap')
        self.assertEqual(self.session.dump('$1', changed=True)[0]['before'], 5)
        self.session.dump('$1', match='Ammo', changed='ctrl.snap')
        self.session.dump('$1', changed=False)
        self.assertEqual(self.session.record('$1', 'burst.tsv', ['Ammo', 'Weapon:State'], 4), ('burst.tsv', 40))
        self.assertEqual(self.lines(), ['snap $1', 'snap 0x10#1:1 ctrl.snap', 'dump $1 --changed',
                                        'dump $1 --match Ammo --changed ctrl.snap', 'dump $1',
                                        'record $1 4 burst.tsv Ammo Weapon:State'])

    def test_each_rows_seconds_and_capped(self):
        self.assertEqual(self.session.each('Pawn', 'Health')[0]['value'], 1)
        rows, changes = self.session.each('Pawn', 'Pawn:GetHealth', seconds=3)
        self.assertEqual((rows[0]['hex'], changes[0]['after']), ('01', 2))
        with self.assertRaises(dl.ReAgentLiveError) as caught:
            self.session.each('Big', 'Health')
        self.assertEqual(len(caught.exception.error['result']['rows']), 1)
        self.assertEqual(self.lines(), ['each Pawn Health', 'each Pawn Pawn:GetHealth --seconds 3', 'each Big Health'])

    def test_call_raw_typed_and_release(self):
        self.assertEqual(self.session.call('$1', 'Pawn:Fn', b'\x01'), b'\x01\x02')
        self.assertEqual(self.session.call('$1', 'Pawn:Fn'), b'\x01\x02')
        typed = self.session.call('$1', 'Pawn:Get', args={'Name': '#5', 'Mode': 2})
        self.assertTrue(typed['kept'])
        self.assertEqual(typed['outputs'][0]['name'], 'ReturnValue')
        self.assertEqual(self.session.call(0x10, 'Pawn:Get', args={})['mode'], 'typed')
        self.assertEqual(self.session.release('$1', 'Pawn:Get')['mode'], 'release')
        self.assertEqual(self.lines(), ['call $1 Pawn:Fn 01', 'call $1 Pawn:Fn', 'call $1 Pawn:Get Name=#5 Mode=2',
                                        'call 0x10 Pawn:Get --typed', 'release $1 Pawn:Get'])

    def test_new_method_error_raises(self):
        with self.assertRaises(dl.ReAgentLiveError) as caught:
            self.session.refs('Missing')
        self.assertEqual(caught.exception.error['kind'], 'not found')

    def test_names_and_errors(self):
        self.assertEqual(self.session.names([5, 6]), {5: 'Five', 6: None})
        with self.assertRaises(dl.ReAgentLiveError):
            self.session.ok('bogus')


if __name__ == '__main__':
    unittest.main()
