# Builds ReAgentLive's examples for the agent procedures (docs/agents/), and
# with -Check loads each new DLL into a throwaway Python process and checks
# its status. Prints one PASS/FAIL line per step, with the console and DLL
# paths. Exits 1 on any failure, 2 when the compiler environment cannot load
# (nothing is built then). Runs in Windows PowerShell 5.1 and PowerShell 7.
#
#   tools/build_examples.ps1 -Examples own_thread
#   tools/build_examples.ps1 -Examples own_thread,unreal -Check
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string[]]$Examples,
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$known = @{
    'own_thread' = @{ Build = 'build\own-thread'; Console = 'reagentlive.exe'; Dll = 'reagentlive-own-thread.dll'
                      Configure = @('-DREAGENTLIVE_PACKS='); Packs = '' }
    'unreal'     = @{ Build = 'build\unreal'; Console = 'reagentlive-ue.exe'; Dll = 'reagentlive-unreal.dll'
                      Configure = @(); Packs = 'ue' }
}
# -Examples a,b arrives as one string from powershell -File; split it.
$names = @($Examples | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($name in $names) {
    if (-not $known.ContainsKey($name)) {
        Write-Host "FAIL arguments: unknown example '$name' (own_thread, unreal)"
        exit 1
    }
}

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
            Set-Location $root
        }
    }
}
$missing = Test-Environment
if ($missing) {
    Write-Host "FAIL environment: $missing not found; install Visual Studio's C++ x64 tools or open an x64 developer shell. Nothing was built."
    exit 2
}

$logs = Join-Path $root 'build\logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$failed = 0
function Step([string]$name, [string]$log, [scriptblock]$run) {
    # Windows PowerShell stops on a native tool's stderr line under Stop.
    $ErrorActionPreference = 'Continue'
    $start = Get-Date
    & $run *> $log
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
function Test-Example([string]$name, $example, [string]$commit) {
    $ErrorActionPreference = 'Continue'
    $console = Join-Path $root "$($example.Build)\$($example.Console)"
    $dll = Join-Path $root "$($example.Build)\$($example.Dll)"
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
    $lines = @('{"verb": "status"}')
    if ($example.Packs -eq 'ue') { $lines += '{"verb": "help", "args": {"verb": "game-thread"}}' }
    [IO.File]::WriteAllLines($calls, $lines)
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
        if (-not $r.ticking) { $why += 'not ticking' }
        if ($r.host_thread -ne 'own') { $why += "host_thread $($r.host_thread), not own" }
        $packs = @($r.packs | Where-Object { $_ }) -join ','
        if ($packs -ne $example.Packs) { $why += "packs [$packs], not [$($example.Packs)]" }
        if ($r.commit -ne $commit) { $why += "commit $($r.commit), not $commit" }
        if ($example.Packs -eq 'ue') {
            if (-not $r.game_thread) { $why += 'no game_thread: the wrong DLL' }
            elseif ($r.game_thread.mode -ne 'off') { $why += "game_thread mode $($r.game_thread.mode), not off" }
            $help = $envelopes | Where-Object { $_.command -eq 'help' } | Select-Object -First 1
            if (-not $help -or -not $help.ok) { $why += 'help for game-thread failed: the console lacks the verb' }
        }
    }
    if ($why.Count) {
        Write-Host "FAIL check ${name}: $($why -join '; ')"
        $script:failed++
    } else {
        Write-Host "PASS check ${name}: status ok, ticking, packs [$($example.Packs)], commit $commit"
    }
}

foreach ($name in $names) {
    $example = $known[$name]
    $build = Join-Path $root $example.Build
    $configure = @('-S', "examples\$name", '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release') + $example.Configure
    if (-not (Step "configure $name" (Join-Path $logs "configure-$name.log") { cmake @configure })) { continue }
    if (Step "build $name" (Join-Path $logs "build-$name.log") { cmake --build $build }) {
        Write-Host "  console $(Join-Path $build $example.Console)"
        Write-Host "  dll     $(Join-Path $build $example.Dll)"
    }
}

if ($Check -and $failed -eq 0) {
    $commit = (git -C $root rev-parse --short=12 HEAD).Trim()
    if (git -C $root status --porcelain --untracked-files=no) { $commit += '-dirty' }
    foreach ($name in $names) { Test-Example $name $known[$name] $commit }
}
if ($failed -gt 0) { exit 1 }
exit 0
