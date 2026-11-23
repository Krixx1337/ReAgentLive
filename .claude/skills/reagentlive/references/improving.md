# Improving this skill

The loop that turns a session's friction into a lesson, an issue or a fix,
and where each kind of lesson goes. Editing the skill itself: the
maintaining reference.

## Mode

Read `improvement` from the setup record
(`%LOCALAPPDATA%\reagentlive\setup.json`). No record or no key: `user`.

| Mode | May change | During the task | After the task | Never |
|---|---|---|---|---|
| `off` | nothing | nothing | nothing | anything |
| `user` (default) | only `references/local-lessons.md` (git-ignored) | scratch notes on friction | up to 3 proposals in plain words, one yes each; everything that is not a local lesson (docs or code problems, missing verbs, host notes, findings) is told to the user as a suggestion or an issue text, never written | any other file: ReAgentLive code, docs, tests, build files, tracked skill files, host notes, the project's findings |
| `dev` | anything: skill, docs, ReAgentLive code, tests, backlog | scratch notes | proposals as diffs or a plan; code changes on a branch with the build and tests run, diff shown | committing or pushing unless asked; silently changing approval or safety rules |

`off`: skip this file. The user changes the mode with one line:
`improvement` in the setup record.

## During the task: the scratch file

- Path: `%LOCALAPPDATA%\reagentlive\sessions\<session>.md`, `<session>`
  being the id `usage` reports for this console session.
- Create it at the first event, never before. One line per event:

```markdown
# <session>
## Goal
## Friction
## Wrong turns
## What worked
## Candidates
```

- **Friction**, one line per event with its evidence: a refusal (verb,
  kind), a repeated or unfiltered call, a `digest`, a result about the
  wrong object, the user correcting you (their words), an approval round
  the skill could have avoided.
- **Wrong turns:** a path dropped after more than two calls, and why.
- **What worked:** a sequence that answered in fewer calls than expected.
- **Candidates:** each `scope | target file | proposed text | evidence`.
- Write the line and go back to the task. Never edit any skill, doc or
  code file mid-task.

## After the task: the review

Run it only when the task ran 10 or more calls, or any mark or refusal
appeared. Otherwise delete the scratch file, if any, and stop. Budget: one
`usage` call and at most 10 lines of review.

1. `usage` for the session with `advice`
   (`{"verb": "usage", "args": {"session": "<id>", "advice": true}}`; drop
   `session` while it runs); copy its marks, its `advice` rows and its
   `refusals` (verb, kind, arg: the help texts that misled) into Friction.
2. Turn Friction and Wrong turns into candidates. Apply the rubric to each;
   drop any that fails one question:
   - Would it have saved a step, a refusal or an approval round?
   - Is it general: no game, host, address, object name or offset?
   - Is it new: search the skill (and `local-lessons.md`) first?
   - Is there evidence from this session: a mark or a concrete event?
   - Does it leave approval, evidence, safety and scope rules alone? One
     that changes them is proposed with that warning and changed only on
     the user's explicit yes.
3. Route each survivor:

| Candidate | `user` | `dev` |
|---|---|---|
| skill method or pitfall | a local lesson | an edit to the tracked skill |
| docs wrong or missing | an issue text for the user | the docs fix |
| missing verb or arg, console bug | an issue text for the user | the fix with tests, or a `docs/ideas.md` entry |
| one host's console | a proposed text for the host notes; the user applies it | the host notes (ask) |
| a fact about the game | a proposed finding; the user records it | the project's findings (ask) |
| none of these | drop | drop |

In the skill, a lesson goes to the reference matching its symptom or
question (Unreal-only ones under `references/unreal/`), worded without any
game, mod or host.

4. Propose at most 3, the strongest first. Each: one sentence, the exact
   text, the target file. `dev`: show the diff or plan.
5. Write only the items the user says yes to, one yes per item. `user`
   writes nothing but `local-lessons.md`; never post an issue. `dev` code
   changes: on a branch, build and run the tests, show the diff; never
   commit or push unless asked.
6. Delete the scratch file (`dev`: once the PR is written).

## Local lessons

`references/local-lessons.md` in this skill folder, git-ignored:
the user's own, `git pull` never touches it, and setup and updating carry
it over when they replace a copied skill. Read it if present.

```markdown
# Local lessons
- <rule, one line> (session <id>; when: <trigger>)
```

- One line per lesson; at most about 40 lines (when full, merge or drop
  the weakest first).
- The same lesson in a second session: add the second id and offer it
  upstream as issue or PR text; the user decides.
- Prune a lesson once it is promoted upstream or an updated skill covers
  it.
