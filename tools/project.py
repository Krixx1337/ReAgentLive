"""Make a project from one of ReAgentLive's kits, or link a project's skill again.

    python -I reagentlive/tools/project.py init --kit own_thread|unreal --name <name>
                                               [--improvement user|dev|off] [--mcp claude-code,cursor,copilot]
    python -I reagentlive/tools/project.py link

Run from a project that carries ReAgentLive as its `reagentlive/` submodule: the project is the folder
that holds this clone. `init` copies the kit (examples/<kit>: CMakeLists.txt, host.cpp, console.cpp) into
the project with its names made the project's (DLL `<name>.dll`, console `<name>-console.exe`, section
`Local\\<name>-<pid>`), writes the project record (reagentlive.json), README.md, AGENTS.md and CLAUDE.md,
.gitignore, the MCP config for each client in --mcp, and links the skill. It refuses when any of those
files exists. `link` (re)creates only the skill links, for a fresh clone of a project: a junction on
Windows (no admin rights), a symlink elsewhere, both ignored by git.

Prints one JSON object: `project`, `written` (files), `links`, `dll`, `console`; or `error`. Exit 0 or 1.
"""
import argparse
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.dirname(REPO)
SUBMODULE = 'reagentlive'
SKILL = os.path.join(REPO, '.claude', 'skills', 'reagentlive')
LINKS = ('.claude/skills/reagentlive', '.agents/skills/reagentlive')
RECORD = 'reagentlive.json'
NAME = re.compile(r'[a-z][a-z0-9-]{0,31}')
# Target names CMake keeps for itself (CMP0037); a name starting with reagentlive would collide with the library's.
RESERVED = {'all', 'clean', 'help', 'install', 'test', 'package', 'package_source', 'edit_cache', 'rebuild_cache'}

# Per kit: the packs its console carries, and the exact strings that carry the kit's own names. Each one
# must occur in its file, so a kit edit that drops one fails here (and in reagentlive.project) instead of
# leaving a kit name behind.
KITS = {
    'own_thread': {
        'packs': [],
        'renames': {
            'CMakeLists.txt': [('reagentlive-console-exe', '{name}-console-exe'),
                               ('OUTPUT_NAME reagentlive)', 'OUTPUT_NAME {name}-console)'),
                               ('reagentlive-own-thread', '{name}')],
            'host.cpp': [('L"Local\\\\reagentlive-"', 'L"Local\\\\{name}-"')],
            'console.cpp': [('L"Local\\\\reagentlive-"', 'L"Local\\\\{name}-"')],
        },
    },
    'unreal': {
        'packs': ['ue'],
        'renames': {
            'CMakeLists.txt': [('reagentlive-ue-console-exe', '{name}-console-exe'),
                               ('OUTPUT_NAME reagentlive-ue)', 'OUTPUT_NAME {name}-console)'),
                               ('reagentlive-unreal', '{name}')],
            'host.cpp': [('L"Local\\\\reagentlive-ue-"', 'L"Local\\\\{name}-"')],
            'console.cpp': [('L"Local\\\\reagentlive-ue-"', 'L"Local\\\\{name}-"')],
        },
    },
}

MCP_SCRIPT = f'{SUBMODULE}/src/reagentlive/frontends/mcp/reagentlive_mcp.py'
# Claude Code starts a project's servers in the folder it runs in; Cursor and VS Code expand ${workspaceFolder}.
MCP_FILES = {
    'claude-code': ('.mcp.json', 'mcpServers', ''),
    'cursor': ('.cursor/mcp.json', 'mcpServers', '${workspaceFolder}/'),
    'copilot': ('.vscode/mcp.json', 'servers', '${workspaceFolder}/'),
}


class Refused(Exception):
    pass


def check_project():
    """The project must hold this clone as its reagentlive/ submodule."""
    if os.path.basename(REPO) != SUBMODULE:
        raise Refused(f'this clone is {REPO}; a project carries it as its {SUBMODULE}/ submodule')
    modules = os.path.join(PROJECT, '.gitmodules')
    text = open(modules, encoding='utf-8').read() if os.path.isfile(modules) else ''
    if not re.search(rf'^\s*path\s*=\s*{SUBMODULE}\s*$', text, re.M):
        raise Refused(f'{PROJECT} has no {SUBMODULE} submodule: '
                      f'git -C <project> submodule add <ReAgentLive URL> {SUBMODULE}')


def kit_files(kit, name):
    """The kit's files with the project's names: {file: text}."""
    files = {}
    for file, renames in KITS[kit]['renames'].items():
        with open(os.path.join(REPO, 'examples', kit, file), encoding='utf-8') as handle:
            text = handle.read()
        for old, new in renames:
            if old not in text:
                raise Refused(f'examples/{kit}/{file} has no {old}: update KITS in tools/project.py')
            text = text.replace(old, new.format(name=name))
        files[file] = text
    # A project has no library two folders up: without the submodule's files, say how to get them.
    fallback = '    else()\n        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." reagentlive)\n'
    if fallback not in files['CMakeLists.txt']:
        raise Refused(f'examples/{kit}/CMakeLists.txt has no in-tree fallback: update kit_files in tools/project.py')
    files['CMakeLists.txt'] = files['CMakeLists.txt'].replace(fallback, (
        '    else()\n        message(FATAL_ERROR "reagentlive/ is empty: git submodule update --init")\n'))
    # The kit's header comment describes the example; the project's says where it came from.
    lines = files['CMakeLists.txt'].split('\n')
    while lines and lines[0].startswith('#'):
        lines.pop(0)
    header = [f'# {name}: a ReAgentLive host DLL and its console, made from the {kit} kit',
              f'# ({SUBMODULE}/examples/{kit}). Build: {SUBMODULE}/tools/build_project.ps1 (README.md).']
    files['CMakeLists.txt'] = '\n'.join(header + lines)
    return files


def project_files(kit, name, improvement, clients):
    """Everything init writes: {path relative to the project: text}."""
    dll, console = f'build/{name}.dll', f'build/{name}-console.exe'
    files = kit_files(kit, name)
    record = {'kit': kit, 'name': name, 'dll': dll, 'console': console,
              'packs': KITS[kit]['packs'], 'improvement': improvement}
    files[RECORD] = json.dumps(record, indent=2) + '\n'
    build = f'powershell -NoProfile -ExecutionPolicy Bypass -File {SUBMODULE}\\tools\\build_project.ps1'
    files['README.md'] = f'''# {name}

A ReAgentLive host DLL and its console, made from the `{kit}` kit
(`{SUBMODULE}/examples/{kit}/README.md`). `host.cpp` and `console.cpp` are
yours to change; ReAgentLive itself is the `{SUBMODULE}/` submodule, updated
by moving its commit, never edited here.

- **Build** (loads Visual Studio's x64 tools itself; `-Check` also loads the
  DLL into a throwaway Python process and checks its `status`):
  `{build}`
- **Output:** `{dll}` (load it into the target with any DLL loader) and
  `{console}`.
- **After a fresh clone:** `git submodule update --init`, then
  `python -I {SUBMODULE}/tools/project.py link`, then build.
- **Hosting** (what `host.cpp` must provide): `{SUBMODULE}/docs/hosting.md`.
- **Update ReAgentLive:** ask your agent to "update ReAgentLive", or follow
  `{SUBMODULE}/docs/agents/update.md`.
'''
    files['AGENTS.md'] = f'''# {name}

A ReAgentLive project (`{kit}` kit): `host.cpp` is the agent DLL, `console.cpp`
its console. ReAgentLive is the `{SUBMODULE}/` submodule: never edit it here.

- Before investigating a running process with the reagentlive tools, read
  `{SUBMODULE}/.claude/skills/reagentlive/SKILL.md` and follow it.
- ReAgentLive docs: `{SUBMODULE}/docs/README.md`; changing the host:
  `{SUBMODULE}/docs/hosting.md`.
- Build: `{build}` (`-Check` to verify). DLL `{dll}`, console `{console}`.
- Project record (kit, outputs, improvement mode): `{RECORD}`.
- Update ReAgentLive: `{SUBMODULE}/docs/agents/update.md`.
'''
    files['CLAUDE.md'] = '@AGENTS.md\n'
    files['.gitignore'] = 'build/\n' + ''.join(f'{link}\n' for link in LINKS)
    for client in clients:
        path, key, root = MCP_FILES[client]
        server = {'command': 'python', 'args': [root + MCP_SCRIPT, '--console', root + console]}
        files[path] = json.dumps({key: {'reagentlive': server}}, indent=2) + '\n'
    return files


def is_link(path):
    """A symlink or (on Windows) a junction."""
    try:
        os.readlink(path)
        return True
    except (OSError, ValueError, NotImplementedError):
        return False


def link_skill():
    """Each skill link points at this clone's skill; returns the links made."""
    made = []
    for relative in LINKS:
        link = os.path.join(PROJECT, *relative.split('/'))
        if is_link(link):
            os.rmdir(link) if os.name == 'nt' else os.unlink(link)
        elif os.path.exists(link):
            raise Refused(f'{relative} is a folder, not a link: move it away first')
        os.makedirs(os.path.dirname(link), exist_ok=True)
        if os.name == 'nt':
            try:
                import _winapi
                _winapi.CreateJunction(SKILL, link)
            except (ImportError, AttributeError, OSError):
                result = subprocess.run(['cmd', '/c', 'mklink', '/J', link, SKILL], capture_output=True, text=True)
                if result.returncode:
                    raise Refused(f'cannot link {relative}: {result.stdout.strip()} {result.stderr.strip()}')
        else:
            os.symlink(SKILL, link, target_is_directory=True)
        made.append(relative)
    return made


def init(args):
    if args.kit not in KITS:
        raise Refused(f'--kit is one of {", ".join(KITS)}')
    if not NAME.fullmatch(args.name):
        raise Refused('--name: lowercase letters, digits and dashes, starting with a letter, at most 32')
    if args.name.startswith('reagentlive') or args.name in RESERVED:
        raise Refused(f'--name {args.name}: taken by ReAgentLive or CMake; pick a name of the project\'s own')
    clients = [c.strip() for c in args.mcp.split(',') if c.strip()]
    unknown = [c for c in clients if c not in MCP_FILES]
    if unknown:
        raise Refused(f'--mcp: unknown {", ".join(unknown)} (one or more of {", ".join(MCP_FILES)})')
    check_project()
    files = project_files(args.kit, args.name, args.improvement, clients)
    taken = [path for path in files if os.path.lexists(os.path.join(PROJECT, *path.split('/')))]
    if taken:
        raise Refused(f'already in {PROJECT}: {", ".join(sorted(taken))}; init writes only a new project')
    for path, text in files.items():
        target = os.path.join(PROJECT, *path.split('/'))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, 'w', encoding='utf-8', newline='\n') as handle:
            handle.write(text)
    record = json.loads(files[RECORD])
    return {'project': PROJECT, 'written': sorted(files), 'links': link_skill(),
            'dll': record['dll'], 'console': record['console']}


def link(_args):
    check_project()
    return {'project': PROJECT, 'links': link_skill()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    make = commands.add_parser('init', help='make a project from a kit')
    make.add_argument('--kit', required=True)
    make.add_argument('--name', required=True)
    make.add_argument('--improvement', choices=('user', 'dev', 'off'), default='user')
    make.add_argument('--mcp', default='claude-code', help='comma-separated: ' + ', '.join(MCP_FILES))
    make.set_defaults(run=init)
    again = commands.add_parser('link', help='link the skill again')
    again.set_defaults(run=link)
    args = parser.parse_args(argv)
    try:
        result = args.run(args)
    except Refused as refusal:
        print(json.dumps({'error': str(refusal)}))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
