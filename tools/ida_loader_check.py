"""Is the IDA loader installed in IDA's loaders folder the repo's copy? Read-only.

    python -I tools/ida_loader_check.py [--loaders <dir>]

It hashes the repo's src/reagentlive/frontends/ida/reagentlive_ida.py and every ReAgentLive loader in
the loaders folder (default: %APPDATA%\\Hex-Rays\\IDA Pro\\loaders; any file name, found by its first
docstring line) and prints one JSON object: `repo` (its SHA-256), `installed` (one row per copy:
`path`, `sha256`, `current`) and `state`: current (every copy matches), stale (one differs: copy the
repo's file over it) or missing (none installed). Exit code 0, 1 or 2 to match. Inside IDA the same
check is `rl.loader_digest() == rl.loader_digest('<repo copy>')`.
"""
import argparse
import hashlib
import json
import os
import sys

REPO_LOADER = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                           'src', 'reagentlive', 'frontends', 'ida', 'reagentlive_ida.py')
MARKER = b'"""ReAgentLive\'s IDA side'


def digest(path):
    """A file's SHA-256, hex."""
    with open(path, 'rb') as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def default_loaders():
    """IDA's user loaders folder on Windows, or None without APPDATA."""
    appdata = os.environ.get('APPDATA')
    return os.path.join(appdata, 'Hex-Rays', 'IDA Pro', 'loaders') if appdata else None


def check(loaders, repo=REPO_LOADER):
    """The report: the repo's digest, each installed ReAgentLive loader's, and the state."""
    wanted = digest(repo)
    installed = []
    if loaders and os.path.isdir(loaders):
        for name in sorted(os.listdir(loaders)):
            path = os.path.join(loaders, name)
            if not name.endswith('.py') or not os.path.isfile(path):
                continue
            with open(path, 'rb') as handle:
                if not handle.read(len(MARKER)) == MARKER:
                    continue
            found = digest(path)
            installed.append({'path': path, 'sha256': found, 'current': found == wanted})
    state = 'missing' if not installed else 'current' if all(row['current'] for row in installed) else 'stale'
    return {'repo': wanted, 'loaders': loaders, 'installed': installed, 'state': state}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--loaders', default=default_loaders(), help="IDA's loaders folder")
    report = check(parser.parse_args(argv).loaders)
    print(json.dumps(report, indent=1))
    return {'current': 0, 'stale': 1, 'missing': 2}[report['state']]


if __name__ == '__main__':
    sys.exit(main())
