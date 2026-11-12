"""A scripted console for the Python tests: no agent or game.

Run as a console: `-c "help --json"` prints the table and exits (as a real
console does with no target); on stdin, `help --json` answers the table, a
JSON call is answered by matching its verb and arguments, a typed line by
its exact text. Every other line gets a syntax error. Lines but the table
are logged. Its replies file: {"table": ..., "calls": [[call, reply]...],
"lines": {line: reply}}, from REAGENTLIVE_FAKE_REPLIES; the log is
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
    if sys.argv[1:] == ['-c', 'help --json']:
        print(json.dumps(dict(table, command='help')), flush=True)
        return
    calls = {key(call): reply for call, reply in replies['calls']}
    log = open(os.environ['REAGENTLIVE_FAKE_LOG'], 'a')
    unknown = {'ok': False, 'error': {'kind': 'syntax', 'message': 'unknown'}}
    for line in sys.stdin:
        line = line.strip()
        if line == 'help --json':
            print(json.dumps(dict(table, command='help')), flush=True)
            continue
        log.write(line + '\n')
        log.flush()
        if line == 'quit':
            break
        if line.startswith('{'):
            call = json.loads(line)
            reply, command = calls.get(key(call), unknown), call.get('verb')
        else:
            reply, command = replies['lines'].get(line, unknown), line.split(' ')[0]
        print(json.dumps(dict(reply, command=command)), flush=True)


if __name__ == '__main__':
    main()
