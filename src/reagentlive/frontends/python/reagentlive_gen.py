"""Typed Python functions for one ReAgentLive console, generated from its verb table.

    python reagentlive_gen.py --console <console.exe> -o my_console.py
    python reagentlive_gen.py --table help.json -o my_console.py   # a saved `help --json` result

The module holds one method per verb (a host's verbs included), named by
origin: core verbs on Console itself (`c.read`), a pack's or the host's
under their origin (`c.ue.find`, `c.host.world`). Arguments are the verb's
positionals then keyword-only flags, typed from the table; each result is
annotated with a TypedDict of the console's own keys, so a type checker
reads a script before it runs. At run time a call goes as JSON through
reagentlive_session.Session.call and returns its strict Record, key for
key what the console printed (an addr stays its hex text).

The module carries the table's hash: Console(session) refuses a console
whose table differs (StaleTableError), so a stale module never runs.

This generator reads `help --json` and nothing else: it names no pack and
no verb. Standard library only.
"""
import argparse
import json
import keyword
import os
import subprocess
import sys

SCALARS = {'bool': 'bool', 'u32': 'int', 'u64': 'int', 'i32': 'int', 'i64': 'int', 'f64': 'float', 'str': 'str',
           'addr': 'str', 'hex': 'str', 'bytes': 'str', 'ref': 'str', 'value': 'Any'}
# Argument types (syntax/verb.h) as Python annotations.
ARGUMENTS = {'addr': 'int | str', 'ref': 'str', 'int': 'int', 'dur': 'float | str', 'bytes': 'bytes | str',
             'pattern': 'str', 'text': 'str', 'str': 'str', 'path': 'str', 'layout': 'str', 'range': 'str',
             'span': 'tuple[int | str, int]', 'line': 'str', 'calls': 'list[Call]', 'bool': 'bool'}
# One value however many tokens it spans: a positional that takes several takes one of these, not a list.
JOINED = ('bytes', 'pattern', 'text')


def identifier(name):
    """A Python name for a key or verb: '-' and '.' become '_', a keyword gets a trailing '_'."""
    text = name.replace('-', '_').replace('.', '_')
    return text + '_' if keyword.iskeyword(text) or text in ('self', 'Any') else text


def class_name(name):
    return ''.join(part[:1].upper() + part[1:] for part in identifier(name).split('_') if part)


def table_of(console):
    """`help --json` from a console, with no target needed."""
    run = subprocess.run([os.path.abspath(console), '-c', 'help --json'], capture_output=True, text=True, check=False)
    for line in run.stdout.splitlines():
        if line.lstrip().startswith('{'):
            envelope = json.loads(line)
            if not envelope.get('ok'):
                raise SystemExit(f'{console}: {envelope.get("error", {}).get("message", "help --json failed")}')
            return envelope['result']
    raise SystemExit(f'{console}: no help --json result')


class Types:
    """TypedDict classes for result types, emitted before their first use."""

    def __init__(self, named):
        self.named, self.lines, self.done = named, [], set()

    def annotation(self, kind, owner):
        if isinstance(kind, str):
            return SCALARS.get(kind, 'Any')
        if 'named' in kind:
            name = class_name(kind['named'])
            if name not in self.done:
                self.done.add(name)
                definition = self.named[kind['named']]['type']
                if isinstance(definition, dict) and 'struct' in definition:
                    self.struct(definition, name)
                else:
                    self.lines.append(f'{name} = {self.annotation(definition, name)}')
            return name
        if 'enum' in kind:
            words = kind['enum']
            return 'str' if '...' in words else 'Literal[' + ', '.join(repr(word) for word in words) + ']'
        if 'list' in kind:
            element = self.annotation(kind['list'], owner)
            return f'list[{element} | None]' if kind.get('nullable') else f'list[{element}]'
        if 'map' in kind:
            return f'dict[str, {self.annotation(kind["map"], owner)}]'
        if 'struct' in kind:
            return self.struct(kind, owner)
        return 'Any'

    def struct(self, kind, name):
        fields = []
        for key, spec in kind['struct'].items():
            inner = self.annotation(spec['type'], name + class_name(key))
            fields.append(f'{key!r}: ' + (f'NotRequired[{inner}]' if spec.get('optional') else inner))
        self.lines.append(f'{name} = TypedDict({name!r}, {{' + ', '.join(fields) + '})')
        return name


def parameters(row):
    """(signature text, [(python name, key)]) of a verb's arguments."""
    params, names, keyword_only = ['self'], [], []
    for positional in row['positionals']:
        name, kind, arity = identifier(positional['key']), positional['type'], positional['arity']
        hint = ARGUMENTS.get(kind, 'str') if '|' not in kind else \
            'Literal[' + ', '.join(repr(word) for word in kind.split('|')) + ']'
        if arity in ('some', 'any') and kind not in JOINED:
            hint = f'list[{hint}]'
        params.append(f'{name}: {hint}' + (' | None = None' if arity in ('optional', 'any') else ''))
        names.append((name, positional['key']))
    for flag in row['flags']:
        name, kind = identifier(flag['key']), flag['type']
        hint = ARGUMENTS.get(kind, 'str') if '|' not in kind else \
            'Literal[' + ', '.join(repr(word) for word in kind.split('|')) + ']'
        if flag['takes'] == 'nothing':
            keyword_only.append(f'{name}: bool = False')
        elif flag['takes'] == 'maybe':
            keyword_only.append(f'{name}: bool | {hint} = False')
        elif flag['takes'] == 'some' and kind not in JOINED:
            keyword_only.append(f'{name}: list[{hint}] | {hint}' + ('' if flag.get('required') else ' | None = None'))
        else:
            keyword_only.append(f'{name}: {hint}' + ('' if flag.get('required') else ' | None = None'))
        names.append((name, flag['key']))
    # Required keyword-only arguments first, for readability.
    keyword_only.sort(key=lambda text: '=' in text)
    if keyword_only:
        params += ['*', *keyword_only]
    return ', '.join(params), names


def docstring(row):
    lines = [row['summary'] + '.', '', row['usage']]
    for item in row['positionals']:
        lines.append(f'{identifier(item["key"])}: {item.get("help", item["name"])}'
                     + (f' (default {item["default"]})' if 'default' in item else ''))
    for item in row['flags']:
        lines.append(f'{identifier(item["key"])} ({item["flag"]}): {item.get("help", "")}'.rstrip(': ')
                     + (f' (default {item["default"]})' if 'default' in item else ''))
    if row.get('notes'):
        lines += ['', row['notes']]
    marks = [mark for mark in ('writes', 'blocks') if row.get(mark)]
    if marks:
        lines += ['', 'Marked: ' + ', '.join(marks) + '.']
    return '\n'.join(lines)


def generate(table, source):
    types = Types(table.get('types', {}))
    groups = {}
    for row in table['rows']:
        if row['verb'] == 'quit':                 # The session owns the console's lifetime.
            continue
        groups.setdefault(row['origin'], []).append(row)
    methods = {}
    tools = []
    for origin, rows in groups.items():
        body = []
        for row in rows:
            name = identifier(row['verb'])
            result = types.annotation(row['type'], class_name(row['verb']) + 'Result')
            signature, names = parameters(row)
            doc = docstring(row).replace('\\', '\\\\').replace('"""', '\\"\\"\\"')
            args = ', '.join(f'{key!r}: {python}' for python, key in names)
            body += [f'    def {name}({signature}) -> {result}:',
                     '        """' + doc.replace('\n', '\n        ') + '\n        """',
                     f'        return self._session.call({row["verb"]!r}, {{{args}}})  # type: ignore[return-value]', '']
            tools.append({'origin': origin, 'method': name, 'verb': row['verb'], 'writes': bool(row.get('writes')),
                          'blocks': bool(row.get('blocks')), 'summary': row['summary'],
                          'cls': 'Console' if origin == 'core' else f'{class_name(origin)}Verbs',
                          'attribute': None if origin == 'core' else identifier(origin)})
        methods[origin] = body
    titles = {item['origin']: item['title'] for item in table.get('origins', [])}
    out = [f'"""Typed functions for one ReAgentLive console, generated by reagentlive_gen.py from {source}.',
           '',
           'Do not edit: generate it again when the console\'s table_hash changes (Console refuses it then).',
           '"""',
           'from typing import Any, Literal, NotRequired, TypedDict',
           '',
           'from reagentlive_session import Session, StaleTableError',
           '',
           f'TABLE_HASH = {table.get("table_hash")!r}',
           '',
           '# One call in a batch: a verb by its console name and its named arguments.',
           "Call = TypedDict('Call', {'verb': str, 'args': NotRequired[dict[str, Any]]})",
           '',
           *types.lines,
           '']
    for origin, body in methods.items():
        if origin == 'core':
            continue
        out += [f'class {class_name(origin)}Verbs:', f'    """{titles.get(origin, origin)}: the {origin!r} verbs."""', '',
                '    def __init__(self, session: Session):', '        self._session = session', '', *body]
    out += ['class Console:',
            '    """The console\'s verbs: the core\'s here, each other origin\'s under its name."""', '',
            '    def __init__(self, session: Session):',
            '        if session.table_hash != TABLE_HASH:',
            "            raise StaleTableError(f'generated from table {TABLE_HASH}, the console has {session.table_hash}: "
            "run reagentlive_gen.py again')",
            '        self._session = session']
    for origin in methods:
        if origin != 'core':
            out.append(f'        self.{identifier(origin)} = {class_name(origin)}Verbs(session)')
    out += ['', *methods.get('core', [])]
    out += ['# Every method: its origin, name, console verb and marks (for a tool adapter).',
            'VERBS = [', *[f'    {tool!r},' for tool in tools], ']', '']
    return '\n'.join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--console', help='the console executable (help --json needs no target)')
    source.add_argument('--table', help='a file holding the help --json result (or its envelope)')
    parser.add_argument('-o', '--output', required=True, help='the module to write')
    args = parser.parse_args(argv)
    if args.console:
        table, name = table_of(args.console), os.path.basename(args.console)
    else:
        with open(args.table, encoding='utf-8') as file:
            table = json.load(file)
        table, name = table.get('result', table), os.path.basename(args.table)
    text = generate(table, name)
    with open(args.output, 'w', encoding='utf-8', newline='\n') as file:
        file.write(text)
    print(f'{args.output}: {sum(1 for _ in table["rows"])} verbs, table {table.get("table_hash")}')


if __name__ == '__main__':
    sys.exit(main())
