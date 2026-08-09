<#
.SYNOPSIS
    HouseForge validation gate: build the editor, then run the HouseForge automation suite.

.DESCRIPTION
    This is the gate described in .claude/rules/03-validation-gate.md. Nothing merges into
    `develop` unless this exits 0.

    Three stages, stopping at the first failure:
      1. Build   - HouseBuilderEditor Win64 Development, via UnrealBuildTool.
      2. Test    - the HouseForge.* automation suite, headless (-nullrhi), via UnrealEditor-Cmd.
      3. Pixels  - the subset that measures the rendered image, re-run WITH a renderer.

    Stage 3 is not a duplicate of stage 2. Under -nullrhi nothing can be drawn, so a test that
    measures pixels reports a warning and passes without asserting anything - which is how the
    millimetre tiling promise came to be guarded by a test the gate never actually executed. Stage 3
    runs those tests with a renderer and fails if any of them still skips its measurement.

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
    [string] $PixelFilter = 'HouseForge.Materials',
    [string] $EngineDir  = 'd:\EpicGames\Engine\UE_5.8'
)

# Deliberately NOT 'Stop'. Windows PowerShell 5.1 wraps a native executable's stderr in an
# ErrorRecord, which under 'Stop' aborts the script on ordinary tool chatter even when the tool
# returned 0. Both stages below check $LASTEXITCODE explicitly instead.
$ErrorActionPreference = 'Continue'

# Scripts/ -> HouseForge/ -> Plugins/ -> project root
$PluginDir  = Split-Path -Parent $PSScriptRoot
$ProjectDir = Split-Path -Parent (Split-Path -Parent $PluginDir)
$UProject   = Join-Path $ProjectDir 'HouseBuilder.uproject'

$Ubt        = Join-Path $EngineDir 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe'
$EditorCmd  = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$ReportDir      = Join-Path $PluginDir 'Saved\TestReports'
$PixelReportDir = Join-Path $PluginDir 'Saved\TestReportsPixels'

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
    Write-Stage 'Stage 1/3  Build  HouseBuilderEditor Win64 Development'

    & $Ubt HouseBuilderEditor Win64 Development -Project="$UProject" -WaitMutex
    if ($LASTEXITCODE -ne 0) {
        Write-Host ''
        Write-Host "GATE FAILED: build returned $LASTEXITCODE" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    Write-Host 'Build OK' -ForegroundColor Green
}
else {
    Write-Stage 'Stage 1/3  Build  SKIPPED (-SkipBuild)'
}

# -------------------------------------------------------------------------- stages 2 and 3: tests
#
# TWO TEST STAGES, AND THE SECOND ONE EXISTS BECAUSE THE FIRST CANNOT DRAW.
#
# Stage 2 runs the whole suite under -nullrhi, which is fast, headless and unable to render a single
# pixel. Tests that measure the rendered image detect that and downgrade to a warning rather than
# reporting a false pass - which meant the one test asserting the millimetre tiling promise, the
# claim this whole milestone rests on, measured NOTHING in the only invocation anybody ever ran.
# Exit 0, green gate, and the assertion never executed.
#
# Stage 3 re-runs just those tests with a renderer attached, and FAILS if any of them still reports
# the HF_UNMEASURED sentinel. A test that silently asserts nothing is the same failure mode this
# milestone was created to fix, so the gate now refuses it rather than printing it in yellow.
function Invoke-TestStage {
    param(
        [string]   $Label,
        [string]   $Filter,
        [string]   $Reports,
        [string[]] $RhiArgs,
        [switch]   $RequireMeasured
    )

    Write-Stage $Label

    if (Test-Path $Reports) { Remove-Item $Reports -Recurse -Force }
    New-Item -ItemType Directory -Path $Reports -Force | Out-Null

    # -testexit stops the editor as soon as the queue drains, so the process cannot hang the gate
    # waiting on a window that will never appear.
    & $EditorCmd "$UProject" `
        -ExecCmds="Automation RunTests $Filter;Quit" `
        -TestExit='Automation Test Queue Empty' `
        -ReportExportPath="$Reports" `
        -unattended -nopause -nosplash -stdout -FullStdOutLogOutput @RhiArgs

    $StageExit = $LASTEXITCODE

    # The editor's exit code is the primary signal, but read the report too: it distinguishes
    # "everything passed" from "nothing ran", which otherwise look identical.
    $IndexPath = Join-Path $Reports 'index.json'
    if (Test-Path $IndexPath) {
        $Report  = Get-Content $IndexPath -Raw | ConvertFrom-Json

        # A test that logs a warning is reported under succeededWithWarnings, NOT under succeeded.
        # Leaving it out undercounts the suite - it is why the gate said 102 of 102 while the report
        # held 106 - and those counts are what a merge commit records as its evidence. Worse, the
        # emptiness check below divides the same way: if every test warned, succeeded would be 0 and a
        # fully passing suite would be rejected as "no tests matched".
        # Every counter must actually be present and numeric. A renamed or dropped field in a future
        # engine version would otherwise read as $null, compare as "not greater than 0", and hand back a
        # confident GATE PASSED computed from nothing at all - the same class of failure as a capture
        # that renders the wrong material without saying so.
        foreach ($Field in @('succeeded', 'succeededWithWarnings', 'failed', 'notRun')) {
            $Value = $Report.$Field
            if ($null -eq $Value -or -not ($Value -is [int] -or $Value -is [long] -or $Value -is [double])) {
                Write-Host ''
                Write-Host "GATE FAILED: the test report has no numeric '$Field' field, so the suite cannot be counted. Report format may have changed." -ForegroundColor Red
                exit 1
            }
        }

        $Passed  = $Report.succeeded + $Report.succeededWithWarnings
        $Total   = $Passed + $Report.failed + $Report.notRun
        Write-Host ''
        Write-Host "Tests: $Passed passed ($($Report.succeededWithWarnings) with warnings), $($Report.failed) failed, $($Report.notRun) not run (of $Total)"

        foreach ($t in $Report.tests) {
            if ($t.state -ne 'Success') {
                Write-Host "  FAIL  $($t.fullTestPath)" -ForegroundColor Red
                foreach ($e in $t.entries) {
                    if ($e.event.type -eq 'Error') { Write-Host "        $($e.event.message)" -ForegroundColor Red }
                }
            }
        }

        # Warnings printed too, because several tests deliberately report a real problem as a warning
        # rather than a failure - a door clashing with a column in the plan is not the articulation's
        # fault, but it is still a door embedded in a column. Written only to the report, the only way
        # to see one was to parse the JSON by hand, so nobody did.
        foreach ($t in $Report.tests) {
            $Warnings = @($t.entries | Where-Object { $_.event.type -eq 'Warning' })
            if ($Warnings.Count -gt 0) {
                Write-Host "  WARN  $($t.fullTestPath)" -ForegroundColor Yellow
                foreach ($e in $Warnings) { Write-Host "        $($e.event.message)" -ForegroundColor Yellow }
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

        # A test that did not run has not passed. Nothing above catches this: notRun is counted into the
        # total and printed, but only `failed` blocked the merge - so a suite where a test was filtered
        # out, crashed before reporting, or was disabled would go green while claiming a total that
        # included it.
        if ($Report.notRun -gt 0) {
            Write-Host ''
            Write-Host "GATE FAILED: $($Report.notRun) test(s) did not run. A test that did not run has not passed." -ForegroundColor Red
            foreach ($t in $Report.tests) {
                if ($t.state -ne 'Success' -and $t.state -ne 'Fail') {
                    Write-Host "  NOT RUN  $($t.fullTestPath) [$($t.state)]" -ForegroundColor Red
                }
            }
            exit 1
        }

        # The per-test states are the primary evidence; the counters above are a summary of them. If the
        # two disagree, the summary is what gets believed and the summary is the thing that has already
        # been wrong once - it omitted succeededWithWarnings and reported 102 of 106.
        $Entries = @($Report.tests).Count
        if ($Entries -ne $Total) {
            Write-Host ''
            Write-Host "GATE FAILED: the report summarises $Total test(s) but lists $Entries. The counts cannot both be right, so neither is trustworthy." -ForegroundColor Red
            exit 1
        }

        $NotSucceeded = @($Report.tests | Where-Object { $_.state -ne 'Success' })
        if ($NotSucceeded.Count -gt 0) {
            Write-Host ''
            Write-Host "GATE FAILED: $($NotSucceeded.Count) test(s) did not succeed, though the summary counters reported none." -ForegroundColor Red
            foreach ($t in $NotSucceeded) { Write-Host "  $($t.state)  $($t.fullTestPath)" -ForegroundColor Red }
            exit 1
        }

        # THE POINT OF THE RENDERER STAGE. A test that could not take its measurement reported a warning
        # and passed, which is right under -nullrhi and completely wrong here: this stage exists to run
        # exactly those measurements, so one that skipped is a stage that did nothing.
        if ($RequireMeasured) {
            $Unmeasured = @()
            foreach ($t in $Report.tests) {
                foreach ($e in $t.entries) {
                    if ($e.event.message -like '*HF_UNMEASURED*') {
                        $Unmeasured += "  $($t.fullTestPath)`n        $($e.event.message)"
                    }
                }
            }
            if ($Unmeasured.Count -gt 0) {
                Write-Host ''
                Write-Host "GATE FAILED: $($Unmeasured.Count) measurement(s) were skipped in the stage that exists to take them. A test that asserts nothing has not passed." -ForegroundColor Red
                foreach ($u in $Unmeasured) { Write-Host $u -ForegroundColor Red }
                exit 1
            }
            Write-Host 'Every pixel measurement was actually taken' -ForegroundColor Green
        }
    }
    else {
        Write-Host ''
        Write-Host "GATE FAILED: no test report written to $Reports" -ForegroundColor Red
        exit 1
    }

    if ($StageExit -ne 0) {
        Write-Host ''
        Write-Host "GATE FAILED: editor returned $StageExit" -ForegroundColor Red
        exit $StageExit
    }
}

Invoke-TestStage -Label "Stage 2/3  Test  automation filter '$TestFilter'" `
                 -Filter  $TestFilter `
                 -Reports $ReportDir `
                 -RhiArgs @('-nullrhi')

# A REAL RHI, AND NOT A WINDOW. -AllowCommandletRendering gives an unattended process a working
# renderer without a visible editor, which is what lets the gate assert the one thing -nullrhi can
# never see: what the material actually draws.
Invoke-TestStage -Label "Stage 3/3  Test  pixel measurements, with a renderer ('$PixelFilter')" `
                 -Filter  $PixelFilter `
                 -Reports $PixelReportDir `
                 -RhiArgs @('-AllowCommandletRendering') `
                 -RequireMeasured

Write-Host ''
Write-Host 'GATE PASSED' -ForegroundColor Green
exit 0
