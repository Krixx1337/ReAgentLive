# Updating ReAgentLive

Instructions an AI agent follows to update a ReAgentLive clone on the user's
machine; humans: see the [README](../../README.md).

Run every step yourself. Ask the user only where a step says to. Use
PowerShell. `<dir>`, `<build>`, `<console>` and `<dll>` mean the same as
in [agent setup](setup.md#2-choose-the-example).

A host project that carries ReAgentLive as a submodule follows
[its own section](#host-projects-with-a-submodule) instead.

## 1. Read the setup record

First: the project you work in carries ReAgentLive as a submodule
(`git submodule status` lists its folder): follow
[its own section](#host-projects-with-a-submodule) instead, whatever the
record holds (a host may keep one for `improvement` alone).

Otherwise read `%LOCALAPPDATA%\reagentlive\setup.json`
([its fields](setup.md#11-write-the-setup-record)). It names `dir`,
the `examples` built, the `client`, whether `python_functions` were
generated, the `skill_install` and the `improvement` mode.

No record: run [the setup procedure](setup.md) again. `<dir>` has no
`.git`: it is not a clone; set it up again the same way.

## 2. Show what is coming

First check the clone's state; never pull blindly:

```powershell
git -C <dir> status --short
git -C <dir> branch --show-current
```

- `git status --short` prints a tracked file (local changes): stop, show the
  user the list and ask what to do with them (a `dev` mode clone may hold
  work in progress).
- `git branch --show-current` prints nothing (a detached HEAD, such as a
  checked-out tag) or a branch other than `main`: tell the user, and ask
  before `git -C <dir> switch main`. A pull on a detached HEAD fails, and
  on another branch it would not match the log below.

Then, on a clean `main`:

```powershell
git -C <dir> rev-parse --short=12 HEAD
git -C <dir> fetch origin
git -C <dir> log --oneline HEAD..origin/main
git -C <dir> diff HEAD origin/main -- CHANGELOG.md
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
  git -C <dir> diff --name-only <old>..origin/main -- src/reagentlive/core/protocol.h "src/reagentlive/packs/*/protocol_*.h"
  ```

  Then every running target must be restarted and get the new DLL
  (step 7).

## 3. Pull and rebuild

```powershell
git -C <dir> pull --ff-only
```

`--ff-only` refuses when `main` has its own commits; report that and
ask the user. Then rebuild every example in `examples`, comma-separated
(`own_thread,unreal`):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "<dir>\tools\build_examples.ps1" -Examples <examples>
```

It reads as in [agent setup](setup.md#3-clone-and-build) and stamps the
clone's commit into the console (`status` shows it in step 10). If the link
step cannot write `<dll>`, a running process still has it loaded: ask the
user to close it, then build again.

**Roll back** when the build still fails, or step 10's check fails: the
pulled MCP server and docs no longer match the old build. Report the
failure and ask the user; on their yes:

```powershell
git -C <dir> reset --keep <old>
```

then run the build script again with `-Check`, skip to step 12 and leave
the record as it was. Report `<old>` as the commit, and the failure.

## 4. Regenerate the typed functions

Only when `python_functions` is true, for each example:

```powershell
python <dir>\src\reagentlive\frontends\python\reagentlive_gen.py --console <dir>\<build>\<console> -o <dir>\<build>\console_api.py
```

## 5. Skill

Nothing to do: a `junction` or `symlink` points into the clone, and an
`instructions` line names a file in it; the client reads the skill each
session. Any other `skill_install`: install the skill again as
[agent setup](setup.md#6-install-the-skill) does, and record what it
used in step 11.

## 6. MCP server

Nothing to do in most updates: the server starts a console for each call,
so it uses the rebuilt console on the next call and sends the client a
changed tool list when the verb table changed.

Only when the server's own Python changed does the client need to start
it again. The server imports the Python session module, so both folders
count:

```powershell
git -C <dir> diff --name-only <old> HEAD -- src/reagentlive/frontends/mcp src/reagentlive/frontends/python
```

It prints a path: tell the user to restart the agent's session (Claude
Code, Codex) or reload its MCP servers (other clients).

## 7. Running targets

A process that loaded the old DLL still runs the old agent. Ask the user
to restart each such target (close it and start it again), then load the
new `<dir>\<build>\<dll>` into the fresh process (the injector from
setup, if any: [agent setup](setup.md#8-optional-get-an-injector)).

After a protocol change (step 2) the restart is required: the old agent's
shared section stays mapped with the old layout, so never load the new DLL
into a process that already runs the old one. Without one, the new DLL may
go into the same process (the newer agent takes over,
[protocol](../protocol.md#section)); a restart is still cleaner.

After a protocol change the new console answers an old agent with
`agent layout differs; build agent and client from the same source`: a
target still runs the old DLL; the update did not fail.

## 8. Local lessons

Read the user's lessons against the updated skill: every file in
`%LOCALAPPDATA%\reagentlive\lessons\`. No folder: skip this step. Propose
dropping each lesson the skill now covers, one line each; the user
decides. How the file works:
[the improvement guide](../../.claude/skills/reagentlive/references/improving.md#local-lessons).

## 9. IDA side

Only when the record's `ida` is true; otherwise skip this step:

```powershell
python -I "<dir>\tools\ida_setup.py"
```

It only reads. `current`: IDA runs the clone's code, so the update needs
nothing here, but an IDA session open since before step 3 keeps the old
code: tell the user to restart IDA (or run `rl.reload()` in it). `stale`
or `missing`: show the user its `why` lines and, on their yes, run it
again with `--install`.

## 10. Verify

Run the build script with `-Check`, as [agent setup](setup.md#7-verify)
does; every example must print `PASS check`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "<dir>\tools\build_examples.ps1" -Examples <examples> -Check
```

Once the user runs a target with the new DLL (step 7), read its `status`
too:

```powershell
& "<dir>\<build>\<console>" -c '{"verb": "status"}'
```

| Check | Pass when |
|---|---|
| Commit | `commit` is the new `git -C <dir> rev-parse --short=12 HEAD` (no `-dirty`); it is the console's commit |
| Agent | when the update changed the agent core, `build` (the agent core's build time, unless the host sets its own label) is from step 3: the target runs the new DLL. It stays the same when only the console or a pack changed |
| Verb table | `table_hash` differs from before when step 2 listed verb changes; the same hash after verb changes means the console was not rebuilt |

`replaced_agents` above 0 means the new DLL was loaded into a process
that already had an agent (step 7).

## 11. Update the setup record

Write the record back with `setup_commit` set to the new commit (the
baseline for the next update) and `skill_install` to what step 5 left.
Write it the way
[agent setup](setup.md#11-write-the-setup-record) does.

## 12. Report

Tell the user:

```
┌─ ReAgentLive update complete ─────────────────────────┐
│  ✓ Commit    <old> → <new>                            │
│  ✓ Built     <dir>\<build> (<example>)                │
│  ✓ Changes   <a few bullets from step 2>              │
│  ✓ Check     status ok, commit <new>                  │
│  ✓ IDA       <state, or "skipped">                    │
│  ✓ Record    %LOCALAPPDATA%\reagentlive\setup.json    │
│                                                       │
│  ⚡ Still to do: <restart the session, re-inject a     │
│     target, restart IDA, or "nothing">                │
└───────────────────────────────────────────────────────┘
```

## Host projects with a submodule

A project that carries ReAgentLive as a git submodule loads the skill from
the submodule's `.claude/skills` folder and needs no setup record. `<host>`
is the project, `<path>` the submodule's folder. The host's own notes win.
Same as steps 2 to 12, with these differences:

1. Check the submodule's state; never update blindly:

   ```powershell
   git -C <host>/<path> status --short
   git -C <host>/<path> branch --show-current
   git -C <host>/<path> fetch origin
   git -C <host>/<path> log --oneline origin/main..HEAD
   ```

   - `status` prints a tracked file: stop, show the list and ask, as in
     step 2.
   - `log` prints commits: the submodule holds work that is not on
     `origin/main` (a developer's host); stop and ask. An update would
     leave those commits on no branch.
   - A detached HEAD is normal for a submodule. A branch other than
     `main`: tell the user and ask before switching.
2. Keep `rev-parse --short=12 HEAD` as `<old>` and show what is coming as
   in step 2, run in `<host>/<path>`. Empty `log`: nothing to update; stop.
3. Update. On `main`: `git -C <host>/<path> pull --ff-only`. Detached:
   `git -C <host> submodule update --remote <path>`.
4. Rebuild with the host's own build, then do steps 6 to 10 (step 9: `missing`
   without the record's `ida` means skip; step 10: the target's `status`
   only).
5. Report with step 12's block; its `Record` line reads `none (submodule)`.
   The host's submodule pointer now shows as changed: commit it only when
   the user asks.
