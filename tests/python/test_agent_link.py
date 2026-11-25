"""tools/agent_link.py: wiring an agent to a host project writes the project record, the MCP files (keeping
other servers), the ReAgentLive block in AGENTS.md (and CLAUDE.md only when it does not import AGENTS.md),
the skill links and their .gitignore lines, and nothing outside the project; a second run updates
all of it in place; a bad argument or record changes nothing. Runs the script from a copy of the repo's
tools and skill, as a project's submodule at a path of its own."""
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))


class Project:
    """A temporary host project carrying a copy of the repo's tools and skill at external/reagentlive."""

    def __init__(self, clone='external/reagentlive'):
        self.root = tempfile.mkdtemp()
        self.local = os.path.join(self.root, 'local')  # LOCALAPPDATA: the script never writes there
        self.clone = os.path.join(self.root, *clone.split('/'))
        for folder in ('tools', os.path.join('.claude', 'skills')):
            shutil.copytree(os.path.join(REPO, folder), os.path.join(self.clone, folder))

    def run(self, *args, here=True):
        """The script's JSON; with here, for this project (--project), else its own default."""
        env = dict(os.environ, LOCALAPPDATA=self.local)
        if here and '--project' not in args:
            args = (*args, '--project', self.root)
        script = os.path.join(self.clone, 'tools', 'agent_link.py')
        result = subprocess.run([sys.executable, '-I', script, *args], capture_output=True, text=True, env=env)
        return result.returncode, json.loads(result.stdout)

    def path(self, relative):
        return os.path.join(self.root, *relative.split('/'))

    def read(self, relative):
        with open(self.path(relative), encoding='utf-8') as handle:
            return handle.read()

    def put(self, relative, text):
        os.makedirs(os.path.dirname(self.path(relative)), exist_ok=True)
        with open(self.path(relative), 'w', encoding='utf-8') as handle:
            handle.write(text)


class AgentLinkTests(unittest.TestCase):
    def setUp(self):
        self.project = Project()

    def tearDown(self):
        def writable(function, path, _):  # git's object files are read-only on Windows
            os.chmod(path, stat.S_IWRITE)
            function(path)
        shutil.rmtree(self.project.root, onerror=writable)

    def link(self, *extra):
        return self.project.run('--console', 'out/tool.exe', '--dll', 'out/host.dll', *extra)

    def test_a_new_project(self):
        code, out = self.link('--mcp', 'claude-code,copilot')
        self.assertEqual(code, 0, out)
        self.assertEqual(json.loads(self.project.read('reagentlive.json')),
                         {'library': 'external/reagentlive', 'mcp': ['claude-code', 'copilot'], 'console': 'out/tool.exe',
                          'dll': 'out/host.dll', 'check': True})
        self.assertIn('build_project.ps1 -Project .`, with `-Check` to check',
                      ' '.join(self.project.read('AGENTS.md').split()))
        claude = json.loads(self.project.read('.mcp.json'))['mcpServers']['reagentlive']
        self.assertEqual(claude['args'], ['external/reagentlive/src/reagentlive/frontends/mcp/reagentlive_mcp.py',
                                          '--console', 'out/tool.exe'])
        copilot = json.loads(self.project.read('.vscode/mcp.json'))['servers']['reagentlive']
        self.assertEqual(copilot['args'][-1], '${workspaceFolder}/out/tool.exe')
        agents = self.project.read('AGENTS.md')
        self.assertIn('external/reagentlive/.claude/skills/reagentlive/SKILL.md', agents)
        self.assertFalse(os.path.exists(self.project.path('CLAUDE.md')))  # Claude Code reads AGENTS.md
        # Wrapped, but a code span (a command) is never split.
        self.assertTrue(all(len(line) <= 80 or line.strip().count('`') == 2 for line in agents.split('\n')), agents)
        self.assertTrue(all(line.count('`') % 2 == 0 for line in agents.split('\n')), agents)
        ignored = self.project.read('.gitignore').split('\n')
        for link in out['links']:
            self.assertIn(link, ignored)
            self.assertTrue(os.path.isfile(os.path.join(self.project.path(link), 'SKILL.md')), link)
        self.assertFalse(os.path.exists(self.project.local))
        # Every repo path the block names exists in ReAgentLive.
        for path in ('.claude/skills/reagentlive/SKILL.md', 'docs/README.md', 'tools/update.ps1',
                     'tools/build_project.ps1', 'docs/agents/update.md'):
            self.assertIn(f'external/reagentlive/{path}', agents)
            self.assertTrue(os.path.exists(os.path.join(REPO, *path.split('/'))), path)

    def test_an_existing_project_keeps_its_own_files(self):
        self.project.put('AGENTS.md', '# My mod\n\nOur rules.\n')
        self.project.put('CLAUDE.md', '# Claude notes\n')
        self.project.put('.mcp.json', json.dumps({'mcpServers': {'other': {'command': 'x'}}}))
        self.project.put('.gitignore', 'bin/')
        code, out = self.link('--build', 'cmake --build out', '--check', 'no')
        self.assertEqual(code, 0, out)
        self.assertTrue(self.project.read('AGENTS.md').startswith('# My mod\n\nOur rules.\n\n<!-- reagentlive -->'))
        self.assertIn('<!-- reagentlive -->', self.project.read('CLAUDE.md'))
        self.assertEqual(set(json.loads(self.project.read('.mcp.json'))['mcpServers']), {'other', 'reagentlive'})
        self.assertTrue(self.project.read('.gitignore').startswith('bin/\n.claude/skills/reagentlive\n'))
        record = json.loads(self.project.read('reagentlive.json'))
        self.assertEqual((record['build'], record['check']), ('cmake --build out', False))
        agents = ' '.join(self.project.read('AGENTS.md').split())
        self.assertIn('Build: `cmake --build out` (it ticks only in the game', agents)
        self.assertNotIn('-Check', agents)  # check is false: no check to run

    def test_a_second_run_updates_in_place(self):
        self.assertEqual(self.link('--mcp', 'cursor')[0], 0)
        code, out = self.project.run('--console', 'out/renamed.exe')
        self.assertEqual(code, 0, out)
        self.assertEqual((out['record']['dll'], out['record']['mcp']), ('out/host.dll', ['cursor']))
        self.assertIn('out/renamed.exe', self.project.read('.cursor/mcp.json'))
        agents = self.project.read('AGENTS.md')
        self.assertEqual(agents.count('<!-- reagentlive -->'), 1)
        self.assertIn('out/renamed.exe', agents)
        self.assertEqual(self.project.read('.gitignore').count('.claude/skills/reagentlive'), 1)
        # Nothing changed: nothing is written.
        code, out = self.project.run()
        self.assertEqual((code, out['written']), (0, []), out)

    def test_refusals_change_nothing(self):
        cases = [(('--dll', 'out/host.dll'), '--console'),
                 (('--console', 'out/tool.exe', '--dll', '../host.dll'), 'inside the project'),
                 (('--console', 'out/tool.exe', '--dll', 'out/host.dll', '--mcp', 'x'), '--mcp'),
                 (('--console', 'out/tool.exe', '--dll', 'out/host.dll', '--check', 'maybe'), '--check')]
        for args, word in cases:
            code, out = self.project.run(*args)
            self.assertEqual(code, 1, args)
            self.assertIn(word, out['error'])
        self.project.put('.mcp.json', 'not json')
        code, out = self.link()
        self.assertIn('not readable JSON', out['error'])
        self.assertEqual(sorted(os.listdir(self.project.root)), ['.mcp.json', 'external'])
        os.remove(self.project.path('.mcp.json'))
        os.makedirs(self.project.path('.agents/skills/reagentlive'))
        code, out = self.link()
        self.assertIn('is an untracked folder, not a link', out['error'])
        self.assertEqual(sorted(os.listdir(self.project.root)), ['.agents', 'external'])
        code, out = self.project.run('--console', 'a.exe', '--dll', 'a.dll', '--project', self.project.clone)
        self.assertIn('this clone itself', out['error'])

    def test_a_project_wired_by_hand_keeps_its_server_and_skill(self):
        # Its own server under its own name, with its own flags and absolute paths; its own tracked skill.
        self.project.put('.mcp.json', json.dumps({'mcpServers': {
            'other': {'command': 'x'},
            'my-dev': {'command': 'python', 'env': {'A': '1'}, 'args': [
                'C:/old/reagentlive/src/reagentlive/frontends/mcp/reagentlive_mcp.py',
                '--console', 'C:/old/bin/dev.exe', '--name', 'my-dev', '--packs', 'explore']}}}))
        self.project.put('.claude/skills/reagentlive/SKILL.md', '# pointer\n')
        subprocess.run(['git', 'init', '-q', self.project.root], check=True)
        subprocess.run(['git', '-C', self.project.root, 'add', '.claude/skills/reagentlive/SKILL.md'], check=True)
        code, out = self.link()
        self.assertEqual(code, 0, out)
        self.assertEqual((out['servers'], out['kept'], out['links']),
                         ({'claude-code': 'my-dev'}, ['.claude/skills/reagentlive'], ['.agents/skills/reagentlive']))
        servers = json.loads(self.project.read('.mcp.json'))['mcpServers']
        self.assertEqual(set(servers), {'other', 'my-dev'})
        self.assertEqual(servers['my-dev'], {'command': 'python', 'env': {'A': '1'}, 'args': [
            'external/reagentlive/src/reagentlive/frontends/mcp/reagentlive_mcp.py',
            '--console', 'out/tool.exe', '--name', 'my-dev', '--packs', 'explore']})
        self.assertEqual(self.project.read('.claude/skills/reagentlive/SKILL.md'), '# pointer\n')
        self.assertIn('`my-dev` MCP tools', self.project.read('AGENTS.md'))
        ignored = self.project.read('.gitignore').split('\n')
        self.assertNotIn('.claude/skills/reagentlive', ignored)
        self.assertIn('.agents/skills/reagentlive', ignored)
        # Two ReAgentLive servers: refused.
        servers['second'] = servers['my-dev']
        self.project.put('.mcp.json', json.dumps({'mcpServers': servers}))
        code, out = self.link()
        self.assertIn('several ReAgentLive servers', out['error'])

    def test_a_clone_that_is_no_submodule_needs_the_project_named(self):
        code, out = self.project.run('--console', 'b/c.exe', '--dll', 'b/h.dll', here=False)
        self.assertEqual(code, 1)
        self.assertIn('--project', out['error'])

    def test_claude_md_and_line_endings(self):
        self.project.put('CLAUDE.md', 'See AGENTS.md for style.\n')
        with open(self.project.path('AGENTS.md'), 'w', newline='') as handle:
            handle.write('# Mod\r\n\r\nRules.\r\n')
        self.assertEqual(self.link()[0], 0)
        # A mention is no import: CLAUDE.md gets the block; AGENTS.md keeps CRLF.
        self.assertIn('<!-- reagentlive -->', self.project.read('CLAUDE.md'))
        with open(self.project.path('AGENTS.md'), newline='') as handle:
            agents = handle.read()
        self.assertTrue(agents.startswith('# Mod\r\n\r\nRules.\r\n\r\n<!-- reagentlive -->\r\n'))
        self.assertNotIn('\n', agents.replace('\r\n', ''))
        # Once CLAUDE.md imports AGENTS.md, its copy of the block goes.
        with open(self.project.path('CLAUDE.md'), 'a') as handle:
            handle.write('@AGENTS.md\n')
        self.assertEqual(self.link()[0], 0)
        claude = self.project.read('CLAUDE.md')
        self.assertNotIn('<!-- reagentlive -->', claude)
        self.assertIn('See AGENTS.md for style.', claude)
        self.assertIn('@AGENTS.md', claude)

    def test_a_clone_outside_the_project_is_refused(self):
        elsewhere = tempfile.mkdtemp()
        try:
            code, out = self.link('--project', elsewhere)
            self.assertEqual(code, 1)
            self.assertIn('not inside', out['error'])
        finally:
            shutil.rmtree(elsewhere)


if __name__ == '__main__':
    unittest.main()
