<#
.SYNOPSIS
    Merge a feature branch into develop, but only through the validation gate.

.DESCRIPTION
    Wraps hf-validate.ps1 so the gate in .claude/rules/03-validation-gate.md cannot be skipped by
    accident. On success it performs a --no-ff merge into develop with the gate evidence recorded
    in the merge commit message.

    Refuses to run if the working tree is dirty, or if the branch being merged is main or develop.

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

# Deliberately NOT 'Stop'. Windows PowerShell 5.1 wraps a native executable's stderr in an
# ErrorRecord, so 'Stop' would abort on perfectly normal git chatter like "Already on 'branch'".
# Every external call below checks $LASTEXITCODE explicitly instead.
$ErrorActionPreference = 'Continue'

$PluginDir = Split-Path -Parent $PSScriptRoot

function Fail([string] $Message) {
    Write-Host ''
    Write-Host "ABORTED: $Message" -ForegroundColor Red
    exit 1
}

# Runs git and returns its combined output as plain text plus the exit code, without letting
# stderr masquerade as a PowerShell error.
function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $GitArgs)

    $Lines = & git -C $PluginDir @GitArgs 2>&1 | ForEach-Object { $_.ToString() }
    return [pscustomobject]@{
        Output   = ($Lines -join [Environment]::NewLine)
        ExitCode = $LASTEXITCODE
    }
}

function Invoke-GitChecked([string] $What, [string[]] $GitArgs) {
    $r = Invoke-Git @GitArgs
    if ($r.ExitCode -ne 0) { Fail "$What failed:`n$($r.Output)" }
    return $r.Output
}

if (-not $Feature) {
    $Feature = (Invoke-GitChecked 'reading current branch' @('rev-parse', '--abbrev-ref', 'HEAD')).Trim()
}

if ($Feature -eq 'main' -or $Feature -eq 'develop') {
    Fail "'$Feature' is not a feature branch. See .claude/rules/02-git-workflow.md."
}

if ((Invoke-GitChecked 'checking working tree' @('status', '--porcelain')).Trim()) {
    Fail 'working tree is dirty. Commit or stash before merging.'
}

Write-Host ''
Write-Host "Merging $Feature -> $Into" -ForegroundColor Cyan
Write-Host 'Running validation gate first...' -ForegroundColor Cyan

Invoke-GitChecked "checkout $Feature" @('checkout', $Feature) | Out-Null

& (Join-Path $PSScriptRoot 'hf-validate.ps1')
if ($LASTEXITCODE -ne 0) {
    Fail "validation gate failed (exit $LASTEXITCODE). Nothing merged."
}

# Re-read the report so the evidence in the merge message is the real numbers, not a guess.
$IndexPath = Join-Path $PluginDir 'Saved\TestReports\index.json'
$Evidence  = 'validation gate passed'
if (Test-Path $IndexPath) {
    $Report   = Get-Content $IndexPath -Raw | ConvertFrom-Json
    $Evidence = "build OK; $($Report.succeeded) tests passed, $($Report.failed) failed"
}

Invoke-GitChecked "checkout $Into" @('checkout', $Into) | Out-Null

$Message = "Merge $Feature into $Into" + [Environment]::NewLine * 2 + "Validation gate: $Evidence"

$Merge = Invoke-Git 'merge' '--no-ff' $Feature '-m' $Message
if ($Merge.ExitCode -ne 0) {
    Fail "merge failed. Resolve conflicts, then re-run.`n$($Merge.Output)"
}

Write-Host ''
Write-Host "Merged $Feature into $Into" -ForegroundColor Green
Write-Host "  $Evidence"

if (-not $KeepBranch) {
    $Del = Invoke-Git 'branch' '-d' $Feature
    if ($Del.ExitCode -eq 0) { Write-Host "Deleted branch $Feature" }
    else { Write-Host "Could not delete $Feature (left in place): $($Del.Output)" -ForegroundColor Yellow }
}

Write-Host ''
Write-Host 'Not pushed. Pushing is an outward-facing action - ask before pushing.' -ForegroundColor Yellow
exit 0
