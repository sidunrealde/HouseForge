<#
.SYNOPSIS
    Regenerates the reference 2BHK drawing set.

.DESCRIPTION
    Provisions a local Python environment for Pillow (Scripts/.venv, gitignored - nothing is
    installed system-wide) and runs gen_sample_drawings.py.

    SVG is written with no dependencies at all and is the diffable source of truth; PNG is what
    Claude reads back when rebuilding the house from the drawings. Both come from one canvas, so
    they cannot disagree.

    Re-export the spec first if the layout changed in C++:
        UnrealEditor-Cmd HouseBuilder.uproject -ExecCmds="HouseForge.ExportSampleSpec" ...

.PARAMETER SvgOnly
    Skip PNG rendering and the venv entirely. Useful on a machine with no network.

.EXAMPLE
    .\hf-drawings.ps1
#>
[CmdletBinding()]
param(
    [switch] $SvgOnly
)

# Native tool stderr must not abort the script - see the note in hf-validate.ps1.
$ErrorActionPreference = 'Continue'

$PluginDir = Split-Path -Parent $PSScriptRoot
$Script    = Join-Path $PSScriptRoot 'gen_sample_drawings.py'
$VenvDir   = Join-Path $PSScriptRoot '.venv'
$VenvPy    = Join-Path $VenvDir 'Scripts\python.exe'

function Fail([string] $Message) {
    Write-Host ''
    Write-Host "FAILED: $Message" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $Script)) { Fail "generator not found at $Script" }

$Python = 'python'

if (-not $SvgOnly) {
    if (-not (Test-Path $VenvPy)) {
        Write-Host 'Creating local Python environment for Pillow...' -ForegroundColor Cyan
        & python -m venv $VenvDir
        if ($LASTEXITCODE -ne 0) { Fail 'could not create the virtual environment. Is Python installed?' }
    }

    & $VenvPy -c "import PIL" 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'Installing Pillow into the local environment...' -ForegroundColor Cyan
        & $VenvPy -m pip install --quiet --disable-pip-version-check pillow
        if ($LASTEXITCODE -ne 0) {
            Fail 'could not install Pillow. Re-run with -SvgOnly to produce SVG without it.'
        }
    }

    $Python = $VenvPy
}

Write-Host ''
Write-Host 'Generating reference 2BHK drawing set...' -ForegroundColor Cyan

if ($SvgOnly) {
    & $Python $Script --svg-only
} else {
    & $Python $Script
}

if ($LASTEXITCODE -ne 0) { Fail "generator exited $LASTEXITCODE" }

Write-Host ''
Write-Host 'Drawings regenerated.' -ForegroundColor Green
exit 0
