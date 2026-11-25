# Makes a ReAgentLive project in one command: checks the machine, creates the folder with ReAgentLive as
# its reagentlive/ submodule, runs tools/project.py init and builds it with tools/build_project.ps1 -Check.
# Self-contained, so it also runs straight from the web before any clone exists:
#
#   & ([scriptblock]::Create((irm https://raw.githubusercontent.com/Krixx1337/ReAgentLive/main/tools/new.ps1))) -Kit unreal -Name my-tool -Dir D:\code\my-tool
#   tools/new.ps1 -Kit own_thread -Name my-tool -Dir D:\code\my-tool -Mcp claude-code,cursor
#
# -Improvement defaults to the machine record's (docs/agents/setup.md#1-check-the-machine), else user.
# -Revision checks the submodule out at a branch, tag or commit instead of the default branch. Installs
# nothing: a missing prerequisite is a FAIL line saying what to install. Prints one PASS/FAIL line per
# step, then the project, DLL and console. Ends with code 1 on a failure (nothing is created when a check
# before `git init` fails), 2 when the build cannot load Visual Studio's environment. Run from the web it
# returns instead of exiting, so the user's shell stays open; $LASTEXITCODE holds the code.
# Windows PowerShell 5.1 and PowerShell 7.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('own_thread', 'unreal')][string]$Kit,
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dir,
    [string]$Improvement = '',
    [string]$Mcp = 'claude-code',
    [string]$Repository = 'https://github.com/Krixx1337/ReAgentLive.git',
    [string]$Revision = ''
)
# Native tools write progress to stderr, which Windows PowerShell turns into errors under Stop: check
# exit codes instead.
$ErrorActionPreference = 'Continue'
$logs = Join-Path ([IO.Path]::GetTempPath()) "reagentlive-new-$Name"

function Fail([string]$line) { throw "FAIL $line" }
function Run([string]$name, [scriptblock]$command) {
    $log = Join-Path $logs (($name -replace '[\\/:*?"<>|]', '_') + '.log')
    & $command *> $log
    if ($LASTEXITCODE -ne 0) {
        Get-Content $log | Select-Object -Last 8 | ForEach-Object { Write-Host "  $_" }
        Fail "$name (exit $LASTEXITCODE, log $log)"
    }
    Write-Host "PASS $name"
}

$code = 0
try {
    if ($Improvement -and $Improvement -notin 'user', 'dev', 'off') { Fail 'arguments: -Improvement is user, dev or off' }
    New-Item -ItemType Directory -Force $logs | Out-Null

    # 1. The machine. Visual Studio, CMake and Ninja are the build script's own check (exit 2).
    if ($env:PROCESSOR_ARCHITECTURE -ne 'AMD64' -and $env:PROCESSOR_ARCHITEW6432 -ne 'AMD64') {
        Fail 'machine: not Windows x64; ReAgentLive is Windows x64 only'
    }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail 'git: not found; install Git' }
    if (-not (Get-Command python -ErrorAction SilentlyContinue)) { Fail 'python: not found; install Python 3.10 or newer' }
    & python -c "import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)" 2>$null
    if ($LASTEXITCODE -ne 0) { Fail 'python: older than 3.10' }
    & python -c "import mcp.server.subscriptions" 2>$null
    if ($LASTEXITCODE -ne 0) { Fail 'mcp: the MCP Python SDK 2 is missing; python -m pip install --upgrade "mcp>=2"' }
    Write-Host 'PASS machine: Windows x64, git, Python, the MCP SDK'

    if (-not $Improvement) {
        $Improvement = 'user'
        $machine = Join-Path $env:LOCALAPPDATA 'reagentlive\machine.json'
        if (Test-Path $machine) {
            try { $known = (Get-Content $machine -Raw | ConvertFrom-Json).improvement } catch { $known = $null }
            if ($known -in 'user', 'dev', 'off') { $Improvement = $known }
        }
    }

    # 2. The folder: new, or empty.
    if ((Test-Path $Dir) -and (Get-ChildItem -Force $Dir | Select-Object -First 1)) {
        Fail "folder: $Dir is not empty; pick a new folder"
    }
    New-Item -ItemType Directory -Force $Dir | Out-Null
    $Dir = (Resolve-Path $Dir).Path
    $sub = Join-Path $Dir 'reagentlive'

    # 3. The repository and the submodule.
    Run 'git init' { git -C $Dir init }
    Run 'submodule' { git -C $Dir submodule add $Repository reagentlive }
    if ($Revision) {
        Run "revision $Revision" {
            git -C $sub fetch origin $Revision
            if ($LASTEXITCODE -eq 0) { git -C $sub checkout --detach FETCH_HEAD }
            # The project's pointer follows the checkout.
            if ($LASTEXITCODE -eq 0) { git -C $Dir add reagentlive }
        }
    }

    # 4. The project's files.
    $out = & python -I (Join-Path $sub 'tools\project.py') init --kit $Kit --name $Name --improvement $Improvement --mcp $Mcp 2>&1
    $made = $null
    try { $made = ($out -join "`n") | ConvertFrom-Json } catch { }
    if (-not $made -or $made.error) {
        $why = if ($made) { $made.error } else { $out -join ' ' }
        Fail "project: $why"
    }
    Write-Host "PASS project: $($made.written.Count) files, links $($made.links -join ', ')"

    # 5. Build and check with the project's own build script, in its own process: the web run bypasses
    # the execution policy only for itself, and the build script loads the compiler environment.
    $shell = (Get-Process -Id $PID).Path
    & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $sub 'tools\build_project.ps1') -Project $Dir -Check | Out-Host
    $code = $LASTEXITCODE
    Write-Host "  project $Dir (kit $Kit, improvement $Improvement)"
    Write-Host "  dll     $(Join-Path $Dir ($made.dll -replace '/', '\'))"
    Write-Host "  console $(Join-Path $Dir ($made.console -replace '/', '\'))"
} catch {
    Write-Host $_.Exception.Message
    $code = 1
}
# A script file exits with the code; the web run's scriptblock returns it, keeping the user's shell.
if ($PSCommandPath) { exit $code }
$global:LASTEXITCODE = $code
