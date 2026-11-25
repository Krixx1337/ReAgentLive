"""Wire an AI agent to a host project: the MCP server for its console, the skill, agent instructions.

    python -I <clone>/tools/agent_link.py --console <exe> --dll <dll> [--build <command>] [--check yes|no]
                                          [--approval read-only|auto|bypass] [--mcp claude-code,cursor,copilot]
                                          [--project <dir>]

The project is any folder that builds a host DLL and its console and carries this clone at a fixed path
inside it (a git submodule; default project: the git project it is a submodule of, else pass --project).
Paths are relative to the project. Writes, and on every later run updates in place:

- reagentlive.json, the project record: `library` (this clone's path in the project), `mcp` (the clients),
  `console`, `dll`, `build` (a command run in the project folder;
  without it tools/build_project.ps1 runs CMake on the project), `check` (false for a host that ticks
  only inside the game, so the update builds without loading the DLL into a test process), `approval`
  (what the agent may change in the target unasked: `read-only`, `auto` (the default) or `bypass`;
  the skill's Approval section). Arguments
  left out keep the record's values. It is committed: nothing personal goes in it (the improvement mode is
  the machine record's).
- the ReAgentLive server in each client's project MCP file: the one already there (any server running
  reagentlive_mcp.py, under its own name, keeping its other arguments), else a new `reagentlive`; other
  servers stay;
- a ReAgentLive block in AGENTS.md, between `<!-- reagentlive -->` markers (an existing CLAUDE.md that does
  not import AGENTS.md gets the block too; none is made: Claude Code reads AGENTS.md);
- a pointer skill in .claude/skills/reagentlive and .agents/skills/reagentlive: a SKILL.md with the
  skill's name and description that sends the agent to the submodule's skill. It is committed, so a clone
  has the skill at once; run this again when an update changes the skill's description (update.ps1 says
  so). A skill folder there that the project tracks without the pointer marker is its own and stays.

Nothing outside the project is written. Prints one JSON object: `project`, `record`, `written`, `servers`
(each client's server name), `kept` (the project's own skill folders); or `error`. Exit 0 or 1.
"""
import argparse
import json
import os
import re
import subprocess
import sys

CLONE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKILL = os.path.join(CLONE, '.claude', 'skills', 'reagentlive', 'SKILL.md')
SKILL_DIRS = ('.claude/skills/reagentlive', '.agents/skills/reagentlive')
POINTER = '<!-- reagentlive pointer: written by tools/agent_link.py -->'
RECORD = 'reagentlive.json'
SERVER_SCRIPT = 'src/reagentlive/frontends/mcp/reagentlive_mcp.py'
BEGIN, END = '<!-- reagentlive -->', '<!-- /reagentlive -->'
# Claude Code starts a project's servers in the folder it runs in; Cursor and VS Code expand ${workspaceFolder}.
MCP_FILES = {
    'claude-code': ('.mcp.json', 'mcpServers', ''),
    'cursor': ('.cursor/mcp.json', 'mcpServers', '${workspaceFolder}/'),
    'copilot': ('.vscode/mcp.json', 'servers', '${workspaceFolder}/'),
}


class Refused(Exception):
    pass


def slash(path):
    return path.replace('\\', '/')


def read_json(path, what):
    """A JSON object from path, {} when the file is missing."""
    if not os.path.isfile(path):
        return {}
    try:
        with open(path, encoding='utf-8-sig') as handle:
            value = json.load(handle)
    except (OSError, ValueError) as error:
        raise Refused(f'{what} {path} is not readable JSON ({error}): fix or remove it')
    if not isinstance(value, dict):
        raise Refused(f'{what} {path} is not a JSON object: fix or remove it')
    return value


def read_text(path):
    """A text file's contents with LF line ends, and its own line end ('\r\n' or '\n')."""
    with open(path, encoding='utf-8', newline='') as handle:
        text = handle.read()
    return text.replace('\r\n', '\n'), '\r\n' if '\r\n' in text else '\n'


def write(path, text, ending='\n'):
    """Writes text (LF line ends) with the given line end, so a CRLF file stays CRLF."""
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='') as handle:
        handle.write(text.replace('\n', ending))


def write_json(path, value):
    """Writes value as JSON when it differs from the file; True when it changed."""
    text = json.dumps(value, indent=2) + '\n'
    if os.path.isfile(path) and read_text(path)[0] == text:
        return False
    write(path, text)
    return True


def block(clone, record, names):
    """The ReAgentLive lines for an agent in the project; names: its MCP servers."""
    # -NonInteractive: an agent's shell may have a console, and update.ps1 asks only a person.
    script = f'powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File {clone}/tools'
    own = f'{script}/build_project.ps1 -Project .'
    if record.get('build'):
        build = f"`{record['build']}`"
        build += f'; build and check: `{own} -Check`' if record.get('check') else ' (it ticks only in the game: check `status` there)'
    else:
        build = f'`{own}`, with `-Check` to check' if record.get('check') else f'`{own}`'
    tools = ' or '.join(f'`{name}`' for name in names)
    items = [
        f'`{clone}/` is the ReAgentLive submodule: never edit it here. Before using the {tools} MCP tools, '
        f'read `{clone}/.claude/skills/reagentlive/SKILL.md` and follow it; docs: `{clone}/docs/README.md`.',
        f"DLL `{record['dll']}`, console `{record['console']}`. Build: {build}.",
        f'Update: `{script}/update.ps1 -Project .` previews, `-Apply` updates (`{clone}/docs/agents/update.md`).',
        f"No {tools} MCP tools, or no `{record['console']}`: this machine is not set up yet; `{clone}/tools/check.ps1` "
        f'says what is missing; offer `{clone}/docs/agents/setup.md` (a clone of a project wired earlier).',
    ]
    return f'{BEGIN}\n## ReAgentLive\n\n' + '\n'.join(fill(item) for item in items) + f'\n{END}\n'


def fill(item, width=76):
    """A list item wrapped at width; a code span is never split, so a command copies whole."""
    lines, line = [], '-'
    for word in re.findall(r'`[^`]*`\S*|\S+', item):
        if len(line) + 1 + len(word) > width and line.strip() not in ('-', ''):
            lines.append(line)
            line = ' '
        line += ' ' + word
    return '\n'.join(lines + [line])


BLOCK = re.compile(r'\n?' + re.escape(BEGIN) + r'.*?' + re.escape(END) + r'\n?', re.S)


def put_block(path, text):
    """Replaces the marked block in path, or appends it (text None: removes it); True when it changed."""
    old, ending = read_text(path) if os.path.isfile(path) else ('', '\n')
    if text is None:
        new = BLOCK.sub('\n', old, count=1).rstrip('\n') + '\n' if BLOCK.search(old) else old
    elif BLOCK.search(old):
        new = BLOCK.sub(lambda m: ('\n' if m.group(0).startswith('\n') else '') + text, old, count=1)
    else:
        separator = '' if not old else '\n' if old.endswith('\n') else '\n\n'
        new = old + separator + text
    if new != old:
        write(path, new, ending)
        return True
    return False


def imports_agents(path):
    """CLAUDE.md imports AGENTS.md: a line that is exactly @AGENTS.md."""
    return any(line.strip() == '@AGENTS.md' for line in read_text(path)[0].split('\n'))


def is_link(path):
    """A symlink or (on Windows) a junction."""
    try:
        os.readlink(path)
        return True
    except (OSError, ValueError, NotImplementedError):
        return False


def tracked(project, relative):
    """The project's git tracks a file under relative."""
    try:
        result = subprocess.run(['git', '-C', project, 'ls-files', '--', relative], capture_output=True, text=True)
    except OSError:
        return False
    return result.returncode == 0 and bool(result.stdout.strip())


def is_pointer(folder):
    """The folder holds a pointer skill this script wrote."""
    path = os.path.join(folder, 'SKILL.md')
    return os.path.isfile(path) and POINTER in read_text(path)[0]


def own_skills(project):
    """The skill folders that hold the project's own skill (tracked, not a pointer), kept as they are;
    refuses before any write on an untracked one (a stale copy)."""
    kept = []
    for relative in SKILL_DIRS:
        folder = os.path.join(project, *relative.split('/'))
        if os.path.exists(folder) and not is_link(folder) and not is_pointer(folder):
            if not tracked(project, relative):
                raise Refused(f'{relative} is an untracked skill folder: move it away first')
            kept.append(relative)
    return kept


def pointer(clone):
    """The pointer skill: the submodule skill's name and description, and where to read it."""
    head = read_text(SKILL)[0].split('\n---\n', 1)[0]  # the front matter, opening --- included
    return f'''{head}
---

{POINTER}
# ReAgentLive (pointer)

The skill is ReAgentLive's own, in the `{clone}/` submodule, so it updates
with it. Read `{clone}/.claude/skills/reagentlive/SKILL.md` now and follow it:
its `references/...` paths resolve inside that folder, its repo paths
(`docs/...`, `src/...`) inside `{clone}/`.
'''


def put_skills(project, clone, kept):
    """Writes the pointer skill where the project has no skill of its own; the folders it changed."""
    text, changed = pointer(clone), []
    for relative in SKILL_DIRS:
        if relative in kept:
            continue
        folder = os.path.join(project, *relative.split('/'))
        if is_link(folder):  # a link into the submodule: writing through it would edit the submodule
            os.rmdir(folder) if os.name == 'nt' else os.unlink(folder)
        path = os.path.join(folder, 'SKILL.md')
        if not os.path.isfile(path) or read_text(path)[0] != text:
            write(path, text)
            changed.append(relative)
    return changed


def is_server(entry):
    """An MCP server entry that runs ReAgentLive's MCP server."""
    args = entry.get('args') if isinstance(entry, dict) else None
    return isinstance(args, list) and any(isinstance(a, str) and a.endswith(SERVER_SCRIPT) for a in args)


def server_name(servers, path):
    """The name of the project's ReAgentLive server in an MCP file: its own, whatever it is called, else
    reagentlive. Refuses on more than one."""
    names = [name for name, entry in servers.items() if is_server(entry)]
    if len(names) > 1:
        raise Refused(f'{path} has several ReAgentLive servers ({", ".join(names)}): keep one')
    return names[0] if names else 'reagentlive'


def set_packs(args, flag, modify):
    """args with flag's pack list holding modify or not, in place (other packs kept); the flag goes when its
    list empties, and is appended when it is new."""
    args = list(args)
    at = args.index(flag) if flag in args and args.index(flag) + 1 < len(args) else None
    packs = [p for p in str(args[at + 1]).split(',') if p and p != 'modify'] if at is not None else []
    if modify:
        packs.append('modify')
    if at is None:
        return args + [flag, ','.join(packs)] if packs else args
    if packs:
        args[at + 1] = ','.join(packs)
    else:
        del args[at:at + 2]
    return args


def server_entry(entry, script, console, approval):
    """The server entry with this script and console, and the pack ceiling the approval mode gives: modify may be
    turned on (--allow) unless read-only, and is never on from the start (--packs). Its other keys and arguments
    (--name, other packs) stay."""
    if is_server(entry):
        entry = dict(entry)
        args = [script if isinstance(a, str) and a.endswith(SERVER_SCRIPT) else a for a in entry['args']]
        if '--console' in args and args.index('--console') + 1 < len(args):
            args[args.index('--console') + 1] = console
        else:
            args[1:1] = ['--console', console]
    else:
        entry, args = {'command': 'python'}, [script, '--console', console]
    args = set_packs(args, '--packs', False)
    entry['args'] = set_packs(args, '--allow', approval != 'read-only')
    return entry


def default_project():
    """The git project this clone is a submodule of; without one, the caller names the project."""
    try:
        result = subprocess.run(['git', '-C', CLONE, 'rev-parse', '--show-superproject-working-tree'],
                                capture_output=True, text=True)
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except OSError:
        pass
    raise Refused(f'this clone ({CLONE}) is no git submodule of a project: add it to one, or pass --project')


def run(args):
    project = os.path.abspath(args.project or default_project())
    if not os.path.isdir(project):
        raise Refused(f'--project {project} is not a folder')
    try:
        clone = os.path.relpath(CLONE, project)
    except ValueError:  # another drive
        clone = '..'
    if clone.startswith('..') or os.path.isabs(clone):
        raise Refused(f'this clone ({CLONE}) is not inside {project}: add it there, as a git submodule')
    if clone == '.':
        raise Refused(f'--project {project} is this clone itself: name the project that carries it')
    clone = slash(clone)

    # Read everything first, so a refusal changes nothing.
    record_path = os.path.join(project, RECORD)
    record = read_json(record_path, 'the project record')
    mcp = args.mcp if args.mcp is not None else ','.join(record.get('mcp') or ['claude-code'])
    clients = [c.strip() for c in mcp.split(',') if c.strip()]
    unknown = [c for c in clients if c not in MCP_FILES]
    if unknown:
        raise Refused(f'--mcp: unknown {", ".join(unknown)} (one or more of {", ".join(MCP_FILES)})')
    record['library'] = clone
    record['mcp'] = clients
    for key in ('console', 'dll', 'build'):
        value = getattr(args, key)
        if value is not None:
            record[key] = slash(value) if key in ('console', 'dll') else value
    for key in ('console', 'dll'):
        if not record.get(key):
            raise Refused(f'--{key}: the project has none yet; pass its path, relative to the project')
        if os.path.isabs(record[key]) or record[key].startswith('..'):
            raise Refused(f'--{key} {record[key]}: a path inside the project, relative to it')
    if args.check is not None:
        if args.check not in ('yes', 'no'):
            raise Refused('--check is yes or no')
        record['check'] = args.check == 'yes'
    record.setdefault('check', True)
    if args.approval is not None:
        record['approval'] = args.approval
    record.setdefault('approval', 'auto')
    if record['approval'] not in ('read-only', 'auto', 'bypass'):
        raise Refused('--approval is read-only, auto or bypass')
    configs, names = {}, {}
    for client in clients:
        path, key, _ = MCP_FILES[client]
        configs[client] = read_json(os.path.join(project, *path.split('/')), 'the MCP file')
        servers = configs[client].get(key) or {}
        if not isinstance(servers, dict):
            raise Refused(f'{path}: `{key}` is not a JSON object: fix it')
        names[client] = server_name(servers, path)
    kept = own_skills(project)

    written = []
    if write_json(record_path, record):
        written.append(RECORD)
    for client in clients:
        path, key, root = MCP_FILES[client]
        config = configs[client]
        servers = config.setdefault(key, {})
        servers[names[client]] = server_entry(servers.get(names[client]), f'{root}{clone}/{SERVER_SCRIPT}',
                                              root + record['console'], record['approval'])
        if write_json(os.path.join(project, *path.split('/')), config):
            written.append(path)
    text = block(clone, record, sorted(set(names.values())))
    agents, claude = os.path.join(project, 'AGENTS.md'), os.path.join(project, 'CLAUDE.md')
    if put_block(agents, text):
        written.append('AGENTS.md')
    if os.path.isfile(claude) and put_block(claude, None if imports_agents(claude) else text):
        written.append('CLAUDE.md')
    written += [f'{folder}/SKILL.md' for folder in put_skills(project, clone, kept)]
    return {'project': project, 'record': record, 'written': written, 'servers': names, 'kept': kept}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--console', help="the host's console, relative to the project")
    parser.add_argument('--dll', help='the host DLL, relative to the project')
    parser.add_argument('--build', help='the build command, run in the project folder (default: CMake)')
    parser.add_argument('--approval', help='read-only, auto or bypass: what the agent may change in the target unasked (default auto)')
    parser.add_argument('--check', help='yes or no: whether a build loads the DLL into a test process (no: it ticks only in the game)')
    parser.add_argument('--mcp', help='comma-separated: ' + ', '.join(MCP_FILES) + " (default: the record's, else claude-code)")
    parser.add_argument('--project', help='the project folder (default: the git project this clone is a submodule of)')
    try:
        result = run(parser.parse_args(argv))
    except Refused as refusal:
        print(json.dumps({'error': str(refusal)}))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
