<#
.SYNOPSIS
    HouseForge validation gate: build the editor, then run the HouseForge automation suite.

.DESCRIPTION
    This is the gate described in .claude/rules/03-validation-gate.md. Nothing merges into
    `develop` unless this exits 0.

    Two stages, stopping at the first failure:
      1. Build   - HouseBuilderEditor Win64 Development, via UnrealBuildTool.
      2. Test    - the HouseForge.* automation suite, headless, via UnrealEditor-Cmd.

.PARAMETER SkipBuild
    Run only the tests. For iterating on tests when the binary is already current.

.PARAMETER TestFilter
    Automation test prefix to run. Defaults to "HouseForge", i.e. the whole suite.

.PARAMETER EngineDir
    Engine root. Defaults to the UE 5.8 install this project is associated with.

.EXAMPLE
    .\hf-validate.ps1
    .\hf-validate.ps1 -SkipBuild -TestFilter HouseForge.Foundation
#>
[CmdletBinding()]
param(
    [switch] $SkipBuild,
    [string] $TestFilter = 'HouseForge',
    [string] $EngineDir  = 'd:\EpicGames\Engine\UE_5.8'
)

$ErrorActionPreference = 'Stop'

# Scripts/ -> HouseForge/ -> Plugins/ -> project root
$PluginDir  = Split-Path -Parent $PSScriptRoot
$ProjectDir = Split-Path -Parent (Split-Path -Parent $PluginDir)
$UProject   = Join-Path $ProjectDir 'HouseBuilder.uproject'

$Ubt        = Join-Path $EngineDir 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe'
$EditorCmd  = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$ReportDir  = Join-Path $PluginDir 'Saved\TestReports'

function Write-Stage([string] $Text) {
    Write-Host ''
    Write-Host "=== $Text ===" -ForegroundColor Cyan
}

function Assert-Exists([string] $Path, [string] $What) {
    if (-not (Test-Path $Path)) {
        Write-Host "FAILED: $What not found at $Path" -ForegroundColor Red
        exit 1
    }
}

Assert-Exists $UProject  'HouseBuilder.uproject'
Assert-Exists $Ubt       'UnrealBuildTool.exe'
Assert-Exists $EditorCmd 'UnrealEditor-Cmd.exe'

# ----------------------------------------------------------------------------- stage 1: build
if (-not $SkipBuild) {
    Write-Stage 'Stage 1/2  Build  HouseBuilderEditor Win64 Development'

    & $Ubt HouseBuilderEditor Win64 Development -Project="$UProject" -WaitMutex
    if ($LASTEXITCODE -ne 0) {
        Write-Host ''
        Write-Host "GATE FAILED: build returned $LASTEXITCODE" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    Write-Host 'Build OK' -ForegroundColor Green
}
else {
    Write-Stage 'Stage 1/2  Build  SKIPPED (-SkipBuild)'
}

# ------------------------------------------------------------------------------ stage 2: tests
Write-Stage "Stage 2/2  Test  automation filter '$TestFilter'"

if (Test-Path $ReportDir) { Remove-Item $ReportDir -Recurse -Force }
New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null

# -nullrhi keeps this headless. -testexit stops the editor as soon as the queue drains, so the
# process cannot hang the gate waiting on a window that will never appear.
& $EditorCmd "$UProject" `
    -ExecCmds="Automation RunTests $TestFilter;Quit" `
    -TestExit='Automation Test Queue Empty' `
    -ReportExportPath="$ReportDir" `
    -unattended -nopause -nosplash -nullrhi -stdout -FullStdOutLogOutput

$TestExit = $LASTEXITCODE

# The editor's exit code is the primary signal, but read the report too: it distinguishes
# "everything passed" from "nothing ran", which otherwise look identical.
$IndexPath = Join-Path $ReportDir 'index.json'
if (Test-Path $IndexPath) {
    $Report  = Get-Content $IndexPath -Raw | ConvertFrom-Json
    $Total   = $Report.succeeded + $Report.failed + $Report.notRun
    Write-Host ''
    Write-Host "Tests: $($Report.succeeded) passed, $($Report.failed) failed, $($Report.notRun) not run (of $Total)"

    foreach ($t in $Report.tests) {
        if ($t.state -ne 'Success') {
            Write-Host "  FAIL  $($t.fullTestPath)" -ForegroundColor Red
            foreach ($e in $t.entries) {
                if ($e.event.type -eq 'Error') { Write-Host "        $($e.event.message)" -ForegroundColor Red }
            }
        }
    }

    if ($Total -eq 0) {
        Write-Host ''
        Write-Host "GATE FAILED: no tests matched '$TestFilter'. An empty suite is not a pass." -ForegroundColor Red
        exit 1
    }
    if ($Report.failed -gt 0) {
        Write-Host ''
        Write-Host "GATE FAILED: $($Report.failed) test(s) failed" -ForegroundColor Red
        exit 1
    }
}
else {
    Write-Host ''
    Write-Host "GATE FAILED: no test report written to $ReportDir" -ForegroundColor Red
    exit 1
}

if ($TestExit -ne 0) {
    Write-Host ''
    Write-Host "GATE FAILED: editor returned $TestExit" -ForegroundColor Red
    exit $TestExit
}

Write-Host ''
Write-Host 'GATE PASSED' -ForegroundColor Green
exit 0
