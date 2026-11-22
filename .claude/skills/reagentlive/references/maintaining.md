# Maintaining this skill

## Skill vs docs

- **Skill = how to use it well:** method, order, judgement, approvals,
  pitfalls. **Docs = what it is and how it works:** verb semantics, guards,
  protocol, hosting, design.
- Never restate a verb's arguments, flags, result keys or behaviour here:
  link `help <verb>` or its docs page. One home per fact; the registry is
  the "where each fact goes" map in
  [contributing](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/contributing.md).
- The skill ships apart from the docs (copied into an agent's skills
  folder): it carries only the minimum an agent needs before its first
  command (the operational status rule, approval rules) and links every
  other fact by URL; the docs own the full statements.
- Links to repo files outside the skill (`docs/`, `src/`, `examples/`) are
  absolute: `https://github.com/Krixx1337/ReAgentLive/blob/main/<path>`,
  anchors kept. Links between skill files stay relative.
- Facts the skill owns: the operational status rule and how to climb
  (SKILL.md; the full status-to-tier table is `docs/tiers.md`'s), approval
  and evidence rules (SKILL.md), which interface for which task and the
  verb-to-tool mapping (`interfaces.md`), method, pitfalls and
  per-question advice (the other references).
- Name verbs as the console writes them (`find`, `game-thread on`). MCP-only
  or Python-only syntax appears only in `interfaces.md` and
  `console-driving.md`. Pass refs as `<ref>`, never `$N`, elsewhere.
- Agent-only style: triggers, rules, steps, checks. No summaries,
  motivation or beginner explanations.

## Structure

- SKILL.md is an orchestrator: what it is, `status` and the tier, rules,
  interface pick, the load-when table, limits. At most ~100 lines (the docs
  test fails past 110). It links every file under `references/`.
- A new lesson goes to the matching reference, symptom or question first.
  It goes in SKILL.md only if every session needs it before its first
  command.
- A reference past about 150 lines splits by sub-topic, with a new
  load-when row.
- A lesson that fits no reference starts a new one only when there are
  three or more such lessons; until then it goes to the closest file.
- References do not link to each other; SKILL.md's table routes. Where a
  step continues in another reference (the new-game procedure across
  `unreal/new-game.md`, `layout-groups.md` and `game-thread.md`), name it
  in plain text ("the game-thread reference"), never restate it.
- Scope: a lesson that would hold in another game goes here, worded without
  the game (an Unreal-only one under `references/unreal/`). One about one
  host's console goes to that project's host notes. A fact about the game
  is a finding for the project's docs. This skill never names a game, mod
  or host; the `reagentlive.docs` test checks.
- Rewrite or delete stale lessons; no dated logs, no counts that rot.

## End of session

- Read `stats`; ask what would have saved time (`stats --all`,
  `stats --sessions`).
- Put each lesson in its home: this skill (method), the docs (behaviour),
  the host notes (one host), the project's docs (findings).

## When ReAgentLive hits a limit

- Add the missing verb or flag instead of working around it twice
  (`find --cls` and `find --live` came from real investigations):
  engine- and game-agnostic, tested, documented, and the backlog
  ([ideas](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/ideas.md)) re-ranked with the evidence.
- Code written twice goes into the console (a verb or a flag, so the
  generated functions and tools get it) with a test, not into the skill.
- Editing the C++: `near` and `far` are Windows macros, bad variable
  names; CRLF files need byte-level edits.
