"""reagentlive_mcp: the tools made from the console's table, through an in-memory MCP client (no agent or game).

The table is the real one (--table <reagentlive-table.exe> or REAGENTLIVE_TABLE);
the console is the scripted fake from fake_console.py.
Skipped without the MCP Python SDK 2.
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
    import anyio  # noqa: E402
    import reagentlive_mcp  # noqa: E402
    from mcp import Client
except ImportError:
    reagentlive_mcp = None

TABLE_TOOL = os.environ.get('REAGENTLIVE_TABLE') and os.path.abspath(os.environ['REAGENTLIVE_TABLE'])
FAKE = os.path.join(HERE, 'fake_console.py')
REF = '0x10#1:1'
OBJECT = {'ref': REF, 'name': 'A', 'class': 'Pawn'}
ALL = ['explore', 'native', 'observe', 'modify']


def call(verb, args=None):
    return {'verb': verb, 'args': args or {}}


@unittest.skipUnless(reagentlive_mcp, 'needs the MCP Python SDK 2 (mcp)')
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
            json.dump({'table': table, 'calls': [
                [call('find', {'text': 'A'}), {'ok': True, 'result': {'rows': [OBJECT], 'total': 1, 'from': 0}}],
                [call('describe', {'object': REF}), {'ok': True, 'result': OBJECT}],
                [call('describe', {'index': 5, 'serial': 9}), {'ok': True, 'result': OBJECT,
                                                                 'context_warning': 'past context_limit 10'}],
                [call('batch', {'calls': [call('describe', {'object': REF}), {'verb': 'modules'}]}), {'ok': True, 'result': {
                    'all_ok': True, 'same_tick': True, 'first_tick': 4, 'last_tick': 4, 'spans_ticks': [], 'rows': [
                        {'command': 'describe', 'ok': True, 'result': OBJECT, 'tick': 4},
                        {'command': 'modules', 'ok': True, 'result': {'rows': [{'name': 'Game.exe', 'base': '0x140000000',
                                                                               'size': 4096}]}, 'tick': 4}]}}],
                [call('rows', {'object': REF}), {'ok': True, 'digest': {
                    'chars': 90000, 'max_chars': 25000, 'rows': {'path': 'rows', 'count': 300},
                    'largest': [{'path': 'rows[].value.Icon', 'chars': 60000}], 'narrow': ['view.match', 'view.fields', 'view.limit'],
                    'next': json.dumps(call('rows', {'object': REF, 'view': {'fields': [['Cost']], 'limit': 40}}))}}],
                [call('call', {'object': REF, 'function': 'Buy'}), {'ok': True, 'digest': {
                    'chars': 40000, 'max_chars': 25000, 'largest': [], 'file': 'C:/temp/r.json'}}],
                [call('read', {'addr': '0x10', 'len': 5000}), {'ok': False, 'error': {
                    'kind': 'schema', 'message': 'len: 5000 is past 4096',
                    'arg': {'key': 'len', 'type': 'int', 'default': 64, 'help': 'how many bytes'},
                    'example': call('read', {'addr': '0x10', 'len': 4096})}}],
            ]}, file)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def run_client(self, body, notices=None, mode='auto', **options):
        server = reagentlive_mcp.build(sys.executable, ['-I', FAKE], **options)

        async def handler(message):
            if notices is not None:
                notices.append(message)

        async def main():
            async with Client(server, message_handler=handler, mode=mode) as client:
                return await body(client)
        return asyncio.run(main())

    def test_the_tool_list_is_the_table(self):
        async def body(client):
            return {tool.name: tool for tool in (await client.list_tools()).tools}
        tools = self.run_client(body, packs=ALL)
        rows = [row for row in self.table['rows'] if row['verb'] != 'quit' and not row.get('blocks')]
        expected = {(row['verb'] if row['origin'] == 'core' else f'{row["origin"]}_{row["verb"]}').replace('-', '_')
                    for row in rows}
        self.assertEqual(set(tools), expected | {'packs'})
        self.assertIn('watch_start', tools)                   # A capture returns at once: a tool.
        self.assertTrue(tools['read'].annotations.read_only_hint)
        self.assertFalse(tools['read'].annotations.destructive_hint)
        self.assertTrue(tools['write'].annotations.destructive_hint)
        self.assertTrue(tools['ue_call'].annotations.destructive_hint)
        schema = tools['read'].input_schema
        self.assertEqual(schema['required'], ['addr'])
        self.assertEqual(set(schema['properties']), {'addr', 'len', 'type', 'max_chars'})   # max_chars: every tool's.
        self.assertFalse(schema['additionalProperties'])          # A guessed argument is refused.
        self.assertEqual(schema['properties']['type']['type'], 'string')
        self.assertIn('rows', tools['ue_find'].output_schema['properties'])
        self.assertEqual(tools['batch'].input_schema['properties']['calls']['type'], 'array')
        write = tools['write'].input_schema
        self.assertNotIn('required', write)                       # The row's required args only: clear needs no addr.
        self.assertEqual(tools['read'].input_schema['required'], ['addr'])
        self.assertIn('undo', write['properties'])
        self.assertEqual(tools['search'].input_schema['properties']['type']['enum'][:2], ['hex', 'u32'])
        self.assertIn('Example: read {"addr": ', tools['read'].description)
        self.assertEqual(schema['properties']['len']['anyOf'][0]['type'], 'integer')   # Or "0x..." text.

    def tool_list(self, **options):
        async def body(client):
            return {tool.name: tool for tool in (await client.list_tools()).tools}
        return self.run_client(body, **options)

    def test_compact_output_schemas_keep_the_top_level_keys_only(self):
        compact, off = (self.tool_list(output_schemas=p) for p in ('compact', 'off'))
        del compact['packs'], off['packs']   # The adapter's own tool has no output schema.
        self.assertEqual(set(off), set(compact))
        find = next(row for row in self.table['rows'] if row['verb'] == 'find')
        self.assertEqual(set(compact['ue_find'].output_schema['properties']), set(find['type']['struct']) | {'digest'})
        self.assertEqual(compact['ue_find'].output_schema['properties']['rows'], {'type': 'array'})
        self.assertNotIn('$defs', compact['ue_find'].output_schema)
        self.assertTrue(all(tool.output_schema is None for tool in off.values()))
        size = lambda tools: sum(len(json.dumps(t.model_dump(exclude_none=True))) for t in tools.values())
        self.assertLess(size(off), size(compact))

    def test_a_type_the_adapter_lacks_stops_it(self):
        with self.assertRaisesRegex(ValueError, "unknown argument type 'newtype'"):
            reagentlive_mcp.argument('newtype')
        with self.assertRaisesRegex(ValueError, "unknown result type 'newscalar'"):
            reagentlive_mcp.result_type('newscalar', {})
        # Every type the real table uses is known (the generator's map is checked in test_session.py).
        named = self.table['types']
        for row in self.table['rows']:
            reagentlive_mcp.input_schema(row)
            reagentlive_mcp.output_schema(row, named)

    def test_every_profile_returns_the_console_result(self):
        for profile in reagentlive_mcp.OUTPUT_SCHEMAS:
            async def body(client):
                return await client.call_tool('ue_find', {'text': 'A'})
            found = self.run_client(body, output_schemas=profile)
            self.assertFalse(found.is_error, f'{profile}: {found.content[0].text}')
            self.assertEqual(found.structured_content['rows'][0]['ref'], REF)

    def test_packs_pick_the_list_and_only_modify_writes(self):
        packs = {row['verb']: row['pack'] for row in self.table['rows']}
        servable = lambda names: {row['verb'] for row in self.table['rows'] if row['pack'] in names
                                  and row['verb'] != 'quit' and not row.get('blocks')}
        explore = self.tool_list()                                # Default: explore.
        self.assertEqual(len(explore) - 1, len(servable({'explore'})))
        self.assertIn('ue_get', explore)
        self.assertNotIn('disasm', explore)
        for names in (['explore'], ['explore', 'native'], ['explore', 'native', 'observe']):
            tools = self.tool_list(packs=names)
            self.assertEqual(len(tools) - 1, len(servable(set(names))), names)
            self.assertFalse(any(tool.annotations.destructive_hint for tool in tools.values()), names)
        everything = self.tool_list(packs=ALL)
        self.assertEqual({name for name, tool in everything.items() if tool.annotations.destructive_hint},
                         {'write', 'ue_call', 'ue_release', 'ue_calls_start', 'ue_game_thread'})
        self.assertEqual(packs['each'], 'explore')
        with self.assertRaisesRegex(ValueError, '--packs: unknown pack nope'):
            reagentlive_mcp.build(sys.executable, ['-I', FAKE], packs=['nope'])
        with self.assertRaisesRegex(ValueError, '--allow: unknown pack'):
            reagentlive_mcp.build(sys.executable, ['-I', FAKE], allow=['writes'])

    def test_the_packs_tool_enables_within_the_ceiling_and_says_the_list_changed(self):
        open(self.log, 'w').close()

        async def body(client):
            listed = await client.call_tool('packs', {})
            outside = await client.call_tool('packs', {'action': 'enable', 'pack': 'modify'})
            off = await client.call_tool('disasm', {'addr': '0x1000'})
            async with client.listen(tools_list_changed=True) as events:
                enabled = await client.call_tool('packs', {'action': 'enable', 'pack': 'native'})
                with anyio.fail_after(5):
                    event = await events.__anext__()
            after = {tool.name for tool in (await client.list_tools(cache_mode='refresh')).tools}
            again = await client.call_tool('packs', {'action': 'enable', 'pack': 'native'})
            explore = await client.call_tool('packs', {'action': 'disable', 'pack': 'explore'})
            await client.call_tool('ue_describe', {'object': REF})
            disabled = await client.call_tool('packs', {'action': 'disable', 'pack': 'native'})
            final = {tool.name for tool in (await client.list_tools(cache_mode='refresh')).tools}
            return listed, outside, off, enabled, event, after, again, explore, disabled, final
        listed, outside, off, enabled, event, after, again, explore, disabled, final = self.run_client(
            body, allow=['native', 'observe'])
        self.assertFalse(listed.is_error, listed.content[0].text)
        states = {pack['pack']: pack['state'] for pack in listed.structured_content['packs']}
        self.assertEqual(states, {'explore': 'on', 'native': 'off', 'observe': 'off', 'modify': 'outside'})
        native = next(pack for pack in listed.structured_content['packs'] if pack['pack'] == 'native')
        self.assertIn('disasm', native['tools'])
        self.assertTrue(outside.is_error)                         # The ceiling is the launcher's, not the agent's.
        self.assertIn('--allow modify', outside.content[0].text)
        self.assertTrue(off.is_error)
        self.assertIn('enable it with packs', off.content[0].text)
        self.assertFalse(enabled.is_error, enabled.content[0].text)
        self.assertTrue(enabled.structured_content['changed'])
        self.assertEqual(type(event).__name__, 'ToolsListChanged')
        self.assertIn('disasm', after)
        self.assertFalse(again.structured_content['changed'])
        self.assertTrue(explore.is_error)                         # Always on.
        self.assertTrue(disabled.structured_content['changed'])
        self.assertNotIn('disasm', final)
        self.assertIn('packs', final)
        with open(self.log) as file:
            lines = file.read().splitlines()
        self.assertIn('packs explore,native,observe', lines)     # Every console gets the ceiling.

    def test_calls_return_the_console_result_and_errors_are_tool_errors(self):
        async def body(client):
            found = await client.call_tool('ue_find', {'text': 'A'})
            batched = await client.call_tool('batch', {'calls': [call('describe', {'object': REF}), {'verb': 'modules'}]})
            refused = await client.call_tool('read', {'addr': '0x10', 'len': 5000})
            guessed = await client.call_tool('read', {'addr': '0x10', 'length': 4})
            nulls = await client.call_tool('ue_find', {'text': 'A', 'cls': None})
            sneaky = await client.call_tool('batch', {'calls': [call('describe', {'object': REF}), {'verb': 'write'}]})
            native = await client.call_tool('batch', {'calls': [call('disasm', {'addr': '0x1000'})]})
            return found, batched, refused, guessed, nulls, sneaky, native
        found, batched, refused, guessed, nulls, sneaky, native = self.run_client(body, packs=['explore', 'modify'])
        self.assertTrue(sneaky.is_error)                               # A read-only tool never carries a write.
        self.assertIn('send write as its own tool', sneaky.content[0].text)
        self.assertTrue(native.is_error)                               # Nor a verb of a pack that is off.
        self.assertIn('disasm is in native', native.content[0].text)
        self.assertTrue(guessed.is_error)                              # Checked against the row before a console runs.
        self.assertIn("'length' was unexpected", guessed.content[0].text)
        self.assertFalse(nulls.is_error, nulls.content[0].text)       # A null argument is a left-out one.
        self.assertFalse(found.is_error, found.content[0].text)
        self.assertEqual(found.structured_content['rows'][0]['ref'], REF)
        self.assertFalse(batched.is_error, batched.content[0].text)
        self.assertTrue(batched.structured_content['same_tick'])
        self.assertTrue(refused.is_error)                              # Refused by the console, with the call to make.
        self.assertIn('len: 5000 is past 4096; example: read {"addr": "0x10", "len": 4096}', refused.content[0].text)
        self.assertIn('"kind": "schema"', refused.content[0].text)   # The error object, not only its message.
        self.assertIn('"key": "len"', refused.content[0].text)

    def test_a_batch_refusal_names_only_a_way_out_this_server_allows(self):
        async def body(client):
            write = await client.call_tool('batch', {'calls': [{'verb': 'write'}]})
            native = await client.call_tool('batch', {'calls': [call('disasm', {'addr': '0x1000'})]})
            return write, native
        write, native = self.run_client(body, allow=['native'])
        self.assertTrue(write.is_error)                                # modify is outside the ceiling: no enable.
        self.assertIn('write is in modify, outside the ceiling of this server', write.content[0].text)
        self.assertNotIn('"enable"', write.content[0].text)
        self.assertTrue(native.is_error)                               # native may be enabled: say how.
        self.assertIn('packs {"action": "enable", "pack": "native"} first', native.content[0].text)

    def test_a_session_past_its_context_limit_adds_the_warning_beside_the_result(self):
        async def body(client):
            return await client.call_tool('ue_describe', {'index': 5, 'serial': 9})
        warned = self.run_client(body)
        self.assertFalse(warned.is_error, warned.content[0].text)
        self.assertEqual(warned.structured_content['ref'], REF)          # The result itself is unchanged.
        self.assertEqual(warned.content[1].text, 'Context warning: past context_limit 10')

    def test_a_result_past_its_max_chars_is_a_digest_with_the_call_to_make(self):
        open(self.log, 'w').close()

        async def body(client):
            narrowed = await client.call_tool('ue_rows', {'object': REF, 'max_chars': 0})
            kept = await client.call_tool('ue_call', {'object': REF, 'function': 'Buy'})
            return narrowed, kept
        narrowed, kept = self.run_client(body, packs=['modify'])
        self.assertFalse(narrowed.is_error, narrowed.content[0].text)
        self.assertEqual(narrowed.structured_content['digest']['chars'], 90000)
        text = narrowed.content[0].text
        self.assertIn('past max_chars 25000', text)
        self.assertIn('Call next: ue_rows {"object": "0x10#1:1", "view": {"fields": [["Cost"]], "limit": 40}}', text)
        self.assertIn('rows[].value.Icon (60000)', text)
        self.assertFalse(kept.is_error, kept.content[0].text)
        self.assertIn('must not run again', kept.content[0].text)
        self.assertIn('C:/temp/r.json', kept.content[0].text)
        self.assertNotIn('Call next', kept.content[0].text)
        with open(self.log) as file:
            sent = [json.loads(line) for line in file if line.startswith('{')]
        self.assertEqual(sent[0]['max_chars'], 0)                # The call's own key, not one of the verb's args.
        self.assertNotIn('max_chars', sent[0]['args'])

    def test_each_call_starts_a_console_that_shares_one_session(self):
        open(self.log, 'w').close()

        async def body(client):
            for _ in range(2):
                self.assertFalse((await client.call_tool('ue_describe', {'object': REF})).is_error)
        self.run_client(body)
        with open(self.log) as file:
            lines = file.read().splitlines()
        sessions = [line for line in lines if line.startswith('session ')]
        self.assertEqual(len(sessions), 2)                       # One console per call...
        self.assertEqual(sessions[0], sessions[1])               # ...in one usage-log session.
        self.assertEqual(lines.count('packs explore'), 2)        # Each with the server's ceiling.
        self.assertEqual(lines.count('quit'), 3)                 # Each ended: the table read at start, then each call.

    def rebuilt_run(self, body, notices=None, mode='auto'):
        """`body` on a client whose console is rebuilt (one verb fewer) after its first list."""
        rebuilt = dict(self.table, table_hash='rebuilt')
        rebuilt['rows'] = [row for row in self.table['rows'] if row['verb'] != 'touches']

        async def wrapped(client):
            before = {tool.name for tool in (await client.list_tools()).tools}
            self.write_replies(rebuilt)                           # The console is rebuilt between two calls.
            return before, *(await body(client))
        try:
            return self.run_client(wrapped, notices, mode, packs=['explore', 'native'])
        finally:
            self.write_replies(self.table)

    def test_a_rebuilt_console_remakes_the_tools_and_tells_the_client(self):
        async def body(client):
            async with client.listen(tools_list_changed=True) as events:
                described = await client.call_tool('ue_describe', {'object': REF})
                with anyio.fail_after(5):
                    event = await events.__anext__()
            gone = await client.call_tool('touches', {'addr': '0x1000', 'offsets': [8]})
            after = {tool.name for tool in (await client.list_tools(cache_mode='refresh')).tools}
            return described, gone, after, event
        before, described, gone, after, event = self.rebuilt_run(body)
        self.assertFalse(described.is_error, described.content[0].text)  # Ran through the new tools.
        self.assertEqual(described.structured_content['ref'], REF)
        self.assertIn('touches', before)
        self.assertNotIn('touches', after)
        self.assertTrue(gone.is_error)                           # An old tool for a verb the console lost.
        self.assertEqual(type(event).__name__, 'ToolsListChanged')   # On the client's listen stream.

    def test_an_older_client_hears_the_change_as_a_notification(self):
        notices = []

        async def body(client):
            return (await client.call_tool('ue_describe', {'object': REF}),)
        _, described = self.rebuilt_run(body, notices, mode='legacy')
        self.assertFalse(described.is_error, described.content[0].text)
        self.assertTrue(any(getattr(getattr(n, 'root', n), 'method', '') == 'notifications/tools/list_changed'
                            for n in notices), notices)

if __name__ == '__main__':
    args = sys.argv[1:]
    if args[:1] == ['--table']:
        TABLE_TOOL = os.path.abspath(args[1])
        args = args[2:]
        McpTests.__unittest_skip__ = not reagentlive_mcp
    unittest.main(argv=[sys.argv[0], *args])
