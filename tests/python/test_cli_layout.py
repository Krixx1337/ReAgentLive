"""The frontends/cli layering rules (docs/contributing.md#source-layout), read from #include lines.

Usage: test_cli_layout.py [--host FILE ...]. Host files may include only the
console's public header and the parser.
"""
import os
import re
import sys
import tempfile
import unittest

CLI = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'reagentlive', 'frontends', 'cli')
PREFIX = 'reagentlive/frontends/cli/'
SYNTAX = 'syntax'
MIDDLE = ('values', 'state', 'results', 'code')
CONSOLE = 'console'
FOLDERS = (SYNTAX, *MIDDLE, CONSOLE)
HOST_HEADERS = (PREFIX + 'console/console.h', PREFIX + 'syntax/parse.h')
IO_HEADERS = ('fstream', 'iostream', 'filesystem')
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)

HOSTS = []


def includes(path):
    with open(path, encoding='utf-8') as file:
        return [(quote == '"', name) for quote, name in INCLUDE.findall(file.read())]


def check(cli, hosts=()):
    """Every broken rule, as 'file: reason'."""
    problems = []
    for entry in sorted(os.listdir(cli)):
        if entry not in FOLDERS:
            problems.append(f'{entry}: frontends/cli holds only the folders {", ".join(FOLDERS)}')
    for folder in FOLDERS:
        root = os.path.join(cli, folder)
        if not os.path.isdir(root):
            continue
        for name in sorted(os.listdir(root)):
            where = f'{folder}/{name}'
            for quoted, target in includes(os.path.join(root, name)):
                if not quoted:
                    if folder != CONSOLE and target.split('/')[0] in IO_HEADERS:
                        problems.append(f'{where}: <{target}>: only console/ does I/O')
                    if folder == SYNTAX and target.split('/')[0] in ('nlohmann', 'Zydis'):
                        problems.append(f'{where}: <{target}>: syntax/ builds without JSON and the decoder')
                    continue
                if not target.startswith('reagentlive/'):
                    problems.append(f'{where}: "{target}": include reagentlive headers by their full path')
                    continue
                if re.match(r'reagentlive/(core/agent|ue/agent)', target):
                    problems.append(f'{where}: "{target}": the console never includes the agent')
                if not target.startswith(PREFIX):
                    continue
                layer = target[len(PREFIX):].split('/')[0]
                if target == PREFIX + 'console/internal.h' and folder != CONSOLE:
                    problems.append(f'{where}: "{target}": console/internal.h is for console/ only')
                elif folder == SYNTAX and layer != SYNTAX:
                    problems.append(f'{where}: "{target}": syntax/ includes only syntax/, core/ and ue/')
                elif folder in MIDDLE and layer == CONSOLE:
                    problems.append(f'{where}: "{target}": {folder}/ never includes console/')
    for host in hosts:
        for quoted, target in includes(host):
            if quoted and target.startswith(PREFIX) and target not in HOST_HEADERS:
                problems.append(f'{os.path.basename(host)}: "{target}": hosts include only {" and ".join(HOST_HEADERS)}')
    return problems


class Tree:
    """A planted frontends/cli tree."""

    def __init__(self, files):
        self.dir = tempfile.TemporaryDirectory()
        for path, text in files.items():
            full = os.path.join(self.dir.name, path)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, 'w', encoding='utf-8') as file:
                file.write(text)

    def __enter__(self):
        return self.dir.name

    def __exit__(self, *_):
        self.dir.cleanup()


def inc(target):
    return f'#include "{target}"\n'


class CliLayout(unittest.TestCase):
    def test_the_tree_keeps_every_rule(self):
        self.assertEqual(check(CLI, HOSTS), [])

    def test_a_clean_planted_tree_passes(self):
        with Tree({'syntax/parse.h': inc('reagentlive/core/protocol.h') + inc('reagentlive/ue/client_ue.h'),
                   'values/format.h': inc(PREFIX + 'syntax/parse.h') + '#include <cstdio>\n',
                   'code/disasm.h': inc(PREFIX + 'values/format.h') + '#include <Zydis/Zydis.h>\n',
                   'console/sig.cpp': inc(PREFIX + 'console/internal.h') + '#include <fstream>\n'}) as cli:
            self.assertEqual(check(cli), [])

    def test_json_in_syntax_fails(self):
        with Tree({'syntax/parse.h': '#include <nlohmann/json.hpp>\n'}) as cli:
            self.assertEqual(len(check(cli)), 1)

    def test_syntax_reaching_up_fails(self):
        with Tree({'syntax/command.h': inc(PREFIX + 'values/format.h')}) as cli:
            self.assertEqual(len(check(cli)), 1)

    def test_middle_including_console_fails(self):
        with Tree({'results/census.h': inc(PREFIX + 'console/console.h'),
                   'state/cache.h': inc(PREFIX + 'console/internal.h')}) as cli:
            self.assertEqual(len(check(cli)), 2)

    def test_io_outside_console_fails(self):
        with Tree({'state/cache.h': '#include <filesystem>\n'}) as cli:
            self.assertEqual(len(check(cli)), 1)

    def test_a_loose_file_or_folder_fails(self):
        with Tree({'loose.h': '', 'extra/x.h': ''}) as cli:
            self.assertEqual(len(check(cli)), 2)

    def test_a_host_reaching_past_the_public_headers_fails(self):
        with Tree({'host.cpp': inc(PREFIX + 'console/console.h') + inc(PREFIX + 'syntax/parse.h') +
                   inc(PREFIX + 'values/format.h'), 'cli/syntax/parse.h': ''}) as root:
            self.assertEqual(len(check(os.path.join(root, 'cli'), [os.path.join(root, 'host.cpp')])), 1)


if __name__ == '__main__':
    args = sys.argv[1:]
    while args[:1] == ['--host']:
        HOSTS.append(args[1])
        args = args[2:]
    unittest.main(argv=[sys.argv[0], *args])
