# Builds a host project in Visual Studio's x64 environment, and with -Check loads its DLL into a
# throwaway Python process and checks its status. The project is the folder that carries this clone
# (default: the git project it is a submodule of) and its record, reagentlive.json (tools/agent_link.py writes it):
# `dll` and `console`, and `build`, a command run in the project folder; without `build`, CMake builds the
# project into out/build/x64-release, through its CMakePresets.json's x64-release preset when it has one (the
# examples do). Logs go to out/logs. Prints one PASS/FAIL line per step, with the console and DLL paths. Exits 1 on any
# failure, 2 when the compiler environment cannot load (nothing is built then). Runs in Windows
# PowerShell 5.1 and PowerShell 7.
#
#   reagentlive/tools/build_project.ps1
#   reagentlive/tools/build_project.ps1 -Check
#   reagentlive/tools/build_project.ps1 -Project <folder> -Check
#
# -Check fits a host that ticks on its own thread (the examples and hosts made from them): a host that
# ticks from the game only ticks inside the game, so check its status there.
[CmdletBinding()]
param(
    [string]$Project,
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Project) {
    $Project = (git -C $root rev-parse --show-superproject-working-tree 2>$null)
    if (-not $Project) { Write-Host "FAIL arguments: $root is no git submodule of a project; pass -Project"; exit 1 }
}
if (-not (Test-Path $Project -PathType Container)) { Write-Host "FAIL arguments: -Project $Project is not a folder"; exit 1 }
$Project = (Resolve-Path $Project).Path
$recordPath = Join-Path $Project 'reagentlive.json'
if (-not (Test-Path $recordPath)) {
    Write-Host "FAIL arguments: no reagentlive.json in $Project; write it with tools/agent_link.py"
    exit 1
}
$record = Get-Content $recordPath -Raw | ConvertFrom-Json
$out = Join-Path $Project 'out'
$build = Join-Path $out 'build\x64-release'
$preset = Test-Path (Join-Path $Project 'CMakePresets.json')
$example =@{ Console = $record.console; Dll = $record.dll }
Set-Location $Project

# The compiler environment: cl, the linker, CMake and Ninja on PATH.
function Test-Environment {
    foreach ($tool in 'cl.exe', 'link.exe', 'cmake.exe', 'ninja.exe') {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { return $tool }
    }
    if (-not $env:INCLUDE -or -not $env:LIB) { return 'INCLUDE/LIB' }
    return $null
}

if (Test-Environment) {
    $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
    $vswhere = Join-Path $installer 'vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $shell = if ($vs) { Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1' }
        if ($shell -and (Test-Path $shell)) {
            # VsDevCmd.bat runs vswhere.exe by bare name; put its folder on PATH.
            $env:PATH = "$installer;$env:PATH"
            try { & $shell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> $null } catch { }
            Set-Location $Project
        }
    }
}
$missing = Test-Environment
if ($missing) {
    Write-Host "FAIL environment: $missing not found; install Visual Studio's C++ x64 tools or open an x64 developer shell. Nothing was built."
    exit 2
}

$logs = Join-Path $out 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$failed = 0
function Step([string]$name, [string]$log, [scriptblock]$run) {
    # Windows PowerShell stops on a native tool's stderr line under Stop.
    $ErrorActionPreference = 'Continue'
    $start = Get-Date
    # UTF-8 text lines (Windows PowerShell's *> writes UTF-16 and wraps stderr lines as error records).
    & $run 2>&1 | ForEach-Object { "$_" } | Out-File $log -Encoding utf8
    $code = $LASTEXITCODE
    $seconds = [int]((Get-Date) - $start).TotalSeconds
    if ($code -eq 0) {
        Write-Host "PASS $name (${seconds}s)"
        return $true
    }
    Write-Host "FAIL $name (exit $code, ${seconds}s, log $log)"
    Get-Content $log | Select-String -Pattern ' error |error [A-Z]+\d+|FAILED:|CMake Error' |
        Select-Object -First 12 | ForEach-Object { Write-Host "  $($_.Line.Trim())" }
    $script:failed++
    return $false
}

# Loads the DLL into a Python process that prints its own pid (a launcher's
# pid, such as the Microsoft Store Python's, is not the process), asks that
# process for status, then ends it.
function Test-Project([string]$name, $example, [string]$commit) {
    $ErrorActionPreference = 'Continue'
    $console = Join-Path $Project $example.Console
    $dll = Join-Path $Project $example.Dll
    $pidFile = Join-Path $logs "check-$name.pid"
    $calls = Join-Path $logs "check-$name.jsonl"
    Remove-Item $pidFile -ErrorAction SilentlyContinue
    $code = "import ctypes,os,time; ctypes.WinDLL(r'$dll'); print(os.getpid(), flush=True); time.sleep(30)"
    Start-Process python -ArgumentList '-c', "`"$code`"" -WindowStyle Hidden -RedirectStandardOutput $pidFile | Out-Null
    $target = $null
    for ($i = 0; $i -lt 20 -and -not $target; $i++) {
        Start-Sleep -Milliseconds 500
        if (Test-Path $pidFile) { $target = (Get-Content $pidFile -ErrorAction SilentlyContinue | Select-Object -First 1) }
    }
    if (-not $target) {
        Write-Host "FAIL check ${name}: the Python process did not load $dll"
        $script:failed++
        return
    }
    [IO.File]::WriteAllLines($calls, @('{"verb": "status"}'))
    $out = @()
    for ($i = 0; $i -lt 10; $i++) {
        $out = @(& $console $target.Trim() --no-usage -c "@$calls" 2>$null)
        if ($LASTEXITCODE -eq 0) { break }
        Start-Sleep -Milliseconds 500
    }
    Stop-Process -Id ([int]$target) -ErrorAction SilentlyContinue

    $why = @()
    $envelopes = @($out | Where-Object { $_ -match '^\s*\{' } | ForEach-Object { $_ | ConvertFrom-Json })
    $status = $envelopes | Where-Object { $_.command -eq 'status' } | Select-Object -First 1
    if (-not $status -or -not $status.ok) {
        $why += "status failed: $($out -join ' ')"
    } else {
        $r = $status.result
        if (-not $r.ticking) { $why += 'not ticking (a host that ticks from the game ticks only inside it: check status there)' }
        if ($r.commit -ne $commit) { $why += "commit $($r.commit), not $commit" }
        $packs = @($r.packs | Where-Object { $_ }) -join ','
    }
    if ($why.Count) {
        Write-Host "FAIL check ${name}: $($why -join '; ')"
        $script:failed++
    } else {
        Write-Host "PASS check ${name}: status ok, ticking, host_thread $($r.host_thread), packs [$packs], commit $commit"
    }
}

$name = Split-Path -Leaf $Project
$built = $false
if ($record.build) {
    # The project's own build, in this compiler environment.
    # cmd reads the command from a variable, so its own quotes reach it as written.
    $env:REAGENTLIVE_BUILD = $record.build
    $built = Step "build $name" (Join-Path $logs "build.log") { cmd /d /c "%REAGENTLIVE_BUILD%" }
} else {
    # The project's x64-release preset when it has presets (it names the same folder), else the same by hand.
    $configure = if ($preset) { @('--preset', 'x64-release') } else { @('-S', $Project, '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release') }
    $compile = if ($preset) { @('--build', '--preset', 'x64-release') } else { @('--build', $build) }
    if (Step "configure $name" (Join-Path $logs "configure.log") { cmake @configure }) {
        $built = Step "build $name" (Join-Path $logs "build.log") { cmake @compile }
    }
}
if ($built) {
    Write-Host "  console $(Join-Path $Project ($example.Console -replace '/', '\'))"
    Write-Host "  dll     $(Join-Path $Project ($example.Dll -replace '/', '\'))"
}

if ($Check -and $failed -eq 0) {
    # status reports the commit of the ReAgentLive tree the console was built from: this clone.
    $commit = (git -C $root rev-parse --short=12 HEAD).Trim()
    if (git -C $root status --porcelain --untracked-files=no) { $commit += '-dirty' }
    Test-Project $name $example $commit
}
if ($failed -gt 0) { exit 1 }
exit 0
