"""The IDA saved-function loader's planning, without IDA."""
import io
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'reagentlive', 'frontends', 'ida'))
import reagentlive_ida as rl  # noqa: E402

# As the console writes it: nlohmann sorts each row's keys.
FUNCTION = ('{"base":"0x140000000","end":"0x140001006","module":"Game.exe","module_size":4096,'
            '"row":"function","start":"0x140001000","timestamp":7}')
CODE = '{"address":"0x140001000","bytes":"f30f1005f8ff","row":"code","rva":"0x1000"}'
DATA = '{"address":"0x140003000","bytes":"0000003f","row":"data","rva":"0x3000"}'
NAME = '{"address":"0x140004000","name":"KERNEL32.dll!Sleep","row":"name","rva":"0x4000","what":"import"}'


class FakeInput:
    """The part of IDA's loader input the loader uses."""
    def __init__(self, text):
        self.data = text.encode()
        self.at = 0

    def seek(self, at):
        self.at = at

    def read(self, n):
        chunk = self.data[self.at:self.at + n]
        self.at += len(chunk)
        return chunk

    def size(self):
        return len(self.data)


class LoadFunction(unittest.TestCase):
    def test_a_saved_file_becomes_segments_and_names(self):
        plan = rl.parse([DATA, FUNCTION, '', CODE, NAME])
        self.assertIsInstance(plan, dict)
        self.assertEqual(plan['functions'], [(0x140001000, 0x140001006)])
        self.assertEqual(plan['build']['base'], 0x140000000)
        self.assertEqual([(s, k) for s, _, k in plan['segments']], [(0x140001000, 'CODE'), (0x140003000, 'DATA')])
        self.assertEqual(plan['segments'][1][1], bytes([0, 0, 0, 0x3f]))
        self.assertEqual(plan['names'], [(0x140004000, 'KERNEL32.dll_Sleep', 'import')])
        self.assertEqual(plan['stubs'], [(0x140004000, 'import')])   # Outside the saved bytes.

    def test_callees_load_as_more_functions_and_named_targets_get_stubs(self):
        callee = FUNCTION.replace('0x140001000', '0x140002000').replace('0x140001006', '0x140002001')
        code = '{"address":"0x140002000","bytes":"c3","row":"code","rva":"0x2000"}'
        names = ['{"address":"0x140002000","name":"AThing::Resolve","row":"name","rva":"0x2000","what":"function"}',
                 '{"address":"0x140005000","name":"fn_5000","row":"name","rva":"0x5000","what":"call"}',
                 '{"address":"0x140005000","name":"fn_5000","row":"name","rva":"0x5000","what":"call"}']
        plan = rl.parse([FUNCTION, callee, CODE, code] + names)
        self.assertEqual(plan['functions'], [(0x140001000, 0x140001006), (0x140002000, 0x140002001)])
        self.assertEqual(plan['stubs'], [(0x140005000, 'call')])     # Once; the saved callee needs none.
        other = FUNCTION.replace('"timestamp":7', '"timestamp":8')
        self.assertIn('another build', rl.parse([FUNCTION, other, CODE]))

    def test_the_console_row_names_the_console_and_its_labels_files(self):
        row = '{"labels":"C:/c/console.labels.jsonl","names":"C:/h/names.jsonl","path":"C:/c/console.exe","row":"console"}'
        plan = rl.parse([FUNCTION, CODE, row])
        self.assertEqual(plan['console'], {'path': 'C:/c/console.exe', 'labels': 'C:/c/console.labels.jsonl',
                                           'names': 'C:/h/names.jsonl'})
        self.assertIsNone(rl.parse([FUNCTION, CODE])['console'])

    def test_chunk_rows_become_function_tails(self):
        chunk = '{"end":"0x140002002","function":"0x140001000","row":"chunk","start":"0x140002000"}'
        code = '{"address":"0x140002000","bytes":"90c3","row":"code","rva":"0x2000"}'
        plan = rl.parse([FUNCTION, chunk, CODE, code])
        self.assertEqual(plan['functions'], [(0x140001000, 0x140001006)])
        self.assertEqual(plan['chunks'], [(0x140002000, 0x140002002, 0x140001000)])

    def test_identical_overlaps_join_into_one_segment(self):
        # A callee listed inside another callee's run: the same bytes twice.
        outer = '{"address":"0x140002000","bytes":"' + bytes(range(16)).hex() + '","row":"code","rva":"0x2000"}'
        inner = '{"address":"0x140002004","bytes":"04050607","row":"code","rva":"0x2004"}'
        tail = '{"address":"0x14000200c","bytes":"0c0d0e0f1011","row":"code","rva":"0x200c"}'
        plan = rl.parse([FUNCTION, CODE, outer, inner, tail])
        self.assertIsInstance(plan, dict)
        self.assertEqual([(s, len(d)) for s, d, _ in plan['segments']], [(0x140001000, 6), (0x140002000, 18)])
        self.assertEqual(plan['segments'][1][1], bytes(range(18)))

    def test_touching_runs_join_only_with_the_same_kind(self):
        chunk = '{"address":"0x140001006","bytes":"90c3","row":"code","rva":"0x1006"}'
        data = '{"address":"0x140001008","bytes":"0000","row":"data","rva":"0x1008"}'
        plan = rl.parse([FUNCTION, CODE, chunk, data])
        self.assertEqual([(s, len(d), k) for s, d, k in plan['segments']],
                         [(0x140001000, 8, 'CODE'), (0x140001008, 2, 'DATA')])
        inside = '{"address":"0x140001002","bytes":"1005","row":"data","rva":"0x1002"}'   # Read by code too.
        self.assertEqual([(s, len(d), k) for s, d, k in rl.parse([FUNCTION, CODE, inside])['segments']],
                         [(0x140001000, 6, 'CODE')])

    def test_overlaps_with_different_bytes_refuse_the_file(self):
        other = '{"address":"0x140001004","bytes":"f8aa","row":"code","rva":"0x1004"}'
        self.assertEqual(rl.parse([FUNCTION, CODE, other]), 'segments overlap at 0x140001005 with different bytes')

    def test_an_added_save_cuts_held_bytes_and_joins_what_it_touches(self):
        existing = [(0x1004, bytes(range(4, 8)), 'CODE'), (0x2000, b'ab', 'DATA'), (0x3000, b'xy', 'CODE')]
        segments = [(0x1000, bytes(range(16)), 'CODE'), (0x2000, b'ab', 'DATA'), (0x4000, b'zz', 'CODE')]
        writes, report = rl.additions(segments, existing)
        self.assertEqual(writes, [(0x1000, bytes(range(16)), 'CODE', [(0x1004, 0x1008)]),
                                  (0x4000, b'zz', 'CODE', [])])
        self.assertEqual(report, {'added': [(0x4000, 0x4002)], 'joined': [(0x1000, 0x1010)],
                                  'cut': [(0x1004, 0x1008), (0x2000, 0x2002)]})
        # A run touching an existing one of its kind grows it; a held run adds nothing.
        writes, report = rl.additions([(0x3002, b'zw', 'CODE'), (0x3000, b'x', 'CODE')], existing)
        self.assertEqual(writes, [(0x3000, b'xyzw', 'CODE', [(0x3000, 0x3002)])])
        self.assertEqual(report['cut'], [(0x3000, 0x3001)])
        self.assertEqual(rl.additions([(0x2000, b'a', 'DATA')], existing), ([], {'added': [], 'joined': [], 'cut': [(0x2000, 0x2001)]}))
        self.assertEqual(rl.additions(segments, []),
                         ([(s, d, k, []) for s, d, k in segments], {'added': [(0x1000, 0x1010), (0x2000, 0x2002), (0x4000, 0x4002)], 'joined': [], 'cut': []}))

    def test_an_added_save_that_disagrees_with_the_database_is_refused(self):
        self.assertIn('0x1005 with different bytes', rl.additions([(0x1000, b'\0' * 8, 'CODE')], [(0x1004, b'\0\1', 'CODE')]))

    def test_code_bytes_a_data_segment_held_turn_it_into_code(self):
        writes, report = rl.additions([(0x1000, b'abcd', 'CODE')], [(0x1000, b'abcd', 'DATA')])
        self.assertEqual(writes, [(0x1000, b'abcd', 'CODE', [(0x1000, 0x1004)])])
        self.assertEqual(report['joined'], [(0x1000, 0x1004)])

    def test_what_cannot_be_loaded_says_why(self):
        self.assertIn('no function row', rl.parse([CODE]))
        self.assertIn('no code rows', rl.parse([FUNCTION, DATA]))
        self.assertIn('line 2', rl.parse([FUNCTION, '{"row":"code","address":"zz","bytes":"90"}']))
        overlap = '{"address":"0x140001002","bytes":"90","row":"data","rva":"0x1002"}'
        self.assertIn('different bytes', rl.parse([FUNCTION, CODE, overlap]))
        newer = '{"address":"0x140001000","row":"type","rva":"0x1000"}'
        self.assertIn('older than the file', rl.parse([FUNCTION, CODE, newer]))

    def test_ida_claims_only_saved_functions(self):
        claimed = rl.accept_file(FakeInput(FUNCTION + '\n' + CODE), 'f.jsonl')
        self.assertEqual(claimed['format'], rl.FORMAT)
        self.assertEqual(claimed['options'], 1 | 0x8000)   # Offered before IDA's own Binary file.
        self.assertEqual(rl.accept_file(FakeInput('{"kind":"exec","rva":"0x10"}'), 'labels.jsonl'), 0)
        self.assertEqual(rl.accept_file(FakeInput('MZ\x90\x00'), 'game.exe'), 0)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + [a for a in sys.argv[1:] if a.startswith('-')])
