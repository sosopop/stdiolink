#!/usr/bin/env pwsh
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$releasePy = Join-Path $scriptDir "release.py"

if (-not (Test-Path -LiteralPath $releasePy -PathType Leaf)) {
    Write-Error "release.py not found at $releasePy"
    exit 1
}

$pythonExe = $null
$python3Cmd = Get-Command python3 -ErrorAction SilentlyContinue
if ($python3Cmd) {
    $pythonExe = $python3Cmd.Source
} else {
    $pythonCmd = Get-Command python -ErrorAction SilentlyContinue
    if ($pythonCmd) {
        $pythonExe = $pythonCmd.Source
    }
}

if (-not $pythonExe) {
    Write-Error "python3/python not found in PATH"
    exit 1
}

& $pythonExe $releasePy @args
exit $LASTEXITCODE
