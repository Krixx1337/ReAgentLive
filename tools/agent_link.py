"""Wire an AI agent to a host project: the MCP server for its console, the skill, agent instructions.

    python -I <clone>/tools/agent_link.py --console <exe> --dll <dll> [--build <command>] [--check yes|no]
                                          [--improvement user|dev|off] [--mcp claude-code,cursor,copilot]
                                          [--project <dir>]

The project is any folder that builds a host DLL and its console and carries this clone at a fixed path
inside it (a git submodule; default project: the git project it is a submodule of, else pass --project).
Paths are relative to the project. Writes, and on every later run updates in place:

- reagentlive.json, the project record: `library` (this clone's path in the project), `mcp` (the clients),
  `console`, `dll`, `build` (a command run in the project folder;
  without it tools/build_project.ps1 runs CMake on the project), `check` (false for a host that ticks
  only inside the game, so the update builds without loading the DLL into a test process),
  `improvement`. Arguments left out keep the record's values.
- the `reagentlive` server in each client's project MCP file (other servers stay);
- a ReAgentLive block in AGENTS.md (CLAUDE.md gets `@AGENTS.md` when it has neither; an existing CLAUDE.md
  that does not import AGENTS.md gets the block too), between `<!-- reagentlive -->` markers;
- skill links in .claude/skills and .agents/skills (a junction on Windows, a symlink elsewhere), and
  their lines in .gitignore;
- the project in the machine record's `projects` (%LOCALAPPDATA%\\reagentlive\\machine.json).

Prints one JSON object: `project`, `record`, `written`, `links`, `registered` (the machine record, or null
without LOCALAPPDATA); or `error`. Exit 0 or 1.
"""
import argparse
import json
import os
import re
import subprocess
import sys

CLONE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKILL = os.path.join(CLONE, '.claude', 'skills', 'reagentlive')
LINKS = ('.claude/skills/reagentlive', '.agents/skills/reagentlive')
RECORD = 'reagentlive.json'
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


def block(clone, record):
    """The ReAgentLive lines for an agent in the project."""
    script = f'powershell -NoProfile -ExecutionPolicy Bypass -File {clone}/tools'
    build = record.get('build') or f'{script}/build_project.ps1 -Project .'
    return f'''{BEGIN}
## ReAgentLive

- Before investigating a running process with the reagentlive tools, read
  `{clone}/.claude/skills/reagentlive/SKILL.md` and follow it. ReAgentLive
  is the `{clone}/` submodule: never edit it here.
- Docs: `{clone}/docs/README.md`; hosting: `{clone}/docs/hosting.md`.
- DLL `{record['dll']}`, console `{record['console']}`; build: `{build}`;
  build and check: `{script}/build_project.ps1 -Project . -Check`.
- Update: `{script}/update.ps1 -Project .` (a preview), then with `-Apply`
  (`{clone}/docs/agents/update.md`).
{END}
'''


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


def check_links(project):
    """Refuses before any write when a link path holds a real folder."""
    for relative in LINKS:
        link = os.path.join(project, *relative.split('/'))
        if os.path.exists(link) and not is_link(link):
            raise Refused(f'{relative} is a folder, not a link: move it away first')


def link_skill(project):
    made = []
    for relative in LINKS:
        link = os.path.join(project, *relative.split('/'))
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


def machine_record():
    base = os.environ.get('LOCALAPPDATA')
    if not base:
        return None, None
    path = os.path.join(base, 'reagentlive', 'machine.json')
    return path, read_json(path, 'the machine record')


def register(project, path, record):
    if path is None:
        return None
    projects = record.get('projects') or []
    if not isinstance(projects, list):
        projects = [projects]
    known = {os.path.normcase(os.path.normpath(p)) for p in projects if isinstance(p, str)}
    if os.path.normcase(os.path.normpath(project)) not in known:
        projects.append(project)
    record['projects'] = projects
    write(path, json.dumps(record, indent=2) + '\n')
    return path


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
    for key in ('console', 'dll', 'build', 'improvement'):
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
    record.setdefault('improvement', 'user')
    if record['improvement'] not in ('user', 'dev', 'off'):
        raise Refused('--improvement is user, dev or off')
    configs = {}
    for client in clients:
        path, key, _ = MCP_FILES[client]
        configs[client] = read_json(os.path.join(project, *path.split('/')), 'the MCP file')
    machine = machine_record()
    check_links(project)

    written = []
    write(record_path, json.dumps(record, indent=2) + '\n')
    written.append(RECORD)
    for client in clients:
        path, key, root = MCP_FILES[client]
        config = configs[client]
        servers = config.setdefault(key, {})
        servers['reagentlive'] = {'command': 'python', 'args': [
            f'{root}{clone}/src/reagentlive/frontends/mcp/reagentlive_mcp.py', '--console', root + record['console']]}
        write(os.path.join(project, *path.split('/')), json.dumps(config, indent=2) + '\n')
        written.append(path)
    text = block(clone, record)
    agents, claude = os.path.join(project, 'AGENTS.md'), os.path.join(project, 'CLAUDE.md')
    if put_block(agents, text):
        written.append('AGENTS.md')
    if not os.path.isfile(claude):
        write(claude, '@AGENTS.md\n')
        written.append('CLAUDE.md')
    elif put_block(claude, None if imports_agents(claude) else text):
        written.append('CLAUDE.md')
    ignore = os.path.join(project, '.gitignore')
    current, ending = read_text(ignore) if os.path.isfile(ignore) else ('', '\n')
    missing = [link for link in LINKS if link not in current.split('\n')]
    if missing:
        current = current.rstrip('\n')
        write(ignore, (current + '\n' if current else '') + ''.join(f'{link}\n' for link in missing), ending)
        written.append('.gitignore')
    return {'project': project, 'record': record, 'written': written, 'links': link_skill(project),
            'registered': register(project, *machine)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--console', help="the host's console, relative to the project")
    parser.add_argument('--dll', help='the host DLL, relative to the project')
    parser.add_argument('--build', help='the build command, run in the project folder (default: CMake)')
    parser.add_argument('--check', help='yes or no: whether a build loads the DLL into a test process (no: it ticks only in the game)')
    parser.add_argument('--improvement', help='user, dev or off: what the skill may change after a task')
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
