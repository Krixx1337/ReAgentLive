"""check_verb_names.py: what it takes for a verb or arg name, and what it reports as stale."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_verb_names as names  # noqa: E402

TABLE = names.Table({'rows': [
    {'verb': 'read', 'origin': 'core', 'args': [{'key': 'addr'}, {'key': 'len'}]},
    {'verb': 'batch', 'origin': 'core', 'args': [{'key': 'calls'}]},
    {'verb': 'find', 'origin': 'ue', 'args': [{'key': 'text'}, {'key': 'view', 'fields': [{'key': 'limit'}, {'key': 'start'}]}]},
]})


def problems(text):
    return names.check_text(TABLE, text, 'doc.md')


class VerbNames(unittest.TestCase):
    def test_json_calls_name_verbs_and_args(self):
        self.assertEqual(problems('`{"verb": "read", "args": {"addr": "0x10", "len": 4}}`'), [])
        self.assertEqual(problems('`{"verb": "base", "args": {}}`'), ["doc.md: no verb 'base'"])
        self.assertEqual(problems('`{"verb": "read", "args": {"count": 4}}`'), ["doc.md: read has no arg 'count'"])
        self.assertEqual(problems('```json\n{"verb": "find",\n "args": {"view": {"max": 5}}}\n```'),
                         ["doc.md: find.view has no key 'max'"])
        # A batch's calls and a plan's steps are calls too.
        self.assertEqual(problems('`{"verb": "batch", "args": {"calls": [{"verb": "pin"}]}}`'), ["doc.md: no verb 'pin'"])
        self.assertEqual(problems('[{"verb": "read", "args": {"addr": "0x1"}, "repeat": {"every": 1, "for": 2}}]'), [])

    def test_python_calls_name_verbs_and_keywords(self):
        self.assertEqual(problems("c.read('0x10', len=4)\nc.ue.find(text='A', view={'limit': 5})"), [])
        self.assertEqual(problems("c.ue.find(text='A', limit=5)")[0], "doc.md: find has no arg 'limit' (c.ue.find(...)")
        self.assertEqual(problems("c.ue.read('0x10')"), ['doc.md: no verb for c.ue.read('])
        self.assertEqual(problems("c.host.world()"), [])                 # No host rows: not checked.
        self.assertEqual(problems("session.call('frobnicate', {})"), ["doc.md: no verb 'frobnicate' in .call('frobnicate'"])

    def test_tool_names_and_key_paths(self):
        self.assertEqual(problems('`ue_find` `view.limit`'), [])
        self.assertEqual(problems('`ue_fields`'), ['doc.md: no such tool: `ue_fields`'])
        self.assertEqual(problems('`view.max`'), ['doc.md: no such key: `view.max`'])


if __name__ == '__main__':
    unittest.main()
