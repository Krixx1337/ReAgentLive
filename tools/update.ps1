# Updates the ReAgentLive in a project: its reagentlive/ submodule. Three modes, so a person or an agent
# can look before anything changes (docs/agents/update.md):
#
#   reagentlive/tools/update.ps1                  preview: checks the submodule, fetches, prints what is coming
#   reagentlive/tools/update.ps1 -Apply           moves the submodule, rebuilds, checks, regenerates console_api.py
#   reagentlive/tools/update.ps1 -Rollback <old>  moves it back to <old> and rebuilds
#
# -Project picks the project (default: the folder that holds this clone); -All runs on every project in
# the machine record (docs/agents/setup.md#1-check-the-machine), each with its own copy of the scripts;
# -Revision updates to a branch, tag or commit instead of origin/main. Preview only reads (it fetches).
# Lines: PASS, FAIL, SKIP, and NOTE for what the user must still decide or do: the kit's changes to bring
# into the project's own files, a protocol change (restart the targets), a changed MCP server (restart
# the session). The kit's changes are never applied here: they go into the user's own code. A project
# without reagentlive.json (a host of its own) is moved but not built. Exits 1 on any FAIL.
# Windows PowerShell 5.1 and PowerShell 7.
[CmdletBinding(DefaultParameterSetName = 'Preview')]
param(
    [string]$Project,
    [switch]$All,
    [string]$Revision = 'origin/main',
    [Parameter(ParameterSetName = 'Apply')][switch]$Apply,
    [Parameter(ParameterSetName = 'Rollback')][string]$Rollback
)
# Native tools write progress to stderr, which Windows PowerShell turns into errors under Stop.
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $PSScriptRoot
$script:failed = 0
$script:shell = (Get-Process -Id $PID).Path

# Arguments go as one array: a bare -- or --flag passed through a function's $args can be eaten. Not
# named Git: command names ignore case, so git inside it would call itself.
function Invoke-Git([string]$repo, [string[]]$arguments) {
    $output = & git.exe -C $repo @arguments 2>$null
    $script:gitCode = $LASTEXITCODE
    return $output
}
function Fail([string]$line) {
    Write-Host "FAIL $line"
    $script:failed++
}

# Builds with the project's own build script; a host project has its own build.
function Build([string]$dir, [string]$sub, [string]$label) {
    if (-not (Test-Path (Join-Path $dir 'reagentlive.json'))) {
        Write-Host "NOTE ${label}: no reagentlive.json; build with the host's own build, then check status"
        return $true
    }
    # In its own process, with its own copy of the script: its errors cannot end this one, and the
    # execution policy is bypassed for it. Its lines go to the host; only its exit code counts.
    & $script:shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $sub 'tools\build_project.ps1') -Project $dir -Check | Out-Host
    if ($LASTEXITCODE -ne 0) { return $false }
    $record = Get-Content (Join-Path $dir 'reagentlive.json') -Raw | ConvertFrom-Json
    $api = Join-Path $dir 'build\console_api.py'
    if (Test-Path $api) {
        $gen = Join-Path $sub 'src\reagentlive\frontends\python\reagentlive_gen.py'
        & python $gen --console (Join-Path $dir $record.console) -o $api *> $null
        if ($LASTEXITCODE -ne 0) { Fail "${label}: console_api.py not regenerated"; return $false }
        Write-Host "PASS ${label}: console_api.py regenerated"
    }
    return $true
}

# The commit a revision names: as given (origin/main, a tag, a commit), as a branch of origin, or fetched.
# ^{commit} turns an annotated tag into its commit, so it compares with HEAD and status's commit.
function Resolve-Revision([string]$sub) {
    foreach ($name in $Revision, "origin/$Revision") {
        $commit = Invoke-Git $sub @('rev-parse', '--verify', '--quiet', '--short=12', "$name^{commit}")
        if ($script:gitCode -eq 0 -and $commit) { return $commit }
    }
    Invoke-Git $sub @('fetch', 'origin', $Revision) | Out-Null
    if ($script:gitCode -ne 0) { return $null }
    $commit = Invoke-Git $sub @('rev-parse', '--verify', '--quiet', '--short=12', 'FETCH_HEAD^{commit}')
    if ($script:gitCode -eq 0) { return $commit }
    return $null
}

function Update-Project([string]$dir) {
    $label = Split-Path -Leaf $dir
    $sub = Join-Path $dir 'reagentlive'
    if (-not (Test-Path (Join-Path $sub '.git'))) { Write-Host "SKIP ${label}: no reagentlive submodule in $dir"; return }

    # Never move a submodule that holds work of its own.
    $changed = @(Invoke-Git $sub @('status', '--short', '--untracked-files=no'))
    if ($changed.Count) { Fail "${label}: the submodule has local changes ($($changed -join '; ')); ReAgentLive is not edited in a project"; return }

    if ($Rollback) {
        Invoke-Git $sub @('checkout', '--detach', "$Rollback^{commit}") | Out-Null
        if ($script:gitCode -ne 0) { Fail "${label}: cannot check out $Rollback"; return }
        if (Build $dir $sub $label) { Write-Host "PASS ${label}: rolled back to $Rollback; undo any kit edits made for the update" }
        else { Fail "${label}: rolled back to $Rollback, but the build fails; undo the kit edits made for the update and build again" }
        return
    }
    $branch = (Invoke-Git $sub @('branch', '--show-current'))
    if ($branch -and $branch -ne 'main') { Fail "${label}: the submodule is on branch $branch, not main or a detached commit; ask before switching"; return }
    Invoke-Git $sub @('fetch', 'origin') | Out-Null
    if ($script:gitCode -ne 0) { Fail "${label}: git fetch failed"; return }
    $ahead = @(Invoke-Git $sub @('log', '--oneline', 'origin/main..HEAD'))
    if ($ahead.Count -and $Revision -eq 'origin/main') { Fail "${label}: the submodule has $($ahead.Count) commits not on origin/main; an update would leave them on no branch"; return }
    $old = (Invoke-Git $sub @('rev-parse', '--short=12', 'HEAD'))
    $new = Resolve-Revision $sub
    if (-not $new) { Fail "${label}: no revision $Revision"; return }
    if ($old -eq $new) { Write-Host "PASS ${label}: up to date at $old"; return }

    $commits = @(Invoke-Git $sub @('log', '--oneline', "$old..$new"))
    Write-Host "PASS ${label}: $old -> $new, $($commits.Count) commits"
    $commits | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" }
    Invoke-Git $sub @('diff', $old, $new, '--', 'CHANGELOG.md') | Where-Object { $_ -match '^\+[^+]' } | ForEach-Object { Write-Host "  changelog $($_.Substring(1))" }
    $protocol = @(Invoke-Git $sub @('diff', '--name-only', $old, $new, '--', 'src/reagentlive/core/protocol.h', 'src/reagentlive/packs/*/protocol_*.h'))
    $protocolLine = @(Invoke-Git $sub @('diff', $old, $new, '--', 'CHANGELOG.md') | Where-Object { $_ -match '^\+.*Protocol change: rebuild and re-inject' })
    if ($protocol.Count -or $protocolLine.Count) { Write-Host "NOTE ${label}: protocol change: restart every target and load the new DLL into the fresh process" }
    $server = @(Invoke-Git $sub @('diff', '--name-only', $old, $new, '--', 'src/reagentlive/frontends/mcp', 'src/reagentlive/frontends/python'))
    if ($server.Count) { Write-Host "NOTE ${label}: the MCP server changed: restart the agent's session after the update" }
    $kit = $null
    $recordPath = Join-Path $dir 'reagentlive.json'
    if (Test-Path $recordPath) { $kit = (Get-Content $recordPath -Raw | ConvertFrom-Json).kit }
    if ($kit) {
        $kitFiles = @(Invoke-Git $sub @('diff', '--name-only', $old, $new, '--', "examples/$kit"))
        if ($kitFiles.Count) {
            Write-Host "NOTE ${label}: the $kit kit changed ($($kitFiles -join ', ')); bring what the project needs into its own files: git -C `"$sub`" diff $old $new -- examples/$kit"
        }
    }
    if (-not $Apply) { Write-Host "  preview only: run again with -Apply to update"; return }

    Invoke-Git $sub @('checkout', '--detach', $new) | Out-Null
    if ($script:gitCode -ne 0) { Fail "${label}: cannot check out $new"; return }
    if (Build $dir $sub $label) {
        Write-Host "PASS ${label}: updated to $new; the project's submodule pointer changed: commit it when you like"
    } else {
        Fail "${label}: the build or check fails at $new; a missing kit change is the usual cause. To go back: update.ps1 -Project `"$dir`" -Rollback $old"
    }
}

if ($All -and $Rollback) { Write-Host 'FAIL arguments: -Rollback goes back to one commit: name the project with -Project, not -All'; exit 1 }
$projects = @()
if ($All) {
    $machine = Join-Path $env:LOCALAPPDATA 'reagentlive\machine.json'
    if (-not (Test-Path $machine)) { Write-Host "FAIL arguments: no machine record at $machine"; exit 1 }
    $projects = @((Get-Content $machine -Raw | ConvertFrom-Json).projects | Where-Object { $_ })
    foreach ($dir in $projects) {
        if (-not (Test-Path $dir)) { Write-Host "SKIP ${dir}: gone; drop it from the machine record if it is not coming back" }
    }
    $projects = @($projects | Where-Object { Test-Path $_ })
} else {
    if (-not $Project) { $Project = Split-Path -Parent $here }
    $projects = @((Resolve-Path $Project).Path)
}
foreach ($dir in $projects) { Update-Project $dir }
if ($script:failed -gt 0) { exit 1 }
exit 0
