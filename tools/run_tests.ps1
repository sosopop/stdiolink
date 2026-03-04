#!/usr/bin/env pwsh
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Show-Usage {
    @"
Usage:
  tools/run_tests.ps1 [options]

Options:
  --build-dir <dir>   Build directory (default: build)
  --config <type>     Build config: debug or release (default: auto-detect)
  --gtest             Run only GTest (C++) tests
  --smoke             Run only smoke tests (Python)
  --vitest            Run only Vitest (WebUI unit) tests
  --playwright        Run only Playwright (E2E) tests
  -h, --help          Show this help

If no test filter is specified, all four suites are executed.

Example:
  tools/run_tests.ps1
  tools/run_tests.ps1 --gtest
  tools/run_tests.ps1 --vitest --playwright
"@ | Write-Host
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = (Resolve-Path (Join-Path $scriptDir "..")).Path

$buildDir = "build"
$buildConfig = ""
$runGtest = $false
$runSmoke = $false
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
        "--config" {
            if ($i + 1 -ge $args.Count) {
                Write-Error "Missing value for --config"
                Show-Usage
                exit 1
            }
            $buildConfig = [string]$args[$i + 1]
            $i += 2
            continue
        }
        "--gtest" {
            $runGtest = $true
            $i += 1
            continue
        }
        "--smoke" {
            $runSmoke = $true
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
if (-not $runGtest -and -not $runSmoke -and -not $runVitest -and -not $runPlaywright) {
    $runGtest = $true
    $runSmoke = $true
    $runVitest = $true
    $runPlaywright = $true
}

# Auto-detect build config if not specified
if ([string]::IsNullOrEmpty($buildConfig)) {
    $absBase = if ([System.IO.Path]::IsPathRooted($buildDir)) { $buildDir } else { Join-Path $rootDir $buildDir }
    if (Test-Path (Join-Path $absBase "runtime_debug")) {
        $buildConfig = "debug"
    } elseif (Test-Path (Join-Path $absBase "runtime_release")) {
        $buildConfig = "release"
    } else {
        $buildConfig = "debug"
    }
}

if ([System.IO.Path]::IsPathRooted($buildDir)) {
    $binDir = Join-Path $buildDir "runtime_$buildConfig/bin"
    $rawBinDir = Join-Path $buildDir $buildConfig
} else {
    $binDir = Join-Path $rootDir (Join-Path $buildDir "runtime_$buildConfig/bin")
    $rawBinDir = Join-Path $rootDir (Join-Path $buildDir $buildConfig)
}

$webuiDir = Join-Path $rootDir "src/webui"
$smokeRunner = Join-Path $rootDir "src/smoke_tests/run_smoke.py"

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

# ── Smoke (Python) ────────────────────────────────────────────────────
if ($runSmoke) {
    Write-Host "=== Smoke (Python) ==="
    if (-not (Test-Path -LiteralPath $smokeRunner -PathType Leaf)) {
        Write-Host "SKIP: smoke runner not found at $smokeRunner"
        $failed++
        $failedNames += "Smoke (runner not found)"
    } else {
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
            Write-Host "SKIP: python interpreter not found"
            $failed++
            $failedNames += "Smoke (python not found)"
        } else {
            $oldBinDirEnv = $env:STDIOLINK_BIN_DIR
            $env:STDIOLINK_BIN_DIR = $rawBinDir
            try {
                & $pythonExe $smokeRunner --plan all
                if ($LASTEXITCODE -ne 0) {
                    Write-Host "FAIL: Smoke failed (exit code $LASTEXITCODE)"
                    $failed++
                    $failedNames += "Smoke"
                } else {
                    Write-Host "  Smoke passed."
                    $passed++
                }
            } finally {
                if ($null -eq $oldBinDirEnv) {
                    Remove-Item Env:STDIOLINK_BIN_DIR -ErrorAction SilentlyContinue
                } else {
                    $env:STDIOLINK_BIN_DIR = $oldBinDirEnv
                }
            }
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
