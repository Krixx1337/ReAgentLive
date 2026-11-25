"""tools/project.py: a project made from each kit carries the project's names and no kit name, its record,
agent files and MCP configs; init refuses a folder that is not a project or already holds one; link makes
the skill links again. Runs the script from a copy of the repo's kits and tools in a temporary project."""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
KIT_NAMES = re.compile(r'reagentlive-(own-thread|unreal|ue-console|ue-|console-exe)|OUTPUT_NAME reagentlive|'
                       r'Local\\\\reagentlive-')
MODULES = '[submodule "reagentlive"]\n\tpath = reagentlive\n\turl = https://example.invalid/ReAgentLive.git\n'


class Project:
    """A temporary project holding a copy of the repo's tools, kits and skill as its reagentlive/ folder."""

    def __init__(self, submodule=True):
        self.root = tempfile.mkdtemp()
        clone = os.path.join(self.root, 'reagentlive')
        for folder in ('tools', 'examples', os.path.join('.claude', 'skills')):
            shutil.copytree(os.path.join(REPO, folder), os.path.join(clone, folder))
        if submodule:
            with open(os.path.join(self.root, '.gitmodules'), 'w') as handle:
                handle.write(MODULES)

    def run(self, *args):
        script = os.path.join(self.root, 'reagentlive', 'tools', 'project.py')
        result = subprocess.run([sys.executable, '-I', script, *args], capture_output=True, text=True)
        return result.returncode, json.loads(result.stdout)

    def read(self, path):
        with open(os.path.join(self.root, *path.split('/')), encoding='utf-8') as handle:
            return handle.read()

    def close(self):
        shutil.rmtree(self.root)


class ProjectTests(unittest.TestCase):
    def setUp(self):
        self.project = Project()

    def tearDown(self):
        self.project.close()

    def test_each_kit_gets_the_project_names(self):
        for kit, packs in (('own_thread', []), ('unreal', ['ue'])):
            with self.subTest(kit=kit):
                project = Project()
                try:
                    code, out = project.run('init', '--kit', kit, '--name', 'my-mod')
                    self.assertEqual(code, 0, out)
                    self.assertEqual(out['dll'], 'build/my-mod.dll')
                    self.assertEqual(out['console'], 'build/my-mod-console.exe')
                    cmake = project.read('CMakeLists.txt')
                    self.assertIn('project(my-mod LANGUAGES CXX)', cmake)
                    self.assertIn('add_library(my-mod SHARED host.cpp)', cmake)
                    self.assertIn('OUTPUT_NAME my-mod-console)', cmake)
                    self.assertIn('git submodule update --init', cmake)
                    self.assertNotIn('../..', cmake)
                    self.assertTrue(cmake.startswith(f'# my-mod: a ReAgentLive host DLL'))
                    for file in ('CMakeLists.txt', 'host.cpp', 'console.cpp'):
                        self.assertIsNone(KIT_NAMES.search(project.read(file)), file)
                    for file in ('host.cpp', 'console.cpp'):
                        self.assertIn('L"Local\\\\my-mod-"', project.read(file))
                    record = json.loads(project.read('reagentlive.json'))
                    self.assertEqual(record, {'kit': kit, 'name': 'my-mod', 'dll': 'build/my-mod.dll',
                                              'console': 'build/my-mod-console.exe', 'packs': packs,
                                              'improvement': 'user'})
                finally:
                    project.close()

    def test_agent_files_mcp_and_links(self):
        code, out = self.project.run('init', '--kit', 'unreal', '--name', 'probe', '--mcp', 'claude-code,copilot',
                                     '--improvement', 'dev')
        self.assertEqual(code, 0, out)
        self.assertEqual(json.loads(self.project.read('reagentlive.json'))['improvement'], 'dev')
        self.assertEqual(self.project.read('CLAUDE.md'), '@AGENTS.md\n')
        self.assertIn('reagentlive/.claude/skills/reagentlive/SKILL.md', self.project.read('AGENTS.md'))
        claude = json.loads(self.project.read('.mcp.json'))['mcpServers']['reagentlive']
        self.assertEqual(claude['args'], ['reagentlive/src/reagentlive/frontends/mcp/reagentlive_mcp.py',
                                          '--console', 'build/probe-console.exe'])
        copilot = json.loads(self.project.read('.vscode/mcp.json'))['servers']['reagentlive']
        self.assertEqual(copilot['args'][-1], '${workspaceFolder}/build/probe-console.exe')
        self.assertFalse(os.path.exists(os.path.join(self.project.root, '.cursor')))
        ignored = self.project.read('.gitignore').split('\n')
        for link in out['links']:
            self.assertIn(link, ignored)
            skill = os.path.join(self.project.root, *link.split('/'), 'SKILL.md')
            self.assertTrue(os.path.isfile(skill), link)
        # Every repo path the generated files name exists in the clone.
        for file in ('README.md', 'AGENTS.md'):
            for path in re.findall(r'`(reagentlive/[^`\s]+\.(?:md|py|ps1))`', self.project.read(file)):
                if not path.startswith('reagentlive/src/'):
                    self.assertTrue(os.path.exists(os.path.join(REPO, *path.split('/')[1:])), path)

    def test_link_again_replaces_the_links(self):
        self.assertEqual(self.project.run('init', '--kit', 'own_thread', '--name', 'probe')[0], 0)
        code, out = self.project.run('link')
        self.assertEqual(code, 0, out)
        self.assertEqual(out['links'], ['.claude/skills/reagentlive', '.agents/skills/reagentlive'])

    def test_refusals(self):
        self.assertIn('--kit', self.project.run('init', '--kit', 'other', '--name', 'probe')[1]['error'])
        self.assertIn('--name', self.project.run('init', '--kit', 'unreal', '--name', 'My_Mod')[1]['error'])
        self.assertIn('--mcp', self.project.run('init', '--kit', 'unreal', '--name', 'p', '--mcp', 'x')[1]['error'])
        for taken in ('reagentlive-core', 'reagentlive', 'install', 'test'):
            self.assertIn('taken', self.project.run('init', '--kit', 'unreal', '--name', taken)[1]['error'], taken)
        self.assertEqual(self.project.run('init', '--kit', 'unreal', '--name', 'probe')[0], 0)
        code, out = self.project.run('init', '--kit', 'unreal', '--name', 'probe')
        self.assertEqual(code, 1)
        self.assertIn('already in', out['error'])
        loose = Project(submodule=False)
        try:
            code, out = loose.run('init', '--kit', 'unreal', '--name', 'probe')
            self.assertEqual(code, 1)
            self.assertIn('no reagentlive submodule', out['error'])
        finally:
            loose.close()


if __name__ == '__main__':
    unittest.main()
