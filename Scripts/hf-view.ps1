<#
.SYNOPSIS
    Runs a HouseForge python script headlessly, with a renderer, and returns its output.

.DESCRIPTION
    "Render it and look" is the only way most of this plugin's defects have ever been found, so it
    has to be one command rather than a rediscovery every session. Three things about the
    invocation are load-bearing and none of them is obvious:

      -run=pythonscript      is the only route that gets GEditor. ApplySpecJson refuses without an
                             editor world, and -ExecutePythonScript quits before the script runs.
      -AllowCommandletRendering  a commandlet is -nullrhi by default, and CaptureView refuses to
                             draw without an RHI - correctly, and with a clear message.
      forward slashes        the script path is re-parsed, and a Windows backslash path is a line
                             continuation error inside Python.

    The script should import Scripts/hf_view.py for build, shoot, emit and done.

.PARAMETER Script
    Path to the python file to run. Absolute, or relative to the current directory.

.PARAMETER Quiet
    Suppress the HFDATA / HF SHOT summary; the log file still has everything.

.EXAMPLE
    .\hf-view.ps1 -Script Saved\tools\probe.py
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Script,
    [switch] $Quiet,
    [string] $EngineDir = 'd:\EpicGames\Engine\UE_5.8'
)

$ErrorActionPreference = 'Continue'

$PluginDir = Split-Path -Parent $PSScriptRoot
$ProjectDir = Split-Path -Parent (Split-Path -Parent $PluginDir)
$UProject = Join-Path $ProjectDir 'HouseBuilder.uproject'
$EditorCmd = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

$ScriptPath = (Resolve-Path $Script).Path -replace '\\', '/'
$LogDir = Join-Path $PluginDir 'Saved\tools'
if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir -Force | Out-Null }
$Log = Join-Path $LogDir ("{0}.log" -f [System.IO.Path]::GetFileNameWithoutExtension($Script))

& $EditorCmd $UProject -run=pythonscript -script="$ScriptPath" -AllowCommandletRendering `
    -unattended -nopause -nosplash -stdout -FullStdOutLogOutput *> $Log

Write-Host "exit=$LASTEXITCODE log=$Log"

if (-not $Quiet) {
    Select-String -Path $Log -Pattern 'HFDATA|HF BUILD|HF SHOT|HF DONE|LogPython: Error' |
        ForEach-Object { $_.Line }
}
