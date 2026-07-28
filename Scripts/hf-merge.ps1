<#
.SYNOPSIS
    Merge a feature branch into develop, but only through the validation gate.

.DESCRIPTION
    Wraps hf-validate.ps1 so the gate in .claude/rules/03-validation-gate.md cannot be skipped by
    accident. On success it performs a --no-ff merge into develop with the gate evidence recorded
    in the merge commit message.

    Refuses to run if the working tree is dirty, or if the target branch is anything but develop
    unless -Into is given explicitly.

.PARAMETER Feature
    Branch to merge. Defaults to the current branch.

.PARAMETER Into
    Target branch. Defaults to develop.

.PARAMETER KeepBranch
    Do not delete the feature branch after a successful merge.

.EXAMPLE
    .\hf-merge.ps1
    .\hf-merge.ps1 -Feature feature/data-model
#>
[CmdletBinding()]
param(
    [string] $Feature,
    [string] $Into = 'develop',
    [switch] $KeepBranch
)

$ErrorActionPreference = 'Stop'

$PluginDir = Split-Path -Parent $PSScriptRoot

function Fail([string] $Message) {
    Write-Host ''
    Write-Host "ABORTED: $Message" -ForegroundColor Red
    exit 1
}

if (-not $Feature) {
    $Feature = (git -C $PluginDir rev-parse --abbrev-ref HEAD).Trim()
}

if ($Feature -eq 'main' -or $Feature -eq 'develop') {
    Fail "'$Feature' is not a feature branch. See .claude/rules/02-git-workflow.md."
}

$Dirty = git -C $PluginDir status --porcelain
if ($Dirty) {
    Fail 'working tree is dirty. Commit or stash before merging.'
}

Write-Host ''
Write-Host "Merging $Feature -> $Into" -ForegroundColor Cyan
Write-Host 'Running validation gate first...' -ForegroundColor Cyan

git -C $PluginDir checkout $Feature | Out-Null

& (Join-Path $PSScriptRoot 'hf-validate.ps1')
if ($LASTEXITCODE -ne 0) {
    Fail "validation gate failed (exit $LASTEXITCODE). Nothing merged."
}

# Re-read the report so the gate evidence in the merge message is the real numbers.
$IndexPath = Join-Path $PluginDir 'Saved\TestReports\index.json'
$Evidence  = 'validation gate passed'
if (Test-Path $IndexPath) {
    $Report   = Get-Content $IndexPath -Raw | ConvertFrom-Json
    $Evidence = "build OK; $($Report.succeeded) tests passed, $($Report.failed) failed"
}

git -C $PluginDir checkout $Into
if ($LASTEXITCODE -ne 0) { Fail "could not check out $Into" }

$Message = @"
Merge $Feature into $Into

Validation gate: $Evidence
"@

git -C $PluginDir merge --no-ff $Feature -m $Message
if ($LASTEXITCODE -ne 0) {
    Fail "merge failed. Resolve conflicts, then re-run."
}

Write-Host ''
Write-Host "Merged $Feature into $Into" -ForegroundColor Green
Write-Host "  $Evidence"

if (-not $KeepBranch) {
    git -C $PluginDir branch -d $Feature
    Write-Host "Deleted branch $Feature"
}

Write-Host ''
Write-Host 'Not pushed. Pushing is an outward-facing action - ask before pushing.' -ForegroundColor Yellow
exit 0
