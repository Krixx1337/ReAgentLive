# Maintaining this skill

For an agent editing the skill. Where a session's lessons go: the
improvement reference.

## Skill vs docs

- **Skill = how to use it well:** method, order, judgement, approvals,
  pitfalls. **Docs = what it is and how it works:** verb semantics, guards,
  protocol, hosting, design. One home per fact: the "where each fact goes"
  map in `docs/contributing.md#where-each-fact-goes`.
- Never restate a verb's arguments, result keys or behaviour: route to
  `help` with `verb` or its docs page. A worked example call naming an arg
  (`{"verb": "find", "args": {"cls": "Pawn"}}`) is allowed; a result key is
  not.
- The skill carries only what an agent needs before its first command
  (the status rule, approval rules) and what no build changes.
- Repo files outside the skill (`docs/`, `src/`, `examples/`) are inline
  code paths, anchors kept (`docs/tiers.md#the-ladder`), never URLs:
  SKILL.md's resolver maps them to the user's build. Links between skill
  files stay relative.
- A verb in prose stands alone (`find`), an arg by its key or path (`cls`,
  `view.limit`), never as `--flag`. Examples are one-line JSON calls with
  real-looking values (`"0x7ff6a1b2c3d0"`, a literal ref, a class name),
  never `<placeholder>` where a value is typed, checked by the
  `reagentlive.verb-names` and `reagentlive.json-examples` tests. MCP-only or Python-only
  syntax appears only in `interfaces.md` and `console-driving.md`. Refs: a
  literal-looking ref (`0x1f2e3d4c50#123:4`) in a call, `<ref>` in prose, never `$N`.
- Agent-only style: triggers, rules, steps, checks. No summaries,
  motivation or beginner explanations.

## Structure

- SKILL.md is an orchestrator: `status` and the tier, rules, the
  load-when table. About 75 lines (the docs test fails past 110); it links
  every file under `references/`.
- A new lesson goes to the matching reference, symptom or question first;
  SKILL.md only if every session needs it before its first command.
- A reference past about 100 lines splits by sub-topic, with a new
  load-when row. A lesson that fits no reference starts a new one only
  with three or more such lessons.
- References name each other in plain text ("the static-tool reference"),
  never restate each other; SKILL.md's table routes.
- Scope: a lesson that holds in another game goes here, worded without the
  game (Unreal-only under `references/unreal/`); one host's console: that
  project's host notes; a fact about the game: the project's findings.
  Never name a game, mod or host (the `reagentlive.docs` test checks).
- Rewrite or delete stale lessons; no dated logs, no counts that rot.
