"""MCP tools for one ReAgentLive console, made from its own verb table.

    python reagentlive_mcp.py --console <console.exe> [-- <console args>]

One tool per verb, made in memory with the generator's code
(reagentlive_gen.py) from the console's `help --json`: a core verb by its
name (`read`), a pack's or the host's prefixed by its origin (`ue_find`,
`host_world`). The argument and result schemas come from the functions'
types, so no tool is written by hand and nothing here names a pack or a
verb. The marks come from the table: a verb that writes the target's
memory or calls into it is destructive, every other one read-only, so a
client can allow the read-only tools and ask first for the rest. A verb
that holds the console for seconds (`blocks`) is left out: a call would
outlast the client's timeout; use `watch-start` and read it later.

Arguments carry the literal refs results print; `$N` is refused (the
console's JSON calls take no session state). `batch` takes a list of calls
({verb, args}, verb by its console name) and runs them in as few host
ticks as possible.

A console per call, never one kept open: nothing holds the executable
between calls, so a rebuilt console links and the next call runs it.
Calls run one at a time (a second console would be refused as busy).
Each start reads the table's hash (`help --hash`, no target needed); when
it changed, the tools are made again before the call runs, and the client
is told the list changed. With no target the console answers `help` and
`stats` and refuses the rest as `startup`. Every console of one server
shares a usage-log session (`--session`) and is marked `--caller tool`:
its results reach the agent's context. Transport: standard input and
output; the protocol revision is whichever the installed SDK negotiates.

Needs the MCP Python SDK (its FastMCP, mcp.server.fastmcp); the session
client and the generator need only the standard library.
"""
import argparse
import inspect
import json
import os
import sys
import threading
import time
import types

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'python'))
import reagentlive_gen  # noqa: E402
from reagentlive_session import ReAgentLiveError, Record, Session  # noqa: E402

from typing import Annotated  # noqa: E402

import anyio  # noqa: E402
from mcp.server.fastmcp import FastMCP  # noqa: E402
from mcp.server.lowlevel.server import NotificationOptions  # noqa: E402
from mcp.types import CallToolResult, TextContent, ToolAnnotations  # noqa: E402

INSTRUCTIONS = (
    'Tools for a live process through a ReAgentLive console. Each tool is one console verb; its description '
    'shows the usage, what every argument takes and the notes. Pass the literal `ref` a result carries '
    '(0x<address>#<index>:<serial>) to the next call; $N is refused. Use `batch` for reads that must come from '
    'one moment. Read-only tools may be allowed freely; tools marked destructive write the target\'s memory '
    'or call into it, so confirm each one first. Results keep the console\'s keys: a list is `rows`, a page '
    'carries `next_cursor` (pass it as `start`), a key that does not apply is absent. `stats` shows what this '
    'session has cost so far, also with no target.')


def module_from(table):
    """The generated functions for a `help --json` result, as a module in memory."""
    module = types.ModuleType(f'reagentlive_tools_{table.get("table_hash")}')
    sys.modules[module.__name__] = module  # Its TypedDicts name it as their module; schema builders look it up.
    exec(compile(reagentlive_gen.generate(table, 'console'), module.__name__, 'exec'), module.__dict__)
    return module


def tool_name(verb):
    name = verb['method'] if verb['origin'] == 'core' else f'{verb["attribute"]}_{verb["method"]}'
    return name.replace('-', '_')


class Live:
    """Starts a console for each call, and keeps the tools made from its table."""

    def __init__(self, console, args=()):
        self.console, self.args = os.path.abspath(console), list(args)
        self.session_id = f'mcp-{time.strftime("%Y%m%dT%H%M%S")}-{os.getpid()}'
        self.lock = threading.Lock()
        with Session(self.console, self.args, caller='tool') as session:
            self.table = session.table
        self.module = module_from(self.table)

    def run(self, name, kwargs):
        """One tool call on a fresh console: (its result, True when the table changed and the tools were made again)."""
        with self.lock:
            session = Session(self.console, [*self.args, '--session', self.session_id], caller='tool', table=self.table)
            try:
                changed = session.table_hash != self.table.get('table_hash')
                if changed:
                    self.table, self.module = session.table, module_from(session.table)
                verb = next((v for v in self.module.VERBS if tool_name(v) == name), None)
                if verb is None:
                    raise RuntimeError(f'the console has no verb for {name} any more; reconnect for its tool list')
                target = self.module.Console(session)
                if verb['attribute']:
                    target = getattr(target, verb['attribute'])
                method = getattr(target, verb['method'])
                try:
                    inspect.signature(method).bind(**kwargs)
                except TypeError as error:
                    raise RuntimeError(f'{name} takes other arguments since the console was rebuilt ({error}); '
                                       'reconnect for its tool list') from None
                try:
                    result = method(**kwargs)
                except ReAgentLiveError as error:
                    # The whole error object, not only its message: a refused --expect carries `found`, every error its kind.
                    raise RuntimeError(f'{error}; error: {json.dumps(error.error)}') from None
            finally:
                session.close()
        return (result.raw if isinstance(result, Record) else result), changed


def make_tool(live, server, verb, method):
    """A function FastMCP reads: the method's parameters (without self) and result type; returns the plain result."""
    signature = inspect.signature(method)
    parameters = list(signature.parameters.values())[1:]
    name = tool_name(verb)

    async def tool(**kwargs):
        raw, changed = await anyio.to_thread.run_sync(live.run, name, kwargs)
        if changed:
            register(server, live)
            await server.get_context().session.send_tool_list_changed()
        # The console's result as it is: FastMCP would dump the TypedDict with every absent key as null,
        # which its own output schema then refuses. The schema still comes from the annotation.
        return CallToolResult(content=[TextContent(type='text', text=json.dumps(raw))], structuredContent=raw)

    returns = Annotated[CallToolResult, signature.return_annotation]
    tool.__signature__ = signature.replace(parameters=parameters, return_annotation=returns)
    tool.__annotations__ = {p.name: p.annotation for p in parameters} | {'return': returns}
    tool.__name__ = name
    tool.__doc__ = method.__doc__
    return tool


def register(server, live):
    """The tools for the live table, in place of any made before."""
    for name in getattr(server, 'tool_names', ()):
        server.remove_tool(name)
    server.tool_names = []
    module = live.module
    for verb in module.VERBS:
        if verb['blocks']:
            continue
        method = getattr(getattr(module, verb['cls']), verb['method'])
        writes = verb['writes']
        server.add_tool(make_tool(live, server, verb, method), name=tool_name(verb), description=method.__doc__,
                        annotations=ToolAnnotations(readOnlyHint=not writes, destructiveHint=writes,
                                                    idempotentHint=False, openWorldHint=False))
        server.tool_names.append(tool_name(verb))


def build(console, args=(), name='reagentlive'):
    """The server for a console: its tools from the console's table, which needs no target."""
    server = FastMCP(name, instructions=INSTRUCTIONS)
    # Say the tool list can change, so a client asks again on the notice.
    lowlevel = server._mcp_server
    options = lowlevel.create_initialization_options
    lowlevel.create_initialization_options = lambda notification_options=None, experimental_capabilities=None: \
        options(notification_options or NotificationOptions(tools_changed=True), experimental_capabilities)
    server.live = Live(console, args)
    register(server, server.live)
    return server


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--console', required=True, help='the console executable')
    parser.add_argument('--name', default='reagentlive', help='the server name clients show')
    parser.add_argument('console_args', nargs='*', help='after --: arguments for the console (a pid, --names <file>)')
    args = parser.parse_args(argv)
    build(args.console, args.console_args, args.name).run()


if __name__ == '__main__':
    sys.exit(main())
