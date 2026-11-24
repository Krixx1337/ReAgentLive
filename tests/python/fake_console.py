"""A scripted console for the Python tests: no agent or game.

Run as a console: `-c '{"verb":"help","args":{"json":true}}'` prints the
table and exits (as a real console does with no target), and `-c` with a
call carrying `refused` (inline or `@<file>`) logs a `refused <session>
<verb> <kind> <key>` line and answers its error; any other `-c` call is
logged (after its `session <id>` line) and answered, then it exits; on stdin, one JSON
call per line: `help` with `json` answers the table and with `hash` its hash
(read at start: a test may change it between runs), `quit` ends it, and any
other call is answered by matching its verb and arguments. Every other call
gets a schema error. Calls but the table's are logged (`quit` as the word),
after a `session <id>` line for a console given --session and a
`packs <list>` line for one given --packs. Its replies file: {"table": ...,
"calls": [[call, reply]...]}, from REAGENTLIVE_FAKE_REPLIES; the log is
REAGENTLIVE_FAKE_LOG.
"""
import json
import os
import sys


def key(call):
    return json.dumps({'verb': call.get('verb'), 'args': call.get('args') or {}}, sort_keys=True)


def main():
    replies = json.load(open(os.environ['REAGENTLIVE_FAKE_REPLIES']))
    table = {'ok': True, 'result': replies['table']}
    help_json = key({'verb': 'help', 'args': {'json': True}})
    if '-c' in sys.argv:
        text = sys.argv[sys.argv.index('-c') + 1]
        one = json.load(open(text[1:])) if text.startswith('@') else json.loads(text)
        if key(one) == help_json:
            print(json.dumps(dict(table, command='help')), flush=True)
        elif 'refused' in one:  # A frontend's own refusal: logged, answered, never run.
            session = sys.argv[sys.argv.index('--session') + 1] if '--session' in sys.argv else ''
            with open(os.environ['REAGENTLIVE_FAKE_LOG'], 'a') as log:
                log.write(f'refused {session} {one["verb"]} {one["refused"]["kind"]} {one["refused"].get("key", "")}\n')
            print(json.dumps({'command': one['verb'], 'ok': False, 'error': one['refused']}), flush=True)
        elif key(one) == key({'verb': 'help', 'args': {'hash': True}}):
            print(json.dumps({'command': 'help', 'ok': True,
                              'result': {'rows': [], 'table_hash': replies['table'].get('table_hash')}}), flush=True)
        else:  # One call, then exit: a one-shot console.
            with open(os.environ['REAGENTLIVE_FAKE_LOG'], 'a') as log:
                if '--session' in sys.argv:
                    log.write(f'session {sys.argv[sys.argv.index("--session") + 1]}\n')
                log.write(json.dumps(one) + '\n')
            calls = {key(call): reply for call, reply in replies['calls']}
            unknown = {'ok': False, 'error': {'kind': 'schema', 'message': 'unknown'}}
            print(json.dumps(dict(calls.get(key(one), unknown), command=one.get('verb'))), flush=True)
        return
    calls = {key(call): reply for call, reply in replies['calls']}
    log = open(os.environ['REAGENTLIVE_FAKE_LOG'], 'a')
    if '--session' in sys.argv:
        log.write(f'session {sys.argv[sys.argv.index("--session") + 1]}\n')
    if '--packs' in sys.argv:
        log.write(f'packs {sys.argv[sys.argv.index("--packs") + 1]}\n')
    unknown = {'ok': False, 'error': {'kind': 'schema', 'message': 'unknown'}}
    for line in sys.stdin:
        call = json.loads(line)
        if key(call) == help_json:
            print(json.dumps(dict(table, command='help')), flush=True)
            continue
        if key(call) == key({'verb': 'help', 'args': {'hash': True}}):
            print(json.dumps({'command': 'help', 'ok': True,
                              'result': {'rows': [], 'table_hash': replies['table'].get('table_hash')}}), flush=True)
            continue
        log.write(('quit' if call.get('verb') == 'quit' else line.strip()) + '\n')
        log.flush()
        if call.get('verb') == 'quit':
            break
        print(json.dumps(dict(calls.get(key(call), unknown), command=call.get('verb'))), flush=True)


if __name__ == '__main__':
    main()
