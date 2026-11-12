"""reagentlive_session against a scripted fake console (no agent or game).

The fake answers `help --json` with the real verb table (printed by the
reagentlive-table tool: pass it as --table <exe>, or set REAGENTLIVE_TABLE),
and every other line with a canned reply. A test checks each canned result
against its verb's type in that table, so the replies cannot drift from the
console's real shapes.
"""
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'src', 'reagentlive', 'frontends', 'python'))
import reagentlive_session as dl  # noqa: E402

TABLE_TOOL = os.environ.get('REAGENTLIVE_TABLE') and os.path.abspath(os.environ['REAGENTLIVE_TABLE'])

# Replies by exact line; records every line it receives in the log.
FAKE = textwrap.dedent('''
    import json, os, sys
    replies = json.load(open(os.environ['REAGENTLIVE_FAKE_REPLIES']))
    log = open(os.environ['REAGENTLIVE_FAKE_LOG'], 'a')
    for line in sys.stdin:
        line = line.strip()
        if line != 'help --json': log.write(line + '\\n'); log.flush()
        if line == 'quit': break
        reply = replies.get(line, {'ok': False, 'error': {'kind': 'syntax', 'message': 'unknown'}})
        print(json.dumps(dict(reply, command=line.split(' ')[0])), flush=True)
''')

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
CENSUS = {'classes': 3, 'instances': 9, 'ms': 4, 'total': 2, 'from': 0}


def ok(result):
    return {'ok': True, 'result': result}


def error(**fields):
    return {'ok': False, 'error': fields}


REPLIES = {
    'status': ok({'pid': 4, 'build': 'b', 'version': '0.1.0', 'replaced_agents': 0, 'ticks': 9, 'ticks_per_second': 60,
                  'interval_ms': 100, 'ticking': True, 'host_thread': 'engine', 'packs': ['ue'], 'table_hash': 'abc'}),
    'region base+0x10': ok({'address': '0x10', 'kind': 'code', 'section': '.text', 'base': '0x0', 'size': '0x1000',
                            'allocation_base': '0x0', 'state': 'commit', 'type': 'image', 'protect': 'r-x',
                            'allocation_protect': 'rwx'}),
    'read 0x1000 4': ok({'address': '0x1000', 'size': 4, 'hex': '00 00 80 3f'}),
    'read $1->0x30->0x8 2': ok({'address': '0x18', 'size': 2, 'hex': '01 02',
                                'chain': {'base': '0x10', 'hops': [], 'address': '0x18'}}),
    'read 0x10 --as cstr': ok({'address': '0x10', 'size': 2, 'hex': '41 00', 'text': 'A', 'length': 1, 'terminated': True}),
    'read $1+0x538 --as "u8 phase, pad 1"': ok({'address': '0x538', 'size': 2, 'hex': '01 00', 'layout': {'phase': 1}}),
    'read-batch 0x1000 2 base+0x10 4': ok({'tick': 7, 'rows': [
        {'address': '0x1000', 'size': 2, 'ok': True, 'hex': '01 02'},
        {'address': '0x10', 'size': 4, 'ok': False, 'error': {'status': 3, 'message': 'unreadable'}}]}),
    'peek base+0x10 1 --label labels.jsonl': ok({'address': '0x10', 'rows': [
        {'address': '0x10', 'value': '0x5', 'label': 'value'}]}),
    'write 0x1000 0000803f': ok({'address': '0x1000', 'size': 4}),
    'write 0x1000 0102 --expect ffff': error(kind='agent', message='memory differs', found='00 00'),
    'search ptr 0x7ff600001234 --heap --background --module Game.exe --max 512': ok({'rows': [
        {'address': '0x10'}, {'address': '0x20', 'module': 'Game.exe', 'rva': '0x20'}], 'from': '0x0', 'next_cursor': '0x21'}),
    'search ptr 0x7ff600001234 --heap --background --module Game.exe --max 512 --from 0x21': ok({
        'rows': [{'address': '0x30'}], 'from': '0x21'}),
    'search f32 100.0 --max 2': ok({'rows': [{'address': '0x10'}, {'address': '0x20'}], 'from': '0x0', 'next_cursor': '0x21'}),
    'search text "Fatal error" --max 1': ok({'rows': [], 'from': '0x0'}),
    'batch { read 0x10 1; get $1 X }': ok({'all_ok': False, 'same_tick': True, 'first_tick': 4, 'last_tick': 4,
                                           'spans_ticks': [], 'rows': [
        {'command': 'read', 'ok': True, 'result': {'address': '0x10', 'size': 1, 'hex': '01'}, 'tick': 4},
        {'command': 'get', 'ok': False, 'error': {'message': 'no field X'}}]}),
    'modules': ok({'rows': [{'name': 'Game.exe', 'base': '0x140000000', 'size': 4096}]}),
    'sig-check 48 8b ?? 89 05 --expect Game.exe+0x10 --offset 4': ok(SIG),
    'sig-check 48 8b ?? ?? 89 --module engine.dll': ok(SIG),
    'sig-check 48 8d 0d ?? ?? ?? ?? --expect 0x1000 --resolve 7,3': ok(SIG),
    'sig-file sigs.jsonl': ok({'file': 'sigs.jsonl', 'winner': 2, 'searches': 1, 'rows': [
        {'line': 1, 'ok': False, 'error': 'no pattern'}, {'line': 2, 'ok': True, 'result': SIG}]}),
    'sig-bytes base+0x10 32': ok({'address': '0x10', 'length': 2, 'pattern': '48 8b', 'list': '0x48, 0x8B',
                                  'suggested': {'pattern': '48 8b', 'list': '0x48, 0x8B', 'fixed_bytes': 2, 'fields': []}}),
    'disasm Game.exe+0x10 8 --function --follow': ok({'address': '0x10', 'size': 1, 'stop': 'count', 'rows': [
        {'address': '0x10', 'bytes': 'c3', 'text': 'ret', 'ends': True}]}),
    'disasm 0x10 24': ok({'address': '0x10', 'size': 0, 'stop': 'count', 'rows': []}),
    'xrefs base+0x10 --module engine.dll --all': ok({'address': '0x10', 'scanned': 'engine.dll', 'kinds': {'call': 1},
                                                    'rows': [{'kind': 'call', 'field': '0x21', 'instruction': '0x20'}],
                                                    'capped': False, 'scan_ms': 3}),
    'touches Game.exe+0x10 0x538 0x540 --range 0x1000': ok({
        'address': '0x10', 'offsets': ['0x538', '0x540'], 'range': 4096,
        'scanned': {'from': '0x0', 'to': '0x1010', 'functions': 2, 'bytes': 64}, 'total': 1, 'capped': False, 'scan_ms': 1,
        'rows': [{'function': {'start': '0x20', 'end': '0x40'}, 'distinct': 1, 'writes': False, 'total': 0, 'uses': []}]}),
    'vtable $1 4 --offset 1568 --resolve': ok({'address': '0x10', 'offset': '0x620', 'table': '0x20', 'stop': 'not code',
                                              'rows': [{'slot': 0, 'offset': '0x0', 'target': '0x30'}]}),
    'vtable 0x20 --table': ok({'address': '0x20', 'offset': '0x0', 'rows': []}),
    'pin $1+0x3e4 0000803f --ms 2500 --expect 00000000': ok(PIN),
    'pin-list': ok({'rows': [PIN]}),
    'pin-clear 1': ok({'cleared': 1}),
    'watch 1500ms $1+0x3e4:4 0x9000:1 --then call $1 Fn 01': ok({
        'capacity': 100, 'complete': True, 'ranges': [RANGE], 'then': {'ok': True, 'hex': '01'},
        'rows': [{'tick': 5, 'us': 0, 'values': ['01 00', None]}, {'tick': 6, 'us': 9000, 'values': ['02 00', 'ff']}]}),
    'watch-start 600000ms 0x538:16 --every 10000ms --follow pawn': ok({
        'capacity': 9000, 'ms': 600000, 'interval_ms': 10000, 'root': 'pawn',
        'ranges': [{'offset': '0x538', 'length': 16, 'guarded': True}]}),
    'watch-read': ok({'running': True, 'capacity': 9000, 'ms': 600000, 'interval_ms': 10000, 'root': 'pawn',
                      'ranges': [{'offset': '0x538', 'length': 16, 'guarded': True}],
                      'rows': [{'tick': 5, 'us': 0, 'values': ['01 00'], 'root': '0x10'}, {'tick': 9, 'us': 10, 'lost': True},
                               {'tick': 12, 'us': 20, 'values': ['02 00'], 'resumed': True, 'root': '0x20'}],
                      'gaps': [{'from_us': 10, 'to_us': 20}]}),
    'watch-stop': ok({'running': False, 'stopped': 'stopped', 'capacity': 9000, 'ms': 600000, 'interval_ms': 10000,
                      'ranges': [], 'rows': []}),
    'trial $1+0x28 0000000000000040 --owner 0x10#1:1 --object 0x2000 --expect 000000000000f03f --ms 5000': ok(TRIAL),
    'trial-list': ok({'rows': [TRIAL]}),
    'trial-clear 1': ok({'cleared': 1, 'rows': [dict(TRIAL, state='restored')]}),
    'find A --class Pawn': ok({'rows': [OBJECT], 'total': 2, 'from': 0, 'next_cursor': 24}),
    'find A --class Pawn --from 24': ok({'rows': [dict(OBJECT, name='B')], 'from': 24}),
    'funcs 0x10#1:1 Get': ok({'ref': '0x10#1:1', 'rows': [{'owner': 'Pawn', 'name': 'A'}], 'from': 0, 'next_cursor': 32}),
    'funcs 0x10#1:1 Get --from 32': ok({'ref': '0x10#1:1', 'rows': [{'owner': 'Actor', 'name': 'B'}], 'from': 32}),
    'fields 0x10': ok({'ref': '0x10', 'rows': [FIELD], 'capped': False}),
    'dump $1 --match Health --nonzero': ok({'ref': '0x10#1:1', 'rows': [dict(FIELD, value=1.5, truncated=False)],
                                            'truncated': False, 'capped': False, 'object_lookup_limited': False,
                                            'filtered': {'shown': 1, 'total': 9}}),
    'dump $1 --changed': ok({'ref': '0x10#1:1', 'rows': [dict(FIELD, name='Ammo', value=4, before=5, truncated=False)],
                             'truncated': False, 'capped': False, 'object_lookup_limited': False}),
    'dump $1 --match Ammo --changed ctrl.snap': ok({'ref': '0x10#1:1', 'rows': [], 'truncated': False, 'capped': False,
                                                    'object_lookup_limited': False}),
    'census --class Actor --match Pawn --limit 0': ok(dict(CENSUS, rows=[
        {'class': 'Pawn', 'ref': '0x20#2:1', 'count': 3, 'samples': []}])),
    'census --match Zone --limit 1': ok(dict(CENSUS, next_cursor=1, rows=[
        {'class': 'ZoneA', 'ref': '0x40#4:1', 'count': 2, 'samples': []}])),
    'census --match Zone --limit 1 --from 1': ok(dict(CENSUS, **{'from': 1}, rows=[
        {'class': 'ZoneB', 'ref': '0x50#5:1', 'count': 1, 'samples': []}])),
    'census-diff --class Actor': ok({'classes': {'before': 3, 'after': 4}, 'instances': {'before': 9, 'after': 11}, 'ms': 4,
                                     'total': 1, 'from': 0, 'filter': 'Actor', 'rows': [
        {'class': 'Proj', 'ref': '0x30#3:1', 'before': 0, 'after': 2, 'samples': ['0x90#9:1']}]}),
    'describe $1': ok(dict(OBJECT, class_size=1264)),
    'func $1 Pawn:A': ok({'ref': '0x10#1:1', 'function': 'Pawn:A', 'address': '0x2000', 'flags': 1024, 'executable': True}),
    'params $1 Pawn:A': ok({'ref': '0x10#1:1', 'function': 'Pawn:A', 'address': '0x2000', 'block_size': 8, 'rows': [
        {'name': 'ReturnValue', 'owner': 'A', 'type': 'FloatProperty', 'offset': 0, 'size': 4, 'direction': 'return',
         'flags': 4}]}),
    'entries $1 Attributes': ok({'ref': '0x10#1:1', 'field': 'Attributes', 'kind': 'map', 'total': 2, 'from': 0,
                                 'next_cursor': 7, 'truncated': False, 'rows': [{'key': 'A', 'value': 1.0}]}),
    'entries $1 Attributes --from 7': ok({'ref': '0x10#1:1', 'field': 'Attributes', 'kind': 'map', 'total': 2, 'from': 7,
                                         'truncated': False, 'rows': [{'key': 'B', 'value': 2.0}]}),
    'get $1 A.B': ok({'ref': '0x10#1:1', 'same_tick': True, 'rows': [
        {'path': 'A.B', 'ok': True, 'value': 1.5, 'address': '0x18', 'object': '0x10#1:1', 'truncated': False}]}),
    'get $1 A.B C': ok({'ref': '0x10#1:1', 'same_tick': True, 'rows': [
        {'path': 'A.B', 'ok': True, 'value': 1.5, 'address': '0x18', 'object': '0x10#1:1', 'truncated': False},
        {'path': 'C', 'ok': False, 'error': 'no field C'}]}),
    'label $1 0x538 f64[3] Start': ok({'path': 'c.native.txt', 'line': 1, 'owner': 'Zone', 'offset': 1336, 'type': 'f64[3]',
                                       'name': 'Start', 'size': 24, 'timestamp': 7}),
    'name 5 6': ok({'rows': [{'index': 5, 'name': 'Five'}, {'index': 6}]}),
    'name-find Tag': ok({'rows': [{'index': 5, 'name': 'Tag.A'}], 'from': 0, 'next_cursor': 8}),
    'name-find Tag --from 8': ok({'rows': [{'index': 9, 'name': 'Tag.B'}], 'from': 8, 'next_cursor': 12}),
    'name-find Tag --from 12': ok({'rows': [{'index': 11, 'name': 'Tag.C'}], 'from': 12}),
    'refs Missing': error(kind='validation', message='no pooled name is exactly Missing'),
    'refs Tag.A': ok({'index': 5, 'name': 'Tag.A', 'capped': False, 'rows': [
        {'ref': '0x40#4:1', 'owner': 'BP', 'name': 'Go', 'offset': 3, 'count': 1}]}),
    'refs #5': ok({'index': 5, 'capped': False, 'rows': []}),
    'script $1 BP:Go': ok({'ref': '0x10#1:1', 'function': 'BP:Go', 'size': 64, 'names': [], 'functions': [], 'properties': []}),
    'export-labels l.jsonl': ok({'path': 'l.jsonl', 'module': 'Game.exe', 'timestamp': 7, 'total': 3, 'globals': 1, 'exec': 2,
                                 'vtables': 0, 'interfaces': 0, 'interface_lookups': 0, 'capped': False}),
    'snap $1': ok({'ref': '0x10#1:1', 'size': 512}),
    'snap 0x10#1:1 ctrl.snap': ok({'ref': '0x10#1:1', 'size': 512, 'path': 'ctrl.snap'}),
    'record $1 4 burst.tsv Ammo Weapon:State': ok({'ref': '0x10#1:1', 'path': 'burst.tsv', 'total': 40}),
    'each Pawn Health': ok({'class': 'Pawn', 'class_ref': '0x8#8:1', 'target': 'Pawn:Health', 'mode': 'field',
                            'capped': False, 'truncated': False, 'rows': [{'ref': '0x10#1:1', 'value': 1}]}),
    'each Pawn Pawn:GetHealth --seconds 3': ok({
        'class': 'Pawn', 'class_ref': '0x8#8:1', 'target': 'Pawn:GetHealth', 'mode': 'call', 'capped': False,
        'truncated': False, 'rows': [{'ref': '0x10#1:1', 'value': 1, 'hex': '01'}], 'seconds': 3, 'polls': 9,
        'changes': [{'ms': 100, 'tick': 8, 'ref': '0x10#1:1', 'before': 1, 'after': 2}]}),
    'call $1 Pawn:Fn 01': ok(CALL),
    'call $1 Pawn:Fn': ok(CALL),
    'call $1 Pawn:Get Name=#5 Mode=2': ok(dict(CALL, mode='typed', kept=True, kept_reason='ReturnValue is a String',
                                                truncated=False, outputs=[
        {'name': 'ReturnValue', 'direction': 'return', 'value': 'x', 'truncated': False}])),
    'call 0x10 Pawn:Get --typed': ok(dict(CALL, mode='typed', outputs=[], truncated=False)),
    'release $1 Pawn:Get': ok(dict(CALL, mode='release', hex='', block_size=0)),
    'help region': ok({'rows': [{'verb': 'region', 'origin': 'core', 'usage': 'region <addr>', 'summary': 's', 'flags': [],
                                 'result': '{address: addr}'}]}),
}


def conforms(value, kind, where):
    """Problems of a canned value against a type from help --json (the console's own check, in small)."""
    if not isinstance(kind, dict):
        return []
    if 'struct' in kind:
        if not isinstance(value, dict):
            return [f'{where}: not an object']
        problems = [f"{where}: unknown key '{key}'" for key in value if key not in kind['struct']]
        problems += [f"{where}: missing key '{key}'" for key, spec in kind['struct'].items()
                     if not spec.get('optional') and key not in value]
        for key, item in value.items():
            if key in kind['struct']:
                problems += conforms(item, kind['struct'][key]['type'], f'{where}.{key}')
        return problems
    if 'list' in kind:
        if not isinstance(value, list):
            return [f'{where}: not a list']
        return [p for i, item in enumerate(value) if item is not None for p in conforms(item, kind['list'], f'{where}[{i}]')]
    if 'enum' in kind:
        return [] if value in kind['enum'] or '...' in kind['enum'] else [f"{where}: '{value}' is not one of {kind['enum']}"]
    return []


@unittest.skipUnless(TABLE_TOOL, 'needs the verb table: --table <reagentlive-table.exe> or REAGENTLIVE_TABLE')
class SessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.table = json.loads(subprocess.run([TABLE_TOOL], capture_output=True, text=True, check=True).stdout)
        cls.types = {row['verb']: row['type'] for row in cls.table['rows']}

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.script = os.path.join(self.dir.name, 'fake_console.py')
        with open(self.script, 'w') as f:
            f.write(FAKE)
        replies = os.path.join(self.dir.name, 'replies.json')
        with open(replies, 'w') as f:
            json.dump(dict(REPLIES, **{'help --json': ok(self.table)}), f)
        self.log = os.path.join(self.dir.name, 'lines.txt')
        os.environ['REAGENTLIVE_FAKE_LOG'] = self.log
        os.environ['REAGENTLIVE_FAKE_REPLIES'] = replies
        self.session = dl.Session(sys.executable, ['-I', self.script])

    def tearDown(self):
        self.session.close()
        self.dir.cleanup()

    def lines(self):
        with open(self.log) as f:
            return [line.strip() for line in f]

    def test_canned_replies_match_the_table(self):
        problems = []
        for line, reply in REPLIES.items():
            if reply['ok']:
                verb = line.split(' ')[0]
                self.assertIn(verb, self.types, line)
                problems += conforms(reply['result'], self.types[verb], line)
        self.assertEqual(problems, [])

    def test_every_verb_has_a_method(self):
        for verb in self.types:
            if verb != 'quit':
                self.assertTrue(callable(getattr(self.session, verb.replace('-', '_'), None)), verb)

    def test_a_wrong_key_raises_and_names_the_valid_ones(self):
        region = self.session.region('base+0x10')
        self.assertEqual(region['kind'], 'code')
        self.assertIsNone(region['module'])              # Optional and absent: None.
        self.assertIsNone(region.get('chain'))
        self.assertEqual(region.get('module', 'none'), 'none')
        with self.assertRaises(dl.ResultKeyError) as caught:
            region['protection']
        self.assertIn("region has no key 'protection'; keys: address, allocation_base, allocation_protect, base, chain?, kind, module?, protect,", str(caught.exception))
        self.assertIn('protect', str(caught.exception))
        with self.assertRaises(dl.ResultKeyError):
            region.get('protection')                     # .get is no way around it.
        with self.assertRaises(dl.ResultKeyError):
            'protection' in region
        self.assertIn('kind', region)
        self.assertNotIn('module', region)
        self.assertEqual(self.session.table_hash, self.table['table_hash'])

    def test_nested_rows_are_strict_too(self):
        chained = self.session.read('$1->0x30->0x8', 2)
        self.assertEqual(chained['chain']['address'], '0x18')
        with self.assertRaises(dl.ResultKeyError) as caught:
            chained['hops']                              # The guess that started this: hops is inside chain.
        self.assertIn('chain?', str(caught.exception))
        row = self.session.sig_bytes('base+0x10', 32)
        with self.assertRaises(dl.ResultKeyError) as caught:
            row['suggested']['text']
        self.assertIn("sig-bytes.suggested has no key 'text'; keys: decoded?, fields, fixed_bytes, list, pattern", str(caught.exception))
        hit = self.session.sig_check('48 8b ?? 89 05', expect='Game.exe+0x10', offset=4)['rows'][0]
        self.assertEqual(hit['rva'], '0x1000')
        with self.assertRaises(dl.ResultKeyError) as caught:
            hit['offset']
        self.assertIn('sig-check.rows[0]', str(caught.exception))
        self.assertEqual(self.session.help('region')['rows'][0]['result'], '{address: addr}')

    def test_sig_lines(self):
        self.assertTrue(self.session.sig_check('48 8b ?? 89 05', expect='Game.exe+0x10', offset=4)['resolves'])
        self.session.sig_check([0x48, 0x8b, None, -1, 0x89], module='engine.dll')
        self.session.sig_check('48 8d 0d ?? ?? ?? ??', expect=0x1000, resolve=(7, 3))
        self.assertEqual(self.session.sig_file('sigs.jsonl')['winner'], 2)
        self.session.sig_bytes('base+0x10', 32)
        self.assertEqual(self.lines(), ['sig-check 48 8b ?? 89 05 --expect Game.exe+0x10 --offset 4',
                                        'sig-check 48 8b ?? ?? 89 --module engine.dll',
                                        'sig-check 48 8d 0d ?? ?? ?? ?? --expect 0x1000 --resolve 7,3',
                                        'sig-file sigs.jsonl', 'sig-bytes base+0x10 32'])

    def test_fast_lane_lines(self):
        result = self.session.disasm('Game.exe+0x10', 8, function=True, follow=True)
        self.assertEqual(result['rows'][0]['text'], 'ret')
        self.assertIsNone(result['function'])
        self.session.disasm(0x10)
        result = self.session.xrefs('base+0x10', module='engine.dll', all=True)
        self.assertEqual((result['rows'][0]['kind'], result['kinds']['call']), ('call', 1))
        self.assertEqual(self.session.touches('Game.exe+0x10', 0x538, 0x540, range=0x1000)['rows'][0]['distinct'], 1)
        self.assertEqual(self.session.vtable('$1', 4, offset=0x620, resolve=True)['rows'][0]['slot'], 0)
        self.assertIsNone(self.session.vtable(0x20, table=True)['table'])
        self.assertEqual(self.lines(), ['disasm Game.exe+0x10 8 --function --follow', 'disasm 0x10 24',
                                        'xrefs base+0x10 --module engine.dll --all',
                                        'touches Game.exe+0x10 0x538 0x540 --range 0x1000',
                                        'vtable $1 4 --offset 1568 --resolve', 'vtable 0x20 --table'])

    def test_reads_decode(self):
        self.assertEqual(dl.f32(self.session.read_bytes(0x1000, 4)), 1.0)
        self.assertEqual(self.session.read_struct(0x1000, 'f'), (1.0,))
        self.assertEqual(self.session.read(0x10, as_='cstr')['text'], 'A')
        self.assertEqual(self.session.read('$1+0x538', as_='u8 phase, pad 1')['layout'], {'phase': 1})
        tick, items = self.session.read_batch_bytes([(0x1000, 2), ('base+0x10', 4)])
        self.assertEqual((tick, items), (7, [b'\x01\x02', None]))
        self.assertEqual(self.session.peek('base+0x10', 1, label='labels.jsonl')['rows'][0]['label'], 'value')
        self.assertEqual(self.lines(), ['read 0x1000 4', 'read 0x1000 4', 'read 0x10 --as cstr',
                                        'read $1+0x538 --as "u8 phase, pad 1"', 'read-batch 0x1000 2 base+0x10 4',
                                        'peek base+0x10 1 --label labels.jsonl'])

    def test_search_pages_until_done(self):
        hits = self.session.search_addresses('ptr', 0x7ff600001234, heap=True, background=True, module='Game.exe')
        self.assertEqual(hits, [0x10, 0x20, 0x30])
        self.assertEqual(self.session.search_addresses('f32', 100.0, limit=2), [0x10, 0x20])
        self.assertEqual(self.session.search('text', 'Fatal error', max=1)['rows'], [])
        self.assertEqual(self.lines()[1], 'search ptr 0x7ff600001234 --heap --background --module Game.exe --max 512 --from 0x21')
        self.assertEqual(self.lines()[2:], ['search f32 100.0 --max 2', 'search text "Fatal error" --max 1'])

    def test_write_expect_failure_raises_with_found_bytes(self):
        self.assertEqual(self.session.write(0x1000, dl.pack('f32', 1.0))['size'], 4)
        with self.assertRaises(dl.ReAgentLiveError) as caught:
            self.session.write(0x1000, b'\x01\x02', expect=b'\xff\xff')
        self.assertEqual(caught.exception.error['found'], '00 00')

    def test_pin_and_watch(self):
        pin = self.session.pin('$1+0x3e4', dl.pack('f32', 1.0), seconds=2.5, expect=bytes(4))
        self.assertEqual(pin['id'], 1)
        self.assertEqual(self.session.pin_list()['rows'][0]['state'], 'active')
        self.assertEqual(self.session.pin_clear(1)['cleared'], 1)
        result = self.session.watch(1.5, [('$1+0x3e4', 4), (0x9000, 1)], then='call $1 Fn 01')
        self.assertTrue(result['complete'])
        self.assertEqual(result['then']['hex'], '01')
        rows = dl.samples(result)
        self.assertEqual(rows[0]['values'], [bytes([1, 0]), None])
        self.assertEqual(rows[1]['values'], [bytes([2, 0]), bytes([255])])
        self.assertEqual(self.session.watch_start(600, [(0x538, 16)], every=10, follow='pawn')['capacity'], 9000)
        result = self.session.watch_read()
        rows = dl.samples(result)
        self.assertEqual([row.get('lost', False) for row in rows], [False, True, False])
        self.assertEqual(rows[2], {'tick': 12, 'us': 20, 'values': [b'\x02\x00'], 'resumed': True, 'root': '0x20'})
        self.assertEqual(result['gaps'][0]['to_us'], 20)
        self.assertEqual(self.session.watch_stop()['stopped'], 'stopped')
        self.assertEqual(self.lines(), ['pin $1+0x3e4 0000803f --ms 2500 --expect 00000000', 'pin-list', 'pin-clear 1',
                                        'watch 1500ms $1+0x3e4:4 0x9000:1 --then call $1 Fn 01',
                                        'watch-start 600000ms 0x538:16 --every 10000ms --follow pawn', 'watch-read',
                                        'watch-stop'])

    def test_get_and_batch(self):
        self.assertEqual(self.session.get_values('$1', 'A.B'), 1.5)
        with self.assertRaises(dl.ReAgentLiveError):
            self.session.get_values('$1', 'A.B', 'C')
        self.assertEqual(self.session.get('$1', 'A.B', 'C')['rows'][1]['error'], 'no field C')
        items, same_tick = self.session.batch_results(['read 0x10 1', 'get $1 X'])
        self.assertTrue(same_tick)
        self.assertEqual(items[0]['hex'], '01')
        with self.assertRaises(dl.ResultKeyError):
            items[0]['bytes']                            # A batch item is as strict as a single command's result.
        self.assertIsInstance(items[1], dl.ReAgentLiveError)
        self.assertEqual(self.session.label('$1', 0x538, 'f64[3]', 'Start')['name'], 'Start')
        self.assertEqual(self.lines(), ['get $1 A.B', 'get $1 A.B C', 'get $1 A.B C', 'batch { read 0x10 1; get $1 X }',
                                        'label $1 0x538 f64[3] Start'])

    def test_lookups_and_pages(self):
        self.assertEqual(self.session.find('A', cls='Pawn')['total'], 2)
        self.assertEqual([o['name'] for o in self.session.find_all('A', cls='Pawn')], ['A', 'B'])
        self.assertEqual([o['name'] for o in self.session.find_all('A', cls='Pawn', pages=1)], ['A'])
        functions = self.session.funcs_all('0x10#1:1', 'Get')
        self.assertEqual([f.raw for f in functions], [{'owner': 'Pawn', 'name': 'A'}, {'owner': 'Actor', 'name': 'B'}])
        self.assertEqual(self.session.fields(0x10)['rows'][0]['offset'], 256)
        self.assertEqual(self.session.dump('$1', match='Health', nonzero=True)['rows'][0]['value'], 1.5)
        self.assertEqual(self.session.census('Actor', match='Pawn', limit=0)['rows'][0]['count'], 3)
        self.assertEqual(self.session.census_diff('Actor')['rows'][0]['after'], 2)
        self.assertEqual([r['class'] for r in self.session.census_all(match='Zone', limit=1)], ['ZoneA', 'ZoneB'])
        self.assertEqual(self.session.describe('$1')['class_size'], 1264)
        self.assertIsNone(self.session.func('$1', 'Pawn:A')['exec'])
        self.assertEqual(self.session.params('$1', 'Pawn:A')['rows'][0]['direction'], 'return')
        self.assertEqual([r['key'] for r in self.session.entries_all('$1', 'Attributes')], ['A', 'B'])
        self.assertEqual(self.session.modules()['rows'][0]['name'], 'Game.exe')
        self.assertEqual(self.lines(), [
            'find A --class Pawn', 'find A --class Pawn', 'find A --class Pawn --from 24', 'find A --class Pawn',
            'funcs 0x10#1:1 Get', 'funcs 0x10#1:1 Get --from 32', 'fields 0x10', 'dump $1 --match Health --nonzero',
            'census --class Actor --match Pawn --limit 0', 'census-diff --class Actor', 'census --match Zone --limit 1',
            'census --match Zone --limit 1 --from 1', 'describe $1', 'func $1 Pawn:A', 'params $1 Pawn:A',
            'entries $1 Attributes', 'entries $1 Attributes --from 7', 'modules'])

    def test_trials(self):
        row = self.session.trial('$1+0x28', dl.pack('f64', 2.0), '0x10#1:1', dl.pack('f64', 1.0), seconds=5, object=0x2000)
        self.assertEqual(row['state'], 'active')
        self.assertIsNone(row['guard'])
        self.assertEqual(self.session.trial_list()['rows'][0]['state'], 'active')
        self.assertEqual(self.session.trial_clear(1)['rows'][0]['state'], 'restored')
        self.assertEqual(self.lines()[1:], ['trial-list', 'trial-clear 1'])

    def test_names_refs_script_labels(self):
        self.assertEqual(self.session.name_map([5, 6]), {5: 'Five', 6: None})
        self.assertEqual([r['index'] for r in self.session.name_find_all('Tag')], [5, 9, 11])
        self.assertEqual([r['index'] for r in self.session.name_find_all('Tag', pages=2)], [5, 9])
        self.assertFalse(self.session.refs('Tag.A')['capped'])
        self.assertIsNone(self.session.refs(5)['name'])
        self.assertEqual(self.session.script('$1', 'BP:Go')['size'], 64)
        self.assertEqual(self.session.export_labels('l.jsonl')['total'], 3)
        with self.assertRaises(dl.ReAgentLiveError) as caught:
            self.session.refs('Missing')
        self.assertEqual(caught.exception.error['kind'], 'validation')
        self.assertEqual(self.lines(), ['name 5 6', 'name-find Tag', 'name-find Tag --from 8', 'name-find Tag --from 12',
                                        'name-find Tag', 'name-find Tag --from 8', 'refs Tag.A', 'refs #5',
                                        'script $1 BP:Go', 'export-labels l.jsonl', 'refs Missing'])

    def test_snap_dump_changed_and_record(self):
        self.assertIsNone(self.session.snap('$1')['path'])
        self.assertEqual(self.session.snap('0x10#1:1', 'ctrl.snap')['path'], 'ctrl.snap')
        self.assertEqual(self.session.dump('$1', changed=True)['rows'][0]['before'], 5)
        self.session.dump('$1', match='Ammo', changed='ctrl.snap')
        self.assertEqual(self.session.record('$1', 'burst.tsv', ['Ammo', 'Weapon:State'], 4)['total'], 40)
        self.assertEqual(self.lines(), ['snap $1', 'snap 0x10#1:1 ctrl.snap', 'dump $1 --changed',
                                        'dump $1 --match Ammo --changed ctrl.snap',
                                        'record $1 4 burst.tsv Ammo Weapon:State'])

    def test_each_and_calls(self):
        self.assertEqual(self.session.each('Pawn', 'Health')['rows'][0]['value'], 1)
        polled = self.session.each('Pawn', 'Pawn:GetHealth', seconds=3)
        self.assertEqual((polled['rows'][0]['hex'], polled['changes'][0]['after'], polled['poll_error']), ('01', 2, None))
        self.assertEqual(self.session.call_bytes('$1', 'Pawn:Fn', b'\x01'), b'\x01\x02')
        self.assertEqual(self.session.call('$1', 'Pawn:Fn')['mode'], 'raw')
        typed = self.session.call('$1', 'Pawn:Get', args={'Name': '#5', 'Mode': 2})
        self.assertTrue(typed['kept'])
        self.assertEqual(typed['outputs'][0]['name'], 'ReturnValue')
        self.assertEqual(self.session.call(0x10, 'Pawn:Get', args={})['mode'], 'typed')
        self.assertEqual(self.session.release('$1', 'Pawn:Get')['mode'], 'release')
        self.assertEqual(self.lines(), ['each Pawn Health', 'each Pawn Pawn:GetHealth --seconds 3', 'call $1 Pawn:Fn 01',
                                        'call $1 Pawn:Fn', 'call $1 Pawn:Get Name=#5 Mode=2', 'call 0x10 Pawn:Get --typed',
                                        'release $1 Pawn:Get'])

    def test_errors_raise(self):
        with self.assertRaises(dl.ReAgentLiveError):
            self.session.ok('bogus')
        self.assertEqual(self.session.status()['packs'], ['ue'])


if __name__ == '__main__':
    args = sys.argv[1:]
    if args[:1] == ['--table']:
        TABLE_TOOL = os.path.abspath(args[1])
        args = args[2:]
        # The class decorator read TABLE_TOOL before this ran.
        SessionTests.__unittest_skip__ = False
    unittest.main(argv=[sys.argv[0], *args])
