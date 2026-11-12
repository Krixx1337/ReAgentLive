"""The console built without the engine pack (no agent or game): its verb table and what is made from it.

Its `help --json` (--console <reagentlive-core-console.exe>) holds core rows
only, less those that need a pack; every core row it keeps is the same as in
the full table (--table <reagentlive-table.exe>). The generated module follows
the table, so it has no pack class and no function for a pack verb.
"""
import json
import os
import subprocess
import sys
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

    def test_only_core_rows_and_origin(self):
        self.assertEqual({row['origin'] for row in self.core['rows']}, {'core'})
        self.assertEqual([origin['origin'] for origin in self.core['origins']], ['core'])
        self.assertTrue(self.pack_verbs, 'the full table has pack rows')

    def test_rows_that_need_a_pack_are_left_out(self):
        verbs = [row['verb'] for row in self.core['rows']]
        self.assertNotIn('trial', verbs)
        self.assertIn('trial-list', verbs)
        self.assertIn('trial', self.full_rows)

    def test_kept_core_rows_are_unchanged(self):
        full_core = [row['verb'] for row in self.full['rows'] if row['origin'] == 'core' and row['verb'] != 'trial']
        self.assertEqual([row['verb'] for row in self.core['rows']], full_core)
        for row in self.core['rows']:
            self.assertEqual(row, self.full_rows[row['verb']], row['verb'])
        self.assertNotEqual(self.core['table_hash'], self.full['table_hash'])

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
