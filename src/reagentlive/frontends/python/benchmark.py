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


def timed(session, line):
    """(seconds, timing dict, envelope) for one command with --timing."""
    started = time.perf_counter()
    envelope = session.send(line + ' --timing')
    return time.perf_counter() - started, envelope.get('timing', {}), envelope


def tick_rate(session):
    session.status()  # Starts a fresh interval.
    return lambda: session.status()['ticks_per_second']


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
        base = s.ok('base')['base']
        rate = tick_rate(s)
        started = time.perf_counter()
        for _ in range(200):
            s.read(base, 8)
        out['read_ms'] = (time.perf_counter() - started) / 200 * 1000
        out['read_ticks_per_second'] = rate()
        rate = tick_rate(s)
        seconds, timing, envelope = timed(s, f'dump {args.object}')
        out['dump'] = {'ms': seconds * 1000, **timing, 'bytes': len(json.dumps(envelope)),
                       'ticks_per_second': rate()}
        _, timing, envelope = timed(s, f'dump {args.object} --nonzero')
        out['dump_nonzero_bytes'] = len(json.dumps(envelope))
        seconds, snap, _ = timed(s, f'snap {args.object}')
        seconds2, diff, _ = timed(s, f'diff {args.object}')
        out['snap_diff'] = {'ms': (seconds + seconds2) * 1000, 'trips': snap.get('trips', 0) + diff.get('trips', 0)}
        if args.path:
            seconds, timing, envelope = timed(s, f'get {args.object} {args.path}')
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
