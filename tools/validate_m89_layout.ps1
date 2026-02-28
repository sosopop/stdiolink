#!/usr/bin/env pwsh
# tools/validate_m89_layout.ps1 — M89 静态校验脚本 (Windows)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$BuildDir = if ($args.Count -ge 1) { $args[0] } else { "build" }
$BuildType = if ($args.Count -ge 2) { $args[1] } else { "debug" }
$ReleaseDir = if ($args.Count -ge 3) { $args[2] } else { "" }
$RawDir = Join-Path $BuildDir $BuildType
$RuntimeDir = Join-Path $BuildDir "runtime_$BuildType"
$ExeSuffix = ".exe"

function Fail($msg) { Write-Host "FAIL: $msg" -ForegroundColor Red; exit 1 }

# T01
Write-Host "T01: checking raw output directory..."
if (!(Test-Path $RawDir)) { Fail "raw dir $RawDir not found" }
if (!(Test-Path (Join-Path $RawDir "stdiolink_server$ExeSuffix"))) { Fail "stdiolink_server not in raw dir" }
Write-Host "OK: raw output directory exists"

# T02
Write-Host "T02: checking build/bin/ removed..."
if (Test-Path (Join-Path $BuildDir "bin")) { Fail "build/bin/ still exists" }
Write-Host "OK: build/bin/ does not exist"

# T03
Write-Host "T03: checking runtime directory skeleton..."
foreach ($d in @("bin","data_root","data_root/drivers","data_root/services",
    "data_root/projects","data_root/workspaces","data_root/logs","demos","scripts")) {
    if (!(Test-Path (Join-Path $RuntimeDir $d))) { Fail "runtime_$BuildType/$d missing" }
}
Write-Host "OK: runtime skeleton complete"

# T04
Write-Host "T04: checking core binaries in runtime/bin/..."
foreach ($b in @("stdiolink_server","stdiolink_service","stdiolink_tests")) {
    if (!(Test-Path (Join-Path $RuntimeDir "bin/$b$ExeSuffix"))) { Fail "$b not in runtime/bin/" }
}
Write-Host "OK: core binaries present"

# T05
Write-Host "T05: checking no drivers in runtime/bin/..."
$drvInBin = Get-ChildItem -Path (Join-Path $RuntimeDir "bin") -Filter "stdio.drv.*" -File -ErrorAction SilentlyContinue
if ($drvInBin) { Fail "found stdio.drv.* in runtime/bin/" }
Write-Host "OK: no drivers in runtime/bin/"

# T06
Write-Host "T06: checking driver subdirectories..."
$drvCount = 0
foreach ($f in (Get-ChildItem -Path $RawDir -Filter "stdio.drv.*" -File)) {
    $stem = [IO.Path]::GetFileNameWithoutExtension($f.Name)
    $drvDir = Join-Path $RuntimeDir "data_root/drivers/$stem"
    if (!(Test-Path $drvDir)) { Fail "driver dir $stem/ missing" }
    if (!(Test-Path (Join-Path $drvDir $f.Name))) { Fail "driver binary $stem/$($f.Name) missing" }
    $drvCount++
}
if ($drvCount -eq 0) { Fail "no drivers found" }
Write-Host "OK: $drvCount driver(s) in subdirectories"

# T07
Write-Host "T07: checking services merge..."
if (!(Test-Path (Join-Path $RuntimeDir "data_root/services/modbustcp_server_service"))) { Fail "production service missing" }
if (!(Test-Path (Join-Path $RuntimeDir "data_root/services/driver_pipeline_service"))) { Fail "demo service missing" }
Write-Host "OK: services merged"

# T08
Write-Host "T08: checking projects merge..."
if (!(Test-Path (Join-Path $RuntimeDir "data_root/projects/manual_modbustcp_server.json"))) { Fail "production project missing" }
Write-Host "OK: projects merged"

# T09
Write-Host "T09: checking demo assets..."
if (!(Test-Path (Join-Path $RuntimeDir "demos/js_runtime_demo"))) { Fail "js_runtime_demo missing" }
if (!(Test-Path (Join-Path $RuntimeDir "demos/config_demo"))) { Fail "config_demo missing" }
Write-Host "OK: demo assets present"

# T10
Write-Host "T10: checking demo scripts..."
if (!(Test-Path (Join-Path $RuntimeDir "scripts/run_demo.sh"))) { Fail "run_demo.sh missing" }
if (!(Test-Path (Join-Path $RuntimeDir "scripts/api_smoke.sh"))) { Fail "api_smoke.sh missing" }
Write-Host "OK: demo scripts present"

# T11
Write-Host "T11: checking Qt plugins..."
if (Test-Path (Join-Path $RuntimeDir "bin/platforms")) {
    Write-Host "OK: Qt plugins present"
} else {
    Write-Host "SKIP: platforms/ not found (Qt plugins not deployed)"
}

# T12
Write-Host "T12: checking isomorphic layout..."
if ([string]::IsNullOrEmpty($ReleaseDir)) {
    if (!(Test-Path (Join-Path $RuntimeDir "bin"))) { Fail "runtime bin/ missing" }
    if (!(Test-Path (Join-Path $RuntimeDir "data_root"))) { Fail "runtime data_root/ missing" }
    $drvInBin2 = Get-ChildItem -Path (Join-Path $RuntimeDir "bin") -Filter "stdio.drv.*" -File -ErrorAction SilentlyContinue
    if ($drvInBin2) { Fail "found stdio.drv.* in runtime/bin/ (isomorphic violation)" }
    $drvSubdirs = Get-ChildItem -Path (Join-Path $RuntimeDir "data_root/drivers") -Directory -ErrorAction SilentlyContinue
    if (!$drvSubdirs -or $drvSubdirs.Count -eq 0) { Fail "data_root/drivers/ has no subdirectories" }
    Write-Host "OK: runtime self-consistent ($($drvSubdirs.Count) driver subdirs)"
} else {
    if (!(Test-Path $ReleaseDir)) { Fail "release dir $ReleaseDir not found" }
    # Filter out binaries that publish_release excludes by default (WITH_TESTS=0)
    $skipFilter = { param($n)
        $s = [IO.Path]::GetFileNameWithoutExtension($n)
        $l = $n.ToLowerInvariant()
        ($s -eq "stdiolink_tests" -or $s -like "test_*" -or $s -eq "gtest" -or $s -eq "demo_host" -or $s -eq "driverlab") -or
        ($l.EndsWith(".log") -or $l.EndsWith(".tmp") -or $l.EndsWith(".json"))
    }
    $rtBins = (Get-ChildItem -Path (Join-Path $RuntimeDir "bin") -File | Where-Object { -not (& $skipFilter $_.Name) } | ForEach-Object { $_.Name } | Sort-Object) -join ","
    $rlBins = (Get-ChildItem -Path (Join-Path $ReleaseDir "bin") -File | ForEach-Object { $_.Name } | Sort-Object) -join ","
    if ($rtBins -ne $rlBins) { Fail "bin/ file sets differ between runtime and release" }
    $rtDrvs = (Get-ChildItem -Path (Join-Path $RuntimeDir "data_root/drivers") -Directory | ForEach-Object { $_.Name } | Sort-Object) -join ","
    $rlDrvs = (Get-ChildItem -Path (Join-Path $ReleaseDir "data_root/drivers") -Directory | ForEach-Object { $_.Name } | Sort-Object) -join ","
    if ($rtDrvs -ne $rlDrvs) { Fail "data_root/drivers/ subdirectory sets differ" }
    $drvInRelBin = Get-ChildItem -Path (Join-Path $ReleaseDir "bin") -Filter "stdio.drv.*" -File -ErrorAction SilentlyContinue
    if ($drvInRelBin) { Fail "found stdio.drv.* in release bin/" }
    Write-Host "OK: isomorphic layout verified (runtime == release)"
}

Write-Host "`nAll M89 layout validations passed." -ForegroundColor Green
