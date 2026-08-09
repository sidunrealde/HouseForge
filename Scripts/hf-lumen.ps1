<#
.SYNOPSIS
    Renders the flat under four Lumen configurations and measures how much light each one produces.

.DESCRIPTION
    The evidence half of milestone 12. HouseForge's own FHFSceneCapture cannot answer this question -
    a one-shot USceneCaptureComponent2D runs no converged global illumination and compensates with an
    ambient cubemap, so measuring indirect light through it measures the cubemap. This drives a REAL
    editor viewport instead, settles it for a couple of hundred frames per state change so Lumen
    converges, and captures with HighResShot.

    Three things about the invocation are load-bearing:

      UnrealEditor.exe        not -Cmd. The full editor is what has a level viewport for HighResShot
                              to render; a commandlet has none whatever rendering flags it is given.
      -RenderOffScreen        renders without a window, so this can run unattended on a machine
                              nobody is looking at, and the frames are real frames rather than
                              whatever a minimised window returns.
      -ExecCmds="py ..."      runs Scripts/hf_lumen.py at startup. The script registers a slate post
                              tick callback and returns; the editor then ticks it through the whole
                              matrix and quits itself at the end.

    Afterwards it runs hf_lumen_measure.py, outside the editor, because reading PNGs needs numpy and
    the editor's embedded python does not have it.

.PARAMETER SkipRender
    Measure the images already in Saved/Review/lumen-baked without re-rendering them.

.EXAMPLE
    .\hf-lumen.ps1
#>
[CmdletBinding()]
param(
    [switch] $SkipRender,
    [string] $EngineDir = 'd:\EpicGames\Engine\UE_5.8'
)

$ErrorActionPreference = 'Continue'

$PluginDir  = Split-Path -Parent $PSScriptRoot
$ProjectDir = Split-Path -Parent (Split-Path -Parent $PluginDir)
$UProject   = Join-Path $ProjectDir 'HouseBuilder.uproject'
$Editor     = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.exe'

$Script  = (Join-Path $PSScriptRoot 'hf_lumen.py') -replace '\\', '/'
$OutDir  = Join-Path $PluginDir 'Saved\Review\lumen-baked'
$ShotDir = Join-Path $ProjectDir 'Saved\Screenshots\WindowsEditor'
$Log     = Join-Path $PluginDir 'Saved\tools\lumen.log'

New-Item -ItemType Directory -Path (Split-Path -Parent $Log) -Force | Out-Null
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

if (-not $SkipRender) {
    # A stale shot of the same name would otherwise be collected as this run's evidence, and
    # HighResShot renames rather than overwrites - which is exactly how a comparison ends up made
    # against an image from a previous configuration.
    if (Test-Path $ShotDir) { Remove-Item (Join-Path $ShotDir '*.png') -Force -ErrorAction SilentlyContinue }

    Write-Host "Rendering. This takes a while: 4 configurations x 2 views x 3 passes, each settled for 200 frames."

    # TWO THINGS HERE ARE LOAD-BEARING, AND BOTH WERE LEARNED THE EXPENSIVE WAY.
    #
    # 1. Start-Process -Wait, not `& $Editor`. UnrealEditor.exe is a WINDOWS subsystem binary, not a
    #    console one, so a PowerShell call operator does not block on it: `&` returned instantly, the
    #    script collected zero images from a directory the editor had not written to yet, and the
    #    whole run reported success in about a second. An empty log and an empty $LASTEXITCODE are
    #    the signature of it.
    #
    # 2. ONE argument STRING, not an array. PowerShell passes a single-string -ArgumentList through
    #    to the command line verbatim; given an array it re-quotes each element on its own rules, and
    #    `-ExecCmds=py <path>` came out as two tokens. The engine then parsed -ExecCmds as a bare
    #    `py` with no argument, ran nothing, and sat there ticking an empty level until it was
    #    killed - which looks exactly like a slow render. The quotes below are inside the string
    #    deliberately so the engine's own parser sees `py <path>` as one value.
    $Arguments = '"{0}" -ExecCmds="py {1}" -RenderOffScreen -unattended -nopause -nosplash -stdout -FullStdOutLogOutput' -f $UProject, $Script

    $Process = Start-Process -FilePath $Editor -ArgumentList $Arguments `
        -Wait -NoNewWindow -PassThru -RedirectStandardOutput $Log

    Write-Host "editor exit=$($Process.ExitCode) log=$Log"

    # The proof that (2) is still true. If the python never ran there is nothing in the log with this
    # tag, and every number produced afterwards would be measured from stale or absent images - which
    # is a worse outcome than stopping, because it looks like data.
    if (-not (Select-String -Path $Log -Pattern 'HFLUMEN' -Quiet)) {
        Write-Error "The editor ran but hf_lumen.py did not: no HFLUMEN line in $Log. Nothing was measured."
        exit 1
    }

    Select-String -Path $Log -Pattern 'HFLUMEN|LogPython: Error' | ForEach-Object { $_.Line }

    $Moved = 0
    Get-ChildItem -Path $ShotDir -Filter '*.png' -ErrorAction SilentlyContinue | ForEach-Object {
        Move-Item $_.FullName (Join-Path $OutDir $_.Name) -Force
        $Moved++
    }
    Write-Host "collected $Moved image(s) into $OutDir"
}

$Python = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if (-not (Test-Path $Python)) { $Python = 'python' }

& $Python (Join-Path $PSScriptRoot 'hf_lumen_measure.py') $OutDir

# The measure step ASSERTS - an incomplete set, or a shadowed wall no brighter baked than live, is a
# failure of this script and not a footnote in its output. Passing the code through is what lets this
# be run from anything that checks one.
exit $LASTEXITCODE
