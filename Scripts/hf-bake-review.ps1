<#
.SYNOPSIS
    Renders the milestone-12 review package: the bake, the guard, articulation and asset overrides,
    seen rather than asserted.

.DESCRIPTION
    Drives a REAL editor viewport, for the same reason hf-lumen.ps1 does: HouseForge's own
    FHFSceneCapture is a one-shot USceneCaptureComponent2D and runs no converged global illumination,
    so every interior it produces is lit by the ambient cubemap the rig pins rather than by the flat.
    Measured: the same baked flat captured through that path with Lumen on, with GI off, and with GI
    and the sky light both off gave three byte-identical PNGs. Judging a bake from one would be
    judging the cubemap.

    Three things about the invocation are load-bearing, all learned the expensive way by hf-lumen.ps1:

      UnrealEditor.exe        not -Cmd. The full editor is what has a level viewport for HighResShot
                              to render into; a commandlet has none, whatever rendering flags it gets.
      -RenderOffScreen        real frames with no window, so this runs unattended.
      Start-Process -Wait     UnrealEditor.exe is a WINDOWS-subsystem binary, so the PowerShell call
                              operator does NOT block on it - `&` returns instantly and the script
                              then collects zero images from a directory nothing has written to,
                              reporting success in about a second.
      ONE argument STRING     given an array, PowerShell re-quotes each element on its own rules and
                              `-ExecCmds=py <path>` arrives as two tokens; the engine then parses a
                              bare `py` with no argument and sits ticking an empty level, which looks
                              exactly like a slow render.

    Afterwards it runs hf_bake_review_compose.py, outside the editor, because reading PNGs needs
    numpy and PIL and the editor's embedded python has neither.

    THIS SCRIPT ASSERTS and exits non-zero. Two of the package's claims are claims about pixels - the
    broken configuration renders BRIGHTER, and clearing an asset override restores the picture
    EXACTLY - and a review package that only displays them can be wrong quietly.

.PARAMETER SkipRender
    Compose and measure the images already in Saved/Review/bake without re-rendering them.

.EXAMPLE
    .\hf-bake-review.ps1
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

$Script  = (Join-Path $PSScriptRoot 'hf_bake_review.py') -replace '\\', '/'
$OutDir  = Join-Path $PluginDir 'Saved\Review\bake'
$ShotDir = Join-Path $ProjectDir 'Saved\Screenshots\WindowsEditor'
$Log     = Join-Path $PluginDir 'Saved\tools\bake-review.log'

New-Item -ItemType Directory -Path (Split-Path -Parent $Log) -Force | Out-Null
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

if (-not $SkipRender) {
    # A stale shot of the same name would otherwise be collected as this run's evidence, and
    # HighResShot renames rather than overwrites - which is exactly how a comparison ends up made
    # against an image from a previous configuration.
    if (Test-Path $ShotDir) { Remove-Item (Join-Path $ShotDir '*.png') -Force -ErrorAction SilentlyContinue }

    Write-Host 'Rendering the review package. Two states, two views, five open amounts, three override steps.'

    $Arguments = '"{0}" -ExecCmds="py {1}" -RenderOffScreen -unattended -nopause -nosplash -stdout -FullStdOutLogOutput' -f $UProject, $Script

    $Process = Start-Process -FilePath $Editor -ArgumentList $Arguments `
        -Wait -NoNewWindow -PassThru -RedirectStandardOutput $Log

    Write-Host "editor exit=$($Process.ExitCode) log=$Log"

    # The proof that the argument quoting above is still right. If the python never ran there is
    # nothing in the log with this tag, and everything composed afterwards would be measured from
    # stale or absent images - which is worse than stopping, because it looks like data.
    if (-not (Select-String -Path $Log -Pattern 'HFBAKEREVIEW' -Quiet)) {
        Write-Error "The editor ran but hf_bake_review.py did not: no HFBAKEREVIEW line in $Log. Nothing was rendered."
        exit 1
    }

    Select-String -Path $Log -Pattern 'HFBAKEREVIEW|LogPython: Error' | ForEach-Object { $_.Line }

    $Moved = 0
    Get-ChildItem -Path $ShotDir -Filter '*.png' -ErrorAction SilentlyContinue | ForEach-Object {
        Move-Item $_.FullName (Join-Path $OutDir $_.Name) -Force
        $Moved++
    }
    Write-Host "collected $Moved image(s) into $OutDir"
}

$Python = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if (-not (Test-Path $Python)) { $Python = 'python' }

& $Python (Join-Path $PSScriptRoot 'hf_bake_review_compose.py') $OutDir
exit $LASTEXITCODE
