# ReAgentLive agent setup

Instructions an AI agent follows to set up a ReAgentLive project on the
user's machine; humans: see the [README](../../README.md).

These steps make a project: a folder of the user's own, copied from one of
ReAgentLive's kits, that builds a DLL to load into a running Windows x64
process and the console your tools drive. ReAgentLive itself sits in the
project as its `reagentlive/` submodule and is never edited there. Run
every step yourself. Ask the user only where a step says to. Use
PowerShell. `<project>`, `<name>` and `<kit>` are the values step 2 picks.
The same steps make every further project: each is separate, with its own
submodule, and an earlier one stays as it is. What belongs to the machine
(the injector, the IDA side, the user's answers) is set up once and kept in
the machine record, so a later project asks only about itself. Later
updates: [the update procedure](update.md).

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

The build script loads the developer shell itself (step 4): check CMake and
Ninja again there before you report them missing.

Then read the machine record, `%LOCALAPPDATA%\reagentlive\machine.json`.
No file: this is the first project on the machine; with an old
`setup.json` beside it (step 9), take that file's `improvement` as this
record's. Its fields:

| Field | Written by | Value |
|---|---|---|
| `projects` | `project.py` (step 3) | every project made or linked on this machine, absolute paths |
| `improvement` | step 2 | the user's answer, the default for every later project |
| `injector` | step 7 | the path of `Injector.exe`, or `declined` |
| `ida` | step 8 | `true` once the IDA side is set up, or `declined` |

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

## 2. Ask what to make

Ask the user, in one message (question 4 only when the machine record has
no `improvement`):

1. "Is the target any Windows x64 process, or an Unreal Engine 5 game?"
   That picks the kit:

   | Target | `<kit>` | Starts at |
   |---|---|---|
   | Any x64 process | `own_thread` ([kit](../../examples/own_thread/README.md)) | T0 |
   | A UE5 game (no dump, no mod needed) | `unreal` ([kit](../../examples/unreal/README.md)) | T1-T2; T3 once `game-thread` is on |

   What each tier can do: [tiers](../tiers.md).
2. "What should the project be called?" `<name>`: lowercase letters,
   digits and dashes, starting with a letter, at most 32, not starting
   with `reagentlive` (the library's targets). It names the DLL
   (`<name>.dll`), the console (`<name>-console.exe`) and the agent's
   section, so two projects never see each other's agents.
3. "Where should it go?" `<project>`: wherever the user keeps their
   code; there is no default, and the machine record lists every project
   wherever it is. It must not exist yet, or be an empty folder.
4. "Do you use ReAgentLive as a user or as a developer?" picks
   `<improvement>`: after a task the skill reviews what slowed it down
   ([the improvement guide](../../.claude/skills/reagentlive/references/improving.md)).

   | Answer | `<improvement>` | The skill may |
   |---|---|---|
   | user (the default, also when unsure) | `user` | keep its own lessons, without asking, in `%LOCALAPPDATA%\reagentlive\lessons\` and write nothing else; anything else (docs or code problems, host notes, findings) becomes a one-line suggestion at the end of a task |
   | developer | `dev` | propose changes to the skill, docs and ReAgentLive code, each on the user's yes; code changes on a branch with the build and tests run, never committed unasked |
   | neither ("off", "no") | `off` | nothing |

   Write the answer to the record's `improvement`. With the record's
   `improvement` already set, use it and say so in the report.

A mod with a host of its own does not start from a kit: it adds the
submodule and links a target ([hosting](../hosting.md#using-it-in-your-project));
its own notes say how it builds. A clone of a project made earlier needs
no kit either: `git clone --recurse-submodules <url> <project>`, then
`python -I <project>\reagentlive\tools\project.py link`, then steps 4 to 9.

## 3. Create the project

```powershell
New-Item -ItemType Directory -Force "<project>" | Out-Null
git -C "<project>" init
git -C "<project>" submodule add https://github.com/Krixx1337/ReAgentLive.git reagentlive
python -I "<project>\reagentlive\tools\project.py" init --kit <kit> --name <name> --improvement <improvement> --mcp <clients>
```

`<clients>` lists the clients that read a project MCP file,
comma-separated: `claude-code` (`.mcp.json`), `cursor`
(`.cursor/mcp.json`), `copilot` (`.vscode/mcp.json`). For Codex or another
client, pass `claude-code` and register it by hand in step 5.

`project.py` prints one JSON object. `error`: report it and stop. It
copies the kit's `CMakeLists.txt`, `host.cpp` and `console.cpp` with the
project's names, and writes:

| File | What |
|---|---|
| `reagentlive.json` | the project record: `kit`, `name`, `dll`, `console`, `packs`, `improvement`; the skill, the build script and [the update procedure](update.md) read it |
| `AGENTS.md`, `CLAUDE.md` | for an agent in the project: read the skill, where the docs are, how to build and update |
| `README.md` | for the user: the same, by hand |
| `.gitignore` | `build/` and the skill links |
| the MCP files of `<clients>` | step 5 |

It also adds the project to the machine record's `projects` (`registered`
in its output names the record).

It links the skill into `.claude\skills\reagentlive` (Claude Code) and
`.agents\skills\reagentlive` (Codex, Cursor and others that read Agent
Skills there), each a junction into the submodule, so updating the
submodule updates the skill. A client without Agent Skills reads the
skill through `AGENTS.md`'s first line.

## 4. Build and check

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "<project>\reagentlive\tools\build_project.ps1" -Check
```

The script loads the x64 Visual Studio environment itself and builds the
project into `<project>\build` (the first build fetches the library's
dependencies, so it needs network access). It prints one `PASS` or `FAIL`
line per step and the console and DLL paths. Exit 2: the environment did
not load (step 1's Visual Studio check); exit 1: a step failed, and its
`FAIL` line names the log under `<project>\build\logs`. Report a failure;
do not work around it.

`-Check` then loads the DLL into a throwaway Python process, asks it for
`status` (and, for the `unreal` kit, `help` for `game-thread`; both only
read) and ends the process:

| Kit | `PASS check` means |
|---|---|
| own_thread | `status` ok, ticking, `host_thread` `own`, no packs, and the submodule's commit |
| unreal | the same with packs `ue` and `game_thread` mode `off` (the DLL has the Draw tick); the console knows `game-thread` |

A `FAIL check` line names each failed check: report it. Do not run Unreal
commands such as `find` in a Python process (no engine), and never send
`game-thread` as a check: it arms the tick.

In the real target, `status` tells you the tier
([how to read it](../tiers.md#read-your-tier-from-status)).
`{"verb": "game-thread", "args": {"action": "on"}}` moves a UE5 agent onto
the game thread for engine calls, and needs the user's approval first.

## 5. Register the MCP server

MCP tools are your default way to drive the agent; when to use Python or
the console instead is the skill's
[`interfaces.md`](../../.claude/skills/reagentlive/references/interfaces.md).
The server is `reagentlive/src/reagentlive/frontends/mcp/reagentlive_mcp.py`
with `--console` set to the project's console; optional flags `--packs`
and `--allow` (name `modify` only for a session that may write):
[MCP tools](../client.md#mcp-tools).

- **Claude Code, Cursor, Copilot:** step 3 wrote the project's MCP file.
  The client asks once to allow the project's server; Claude Code runs it
  from the folder it starts in, so start it in `<project>`.
- **Codex and other clients:** register it under the name `reagentlive`
  with absolute paths. It then serves this project only; another project
  replaces the entry.

  ```powershell
  codex mcp add reagentlive -- python "<project>\reagentlive\src\reagentlive\frontends\mcp\reagentlive_mcp.py" --console "<project>\build\<name>-console.exe"
  ```

Tools that write or call into the process are marked destructive, so the
client asks first. Which writes need approval is the skill's rule.

## 6. (Optional) Generate the typed functions

Only for Python scripts; the MCP server needs no module (it reads the
console's table each time it starts).

```powershell
python "<project>\reagentlive\src\reagentlive\frontends\python\reagentlive_gen.py" --console "<project>\build\<name>-console.exe" -o "<project>\build\console_api.py"
```

It prints the number of verbs and the table hash. [The update
procedure](update.md) generates it again when the file exists. How scripts
use it: [generated functions](../client.md#generated-python-functions).

## 7. The injector, once per machine

ReAgentLive does not load itself. The machine record's `injector` is a
path: use it (gone from disk: ask again). `declined`: skip. Neither: ask
whether the user wants [Injector](https://github.com/nefarius/Injector)
(MIT, third-party command-line DLL injector); if they have a loader or say
no, write `declined` and skip.

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
& "<injector.exe>" --process-name <target.exe> --inject "<project>\build\<name>.dll"
& "<injector.exe>" --process-id <pid> --inject "<project>\build\<name>.dll"
```

Inject only into a process the user names. The DLL is never unloaded: no
`--eject`, restart the process instead. In a UE5 game the first Unreal
command stalls the game once
([before the first command](../commands/unreal.md#before-the-first-command)).

With an injector, add this line to the project's `AGENTS.md`:

```
Load the DLL with <injector.exe> --process-name <target.exe> --inject build\<name>.dll, only into a process the user names.
```

## 8. The IDA side, once per machine

The machine record's `ida` is `declined`: skip. Otherwise ask whether the
user uses IDA Pro 9 and wants live decompiling
([IDA loader](../commands/ida-loader.md)); with `ida` true, ask only
whether IDA should run this project's copy from now on. No: skip (write
`declined` when `ida` was unset).

```powershell
python -I "<project>\reagentlive\tools\ida_setup.py" --install
```

It passes when `state` is `current`: write `true` to the record's `ida`.
On "no IDA Python found", ask for the `python.exe` IDA uses and add
`--python <exe>`; on any other `why`, tell the user and skip. IDA runs the
copy of the project that set it up last.

## 9. Retire an old install

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

## 10. Report

Tell the user:

```
┌─ ReAgentLive setup complete ──────────────────────────┐
│  ✓ Project   <project> (<kit> kit)                    │
│  ✓ Built     <name>.dll, <name>-console.exe            │
│  ✓ Check     status ok, ticking, packs <packs>        │
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

Then say in one line that `host.cpp` and `console.cpp` are theirs to build
on ([hosting](../hosting.md); a UE5 mod with its SDK hosts it for T4), that
the project is a git repository they can commit when they like, and that
"update ReAgentLive" updates it later.
