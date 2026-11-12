"""MCP tools for one ReAgentLive console, made from its generated Python module.

    python reagentlive_gen.py --console <console.exe> -o my_console.py
    python reagentlive_mcp.py --console <console.exe> --module my_console.py

One tool per verb of the generated module (reagentlive_gen.py): a core verb
by its name (`read`), a pack's or the host's prefixed by its origin
(`ue_find`, `host_world`). The argument and result schemas come from the
functions' types, so no tool is written by hand and nothing here names a
pack or a verb. The marks come from the table: a verb that writes the
target's memory or calls into it is destructive, every other one read-only,
so a client can allow the read-only tools and ask first for the rest. A
verb that holds the console for seconds (`blocks`) is left out: a call
would outlast the client's timeout; use `watch-start` and read it later.

Arguments carry the literal refs results print; `$N` is refused (the
console's JSON calls take no session state). `batch` takes a list of calls
({verb, args}, verb by its console name) and runs them in as few host
ticks as possible.

The module is checked against the console before the server starts: a
module generated from another table is refused. The console starts on the
first tool call and again after it exits, so the tool list is known with
no target running. Transport: standard input and output; the protocol
revision is whichever the installed SDK negotiates.

Needs the MCP Python SDK (its FastMCP, mcp.server.fastmcp); the session
client and the generated module need only the standard library.
"""
import argparse
import importlib.util
import inspect
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'python'))
from reagentlive_session import ReAgentLiveError, Record, Session  # noqa: E402

from typing import Annotated  # noqa: E402

from mcp.server.fastmcp import FastMCP  # noqa: E402
from mcp.types import CallToolResult, TextContent, ToolAnnotations  # noqa: E402

INSTRUCTIONS = (
    'Tools for a live process through a ReAgentLive console. Each tool is one console verb; its description '
    'shows the usage, what every argument takes and the notes. Pass the literal `ref` a result carries '
    '(0x<address>#<index>:<serial>) to the next call; $N is refused. Use `batch` for reads that must come from '
    'one moment. Read-only tools may be allowed freely; tools marked destructive write the target\'s memory '
    'or call into it, so confirm each one first. Results keep the console\'s keys: a list is `rows`, a page '
    'carries `next_cursor` (pass it as `from_`), a key that does not apply is absent.')


def load_module(path):
    """The generated module at `path`."""
    folder = os.path.dirname(os.path.abspath(path))
    if folder not in sys.path:
        sys.path.insert(0, folder)
    spec = importlib.util.spec_from_file_location(os.path.splitext(os.path.basename(path))[0], path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def console_hash(console):
    """The console's table hash, from `help --json` (no target needed)."""
    run = subprocess.run([os.path.abspath(console), '-c', 'help --json'], capture_output=True, text=True, check=False)
    for line in run.stdout.splitlines():
        if line.lstrip().startswith('{'):
            envelope = json.loads(line)
            if envelope.get('ok'):
                return envelope['result'].get('table_hash')
    raise SystemExit(f'{console}: help --json gave no table')


class Live:
    """The console process and the generated Console over it, started on first use."""

    def __init__(self, module, console, args=()):
        self.module, self.console, self.args = module, os.path.abspath(console), list(args)
        self.session = self.wrapped = None

    def get(self):
        if self.session is None:
            self.session = Session(self.console, self.args)
            self.wrapped = self.module.Console(self.session)
        return self.wrapped

    def drop(self):
        if self.session is not None:
            self.session.close()
        self.session = self.wrapped = None


def tool_name(verb):
    return verb['method'] if verb['origin'] == 'core' else f'{verb["attribute"]}_{verb["method"]}'


def make_tool(live, verb, method):
    """A function FastMCP reads: the method's parameters (without self) and result type; returns the plain result."""
    signature = inspect.signature(method)
    parameters = list(signature.parameters.values())[1:]

    def tool(**kwargs):
        target = live.get()
        if verb['attribute']:
            target = getattr(target, verb['attribute'])
        try:
            result = getattr(target, verb['method'])(**kwargs)
        except ReAgentLiveError as error:
            if error.error.get('message') == 'console exited' or error.command == 'startup':
                live.drop()
            # The whole error object, not only its message: a refused --expect carries `found`, every error its kind.
            raise RuntimeError(f'{error}; error: {json.dumps(error.error)}') from None
        raw = result.raw if isinstance(result, Record) else result
        # The console's result as it is: FastMCP would dump the TypedDict with every absent key as null,
        # which its own output schema then refuses. The schema still comes from the annotation.
        return CallToolResult(content=[TextContent(type='text', text=json.dumps(raw))], structuredContent=raw)

    returns = Annotated[CallToolResult, signature.return_annotation]
    tool.__signature__ = signature.replace(parameters=parameters, return_annotation=returns)
    tool.__annotations__ = {p.name: p.annotation for p in parameters} | {'return': returns}
    tool.__name__ = tool_name(verb).replace('-', '_')
    tool.__doc__ = method.__doc__
    return tool


def build(module, console, args=(), name='reagentlive', check=True):
    """The server for a generated module and its console; refuses a module from another table."""
    if check:
        found = console_hash(console)
        if found != module.TABLE_HASH:
            raise SystemExit(f'{module.__name__} was generated from table {module.TABLE_HASH}, the console has '
                             f'{found}: run reagentlive_gen.py --console {console} -o <module> again')
    server = FastMCP(name, instructions=INSTRUCTIONS)
    live = Live(module, console, args)
    for verb in module.VERBS:
        if verb['blocks']:
            continue
        method = getattr(getattr(module, verb['cls']), verb['method'])
        writes = verb['writes']
        server.add_tool(make_tool(live, verb, method), name=tool_name(verb), description=method.__doc__,
                        annotations=ToolAnnotations(readOnlyHint=not writes, destructiveHint=writes,
                                                    idempotentHint=False, openWorldHint=False))
    server.live = live
    return server


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--console', required=True, help='the console executable')
    parser.add_argument('--module', required=True, help='the module reagentlive_gen.py generated for it')
    parser.add_argument('--name', default='reagentlive', help='the server name clients show')
    parser.add_argument('console_args', nargs='*', help='after --: arguments for the console (a pid, --names <file>)')
    args = parser.parse_args(argv)
    server = build(load_module(args.module), args.console, args.console_args, args.name)
    try:
        server.run()
    finally:
        server.live.drop()


if __name__ == '__main__':
    sys.exit(main())
