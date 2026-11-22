"""The IDA saved-function loader's planning, without IDA."""
import io
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'reagentlive', 'frontends', 'ida'))
import load_function as loader  # noqa: E402

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
        plan = loader.parse([DATA, FUNCTION, '', CODE, NAME])
        self.assertIsInstance(plan, dict)
        self.assertEqual(plan['function']['start'], 0x140001000)
        self.assertEqual(plan['function']['base'], 0x140000000)
        self.assertEqual([(s, k) for s, _, k in plan['segments']], [(0x140001000, 'CODE'), (0x140003000, 'DATA')])
        self.assertEqual(plan['segments'][1][1], bytes([0, 0, 0, 0x3f]))
        self.assertEqual(plan['names'], [(0x140004000, 'KERNEL32.dll_Sleep')])

    def test_what_cannot_be_loaded_says_why(self):
        self.assertIn('no function row', loader.parse([CODE]))
        self.assertIn('no code rows', loader.parse([FUNCTION, DATA]))
        self.assertIn('line 2', loader.parse([FUNCTION, '{"row":"code","address":"zz","bytes":"90"}']))
        overlap = '{"address":"0x140001002","bytes":"90","row":"data","rva":"0x1002"}'
        self.assertIn('overlap', loader.parse([FUNCTION, CODE, overlap]))

    def test_ida_claims_only_saved_functions(self):
        self.assertTrue(loader.accept_file(FakeInput(FUNCTION + '\n' + CODE), 'f.jsonl'))
        self.assertEqual(loader.accept_file(FakeInput('{"kind":"exec","rva":"0x10"}'), 'labels.jsonl'), 0)
        self.assertEqual(loader.accept_file(FakeInput('MZ\x90\x00'), 'game.exe'), 0)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + [a for a in sys.argv[1:] if a.startswith('-')])
