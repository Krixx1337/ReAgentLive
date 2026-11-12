"""The frontends/cli layering rules (docs/contributing.md#source-layout), read from #include lines.

Two kinds of rule: layers (syntax, the middle folders, console) and the pack
boundary (each layer's Unreal part lives in its ue/ folder; nothing outside a
ue/ folder includes the Unreal pack or names ue::).

Usage: test_cli_layout.py [--host FILE ...]. Host files may include only the
console's public header, the parser, the verb table's row and the result schema.
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
PACK = 'ue'
HOST_HEADERS = (PREFIX + 'console/console.h', PREFIX + 'syntax/parse.h', PREFIX + 'syntax/verb.h',
                PREFIX + 'results/schema.h')
IO_HEADERS = ('fstream', 'iostream', 'filesystem')
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)
# ue:: as a name, outside comments.
PACK_NAME = re.compile(r'(?<![A-Za-z0-9_])ue::')
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)

HOSTS = []


def read(path):
    with open(path, encoding='utf-8') as file:
        return file.read()


def includes(text):
    return [(quote == '"', name) for quote, name in INCLUDE.findall(text)]


def pack_include(target):
    """The Unreal pack's own headers, or a ue/ part of a CLI layer."""
    if target.startswith('reagentlive/ue/'):
        return True
    return target.startswith(PREFIX) and f'/{PACK}/' in target[len(PREFIX):]


def files(cli):
    """(layer, in_pack, relative path, full path) for every file; problems for misplaced ones."""
    found, problems = [], []
    for entry in sorted(os.listdir(cli)):
        if entry not in FOLDERS:
            problems.append(f'{entry}: frontends/cli holds only the folders {", ".join(FOLDERS)}')
    for folder in FOLDERS:
        root = os.path.join(cli, folder)
        if not os.path.isdir(root):
            continue
        for name in sorted(os.listdir(root)):
            full = os.path.join(root, name)
            if not os.path.isdir(full):
                found.append((folder, False, f'{folder}/{name}', full))
            elif name != PACK:
                problems.append(f'{folder}/{name}: a layer holds files and its {PACK}/ folder only')
            else:
                for inner in sorted(os.listdir(full)):
                    path = os.path.join(full, inner)
                    if os.path.isdir(path):
                        problems.append(f'{folder}/{PACK}/{inner}: no folders inside {PACK}/')
                    else:
                        found.append((folder, True, f'{folder}/{PACK}/{inner}', path))
    return found, problems


def check(cli, hosts=()):
    """Every broken rule, as 'file: reason'."""
    found, problems = files(cli)
    for folder, in_pack, where, full in found:
        text = read(full)
        if not in_pack and PACK_NAME.search(COMMENT.sub('', text)):
            problems.append(f'{where}: names ue::; the Unreal part goes in {folder}/{PACK}/')
        for quoted, target in includes(text):
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
            if not in_pack and pack_include(target):
                problems.append(f'{where}: "{target}": only {PACK}/ folders include the Unreal pack')
            if not target.startswith(PREFIX):
                continue
            layer = target[len(PREFIX):].split('/')[0]
            if target in (PREFIX + 'console/internal.h', PREFIX + 'console/pack.h') and folder != CONSOLE:
                problems.append(f'{where}: "{target}": console internals are for console/ only')
            elif target == PREFIX + f'console/{PACK}/internal_ue.h' and not (folder == CONSOLE and in_pack):
                problems.append(f'{where}: "{target}": console/{PACK}/internal_ue.h is for console/{PACK}/ only')
            elif folder == SYNTAX and layer != SYNTAX:
                problems.append(f'{where}: "{target}": syntax/ includes only syntax/, core/ and the pack')
            elif folder in MIDDLE and layer == CONSOLE:
                problems.append(f'{where}: "{target}": {folder}/ never includes console/')
    for host in hosts:
        for quoted, target in includes(read(host)):
            if quoted and target.startswith(PREFIX) and target not in HOST_HEADERS:
                problems.append(f'{os.path.basename(host)}: "{target}": hosts include only {", ".join(HOST_HEADERS)}')
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
        with Tree({'syntax/parse.h': inc('reagentlive/core/protocol.h'),
                   'syntax/ue/parse_unreal.cpp': inc(PREFIX + 'syntax/parse.h') + inc('reagentlive/ue/protocol_ue.h') +
                   'int x = ue::Find;\n',
                   'values/format.h': inc(PREFIX + 'syntax/parse.h') + '#include <cstdio>\n// ue:: in a comment is fine\n',
                   'values/ue/json_output.h': inc(PREFIX + 'values/format.h') + inc('reagentlive/ue/client_ue.h'),
                   'code/disasm.h': inc(PREFIX + 'values/format.h') + '#include <Zydis/Zydis.h>\n',
                   'console/sig.cpp': inc(PREFIX + 'console/internal.h') + inc(PREFIX + 'console/pack.h') +
                   '#include <fstream>\n',
                   'console/ue/pack.cpp': inc(PREFIX + 'console/ue/internal_ue.h') +
                   inc(PREFIX + 'values/ue/json_output.h')}) as cli:
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
        with Tree({'loose.h': '', 'extra/x.h': '', 'values/other/x.h': '', 'values/ue/deeper/x.h': ''}) as cli:
            self.assertEqual(len(check(cli)), 4)

    def test_core_reaching_into_the_pack_fails(self):
        with Tree({'syntax/command.h': inc('reagentlive/ue/protocol_ue.h'),
                   'results/result_json.h': inc(PREFIX + 'results/ue/census.h'),
                   'console/memory.cpp': 'auto x = ue::op::Find;\n',
                   'console/code.cpp': inc(PREFIX + 'console/ue/internal_ue.h')}) as cli:
            problems = check(cli)
            self.assertEqual(len(problems), 5, problems) # code.cpp breaks two rules.

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
