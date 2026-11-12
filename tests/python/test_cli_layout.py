"""The frontends/cli layering rules (docs/contributing.md#source-layout), read from #include lines.

Two kinds of rule: layers (syntax, the middle folders, console) and the pack
boundary (each layer's Unreal part lives in its ue/ folder; nothing outside a
ue/ folder includes the Unreal pack, names ue:: or spells the pack's id as a
string). The boundary holds beyond frontends/cli too: core/ never includes
ue/, and the Python generator and the MCP adapter, which read only
`help --json`, never spell the pack's id.

Usage: test_cli_layout.py [--host FILE ...]. Host files may include only the
console's public header, the parser, the verb table's row and the result schema.
"""
import os
import re
import sys
import tempfile
import unittest

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'reagentlive')
CLI = os.path.join(SRC, 'frontends', 'cli')
CORE = os.path.join(SRC, 'core')
# Frontends that read the table only, so they must name no pack.
TABLE_READERS = (os.path.join(SRC, 'frontends', 'python', 'reagentlive_gen.py'),
                 os.path.join(SRC, 'frontends', 'python', 'reagentlive_session.py'),
                 os.path.join(SRC, 'frontends', 'mcp', 'reagentlive_mcp.py'))
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
# ue:: as a name, outside comments; the pack's id as a string literal.
PACK_NAME = re.compile(r'(?<![A-Za-z0-9_])ue::')
PACK_STRING = re.compile(r'["\']ue["\'.]')
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
PY_COMMENT = re.compile(r'#[^\n]*')

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


def core_files(core):
    if not core or not os.path.isdir(core):
        return []
    return [os.path.join(root, name) for root, _, names in os.walk(core) for name in sorted(names)]


def check(cli, hosts=(), core=None, readers=()):
    """Every broken rule, as 'file: reason'."""
    found, problems = files(cli)
    for folder, in_pack, where, full in found:
        text = read(full)
        if not in_pack and PACK_NAME.search(COMMENT.sub('', text)):
            problems.append(f'{where}: names ue::; the Unreal part goes in {folder}/{PACK}/')
        if not in_pack and PACK_STRING.search(COMMENT.sub('', text)):
            problems.append(f'{where}: spells the pack id "{PACK}"; a pack gives its own id and title')
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
    for path in core_files(core):
        for quoted, target in includes(read(path)):
            if quoted and (target.startswith('reagentlive/ue/') or pack_include(target)):
                problems.append(f'core/{os.path.basename(path)}: "{target}": core/ never includes the pack')
    for path in readers:
        if os.path.exists(path) and PACK_STRING.search(PY_COMMENT.sub('', read(path))):
            problems.append(f'{os.path.basename(path)}: spells the pack id "{PACK}"; it reads help --json only')
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
        self.assertEqual(check(CLI, HOSTS, CORE, TABLE_READERS), [])

    def test_the_pack_id_outside_the_pack_fails(self):
        with Tree({'cli/results/docs.h': 'auto t = origin == "ue" ? 1 : 0;\n// "ue" in a comment is fine\n',
                   'cli/results/ue/verbs_ue.h': 'Origin o{"ue", "Unreal pack"};\n',
                   'core/agent.h': inc('reagentlive/ue/protocol_ue.h'),
                   'gen.py': "if origin == 'ue': pass\n# 'ue' in a comment is fine\n"}) as root:
            problems = check(os.path.join(root, 'cli'), core=os.path.join(root, 'core'),
                             readers=[os.path.join(root, 'gen.py')])
            self.assertEqual(len(problems), 3, problems)  # docs.h, core/agent.h, gen.py.

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
