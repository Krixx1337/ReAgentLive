"""reagentlive_mcp: the tools made from a generated module, through an in-memory MCP client (no agent or game).

The module is generated from the real verb table (--table <reagentlive-table.exe>
or REAGENTLIVE_TABLE); the console is the scripted fake from fake_console.py.
Skipped without the MCP Python SDK.
"""
import asyncio
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
FRONTENDS = os.path.join(HERE, '..', '..', 'src', 'reagentlive', 'frontends')
sys.path[:0] = [os.path.join(FRONTENDS, 'python'), os.path.join(FRONTENDS, 'mcp')]

try:
    import reagentlive_mcp  # noqa: E402
    from mcp.shared.memory import create_connected_server_and_client_session
except ImportError:
    reagentlive_mcp = None

TABLE_TOOL = os.environ.get('REAGENTLIVE_TABLE') and os.path.abspath(os.environ['REAGENTLIVE_TABLE'])
FAKE = os.path.join(HERE, 'fake_console.py')
REF = '0x10#1:1'
OBJECT = {'ref': REF, 'name': 'A', 'class': 'Pawn'}


def call(verb, args=None):
    return {'verb': verb, 'args': args or {}}


@unittest.skipUnless(reagentlive_mcp, 'needs the MCP Python SDK (fastmcp or mcp)')
@unittest.skipUnless(TABLE_TOOL, 'needs the verb table: --table <reagentlive-table.exe> or REAGENTLIVE_TABLE')
class McpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.table = json.loads(subprocess.run([TABLE_TOOL], capture_output=True, text=True, check=True).stdout)
        cls.dir = tempfile.TemporaryDirectory()
        cls.replies = os.path.join(cls.dir.name, 'replies.json')
        cls.write_replies(cls.table)
        os.environ['REAGENTLIVE_FAKE_REPLIES'] = cls.replies
        os.environ['REAGENTLIVE_FAKE_LOG'] = cls.log = os.path.join(cls.dir.name, 'lines.txt')

    @classmethod
    def write_replies(cls, table):
        """The fake console's replies, for a console whose table is `table` (a later start reads them again)."""
        with open(cls.replies, 'w') as file:
            json.dump({'table': table, 'lines': {}, 'calls': [
                [call('find', {'text': 'A'}), {'ok': True, 'result': {'rows': [OBJECT], 'total': 1, 'from': 0}}],
                [call('describe', {'object': REF}), {'ok': True, 'result': OBJECT}],
                [call('batch', {'calls': [call('describe', {'object': REF}), {'verb': 'base'}]}), {'ok': True, 'result': {
                    'all_ok': True, 'same_tick': True, 'first_tick': 4, 'last_tick': 4, 'spans_ticks': [], 'rows': [
                        {'command': 'describe', 'ok': True, 'result': OBJECT, 'tick': 4},
                        {'command': 'base', 'ok': True, 'result': {'name': 'Game.exe', 'base': '0x140000000',
                                                                   'size': 4096}, 'tick': 4}]}}],
                [call('read', {'addr': '$1'}), {'ok': False, 'error': {
                    'kind': 'syntax', 'message': "addr: '$1': a JSON call takes the literal ref a result carries, not $N"}}],
            ]}, file)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def run_client(self, body, notices=None):
        server = reagentlive_mcp.build(sys.executable, ['-I', FAKE])

        async def handler(message):
            if notices is not None:
                notices.append(message)

        async def main():
            async with create_connected_server_and_client_session(server, message_handler=handler) as client:
                return await body(client)
        return asyncio.run(main())

    def test_the_tool_list_is_the_table(self):
        async def body(client):
            return {tool.name: tool for tool in (await client.list_tools()).tools}
        tools = self.run_client(body)
        rows = [row for row in self.table['rows'] if row['verb'] != 'quit' and not row.get('blocks')]
        expected = {(row['verb'] if row['origin'] == 'core' else f'{row["origin"]}_{row["verb"]}').replace('-', '_')
                    for row in rows}
        self.assertEqual(set(tools), expected)
        self.assertNotIn('watch', tools)                      # Blocking verbs are left out.
        self.assertTrue(tools['read'].annotations.readOnlyHint)
        self.assertFalse(tools['read'].annotations.destructiveHint)
        self.assertTrue(tools['write'].annotations.destructiveHint)
        self.assertTrue(tools['ue_call'].annotations.destructiveHint)
        schema = tools['read'].inputSchema
        self.assertEqual(schema['required'], ['addr'])
        self.assertEqual(set(schema['properties']), {'addr', 'len', 'type'})
        self.assertIn('rows', tools['ue_find'].outputSchema['properties'])
        self.assertIn('Object', tools['ue_find'].outputSchema['$defs'])   # Named types stay named.
        self.assertIn('calls', tools['batch'].inputSchema['properties'])
        self.assertIn('read <addr> [len] [--type type]', tools['read'].description)

    def test_calls_return_the_console_result_and_errors_are_tool_errors(self):
        async def body(client):
            found = await client.call_tool('ue_find', {'text': 'A'})
            batched = await client.call_tool('batch', {'calls': [call('describe', {'object': REF}), {'verb': 'base'}]})
            refused = await client.call_tool('read', {'addr': '$1'})
            return found, batched, refused
        found, batched, refused = self.run_client(body)
        self.assertFalse(found.isError, found.content[0].text)
        self.assertEqual(found.structuredContent['rows'][0]['ref'], REF)
        self.assertFalse(batched.isError, batched.content[0].text)
        self.assertTrue(batched.structuredContent['same_tick'])
        self.assertTrue(refused.isError)
        self.assertIn('literal ref', refused.content[0].text)
        self.assertIn('"kind": "syntax"', refused.content[0].text)   # The error object, not only its message.

    def test_each_call_starts_a_console_that_shares_one_session(self):
        open(self.log, 'w').close()

        async def body(client):
            for _ in range(2):
                self.assertFalse((await client.call_tool('ue_describe', {'object': REF})).isError)
        self.run_client(body)
        with open(self.log) as file:
            lines = file.read().splitlines()
        sessions = [line for line in lines if line.startswith('session ')]
        self.assertEqual(len(sessions), 2)                       # One console per call...
        self.assertEqual(sessions[0], sessions[1])               # ...in one usage-log session.
        self.assertEqual(lines.count('quit'), 3)                 # Each ended: the table read at start, then each call.

    def test_a_rebuilt_console_remakes_the_tools_and_tells_the_client(self):
        notices = []
        rebuilt = dict(self.table, table_hash='rebuilt')
        rebuilt['rows'] = [row for row in self.table['rows'] if row['verb'] != 'region']

        async def body(client):
            before = {tool.name for tool in (await client.list_tools()).tools}
            self.write_replies(rebuilt)                           # The console is rebuilt between two calls.
            described = await client.call_tool('ue_describe', {'object': REF})
            gone = await client.call_tool('region', {'addr': '0x1000'})
            after = {tool.name for tool in (await client.list_tools()).tools}
            return before, described, gone, after
        try:
            before, described, gone, after = self.run_client(body, notices)
        finally:
            self.write_replies(self.table)
        self.assertFalse(described.isError, described.content[0].text)   # Ran through the new tools.
        self.assertEqual(described.structuredContent['ref'], REF)
        self.assertIn('region', before)
        self.assertNotIn('region', after)
        self.assertTrue(gone.isError)                            # An old tool for a verb the console lost.
        self.assertTrue(any(getattr(getattr(n, 'root', n), 'method', '') == 'notifications/tools/list_changed'
                            for n in notices), notices)


if __name__ == '__main__':
    args = sys.argv[1:]
    if args[:1] == ['--table']:
        TABLE_TOOL = os.path.abspath(args[1])
        args = args[2:]
        McpTests.__unittest_skip__ = not reagentlive_mcp
    unittest.main(argv=[sys.argv[0], *args])
