"""The docs rules (docs/contributing.md#docs): links resolve, every docs page is reachable from docs/README.md through folder READMEs, no host names,
every listed page opens with its header, and the skill's SKILL.md stays short and reaches every reference.
AGENTS.md (for an agent changing the repo; CLAUDE.md imports it) stays within its line budget and needs no
header; links and names are checked there as everywhere. The skill names repo files as inline code paths (`docs/tiers.md#the-ladder`), resolved to the user's build:
every such path must exist, and no skill file links GitHub's main branch. Every example carries the library's
CMakePresets.json unchanged, so a copied example builds as the library does.

Usage: test_docs.py [--forbid WORD ...]. A host passes its own and its
game's names, so the library never has to spell them.
"""
import os
import re
import sys
import tempfile
import unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SKIP_DIRS = {'__pycache__', '.git', 'build', 'out'}  # build trees are git-ignored
TEXT = ('.md', '.h', '.cpp', '.py', '.txt', '.cmake')
LINK = re.compile(r'(?<!!)\[[^\]]*\]\(([^)\s]+)\)')
HEADING = re.compile(r'^(#{1,6})\s+(.*?)\s*#*\s*$')
EXPLICIT = re.compile(r'<a id="([^"]+)"')
FENCE = re.compile(r'^\s*(```|~~~)')

FORBID = []
HEADER_LINES = 15
SKILL_MAX_LINES = 110
AGENTS_MAX_LINES = 120
AGENT_FILES = {'AGENTS.md', 'CLAUDE.md'}  # for an agent changing the repo: no TL;DR or Home of line
SKILL = os.path.join('.claude', 'skills', 'reagentlive')
GENERATED = {'docs/commands/results.md', 'docs/commands/reference.md'}
AGENT_PROCEDURES = 'docs/agents/'  # pages an agent runs: no TL;DR or Home of line
CODE = re.compile(r'`([^`]+)`')
CODE_PATH = re.compile(r'(docs|src|examples|tests|cmake)/[\w./#-]+')
MAIN_URL = re.compile(r'github\.com/\S*?/blob/main\b')


def files(root, suffixes):
    for base, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in sorted(names):
            path = os.path.join(base, name)
            if name.endswith(suffixes):
                yield path


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


def reachable(root):
    """docs/README.md, every Markdown page it links, and through each linked README.md under docs/
    (a folder's index), every page that one links: normalized absolute paths."""
    docs = os.path.normpath(os.path.join(root, 'docs'))
    index = os.path.join(docs, 'README.md')
    if not os.path.exists(index):
        return set()
    seen, queue = {index}, [index]
    while queue:
        page = queue.pop()
        with open(page, encoding='utf-8') as file:
            targets = {t.partition('#')[0] for t in LINK.findall(file.read())}
        for target in targets:
            if not target.endswith('.md') or re.match(r'[a-z]+:', target):
                continue
            path = os.path.normpath(os.path.join(os.path.dirname(page), target))
            if path in seen or not os.path.exists(path):
                continue
            seen.add(path)
            if os.path.basename(path) == 'README.md' and path.startswith(docs + os.sep):
                queue.append(path)
    return seen


def check_index(root):
    """Every page under docs/ is reachable from docs/README.md through folder READMEs."""
    if not os.path.exists(os.path.join(root, 'docs', 'README.md')):
        return ['docs/README.md is missing']
    listed = reachable(root)
    return [f'{os.path.relpath(p, root)}: not reachable from docs/README.md'.replace('\\', '/')
            for p in files(os.path.join(root, 'docs'), ('.md',)) if os.path.normpath(p) not in listed]


def listed_pages(root):
    """The pages reachable from docs/README.md, as root-relative paths."""
    return sorted(os.path.relpath(p, root).replace('\\', '/') for p in reachable(root))


def is_orchestrator(page):
    """A README.md is its folder's index: it routes and needs no Home of line."""
    return os.path.basename(page) == 'README.md'


def check_headers(root):
    """Every listed page has **TL;DR:** in its first lines; SSOT pages also *Home of:* (contributing.md#docs).
    Agent procedures under docs/agents/ are exempt; links, the index and names still apply to them."""
    problems = []
    for page in listed_pages(root):
        if page in GENERATED or page in AGENT_FILES or page.startswith(('.claude/', AGENT_PROCEDURES)):
            continue  # generated pages, skill files, agent procedures and AGENTS.md carry no header
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


def check_agents(root):
    """AGENTS.md stays an orchestrator: at most AGENTS_MAX_LINES lines."""
    path = os.path.join(root, 'AGENTS.md')
    if not os.path.exists(path):
        return []
    with open(path, encoding='utf-8') as file:
        lines = len(file.read().rstrip('\n').split('\n'))
    return [f'AGENTS.md: {lines} lines, over {AGENTS_MAX_LINES}'] if lines > AGENTS_MAX_LINES else []


def check_code_paths(root):
    """Every inline code path in the skill (`docs/...`, `src/...`) exists, with its anchor in a .md file."""
    problems, cache = [], {}
    for path in files(os.path.join(root, SKILL), ('.md',)):
        with open(path, encoding='utf-8') as file:
            text = file.read()
        where = os.path.relpath(path, root).replace('\\', '/')
        for number, line in prose(text):
            for code in CODE.findall(line):
                if not CODE_PATH.fullmatch(code) or '...' in code:
                    continue  # not a repo path, or a placeholder
                name, _, frag = code.partition('#')
                dest = os.path.normpath(os.path.join(root, name))
                if not os.path.exists(dest):
                    problems.append(f'{where}:{number}: {code}: no such file')
                    continue
                if frag and dest.endswith('.md'):
                    if dest not in cache:
                        cache[dest] = anchors(dest)
                    if frag not in cache[dest]:
                        problems.append(f'{where}:{number}: {code}: no such anchor')
    return problems


def check_main_urls(root):
    """No skill file links GitHub's main branch: it can run ahead of the user's build."""
    problems = []
    for path in files(os.path.join(root, SKILL), ('.md',)):
        with open(path, encoding='utf-8') as file:
            for number, line in enumerate(file, 1):
                if MAIN_URL.search(line):
                    where = os.path.relpath(path, root).replace('\\', '/')
                    problems.append(f'{where}:{number}: links blob/main; use a repo path')
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

    def test_agents_md_stays_short(self):
        self.assertEqual(check_agents(ROOT), [])

    def test_the_skills_repo_paths_exist(self):
        self.assertEqual(check_code_paths(ROOT), [])

    def test_the_skill_never_links_main(self):
        self.assertEqual(check_main_urls(ROOT), [])

    def test_every_example_carries_the_librarys_presets(self):
        # A copied example builds as the library does: its CMakePresets.json is the root one, byte for byte.
        def presets(folder):
            with open(os.path.join(folder, 'CMakePresets.json'), encoding='utf-8') as handle:
                return handle.read().replace('\r\n', '\n')
        examples = os.path.join(ROOT, 'examples')
        for name in sorted(os.listdir(examples)):
            if os.path.isfile(os.path.join(examples, name, 'CMakeLists.txt')):
                self.assertEqual(presets(os.path.join(examples, name)), presets(ROOT), name)

    def test_a_missing_file_or_anchor_fails(self):
        with Tree({'docs/a.md': '# A\n\n## Two words\n\n[ok](#two-words) [bad](#nope) [gone](b.md)\n'}) as root:
            self.assertEqual(len(check_links(root)), 2)

    def test_a_missing_code_path_or_anchor_fails(self):
        skill = '.claude/skills/reagentlive/'
        body = ('`docs/a.md#two-words` `src/x.h` `examples/one` `docs/a.md#nope` `src/gone.h`\n'
                '`docs/...` `docs/<page>` `status` `--every` `src/x.h#not-checked`\n'
                '```\n`docs/fenced.md`\n```\n')
        with Tree({'docs/a.md': '# A\n\n## Two words\n', 'src/x.h': '', 'examples/one/README.md': '',
                   skill + 'references/r.md': body}) as root:
            self.assertEqual(check_code_paths(root), [
                '.claude/skills/reagentlive/references/r.md:1: docs/a.md#nope: no such anchor',
                '.claude/skills/reagentlive/references/r.md:1: src/gone.h: no such file'])

    def test_a_main_branch_url_in_the_skill_fails(self):
        skill = '.claude/skills/reagentlive/'
        main = 'https://github.com/Owner/Repo/blob/main/docs/a.md'
        with Tree({skill + 'SKILL.md': f'[a]({main})\n`https://github.com/Owner/Repo/blob/<commit>/<path>`\n',
                   skill + 'references/r.md': f'see `{main}#x`\n',
                   'docs/a.md': f'[a]({main})\n'}) as root:
            self.assertEqual(check_main_urls(root), [
                '.claude/skills/reagentlive/SKILL.md:1: links blob/main; use a repo path',
                '.claude/skills/reagentlive/references/r.md:1: links blob/main; use a repo path'])
            self.assertEqual(check_links(root), [])  # an absolute URL is not a local link

    def test_slugs_match_github(self):
        self.assertEqual(slug('17 · `call-native` (caution)'), '17--call-native-caution')
        self.assertEqual(slug('Labels for a static tool'), 'labels-for-a-static-tool')

    def test_links_in_code_are_ignored(self):
        with Tree({'docs/a.md': '# A\n\n```text\n[x](missing.md)\n```\n\n`[y](missing.md)`\n'}) as root:
            self.assertEqual(check_links(root), [])

    def test_an_unlisted_page_fails(self):
        with Tree({'docs/README.md': '[a](a.md)\n', 'docs/a.md': '# A\n', 'docs/b.md': '# B\n'}) as root:
            self.assertEqual(len(check_index(root)), 1)

    def test_a_page_listed_by_a_folder_readme_is_reachable(self):
        tree = {'docs/README.md': '**TL;DR:** x.\n[c](commands/README.md)\n',
                'docs/commands/README.md': '# C\n\n**TL;DR:** routes.\n\n[m](memory.md)\n',
                'docs/commands/memory.md': '# M\n\n*Home of:* m.\n\n**TL;DR:** fine.\n'}
        with Tree(tree) as root:
            self.assertEqual(check_index(root), [])
            self.assertEqual(check_headers(root), [])  # a folder README needs no Home of line
        with Tree({**tree, 'docs/commands/memory.md': '# M\n'}) as root:
            self.assertEqual(len(check_headers(root)), 2)  # pages behind a folder README keep their header

    def test_a_forbidden_name_fails_in_any_case(self):
        with Tree({'core/x.h': '// for SomeGame only\n'}) as root:
            self.assertEqual(len(check_names(root, ['somegame'])), 1)

    def test_a_page_without_a_tldr_or_home_fails(self):
        index = '**TL;DR:** routes.\n\n[a](a.md) [b](b.md) [r](commands/results.md)\n'
        with Tree({'docs/README.md': index,
                   'docs/a.md': '# A\n\n*Home of:* a.\n\n**TL;DR:** fine.\n',
                   'docs/b.md': '# B\n\nNo header.\n',
                   'docs/commands/results.md': '# Generated\n'}) as root:
            self.assertEqual(check_headers(root), ['docs/b.md: no **TL;DR:** in its first 15 lines',
                                                   'docs/b.md: no *Home of:* line in its first 15 lines'])

    def test_agent_procedures_need_no_header_but_keep_the_other_checks(self):
        index = '**TL;DR:** x.\n[r](agents/README.md) [s](agents/setup.md)\n'
        with Tree({'docs/README.md': index,
                   'docs/agents/README.md': '# Agents\n\nFor agents. [s](setup.md)\n',
                   'docs/agents/setup.md': '# Setup\n\nFor agents. [gone](missing.md) SomeGame\n',
                   'docs/agents/update.md': '# Update\n\nUnlisted.\n'}) as root:
            self.assertEqual(check_headers(root), [])
            self.assertEqual(check_links(root), ['docs/agents/setup.md:3: missing.md: no such file'])
            self.assertEqual(check_index(root), ['docs/agents/update.md: not reachable from docs/README.md'])
            self.assertEqual(len(check_names(root, ['somegame'])), 1)

    def test_a_tldr_below_the_header_lines_fails(self):
        late = '# A\n\n*Home of:* a.\n' + '\n' * 20 + '**TL;DR:** late.\n'
        with Tree({'docs/README.md': '**TL;DR:** x.\n[a](a.md)\n', 'docs/a.md': late}) as root:
            self.assertEqual(check_headers(root), ['docs/a.md: no **TL;DR:** in its first 15 lines'])

    def test_the_index_itself_needs_a_tldr(self):
        with Tree({'docs/README.md': '# Index\n'}) as root:
            self.assertEqual(check_headers(root), ['docs/README.md: no **TL;DR:** in its first 15 lines'])

    def test_orchestrators_need_no_home_line(self):
        index = '**TL;DR:** x.\n[r](../README.md) [e](../examples/one/README.md) [s](../.claude/s/SKILL.md)\n'
        with Tree({'docs/README.md': index, 'README.md': '# R\n\n**TL;DR:** pitch.\n', '.claude/s/SKILL.md': 'x\n',
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

    def test_a_long_agents_md_fails(self):
        with Tree({'AGENTS.md': 'x\n' * 120}) as root:
            self.assertEqual(check_agents(root), [])
        with Tree({'AGENTS.md': 'x\n' * 121}) as root:
            self.assertEqual(check_agents(root), ['AGENTS.md: 121 lines, over 120'])
        with Tree({'docs/a.md': '# A\n'}) as root:
            self.assertEqual(check_agents(root), [])  # no AGENTS.md, nothing to check

    def test_agent_files_need_no_header_but_keep_links_and_names(self):
        index = '**TL;DR:** x.\n[a](../AGENTS.md) [c](../CLAUDE.md)\n'
        with Tree({'docs/README.md': index,
                   'AGENTS.md': '# Working on it\n\n[ok](docs/README.md) [gone](docs/missing.md) SomeGame\n',
                   'CLAUDE.md': '@AGENTS.md\n[bad](nope.md) somegame\n'}) as root:
            self.assertEqual(check_headers(root), [])
            self.assertEqual(check_links(root), ['AGENTS.md:3: docs/missing.md: no such file',
                                                 'CLAUDE.md:2: nope.md: no such file'])
            self.assertEqual(check_names(root, ['somegame']), ['AGENTS.md:3: names the host or game',
                                                               'CLAUDE.md:2: names the host or game'])


if __name__ == '__main__':
    args = sys.argv[1:]
    while args[:1] == ['--forbid']:
        FORBID.append(args[1])
        args = args[2:]
    unittest.main(argv=[sys.argv[0], *args])
