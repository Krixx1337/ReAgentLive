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
import reagentlive_gen  # noqa: E402

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
        path = os.path.join(cls.dir.name, 'generated_console.py')
        with open(path, 'w', encoding='utf-8') as file:
            file.write(reagentlive_gen.generate(cls.table, 'reagentlive-table'))
        cls.module = reagentlive_mcp.load_module(path)
        replies = os.path.join(cls.dir.name, 'replies.json')
        with open(replies, 'w') as file:
            json.dump({'table': cls.table, 'lines': {}, 'calls': [
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
        os.environ['REAGENTLIVE_FAKE_REPLIES'] = replies
        os.environ['REAGENTLIVE_FAKE_LOG'] = os.path.join(cls.dir.name, 'lines.txt')
        cls.console = os.path.join(cls.dir.name, 'console.cmd')
        with open(cls.console, 'w') as file:
            file.write(f'@"{sys.executable}" -I "{FAKE}" %*\n')

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def run_client(self, body):
        server = reagentlive_mcp.build(self.module, sys.executable, ['-I', FAKE], check=False)

        async def main():
            async with create_connected_server_and_client_session(server) as client:
                return await body(client)
        try:
            return asyncio.run(main())
        finally:
            server.live.drop()

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

    def test_a_stale_module_is_refused_before_the_server_starts(self):
        self.assertEqual(reagentlive_mcp.console_hash(self.console), self.table['table_hash'])
        was = self.module.TABLE_HASH
        self.module.TABLE_HASH = 'different'
        try:
            with self.assertRaises(SystemExit) as caught:
                reagentlive_mcp.build(self.module, self.console)
            self.assertIn('reagentlive_gen.py', str(caught.exception))
        finally:
            self.module.TABLE_HASH = was


if __name__ == '__main__':
    args = sys.argv[1:]
    if args[:1] == ['--table']:
        TABLE_TOOL = os.path.abspath(args[1])
        args = args[2:]
        McpTests.__unittest_skip__ = not reagentlive_mcp
    unittest.main(argv=[sys.argv[0], *args])
