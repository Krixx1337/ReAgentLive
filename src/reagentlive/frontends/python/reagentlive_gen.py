"""Typed Python functions for one ReAgentLive console, generated from its verb table.

    python reagentlive_gen.py --console <console.exe> -o my_console.py
    python reagentlive_gen.py --table help.json -o my_console.py   # a saved `help` with `json` result

The module holds one method per verb (a host's verbs included), named by
origin: core verbs on Console itself (`c.read`), a pack's or the host's
under their origin (`c.ue.find`, `c.host.world`). Arguments follow the row's
`args`: the required ones first, by position or name, then the optional
ones by name only, typed from the table; each result is
annotated with a TypedDict of the console's own keys, so a type checker
reads a script before it runs. At run time a call goes as JSON through
reagentlive_session.Session.call and returns its strict Record, key for
key what the console printed (an addr stays its hex text).

The module carries the table's hash: Console(session) refuses a console
whose table differs (StaleTableError), so a stale module never runs.

This generator reads `help` with `json` and nothing else: it names no pack and
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
# Argument types (call/verb.h) as Python annotations. An int may be "0x..." text; bytes may be Python bytes
# (the session sends them as hex pairs).
ARGUMENTS = {'bool': 'bool', 'int': 'int | str', 'num': 'float', 'dur': 'float', 'str': 'str', 'text': 'str',
             'file': 'str', 'bytes': 'bytes | str', 'pattern': 'str', 'layout': 'str', 'addr': 'str | dict[str, Any]',
             'chain': 'str | dict[str, Any]', 'ref': 'str', 'path': 'list[str]', 'filter': 'dict[str, Any]',
             'params': 'dict[str, Any]', 'range': 'dict[str, Any]', 'point': 'list[float] | str | dict[str, Any]',
             'scalar': 'str | int | float', 'calls': 'list[Call]', 'obj': 'dict[str, Any]'}


def known(vocabulary, kind, what):
    """A type's annotation; a type the console added and this file lacks stops the generator."""
    if kind not in vocabulary:
        raise ValueError(f'unknown {what} type {kind!r}: add it to reagentlive_gen.py')
    return vocabulary[kind]


def argument_hint(arg):
    """An `args` item's annotation: an enum is a Literal, a list a list of its type."""
    hint = known(ARGUMENTS, arg['type'], 'argument')
    if 'enum' in arg:
        hint = 'Literal[' + ', '.join(repr(word) for word in arg['enum']) + ']'
    return f'list[{hint}]' if arg.get('list') else hint


def identifier(name):
    """A Python name for a key or verb: '-' and '.' become '_', a keyword gets a trailing '_'."""
    text = name.replace('-', '_').replace('.', '_')
    return text + '_' if keyword.iskeyword(text) or text in ('self', 'Any') else text


def class_name(name):
    return ''.join(part[:1].upper() + part[1:] for part in identifier(name).split('_') if part)


def table_of(console):
    """`help` with `json` from a console, with no target needed."""
    call = json.dumps({'verb': 'help', 'args': {'json': True}}, separators=(',', ':'))
    run = subprocess.run([os.path.abspath(console), '-c', call], capture_output=True, text=True, check=False)
    for line in run.stdout.splitlines():
        if line.lstrip().startswith('{'):
            envelope = json.loads(line)
            if not envelope.get('ok'):
                raise SystemExit(f'{console}: {envelope.get("error", {}).get("message", "help with json failed")}')
            return envelope['result']
    raise SystemExit(f'{console}: no help with json result')


class Types:
    """TypedDict classes for result types, emitted before their first use."""

    def __init__(self, named):
        self.named, self.lines, self.done = named, [], set()

    def annotation(self, kind, owner):
        if isinstance(kind, str):
            return known(SCALARS, kind, 'result')
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
        raise ValueError(f'unknown result type {kind!r}: add it to reagentlive_gen.py')

    def struct(self, kind, name):
        fields = []
        for key, spec in kind['struct'].items():
            inner = self.annotation(spec['type'], name + class_name(key))
            fields.append(f'{key!r}: ' + (f'NotRequired[{inner}]' if spec.get('optional') else inner))
        self.lines.append(f'{name} = TypedDict({name!r}, {{' + ', '.join(fields) + '})')
        return name


def parameters(row):
    """(signature text, [(python name, key)]) of a verb's arguments: required ones in row order, then the
    optional ones by name only (None leaves one out, so the console applies its default)."""
    required, optional, names = [], [], []
    for arg in row['args']:
        name = identifier(arg['key'])
        if arg.get('required'):
            required.append(f'{name}: {argument_hint(arg)}')
        else:
            optional.append(f'{name}: {argument_hint(arg)} | None = None')
        names.append((name, arg['key']))
    params = ['self', *required] + (['*', *optional] if optional else [])
    return ', '.join(params), names


def docstring(row):
    lines = [row['summary'] + '.', '', 'Example: ' + json.dumps(row['example'])]
    for arg in row['args']:
        text = f'{identifier(arg["key"])}: {arg.get("help", "")}'.rstrip(': ')
        lines.append(text + (f' (default {json.dumps(arg["default"])})' if 'default' in arg else ''))
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
    source.add_argument('--console', help='the console executable (help with json needs no target)')
    source.add_argument('--table', help='a file holding the help with json result (or its envelope)')
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
