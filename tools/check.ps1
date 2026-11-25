# Checks, and only reads, whether this machine and a project are ready for ReAgentLive: what setup step 1
# checks, what a fresh clone may lack, and what the skill looks at before saying the machine is not set up.
# Installs nothing and changes nothing; each failed item names its fix, which needs the user's yes.
#
#   reagentlive/tools/check.ps1                    this machine, and the project this clone is a submodule of
#   reagentlive/tools/check.ps1 -Project <folder>  this machine and that project
#
# Prints one JSON object: `ok` (every needed item passes), `machine` and `project` (null without one), each a
# list of {name, ok, detail, fix}. `fix` is a command, or what to ask the user for. An item with `needed`
# false never fails `ok` (the machine record: `improvement` is `user` without it). Exit 0 when ok, else 1.
# Windows PowerShell 5.1 and PowerShell 7.
[CmdletBinding()]
param([string]$Project)
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot

function Item([string]$name, [bool]$ok, [string]$detail, [string]$fix, [bool]$needed = $true) {
    [ordered]@{ name = $name; ok = $ok; needed = $needed; detail = $detail; fix = $(if ($ok) { $null } else { $fix }) }
}

# A native tool's first output line and exit code; $null when it is not on PATH.
function Run([string]$exe, [string[]]$arguments) {
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) { return $null }
    $out = @(& $exe @arguments 2>&1 | ForEach-Object { "$_" })
    return @{ code = $LASTEXITCODE; text = ($out | Select-Object -First 1) }
}

# --- The machine.
$machine = @()
$machine += Item 'windows-x64' ($env:PROCESSOR_ARCHITECTURE -eq 'AMD64') $env:PROCESSOR_ARCHITECTURE 'stop: ReAgentLive is Windows x64 only'

$vs = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$machine += Item 'visual-studio-cpp' ([bool]$vs) $(if ($vs) { $vs } else { 'not found' }) 'ask the user to install Visual Studio 2022 or newer with "Desktop development with C++" (Build Tools are enough)'

# CMake and Ninja: on PATH, or the copies Visual Studio ships (the build script loads its developer shell).
$vsTools = if ($vs) { Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake' }
foreach ($tool in @(@{ name = 'cmake'; exe = 'cmake.exe'; vs = 'CMake\bin\cmake.exe'; min = [version]'3.25' },
                    @{ name = 'ninja'; exe = 'ninja.exe'; vs = 'Ninja\ninja.exe'; min = $null })) {
    $exe = $tool.exe
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue) -and $vsTools -and (Test-Path (Join-Path $vsTools $tool.vs))) {
        $exe = Join-Path $vsTools $tool.vs
    }
    $ran = if (Test-Path $exe -ErrorAction SilentlyContinue) { $out = @(& $exe --version 2>&1 | ForEach-Object { "$_" }); @{ code = $LASTEXITCODE; text = $out[0] } } else { Run $tool.exe @('--version') }
    $ok = $ran -and $ran.code -eq 0
    if ($ok -and $tool.min -and $ran.text -match '(\d+\.\d+)') { $ok = [version]$Matches[1] -ge $tool.min }
    $min = if ($tool.min) { " $($tool.min) or newer" } else { '' }
    $machine += Item $tool.name $ok $(if ($ran) { $ran.text } else { 'not found' }) "ask the user to install $($tool.name)$min, or Visual Studio's C++ CMake tools"
}

$git = Run 'git' @('--version')
$machine += Item 'git' ([bool]($git -and $git.code -eq 0)) $(if ($git) { $git.text } else { 'not found' }) 'ask the user to install Git'

# Python: the Microsoft Store's alias stub answers exit 9009 when no Python is installed. The code holds no
# quote: Windows PowerShell 5.1 and 7 pass quotes to a native argument differently.
$python = Run 'python' @('-c', 'import sys; print(*sys.version_info[:2], sep=chr(46))')
$pythonOk = $python -and $python.code -eq 0 -and $python.text -match '^\d+\.\d+$' -and [version]$python.text -ge [version]'3.10'
$machine += Item 'python' ([bool]$pythonOk) $(if ($python) { $python.text } else { 'not found' }) 'ask the user to install Python 3.10 or newer, as `python` on PATH'
$mcp = if ($pythonOk) { Run 'python' @('-c', 'import mcp.server.subscriptions') }
$machine += Item 'python-mcp' ([bool]($mcp -and $mcp.code -eq 0)) $(if ($mcp -and $mcp.code -eq 0) { 'importable' } else { 'not importable' }) 'python -m pip install --upgrade "mcp>=2" (ask first)'

$record = Join-Path $env:LOCALAPPDATA 'reagentlive\machine.json'
$machine += Item 'machine-record' (Test-Path $record) $(if (Test-Path $record) { $record } else { 'none: improvement is user; the injector and IDA are asked about when first needed' }) 'nothing now; setup writes it when the user answers' $false

# --- The project.
if (-not $Project) { $Project = (git -C $root rev-parse --show-superproject-working-tree 2>$null) }
$projectItems = $null
if ($Project) {
    $projectItems = @()
    if (-not (Test-Path $Project -PathType Container)) {
        $projectItems += Item 'folder' $false $Project 'name an existing project folder'
    } else {
        $Project = (Resolve-Path $Project).Path
        $recordPath = Join-Path $Project 'reagentlive.json'
        $projectRecord = if (Test-Path $recordPath) { Get-Content $recordPath -Raw | ConvertFrom-Json }
        $library = if ($projectRecord -and $projectRecord.library) { $projectRecord.library } else { 'reagentlive' }
        $sub = Join-Path $Project ($library -replace '/', '\')
        $initialized = Test-Path (Join-Path $sub '.git')
        $projectItems += Item 'submodule' $initialized $sub "git -C `"$Project`" submodule update --init --recursive"
        $projectItems += Item 'record' ([bool]$projectRecord) $recordPath 'setup step 4 (tools/agent_link.py) writes it'
        if ($projectRecord) {
            foreach ($key in 'console', 'dll') {
                $path = Join-Path $Project ($projectRecord.$key -replace '/', '\')
                $projectItems += Item "$key-built" (Test-Path $path) $path 'build: tools/build_project.ps1 -Project <project>'
            }
            $clients = @{ 'claude-code' = '.mcp.json'; 'cursor' = '.cursor\mcp.json'; 'copilot' = '.vscode\mcp.json' }
            foreach ($client in @($projectRecord.mcp)) {
                if (-not $clients.ContainsKey($client)) { continue }
                $file = Join-Path $Project $clients[$client]
                $has = (Test-Path $file) -and ((Get-Content $file -Raw) -match 'reagentlive_mcp\.py')
                $projectItems += Item "mcp-$client" $has $file 'run tools/agent_link.py again'
            }
        }
    }
}

$all = @($machine) + @($projectItems | Where-Object { $_ })
$ok = -not ($all | Where-Object { $_.needed -and -not $_.ok })
[ordered]@{ ok = [bool]$ok; machine = $machine; project = $projectItems } | ConvertTo-Json -Depth 4
if ($ok) { exit 0 }
exit 1
