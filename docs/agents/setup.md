# ReAgentLive agent setup

Instructions an AI agent follows to set up ReAgentLive on the user's machine;
humans: see the [README](../../README.md).

These steps set up ReAgentLive so you can investigate a running Windows
x64 process. Run every step yourself. Ask the user only where a step says
to. Use PowerShell. Replace `<dir>` with the folder you clone into
(default `$HOME\ReAgentLive`), and `<build>`, `<console>` and `<dll>` with
the values step 2 picks. Later updates: [the update procedure](update.md).

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
| A UE5 game (no dump, no mod needed) | [`examples/unreal`](../../examples/unreal/README.md) | `build\unreal` | `reagentlive-ue.exe` | `reagentlive-unreal.dll` | T1-T2; T3 once `game-thread` is on |

What each tier can do: [tiers](../tiers.md).

## 3. Clone and build

Clone, then build the chosen example (`own_thread` or `unreal`; both:
`own_thread,unreal`). The build fetches its dependencies, so it needs
network access.

```powershell
git clone https://github.com/Krixx1337/ReAgentLive.git <dir>
powershell -NoProfile -ExecutionPolicy Bypass -File "<dir>\tools\build_examples.ps1" -Examples <example>
```

The script loads the x64 Visual Studio environment itself and prints one
`PASS` or `FAIL` line per step, then the `<build>\<console>` and
`<build>\<dll>` paths. Exit 2: the environment did not load (step 1's
Visual Studio check); exit 1: a step failed, and its `FAIL` line names
the log under `<dir>\build\logs`. Report a failure; do not work around it.

## 4. Register the MCP server

MCP tools are your default way to drive the agent; when to use Python or
the console instead is the skill's
[`interfaces.md`](../../.claude/skills/reagentlive/references/interfaces.md).

Use absolute paths. The server is the same for every client:

- command: `python`
- args: `<dir>\src\reagentlive\frontends\mcp\reagentlive_mcp.py`,
  `--console`, `<dir>\<build>\<console>`

`--console` must be the console of the example you built: each console
reaches only its own example's agent. Optional server flags: `--packs`
and `--allow` (name `modify` only for a session that may write):
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

Tools that write or call into the process are marked destructive, so the
client asks first. Which writes need approval is the skill's rule.

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

The `reagentlive` skill is one folder (`SKILL.md` and `references/`). It
reads the docs from the clone, so install it as a link into `<dir>`: then
`git pull` updates skill, docs and build together. Remember how you
installed it (`skill_install` in step 11).

Pick the link by client:

| Client | `<link>` |
|---|---|
| Claude Code | `$HOME\.claude\skills\reagentlive` |
| Codex, Cursor, or another client that reads Agent Skills from `~/.agents/skills` | `$HOME\.agents\skills\reagentlive` |

Cursor also reads `~/.claude/skills`: with Claude Code installed too, one
link serves both. Clear `<link>` first (`cmd /c rmdir` removes only a link;
a folder there is deleted; the user's lessons live elsewhere):

```powershell
$link = "<link>"
New-Item -ItemType Directory -Force (Split-Path $link) | Out-Null
if (Test-Path $link) {
  if ((Get-Item $link -Force).LinkType) { cmd /c rmdir "$link" }
  else { Remove-Item $link -Recurse -Force }
}
```

1. **Junction** (`junction`, the default; no admin rights):

   ```powershell
   New-Item -ItemType Junction -Path $link -Target "<dir>\.claude\skills\reagentlive"
   ```

2. **Symlink** (`symlink`), if the junction fails (clone on a network or
   non-NTFS drive) or the skill is missing after the restart; needs
   Developer Mode or an elevated shell. Clear the link, run
   `New-Item -ItemType SymbolicLink -Path $link -Target "<dir>\.claude\skills\reagentlive"`
   and set `skill_install` to `symlink` in the record.

Neither works: stop and report the error.

**A client without Agent Skills** (`instructions`): add this line to its
user-level instructions file instead. It points into the clone, so it
never goes stale:

```
Before investigating a running process with the reagentlive tools, read <dir>/.claude/skills/reagentlive/SKILL.md and the references it names, and follow them.
```

For every client, add this line to its user-level instructions file
(`$HOME\.claude\CLAUDE.md` for Claude Code, `$HOME\.codex\AGENTS.md` for
Codex):

```
ReAgentLive docs: <dir>/docs/README.md (tiers, every command, its page and a glossary).
```

## 7. Verify

Run the build script again with `-Check` (the build itself is a no-op
now). It loads each example's DLL into a throwaway Python process, asks
it for `status` (and, for the Unreal example, `help` for `game-thread`;
both only read) and ends the process:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "<dir>\tools\build_examples.ps1" -Examples <example> -Check
```

| Example | `PASS check` means |
|---|---|
| own_thread | `status` ok, ticking, `host_thread` `own`, no packs, and the clone's commit |
| unreal | the same with packs `ue` and `game_thread` mode `off` (the DLL has the Draw tick); the console knows `game-thread` |

A `FAIL check` line names each failed check: report it. Do not run Unreal
commands such as `find` in a Python process (no engine), and never send
`game-thread` as a check: it arms the tick.

In the real target, `status` tells you the tier
([how to read it](../tiers.md#read-your-tier-from-status)).
`{"verb": "game-thread", "args": {"action": "on"}}` moves a UE5 agent onto
the game thread for engine calls, and needs the user's approval first.

## 8. (Optional) Get an injector

ReAgentLive does not load itself. Ask whether the user wants
[Injector](https://github.com/nefarius/Injector) (MIT, third-party
command-line DLL injector); if they have a loader or say no, skip.

```powershell
$zip = "<dir>\build\injector.zip"
$url = (Invoke-RestMethod https://api.github.com/repos/nefarius/Injector/releases/latest).assets |
  Where-Object name -like '*.zip' | Select-Object -First 1 -ExpandProperty browser_download_url
Invoke-WebRequest $url -OutFile $zip
Expand-Archive $zip "<dir>\build\injector" -Force
Get-ChildItem "<dir>\build\injector" -Recurse -Filter Injector.exe | Select-Object FullName
```

Use the x64 binary. If antivirus quarantines it, tell the user and skip.

Load the host into a process by name or by pid:

```powershell
& "<injector.exe>" --process-name <target.exe> --inject "<dir>\<build>\<dll>"
& "<injector.exe>" --process-id <pid> --inject "<dir>\<build>\<dll>"
```

Inject only into a process the user names. The host is never unloaded: no
`--eject`, restart the process instead. In a UE5 game the first Unreal
command stalls the game once
([before the first command](../commands/unreal.md#before-the-first-command)).

Add this line to the user-level instructions file from step 6:

```
ReAgentLive host: <dir>/<build>/<dll>, console <dir>/<build>/<console>; load it with <injector.exe> --process-name <name> --inject <dll>, only into a process the user names.
```

## 9. (Optional) Set up the IDA side

Ask whether the user uses IDA Pro 9 and wants live decompiling
([IDA loader](../commands/ida-loader.md)). No: skip (`ida` false).

```powershell
python -I "<dir>\tools\ida_setup.py" --install
```

It passes when `state` is `current`. On "no IDA Python found", ask for the
`python.exe` IDA uses and add `--python <exe>`; on any other `why`, tell the
user and skip.

## 10. Choose the improvement mode

After a task the skill reviews what slowed it down: for a user it keeps
its own lessons without asking, for a developer it proposes fixes
([the improvement guide](../../.claude/skills/reagentlive/references/improving.md)).
Ask the user: "Do you use ReAgentLive as a user or as a developer?"

| Answer | `improvement` | The skill may |
|---|---|---|
| user (the default, also when unsure) | `user` | keep its own lessons, without asking, in `%LOCALAPPDATA%\reagentlive\lessons\` and write nothing else; anything else (docs or code problems, host notes, findings) becomes a one-line suggestion at the end of a task |
| developer | `dev` | propose changes to the skill, docs and ReAgentLive code, each on the user's yes; code changes on a branch with the build and tests run, never committed unasked |
| neither ("off", "no") | `off` | nothing |

## 11. Write the setup record

One file, `%LOCALAPPDATA%\reagentlive\setup.json`, so the skill and
[the update procedure](update.md) need no questions:

| Field | Value |
|---|---|
| `dir` | `<dir>`, absolute |
| `examples` | the example folders built: `["own_thread"]`, `["unreal"]` or both |
| `client` | `claude-code`, `codex`, `cursor`, `copilot` or the client's name |
| `python_functions` | `true` when step 5 ran |
| `skill_install` | `junction` or `symlink` (step 6), or `instructions` for an instructions line |
| `improvement` | `user`, `dev` or `off` (step 10) |
| `ida` | `true` when step 9 set up the IDA side |
| `setup_commit` | the clone's commit, `git -C <dir> rev-parse --short=12 HEAD`; [the update procedure](update.md) sets it to the new commit |

```powershell
$commit = git -C "<dir>" rev-parse --short=12 HEAD
$record = [ordered]@{
  dir = "<dir>"; examples = @("<example>"); client = "<client>"
  python_functions = $false; skill_install = "junction"
  improvement = "user"; ida = $false; setup_commit = $commit
}
New-Item -ItemType Directory -Force "$env:LOCALAPPDATA\reagentlive" | Out-Null
[IO.File]::WriteAllText("$env:LOCALAPPDATA\reagentlive\setup.json", ($record | ConvertTo-Json))
```

`WriteAllText` writes UTF-8 without a byte order mark, which every JSON
reader accepts. Replace an existing record.

## 12. Report

Tell the user:

```
┌─ ReAgentLive setup complete ──────────────────────────┐
│  ✓ Built     <dir>\<build> (<example>)                │
│  ✓ MCP       reagentlive (<client>)                   │
│  ✓ Skill     <path> (<skill_install>)                 │
│  ✓ Check     status ok, ticking, packs <packs>        │
│  ✓ Tier      <T0, or T1-T2 (T3 after game-thread on)> │
│  ✓ Injector  <path, or "skipped">                     │
│  ✓ IDA       <state, or "skipped">                    │
│  ✓ Mode      <user, dev or off>                       │
│  ✓ Record    %LOCALAPPDATA%\reagentlive\setup.json    │
│                                                       │
│  ⚡ Restart your agent to load the MCP tools           │
│  Then name a process and ask:                         │
│  "load ReAgentLive into it and find <something>"      │
└───────────────────────────────────────────────────────┘
```

Then say in one line that a UE5 mod with its SDK can host ReAgentLive for T4
([hosting](../hosting.md)), and that "update ReAgentLive" updates it later.
