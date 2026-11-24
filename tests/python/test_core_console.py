"""The console built without the engine pack (no agent or game): its verb table and what is made from it.

Its `help --json` (--console <reagentlive-core-console.exe>) holds core rows
only, every one of them, each the same as in
the full table (--table <reagentlive-table.exe>). The generated module follows
the table, so it has no pack class and no function for a pack verb.
"""
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'src', 'reagentlive', 'frontends', 'python'))
import reagentlive_gen  # noqa: E402

CONSOLE = None
TABLE_TOOL = None


class CoreConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = reagentlive_gen.table_of(CONSOLE)
        cls.full = json.loads(subprocess.run([TABLE_TOOL], capture_output=True, text=True, check=True).stdout)
        cls.full_rows = {row['verb']: row for row in cls.full['rows']}
        cls.pack_verbs = [row['verb'] for row in cls.full['rows'] if row['origin'] not in ('core', 'host')]

    def test_the_table_is_never_digested(self):
        # A tool client reads it before any call (the MCP server at start), whatever the budget.
        call = json.dumps({'verb': 'help', 'args': {'json': True}, 'max_chars': 100})
        out = subprocess.run([CONSOLE, '--caller', 'tool', '--no-usage', '-c', call],
                             capture_output=True, text=True, timeout=30).stdout
        reply = json.loads(next(line for line in out.splitlines() if line.startswith('{')))
        self.assertTrue(reply['ok'])
        self.assertIn('rows', reply['result'])
        self.assertNotIn('digest', reply)

    def test_only_core_rows_and_origin(self):
        self.assertEqual({row['origin'] for row in self.core['rows']}, {'core'})
        self.assertEqual([origin['origin'] for origin in self.core['origins']], ['core'])
        self.assertTrue(self.pack_verbs, 'the full table has pack rows')

    def test_core_rows_are_unchanged(self):
        full_core = [row['verb'] for row in self.full['rows'] if row['origin'] == 'core']
        self.assertEqual([row['verb'] for row in self.core['rows']], full_core)
        for row in self.core['rows']:
            self.assertEqual(row, self.full_rows[row['verb']], row['verb'])
        self.assertNotEqual(self.core['table_hash'], self.full['table_hash'])

    def test_consoles_writing_the_usage_log_at_once_keep_every_line_whole(self):
        # Each refused call is one usage line; with no target the console only logs it. Every line goes
        # in one append-only write, so lines from consoles writing at once never interleave.
        consoles, calls = 6, 25
        session = f'append-{os.getpid()}-{time.time_ns()}'
        line = json.dumps([{'verb': 'read', 'args': {}, 'refused': {'kind': 'schema', 'message': 'x' * 300}}] * calls)
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, 'calls.json')
            with open(path, 'w', encoding='utf-8') as file:
                file.write(line)
            runs = [subprocess.Popen([CONSOLE, '--session', f'{session}-{i}', '-c', f'@{path}'],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) for i in range(consoles)]
            for run in runs:
                run.wait(60)
        usage = os.path.splitext(CONSOLE)[0] + '.usage.jsonl'
        counts = {}
        with open(usage, encoding='utf-8') as file:
            for text in file:
                if session not in text:
                    continue
                record = json.loads(text)  # A torn or interleaved line would not parse.
                counts[record['session']] = counts.get(record['session'], 0) + 1
        self.assertEqual(counts, {f'{session}-{i}': calls for i in range(consoles)})

    def test_the_generated_module_has_no_pack_verbs(self):
        source = reagentlive_gen.generate(self.core, 'reagentlive-core-console')
        self.assertIn('def read(', source)
        for verb in self.pack_verbs:
            self.assertNotIn(f'def {reagentlive_gen.identifier(verb)}(', source, verb)
        for origin in self.full['origins']:
            if origin['origin'] not in ('core', 'host'):
                self.assertNotIn(f'class {reagentlive_gen.class_name(origin["origin"])}Verbs', source)


if __name__ == '__main__':
    args = sys.argv[1:]
    while args[:1] in (['--console'], ['--table']):
        if args[0] == '--console':
            CONSOLE = os.path.abspath(args[1])
        else:
            TABLE_TOOL = os.path.abspath(args[1])
        args = args[2:]
    if not (CONSOLE and TABLE_TOOL):
        sys.exit('usage: test_core_console.py --console <reagentlive-core-console.exe> --table <reagentlive-table.exe>')
    unittest.main(argv=[sys.argv[0], *args])
