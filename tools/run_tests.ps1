#!/usr/bin/env pwsh
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Show-Usage {
    @"
Usage:
  tools/run_tests.ps1 [options]

Options:
  --build-dir <dir>   Build directory (default: build)
  --gtest             Run only GTest (C++) tests
  --vitest            Run only Vitest (WebUI unit) tests
  --playwright        Run only Playwright (E2E) tests
  -h, --help          Show this help

If no test filter is specified, all three suites are executed.

Example:
  tools/run_tests.ps1
  tools/run_tests.ps1 --gtest
  tools/run_tests.ps1 --vitest --playwright
"@ | Write-Host
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = (Resolve-Path (Join-Path $scriptDir "..")).Path

$buildDir = "build"
$runGtest = $false
$runVitest = $false
$runPlaywright = $false

for ($i = 0; $i -lt $args.Count; ) {
    $arg = [string]$args[$i]
    switch ($arg) {
        "--build-dir" {
            if ($i + 1 -ge $args.Count) {
                Write-Error "Missing value for --build-dir"
                Show-Usage
                exit 1
            }
            $buildDir = [string]$args[$i + 1]
            $i += 2
            continue
        }
        "--gtest" {
            $runGtest = $true
            $i += 1
            continue
        }
        "--vitest" {
            $runVitest = $true
            $i += 1
            continue
        }
        "--playwright" {
            $runPlaywright = $true
            $i += 1
            continue
        }
        "-h" {
            Show-Usage
            exit 0
        }
        "--help" {
            Show-Usage
            exit 0
        }
        default {
            Write-Error "Unknown option: $arg"
            Show-Usage
            exit 1
        }
    }
}

# If no filter specified, run all
if (-not $runGtest -and -not $runVitest -and -not $runPlaywright) {
    $runGtest = $true
    $runVitest = $true
    $runPlaywright = $true
}

if ([System.IO.Path]::IsPathRooted($buildDir)) {
    $binDir = Join-Path $buildDir "bin"
} else {
    $binDir = Join-Path $rootDir (Join-Path $buildDir "bin")
}

$webuiDir = Join-Path $rootDir "src/webui"

# Resolve npm.cmd / npx.cmd to bypass the npm.ps1 shim
$npmExe = $null
$npxExe = $null
$npmCmd = Get-Command npm -ErrorAction SilentlyContinue
if ($npmCmd) {
    $npmExe = Join-Path (Split-Path $npmCmd.Source) "npm.cmd"
    $npxExe = Join-Path (Split-Path $npmCmd.Source) "npx.cmd"
}

$passed = 0

# ── GTest (C++) ───────────────────────────────────────────────────────
if ($runGtest) {
    Write-Host "=== GTest (C++) ==="
    $gtestBin = Join-Path $binDir "stdiolink_tests.exe"
    if (-not (Test-Path -LiteralPath $gtestBin -PathType Leaf)) {
        Write-Error "GTest binary not found at $gtestBin. Build the project first or use --build-dir."
        exit 1
    }
    & $gtestBin
    if ($LASTEXITCODE -ne 0) {
        Write-Error "GTest failed (exit code $LASTEXITCODE)"
        exit 1
    }
    Write-Host "  GTest passed."
    $passed++
}

# ── Vitest (WebUI unit) ──────────────────────────────────────────────
if ($runVitest) {
    Write-Host "=== Vitest (WebUI unit tests) ==="
    if (-not (Test-Path -LiteralPath (Join-Path $webuiDir "node_modules") -PathType Container)) {
        Write-Error "node_modules not found in $webuiDir. Run 'npm ci' in src/webui first."
        exit 1
    }
    if (-not $npmExe) {
        Write-Error "npm not found. Install Node.js to run Vitest."
        exit 1
    }
    Push-Location $webuiDir
    try {
        & $npmExe run test 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Vitest failed (exit code $LASTEXITCODE)"
            exit 1
        }
    } finally {
        Pop-Location
    }
    Write-Host "  Vitest passed."
    $passed++
}

# ── Playwright (E2E) ─────────────────────────────────────────────────
if ($runPlaywright) {
    Write-Host "=== Playwright (E2E tests) ==="
    if (-not (Test-Path -LiteralPath (Join-Path $webuiDir "node_modules") -PathType Container)) {
        Write-Error "node_modules not found in $webuiDir. Run 'npm ci' in src/webui first."
        exit 1
    }
    if (-not $npxExe) {
        Write-Error "npx not found. Install Node.js to run Playwright."
        exit 1
    }
    Push-Location $webuiDir
    try {
        & $npxExe playwright install chromium 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Playwright browser install failed (exit code $LASTEXITCODE)"
            exit 1
        }
        & $npxExe playwright test 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Playwright tests failed (exit code $LASTEXITCODE)"
            exit 1
        }
    } finally {
        Pop-Location
    }
    Write-Host "  Playwright passed."
    $passed++
}

Write-Host ""
Write-Host "=== All $passed test suite(s) passed ==="
