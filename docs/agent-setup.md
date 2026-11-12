# ReAgentLive agent setup

These are instructions for an AI coding agent. They set up ReAgentLive so you
can investigate a running Windows x64 process: build it, register its MCP
tools with your client, install its skill and check it works.

Run every step yourself. Ask the user only where a step says to. Use
PowerShell. Replace `<dir>` with the folder you clone into (default
`$HOME\ReAgentLive`).

Steps:

1. Check prerequisites
2. Clone and build
3. Generate the typed functions
4. Register the MCP server
5. Install the skill
6. Verify
7. Report

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
| The MCP Python SDK | `python -c "import mcp"` | `python -m pip install mcp` (ask first) |

Check CMake and Ninja again inside the developer shell (step 2) before you
report them missing.

## 2. Clone and build

Clone, then load the x64 Visual Studio environment and build the
own-thread example. It is a host DLL that works in any x64 process, and
the `reagentlive.exe` console. The build fetches its dependencies, so it
needs network access.

```powershell
git clone https://github.com/Krixx1337/ReAgentLive.git <dir>
cd <dir>
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64"
cmake -S examples/own_thread -B build/own-thread -G Ninja -DCMAKE_BUILD_TYPE=Release -DREAGENTLIVE_CONSOLE_UE=OFF
cmake --build build/own-thread
```

Run the dev shell and both `cmake` commands in one PowerShell call: the
environment does not carry over to a new shell. The build writes
`build/own-thread/reagentlive.exe` and
`build/own-thread/reagentlive-own-thread.dll`.

`REAGENTLIVE_CONSOLE_UE=OFF` leaves out the Unreal pack's tools. The
own-thread host cannot run them, and leaving them out keeps the tool list
small. Unreal support needs a host built into the user's own mod
([hosting](hosting.md)).

## 3. Generate the typed functions

```powershell
python src/reagentlive/frontends/python/reagentlive_gen.py --console build/own-thread/reagentlive.exe -o build/own-thread/console_api.py
```

It prints the number of verbs and the table hash. The MCP server and
scripts use this module.

## 4. Register the MCP server

Use absolute paths. The server is the same for every client:

- command: `python`
- args: `<dir>\src\reagentlive\frontends\mcp\reagentlive_mcp.py`,
  `--console`, `<dir>\build\own-thread\reagentlive.exe`, `--module`,
  `<dir>\build\own-thread\console_api.py`

Register it under the name `reagentlive` in the user's scope, using the
section for your client. If an entry with that name exists, replace it.

### Claude Code

```powershell
claude mcp add reagentlive --scope user -- python "<dir>\src\reagentlive\frontends\mcp\reagentlive_mcp.py" --console "<dir>\build\own-thread\reagentlive.exe" --module "<dir>\build\own-thread\console_api.py"
```

### Codex

```powershell
codex mcp add reagentlive -- python "<dir>\src\reagentlive\frontends\mcp\reagentlive_mcp.py" --console "<dir>\build\own-thread\reagentlive.exe" --module "<dir>\build\own-thread\console_api.py"
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
    "--console", "<dir>/build/own-thread/reagentlive.exe",
    "--module", "<dir>/build/own-thread/console_api.py"
  ]
}
```

Tools that write memory or call into the process are marked destructive,
so the client asks before running them. Read-only tools are marked as
such, and the user may allow them.

## 5. Install the skill

The `reagentlive` skill teaches the investigation method: rules, workflow
and pitfalls.

- **Claude Code:** copy `<dir>\.claude\skills\reagentlive` to
  `$HOME\.claude\skills\reagentlive`, replacing an older copy.
- **Other agents:** add this line to the user-level instructions file, for
  example `$HOME\.codex\AGENTS.md` for Codex:

  ```
  Before investigating a running process with the reagentlive tools, read <dir>/.claude/skills/reagentlive/SKILL.md and follow it.
  ```

For every agent, add this line to the same user-level instructions file
(`$HOME\.claude\CLAUDE.md` for Claude Code):

```
ReAgentLive docs: <dir>/docs/INDEX.md (every command, its page and a glossary).
```

## 6. Verify

Load the host DLL into a throwaway Python process and ask it for its
status:

```powershell
$p = Start-Process python -ArgumentList '-c', '"import ctypes,time; ctypes.WinDLL(r''<dir>\build\own-thread\reagentlive-own-thread.dll''); time.sleep(20)"' -PassThru -WindowStyle Hidden
Start-Sleep 2
& "<dir>\build\own-thread\reagentlive.exe" $p.Id -c "status"
Stop-Process $p -Force
```

Pass when the result has `"ok":true`, `"ticking":true` and
`"host_thread":"own"`. If it fails, report the output; do not work around
it.

## 7. Report

Tell the user:

```
┌─ ReAgentLive setup complete ──────────────────────────┐
│  ✓ Built   <dir>\build\own-thread                     │
│  ✓ MCP     reagentlive (<client>)                     │
│  ✓ Skill   <path>                                     │
│  ✓ Check   status ok, ticking                         │
│                                                       │
│  ⚡ Restart your agent to load the MCP tools           │
│  Then load reagentlive-own-thread.dll into a process  │
│  and ask: "what does this process have in memory?"    │
└───────────────────────────────────────────────────────┘
```

Then say in one line that loading the DLL into a process is the user's
job, with any DLL loader, and that their own mod can host ReAgentLive
instead for Unreal objects by name ([hosting](hosting.md)).
