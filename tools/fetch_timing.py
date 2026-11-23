"""Timing for the fast-lane IDA loop: an address to decompiled text, and databases opened per run.

Inside IDA (an open save database), load it as a module next to the loader:

    import importlib.util
    spec = importlib.util.spec_from_file_location('fetch_timing', r'<repo>/tools/fetch_timing.py')
    timing = importlib.util.module_from_spec(spec); spec.loader.exec_module(timing)
    rl = timing.loader(r'<repo>/src/reagentlive/frontends/ida/reagentlive_ida.py')
    timing.time_fetch(rl, ['0x<addr>'], '<dir>')                 # live: 1 function
    timing.time_fetch(rl, ['0x<a>', '0x<b>', ...], '<dir>')      # live: a chain, hop by hop
    timing.time_saves(rl, ['<save>', ...], '<dir>')              # offline: saved files, no console

Each returns {'rows': one per hop, 'total': seconds}. A hop is what one question costs: the
console's save (live only), add_saved into the open database, decompile_to of the hop's functions.

Outside IDA, databases opened by an IDA MCP session (its session log, JSON lines):

    python tools/fetch_timing.py opens <session.jsonl> [--since 2026-01-01T00:00]
"""
import json
import sys
import time


def loader(path):
    """The IDA loader module from its file."""
    import importlib.util

    spec = importlib.util.spec_from_file_location('reagentlive_ida', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _functions(rl, path):
    with open(path, encoding='utf-8') as handle:
        plan = rl.parse(handle.read().splitlines())
    if isinstance(plan, str):
        raise ValueError(f'{path}: {plan}')
    return [start for start, _ in plan['functions']]


def time_saves(rl, paths, out_dir):
    """IDA, offline: per saved file, add_saved then decompile_to of its functions, timed."""
    rows, total = [], 0.0
    for path in paths:
        started = time.perf_counter()
        loaded = rl.add_saved(path)
        added = time.perf_counter()
        decompiled = rl.decompile_to(_functions(rl, path), out_dir)
        done = time.perf_counter()
        rows.append({'save': path, 'functions': len(decompiled), 'load': round(added - started, 3),
                     'decompile': round(done - added, 3), 'seconds': round(done - started, 3),
                     'this_typed': loaded.get('this_typed', 0),
                     'errors': [one['error'] for one in decompiled if 'error' in one]})
        total += done - started
    return {'rows': rows, 'total': round(total, 3)}


def time_fetch(rl, addrs, out_dir, depth=1, console=None):
    """IDA, live: one fetch per address in order (a chain is walked one hop at a time), timed."""
    rows, total = [], 0.0
    for address in addrs:
        started = time.perf_counter()
        out = rl.fetch(address, depth=depth, decompile_dir=out_dir, console=console)
        seconds = time.perf_counter() - started
        rows.append({'address': str(address), 'functions': len(out['functions']), 'seconds': round(seconds, 3),
                     'parts': out['seconds'], 'this_typed': out['this_typed']})
        total += seconds
    return {'rows': rows, 'total': round(total, 3)}


def opens(lines, since=None):
    """Databases an IDA MCP session opened: {'opened': n, 'seconds': their open time, 'paths': each once}."""
    count, seconds, paths, calls = 0, 0.0, [], {}
    for line in lines:
        try:
            event = json.loads(line)
        except ValueError:
            continue
        if event.get('tool') != 'open_database' or (since and event.get('ts', '') < since):
            continue
        if event.get('event') == 'tool_call':
            calls[event.get('call_id')] = (event.get('input') or {}).get('path')
        elif event.get('event') == 'tool_result':
            count += 1
            seconds += (event.get('duration_ms') or 0) / 1000
            path = calls.get(event.get('call_id'))
            if path and path not in paths:
                paths.append(path)
    return {'opened': count, 'seconds': round(seconds, 1), 'paths': paths}


def main(argv):
    if len(argv) >= 2 and argv[0] == 'opens':
        since = argv[argv.index('--since') + 1] if '--since' in argv else None
        with open(argv[1], encoding='utf-8') as handle:
            print(json.dumps(opens(handle, since)))
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
