"""The docs rules (docs/contributing.md#docs): links resolve, INDEX lists every page, no host names,
every listed page opens with its header, and the skill's SKILL.md stays short and reaches every reference.

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
HEADER_LINES = 15
SKILL_MAX_LINES = 110
SKILL = os.path.join('.claude', 'skills', 'reagentlive')
GENERATED = {'docs/commands/results.md'}
ORCHESTRATORS = {'README.md', 'docs/INDEX.md', 'docs/agent-setup.md'}
REPO_URL = 'https://github.com/Krixx1337/ReAgentLive/blob/main/'  # the skill links repo files by URL


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
                local = target.startswith(REPO_URL)
                if re.match(r'[a-z]+:', target) and not local:
                    continue
                name, _, frag = target[len(REPO_URL):].partition('#') if local else target.partition('#')
                base = root if local else os.path.dirname(path)
                dest = os.path.normpath(os.path.join(base, name)) if name else path
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


def listed_pages(root):
    """docs/INDEX.md and every Markdown page it links, as root-relative paths."""
    docs = os.path.join(root, 'docs')
    index = os.path.join(docs, 'INDEX.md')
    if not os.path.exists(index):
        return []
    with open(index, encoding='utf-8') as file:
        targets = {t.partition('#')[0] for t in LINK.findall(file.read())}
    pages = {'docs/INDEX.md'}
    for target in targets:
        if target.endswith('.md') and not re.match(r'[a-z]+:', target):
            path = os.path.normpath(os.path.join(docs, target))
            if os.path.exists(path):
                pages.add(os.path.relpath(path, root).replace('\\', '/'))
    return sorted(pages)


def is_orchestrator(page):
    return page in ORCHESTRATORS or re.fullmatch(r'examples/[^/]+/README\.md', page) is not None


def check_headers(root):
    """Every listed page has **TL;DR:** in its first lines; SSOT pages also *Home of:* (contributing.md#docs)."""
    problems = []
    for page in listed_pages(root):
        if page in GENERATED or page.startswith('.claude/'):
            continue  # generated pages and skill files carry no header
        with open(os.path.join(root, page), encoding='utf-8') as file:
            head = file.read().split('\n')[:HEADER_LINES]
        if not any(line.startswith('**TL;DR:**') for line in head):
            problems.append(f'{page}: no **TL;DR:** in its first {HEADER_LINES} lines')
        if not is_orchestrator(page) and not any(line.startswith('*Home of:*') for line in head):
            problems.append(f'{page}: no *Home of:* line in its first {HEADER_LINES} lines')
    return problems


def check_skill(root):
    """SKILL.md stays an orchestrator: at most SKILL_MAX_LINES lines, linking every reference file."""
    skill = os.path.join(root, SKILL)
    main = os.path.join(skill, 'SKILL.md')
    if not os.path.exists(main):
        return []
    with open(main, encoding='utf-8') as file:
        text = file.read()
    problems = []
    lines = len(text.rstrip('\n').split('\n'))
    if lines > SKILL_MAX_LINES:
        problems.append(f'SKILL.md: {lines} lines, over {SKILL_MAX_LINES}')
    linked = {os.path.normpath(os.path.join(skill, t.partition('#')[0]))
              for _, line in prose(text) for t in LINK.findall(line)}
    for path in files(os.path.join(skill, 'references'), ('.md',)):
        if os.path.normpath(path) not in linked:
            problems.append(f'{os.path.relpath(path, skill)}: not linked from SKILL.md'.replace('\\', '/'))
    return problems


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

    def test_every_listed_page_has_its_header(self):
        self.assertEqual(check_headers(ROOT), [])

    def test_the_skill_stays_short_and_reaches_every_reference(self):
        self.assertEqual(check_skill(ROOT), [])

    def test_a_missing_file_or_anchor_fails(self):
        with Tree({'docs/a.md': '# A\n\n## Two words\n\n[ok](#two-words) [bad](#nope) [gone](b.md)\n'}) as root:
            self.assertEqual(len(check_links(root)), 2)

    def test_repo_urls_are_checked_as_local_files(self):
        url = REPO_URL + 'docs/a.md'
        with Tree({'docs/a.md': '# A\n\n## Two words\n',
                   'skill/s.md': f'[ok]({url}#two-words) [bad]({url}#nope) [gone]({REPO_URL}docs/b.md)\n'}) as root:
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

    def test_a_page_without_a_tldr_or_home_fails(self):
        index = '**TL;DR:** routes.\n\n[a](a.md) [b](b.md) [r](commands/results.md) [s](agent-setup.md)\n'
        with Tree({'docs/INDEX.md': index,
                   'docs/a.md': '# A\n\n*Home of:* a.\n\n**TL;DR:** fine.\n',
                   'docs/b.md': '# B\n\nNo header.\n',
                   'docs/commands/results.md': '# Generated\n',
                   'docs/agent-setup.md': '# Setup\n\n**TL;DR:** steps.\n'}) as root:
            self.assertEqual(check_headers(root), ['docs/b.md: no **TL;DR:** in its first 15 lines',
                                                   'docs/b.md: no *Home of:* line in its first 15 lines'])

    def test_a_tldr_below_the_header_lines_fails(self):
        late = '# A\n\n*Home of:* a.\n' + '\n' * 20 + '**TL;DR:** late.\n'
        with Tree({'docs/INDEX.md': '**TL;DR:** x.\n[a](a.md)\n', 'docs/a.md': late}) as root:
            self.assertEqual(check_headers(root), ['docs/a.md: no **TL;DR:** in its first 15 lines'])

    def test_the_index_itself_needs_a_tldr(self):
        with Tree({'docs/INDEX.md': '# Index\n'}) as root:
            self.assertEqual(check_headers(root), ['docs/INDEX.md: no **TL;DR:** in its first 15 lines'])

    def test_orchestrators_need_no_home_line(self):
        index = '**TL;DR:** x.\n[r](../README.md) [e](../examples/one/README.md) [s](../.claude/s/SKILL.md)\n'
        with Tree({'docs/INDEX.md': index, 'README.md': '# R\n\n**TL;DR:** pitch.\n', '.claude/s/SKILL.md': 'x\n',
                   'examples/one/README.md': '# E\n\n**TL;DR:** build.\n'}) as root:
            self.assertEqual(check_headers(root), [])

    def test_a_long_skill_or_an_unlinked_reference_fails(self):
        skill = '.claude/skills/reagentlive/'
        refs = {skill + 'references/w.md': 'w\n', skill + 'references/unreal/n.md': 'n\n'}
        body = '[w](references/w.md) [n](references/unreal/n.md#top)\n'
        with Tree({skill + 'SKILL.md': body, **refs}) as root:
            self.assertEqual(check_skill(root), [])
        with Tree({skill + 'SKILL.md': '[w](references/w.md)\n' + 'x\n' * 110, **refs}) as root:
            self.assertEqual(check_skill(root), ['SKILL.md: 111 lines, over 110',
                                                 'references/unreal/n.md: not linked from SKILL.md'])


if __name__ == '__main__':
    args = sys.argv[1:]
    while args[:1] == ['--forbid']:
        FORBID.append(args[1])
        args = args[2:]
    unittest.main(argv=[sys.argv[0], *args])
