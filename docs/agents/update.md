# Updating ReAgentLive

Instructions an AI agent follows to update the ReAgentLive in a project on
the user's machine; humans: see the [README](../../README.md).

A project carries ReAgentLive as its `reagentlive/` submodule; an update
moves the submodule to a newer commit, rebuilds and checks.
`tools/update.ps1` does the work in three modes: a preview that only
reads, `-Apply` and `-Rollback`. Run every step yourself. Ask the user
only where a step says to. Use PowerShell. `<project>` is the project's
folder and `<sub>` the submodule in it (`<project>\reagentlive` unless the
project put it elsewhere); `<console>` is the project record's
`console`.

## 1. Find the project

The project is the folder you work in, or the one the user names. It holds
the submodule when `git -C <project> submodule status` lists `reagentlive`.
Each project updates on its own: for several, ask the user for their
folders and follow these steps for each.

- With `reagentlive.json` (a project [agent setup](setup.md) wired):
  follow every step.
- Without it: `update.ps1` moves the submodule but does not build; build
  with the project's own build (its notes win) and check the target's
  `status` only. [Agent setup](setup.md#4-wire-the-agent) writes the
  record for next time.
- No submodule (`update.ps1` prints `SKIP ... no reagentlive submodule at ...`):
  there is no project here. With
  `%LOCALAPPDATA%\reagentlive\setup.json` present, the user has an
  install from before projects: run [agent setup](setup.md) to make a
  project; its last step retires the old install.

## 2. Show what is coming

```powershell
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "<sub>\tools\update.ps1" -Project "<project>"
```

It checks the submodule, fetches and prints, changing nothing. Run by a
person at a terminal, it then asks whether to apply; under an agent
(`-NonInteractive`) it never asks:

| Line | Means | Do |
|---|---|---|
| `FAIL ... local changes` | the submodule holds edits; ReAgentLive is not edited in a project | show the user the list and ask; the changes belong in a clone of their own |
| `FAIL ... commits not on origin/main` | the submodule holds work of its own; an update would leave it on no branch | stop and ask |
| `FAIL ... on branch` | not `main` and not detached | tell the user; ask before switching |
| `PASS ... up to date` | nothing to update | say so and stop |
| `PASS ... <old> -> <new>`, then the commits and `changelog` lines | what is coming | summarise in a few bullets; keep `<old>` for a roll back |
| `NOTE ... protocol change` | every running target must be restarted and get the new DLL | step 6 |
| `NOTE ... the MCP server changed` | the client must start the server again | step 5 |
| `NOTE ... host headers changed` | the headers a host includes changed, so the project's host code may need the CHANGELOG's breaking changes | step 3 |

How the protocol-change flag is found: the CHANGELOG line
`Protocol change: rebuild and re-inject`
([the convention](../contributing.md#docs)) or a changed protocol header.
Show the user the summary and the `NOTE` lines, and ask before step 4.

## 3. Bring the host along

Only with a host-headers `NOTE`. The project's host code is the user's, so
an update never changes it. Show the user the CHANGELOG's breaking entries
from step 2, and, when the project started from an example, what changed
there (the `git diff` the `NOTE` names); say which changes the build needs
and which are optional, and on their yes make them in the project's files.
They build only against the new ReAgentLive: step 4 checks them.

## 4. Update

```powershell
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "<sub>\tools\update.ps1" -Project "<project>" -Apply
```

It checks again, moves the submodule to the new commit, rebuilds (with
`-Check` unless the record's `check` is false; the lines of
[agent setup](setup.md#5-build-and-check)), and
regenerates `build\console_api.py` when it exists. The project's
submodule pointer then shows as changed: commit it only when the user
asks. If the link step cannot write the DLL, a running process still has
it loaded: ask the user to close it, then run `-Apply` again.

**Roll back** on a `FAIL` here: report it (a compile error in the host's
code usually means step 3 left a needed change out) and ask
the user. On their yes, undo step 3's edits and:

```powershell
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "<sub>\tools\update.ps1" -Project "<project>" -Rollback <old>
```

then skip to step 8, and report `<old>` as the commit, and the failure.

## 5. Skill and MCP server

**Skill:** nothing to do: the links point into the submodule, and the
client reads the skill each session. A link missing (a fresh clone of the
project): `python -I "<sub>\tools\agent_link.py" --project "<project>"`.

**MCP server:** nothing to do in most updates: the server starts a console
for each call, so it uses the rebuilt console on the next call and sends
the client a changed tool list when the verb table changed. With step 2's
MCP `NOTE`, tell the user to restart the agent's session (Claude Code,
Codex) or reload its MCP servers (other clients).

## 6. Running targets

A process that loaded the old DLL still runs the old agent. Ask the user
to restart each such target (close it and start it again), then load the
new DLL into the fresh process (the injector from setup, if any:
[agent setup](setup.md#8-the-injector-once-per-machine)).

After a protocol change (step 2) the restart is required: the old agent's
shared section stays mapped with the old layout, so never load the new DLL
into a process that already runs the old one. Without one, the new DLL may
go into the same process (the newer agent takes over,
[protocol](../protocol.md#section)); a restart is still cleaner.

After a protocol change the new console answers an old agent with
`agent layout differs; build agent and client from the same source`: a
target still runs the old DLL; the update did not fail.

## 7. Verify in the target

Step 4's `PASS check` covers the build. Once the user runs a target with
the new DLL (step 6), read its `status` too:

```powershell
& "<project>\<console>" -c '{"verb": "status"}'
```

| Check | Pass when |
|---|---|
| Commit | `commit` is the new `git -C <sub> rev-parse --short=12 HEAD` (no `-dirty`); it is the console's commit |
| Agent | when the update changed the agent core, `build` (the agent core's build time, unless the host sets its own label) is from step 4: the target runs the new DLL. It stays the same when only the console or a pack changed |
| Verb table | `table_hash` differs from before when step 2 listed verb changes; the same hash after verb changes means the console was not rebuilt |

`replaced_agents` above 0 means the new DLL was loaded into a process
that already had an agent (step 6).

## 8. Lessons and the IDA side

**Lessons:** read the project's lessons against the updated skill: every
file in `<project>\.reagentlive\lessons\`. No folder: skip. Propose dropping
each lesson the skill now covers, one line each; the user decides. How the
files work:
[the improvement guide](../../.claude/skills/reagentlive/references/improving.md#lessons).

**IDA side:** only when the machine record's `ida` is `true`:

```powershell
python -I "<sub>\tools\ida_setup.py"
```

It only reads. `current`: IDA runs this project's code, so the update
needs nothing here, but an IDA session open since before step 4 keeps the
old code: tell the user to restart IDA (or run `rl.reload()` in it).
`stale` (an old loader, or IDA set up from another project): show the user
its `why` lines and, on their yes, run it again with `--install`.

## 9. Report

Tell the user:

```
┌─ ReAgentLive update complete ─────────────────────────┐
│  ✓ Commit    <old> → <new>                            │
│  ✓ Project   <project>                                │
│  ✓ Host      <changes made, or none needed>           │
│  ✓ Changes   <a few bullets from step 2>              │
│  ✓ Check     status ok, commit <new>                  │
│  ✓ IDA       <state, or "skipped">                    │
│                                                       │
│  ⚡ Still to do: <commit the submodule pointer,        │
│     restart the session, re-inject a target,          │
│     restart IDA, or "nothing">                        │
└───────────────────────────────────────────────────────┘
```
