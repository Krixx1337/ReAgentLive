# Improving this skill

The loop that turns a session's friction into a lesson, an issue or a fix.
Where each kind of lesson belongs: the maintaining reference.

## Mode

Read `improvement` from the setup record
(`%LOCALAPPDATA%\reagentlive\setup.json`). No record or no key: `user`.

| Mode | May change | During the task | After the task | Never |
|---|---|---|---|---|
| `off` | nothing | nothing | nothing | anything |
| `user` (default) | only `references/local-lessons.md` (git-ignored) | scratch notes on friction | up to 3 proposals in plain words, one yes each; everything that is not a local lesson (docs or code problems, missing verbs, host notes, findings) is told to the user as a suggestion or an issue text, never written | any other file: ReAgentLive code, docs, tests, build files, tracked skill files, host notes, the project's findings |
| `dev` | anything: skill, docs, ReAgentLive code, tests, backlog | scratch notes | proposals as diffs or a plan; code changes on a branch with the build and tests run, diff shown | committing or pushing unless asked; silently changing approval or safety rules |

- `off`: skip this file.
- A plugin from a remote marketplace (this skill folder lies under
  `~/.claude/plugins/cache`, a cached copy) is never edited: local lessons
  then live at `%LOCALAPPDATA%\reagentlive\local-lessons.md`. Every other
  install (a link, a local plugin, a copy) keeps them in this folder.
- The user changes the mode with one line: `improvement` in the setup record.

## During the task: the scratch file

- Path: `%LOCALAPPDATA%\reagentlive\sessions\<session>.md`, `<session>`
  being the id `stats` reports for this console session.
- Create it at the first event, never before. Nothing happened: no file.
- One line per event, under these sections, in this order:

```markdown
# <session>
## Goal
## Friction
## Wrong turns
## What worked
## Candidates
```

- **Goal:** one line, the user's question.
- **Friction:** one line per event, with its evidence:
  - a refusal: verb and error kind (`refused` mark);
  - the same input sent twice (`repeated` mark);
  - output printed whole where a filter existed (`unfiltered` mark);
  - a `digest` that stood in for a result (`digested` in `stats`);
  - a result about the wrong object: the command and what came back;
  - the user corrected you: their words, short;
  - an approval round the skill could have avoided: the command.
- **Wrong turns:** a path dropped after more than two calls: the commands
  and why it failed.
- **What worked:** a sequence that answered in fewer calls than expected:
  the commands.
- **Candidates:** each `scope | target file | proposed text | evidence`.
- Write the line and go back to the task. Never edit any skill, doc or
  code file mid-task.

## After the task: the review

Run it only when the task ran 10 or more calls, or any mark or refusal
appeared. Otherwise delete the scratch file, if any, and stop. Budget: one
`stats` call and at most 10 lines of review.

1. `stats` for the session (`{"verb": "stats", "args": {"session": "<id>"}}`
   when the session has ended, e.g. after an MCP restart); copy its marks
   into Friction.
2. Turn Friction and Wrong turns into candidates. Apply the rubric to each;
   drop any that fails one question:
   - Would it have saved a step, a refusal or an approval round?
   - Is it general: no game, host, address, object name or offset?
   - Is it new: search the skill (and `local-lessons.md`) first?
   - Is there evidence from this session: a mark or a concrete event?
3. Route each survivor:

| Candidate | `user` | `dev` |
|---|---|---|
| skill method or pitfall | a local lesson | an edit to the tracked skill |
| docs wrong or missing | an issue text for the user | the docs fix |
| missing verb or arg, console bug | an issue text for the user | the fix with tests, or a `docs/ideas.md` entry |
| one host's console | a proposed text for the host notes; the user applies it | the host notes (ask) |
| a fact about the game | a proposed finding; the user records it | the project's findings (ask) |
| none of these | drop | drop |

4. Propose at most 3, the strongest first. Each: one sentence, the exact
   text, the target file. `dev`: show the diff or plan.
5. Write only the items the user says yes to, one yes per item. `user`
   writes nothing but `local-lessons.md`: every other item is text handed
   to the user in chat. Never post an issue.
6. `dev` code changes: on a branch, build and run the tests, show the
   diff. Never commit or push unless asked.
7. Delete the scratch file. `dev`: keep it until the PR is written.

## Local lessons

`references/local-lessons.md` in this skill folder (a remote plugin's
cached copy: `%LOCALAPPDATA%\reagentlive\local-lessons.md`), git-ignored:
the user's own, `git pull` never touches it, and setup and updating carry
it over when they replace a copied skill. Read it if present.

```markdown
# Local lessons
- <rule, one line> (session <id>; when: <trigger>)
```

- One line per lesson: the rule, the session it came from, the trigger it
  applies to (a verb, a refusal, a task).
- At most about 40 lines. When full, merge or drop the weakest before
  adding.
- The same lesson in a second session: add the second id, then offer it
  upstream as issue or PR text. The user decides; `dev` makes it a real
  change.
- Prune a lesson once it is promoted upstream, or once an updated skill
  covers it (`docs/agents/update.md` asks for this).

## Safeguards

- Never edit during the task; proposals come only after it.
- Approval, evidence, safety and scope rules are never changed by the
  loop. A candidate touching one is proposed with a warning that it
  changes a safety rule, and changed only if the user explicitly says so.
- Evidence or nothing: no "I could have been faster" reflections.
- Small tasks (under 10 calls, no marks) skip the review.
- `user` never writes any file but `local-lessons.md`; host notes and
  findings are suggestions the user applies.
- `dev` never commits or pushes unasked.
- Upstream edits keep every skill rule: no game, mod or host names, the
  size budgets, the docs test (`python -I tests/python/test_docs.py`).
