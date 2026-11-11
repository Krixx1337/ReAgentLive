"""The docs rules (docs/contributing.md#docs): links resolve, INDEX lists every page, no host names.

Usage: test_docs.py [--forbid WORD ...]. A host passes its own and its
game's names, so the library never has to spell them.
"""
import os
import re
import sys
import tempfile
import unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SKIP_DIRS = {'third_party', '__pycache__', '.git'}
TEXT = ('.md', '.h', '.cpp', '.py', '.txt', '.cmake')
LINK = re.compile(r'(?<!!)\[[^\]]*\]\(([^)\s]+)\)')
HEADING = re.compile(r'^(#{1,6})\s+(.*?)\s*#*\s*$')
EXPLICIT = re.compile(r'<a id="([^"]+)"')
FENCE = re.compile(r'^\s*(```|~~~)')

FORBID = []


def files(root, suffixes):
    for base, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in sorted(names):
            if name.endswith(suffixes):
                yield os.path.join(base, name)


def prose(text):
    """Lines outside fenced code blocks, with their line numbers."""
    fenced = False
    for number, line in enumerate(text.split('\n'), 1):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if not fenced:
            yield number, line


def slug(heading):
    """GitHub's heading anchor."""
    text = re.sub(r'<[^>]+>', '', heading).replace('`', '').lower()
    text = re.sub(r'[^\w\- ]', '', text)
    return text.replace(' ', '-')


def anchors(path):
    with open(path, encoding='utf-8') as file:
        text = file.read()
    found, seen = set(EXPLICIT.findall(text)), {}
    for _, line in prose(text):
        match = HEADING.match(line)
        if match:
            base = slug(match.group(2))
            count = seen.get(base, 0)
            seen[base] = count + 1
            found.add(base if count == 0 else f'{base}-{count}')
    return found


def check_links(root):
    problems, cache = [], {}
    for path in files(root, ('.md',)):
        with open(path, encoding='utf-8') as file:
            text = file.read()
        where = os.path.relpath(path, root).replace('\\', '/')
        for number, line in prose(text):
            for target in LINK.findall(re.sub(r'`[^`]*`', '', line)):
                if re.match(r'[a-z]+:', target):
                    continue
                name, _, frag = target.partition('#')
                dest = os.path.normpath(os.path.join(os.path.dirname(path), name)) if name else path
                if not os.path.exists(dest):
                    problems.append(f'{where}:{number}: {target}: no such file')
                    continue
                if frag and dest.endswith('.md'):
                    if dest not in cache:
                        cache[dest] = anchors(dest)
                    if frag not in cache[dest]:
                        problems.append(f'{where}:{number}: {target}: no such anchor')
    return problems


def check_index(root):
    docs = os.path.join(root, 'docs')
    index = os.path.join(docs, 'INDEX.md')
    if not os.path.exists(index):
        return ['docs/INDEX.md is missing']
    with open(index, encoding='utf-8') as file:
        listed = {os.path.normpath(os.path.join(docs, t.partition('#')[0])) for t in LINK.findall(file.read())}
    return [f'{os.path.relpath(p, root)}: not listed in docs/INDEX.md'.replace('\\', '/')
            for p in files(docs, ('.md',)) if p != index and os.path.normpath(p) not in listed]


def check_names(root, words):
    problems = []
    if not words:
        return problems
    pattern = re.compile('|'.join(re.escape(w) for w in words), re.I)
    for path in files(root, TEXT):
        with open(path, encoding='utf-8', errors='replace') as file:
            for number, line in enumerate(file, 1):
                if pattern.search(line):
                    problems.append(f'{os.path.relpath(path, root)}:{number}: names the host or game'.replace('\\', '/'))
    return problems


class Tree:
    """A planted library tree."""

    def __init__(self, files):
        self.dir = tempfile.TemporaryDirectory()
        for path, text in files.items():
            full = os.path.join(self.dir.name, path)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, 'w', encoding='utf-8') as file:
                file.write(text)

    def __enter__(self):
        return self.dir.name

    def __exit__(self, *_):
        self.dir.cleanup()


class Docs(unittest.TestCase):
    def test_the_links_resolve(self):
        self.assertEqual(check_links(ROOT), [])

    def test_the_index_lists_every_page(self):
        self.assertEqual(check_index(ROOT), [])

    def test_no_host_or_game_names(self):
        self.assertEqual(check_names(ROOT, FORBID), [])

    def test_a_missing_file_or_anchor_fails(self):
        with Tree({'docs/a.md': '# A\n\n## Two words\n\n[ok](#two-words) [bad](#nope) [gone](b.md)\n'}) as root:
            self.assertEqual(len(check_links(root)), 2)

    def test_slugs_match_github(self):
        self.assertEqual(slug('17 · `call-native` (caution)'), '17--call-native-caution')
        self.assertEqual(slug('Labels for a static tool'), 'labels-for-a-static-tool')

    def test_links_in_code_are_ignored(self):
        with Tree({'docs/a.md': '# A\n\n```text\n[x](missing.md)\n```\n\n`[y](missing.md)`\n'}) as root:
            self.assertEqual(check_links(root), [])

    def test_an_unlisted_page_fails(self):
        with Tree({'docs/INDEX.md': '[a](a.md)\n', 'docs/a.md': '# A\n', 'docs/b.md': '# B\n'}) as root:
            self.assertEqual(len(check_index(root)), 1)

    def test_a_forbidden_name_fails_in_any_case(self):
        with Tree({'core/x.h': '// for SomeGame only\n'}) as root:
            self.assertEqual(len(check_names(root, ['somegame'])), 1)


if __name__ == '__main__':
    args = sys.argv[1:]
    while args[:1] == ['--forbid']:
        FORBID.append(args[1])
        args = args[2:]
    unittest.main(argv=[sys.argv[0], *args])
