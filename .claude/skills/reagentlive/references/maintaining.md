# Maintaining this skill

For an agent editing the skill. Where a session's lessons go: the
improvement reference.

## Skill vs docs

- **Skill = how to use it well:** method, order, judgement, approvals,
  pitfalls. **Docs = what it is:** verb semantics, protocol, design. One
  home per fact (`docs/contributing.md#where-each-fact-goes`). Never
  restate a verb's arguments or result keys: route to `help` with `verb`
  or its docs page.
- Repo files outside the skill are inline code paths with anchors
  (`docs/tiers.md#the-ladder`), never URLs: SKILL.md's resolver maps them
  to the user's build. Links between skill files stay relative.
- Never name a target (a game or app), mod or host (the `reagentlive.docs` test checks).
- A verb in prose stands alone (`find`), an arg by its key or path (`cls`,
  `view.limit`), never `--flag`. Examples are one-line JSON calls with
  real-looking values (`"0x7ff6a1b2c3d0"`, a literal ref, a class name),
  never `<placeholder>` where a value is typed; the `reagentlive.verb-names`
  and `reagentlive.json-examples` tests check them. MCP-only or Python-only
  syntax appears only in `interfaces.md`. Refs: a literal-looking ref
  (`0x1f2e3d4c50#123:4`) in a call, `<ref>` in prose, never `$N`.
- Agent-only style: triggers, rules, steps, checks. No summaries,
  motivation or beginner explanations.

## Structure

- SKILL.md orchestrates: `status` and the tier, rules, the load-when table.
  Stay within the docs-test budget; it links every file under
  `references/`.
- A new lesson goes to the matching reference, symptom or question first;
  SKILL.md only if every session needs it before its first command.
- A reference past about 100 lines splits by sub-topic, with a new
  load-when row. Unreal-only lessons live under `references/unreal/`.
- References name each other in plain text ("the static-tool reference"),
  never restate each other.
- Rewrite or delete stale lessons; no dated logs, no counts that rot.
