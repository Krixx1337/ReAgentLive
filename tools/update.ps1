# Updates the ReAgentLive in a project: its reagentlive/ submodule. Three modes, so a person or an agent
# can look before anything changes (docs/agents/update.md):
#
#   reagentlive/tools/update.ps1                  preview: checks the submodule, fetches, prints what is coming
#   reagentlive/tools/update.ps1 -Apply           moves the submodule, rebuilds, checks, regenerates console_api.py
#   reagentlive/tools/update.ps1 -Rollback <old>  moves it back to <old> and rebuilds
#
# -Project picks the project (default: the git project this clone is a submodule of); -Revision updates to a branch, tag or commit instead of origin/main. Preview only reads (it fetches).
# Lines: PASS, FAIL, SKIP, and NOTE for what the user must still decide or do: changed host headers (the
# host's code may need the CHANGELOG's breaking changes), a protocol change (restart the targets), a
# changed MCP server (restart the session). The host's code is never changed here. A project without
# reagentlive.json is moved but not built. The build runs -Check unless the record's `check` is false (a
# host that ticks only inside the game). Exits 1 on any FAIL. A person at a
# terminal is asked after the preview whether to apply it; with redirected input or -NonInteractive (an
# agent) the preview only prints, and the agent runs -Apply once the user agrees.
# Windows PowerShell 5.1 and PowerShell 7.
[CmdletBinding(DefaultParameterSetName = 'Preview')]
param(
    [string]$Project,
    [string]$Revision = 'origin/main',
    [Parameter(ParameterSetName = 'Apply')][switch]$Apply,
    [Parameter(ParameterSetName = 'Rollback')][string]$Rollback
)
# Native tools write progress to stderr, which Windows PowerShell turns into errors under Stop.
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $PSScriptRoot
$script:failed = 0
$script:shell = (Get-Process -Id $PID).Path

# A person at a terminal: not started with -NonInteractive, input not redirected.
function Test-Interactive {
    if (-not [Environment]::UserInteractive) { return $false }
    if ([Environment]::GetCommandLineArgs() | Where-Object { $_ -like '-noni*' }) { return $false }
    try { if ([Console]::IsInputRedirected) { return $false } } catch { }
    return $true
}
$script:interactive = Test-Interactive

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
    $record = Get-Content (Join-Path $dir 'reagentlive.json') -Raw | ConvertFrom-Json
    $check = @()
    if ($record.check -ne $false) { $check = @('-Check') }
    & $script:shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $sub 'tools\build_project.ps1') -Project $dir @check | Out-Host
    if ($LASTEXITCODE -ne 0) { return $false }
    $api = Join-Path $dir 'out\console_api.py'
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
    # The record names where the project carries ReAgentLive; without one, reagentlive/.
    $library = 'reagentlive'
    $recordPath = Join-Path $dir 'reagentlive.json'
    if (Test-Path $recordPath) {
        $named = (Get-Content $recordPath -Raw | ConvertFrom-Json).library
        if ($named) { $library = $named }
    }
    $sub = Join-Path $dir ($library -replace '/', '\')
    if (-not (Test-Path (Join-Path $sub '.git'))) {
        # A clone made without its submodules lists the path with a leading '-'.
        $status = @(Invoke-Git $dir @('submodule', 'status', '--', $library))
        if ($status.Count -and $status[0].StartsWith('-')) { Fail "${label}: the submodule $library is not initialized: git -C `"$dir`" submodule update --init"; return }
        Write-Host "SKIP ${label}: no reagentlive submodule at $sub"; return
    }

    # Never move a submodule that holds work of its own.
    $changed = @(Invoke-Git $sub @('status', '--short', '--untracked-files=no'))
    if ($changed.Count) { Fail "${label}: the submodule has local changes ($($changed -join '; ')); ReAgentLive is not edited in a project"; return }

    if ($Rollback) {
        Invoke-Git $sub @('checkout', '--detach', "$Rollback^{commit}") | Out-Null
        if ($script:gitCode -ne 0) { Fail "${label}: cannot check out $Rollback"; return }
        if (Build $dir $sub $label) { Write-Host "PASS ${label}: rolled back to $Rollback; undo any host edits made for the update" }
        else { Fail "${label}: rolled back to $Rollback, but the build fails; undo the host edits made for the update and build again" }
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
    # The project's pointer skills copy the skill's front matter (tools/agent_link.py).
    $front = @(Invoke-Git $sub @('diff', $old, $new, '--', '.claude/skills/reagentlive/SKILL.md') | Where-Object { $_ -match '^\+(name|description):' })
    if ($front.Count) { Write-Host "NOTE ${label}: the skill's description changed: after the update run tools/agent_link.py again and commit the project's pointer skills" }
    # The headers a host includes (docs/hosting.md): a change there may need the host's code to follow.
    $hostHeaders = @(Invoke-Git $sub @('diff', '--name-only', $old, $new, '--', 'src/reagentlive/core/agent.h',
        'src/reagentlive/core/protocol.h', 'src/reagentlive/packs/*/protocol_*.h', 'src/reagentlive/core/client.h',
        'src/reagentlive/packs/*/client_*.h',
        'src/reagentlive/packs/*/agent_*.h', 'src/reagentlive/packs/*/host_*.h', 'src/reagentlive/frontends/cli/console/console.h',
        'src/reagentlive/frontends/cli/call/build.h', 'src/reagentlive/frontends/cli/call/verb.h',
        'src/reagentlive/frontends/cli/results/schema.h', ':(exclude)src/reagentlive/packs/*/agent_*_internal.h',
        ':(exclude)src/reagentlive/packs/*/agent_*_base.h'))
    if ($hostHeaders.Count) {
        Write-Host "NOTE ${label}: host headers changed ($($hostHeaders -join ', ')); the host's code may need the CHANGELOG's breaking changes; the examples show the new use: git -C `"$sub`" diff $old $new -- examples"
    }
    if (-not $Apply) {
        $question = "Apply the update to ${label}? [y/N]"
        if ($hostHeaders.Count) { $question = "Apply the update to ${label}? The host may need changes first. [y/N]" }
        if (-not $script:interactive -or (Read-Host $question) -notmatch '^(y|yes)$') {
            Write-Host "  preview only: run again with -Apply to update"
            return
        }
    }

    Invoke-Git $sub @('checkout', '--detach', $new) | Out-Null
    if ($script:gitCode -ne 0) { Fail "${label}: cannot check out $new"; return }
    if (Build $dir $sub $label) {
        Write-Host "PASS ${label}: updated to $new; the project's submodule pointer changed: commit it when you like"
    } else {
        Fail "${label}: the build or check fails at $new; a host change the update needs is the usual cause. To go back: update.ps1 -Project `"$dir`" -Rollback $old"
    }
}

if (-not $Project) {
    $Project = (git -C $here rev-parse --show-superproject-working-tree 2>$null)
    if (-not $Project) { Write-Host "FAIL arguments: $here is no git submodule of a project; pass -Project"; exit 1 }
}
Update-Project (Resolve-Path $Project).Path
if ($script:failed -gt 0) { exit 1 }
exit 0
