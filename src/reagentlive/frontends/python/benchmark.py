"""Round-trip benchmark for a ReAgentLive console against a running host.

    python benchmark.py <console.exe> --object <ref> [--class <Class>] [--path <Field.Path>]

<ref> is a literal ref (0x...#index:serial) of a large object; --class names
a class for the one-shot lookup; --path is a pointer chain for `get` on the
object. Prints one JSON object; record it with the host tick rate.
"""
import argparse
import json
import subprocess
import time

from reagentlive_session import Session


def timed(session, verb, args):
    """(seconds, timing dict, envelope) for one JSON call with timing."""
    started = time.perf_counter()
    envelope = session.send(json.dumps({'verb': verb, 'args': args, 'timing': True}))
    return time.perf_counter() - started, envelope.get('timing', {}), envelope


def tick_rate(session):
    session.call('status')  # Starts a fresh interval.
    return lambda: session.call('status')['ticks_per_second']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('console')
    parser.add_argument('--object', required=True)
    parser.add_argument('--class', dest='class_name')
    parser.add_argument('--path')
    args = parser.parse_args()
    out = {}
    with Session(args.console) as s:
        rate = tick_rate(s)
        time.sleep(1)
        out['idle_ticks_per_second'] = rate()
        base = s.call('modules')['rows'][0]['base']
        rate = tick_rate(s)
        started = time.perf_counter()
        for _ in range(200):
            s.call('read', {'addr': base, 'len': 8})
        out['read_ms'] = (time.perf_counter() - started) / 200 * 1000
        out['read_ticks_per_second'] = rate()
        rate = tick_rate(s)
        seconds, timing, envelope = timed(s, 'dump', {'object': args.object})
        out['dump'] = {'ms': seconds * 1000, **timing, 'bytes': len(json.dumps(envelope)),
                       'ticks_per_second': rate()}
        _, timing, envelope = timed(s, 'dump', {'object': args.object, 'nonzero': True})
        out['dump_nonzero_bytes'] = len(json.dumps(envelope))
        seconds, snap, _ = timed(s, 'snap', {'object': args.object})
        seconds2, diff, _ = timed(s, 'dump', {'object': args.object, 'changed': True})
        out['snap_diff'] = {'ms': (seconds + seconds2) * 1000, 'trips': snap.get('trips', 0) + diff.get('trips', 0)}
        if args.path:
            seconds, timing, envelope = timed(s, 'get', {'object': args.object, 'paths': [args.path]})
            out['get'] = {'ms': seconds * 1000, **timing, 'same_tick': envelope.get('result', {}).get('same_tick')}
    if args.class_name:
        runs = []
        for _ in range(2):
            started = time.perf_counter()
            subprocess.run([args.console, '-c', f'fields {args.class_name}'], capture_output=True, check=False)
            runs.append((time.perf_counter() - started) * 1000)
        out['one_shot_fields_ms'] = runs
    print(json.dumps(out, indent=1))


if __name__ == '__main__':
    main()
