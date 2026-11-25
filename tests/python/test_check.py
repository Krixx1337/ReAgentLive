"""tools/check.ps1: one JSON object with the machine's and a project's items, each with a fix when it fails;
a project missing its submodule, record or build fails with those fixes; the machine record is never needed;
nothing is written. Runs under Windows PowerShell 5.1 (the oldest shell the scripts support); skipped off
Windows."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SCRIPT = os.path.join(REPO, 'tools', 'check.ps1')
MACHINE = ['windows-x64', 'visual-studio-cpp', 'cmake', 'ninja', 'git', 'python', 'python-mcp', 'machine-record']


@unittest.skipUnless(os.name == 'nt' and shutil.which('powershell'), 'Windows PowerShell only')
class CheckTests(unittest.TestCase):
    def check(self, *args):
        result = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                                 '-File', SCRIPT, *args], capture_output=True, text=True, timeout=120)
        return result.returncode, json.loads(result.stdout)

    def test_a_project_missing_its_parts(self):
        root = tempfile.mkdtemp()
        try:
            with open(os.path.join(root, 'reagentlive.json'), 'w') as handle:
                json.dump({'library': 'reagentlive', 'mcp': ['claude-code'], 'console': 'out/tool.exe',
                           'dll': 'out/host.dll', 'check': True}, handle)
            before = sorted(os.listdir(root))
            code, out = self.check('-Project', root)
            self.assertEqual((code, out['ok']), (1, False))
            self.assertEqual([item['name'] for item in out['machine']], MACHINE)
            items = {item['name']: item for item in out['project']}
            self.assertEqual(list(items), ['submodule', 'record', 'console-built', 'dll-built', 'mcp-claude-code'])
            for name in ('submodule', 'console-built', 'dll-built', 'mcp-claude-code'):
                self.assertFalse(items[name]['ok'], name)
                self.assertTrue(items[name]['fix'], name)
            self.assertIn('submodule update --init', items['submodule']['fix'])
            self.assertTrue(items['record']['ok'])
            self.assertIsNone(items['record']['fix'])
            # The machine record is never needed: improvement is user without it.
            record = out['machine'][-1]
            self.assertFalse(record['needed'])
            self.assertEqual(sorted(os.listdir(root)), before)  # read-only
        finally:
            shutil.rmtree(root)

    def test_a_folder_that_is_not_there(self):
        code, out = self.check('-Project', os.path.join(tempfile.gettempdir(), 'reagentlive-no-such-folder'))
        self.assertEqual(code, 1)
        self.assertEqual([(item['name'], item['ok']) for item in out['project']], [('folder', False)])


if __name__ == '__main__':
    unittest.main()
