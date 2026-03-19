#!/usr/bin/env pwsh
$scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$dataRoot = Join-Path $scriptDir "data_root"
$server = Join-Path $scriptDir "bin/stdiolink_server.exe"

if (-not (Test-Path -LiteralPath $server)) {
    Write-Error "Server binary not found: $server"
    exit 1
}

Write-Host "Starting stdiolink_server..."
Write-Host "  data_root : $dataRoot"
Write-Host "  args      : $args"
$serverArgs = @("--data-root=$dataRoot")
if ($Host.Name -eq "ConsoleHost") {
    $serverArgs += "--attach-console"
}
$serverArgs += $args
& $server @serverArgs
