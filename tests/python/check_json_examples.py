"""Every JSON call a doc shows passes the console's own checks (roadmap Z3: examples are tested like help's).

A doc shows a call in a fenced block or a code span: a JSON call ({"verb": ..., "args": {...}}), a list of them
(a -c one-shot, a plan's steps with expect or repeat, a batch's calls), or a session call whose args are JSON
(session.call('verb', {...})). Each is parsed and handed to the real validator with no target: a console's
--check, or reagentlive-table --check-calls (the core's and the pack's rows; build.h CheckWithoutTarget). It
checks the verb, the args against the row's schema and the row's builder, as a call is checked before it runs;
every {"module": ...} counts as loaded. Text that starts a call with a known verb but is not JSON is refused
too. Left out: a call shape whose verb is no name ("verb": ... or "<verb>"), help rows (they carry origin, pack
or usage), and a block or span marked as a refusal: a line `<!-- refused example -->` right before it.

Usage: check_json_examples.py --table <reagentlive-table or a console> [--root DIR] [FILE_OR_DIR ...]
       (no paths: the repo's docs/, README.md, examples/ and the skill). Exit 1 with one line per bad example.
Run by ctest as reagentlive.json-examples; a host runs it on its own docs with its own console.
"""
import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location('check_verb_names', os.path.join(HERE, 'check_verb_names.py'))
names = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(names)

CALL_KEYS = {'verb', 'args', 'meta', 'timing', 'max_chars', 'big', 'expect', 'repeat'}
HELP_ROW_KEYS = {'origin', 'pack', 'usage', 'summary', 'writes', 'notes', 'type'}
VERB_AT = re.compile(r'\{\s*"verb"\s*:\s*"([a-z][a-z0-9-]*)"')
SESSION_CALL = re.compile(r"""\.call\(\s*['"]([a-z][a-z0-9-]*)['"]\s*,\s*""")
REFUSED_MARK = '<!-- refused example -->'
FENCE = re.compile(r'^\s*(```|~~~)')
CODE_SPAN = re.compile(r'(?<!`)`([^`\n]+)`(?!`)')


def regions(text):
    """(line number, code text) for each fenced block and each code span outside one; marked ones left out."""
    lines = text.split('\n')
    out, block, start, marked, previous = [], None, 0, False, ''
    for number, line in enumerate(lines, 1):
        if FENCE.match(line):
            if block is None:
                block, start, marked = [], number, previous.strip() == REFUSED_MARK
            else:
                if not marked:
                    out.append((start, '\n'.join(block)))
                block = None
        elif block is not None:
            block.append(line)
        elif previous.strip() != REFUSED_MARK:
            out += [(number, span) for span in CODE_SPAN.findall(line)]
        if line.strip():
            previous = line
    return out


def is_call(value):
    return isinstance(value, dict) and isinstance(value.get('verb'), str)


def calls_of(value):
    """The calls a decoded value shows, or None when it is no call (a help row, an args object)."""
    if is_call(value):
        if set(value) - CALL_KEYS and set(value) & HELP_ROW_KEYS:
            return None
        return [value]
    if isinstance(value, list) and value and all(is_call(item) for item in value):
        calls = [calls_of(item) for item in value]
        return None if any(c is None for c in calls) else [c[0] for c in calls]
    return None


def examples(code, verbs):
    """(call, None) for each call in a code text, or (None, why) for call text that is not JSON."""
    # A shell line may escape the quotes of a -c call.
    text = code.replace('\\"', '"') if '{\\"verb\\"' in code else code
    decoder = json.JSONDecoder()
    covered, found = [], []
    i = 0
    while i < len(text):
        if text[i] not in '[{':
            i += 1
            continue
        try:
            value, end = decoder.raw_decode(text, i)
        except ValueError:
            i += 1
            continue
        covered.append((i, end))        # JSON, so not "not JSON", call or not.
        calls = calls_of(value)
        if calls is None:
            i += 1
            continue
        found += [(call, None) for call in calls]
        i = end
    for match in SESSION_CALL.finditer(text):
        try:
            args, _ = decoder.raw_decode(text, match.end())
        except ValueError:
            continue
        if isinstance(args, dict):
            found.append(({'verb': match.group(1), 'args': args}, None))
    for match in VERB_AT.finditer(text):
        if match.group(1) in verbs and not any(a <= match.start() < b for a, b in covered):
            found.append((None, f'not JSON: {text[match.start():match.start() + 70]!r}'))
    return found


def check(console, calls):
    """The validator's envelope for each call, in order."""
    exe = os.path.abspath(console)
    table = os.path.basename(exe).lower().startswith('reagentlive-table')
    command = [exe, '--check-calls'] if table else [exe, '--check']
    stdin = ''.join(json.dumps(call, separators=(',', ':')) + '\n' for call in calls)
    run = subprocess.run(command, input=stdin, capture_output=True, text=True, check=False)
    envelopes = [json.loads(line) for line in run.stdout.splitlines() if line.lstrip().startswith('{')]
    if len(envelopes) != len(calls):
        raise SystemExit(f'{exe}: {len(envelopes)} answers for {len(calls)} calls\n{run.stdout}{run.stderr}')
    return envelopes


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--table', required=True, help='reagentlive-table, or a console that takes --check')
    parser.add_argument('--root', default=os.path.normpath(os.path.join(HERE, '..', '..')))
    parser.add_argument('paths', nargs='*')
    args = parser.parse_args(argv)
    verbs = {row['verb'] for row in names.table_of(args.table)['rows']}
    generated = {os.path.abspath(os.path.join(args.root, 'docs', 'commands', page)) for page in ('results.md', 'reference.md')}
    problems, pending = [], []
    for path in names.files(args.paths or names.default_paths(args.root), generated):
        with open(path, encoding='utf-8') as file:
            text = file.read()
        where = os.path.relpath(path, args.root)
        for line, code in regions(text):
            for call, why in examples(code, verbs):
                if why:
                    problems.append(f'{where}:{line}: {why}')
                else:
                    pending.append((f'{where}:{line}', call))
    seen = {}
    for where, call in pending:
        seen.setdefault(json.dumps(call, sort_keys=True), (where, call))
    unique = list(seen.values())
    for (where, call), envelope in zip(unique, check(args.table, [call for _, call in unique]) if unique else []):
        if not envelope.get('ok'):
            message = envelope.get('error', {}).get('message', envelope)
            problems.append(f'{where}: {json.dumps(call, separators=(", ", ": "))}: {message}')
    for line in problems:
        print(line)
    print(f'{len(problems)} of {len(unique)} JSON examples refused' if problems
          else f'all {len(unique)} JSON examples pass the console\'s checks')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
