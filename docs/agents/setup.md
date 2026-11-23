# ReAgentLive agent setup

Instructions an AI agent follows to set up ReAgentLive on the user's machine;
humans: see the [README](../../README.md).

These steps set up ReAgentLive so you can investigate a running Windows
x64 process. Run every step yourself. Ask the user only where a step says
to. Use PowerShell. Replace `<dir>` with the folder you clone into
(default `$HOME\ReAgentLive`), and `<build>`, `<console>` and `<dll>` with
the values step 2 picks. Later updates: [the update procedure](update.md).

Steps:

1. Check prerequisites
2. Choose the example
3. Clone and build
4. Register the MCP server
5. (Optional) Generate the typed functions
6. Install the skill
7. Verify
8. (Optional) Get an injector
9. Choose the improvement mode
10. Write the setup record
11. Report

---

## 1. Check prerequisites

All of these must be present. Check each one; do not install anything
without asking the user.

| Need | Check | If missing |
|---|---|---|
| Windows x64 | `$env:PROCESSOR_ARCHITECTURE` is `AMD64` | stop: ReAgentLive is Windows x64 only |
| Visual Studio 2022 or newer with the C++ x64 tools | `& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath` prints a path | ask the user to install "Desktop development with C++" (Build Tools are enough) |
| CMake 3.25 or newer | `cmake --version` | ask; Visual Studio ships one in its developer shell |
| Ninja | `ninja --version` | ask; Visual Studio ships one in its developer shell |
| Git | `git --version` | ask |
| Python 3.10 or newer | `python --version` | ask |
| The MCP Python SDK 2 | `python -c "import mcp.server.subscriptions"` | `python -m pip install --upgrade "mcp>=2"` (ask first) |

Check CMake and Ninja again inside the developer shell (step 3) before you
report them missing.

## 2. Choose the example

Ask the user: "Is the target any Windows x64 process, or an Unreal Engine 5
game?" Then use that row's values in every later step:

| Target | Example | `<build>` | `<console>` | `<dll>` | Starts at |
|---|---|---|---|---|---|
| Any x64 process | [`examples/own_thread`](../../examples/own_thread/README.md) | `build\own-thread` | `reagentlive.exe` | `reagentlive-own-thread.dll` | T0 |
| A UE5 game (no dump, no mod needed) | [`examples/unreal`](../../examples/unreal/README.md) | `build\unreal` | `reagentlive-ue.exe` | `reagentlive-unreal.dll` | T1-T2; T3 after `game-thread on` |

What each tier can do: [tiers](../tiers.md).

## 3. Clone and build

Clone, then load the x64 Visual Studio environment and build the chosen
example. The build fetches its dependencies, so it needs network access.

```powershell
git clone https://github.com/Krixx1337/ReAgentLive.git <dir>
cd <dir>
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64"
# Any x64 process:
cmake -S examples/own_thread -B build/own-thread -G Ninja -DCMAKE_BUILD_TYPE=Release -DREAGENTLIVE_PACKS=
cmake --build build/own-thread
# Or, a UE5 game:
cmake -S examples/unreal -B build/unreal -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/unreal
```

Run the dev shell and both `cmake` commands in one PowerShell call: the
environment does not carry over to a new shell. The build writes
`<build>\<console>` and `<build>\<dll>`.

For the own-thread example, `REAGENTLIVE_PACKS` empty leaves out the
Unreal pack's tools: its host cannot run them, and leaving them out keeps
the tool list small. The Unreal example needs the pack, which is the
default.

## 4. Register the MCP server

MCP tools are your default way to drive the agent; when to use Python or
the console instead is the skill's
[`interfaces.md`](../../.claude/skills/reagentlive/references/interfaces.md).

Use absolute paths. The server is the same for every client:

- command: `python`
- args: `<dir>\src\reagentlive\frontends\mcp\reagentlive_mcp.py`,
  `--console`, `<dir>\<build>\<console>`

`--console` must be the console of the example you built: each console
reaches only its own example's agent (they open different shared-memory
names), so `reagentlive.exe` cannot reach the Unreal example's agent, and
the reverse.
Optional server flags: `--packs` (the tool packs served from the start,
default `explore`) and `--allow` (those the agent may enable later; name
`modify` only for a session that may write):
[MCP tools](../client.md#mcp-tools).

Register it under the name `reagentlive` in the user's scope, using the
section for your client. If an entry with that name exists, replace it.

### Claude Code

```powershell
claude mcp add reagentlive --scope user -- python "<dir>\src\reagentlive\frontends\mcp\reagentlive_mcp.py" --console "<dir>\<build>\<console>"
```

### Codex

```powershell
codex mcp add reagentlive -- python "<dir>\src\reagentlive\frontends\mcp\reagentlive_mcp.py" --console "<dir>\<build>\<console>"
```

### Cursor, GitHub Copilot and other clients

Add this under `"mcpServers"` in the client's MCP config file, for example
`~/.cursor/mcp.json` (Cursor) or `.vscode/mcp.json` (Copilot, which uses
`"servers"`). Use forward slashes or doubled backslashes:

```json
"reagentlive": {
  "command": "python",
  "args": [
    "<dir>/src/reagentlive/frontends/mcp/reagentlive_mcp.py",
    "--console", "<dir>/<build>/<console>"
  ]
}
```

Tools that write memory or call into the process are marked destructive,
so the client asks before running them; read-only tools may be allowed.
`status` reads the game-thread mode without a prompt. Which writes need the user's
approval is the skill's rule, not the client's.

## 5. (Optional) Generate the typed functions

Only for Python scripts; the MCP server needs no module (it reads the
console's table each time it starts).

```powershell
python <dir>\src\reagentlive\frontends\python\reagentlive_gen.py --console <dir>\<build>\<console> -o <dir>\<build>\console_api.py
```

It prints the number of verbs and the table hash. Generate it again after
a rebuild that changes a verb. How scripts use it:
[generated functions](../client.md#generated-python-functions).

## 6. Install the skill

The `reagentlive` skill is one folder: `SKILL.md` (first command, tiers,
rules, which interface) and `references/`, which `SKILL.md` names and says
when to read. It reads the docs from the clone, so install it as a link
into `<dir>`: then `git pull` updates skill, docs and build together. Use
the first option that works and remember which (`skill_install` in step
10).

**Claude Code.** The link is `$HOME\.claude\skills\reagentlive`. First
remove an old one: a link only loses the link (`cmd /c rmdir` never
touches the folder it points to); an old copy is deleted after its
`references/local-lessons.md` (the user's own lessons, if any) moves into
the clone's skill folder, where it is git-ignored and every option below
finds it:

```powershell
$link = "$HOME\.claude\skills\reagentlive"
$skill = "<dir>\.claude\skills\reagentlive"
New-Item -ItemType Directory -Force "$HOME\.claude\skills" | Out-Null
if (Test-Path $link) {
  if ((Get-Item $link -Force).LinkType) { cmd /c rmdir "$link" }
  else {
    if (Test-Path "$link\references\local-lessons.md") {
      Move-Item "$link\references\local-lessons.md" "$skill\references\" -Force
    }
    Remove-Item $link -Recurse -Force
  }
}
```

1. **Junction** (`junction`, the default; no admin rights):

   ```powershell
   New-Item -ItemType Junction -Path $link -Target "<dir>\.claude\skills\reagentlive"
   ```

   or from `cmd`: `mklink /J "%USERPROFILE%\.claude\skills\reagentlive" "<dir>\.claude\skills\reagentlive"`.
2. **Symlink** (`symlink`), if the junction fails, or the skill is missing
   from Claude Code after the restart (step 11): it needs Developer Mode
   or an elevated shell. Remove the junction as above, then:

   ```powershell
   New-Item -ItemType SymbolicLink -Path $link -Target "<dir>\.claude\skills\reagentlive"
   ```

   Then set `skill_install` to `symlink` in the record (step 10), also
   when the record was already written.

3. **Local plugin** (`plugin`): reserved until the repo ships
   `<dir>\.claude-plugin\marketplace.json`; skip it while that file is
   absent. When present: ask the user to run `/plugin marketplace add <dir>`
   in Claude Code and install the plugin it lists. It loads in place from
   the clone, named `reagentlive:reagentlive`; updates need
   `/reload-plugins`. Set `skill_install` to `plugin` in the record. (A
   plugin from a remote marketplace is a cached copy under
   `~/.claude/plugins/cache`, never edited; its lessons live at
   `%LOCALAPPDATA%\reagentlive\local-lessons.md`. Setup does not offer it.)
4. **Copy** (`copy`), the last resort:
   `Copy-Item $skill $link -Recurse` (it takes a moved
   `local-lessons.md` along). A copy does not follow the clone; record its commit (`skill_commit`,
   `git -C <dir> rev-parse --short=12 HEAD`) so the skill can tell when it
   is older than the build.

**Other agents** (`instructions`): add this line to the user-level
instructions file, for example `$HOME\.codex\AGENTS.md` for Codex. It
points into the clone, so it never goes stale:

```
Before investigating a running process with the reagentlive tools, read <dir>/.claude/skills/reagentlive/SKILL.md and the references it names, and follow them.
```

For every agent, add this line to the same user-level instructions file
(`$HOME\.claude\CLAUDE.md` for Claude Code):

```
ReAgentLive docs: <dir>/docs/INDEX.md (tiers, every command, its page and a glossary).
```

## 7. Verify

Load the host DLL into a throwaway Python process and ask it for its
status. Any x64 process:

```powershell
Start-Process python -ArgumentList '-c', '"import ctypes,time; ctypes.WinDLL(r''<dir>\<build>\<dll>''); time.sleep(20)"' -WindowStyle Hidden
Start-Sleep 2
& "<dir>\<build>\<console>" -c "status"
```

For the Unreal example, run `-c "status; help game-thread; game-thread"`
instead (`game-thread` with no argument only reads the mode).

No pid: the console finds the one process with its agent (the pid
`Start-Process` returns can be a launcher's, such as the Microsoft Store
Python's, which has already exited). The Python process ends by itself.

| Example | Pass when |
|---|---|
| own-thread | `status` has `"ok":true`, `"ticking":true`, `"host_thread":"own"`, `"packs":[]` and a `commit` equal to `git -C <dir> rev-parse --short=12 HEAD` |
| Unreal | the same with `"packs":["ue"]`; `help game-thread` answers `ok` (the console has the verb) and `game-thread` answers `"mode":"off"` (the DLL has the Draw tick; `unsupported` means the wrong DLL) |

Do not run Unreal commands such as `find` in the Python process: there is
no engine, so they are refused. Never run `game-thread on` as a check. If
a check fails, report the output; do not work around it.

In the real target, `status` tells you the tier
([how to read it](../tiers.md#read-your-tier-from-status)). In a UE5 game,
`game-thread` (no argument) reports the tick's mode; `game-thread on`
moves the agent onto the game thread for engine calls, and needs the
user's approval first.

## 8. (Optional) Get an injector

ReAgentLive does not load itself into a process. Any DLL loader works;
a command-line one lets you load it without the user's help. Ask the user
whether they want [Injector](https://github.com/nefarius/Injector) (MIT,
by nefarius), a third-party command-line DLL injector. If they already
have a loader, or say no, skip this step.

If they agree, download the latest release into the build folder and find
the x64 binary:

```powershell
$zip = "<dir>\build\injector.zip"
$url = (Invoke-RestMethod https://api.github.com/repos/nefarius/Injector/releases/latest).assets |
  Where-Object name -like '*.zip' | Select-Object -First 1 -ExpandProperty browser_download_url
Invoke-WebRequest $url -OutFile $zip
Expand-Archive $zip "<dir>\build\injector" -Force
Get-ChildItem "<dir>\build\injector" -Recurse -Filter Injector.exe | Select-Object FullName
```

Use the path whose folder names x64 (`x64` or `amd64`), not x86 or ARM64.
Antivirus software often flags DLL injectors; if the file is quarantined,
tell the user and skip this step.

Load the host into a process by name or by pid:

```powershell
& "<injector.exe>" --process-name <target.exe> --inject "<dir>\<build>\<dll>"
& "<injector.exe>" --process-id <pid> --inject "<dir>\<build>\<dll>"
```

Inject only into a process the user names. Never pick a target yourself.
The host is never unloaded, so do not use `--eject` on it. Restart the
process instead. In a UE5 game, the first Unreal command stalls the game
once ([before the first command](../commands/unreal.md#before-the-first-command)).

Add this line to the user-level instructions file from step 6:

```
ReAgentLive host: <dir>/<build>/<dll>, console <dir>/<build>/<console>; load it with <injector.exe> --process-name <name> --inject <dll>, only into a process the user names.
```

## 9. Choose the improvement mode

The skill keeps notes on what slowed it down and proposes fixes after a
task ([the improvement guide](../../.claude/skills/reagentlive/references/improving.md)).
Ask the user: "Do you use ReAgentLive as a user or as a developer?"

| Answer | `improvement` | The skill may |
|---|---|---|
| user (the default, also when unsure) | `user` | write one file only: its git-ignored `references/local-lessons.md` (`%LOCALAPPDATA%\reagentlive\local-lessons.md` for a remote plugin's cached copy); anything else (docs or code problems, host notes, findings) becomes a suggestion or issue text the user may act on |
| developer | `dev` | propose changes to the skill, docs and ReAgentLive code; code changes on a branch with the build and tests run, never committed unasked |
| neither ("off", "no") | `off` | nothing |

## 10. Write the setup record

One file, `%LOCALAPPDATA%\reagentlive\setup.json`, so the skill and
[the update procedure](update.md) need no questions:

| Field | Value |
|---|---|
| `dir` | `<dir>`, absolute |
| `examples` | the example folders built: `["own_thread"]`, `["unreal"]` or both |
| `client` | `claude-code`, `codex`, `cursor`, `copilot` or the client's name |
| `python_functions` | `true` when step 5 ran |
| `skill_install` | `junction`, `symlink`, `plugin`, `copy` (step 6), or `instructions` for an instructions line |
| `skill_commit` | copy installs only: the commit copied |
| `improvement` | `user`, `dev` or `off` (step 9) |
| `setup_commit` | the clone's commit, `git -C <dir> rev-parse --short=12 HEAD`; [the update procedure](update.md) sets it to the new commit |

```powershell
$commit = git -C "<dir>" rev-parse --short=12 HEAD
$record = [ordered]@{
  dir = "<dir>"; examples = @("<example>"); client = "<client>"
  python_functions = $false; skill_install = "junction"
  improvement = "user"; setup_commit = $commit
}
# Copy installs only: $record.skill_commit = $commit
New-Item -ItemType Directory -Force "$env:LOCALAPPDATA\reagentlive" | Out-Null
[IO.File]::WriteAllText("$env:LOCALAPPDATA\reagentlive\setup.json", ($record | ConvertTo-Json))
```

`WriteAllText` writes UTF-8 without a byte order mark, which every JSON
reader accepts. Replace an existing record.

## 11. Report

Tell the user:

```
┌─ ReAgentLive setup complete ──────────────────────────┐
│  ✓ Built     <dir>\<build> (<example>)                │
│  ✓ MCP       reagentlive (<client>)                   │
│  ✓ Skill     <path> (<skill_install>)                 │
│  ✓ Check     status ok, ticking, packs <packs>        │
│  ✓ Tier      <T0, or T1-T2 (T3 after game-thread on)> │
│  ✓ Injector  <path, or "skipped">                     │
│  ✓ Mode      <user, dev or off>                       │
│  ✓ Record    %LOCALAPPDATA%\reagentlive\setup.json    │
│                                                       │
│  ⚡ Restart your agent to load the MCP tools           │
│  Then name a process and ask:                         │
│  "load ReAgentLive into it and find <something>"      │
└───────────────────────────────────────────────────────┘
```

Then say in one line that for a UE5 game they mod with its SDK, the mod
itself can host ReAgentLive for T4 ([hosting](../hosting.md)), and in one
line that to update later they paste the update prompt from the README
(it runs [the update procedure](update.md)).
