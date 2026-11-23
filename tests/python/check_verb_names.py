"""Every verb and arg a doc names must exist in the console's verb table (J6: one source for verb text).

A doc names a verb or an arg when it writes a call: a JSON call ({"verb": ..., "args": {...}}, a plan step, a
batch's calls), a Python session call (c.read(...), c.ue.find(..., view=...), session.call('verb', {...})), an MCP
tool name (ue_find) or a key by its path (view.limit). Host verbs (c.host.*, host_*) are checked only against a
table that has host rows. The text grammar is gone, so a code span that starts with a verb and goes on in words
or flags (`find PC_`, `get --types`), a one-shot `-c` with no JSON, and a flag that spells a verb's arg
(`--limit`) are stale forms; a span that runs a program (python, cmake, a console's own options) may use flags.

Usage: check_verb_names.py --table <reagentlive-table or a console> [--root DIR] [FILE_OR_DIR ...]
       (no paths: the repo's docs/, README.md and the skill). Exit 1 with one line per stale name.
Run by ctest as reagentlive.verb-names; a host runs it on its own docs with its own console's table.
"""
import argparse
import json
import os
import re
import subprocess
import sys

CODE_SPAN = re.compile(r'(?<!`)`([^`\n]+)`(?!`)')
FENCE = re.compile(r'^\s*(```|~~~)')
PY_CALL = re.compile(r'\b(?:c|console|self\.c|x\.c)\.(?:(ue|host)\.)?([a-z][a-z0-9_]*)\(')
SESSION_CALL = re.compile(r"""\.call\(\s*['"]([a-z][a-z0-9-]*)['"]""")
TOOL_NAME = re.compile(r'^(ue|host)_([a-z][a-z0-9_]*)$')
KEY_PATH = re.compile(r'^(view|scope|save|undo)\.([a-z_]+)$')
FLAG = re.compile(r'(?<![\w-])--([a-z][a-z0-9-]*)')
ONE_SHOT = re.compile(r"""(?<![\w-])-c\s+["']?(?![\s"']*[{\[@])""")
# A span that runs one of these takes its own flags.
PROGRAMS = ('python', 'py', 'cmake', 'ctest', 'git', 'pip', 'pwsh', 'powershell', 'reagentlive-table', 'ninja', 'cl')
# Session helpers that are not verbs, and console or tool options a span may name.
SESSION_METHODS = {'call', 'batch', 'batch_results', 'watch', 'close', 'help', 'table', 'records', 'raw'}
SKIP_DIRS = {'__pycache__', '.git', 'build', 'out', 'archive'}


def table_of(path):
    """help --json from reagentlive-table (no args) or a console (-c help json)."""
    path = os.path.abspath(path)
    name = os.path.basename(path).lower()
    if name.startswith('reagentlive-table'):
        run = subprocess.run([path], capture_output=True, text=True, check=True)
        return json.loads(run.stdout)
    call = json.dumps({'verb': 'help', 'args': {'json': True}})
    run = subprocess.run([path, '-c', call], capture_output=True, text=True, check=False)
    for line in run.stdout.splitlines():
        if line.lstrip().startswith('{'):
            envelope = json.loads(line)
            if envelope.get('ok'):
                return envelope['result']
    raise SystemExit(f'{path}: no help --json result')


class Table:
    def __init__(self, help_json):
        self.rows = {row['verb']: row for row in help_json['rows']}
        self.keys = set()
        for row in self.rows.values():
            for arg in row.get('args', []):
                self.keys.add(arg['key'])
                for field in arg.get('fields', []):
                    self.keys.add(field['key'])

    def args_problems(self, verb, args, where):
        row = self.rows[verb]
        known = {arg['key']: arg for arg in row.get('args', [])}
        out = []
        if not isinstance(args, dict):
            return out
        for key, value in args.items():
            if key in ('meta', 'timing', 'big', 'budget'):
                continue
            if key not in known:
                out.append(f'{where}: {verb} has no arg {key!r}')
                continue
            fields = {field['key'] for field in known[key].get('fields', [])}
            if fields and isinstance(value, dict):
                out += [f'{where}: {verb}.{key} has no key {inner!r}' for inner in value if inner not in fields]
            if verb == 'batch' and key == 'calls' and isinstance(value, list):
                for call in value:
                    out += self.call_problems(call, where)
        return out

    def call_problems(self, call, where):
        if not isinstance(call, dict) or not isinstance(call.get('verb'), str):
            return []
        verb = call['verb']
        if verb not in self.rows:
            return [f'{where}: no verb {verb!r}']
        return self.args_problems(verb, call.get('args', {}), where)

    def has_origin(self, origin):
        return any(row['origin'] == origin for row in self.rows.values())

    def python_verb(self, origin, method):
        verb = method.replace('_', '-')
        row = self.rows.get(verb)
        if row and row['origin'] == (origin or 'core'):
            return verb
        return None


def json_values(text):
    """Every JSON object or list in the text that parses and holds "verb"."""
    decoder = json.JSONDecoder()
    found = []
    for match in re.finditer(r'[\[{]', text):
        start = match.start()
        if '"verb"' not in text[start:start + 2000]:
            continue
        try:
            value, _ = decoder.raw_decode(text[start:])
        except ValueError:
            continue
        found.append(value)
    return found


def calls_in(value):
    """Calls and plan steps in a parsed JSON value, nested ones too."""
    if isinstance(value, list):
        for item in value:
            yield from calls_in(item)
    elif isinstance(value, dict):
        if isinstance(value.get('verb'), str):
            yield value


def py_kwargs(text, start):
    """The keyword names of the call whose '(' ends at start-1, as far as its closing ')'."""
    depth, i = 1, start
    while i < len(text) and depth:
        depth += {'(': 1, ')': -1}.get(text[i], 0)
        i += 1
    inner = text[start:i - 1]
    # Only top-level keywords: drop nested brackets first.
    flat, depth = [], 0
    for ch in inner:
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        elif depth == 0:
            flat.append(ch)
    return re.findall(r'(?:^|,)\s*([a-z_][a-z0-9_]*)\s*=(?!=)', ''.join(flat))


def check_text(table, text, name):
    problems = []
    seen = set()
    for value in json_values(text):
        for call in calls_in(value):
            key = json.dumps(call, sort_keys=True)
            if key not in seen:
                seen.add(key)
                problems += table.call_problems(call, name)
    for match in PY_CALL.finditer(text):
        origin, method = match.group(1), match.group(2)
        if method in SESSION_METHODS or (origin == 'host' and not table.has_origin('host')):
            continue
        verb = table.python_verb(origin, method)
        if not verb:
            problems.append(f'{name}: no verb for {match.group(0)}')
            continue
        known = {arg['key'] for arg in table.rows[verb].get('args', [])}
        for kw in py_kwargs(text, match.end()):
            if kw.rstrip('_') not in known and kw not in ('budget', 'meta', 'timing', 'big'):
                problems.append(f'{name}: {verb} has no arg {kw!r} ({match.group(0)}...)')
    for match in SESSION_CALL.finditer(text):
        if match.group(1) not in table.rows:
            problems.append(f'{name}: no verb {match.group(1)!r} in {match.group(0)}')
    for span in code_spans(text):
        problems += [f'{name}: {why}: `{span}`' for why in span_problems(table, span)]
    return list(dict.fromkeys(problems))    # A call inside a batch is found twice.


def code_spans(text):
    fenced = False
    for line in text.split('\n'):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if not fenced:
            yield from CODE_SPAN.findall(line)


def span_problems(table, span):
    span = span.strip()
    out = []
    tool = TOOL_NAME.match(span)
    if tool and table.has_origin(tool.group(1)) and not table.python_verb(tool.group(1), tool.group(2)):
        out.append('no such tool')
    path = KEY_PATH.match(span)
    if path and path.group(2) not in table.keys:
        out.append('no such key')
    words = span.split()
    if not words:
        return out
    if len(words) > 1 and words[0] in table.rows and not words[1].startswith(('{', '(', '=', ':', '|', ',', ')')):
        out.append('text grammar (a call is JSON)')
    first = words[0].lower()
    if ONE_SHOT.search(span) and first not in PROGRAMS:
        out.append('one-shot -c without a JSON call')
    if not (first.endswith(('.py', '.exe', '.ps1')) or first.startswith('<') or first in PROGRAMS):
        for flag in FLAG.findall(span):
            if flag in table.keys or flag.replace('-', '_') in table.keys:
                out.append(f'flag --{flag} (args are JSON keys)')
    return out


def default_paths(root):
    paths = [os.path.join(root, 'README.md'), os.path.join(root, 'docs'), os.path.join(root, 'examples'),
             os.path.join(root, '.claude', 'skills', 'reagentlive')]
    return [path for path in paths if os.path.exists(path)]


def files(paths, generated):
    for path in paths:
        if os.path.isfile(path):
            yield path
            continue
        for base, dirs, names in os.walk(path):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            for name in sorted(names):
                full = os.path.join(base, name)
                if name.endswith('.md') and os.path.abspath(full) not in generated and not name.startswith('local-lessons'):
                    yield full


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--table', required=True, help='reagentlive-table, or a console that answers help --json')
    parser.add_argument('--root', default=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')))
    parser.add_argument('paths', nargs='*')
    args = parser.parse_args(argv)
    table = Table(table_of(args.table))
    generated = {os.path.abspath(os.path.join(args.root, 'docs', 'commands', page)) for page in ('results.md', 'reference.md')}
    problems = []
    for path in files(args.paths or default_paths(args.root), generated):
        with open(path, encoding='utf-8') as file:
            problems += check_text(table, file.read(), os.path.relpath(path, args.root))
    for line in problems:
        print(line)
    print(f'{len(problems)} stale verb or arg names' if problems else 'every verb and arg name is in the table')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
