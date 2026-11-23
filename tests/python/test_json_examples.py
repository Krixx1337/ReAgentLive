"""check_json_examples.py: which doc text it takes as a call, and that the table's validator refuses a bad one.

Usage: test_json_examples.py --table <reagentlive-table>
"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_json_examples as examples  # noqa: E402

VERBS = {'read', 'batch', 'find', 'sig-check'}
TABLE = None


def found(markdown):
    return [hit for _, code in examples.regions(markdown) for hit in examples.examples(code, VERBS)]


class Extraction(unittest.TestCase):
    def test_calls_lists_and_one_shots(self):
        self.assertEqual(found('`{"verb": "read", "args": {"addr": "0x10"}}`'),
                         [({'verb': 'read', 'args': {'addr': '0x10'}}, None)])
        listed = found("```powershell\nconsole.exe -c '[{\"verb\": \"read\"}, {\"verb\": \"find\"}]'\n```")
        self.assertEqual([call['verb'] for call, _ in listed], ['read', 'find'])
        escaped = found('`console.exe -c "{\\"verb\\": \\"read\\", \\"args\\": {\\"addr\\": \\"0x10\\"}}"`')
        self.assertEqual(escaped, [({'verb': 'read', 'args': {'addr': '0x10'}}, None)])
        # A batch is one call; the validator checks its calls.
        self.assertEqual(len(found('`{"verb": "batch", "args": {"calls": [{"verb": "read"}]}}`')), 1)
        self.assertEqual(found("`session.call('find', {\"text\": \"Pawn\"})`"),
                         [({'verb': 'find', 'args': {'text': 'Pawn'}}, None)])

    def test_what_is_left_out(self):
        self.assertEqual(found('`{"verb": ..., "args": {...}}`'), [])                 # A shape, not a call.
        self.assertEqual(found('`{"verb": "read", "origin": "core", "args": []}`'), [])  # A help row.
        self.assertEqual(found('{"verb": "read", "args": {"addr": "0x10"}}'), [])      # Prose, not code.
        self.assertEqual(found('<!-- refused example -->\n`{"verb": "read", "args": {"adr": 1}}`'), [])
        self.assertEqual(found('<!-- refused example -->\n```json\n{"verb": "read"}\n```'), [])

    def test_call_text_that_is_not_json(self):
        (call, why), = found('`{"verb": "read", "args": {"addr": <ref>}}`')
        self.assertIsNone(call)
        self.assertTrue(why.startswith('not JSON'))


class Validator(unittest.TestCase):
    def test_the_table_refuses_a_bad_example(self):
        if not TABLE:
            self.skipTest('no --table')
        with tempfile.TemporaryDirectory() as root:
            doc = os.path.join(root, 'doc.md')
            with open(doc, 'w', encoding='utf-8') as file:
                file.write('`{"verb": "read", "args": {"addr": "0x10", "len": 8}}`\n'
                           '`{"verb": "read", "args": {"addr": "<ref>"}}`\n'
                           '`{"verb": "sig-check", "args": {"pattern": "48 8b ?? ??"}}`\n')
            self.assertEqual(examples.main(['--table', TABLE, '--root', root, doc]), 1)
            envelopes = examples.check(TABLE, [{'verb': 'read', 'args': {'addr': '0x10', 'len': 8}},
                                               {'verb': 'read', 'args': {'addr': '<ref>'}},
                                               {'verb': 'read', 'args': {'addr': {'module': 'any.dll', 'rva': 16}}}])
            self.assertEqual([envelope['ok'] for envelope in envelopes], [True, False, True])
            self.assertEqual(envelopes[1]['error']['key'], 'addr')


if __name__ == '__main__':
    if '--table' in sys.argv:
        at = sys.argv.index('--table')
        TABLE = sys.argv[at + 1]
        del sys.argv[at:at + 2]
    unittest.main()
