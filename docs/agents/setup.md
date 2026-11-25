# ReAgentLive agent setup

Instructions an AI agent follows to set up ReAgentLive in a project on the
user's machine; humans: see the [README](../../README.md).

ReAgentLive is a C++ library a project carries as a git submodule. These
steps put it into a project, either the user's own (a mod that hosts the
agent) or a new one started from an example, and wire you to it: the MCP
server for the project's console, the skill, the docs. Run every step
yourself. Ask the user only where a step says to. Use PowerShell.
`<project>` is the project folder; `<sub>` the submodule's path in it
(`reagentlive` unless the project put it elsewhere); `<console>` and
`<dll>` the host's console and DLL, relative to `<project>`. Run the same
steps for every further project: what belongs to the machine (the
injector, the IDA side, the user's answers) is set up once and kept in the
machine record. Later updates: [the update procedure](update.md).

## 1. Check the machine

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

The build script loads the developer shell itself (step 5): check CMake and
Ninja again there before you report them missing.

Then read the machine record, `%LOCALAPPDATA%\reagentlive\machine.json`.
No file: this is the first project on the machine; with an old
`setup.json` beside it (step 10), take that file's `improvement` as this
record's. Its fields:

| Field | Written by | Value |
|---|---|---|
| `improvement` | step 2 | the user's answer, the default for every later project |
| `injector` | step 8 | the path of `Injector.exe`, or `declined` |
| `ida` | step 9 | `true` once the IDA side is set up, or `declined` |

Write a field this way (it keeps the others; for `true`, pass `$true`,
not a string):

```powershell
$path = "$env:LOCALAPPDATA\reagentlive\machine.json"
$record = if (Test-Path $path) { Get-Content $path -Raw | ConvertFrom-Json } else { [pscustomobject]@{} }
$record | Add-Member -Force -NotePropertyName "<field>" -NotePropertyValue "<value>"
New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
[IO.File]::WriteAllText($path, ($record | ConvertTo-Json))
```

`WriteAllText` writes UTF-8 without a byte order mark, which every JSON
reader accepts.

## 2. Ask about the project

Ask the user, in one message:

1. "Should ReAgentLive go into a project of yours (a mod that hosts it),
   or into a new one to look at a game?"
   - **Their project:** ask for its folder (`<project>`), its console and
     DLL once built (`<console>`, `<dll>`), its build command when it is
     not a CMake project at the folder's top, and whether its host ticks
     on a thread of its own (a `-Check` test can load it) or only inside
     the game. Its own notes win; [hosting](../hosting.md) says what a
     host needs.
   - **A new one:** "Is the target any Windows x64 process, or an Unreal
     Engine 5 game?" picks the example:

     | Target | `<example>` | `<console>` | `<dll>` | Starts at |
     |---|---|---|---|---|
     | Any x64 process | [`own_thread`](../../examples/own_thread/README.md) | `build/reagentlive.exe` | `build/reagentlive-own-thread.dll` | T0 |
     | A UE5 game (no dump, no mod needed) | [`unreal`](../../examples/unreal/README.md) | `build/reagentlive-ue.exe` | `build/reagentlive-unreal.dll` | T1-T2; T3 once `game-thread` is on |

     What each tier can do: [tiers](../tiers.md). Then ask for a short
     name (`<name>`: lowercase letters, digits and dashes; it names the
     agent's section) and a folder (`<project>`: new or empty, wherever
     the user keeps their code; there is no default).
2. Only when the machine record has no `improvement`: "Do you use
   ReAgentLive as a user or as a developer?" picks `<improvement>`: after
   a task the skill reviews what slowed it down
   ([the improvement guide](../../.claude/skills/reagentlive/references/improving.md)).

   | Answer | `<improvement>` | The skill may |
   |---|---|---|
   | user (the default, also when unsure) | `user` | keep its own lessons, without asking, in `%LOCALAPPDATA%\reagentlive\lessons\` and write nothing else; anything else (docs or code problems, host notes, findings) becomes a one-line suggestion at the end of a task |
   | developer | `dev` | propose changes to the skill, docs and ReAgentLive code, each on the user's yes; code changes on a branch with the build and tests run, never committed unasked |
   | neither ("off", "no") | `off` | nothing |

   Write the answer to the record's `improvement`. With the record's
   `improvement` already set, use it and say so in the report.

A clone of a project wired earlier needs only `git submodule update
--init`, then step 4 with no arguments but `--project` (the record keeps
the rest, the clients too), then step 5.

## 3. Put ReAgentLive into the project

**Their project**, when `git -C <project> submodule status` does not list
ReAgentLive yet:

```powershell
git -C "<project>" submodule add https://github.com/Krixx1337/ReAgentLive.git reagentlive
```

Then link the project's targets to the library
([using it in your project](../hosting.md#using-it-in-your-project)) when
it does not yet, with the user.

**A new one:**

```powershell
New-Item -ItemType Directory -Force "<project>" | Out-Null
git -C "<project>" init
git -C "<project>" submodule add https://github.com/Krixx1337/ReAgentLive.git reagentlive
Copy-Item "<project>\reagentlive\examples\<example>\CMakeLists.txt", "<project>\reagentlive\examples\<example>\host.cpp", "<project>\reagentlive\examples\<example>\console.cpp" "<project>"
Set-Content "<project>\.gitignore" "build/"
```

In the copied `host.cpp` and `console.cpp`, set `SectionPrefix` to
`L"Local\\<name>-"` (both files the same), so this project's console never
attaches to another project's agent
([starting from an example](../hosting.md#starting-from-an-example)). The
files are the user's from now on.

## 4. Wire the agent

```powershell
python -I "<project>\<sub>\tools\agent_link.py" --project "<project>" --console <console> --dll <dll> --improvement <improvement> --mcp <clients>
```

Add `--build "<command>"` for a project with a build of its own, and
`--check no` for a host that ticks only inside the game. `<clients>` lists
the clients that read a project MCP file, comma-separated: `claude-code`
(`.mcp.json`), `cursor` (`.cursor/mcp.json`), `copilot`
(`.vscode/mcp.json`). For Codex or another client, pass `claude-code` and
register it by hand in step 6.

It prints one JSON object; `error`: report it and stop (a refusal changes
nothing). It writes, keeping what the project already has:

| File | What |
|---|---|
| `reagentlive.json` | the project record: `library` (the submodule's path), `mcp` (the clients), `console`, `dll`, `build`, `check`, `improvement`; the skill and the scripts read it |
| the MCP files of `<clients>` | the ReAgentLive server: the project's own when it has one (any server running `reagentlive_mcp.py`, under its name, its other flags kept), else a new `reagentlive`; other servers stay. `servers` in the output names it |
| `AGENTS.md` | a ReAgentLive block (read the skill, the docs, build, update); `CLAUDE.md` imports it, or gets the block when it does not |
| `.gitignore` | the skill links |

It links the skill into `.claude\skills\reagentlive` (Claude Code) and
`.agents\skills\reagentlive` (Codex, Cursor and others that read Agent
Skills there), each a junction into the submodule, so updating the
submodule updates the skill; a client without Agent Skills reads it
through the `AGENTS.md` block. A skill folder the project tracks at either
path is its own (a host's pointer skill, say): it stays, and `kept` names
it. It writes nothing outside the project. Run
it again after any change to these values.

## 5. Build and check

```powershell
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "<project>\<sub>\tools\build_project.ps1" -Project "<project>" -Check
```

Leave out `-Check` when the record's `check` is false. The script loads
the x64 Visual Studio environment itself and runs the record's `build`, or
CMake on the project into `<project>\build` (the first build fetches the
library's dependencies, so it needs network access). It prints one `PASS`
or `FAIL` line per step and the console and DLL paths. Exit 2: the
environment did not load (step 1's Visual Studio check); exit 1: a step
failed, and its `FAIL` line names the log under `<project>\build\logs`.
Report a failure; do not work around it.

`-Check` then loads the DLL into a throwaway Python process, asks it for
`status` (it only reads) and ends the process. `PASS check` means `status`
ok, ticking, and the submodule's commit; it names the `host_thread` and
the packs (`[]` for `own_thread`, `[ue]` for `unreal`). A `FAIL check`
line names each failed check: report it. Do not run Unreal commands such
as `find` in a Python process (no engine).

In the real target, `status` tells you the tier
([how to read it](../tiers.md#read-your-tier-from-status)).
`{"verb": "game-thread", "args": {"action": "on"}}` moves a UE5 agent onto
the game thread for engine calls, and needs the user's approval first.

## 6. Register the MCP server

MCP tools are your default way to drive the agent; when to use Python or
the console instead is the skill's
[`interfaces.md`](../../.claude/skills/reagentlive/references/interfaces.md).
The server is `<sub>/src/reagentlive/frontends/mcp/reagentlive_mcp.py`
with `--console` set to the project's console; optional flags `--packs`
and `--allow` (name `modify` only for a session that may write):
[MCP tools](../client.md#mcp-tools).

- **Claude Code, Cursor, Copilot:** step 4 wrote the project's MCP file.
  The client asks once to allow the project's server; Claude Code runs it
  from the folder it starts in, so start it in `<project>`.
- **Codex and other clients:** register it under the name `reagentlive`
  with absolute paths. It then serves this project only; another project
  replaces the entry.

  ```powershell
  codex mcp add reagentlive -- python "<project>\<sub>\src\reagentlive\frontends\mcp\reagentlive_mcp.py" --console "<project>\<console>"
  ```

Tools that write or call into the process are marked destructive, so the
client asks first. Which writes need approval is the skill's rule.

## 7. (Optional) Generate the typed functions

Only for Python scripts; the MCP server needs no module (it reads the
console's table each time it starts).

```powershell
python "<project>\<sub>\src\reagentlive\frontends\python\reagentlive_gen.py" --console "<project>\<console>" -o "<project>\build\console_api.py"
```

It prints the number of verbs and the table hash. The update script
generates it again when the file exists. How scripts use it:
[generated functions](../client.md#generated-python-functions).

## 8. The injector, once per machine

ReAgentLive does not load itself. A mod is loaded with the game; skip this
for it. The machine record's `injector` is a path: use it (gone from disk:
ask again). `declined`: skip. Neither: ask whether the user wants
[Injector](https://github.com/nefarius/Injector) (MIT, third-party
command-line DLL injector); if they have a loader or say no, write
`declined` and skip.

```powershell
$dir = "$env:LOCALAPPDATA\reagentlive\injector"
New-Item -ItemType Directory -Force $dir | Out-Null
$url = (Invoke-RestMethod https://api.github.com/repos/nefarius/Injector/releases/latest).assets |
  Where-Object name -like '*.zip' | Select-Object -First 1 -ExpandProperty browser_download_url
Invoke-WebRequest $url -OutFile "$dir\injector.zip"
Expand-Archive "$dir\injector.zip" $dir -Force
Get-ChildItem $dir -Recurse -Filter Injector.exe | Select-Object FullName
```

Use the x64 binary and write its path to the record's `injector`. If
antivirus quarantines it, tell the user and skip.

Load the DLL into a process by name or by pid:

```powershell
& "<injector.exe>" --process-name <target.exe> --inject "<project>\<dll>"
& "<injector.exe>" --process-id <pid> --inject "<project>\<dll>"
```

Inject only into a process the user names. The DLL is never unloaded: no
`--eject`, restart the process instead. In a UE5 game the first Unreal
command stalls the game once
([before the first command](../commands/unreal.md#before-the-first-command)).

With an injector, add this line to the project's `AGENTS.md`, outside the
ReAgentLive block (step 4 rewrites the block):

```
Load the DLL with <injector.exe> --process-name <target.exe> --inject <dll>, only into a process the user names.
```

## 9. The IDA side, once per machine

The machine record's `ida` is `declined`: skip. Otherwise ask whether the
user uses IDA Pro 9 and wants live decompiling
([IDA loader](../commands/ida-loader.md)); with `ida` true, ask only
whether IDA should run this project's copy from now on. No: skip (write
`declined` when `ida` was unset).

```powershell
python -I "<project>\<sub>\tools\ida_setup.py" --install
```

It passes when `state` is `current`: write `true` to the record's `ida`.
On "no IDA Python found", ask for the `python.exe` IDA uses and add
`--python <exe>`; on any other `why`, tell the user and skip. IDA runs the
copy of the project that set it up last.

## 10. Retire an old install

Only when `%LOCALAPPDATA%\reagentlive\setup.json` exists: an install from
before projects, a clone of its own (the record's `dir`) with a user-level
MCP entry and skill link. Ask the user before each removal:

- the user-level MCP entry `reagentlive` when it names the old clone
  (`claude mcp remove reagentlive --scope user`; other clients: their
  user-level config);
- `$HOME\.claude\skills\reagentlive` and `$HOME\.agents\skills\reagentlive`
  when they are links into the old clone (`cmd /c rmdir "<link>"` removes
  only the link);
- the `ReAgentLive docs:`, `ReAgentLive host:` and skill lines in the
  user-level instructions files (`$HOME\.claude\CLAUDE.md`,
  `$HOME\.codex\AGENTS.md`);
- `setup.json` itself, and the old clone once the user says so.

Keep `%LOCALAPPDATA%\reagentlive\lessons\` and `machine.json`: they serve
every project.

## 11. Report

Tell the user:

```
┌─ ReAgentLive setup complete ──────────────────────────┐
│  ✓ Project   <project> (<theirs, or the example>)     │
│  ✓ Built     <dll>, <console>                         │
│  ✓ Check     <status ok, ticking, packs; or skipped>  │
│  ✓ Tier      <T0, or T1-T2 (T3 after game-thread on)> │
│  ✓ MCP       reagentlive (<project file or client>)   │
│  ✓ Skill     .claude\skills, .agents\skills (links)   │
│  ✓ Injector  <path, declined, or "skipped">           │
│  ✓ IDA       <state, declined, or "skipped">          │
│  ✓ Mode      <user, dev or off; asked or from record>  │
│  ✓ Old setup <retired, kept, or "none">               │
│                                                       │
│  ⚡ Start your agent in <project> to load the tools    │
│  Then name a process and ask:                         │
│  "load ReAgentLive into it and find <something>"      │
└───────────────────────────────────────────────────────┘
```

Then say in one line that the project's code is theirs to build on
([hosting](../hosting.md); a UE5 mod with its SDK hosts it for T4), that
the submodule pointer and the new files are theirs to commit when they
like, and that "update ReAgentLive" updates it later.
