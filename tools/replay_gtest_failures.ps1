#!/usr/bin/env pwsh
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Show-Usage {
    @"
Usage:
  tools/replay_gtest_failures.ps1 [options]

Options:
  --build-dir <dir>      Build directory (default: build)
  --config <type>        Build config: debug or release (default: auto-detect)
  --binary <path>        Explicit stdiolink_tests binary path
  --filter <expr>        Direct gtest filter expression
  --target <Suite.Test>  Target test name used to discover adjacent tests
  --source-file <path>   Source file for adjacent discovery (optional if --target is unique in src/tests)
  --adjacent <n>         Include n neighboring tests on each side (default: 0)
  --repeat <n>           GTest repeat count (default: 50)
  --shuffle              Enable --gtest_shuffle
  --seed <n>             Set --gtest_random_seed
  --log-dir <dir>        Failure log directory (default: logs/gtest_replay/<timestamp>)
  -h, --help             Show this help

Examples:
  tools/replay_gtest_failures.ps1 --target BinScanOrchestratorServiceTest.T06_CraneWaitTimeout --source-file src/tests/test_bin_scan_orchestrator_service.cpp --adjacent 1 --repeat 100
  tools/replay_gtest_failures.ps1 --filter BinScanOrchestratorServiceTest.T05_*:BinScanOrchestratorServiceTest.T06_*:BinScanOrchestratorServiceTest.T07_* --repeat 100 --shuffle
"@ | Write-Host
}

function Resolve-RootedPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BaseDir,
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return (Resolve-Path -LiteralPath $Path).Path
    }
    return (Resolve-Path -LiteralPath (Join-Path $BaseDir $Path)).Path
}

function Get-GtestTestsFromSourceFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $tests = New-Object System.Collections.Generic.List[object]
    $lines = Get-Content -Path $Path -Encoding UTF8
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match '^\s*TEST(?:_F|_P)?\s*\(\s*([A-Za-z0-9_:]+)\s*,\s*([A-Za-z0-9_]+)\s*\)') {
            $tests.Add([pscustomobject]@{
                    Suite   = $matches[1]
                    Test    = $matches[2]
                    FullName = "$($matches[1]).$($matches[2])"
                    Line    = $i + 1
                })
        }
    }
    return $tests
}

function Find-SourceFileForTarget {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RootDir,
        [Parameter(Mandatory = $true)]
        [string]$Target
    )

    $targetTest = $Target
    if ($Target.Contains(".")) {
        $parts = $Target.Split(".", 2)
        $targetTest = $parts[1]
    }

    $rg = Get-Command rg -ErrorAction SilentlyContinue
    if (-not $rg) {
        throw "ripgrep (rg) is required to auto-discover --source-file. Please install rg or pass --source-file."
    }

    $matches = @(& $rg.Source -l --fixed-strings $targetTest (Join-Path $RootDir "src/tests"))
    $matches = $matches | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    if ($matches.Count -eq 0) {
        throw "Could not locate source file for target '$Target' under src/tests. Please pass --source-file."
    }
    if ($matches.Count -gt 1) {
        throw "Target '$Target' matched multiple source files. Please pass --source-file explicitly.`n$($matches -join "`n")"
    }
    return (Resolve-Path -LiteralPath $matches[0]).Path
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = (Resolve-Path (Join-Path $scriptDir "..")).Path

$buildDir = "build"
$buildConfig = ""
$binaryPath = ""
$filter = ""
$target = ""
$sourceFile = ""
$adjacent = 0
$repeat = 50
$shuffle = $false
$seed = 0
$hasSeed = $false
$logDir = ""

$requestedBuildDir = $buildDir
$requestedBuildConfig = $buildConfig
$requestedBinaryPath = $binaryPath
$requestedFilter = $filter
$requestedTarget = $target
$requestedSourceFile = $sourceFile
$requestedAdjacent = $adjacent
$requestedRepeat = $repeat
$requestedShuffle = $shuffle
$requestedSeed = ""
$requestedLogDir = $logDir

for ($i = 0; $i -lt $args.Count; ) {
    $arg = [string]$args[$i]
    switch ($arg) {
        "--build-dir" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --build-dir" }
            $buildDir = [string]$args[$i + 1]
            $requestedBuildDir = $buildDir
            $i += 2
            continue
        }
        "--config" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --config" }
            $buildConfig = [string]$args[$i + 1]
            $requestedBuildConfig = $buildConfig
            $i += 2
            continue
        }
        "--binary" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --binary" }
            $binaryPath = [string]$args[$i + 1]
            $requestedBinaryPath = $binaryPath
            $i += 2
            continue
        }
        "--filter" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --filter" }
            $filter = [string]$args[$i + 1]
            $requestedFilter = $filter
            $i += 2
            continue
        }
        "--target" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --target" }
            $target = [string]$args[$i + 1]
            $requestedTarget = $target
            $i += 2
            continue
        }
        "--source-file" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --source-file" }
            $sourceFile = [string]$args[$i + 1]
            $requestedSourceFile = $sourceFile
            $i += 2
            continue
        }
        "--adjacent" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --adjacent" }
            $adjacent = [int]$args[$i + 1]
            $requestedAdjacent = $adjacent
            $i += 2
            continue
        }
        "--repeat" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --repeat" }
            $repeat = [int]$args[$i + 1]
            $requestedRepeat = $repeat
            $i += 2
            continue
        }
        "--shuffle" {
            $shuffle = $true
            $requestedShuffle = $shuffle
            $i += 1
            continue
        }
        "--seed" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --seed" }
            $seed = [int]$args[$i + 1]
            $hasSeed = $true
            $requestedSeed = [string]$seed
            $i += 2
            continue
        }
        "--log-dir" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --log-dir" }
            $logDir = [string]$args[$i + 1]
            $requestedLogDir = $logDir
            $i += 2
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
            throw "Unknown option: $arg"
        }
    }
}

if ($adjacent -lt 0) {
    throw "--adjacent must be >= 0"
}
if ($repeat -le 0) {
    throw "--repeat must be > 0"
}
if (-not [string]::IsNullOrWhiteSpace($filter) -and -not [string]::IsNullOrWhiteSpace($target)) {
    throw "Use either --filter or --target, not both."
}
if ([string]::IsNullOrWhiteSpace($filter) -and [string]::IsNullOrWhiteSpace($target)) {
    throw "One of --filter or --target is required."
}

if ([string]::IsNullOrWhiteSpace($binaryPath)) {
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

    $baseDir = if ([System.IO.Path]::IsPathRooted($buildDir)) { $buildDir } else { Join-Path $rootDir $buildDir }
    $binaryPath = Join-Path $baseDir "runtime_$buildConfig/bin/stdiolink_tests.exe"
}

if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) {
    throw "GTest binary not found: $binaryPath"
}
$binaryPath = (Resolve-Path -LiteralPath $binaryPath).Path
$resolvedBuildDir = if ([System.IO.Path]::IsPathRooted($buildDir)) {
    $buildDir
} else {
    Join-Path $rootDir $buildDir
}
if (Test-Path -LiteralPath $resolvedBuildDir) {
    $resolvedBuildDir = (Resolve-Path -LiteralPath $resolvedBuildDir).Path
}

$selectedTests = @()
$resolvedSourceFile = ""
if (-not [string]::IsNullOrWhiteSpace($target)) {
    if (-not $target.Contains(".")) {
        throw "--target must use the form Suite.Test"
    }

    if ([string]::IsNullOrWhiteSpace($sourceFile)) {
        $resolvedSourceFile = Find-SourceFileForTarget -RootDir $rootDir -Target $target
    } else {
        $resolvedSourceFile = Resolve-RootedPath -BaseDir $rootDir -Path $sourceFile
    }

    $tests = @(Get-GtestTestsFromSourceFile -Path $resolvedSourceFile)
    if ($tests.Count -eq 0) {
        throw "No TEST/TEST_F declarations found in $resolvedSourceFile"
    }

    $index = -1
    for ($j = 0; $j -lt $tests.Count; $j++) {
        if ($tests[$j].FullName -eq $target) {
            $index = $j
            break
        }
    }
    if ($index -lt 0) {
        throw "Target '$target' was not found in $resolvedSourceFile"
    }

    $start = [Math]::Max(0, $index - $adjacent)
    $end = [Math]::Min($tests.Count - 1, $index + $adjacent)
    $selectedTests = @($tests[$start..$end] | ForEach-Object { $_.FullName })
    $filter = ($selectedTests | Select-Object -Unique) -join ":"
} else {
    $selectedTests = @($filter.Split(":") | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
if ([string]::IsNullOrWhiteSpace($logDir)) {
    $logDir = Join-Path $rootDir "logs/gtest_replay/$timestamp"
} elseif (-not [System.IO.Path]::IsPathRooted($logDir)) {
    $logDir = Join-Path $rootDir $logDir
}
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$logDir = (Resolve-Path -LiteralPath $logDir).Path

$outputPath = Join-Path $logDir "gtest_brief_output.log"
$failurePath = Join-Path $logDir "failures.log"

$gtestArgs = @(
    "--gtest_color=no",
    "--gtest_brief=1",
    "--gtest_print_time=1",
    "--gtest_repeat=$repeat",
    "--gtest_filter=$filter"
)
if ($shuffle) {
    $gtestArgs += "--gtest_shuffle"
}
if ($hasSeed) {
    $gtestArgs += "--gtest_random_seed=$seed"
}

Write-Host "GTest binary: $binaryPath"
Write-Host "Filter: $filter"
Write-Host "Repeat: $repeat"
if ($selectedTests.Count -gt 0) {
    Write-Host "Selected tests:"
    foreach ($name in $selectedTests) {
        Write-Host "  - $name"
    }
}
if (-not [string]::IsNullOrWhiteSpace($resolvedSourceFile)) {
    Write-Host "Source file: $resolvedSourceFile"
}
Write-Host "Failure log dir: $logDir"

$sw = [System.Diagnostics.Stopwatch]::StartNew()
if ($PSVersionTable.PSVersion.Major -ge 7) {
    $oldPSNativeCommandUseErrorActionPreference = $PSNativeCommandUseErrorActionPreference
    $PSNativeCommandUseErrorActionPreference = $false
}
try {
    & $binaryPath @gtestArgs *> $outputPath
    $exitCode = $LASTEXITCODE
} finally {
    if ($PSVersionTable.PSVersion.Major -ge 7) {
        $PSNativeCommandUseErrorActionPreference = $oldPSNativeCommandUseErrorActionPreference
    }
}
$sw.Stop()

$outputText = ""
if (Test-Path -LiteralPath $outputPath) {
    $outputText = Get-Content -Path $outputPath -Raw -Encoding UTF8
}

$hasFailureText = $outputText.Contains("[  FAILED  ]") -or $outputText.Contains("FAILED TEST")
if ($exitCode -ne 0 -or $hasFailureText) {
    $header = @(
        "timestamp=$timestamp",
        "duration_ms=$($sw.ElapsedMilliseconds)",
        "exit_code=$exitCode",
        "requested_build_dir=$requestedBuildDir",
        "effective_build_dir=$resolvedBuildDir",
        "requested_config=$requestedBuildConfig",
        "effective_config=$buildConfig",
        "requested_binary=$requestedBinaryPath",
        "binary=$binaryPath",
        "requested_filter=$requestedFilter",
        "filter=$filter",
        "requested_target=$requestedTarget",
        "target=$target",
        "requested_source_file=$requestedSourceFile",
        "source_file=$resolvedSourceFile",
        "requested_adjacent=$requestedAdjacent",
        "adjacent=$adjacent",
        "requested_repeat=$requestedRepeat",
        "repeat=$repeat",
        "requested_shuffle=$requestedShuffle",
        "shuffle=$shuffle",
        "requested_seed=$requestedSeed",
        "seed=" + ($(if ($hasSeed) { $seed } else { "" })),
        "requested_log_dir=$requestedLogDir",
        "log_dir=$logDir",
        "selected_tests=$($selectedTests -join ',')",
        "gtest_args=$($gtestArgs -join ' ')",
        "command=$binaryPath $($gtestArgs -join ' ')",
        ""
    )
    Set-Content -Path $failurePath -Value ($header -join [Environment]::NewLine) -Encoding UTF8
    Add-Content -Path $failurePath -Value $outputText -Encoding UTF8
    Write-Host "Failure details saved to $failurePath"
    exit $exitCode
}

if (Test-Path -LiteralPath $outputPath) {
    Remove-Item -LiteralPath $outputPath -Force
}
if ((Test-Path -LiteralPath $logDir) -and -not (Get-ChildItem -LiteralPath $logDir -Force)) {
    Remove-Item -LiteralPath $logDir -Force
}
Write-Host "No failures captured in $repeat repetition(s)."
