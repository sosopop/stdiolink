#!/usr/bin/env pwsh
# validate_m87_migration.ps1 — M87 静态校验脚本 (Windows)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$DemoRoot = "src/demo/server_manager_demo/data_root"
$DataRoot = "src/data_root"
$RuntimeUtils = "src/demo/js_runtime_demo/shared/lib/runtime_utils.js"

function Fail($msg) { Write-Host "FAIL: $msg" -ForegroundColor Red; exit 1 }

# T01
Write-Host "T01: checking shared reference residuals..."
$hits = Select-String -Path "src/demo/server_manager_demo/**/*.js","src/data_root/**/*.js" `
    -Pattern "../../shared/" -ErrorAction SilentlyContinue
if ($hits) { Fail "found ../../shared/ references" }
Write-Host "OK: no shared references"

# T02
Write-Host "T02: checking shared directory deleted..."
if (Test-Path "$DemoRoot/shared") { Fail "shared/ directory still exists" }
Write-Host "OK: shared/ deleted"

# T03
Write-Host "T03: checking data_root structure..."
$services = @("modbustcp_server_service","modbusrtu_server_service","modbusrtu_serial_server_service")
$projects = @("manual_modbustcp_server","manual_modbusrtu_server","manual_modbusrtu_serial_server")
foreach ($svc in $services) {
    foreach ($f in @("manifest.json","config.schema.json","index.js")) {
        if (!(Test-Path "$DataRoot/services/$svc/$f")) { Fail "$svc/$f missing" }
    }
}
foreach ($prj in $projects) {
    if (!(Test-Path "$DataRoot/projects/$prj.json")) { Fail "$prj.json missing" }
}
Write-Host "OK: data_root structure complete"

# T04
Write-Host "T04: checking resolveDriver imports..."
$migrated = @(
    "$DemoRoot/services/driver_pipeline_service/index.js",
    "$DemoRoot/services/process_exec_service/index.js",
    "$DataRoot/services/modbustcp_server_service/index.js",
    "$DataRoot/services/modbusrtu_server_service/index.js",
    "$DataRoot/services/modbusrtu_serial_server_service/index.js"
)
foreach ($f in $migrated) {
    if (!(Select-String -Path $f -Pattern "stdiolink/driver" -Quiet)) {
        Fail "$f missing resolveDriver import"
    }
}
Write-Host "OK: all migrated files have resolveDriver import"

# T05
Write-Host "T05: checking no findDriverPath residuals..."
foreach ($f in $migrated) {
    if (Select-String -Path $f -Pattern "findDriverPath" -Quiet) {
        Fail "$f still contains findDriverPath"
    }
}
Write-Host "OK: no findDriverPath residuals"

# T06
Write-Host "T06: checking no inline findDriver..."
if (Select-String -Path "$DemoRoot/services/process_exec_service/index.js" `
    -Pattern "function findDriver" -Quiet) {
    Fail "inline findDriver still exists"
}
Write-Host "OK: no inline findDriver"

# T07
Write-Host "T07: checking no driverPathCandidates..."
if (Select-String -Path $RuntimeUtils -Pattern "driverPathCandidates" -Quiet) {
    Fail "driverPathCandidates still exists"
}
Write-Host "OK: no driverPathCandidates"

# T08
Write-Host "T08: checking firstSuccess preserved..."
if (!(Select-String -Path $RuntimeUtils -Pattern "export function firstSuccess" -Quiet)) {
    Fail "firstSuccess missing"
}
Write-Host "OK: firstSuccess preserved"

# T09
Write-Host "T09: checking demo services unaffected..."
$qs = "$DemoRoot/services/quick_start_service/index.js"
if (!(Test-Path $qs)) { Fail "quick_start missing" }
if (Select-String -Path $qs -Pattern "../../shared/" -Quiet) {
    Fail "quick_start has shared reference"
}
Write-Host "OK: demo services unaffected"

Write-Host "`nAll M87 static validations passed." -ForegroundColor Green
