"""The frontends/cli layering rules and the pack boundary (docs/contributing.md#source-layout), read from #include lines.

Two kinds of rule: layers (syntax, the middle folders, console) and the pack
boundary. Each engine pack lives in src/reagentlive/packs/<id>/: its agent and
client files at the top, its part of each CLI layer in cli/<layer>/, under
that layer's rule. Outside packs/ nothing includes a pack, names <id>:: or
spells a pack's id as a string, and a pack never includes another pack. The
boundary holds beyond frontends/cli too: core/ never includes packs/, and the
Python generator and the MCP adapter, which read only `help --json`, never
spell a pack's id.

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
PACKS = os.path.join(SRC, 'packs')
# Frontends that read the table only, so they must name no pack.
TABLE_READERS = (os.path.join(SRC, 'frontends', 'python', 'reagentlive_gen.py'),
                 os.path.join(SRC, 'frontends', 'python', 'reagentlive_session.py'),
                 os.path.join(SRC, 'frontends', 'mcp', 'reagentlive_mcp.py'))
PREFIX = 'reagentlive/frontends/cli/'
PACKS_PREFIX = 'reagentlive/packs/'
SYNTAX = 'syntax'
MIDDLE = ('values', 'state', 'results', 'code')
CONSOLE = 'console'
FOLDERS = (SYNTAX, *MIDDLE, CONSOLE)
PACK_FILES = ('pack.cmake',)  # A pack's top holds its sources, pack.cmake and cli/.
HOST_HEADERS = (PREFIX + 'console/console.h', PREFIX + 'syntax/parse.h', PREFIX + 'syntax/verb.h',
                PREFIX + 'results/schema.h')
IO_HEADERS = ('fstream', 'iostream', 'filesystem')
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
PY_COMMENT = re.compile(r'#[^\n]*')

HOSTS = []


def read(path):
    with open(path, encoding='utf-8') as file:
        return file.read()


def includes(text):
    return [(quote == '"', name) for quote, name in INCLUDE.findall(text)]


def pack_ids(packs):
    if not packs or not os.path.isdir(packs):
        return []
    return sorted(name for name in os.listdir(packs) if os.path.isdir(os.path.join(packs, name)))


def pack_name(pack):
    """<id>:: as a name, outside comments."""
    return re.compile(rf'(?<![A-Za-z0-9_]){re.escape(pack)}::')


def pack_string(pack):
    """The pack's id as a string literal."""
    return re.compile(rf'["\']{re.escape(pack)}["\'.]')


def target_pack(target):
    """The pack a header belongs to, or None."""
    if target.startswith(PACKS_PREFIX):
        return target[len(PACKS_PREFIX):].split('/')[0]
    return None


def target_layer(target):
    """The CLI layer a header belongs to (core or a pack's part), or None."""
    if target.startswith(PREFIX):
        return target[len(PREFIX):].split('/')[0]
    pack = target_pack(target)
    rest = target[len(PACKS_PREFIX) + len(pack) + 1:] if pack else ''
    if rest.startswith('cli/'):
        return rest[len('cli/'):].split('/')[0]
    return None


def layer_files(root, label, pack, problems):
    """(layer, pack, where, full path) for the files of each layer folder in root."""
    found = []
    if not os.path.isdir(root):
        return found
    for entry in sorted(os.listdir(root)):
        if entry not in FOLDERS:
            problems.append(f'{label}{entry}: holds only the folders {", ".join(FOLDERS)}')
    for folder in FOLDERS:
        layer_root = os.path.join(root, folder)
        if not os.path.isdir(layer_root):
            continue
        for name in sorted(os.listdir(layer_root)):
            full = os.path.join(layer_root, name)
            if os.path.isdir(full):
                problems.append(f'{label}{folder}/{name}: a layer holds files only; a pack\'s part goes in packs/<id>/cli/{folder}/')
            else:
                found.append((folder, pack, f'{label}{folder}/{name}', full))
    return found


def files(cli, packs=None):
    """(layer, pack, where, full path) for every CLI and pack file; problems for misplaced ones.
    layer is None for a pack's agent and client files; pack is None outside packs/."""
    problems = []
    found = layer_files(cli, '', None, problems)
    for pack in pack_ids(packs):
        root = os.path.join(packs, pack)
        for name in sorted(os.listdir(root)):
            full = os.path.join(root, name)
            if name == 'cli' and os.path.isdir(full):
                found += layer_files(full, f'packs/{pack}/cli/', pack, problems)
            elif os.path.isdir(full):
                problems.append(f'packs/{pack}/{name}: a pack holds its files, pack.cmake and cli/ only')
            elif name not in PACK_FILES:
                found.append((None, pack, f'packs/{pack}/{name}', full))
    return found, problems


def core_files(core):
    if not core or not os.path.isdir(core):
        return []
    return [os.path.join(root, name) for root, _, names in os.walk(core) for name in sorted(names)]


def check(cli, hosts=(), core=None, readers=(), packs=None):
    """Every broken rule, as 'file: reason'."""
    found, problems = files(cli, packs)
    ids = pack_ids(packs)
    for folder, pack, where, full in found:
        text = read(full)
        code = COMMENT.sub('', text)
        for other in ids:
            if other == pack:
                continue
            if pack_name(other).search(code):
                problems.append(f'{where}: names {other}::; that part goes in packs/{other}/')
            if pack_string(other).search(code):
                problems.append(f'{where}: spells the pack id "{other}"; a pack gives its own id and title')
        for quoted, target in includes(text):
            if not quoted:
                if folder and folder != CONSOLE and target.split('/')[0] in IO_HEADERS:
                    problems.append(f'{where}: <{target}>: only console/ does I/O')
                if folder == SYNTAX and target.split('/')[0] in ('nlohmann', 'Zydis'):
                    problems.append(f'{where}: <{target}>: syntax/ builds without JSON and the decoder')
                continue
            owner = target_pack(target)
            if owner and owner != pack:
                problems.append(f'{where}: "{target}": only packs/{owner}/ includes that pack')
            if folder is None:
                continue  # A pack's agent and client files (they include its SDK): the rule above only.
            if not target.startswith('reagentlive/'):
                problems.append(f'{where}: "{target}": include reagentlive headers by their full path')
                continue
            if re.match(r'reagentlive/(core/agent|packs/[^/]+/agent)', target):
                problems.append(f'{where}: "{target}": the console never includes the agent')
            layer = target_layer(target)
            if layer is None:
                continue
            if target in (PREFIX + 'console/internal.h', PREFIX + 'console/pack.h') and folder != CONSOLE:
                problems.append(f'{where}: "{target}": console internals are for console/ only')
            elif owner and target == f'{PACKS_PREFIX}{owner}/cli/console/internal_{owner}.h' and \
                    not (folder == CONSOLE and pack == owner):
                problems.append(f'{where}: "{target}": internal_{owner}.h is for packs/{owner}/cli/console/ only')
            elif folder == SYNTAX and layer != SYNTAX:
                problems.append(f'{where}: "{target}": syntax/ includes only syntax/, core/ and its pack')
            elif folder in MIDDLE and layer == CONSOLE:
                problems.append(f'{where}: "{target}": {folder}/ never includes console/')
    for path in core_files(core):
        for quoted, target in includes(read(path)):
            if quoted and target_pack(target):
                problems.append(f'core/{os.path.basename(path)}: "{target}": core/ never includes a pack')
    for path in readers:
        if not os.path.exists(path):
            continue
        code = PY_COMMENT.sub('', read(path))
        for pack in ids:
            if pack_string(pack).search(code):
                problems.append(f'{os.path.basename(path)}: spells the pack id "{pack}"; it reads help --json only')
    for host in hosts:
        for quoted, target in includes(read(host)):
            if quoted and target.startswith(PREFIX) and target not in HOST_HEADERS:
                problems.append(f'{os.path.basename(host)}: "{target}": hosts include only {", ".join(HOST_HEADERS)}')
    return problems


class Tree:
    """A planted tree."""

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


UE = PACKS_PREFIX + 'ue/'


def planted(root, **kwargs):
    return check(os.path.join(root, 'cli'), packs=os.path.join(root, 'packs'), **kwargs)


class CliLayout(unittest.TestCase):
    def test_the_tree_keeps_every_rule(self):
        self.assertEqual(pack_ids(PACKS), ['ue'])
        self.assertEqual(check(CLI, HOSTS, CORE, TABLE_READERS, PACKS), [])

    def test_the_pack_id_outside_the_pack_fails(self):
        with Tree({'cli/results/docs.h': 'auto t = origin == "ue" ? 1 : 0;\n// "ue" in a comment is fine\n',
                   'packs/ue/cli/results/verbs_ue.h': 'Origin o{"ue", "Unreal pack"};\n',
                   'core/agent.h': inc(UE + 'protocol_ue.h'),
                   'gen.py': "if origin == 'ue': pass\n# 'ue' in a comment is fine\n"}) as root:
            problems = planted(root, core=os.path.join(root, 'core'), readers=[os.path.join(root, 'gen.py')])
            self.assertEqual(len(problems), 3, problems)  # docs.h, core/agent.h, gen.py.

    def test_a_clean_planted_tree_passes(self):
        with Tree({'cli/syntax/parse.h': inc('reagentlive/core/protocol.h'),
                   'cli/values/format.h': inc(PREFIX + 'syntax/parse.h') + '#include <cstdio>\n// ue:: in a comment is fine\n',
                   'cli/code/disasm.h': inc(PREFIX + 'values/format.h') + '#include <Zydis/Zydis.h>\n',
                   'cli/console/sig.cpp': inc(PREFIX + 'console/internal.h') + inc(PREFIX + 'console/pack.h') +
                   '#include <fstream>\n',
                   'packs/ue/pack.cmake': '',
                   'packs/ue/agent_ue.cpp': inc('reagentlive/core/agent.h') + inc(UE + 'protocol_ue.h'),
                   'packs/ue/cli/syntax/parse_unreal.cpp': inc(PREFIX + 'syntax/parse.h') + inc(UE + 'protocol_ue.h') +
                   'int x = ue::Find;\n',
                   'packs/ue/cli/values/json_output.h': inc(PREFIX + 'values/format.h') + inc(UE + 'client_ue.h'),
                   'packs/ue/cli/console/pack.cpp': inc(UE + 'cli/console/internal_ue.h') +
                   inc(UE + 'cli/values/json_output.h') + inc(PREFIX + 'console/pack.h')}) as root:
            self.assertEqual(planted(root), [])

    def test_json_in_syntax_fails(self):
        with Tree({'cli/syntax/parse.h': '#include <nlohmann/json.hpp>\n',
                   'packs/ue/cli/syntax/parse_unreal.cpp': '#include <nlohmann/json.hpp>\n'}) as root:
            self.assertEqual(len(planted(root)), 2)

    def test_syntax_reaching_up_fails(self):
        with Tree({'cli/syntax/command.h': inc(PREFIX + 'values/format.h'),
                   'packs/ue/cli/syntax/parse_unreal.cpp': inc(UE + 'cli/values/format_ue.h')}) as root:
            self.assertEqual(len(planted(root)), 2)

    def test_middle_including_console_fails(self):
        with Tree({'cli/results/census.h': inc(PREFIX + 'console/console.h'),
                   'cli/state/cache.h': inc(PREFIX + 'console/internal.h'),
                   'packs/ue/cli/results/census.h': inc(UE + 'cli/console/internal_ue.h')}) as root:
            self.assertEqual(len(planted(root)), 3)

    def test_io_outside_console_fails(self):
        with Tree({'cli/state/cache.h': '#include <filesystem>\n',
                   'packs/ue/cli/state/cache_ue.h': '#include <fstream>\n'}) as root:
            self.assertEqual(len(planted(root)), 2)

    def test_a_loose_file_or_folder_fails(self):
        with Tree({'cli/loose.h': '', 'cli/extra/x.h': '', 'cli/values/other/x.h': '', 'cli/values/ue/x.h': '',
                   'packs/ue/cli/loose.h': '', 'packs/ue/cli/values/deeper/x.h': '', 'packs/ue/extra/x.h': ''}) as root:
            self.assertEqual(len(planted(root)), 7)

    def test_core_reaching_into_the_pack_fails(self):
        with Tree({'cli/syntax/command.h': inc(UE + 'protocol_ue.h'),
                   'cli/results/result_json.h': inc(UE + 'cli/results/census.h'),
                   'cli/console/memory.cpp': 'auto x = ue::op::Find;\n',
                   'cli/console/code.cpp': inc(UE + 'cli/console/internal_ue.h'),
                   'packs/ue/agent_ue.h': ''}) as root:
            problems = planted(root)
            self.assertEqual(len(problems), 5, problems)  # code.cpp breaks two rules.

    def test_a_pack_reaching_into_another_pack_fails(self):
        with Tree({'packs/ue/agent_ue.h': '',
                   'packs/unity/agent_unity.cpp': inc(UE + 'agent_ue.h'),
                   'packs/unity/cli/console/pack.cpp': inc(UE + 'cli/console/internal_ue.h') + 'auto x = ue::op::Find;\n',
                   'packs/unity/cli/values/format_unity.h': inc(PACKS_PREFIX + 'unity/cli/values/x.h')}) as root:
            problems = planted(root)
            # agent_unity.cpp; pack.cpp: the include twice (other pack, internal_ue.h) and ue::.
            self.assertEqual(len(problems), 4, problems)

    def test_the_console_including_a_pack_agent_fails(self):
        with Tree({'packs/ue/cli/console/pack.cpp': inc(UE + 'agent_ue.h'),
                   'packs/ue/client_ue.cpp': inc(UE + 'agent_ue.h')}) as root:
            self.assertEqual(len(planted(root)), 1)  # The agent file itself may.

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
