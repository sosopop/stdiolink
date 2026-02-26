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
$failed = 0
$failedNames = @()

# ── GTest (C++) ───────────────────────────────────────────────────────
if ($runGtest) {
    Write-Host "=== GTest (C++) ==="
    $gtestBin = Join-Path $binDir "stdiolink_tests.exe"
    if (-not (Test-Path -LiteralPath $gtestBin -PathType Leaf)) {
        Write-Host "SKIP: GTest binary not found at $gtestBin"
        $failed++
        $failedNames += "GTest (binary not found)"
    } else {
        & $gtestBin
        if ($LASTEXITCODE -ne 0) {
            Write-Host "FAIL: GTest failed (exit code $LASTEXITCODE)"
            $failed++
            $failedNames += "GTest"
        } else {
            Write-Host "  GTest passed."
            $passed++
        }
    }
}

# ── Vitest (WebUI unit) ──────────────────────────────────────────────
if ($runVitest) {
    Write-Host "=== Vitest (WebUI unit tests) ==="
    if (-not (Test-Path -LiteralPath (Join-Path $webuiDir "node_modules") -PathType Container)) {
        Write-Host "SKIP: node_modules not found in $webuiDir"
        $failed++
        $failedNames += "Vitest (node_modules not found)"
    } elseif (-not $npmExe) {
        Write-Host "SKIP: npm not found"
        $failed++
        $failedNames += "Vitest (npm not found)"
    } else {
        Push-Location $webuiDir
        try {
            & $npmExe run test
            if ($LASTEXITCODE -ne 0) {
                Write-Host "FAIL: Vitest failed (exit code $LASTEXITCODE)"
                $failed++
                $failedNames += "Vitest"
            } else {
                Write-Host "  Vitest passed."
                $passed++
            }
        } finally {
            Pop-Location
        }
    }
}

# ── Playwright (E2E) ─────────────────────────────────────────────────
if ($runPlaywright) {
    Write-Host "=== Playwright (E2E tests) ==="
    if (-not (Test-Path -LiteralPath (Join-Path $webuiDir "node_modules") -PathType Container)) {
        Write-Host "SKIP: node_modules not found in $webuiDir"
        $failed++
        $failedNames += "Playwright (node_modules not found)"
    } elseif (-not $npxExe) {
        Write-Host "SKIP: npx not found"
        $failed++
        $failedNames += "Playwright (npx not found)"
    } else {
        Push-Location $webuiDir
        try {
            & $npxExe playwright install chromium
            if ($LASTEXITCODE -ne 0) {
                Write-Host "FAIL: Playwright browser install failed (exit code $LASTEXITCODE)"
                $failed++
                $failedNames += "Playwright (browser install)"
            } else {
                & $npxExe playwright test
                if ($LASTEXITCODE -ne 0) {
                    Write-Host "FAIL: Playwright tests failed (exit code $LASTEXITCODE)"
                    $failed++
                    $failedNames += "Playwright"
                } else {
                    Write-Host "  Playwright passed."
                    $passed++
                }
            }
        } finally {
            Pop-Location
        }
    }
}

Write-Host ""
if ($failed -gt 0) {
    Write-Host "=== $passed passed, $failed failed ==="
    Write-Host "Failed suites:"
    foreach ($name in $failedNames) {
        Write-Host "  - $name"
    }
    exit 1
} else {
    Write-Host "=== All $passed test suite(s) passed ==="
}
