"""MCP tools for one ReAgentLive console, made from its own verb table.

    python reagentlive_mcp.py --console <console.exe> [--output-schemas compact|off]
                              [--families core,<pack>,host] [--read-only] [-- <console args>]

One tool per row of the console's `help --json`: a core verb by its name
(`read`), a pack's or the host's prefixed by its origin (`ue_find`,
`host_world`). Each tool's input schema is made here from its row (the
argument types of syntax/verb.h), and a call goes to the console as a JSON
call with the row's keys, so nothing here names a pack or a verb. The marks
come from the table: a verb that writes the target's memory or calls into
it is destructive, every other one read-only, so a client can allow the
read-only tools and ask first for the rest. A verb that holds the console
for seconds (`blocks`) is left out: a call would outlast the client's
timeout; use `watch-start` and read it later.

Arguments carry the literal refs results print; `$N` is refused (the
console's JSON calls take no session state). `batch` takes a list of calls
({verb, args}, verb by its console name) and runs them in as few host
ticks as possible.

A console per call, never one kept open: nothing holds the executable
between calls, so a rebuilt console links and the next call runs it.
Calls run one at a time (a second console would wait, then be refused as
busy). Each start reads the table's hash (`help --hash`, no target
needed); when it changed, the table is read again before the call runs,
and the client is told the tool list changed. With no target the console
answers `help` and `stats` and refuses the rest as `startup`. Every
console of one server shares a usage-log session (`--session`) and is
marked `--caller tool`: its results reach the agent's context.

What a client loads up front is the tool list, so it is kept small: output
schemas are `compact` by default (each result's top-level keys and their
JSON types, from the row; the console checks every result against its
whole type either way), `off` advertises none. `--families` serves only the
verbs of the origins named (`help --json` lists them under `origins`) and
`--read-only` leaves out every verb that writes the target or calls into it.

Needs the MCP Python SDK (its low-level server, mcp.server.lowlevel); the
session client needs only the standard library. Transport: standard input
and output; the protocol revision is whichever the installed SDK negotiates.
"""
import argparse
import json
import os
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'python'))
from reagentlive_session import Digest, ReAgentLiveError, Record, Session  # noqa: E402

import anyio  # noqa: E402
import jsonschema  # noqa: E402
from mcp import types  # noqa: E402
from mcp.server.lowlevel import NotificationOptions, Server  # noqa: E402
from mcp.server.stdio import stdio_server  # noqa: E402

INSTRUCTIONS = (
    'Tools for a live process through a ReAgentLive console. Each tool is one console verb; its description '
    'shows the usage, what every argument takes and the notes. Pass the literal `ref` a result carries '
    '(0x<address>#<index>:<serial>) to the next call; $N is refused. Use `batch` for reads that must come from '
    'one moment. Read-only tools may be allowed freely; tools marked destructive write the target\'s memory '
    'or call into it, so confirm each one first. Results keep the console\'s keys: a list is `rows`, a page '
    'carries `next_cursor` (pass it as `start`), a key that does not apply is absent. A result past 25,000 '
    'characters is not shown: a digest stands for it, with its largest parts and the call narrowed to run '
    'next (or the file holding the whole result of a call that must not run again); `budget` raises the '
    'limit for one call (0: none). `stats` shows what this session has cost so far, also with no target.')
# The output budget, one argument every tool takes: the console's line flag, not a verb's.
BUDGET = {'type': 'integer', 'minimum': 0,
          'description': 'characters this result may take (default 25000; 0: no limit); past it a digest stands for it'}

OUTPUT_SCHEMAS = ('compact', 'off')

# Argument types (syntax/verb.h) as JSON Schema. An enum ('a|b') is made from its words.
CALL = {'type': 'object', 'properties': {'verb': {'type': 'string'}, 'args': {'type': 'object'}},
        'required': ['verb'], 'additionalProperties': False}
ARGUMENTS = {'addr': {'type': ['integer', 'string']}, 'ref': {'type': 'string'}, 'int': {'type': 'integer'},
             'dur': {'type': ['number', 'string']}, 'bytes': {'type': 'string'}, 'pattern': {'type': 'string'},
             'text': {'type': 'string'}, 'str': {'type': 'string'}, 'path': {'type': 'string'},
             'layout': {'type': 'string'}, 'range': {'type': 'string'}, 'line': {'type': 'string'},
             'span': {'type': 'array', 'prefixItems': [{'type': ['integer', 'string']}, {'type': 'integer'}],
                      'minItems': 2, 'maxItems': 2},
             'calls': {'type': 'array', 'items': CALL}, 'bool': {'type': 'boolean'}}
# One value however many tokens it spans: a positional that takes several takes one of these, not a list.
JOINED = ('bytes', 'pattern', 'text')
# Result scalars (results/schema.h) as JSON types; `value` is game data and stays untyped.
RESULTS = {'bool': 'boolean', 'u32': 'integer', 'u64': 'integer', 'i32': 'integer', 'i64': 'integer',
           'f64': 'number', 'str': 'string', 'addr': 'string', 'hex': 'string', 'bytes': 'string', 'ref': 'string',
           'value': None}


def argument(kind):
    """An argument type's schema; a type the console added and this file lacks stops the server."""
    if '|' in kind:
        return {'type': 'string', 'enum': kind.split('|')}
    if kind not in ARGUMENTS:
        raise ValueError(f'unknown argument type {kind!r}: add it to reagentlive_mcp.py')
    return dict(ARGUMENTS[kind])


def described(schema, item):
    """The schema with the row's help line and default."""
    text = item.get('help', '') + (f' (default {item["default"]})' if 'default' in item else '')
    return dict(schema, description=text) if text else schema


def input_schema(row):
    """A verb's arguments as JSON Schema, keyed as the console's JSON calls take them."""
    properties, required = {}, []
    for positional in row['positionals']:
        schema, arity = argument(positional['type']), positional['arity']
        if arity in ('some', 'any') and positional['type'] not in JOINED:
            schema = {'type': 'array', 'items': schema}
        properties[positional['key']] = described(schema, positional)
        if arity in ('one', 'some', 'rest'):
            required.append(positional['key'])
    for flag in row['flags']:
        schema, takes = argument(flag['type']), flag['takes']
        if takes == 'nothing':
            schema = {'type': 'boolean'}
        elif takes == 'maybe':
            schema = {'anyOf': [{'type': 'boolean'}, schema]}
        elif takes == 'some' and flag['type'] not in JOINED:
            schema = {'anyOf': [schema, {'type': 'array', 'items': schema}]}
        properties[flag['key']] = described(schema, flag)
        if flag.get('required'):
            required.append(flag['key'])
    properties.setdefault('budget', BUDGET)
    out = {'type': 'object', 'properties': properties, 'additionalProperties': False}
    if required:
        out['required'] = required
    return out


def result_type(kind, named):
    """The JSON type of a result type, None for any (named types resolved)."""
    while isinstance(kind, dict) and 'named' in kind:
        kind = named[kind['named']]['type']
    if isinstance(kind, str):
        if kind not in RESULTS:
            raise ValueError(f'unknown result type {kind!r}: add it to reagentlive_mcp.py')
        return RESULTS[kind]
    for key, json_type in (('enum', 'string'), ('list', 'array'), ('map', 'object'), ('struct', 'object')):
        if key in kind:
            return json_type
    raise ValueError(f'unknown result type {kind!r}: add it to reagentlive_mcp.py')


def output_schema(row, named):
    """A result's top-level keys and their JSON types: never narrower than its whole type."""
    kind = row['type']
    while isinstance(kind, dict) and 'named' in kind:
        kind = named[kind['named']]['type']
    fields = kind.get('struct', {}) if isinstance(kind, dict) else {}
    properties = {}
    for key, spec in fields.items():
        json_type = result_type(spec['type'], named)
        properties[key] = {'type': json_type} if json_type else {}
    # A digest stands for a result past the budget: one or the other.
    properties['digest'] = {'type': 'object'}
    out = {'type': 'object', 'properties': properties}
    required = [key for key, spec in fields.items() if not spec.get('optional')]
    out['anyOf'] = [{'required': required} if required else {'not': {'required': ['digest']}}, {'required': ['digest']}]
    return out


def digest_text(digest, tool):
    """What a tool's caller reads for a digest: why there is no result, and what to call instead."""
    parts = ', '.join(f'{part["path"]} ({part["chars"]})' for part in digest.get('largest', []))
    text = (f'Not shown: the result is {digest["chars"]} characters, past the budget of {digest["budget"]}. '
            f'Largest parts: {parts or "none"}.')
    if 'rows' in digest:
        text += f' The list is {digest["rows"]["path"]}, {digest["rows"]["count"]} rows.'
    if 'file' in digest:
        text += f' The call must not run again: its whole result is in {digest["file"]}; read what you need from it.'
    elif 'next' in digest:
        call = Digest(digest).next_call()
        text += f' Call next: {tool} {json.dumps(call.get("args", {}))}.'
    if 'narrow' in digest:
        text += f' This tool narrows with: {", ".join(flag.lstrip("-") for flag in digest["narrow"])}; budget 0 shows all.'
    return text


def description(row):
    """What a tool's caller reads: the summary, the usage line, the notes and the marks."""
    lines = [row['summary'] + '.', '', row['usage']]
    if row.get('notes'):
        lines += ['', row['notes']]
    if row.get('writes'):
        lines += ['', 'Marked: writes.']
    return '\n'.join(lines)


def tool_name(row):
    name = row['verb'] if row['origin'] == 'core' else f'{row["origin"]}_{row["verb"]}'
    return name.replace('-', '_')


def served(row, families, read_only):
    """Whether the server offers a tool for this verb."""
    if row['verb'] == 'quit' or row.get('blocks') or (read_only and row.get('writes')):
        return False
    return families is None or row['origin'] in families


class Live:
    """Starts a console for each call, and keeps the table it read last."""

    def __init__(self, console, args=()):
        self.console, self.args = os.path.abspath(console), list(args)
        self.session_id = f'mcp-{time.strftime("%Y%m%dT%H%M%S")}-{os.getpid()}'
        self.lock = threading.Lock()
        with Session(self.console, self.args, caller='tool') as session:
            self.table = session.table

    def row(self, name):
        return next((row for row in self.table['rows'] if tool_name(row) == name), None)

    def run(self, name, arguments):
        """One tool call on a fresh console: (its result or Digest, True when the table changed since the last call)."""
        with self.lock:
            session = Session(self.console, [*self.args, '--session', self.session_id], caller='tool', table=self.table)
            try:
                changed = session.table_hash != self.table.get('table_hash')
                if changed:
                    self.table = session.table
                row = self.row(name)
                if row is None:
                    raise RuntimeError(f'the console has no verb for {name} any more; reconnect for its tool list')
                # Absent and null are the same to a call; the row's schema checks the rest.
                arguments = {key: value for key, value in arguments.items() if value is not None}
                budget = None if any(flag['key'] == 'budget' for flag in row['flags']) else arguments.pop('budget', None)
                try:
                    jsonschema.validate(arguments if budget is None else dict(arguments, budget=budget),
                                        input_schema(row))
                except jsonschema.ValidationError as error:
                    stale = ' (the console was rebuilt: reconnect for its tool list)' if changed else ''
                    raise RuntimeError(f'{name}: {error.message}{stale}; usage: {row["usage"]}') from None
                try:
                    result = session.call(row['verb'], arguments, budget=budget)
                except ReAgentLiveError as error:
                    # The whole error object, not only its message: a refused --expect carries `found`, every error its kind.
                    raise RuntimeError(f'{error}; error: {json.dumps(error.error)}') from None
            finally:
                session.close()
        return (result.raw if isinstance(result, Record) else result), changed


class ReAgentLiveServer(Server):
    """The low-level server, saying that its tool list can change so a client asks again on the notice."""

    def create_initialization_options(self, notification_options=None, experimental_capabilities=None):
        return super().create_initialization_options(notification_options or NotificationOptions(tools_changed=True),
                                                     experimental_capabilities)


def instructions(families, read_only):
    """The server's instructions, saying what it leaves out."""
    text = INSTRUCTIONS
    if families is not None:
        text += f' This server serves only the {", ".join(sorted(families))} verbs; the console has more (help lists them).'
    if read_only:
        text += ' It serves no verb that writes the target or calls into it.'
    return text


def build(console, args=(), name='reagentlive', output_schemas='compact', families=None, read_only=False):
    """The server for a console: its tools from the console's table, which needs no target.

    output_schemas: compact or off. families: the origins to serve (None: all). read_only: no writing verbs.
    """
    if output_schemas not in OUTPUT_SCHEMAS:
        raise ValueError(f'output_schemas is one of {", ".join(OUTPUT_SCHEMAS)}, not {output_schemas!r}')
    live = Live(console, args)
    if families is not None:
        families = set(families)
        known = {origin['origin'] for origin in live.table.get('origins', [])}
        if unknown := families - known:
            raise ValueError(f'unknown famil{"ies" if len(unknown) > 1 else "y"} {", ".join(sorted(unknown))}; '
                             f'this console has {", ".join(sorted(known))}')
    server = ReAgentLiveServer(name, instructions=instructions(families, read_only))

    def tools():
        named = live.table.get('types', {})
        return [types.Tool(name=tool_name(row), description=description(row), inputSchema=input_schema(row),
                           outputSchema=output_schema(row, named) if output_schemas == 'compact' else None,
                           annotations=types.ToolAnnotations(readOnlyHint=not row.get('writes'),
                                                             destructiveHint=bool(row.get('writes')),
                                                             idempotentHint=False, openWorldHint=False))
                for row in live.table['rows'] if served(row, families, read_only)]

    tools()  # A type this file lacks stops the server at start, not at the client's first list.

    @server.list_tools()
    async def list_tools():
        return tools()

    @server.call_tool(validate_input=False)  # Live.run checks against the row of the console it started.
    async def call_tool(name, arguments):
        row = live.row(name)
        if row is None or not served(row, families, read_only):
            return types.CallToolResult(isError=True, content=[types.TextContent(type='text', text=f'no tool {name}')])
        try:
            raw, changed = await anyio.to_thread.run_sync(live.run, name, arguments or {})
        except RuntimeError as error:
            raw, changed = None, False
            result = types.CallToolResult(isError=True, content=[types.TextContent(type='text', text=str(error))])
        else:
            if isinstance(raw, Digest):  # Past the budget: what to call instead, not the result.
                raw = {'digest': dict(raw)}
                content = [types.TextContent(type='text', text=digest_text(raw['digest'], name))]
            else:
                content = [types.TextContent(type='text', text=json.dumps(raw))]
            result = types.CallToolResult(content=content, structuredContent=raw)
        if changed:
            await server.request_context.session.send_tool_list_changed()
        return result

    server.live = live
    return server


async def serve(server):
    async with stdio_server() as (read, write):
        await server.run(read, write, server.create_initialization_options())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--console', required=True, help='the console executable')
    parser.add_argument('--name', default='reagentlive', help='the server name clients show')
    parser.add_argument('--output-schemas', choices=OUTPUT_SCHEMAS, default='compact',
                        help='result schemas in the tool list: top-level keys only, or none')
    parser.add_argument('--families', help='serve only these origins, comma-separated (help --json lists them)')
    parser.add_argument('--read-only', action='store_true', help='leave out every verb that writes or calls')
    parser.add_argument('console_args', nargs='*', help='after --: arguments for the console (a pid, --names <file>)')
    args = parser.parse_args(argv)
    families = [f.strip() for f in args.families.split(',') if f.strip()] if args.families else None
    try:
        server = build(args.console, args.console_args, args.name, args.output_schemas, families, args.read_only)
    except ValueError as error:
        parser.error(str(error))
    anyio.run(serve, server)


if __name__ == '__main__':
    sys.exit(main())
