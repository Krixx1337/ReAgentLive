"""Is ReAgentLive's IDA side set up, and set it up. Read-only without --install.

    python -I tools/ida_setup.py [--install] [--loaders <dir>] [--python <exe>]

The IDA side is the repo's reagentlive_ida package (src/reagentlive/frontends/ida) and a loader stub,
reagentlive_loader.py, in IDA's user loaders folder (default: %APPDATA%\\Hex-Rays\\IDA Pro\\loaders).
IDA imports the package from the repo through a path file, reagentlive_ida.pth, in its Python's
site-packages (default Python: the one IDA's registry key Python3TargetDLL names), so it always runs
the repo's code: nothing to copy after an update.

Prints one JSON object: `package` (the folder IDA's Python imports reagentlive_ida from, or null),
`installed` (one row per ReAgentLive file in the loaders folder: `path`, `current`), `why` (what is
wrong) and `state`: current, stale (an old or edited loader file, or the package from elsewhere) or
missing (no stub, or no package). Exit code 0, 1 or 2 to match. --install writes the path file and the
stub, removes other ReAgentLive files from the loaders folder (the one-file loader of old), then checks.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

REPO_IDA = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        'src', 'reagentlive', 'frontends', 'ida')
STUB = os.path.join(REPO_IDA, 'reagentlive_loader.py')
STUB_NAME = 'reagentlive_loader.py'
PTH_NAME = 'reagentlive_ida.pth'
MARKER = b'"""ReAgentLive\'s IDA'  # The first docstring line of the stub and of the one-file loader of old.


def digest(path):
    """A file's SHA-256, hex."""
    with open(path, 'rb') as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def default_loaders():
    """IDA's user loaders folder on Windows, or None without APPDATA."""
    appdata = os.environ.get('APPDATA')
    return os.path.join(appdata, 'Hex-Rays', 'IDA Pro', 'loaders') if appdata else None


def ida_python():
    """The python.exe beside the DLL IDA's registry key Python3TargetDLL names (idapyswitch sets it), or
    None (no key, not Windows): then pass --python."""
    try:
        import winreg

        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Hex-Rays\IDA') as key:
            dll = winreg.QueryValueEx(key, 'Python3TargetDLL')[0]
    except (ImportError, OSError):
        return None
    python = os.path.join(os.path.dirname(dll), 'python.exe')
    return python if os.path.isfile(python) else None


def _ask(python, code):
    """Run `code` in `python` (isolated, so only site-packages path files count): its stdout, or None."""
    try:
        ran = subprocess.run([python, '-I', '-c', code], capture_output=True, text=True, timeout=60)
    except OSError:
        return None
    return ran.stdout.strip() if ran.returncode == 0 else None


def imported_from(python):
    """The folder `python` imports reagentlive_ida from, or None."""
    return _ask(python, 'import os, reagentlive_ida; print(os.path.dirname(os.path.abspath(reagentlive_ida.__file__)))') or None


def site_packages(python):
    """`python`'s site-packages folder."""
    found = json.loads(_ask(python, 'import json, site; print(json.dumps(site.getsitepackages()))') or '[]')
    found = [path for path in found if path.rstrip('\\/').endswith('site-packages')]
    if not found:
        raise SystemExit(f'{python}: no site-packages folder')
    return found[0]


def check(loaders, package, repo=REPO_IDA):
    """The report for a loaders folder and the folder IDA's Python imports the package from (None: not
    importable)."""
    wanted = digest(STUB)
    installed = []
    if loaders and os.path.isdir(loaders):
        for name in sorted(os.listdir(loaders)):
            path = os.path.join(loaders, name)
            if not name.endswith('.py') or not os.path.isfile(path):
                continue
            with open(path, 'rb') as handle:
                if handle.read(len(MARKER)) != MARKER:
                    continue
            installed.append({'path': path, 'current': name == STUB_NAME and digest(path) == wanted})
    why = []
    if not any(row['current'] for row in installed):
        why.append(f'no current {STUB_NAME} in the loaders folder')
    why += [f"{row['path']}: an old or edited ReAgentLive loader" for row in installed if not row['current']]
    if package is None:
        why.append("IDA's Python does not import reagentlive_ida")
    elif os.path.normcase(os.path.abspath(package)) != os.path.normcase(os.path.join(repo, 'reagentlive_ida')):
        why.append(f'reagentlive_ida comes from {package}, not this repo')
    missing = package is None or not any(os.path.basename(row['path']) == STUB_NAME for row in installed)
    state = 'current' if not why else 'missing' if missing else 'stale'
    return {'loaders': loaders, 'installed': installed, 'package': package, 'why': why, 'state': state}


def install(loaders, python):
    """The path file in `python`'s site-packages, the stub in `loaders`, other ReAgentLive files there
    removed. Returns what it wrote and removed."""
    pth = os.path.join(site_packages(python), PTH_NAME)
    try:
        with open(pth, 'w', encoding='utf-8') as handle:
            handle.write(REPO_IDA + '\n')
    except OSError as error:
        raise SystemExit(f'cannot write {pth}: {error} (a Python under Program Files needs an elevated shell)')
    os.makedirs(loaders, exist_ok=True)
    removed = [row['path'] for row in check(loaders, None)['installed']
               if os.path.basename(row['path']) != STUB_NAME]
    for path in removed:
        os.remove(path)
    shutil.copyfile(STUB, os.path.join(loaders, STUB_NAME))
    for path in (pth, os.path.join(loaders, STUB_NAME)):
        where = redirected(path)
        if where:
            raise SystemExit(f'{path} landed in {where}: this Python (from the Microsoft Store) redirects new '
                             'AppData files where IDA does not look. Start IDA once so it makes its folder, or '
                             'run this script with a python.org Python')
    return {'path_file': pth, 'stub': os.path.join(loaders, STUB_NAME), 'removed': removed}


def redirected(path):
    """Where a file this process wrote really is when Windows redirected it (a Microsoft Store Python writing
    a new AppData folder), else None."""
    real = os.path.realpath(path)
    moved = os.path.normcase(real) != os.path.normcase(os.path.abspath(path))
    return real if moved and '\\localcache\\' in os.path.normcase(real) else None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--loaders', default=default_loaders(), help="IDA's loaders folder")
    parser.add_argument('--python', default=ida_python(), help="IDA's Python (python.exe)")
    parser.add_argument('--install', action='store_true', help='write the path file and the stub')
    args = parser.parse_args(argv)
    if not args.python:
        parser.error("no IDA Python found in the registry: pass --python")
    report = {'python': args.python}
    if args.install:
        if not args.loaders:
            parser.error('no loaders folder: pass --loaders')
        report['installed_now'] = install(args.loaders, args.python)
    report.update(check(args.loaders, imported_from(args.python)))
    print(json.dumps(report, indent=1))
    return {'current': 0, 'stale': 1, 'missing': 2}[report['state']]


if __name__ == '__main__':
    sys.exit(main())
