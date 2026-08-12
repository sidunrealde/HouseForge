<#
.SYNOPSIS
    HouseForge validation gate: build the editor, then run the HouseForge automation suite.

.DESCRIPTION
    This is the gate described in .claude/rules/03-validation-gate.md. Nothing merges into
    `develop` unless this exits 0.

    Four stages, stopping at the first failure:
      1. Build   - HouseBuilderEditor Win64 Development, via UnrealBuildTool.
      2. Test    - the HouseForge.* automation suite, headless (-nullrhi), via UnrealEditor-Cmd.
      3. Pixels  - the subset that measures the rendered image, re-run WITH a renderer.
      4. Lumen   - hf-lumen.ps1: does indirect light actually arrive in a baked flat.

    Stage 3 is not a duplicate of stage 2. Under -nullrhi nothing can be drawn, so a test that
    measures pixels reports a warning and passes without asserting anything - which is how the
    millimetre tiling promise came to be guarded by a test the gate never actually executed. Stage 3
    runs those tests with a renderer and fails if any of them still skips its measurement.

    STAGE 4 EXISTS BECAUSE STAGES 2 AND 3 CANNOT MEASURE LIGHT, AND THE BAKE IS ABOUT LIGHT.
    Every HouseForge.Lumen.* test asserts a MECHANISM - DistanceFieldResolutionScale non-zero, an
    orthogonal transform, a face over the card threshold - because that is all a settings-level
    assertion can reach. Stage 3's only capture instrument is FHFSceneCapture, and a
    USceneCaptureComponent2D runs no global illumination at all: measured, the same baked flat
    captured with Lumen on, with r.DynamicGlobalIlluminationMethod 0, and with GI and the sky light
    both off produced three BYTE-IDENTICAL PNGs. So the gate could be green - and was, 478/478 and
    31/31 - with nothing anywhere having checked that light arrives.

    hf-lumen.ps1 drives a real viewport, settles Lumen for 200 frames per state change, and asserts
    that the shadowed wall is at least 1.5x brighter baked than live on the same tracing path. It is
    the only instrument in the repo that measures the thing the bake was promoted to a prerequisite
    for. It needs a GPU and about twenty minutes, which is the argument for -SkipLumen when a
    developer is iterating, and not an argument for leaving it out of the gate that decides merges.
    hf-merge.ps1 does not pass -SkipLumen.

.PARAMETER SkipBuild
    Run only the tests. For iterating on tests when the binary is already current.

.PARAMETER SkipLumen
    Skip stage 4. For iterating without a GPU or without twenty minutes. Never used by hf-merge.ps1.

.PARAMETER TestFilter
    Automation test prefix to run. Defaults to "HouseForge", i.e. the whole suite.

.PARAMETER EngineDir
    Engine root. Defaults to the UE 5.8 install this project is associated with.

.PARAMETER MinTests
    Absolute floor on how many tests stage 2 must run, applied only to a full-suite run. Every other
    count check in this script is relative and is satisfied by a run of any size; this one catches a
    whole module failing to load. A ratchet - raise it as the suite grows, lower it only in the same
    commit that removes tests.

.PARAMETER MinPixelTests
    The same floor under stage 3, so a narrowed -PixelFilter cannot silently run nothing.

.EXAMPLE
    .\hf-validate.ps1
    .\hf-validate.ps1 -SkipBuild -TestFilter HouseForge.Foundation
#>
[CmdletBinding()]
param(
    [switch] $SkipBuild,
    [switch] $SkipLumen,
    [string] $TestFilter = 'HouseForge',

    # WHAT STAGE 3 RE-RUNS WITH A RENDERER, AND WHY IT IS THREE FILTERS RATHER THAN ONE.
    #
    # This was 'HouseForge.Materials' alone, which made the HF_UNMEASURED mechanism a mechanism with
    # one participant: the sentinel was emitted by a single test in the whole suite, and the stage
    # that exists to refuse it could only ever see that one. Two other tests skip their measurement
    # for exactly the same reason - no renderer, no Slate - and both now say so with the sentinel:
    #
    #   HouseForge.Editor.Panel.*  - including SurfacesEditReachesTheRenderer, the ONLY test that
    #                                proves a panel edit reaches the rendered material.
    #   HouseForge.Capture.*       - including APlanIsOrientedTheWayItSays, whose orientation-message
    #                                assertion is skipped whenever the capture cannot draw.
    #
    # A sentinel nothing greps for is decoration. `+` is how the automation runner separates filters
    # (AutomationCommandline.cpp parses the string on it).
    [string] $PixelFilter = 'HouseForge.Materials+HouseForge.Editor.Panel+HouseForge.Capture',
    [string] $EngineDir  = 'd:\EpicGames\Engine\UE_5.8',

    # THE FLOOR UNDER THE WHOLE SUITE, and the reason it exists.
    #
    # Every other count check in this script is RELATIVE: the summary is cross-checked against the
    # per-test list, notRun must be zero, every state must be Success. All of those are satisfied,
    # perfectly and self-consistently, by a run of any size. If the HouseForgeEditor module failed to
    # load, or a `#if WITH_DEV_AUTOMATION_TESTS` block were switched off, or a filter typo narrowed
    # the run, the report would be internally flawless at a fraction of the suite and this script
    # would print GATE PASSED - and the merge commit would then record that fraction as its evidence.
    # Given that the counter arithmetic in this very script has already been wrong once, the absence
    # of an absolute floor is the same class of hole one level up.
    #
    # It is a ratchet, not a target: adding tests never trips it, and lowering it is a deliberate edit
    # with a diff. Only applied when the gate is running the whole suite - a deliberately narrow
    # -TestFilter is a developer iterating, not the gate.
    #
    # FALSIFIED. Run as the full suite against a filter matching 4 tests: the report was internally
    # flawless - 4 passed, 0 failed, 0 not run, every state Success, summary matching the list - and
    # every relative check in this script passed it. Only this floor stopped it:
    #   "GATE FAILED: only 4 test(s) ran, and the suite is at least 440."
    [int] $MinTests = 440,

    # THE SAME FLOOR UNDER STAGE 3, which needs its own because it runs its own filter. A typo in
    # -PixelFilter, or a renamed test category, narrows the renderer stage to nothing while stage 2
    # still passes - and stage 3 is the only stage that can measure a pixel, so silently running none
    # of it is exactly the hole that put an unmeasured tiling assertion through a green gate.
    #
    # FALSIFIED. Narrowing -PixelFilter to 'HouseForge.Capture' alone:
    #   "GATE FAILED: only 6 test(s) ran, and the suite is at least 28."
    [int] $MinPixelTests = 28
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
# ONE REPORT DIRECTORY PER RUN, because two gates on one machine shared these and it is not a
# tidiness problem. Each stage begins by deleting its report directory and ends by reading index.json
# back out of it, so a second gate starting mid-run deletes the first one's evidence underneath it.
#
# Seen: a full-suite run reached HouseForge.Editor.Surfaces and then died with "no test report
# written to ...\Saved\TestReports", because a concurrent run had just cleared it.
#
# That failure is loud. The one that is not: the two runs overlap the other way and a gate reads an
# index.json written by somebody ELSE'S suite - a different filter, a different tree, possibly a
# different commit - and reports those counts as its own. The merge commit then records, as its
# evidence, a run that never happened on that code. Every count check in this script is honest about
# the file it was handed and none of them can tell whose file it is.
$ReportDir      = Join-Path $PluginDir "Saved\TestReports-$PID"
$PixelReportDir = Join-Path $PluginDir "Saved\TestReportsPixels-$PID"

# WHAT THE MERGE COMMIT RECORDS, WRITTEN BY THE RUN THAT EARNED IT.
#
# Rule 03 says the gate's evidence goes into the merge commit so history shows what was actually
# verified. hf-merge.ps1 read that out of Saved\TestReports\index.json - a path that stopped existing
# when the report directories became per-PID, so Test-Path failed, the fallback fired, and every
# merge message since has said the unfalsifiable "validation gate passed" instead of a count. It also
# read $Report.succeeded alone, which omits succeededWithWarnings and undercounts the suite by
# however many tests warned.
#
# So the gate writes its own evidence, at a stable path, from the numbers it has just checked - and
# deletes it up front, so a stale file from a previous run can never be picked up as this one's.
$EvidencePath = Join-Path $PluginDir 'Saved\GateEvidence.json'
$Evidence = [ordered]@{
    ranAtUtc = (Get-Date).ToUniversalTime().ToString('s') + 'Z'
    commit   = (& git -C $PluginDir rev-parse --short HEAD 2>$null)
    build    = 'not run'
    suite    = 'not run'
    pixels   = 'not run'
    lumen    = 'not run'
}

function Write-Evidence {
    New-Item -ItemType Directory -Path (Split-Path -Parent $EvidencePath) -Force | Out-Null
    $Evidence | ConvertTo-Json | Out-File -FilePath $EvidencePath -Encoding utf8
}

if (Test-Path $EvidencePath) { Remove-Item $EvidencePath -Force }

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
    Write-Stage 'Stage 1/4  Build  HouseBuilderEditor Win64 Development'

    & $Ubt HouseBuilderEditor Win64 Development -Project="$UProject" -WaitMutex
    if ($LASTEXITCODE -ne 0) {
        Write-Host ''
        Write-Host "GATE FAILED: build returned $LASTEXITCODE" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    Write-Host 'Build OK' -ForegroundColor Green
    $Evidence.build = 'OK'
}
else {
    Write-Stage 'Stage 1/4  Build  SKIPPED (-SkipBuild)'
    $Evidence.build = 'SKIPPED (-SkipBuild)'
}
Write-Evidence

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
        [switch]   $RequireMeasured,

        # Which field of $Evidence this stage's counts are recorded into, for the merge commit.
        [string]   $EvidenceKey = '',

        # Absolute floor on how many tests must have run. Zero disables it, for a narrow filter.
        [int]      $Minimum = 0,

        # Tests that MUST appear in the report by name, one per module. See the note at the call.
        [string[]] $Canaries = @()
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

        # Recorded before the checks below, deliberately: if a check fails the script exits and this
        # file is left describing the run that failed, which is more use than no file at all.
        if ($EvidenceKey -ne '') {
            $Evidence[$EvidenceKey] = "$Passed/$Total passed ($($Report.succeededWithWarnings) with warnings), $($Report.failed) failed, $($Report.notRun) not run"
            Write-Evidence
        }

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
            Write-Host "GATE FAILED: no tests matched '$Filter'. An empty suite is not a pass." -ForegroundColor Red
            exit 1
        }

        # AN INTERNALLY CONSISTENT REPORT OF THE WRONG SIZE. Every check around this one is relative -
        # the summary against the list, notRun against zero, each state against Success - and all of
        # them are satisfied by a run of any size at all. Losing a whole module, or a
        # WITH_DEV_AUTOMATION_TESTS block, or narrowing the filter by a typo, produces a perfectly
        # self-consistent report at a fraction of the suite and a confident GATE PASSED, and the merge
        # commit then records that fraction as its evidence.
        if ($Minimum -gt 0 -and $Total -lt $Minimum) {
            Write-Host ''
            Write-Host "GATE FAILED: only $Total test(s) ran, and the suite is at least $Minimum. A report can be perfectly self-consistent and still be missing an entire module - that is what this floor is for. If tests were deliberately removed, lower -MinTests in the same commit." -ForegroundColor Red
            exit 1
        }

        # AND THE FLOOR ALONE CANNOT SAY WHICH TESTS THEY WERE. A count is satisfied by any 372 tests,
        # so one named test per module is required by name as well: if HouseForgeEditor fails to load,
        # its canary is simply absent and the count could still be met by the runtime module growing.
        # These are ordinary tests, not markers - they are named here because losing them means losing
        # everything beside them.
        foreach ($Canary in $Canaries) {
            if (-not ($Report.tests | Where-Object { $_.fullTestPath -eq $Canary })) {
                Write-Host ''
                Write-Host "GATE FAILED: '$Canary' is not in the report. It is named here as the canary for its module, so its absence means that module's tests did not run at all." -ForegroundColor Red
                exit 1
            }
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
        #
        # FALSIFIED, as an A/B on one run each, stage 3 forced onto -nullrhi so all three sentinel
        # tests genuinely skip. Same three skips both times; the only difference is how they say so:
        #   with the sentinel  - "GATE FAILED: 3 measurement(s) were skipped in the stage that exists
        #                        to take them", naming Capture.APlanIsOrientedTheWayItSays,
        #                        Editor.Panel.SurfacesEditReachesTheRenderer and Editor.Panel.TabSpawns.
        #   with the AddInfo   - "Every pixel measurement was actually taken", GATE PASSED, exit 0.
        # That second line is what this gate printed for ten milestones.
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

        # AND SAY WHY, BECAUSE THIS MESSAGE HAS TWO VERY DIFFERENT CAUSES AND ONLY ONE OF THEM IS
        # BENIGN. The note at $ReportDir explains the first: a concurrent gate cleared the directory
        # underneath this one, which is a scheduling problem and nothing to do with the code.
        #
        # The second is that the editor CRASHED before the queue drained, which is a real defect and
        # was being reported here as though it were the first. Seen: an access violation in
        # UE::Geometry::FMeshBevel::ComputeUVs, reached from FHFMeshOps::BevelConvexEdges while a
        # test built the reference flat - a hard crash in the bevel's UV path that did not reproduce
        # on the next run. Intermittent, which is exactly the kind that gets waved through as "the
        # gate was flaky" when nobody is shown the callstack.
        #
        # So the crash is surfaced here rather than left in a log nobody opens. The engine rolls its
        # log as HouseBuilder.log, HouseBuilder_2.log and so on, and a second gate on this machine
        # writes into the same set - so this is a POINTER at a callstack to go and read, not proof
        # that the crash was this run's. It says so.
        #
        # FALSIFIED, both arms, against the log of the crash described above:
        #   with the crash lines present - fires, naming "Fatal error!" and "Unhandled Exception:
        #                                 EXCEPTION_ACCESS_VIOLATION reading address 0x...02".
        #   the same log, only those lines removed - quiet, and reports the concurrent-run case.
        # A detector that fires either way would be worse than none, so both directions were run.
        $Crash = Get-ChildItem (Join-Path $ProjectDir 'Saved\Logs\HouseBuilder*.log') -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 5 |
            Select-String -Pattern 'Unhandled Exception|Assertion failed|Fatal error!' |
            Select-Object -First 3

        if ($Crash) {
            Write-Host 'An editor CRASH is in the recent logs - which is a defect, not a scheduling clash:' -ForegroundColor Red
            foreach ($c in $Crash) {
                Write-Host "    $(Split-Path $c.Path -Leaf): $($c.Line.Trim())" -ForegroundColor Red
            }
            Write-Host "    Callstack in $ProjectDir\Saved\Logs. Check the timestamp - a concurrent gate writes here too." -ForegroundColor Red
        }
        else {
            Write-Host 'No crash in the recent logs, so a concurrent gate run most likely cleared the directory.' -ForegroundColor Yellow
        }

        exit 1
    }

    if ($StageExit -ne 0) {
        Write-Host ''
        Write-Host "GATE FAILED: editor returned $StageExit" -ForegroundColor Red
        exit $StageExit
    }
}

# The floor and the canaries only apply when the gate is running the whole suite. A narrow
# -TestFilter is a developer iterating on one area, and failing that for being small would just teach
# everyone to pass -SkipBuild and ignore the exit code.
$FullSuite = $TestFilter -eq 'HouseForge'

# ONE CANARY PER MODULE, and each is a real test that measures something structural:
#   HouseForge      (Runtime) - the sample spec against the code that defines it.
#   HouseForgeEditor(Editor)  - the whole flat built and walked from the front door.
# If either module fails to load, its canary vanishes from the report while everything else about
# that report stays consistent. That is the failure the count alone cannot name.
#
# FALSIFIED, and this is the case the floor above cannot catch. Run as the full suite against
# 'HouseForge.Model' - the runtime module alone, the editor module contributing nothing, which is
# exactly what losing HouseForgeEditor looks like. 34 tests, all Success, report self-consistent,
# and with -MinTests lowered enough to clear the floor the only thing that stopped it was:
#   "GATE FAILED: 'HouseForge.Flat.EveryRoomIsReachableFromTheFrontDoor' is not in the report."
$Canaries = @(
    'HouseForge.Model.SampleSpecFileInSync',
    'HouseForge.Flat.EveryRoomIsReachableFromTheFrontDoor'
)

Invoke-TestStage -Label "Stage 2/4  Test  automation filter '$TestFilter'" `
                 -Filter  $TestFilter `
                 -Reports $ReportDir `
                 -RhiArgs @('-nullrhi') `
                 -EvidenceKey 'suite' `
                 -Minimum $(if ($FullSuite) { $MinTests } else { 0 }) `
                 -Canaries $(if ($FullSuite) { $Canaries } else { @() })

# A REAL RHI, AND NOT A WINDOW. -AllowCommandletRendering gives an unattended process a working
# renderer without a visible editor, which is what lets the gate assert the one thing -nullrhi can
# never see: what the material actually draws.
Invoke-TestStage -Label "Stage 3/4  Test  pixel measurements, with a renderer ('$PixelFilter')" `
                 -Filter  $PixelFilter `
                 -Reports $PixelReportDir `
                 -RhiArgs @('-AllowCommandletRendering') `
                 -EvidenceKey 'pixels' `
                 -Minimum $MinPixelTests `
                 -RequireMeasured

# ------------------------------------------------------------------ stage 4: does light arrive
#
# THE ONLY STAGE THAT MEASURES LIGHT, AND FOR TEN MILESTONES IT WAS NOT IN THE GATE AT ALL.
#
# hf_lumen_measure.py::verdict describes itself as "the third of the milestone's three tests" and it
# is the only instrument in this repo that asserts indirect light actually ARRIVES - a 1.5x floor on
# the shadowed wall baked against live, plus the surface-cache pink fraction. It was reachable only
# by running Scripts/hf-lumen.ps1 by hand, and the string 'hf-lumen' appeared nowhere in this file or
# in hf-merge.ps1.
#
# Everything stages 2 and 3 check about Lumen is settings-level, because that is all they can reach:
# DistanceFieldResolutionScale non-zero, an orthogonal transform, a face over the card threshold. A
# regression those settings cannot see - card placement, material classification, a change of tracing
# path, or the surface cache simply not being captured - passes both stages without a murmur. And the
# reason that matters more here than it would anywhere else is the measured asymmetry this whole
# milestone turns on: the BROKEN configuration renders BRIGHTER than the correct one, 0.662 against
# 0.104 whole-frame, because unoccluded sky floods through walls Lumen cannot see. A broken render
# looks bright and cheerful, so there is no version of "look at it and see" that survives.
#
# It needs a GPU and about twenty minutes. That is the argument for -SkipLumen, and hf-merge.ps1
# deliberately does not pass it.
if ($SkipLumen) {
    Write-Stage 'Stage 4/4  Lumen  SKIPPED (-SkipLumen)'
    Write-Host 'Nothing has measured whether indirect light arrives. Do not record this run as gate evidence for a merge.' -ForegroundColor Yellow
    $Evidence.lumen = 'SKIPPED (-SkipLumen) - NOTHING MEASURED WHETHER LIGHT ARRIVES'
}
else {
    Write-Stage 'Stage 4/4  Lumen  does indirect light actually arrive in a baked flat'

    $LumenScript = Join-Path $PSScriptRoot 'hf-lumen.ps1'
    Assert-Exists $LumenScript 'hf-lumen.ps1'

    & $LumenScript -EngineDir $EngineDir
    $LumenExit = $LASTEXITCODE

    if ($LumenExit -ne 0) {
        Write-Host ''
        Write-Host "GATE FAILED: the Lumen stage returned $LumenExit. Either the flat is not reaching the Lumen scene when baked, or indirect light is not arriving on the shadowed wall. Images and numbers in $PluginDir\Saved\Review\lumen-baked." -ForegroundColor Red
        exit $LumenExit
    }

    # The measured numbers, not just a verdict. hf_lumen_measure.py writes them beside the images.
    $Ratio = 'measured'
    $MeasurePath = Join-Path $PluginDir 'Saved\Review\lumen-baked\measurements.json'
    if (Test-Path $MeasurePath) {
        $M = Get-Content $MeasurePath -Raw | ConvertFrom-Json
        $Live  = $M.'B-live-hardware'.mbed.wall_left
        $Baked = $M.'D-baked-hardware'.mbed.wall_left
        if ($Live -gt 0) { $Ratio = ('shadowed wall {0:N3} baked against {1:N3} live, {2:N2}x' -f $Baked, $Live, ($Baked / $Live)) }
    }

    Write-Host 'Indirect light arrives, and it arrives because of the bake' -ForegroundColor Green
    $Evidence.lumen = $Ratio
}
Write-Evidence

Write-Host ''
Write-Host 'GATE PASSED' -ForegroundColor Green
exit 0
