# Updating ReAgentLive

*Home of:* the update procedure for a clone set up by [agent setup](agent-setup.md): what to read first, what to rebuild, what each skill install and the MCP server need, re-injecting, and the checks after.

**TL;DR:** Instructions for an AI coding agent. Read the setup record,
check the clone is a clean `main`, show the user what is coming, pull,
rebuild the same example, then restart running targets and inject the new
DLL (never into the old process after a protocol change) and check
`status` reports the new `commit`. A linked skill and the MCP server follow the clone by
themselves; a copied skill is copied again.

Run every step yourself. Ask the user only where a step says to. Use
PowerShell. `<dir>`, `<build>`, `<console>` and `<dll>` mean the same as
in [agent setup](agent-setup.md#2-choose-the-example).

Steps:

1. Read the setup record
2. Show what is coming
3. Pull and rebuild
4. Regenerate the typed functions
5. Skill
6. MCP server
7. Running targets
8. Local lessons
9. Verify
10. Update the setup record

A host project that carries ReAgentLive as a submodule follows
[its own section](#host-projects-with-a-submodule) instead.

---

## 1. Read the setup record

Read `%LOCALAPPDATA%\reagentlive\setup.json`
([its fields](agent-setup.md#10-write-the-setup-record)). It names `dir`,
the `examples` built, the `client`, whether `python_functions` were
generated, the `skill_install` and the `improvement` mode.

No record: the clone was set up before the record existed. Follow
[installs from before the setup record](#installs-from-before-the-setup-record)
first, then come back to step 2. `<dir>` has no `.git`: it is not a
clone; set it up again with [agent setup](agent-setup.md).

## 2. Show what is coming

First check the clone's state; never pull blindly:

```powershell
git -C <dir> status --short
git -C <dir> branch --show-current
```

- `status --short` prints a tracked file (local changes): stop, show the
  user the list and ask what to do with them (a `dev` mode clone may hold
  work in progress).
- `branch --show-current` prints nothing (a detached HEAD, such as a
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
  ([the convention](contributing.md#docs)), or this prints any path:

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
ask the user. Then rebuild every example in `examples` with the commands
setup used. Run the dev shell and the `cmake` commands in one PowerShell
call:

```powershell
cd <dir>
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64"
# own_thread:
cmake -S examples/own_thread -B build/own-thread -G Ninja -DCMAKE_BUILD_TYPE=Release -DREAGENTLIVE_PACKS=
cmake --build build/own-thread
# unreal:
cmake -S examples/unreal -B build/unreal -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/unreal
```

Each build stamps the clone's commit into the console, so `status` shows
it after (step 9). If the link step cannot write `<dll>`, a running
process still has it loaded: ask the user to close that process, then
build again. Check the exit code of each build; report a failure, do not
work around it.

## 4. Regenerate the typed functions

Only when `python_functions` is true, for each example:

```powershell
python <dir>\src\reagentlive\frontends\python\reagentlive_gen.py --console <dir>\<build>\<console> -o <dir>\<build>\console_api.py
```

## 5. Skill

| `skill_install` | Do |
|---|---|
| `junction` or `symlink` | nothing: the link points into the clone, and the client reads the skill each session |
| `plugin` (a local marketplace, loaded in place) | ask the user to run `/reload-plugins` in Claude Code |
| `copy` | copy it again as [agent setup](agent-setup.md#6-install-the-skill) does; its snippet keeps `references/local-lessons.md` (the copy's file wins over the clone's). Set `skill_commit` in step 10. Offer the junction instead (setup step 6, option 1): later updates then need nothing here |
| `instructions` (other agents) | nothing: the line points into the clone |

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
setup, if any: [agent setup](agent-setup.md#8-optional-get-an-injector)).

After a protocol change (step 2) the restart is required: never load the
new DLL into a process that already runs the old one. The old agent's
shared section stays mapped with the old layout, so the new agent cannot
take it over safely. Only when step 2 found no protocol change may the new
DLL go into the same process (the newer agent takes over and the old one
stops ticking, [protocol](protocol.md#section)); host copies stack, so a
restart is still cleaner.

After a protocol change the new console refuses an old agent: every
command answers with an error saying
`agent layout differs; build agent and client from the same source`. That
error means a target still runs the old DLL, not that the update failed.

## 8. Local lessons

Read the user's lessons against the updated skill: the skill folder's
`references/local-lessons.md`, or `%LOCALAPPDATA%\reagentlive\local-lessons.md`
when the skill is a plugin installed from a remote marketplace (a cached
copy under `~/.claude/plugins/cache`, never edited). No file: skip this
step. Propose dropping each lesson the skill now
covers, one line each; the user decides. How the file works:
[the improvement guide](../.claude/skills/reagentlive/references/improving.md#local-lessons).

## 9. Verify

With a target running the new DLL (or the throwaway Python process from
[agent setup](agent-setup.md#7-verify), same commands and pass checks):

```powershell
& "<dir>\<build>\<console>" -c "status"
```

| Check | Pass when |
|---|---|
| Commit | `commit` is the new `git -C <dir> rev-parse --short=12 HEAD` (no `-dirty`); it is the console's commit |
| Agent | when the update changed the agent core, `build` (the agent core's build time, unless the host sets its own label) is from step 3: the target runs the new DLL. It stays the same when only the console or a pack changed |
| Verb table | `table_hash` differs from before when step 2 listed verb changes; the same hash after verb changes means the console was not rebuilt |
| Example | the example's row in [agent setup](agent-setup.md#7-verify) passes |

`replaced_agents` above 0 means the new DLL was loaded into a process
that already had an agent (step 7).

## 10. Update the setup record

Write the record back with `setup_commit` set to the new commit (the
baseline for the next update), `skill_commit` to the same value for a
copy install, and `skill_install` to what step 5 left (`junction` after
replacing a copy). Write it the way
[agent setup](agent-setup.md#10-write-the-setup-record) does.

Tell the user in a few lines: from `<old>` to the new commit, what
changed for them, what they must still do (restart a session, re-inject a
target).

## Installs from before the setup record

A clone set up before the record existed has no `setup.json`, and its
Claude Code skill is usually a copy. Fill in the record's fields, then
continue at step 2:

1. Ask the user for `<dir>` and which example(s) they built (`examples`).
2. `client`: the client whose MCP config names `reagentlive_mcp.py`
   (`claude mcp list` for Claude Code, `codex mcp list` for Codex, else
   the config files in [agent setup](agent-setup.md#4-register-the-mcp-server)).
3. `python_functions`: true when `<dir>\<build>\console_api.py` exists.
4. `skill_install`:

   ```powershell
   $link = "$HOME\.claude\skills\reagentlive"
   if (Test-Path $link) { (Get-Item $link -Force).LinkType } else { "absent" }
   ```

   `Junction` or `SymbolicLink`: `junction` or `symlink`. Empty: a copy.
   `absent`: another agent's instructions line (`instructions`) or none.
5. A copy: offer to replace it with the junction. Yes: run
   [agent setup step 6](agent-setup.md#6-install-the-skill), whose snippet
   carries the copy's `references/local-lessons.md` into the clone first
   (the file may not exist); record `junction`. No: keep `copy`; step 5
   copies it again.
6. `improvement`: ask the question of
   [agent setup step 9](agent-setup.md#9-choose-the-improvement-mode).

Step 10 writes the record with these fields.

## Host projects with a submodule

A project that carries ReAgentLive as a git submodule loads the skill in
place from the submodule's `.claude/skills` folder; there is no setup
record. To update:

```powershell
git -C <host> submodule update --remote <path>
git -C <host>/<path> log --oneline <old>..HEAD
```

where `<path>` is the submodule's folder and `<old>` the commit it was
at. Read the CHANGELOG lines as in step 2, rebuild the host with its own
build, re-inject (step 7), verify `status` (step 9), and commit the new
submodule pointer only when the user asks.
