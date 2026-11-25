# Updating ReAgentLive

Instructions an AI agent follows to update the ReAgentLive in a project on
the user's machine; humans: see the [README](../../README.md).

A project carries ReAgentLive as its `reagentlive/` submodule; an update
moves the submodule to a newer commit, rebuilds and checks. Run every step
yourself. Ask the user only where a step says to. Use PowerShell.
`<project>` is the project's folder and `<sub>` is `<project>\reagentlive`;
`<name>` and `<kit>` mean the same as in [agent setup](setup.md#2-ask-what-to-make).

## 1. Find the project

The project is the folder you work in, or the one the user names. It holds
the submodule when `git -C <project> submodule status` lists `reagentlive`.

- With `reagentlive.json` (a project made by [agent setup](setup.md)):
  read it ([its fields](setup.md#3-create-the-project)) and follow every
  step.
- Without it: a host project of its own (a mod that added the submodule).
  Follow the same steps, but build with the host's own build (its notes
  win), skip the kit's diff in step 4 and the `-Check` in step 9, and check
  the target's `status` only.
- No submodule: there is no project here. With
  `%LOCALAPPDATA%\reagentlive\setup.json` present, the user has an
  install from before projects: run [agent setup](setup.md) to make a
  project; its last step retires the old install.

## 2. Show what is coming

First check the submodule's state; never update blindly:

```powershell
git -C <sub> status --short
git -C <sub> branch --show-current
git -C <sub> fetch origin
git -C <sub> log --oneline origin/main..HEAD
```

- `status` prints a tracked file (local changes): stop, show the user the
  list and ask what to do with them: ReAgentLive is not edited in a
  project, so the changes belong in a clone of their own.
- `log` prints commits: the submodule holds work that is not on
  `origin/main`; stop and ask. An update would leave those commits on no
  branch.
- A detached HEAD is normal for a submodule. A branch other than `main`:
  tell the user and ask before switching.

Then:

```powershell
git -C <sub> rev-parse --short=12 HEAD
git -C <sub> log --oneline HEAD..origin/main
git -C <sub> diff HEAD origin/main -- CHANGELOG.md
```

- Keep the `rev-parse` output: it is the `<old>` commit for the later
  steps.
- `log` empty: nothing to update; say so and stop.
- Summarise the `log` and the CHANGELOG lines in a few bullets for the
  user.
- Flag a protocol change when either holds: a new CHANGELOG line carries
  `Protocol change: rebuild and re-inject`
  ([the convention](../contributing.md#docs)), or this prints any path:

  ```powershell
  git -C <sub> diff --name-only <old>..origin/main -- src/reagentlive/core/protocol.h "src/reagentlive/packs/*/protocol_*.h"
  ```

  Then every running target must be restarted and get the new DLL
  (step 8).

## 3. Move the submodule

```powershell
git -C <sub> checkout --detach origin/main
```

The project's submodule pointer now shows as changed: commit it only when
the user asks.

## 4. Bring the kit's changes over

The project's `CMakeLists.txt`, `host.cpp` and `console.cpp` are the
user's copies of the kit, so an update never changes them. Only with a
project record:

```powershell
git -C <sub> diff <old> HEAD -- examples/<kit>
```

Empty: go on. Otherwise show the user the diff, say which changes the
build needs (the host API changed) and which are optional, and on their
yes apply them to the project's files by hand, keeping the project's names
and the user's own changes.

## 5. Rebuild

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "<sub>\tools\build_project.ps1" -Project "<project>"
```

It reads as in [agent setup](setup.md#4-build-and-check) and stamps the
submodule's commit into the console (`status` shows it in step 9). If the
link step cannot write the DLL, a running process still has it loaded:
ask the user to close it, then build again. A compile error in
`host.cpp` or `console.cpp` usually means step 4 left a needed kit change
out.

**Roll back** when the build still fails, or step 9's check fails: the
updated MCP server and docs no longer match the old build. Report the
failure and ask the user; on their yes, undo step 4's edits and:

```powershell
git -C <sub> checkout --detach <old>
```

then rebuild (the build script with `-Check`; a host project: its own
build), skip to step 11, and report
`<old>` as the commit, and the failure.

## 6. Regenerate the typed functions

Only when `<project>\build\console_api.py` exists:

```powershell
python "<sub>\src\reagentlive\frontends\python\reagentlive_gen.py" --console "<project>\build\<name>-console.exe" -o "<project>\build\console_api.py"
```

## 7. Skill and MCP server

**Skill:** nothing to do: the links point into the submodule, and the
client reads the skill each session. A link missing (a fresh clone of the
project): `python -I "<sub>\tools\project.py" link`.

**MCP server:** nothing to do in most updates: the server starts a console
for each call, so it uses the rebuilt console on the next call and sends
the client a changed tool list when the verb table changed. Only when the
server's own Python changed does the client need to start it again. The
server imports the Python session module, so both folders count:

```powershell
git -C <sub> diff --name-only <old> HEAD -- src/reagentlive/frontends/mcp src/reagentlive/frontends/python
```

It prints a path: tell the user to restart the agent's session (Claude
Code, Codex) or reload its MCP servers (other clients).

## 8. Running targets

A process that loaded the old DLL still runs the old agent. Ask the user
to restart each such target (close it and start it again), then load the
new DLL into the fresh process (the injector from setup, if any:
[agent setup](setup.md#7-optional-get-an-injector)).

After a protocol change (step 2) the restart is required: the old agent's
shared section stays mapped with the old layout, so never load the new DLL
into a process that already runs the old one. Without one, the new DLL may
go into the same process (the newer agent takes over,
[protocol](../protocol.md#section)); a restart is still cleaner.

After a protocol change the new console answers an old agent with
`agent layout differs; build agent and client from the same source`: a
target still runs the old DLL; the update did not fail.

## 9. Verify

Run the build script with `-Check`, as [agent setup](setup.md#4-build-and-check)
does; it must print `PASS check`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "<sub>\tools\build_project.ps1" -Project "<project>" -Check
```

Once the user runs a target with the new DLL (step 8), read its `status`
too:

```powershell
& "<project>\build\<name>-console.exe" -c '{"verb": "status"}'
```

| Check | Pass when |
|---|---|
| Commit | `commit` is the new `git -C <sub> rev-parse --short=12 HEAD` (no `-dirty`); it is the console's commit |
| Agent | when the update changed the agent core, `build` (the agent core's build time, unless the host sets its own label) is from step 5: the target runs the new DLL. It stays the same when only the console or a pack changed |
| Verb table | `table_hash` differs from before when step 2 listed verb changes; the same hash after verb changes means the console was not rebuilt |

`replaced_agents` above 0 means the new DLL was loaded into a process
that already had an agent (step 8).

## 10. Lessons and the IDA side

**Lessons:** read the user's lessons against the updated skill: every file
in `%LOCALAPPDATA%\reagentlive\lessons\`. No folder: skip. Propose dropping
each lesson the skill now covers, one line each; the user decides. How the
file works:
[the improvement guide](../../.claude/skills/reagentlive/references/improving.md#local-lessons).

**IDA side:**

```powershell
python -I "<sub>\tools\ida_setup.py"
```

It only reads. `missing`: the user never set it up; skip. `current`: IDA
runs this project's code, so the update needs nothing here, but an IDA
session open since before step 3 keeps the old code: tell the user to
restart IDA (or run `rl.reload()` in it). `stale` (an old loader, or IDA
set up from another project or clone): show the user its `why` lines and,
on their yes, run it again with `--install`.

## 11. Report

Tell the user:

```
┌─ ReAgentLive update complete ─────────────────────────┐
│  ✓ Commit    <old> → <new>                            │
│  ✓ Project   <project> (<kit> kit, or "own host")     │
│  ✓ Kit       <changes applied, none, or "own host">   │
│  ✓ Changes   <a few bullets from step 2>              │
│  ✓ Check     status ok, commit <new>                  │
│  ✓ IDA       <state, or "skipped">                    │
│                                                       │
│  ⚡ Still to do: <commit the submodule pointer,        │
│     restart the session, re-inject a target,          │
│     restart IDA, or "nothing">                        │
└───────────────────────────────────────────────────────┘
```
