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
        # A result key that looks like a tool name (status's host_thread) is not one.
        hosted = names.Table({'rows': [
            {'verb': 'world', 'origin': 'host', 'args': []},
            {'verb': 'status', 'origin': 'core', 'args': [], 'type': {'struct': {'host_thread': {'type': 'str'}}}}]})
        self.assertEqual(names.check_text(hosted, '`host_thread` `host_world`', 'doc.md'), [])
        self.assertEqual(names.check_text(hosted, '`host_wrold`', 'doc.md'), ['doc.md: no such tool: `host_wrold`'])

    def test_text_grammar_and_flags_are_stale(self):
        self.assertEqual(problems('`find PC_` `read 0x10 --len 4`'), [
            'doc.md: text grammar (a call is JSON): `find PC_`',
            'doc.md: text grammar (a call is JSON): `read 0x10 --len 4`',
            'doc.md: flag --len (args are JSON keys): `read 0x10 --len 4`'])
        self.assertEqual(problems('`--limit`'), ['doc.md: flag --limit (args are JSON keys): `--limit`'])
        self.assertEqual(problems("`<console> -c 'find X'`"), ["doc.md: one-shot -c without a JSON call: `<console> -c 'find X'`"])
        # A verb alone, a JSON one-shot, a program's own flags and a console option are fine.
        self.assertEqual(problems("`find` `<console> -c '{\"verb\": \"read\", \"args\": {\"addr\": \"0x1\"}}'` `python x.py --limit 3` `--packs`"), [])
        self.assertEqual(problems('```\nfind PC_\n```'), [])          # Fenced text is not a code span.


    def test_code_string_literals(self):
        table = names.Table({'rows': [
            {'verb': 'read', 'origin': 'core', 'args': [{'key': 'addr'}]},
            {'verb': 'labels-export', 'origin': 'ue', 'args': []},
            {'verb': 'mem-snap', 'origin': 'core', 'args': []}]})
        cpp = ('// "export-labels" in a comment is not checked\n'
               'Fail("addr: use " "export-labels first");\n'
               'Note("{\\"verb\\": \\"read\\", \\"args\\": {\\"len\\": 4}}");\n'
               'Ok("read-only, labels-export, ue_labels_export, read-batch"); Bad("ue_export_labels");\n')
        self.assertEqual(names.check_code(table, cpp, 'x.cpp'), [
            "x.cpp:2: no verb 'export-labels'",
            "x.cpp:3: read has no arg 'len'",
            "x.cpp:4: no such tool 'ue_export_labels'"])
        py = '"""Run `labels-export` then `export-labels`."""\nx = f"{1} ue_labels_export"\n'
        self.assertEqual(names.check_code(table, py, 'x.py'), ["x.py:1: no verb 'export-labels'"])
        self.assertEqual(names.check_text(table, '`--session`', 'doc.md'), [])   # A console option.


if __name__ == '__main__':
    unittest.main()
