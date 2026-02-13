#!/usr/bin/env pwsh
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Show-Usage {
    @"
Usage:
  tools/publish_release.ps1 [options]

Options:
  --build-dir <dir>    Build directory (default: build)
  --output-dir <dir>   Release output root (default: release)
  --name <name>        Package name (default: stdiolink_<timestamp>_<git>)
  --with-tests         Include test binaries in bin/
  -h, --help           Show this help

Example:
  tools/publish_release.ps1 --build-dir build --output-dir release
"@ | Write-Host
}

function Resolve-AbsolutePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$RootDir
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $RootDir $Path))
}

function Get-GitRev {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RootDir,
        [Parameter(Mandatory = $true)]
        [string[]]$RevArgs
    )

    try {
        $value = (& git -C $RootDir rev-parse @RevArgs 2>$null)
        if ($LASTEXITCODE -eq 0 -and $value) {
            return $value.Trim()
        }
    } catch {
    }
    return "unknown"
}

function Copy-FileIfExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,
        [Parameter(Mandatory = $true)]
        [string]$DestinationDir
    )

    if (Test-Path -LiteralPath $Source -PathType Leaf) {
        Copy-Item -LiteralPath $Source -Destination $DestinationDir -Force
    }
}

function Copy-DirClean {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,
        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }

    $parent = Split-Path -Parent $Destination
    if ($parent) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = (Resolve-Path (Join-Path $scriptDir "..")).Path

$buildDir = "build"
$outputDir = "release"
$packageName = ""
$withTests = $false

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
        "--output-dir" {
            if ($i + 1 -ge $args.Count) {
                Write-Error "Missing value for --output-dir"
                Show-Usage
                exit 1
            }
            $outputDir = [string]$args[$i + 1]
            $i += 2
            continue
        }
        "--name" {
            if ($i + 1 -ge $args.Count) {
                Write-Error "Missing value for --name"
                Show-Usage
                exit 1
            }
            $packageName = [string]$args[$i + 1]
            $i += 2
            continue
        }
        "--with-tests" {
            $withTests = $true
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

if ([string]::IsNullOrWhiteSpace($buildDir) -or [string]::IsNullOrWhiteSpace($outputDir)) {
    Write-Error "--build-dir and --output-dir cannot be empty"
    exit 1
}

$buildDirAbs = Resolve-AbsolutePath -Path $buildDir -RootDir $rootDir
$outputDirAbs = Resolve-AbsolutePath -Path $outputDir -RootDir $rootDir
$binDir = Join-Path $buildDirAbs "bin"

if (-not (Test-Path -LiteralPath $binDir -PathType Container)) {
    Write-Error "Build bin directory not found: $binDir"
    Write-Error "Please build project first, for example: build.bat Release"
    exit 1
}

if ([string]::IsNullOrWhiteSpace($packageName)) {
    $ts = Get-Date -Format "yyyyMMdd_HHmmss"
    $gitHash = Get-GitRev -RootDir $rootDir -RevArgs @("--short", "HEAD")
    if ($gitHash -eq "unknown") {
        $gitHash = "unknown"
    }
    $packageName = "stdiolink_${ts}_${gitHash}"
}

$packageDir = Join-Path $outputDirAbs $packageName

Write-Host "Preparing release package:"
Write-Host "  root        : $rootDir"
Write-Host "  build bin   : $binDir"
Write-Host "  output root : $outputDirAbs"
Write-Host "  package dir : $packageDir"
Write-Host "  with tests  : $([int][bool]$withTests)"

if (Test-Path -LiteralPath $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}

$dirsToCreate = @(
    "bin",
    "demo",
    "doc",
    "data_root/drivers",
    "data_root/services",
    "data_root/projects",
    "data_root/workspaces",
    "data_root/logs",
    "data_root/shared"
)

foreach ($relDir in $dirsToCreate) {
    $fullDir = Join-Path $packageDir $relDir
    New-Item -ItemType Directory -Path $fullDir -Force | Out-Null
}

function Should-SkipBinary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [bool]$WithTests
    )

    if (-not $WithTests) {
        $base = [System.IO.Path]::GetFileNameWithoutExtension($Name)
        if ($base -eq "stdiolink_tests" -or $base.StartsWith("test_") -or $base -eq "gtest") {
            return $true
        }
    }

    $lower = $Name.ToLowerInvariant()
    if ($lower.EndsWith(".log") -or $lower.EndsWith(".tmp") -or $lower.EndsWith(".json")) {
        return $true
    }

    return $false
}

Write-Host "Copying binaries..."
$binFiles = Get-ChildItem -LiteralPath $binDir -File
foreach ($file in $binFiles) {
    if (Should-SkipBinary -Name $file.Name -WithTests $withTests) {
        continue
    }
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $packageDir "bin") -Force
}

Write-Host "Copying demo assets..."
$binConfigDemo = Join-Path $binDir "config_demo"
$srcConfigServices = Join-Path $rootDir "src/demo/config_demo/services"
if (Test-Path -LiteralPath $binConfigDemo -PathType Container) {
    Copy-DirClean -Source $binConfigDemo -Destination (Join-Path $packageDir "demo/config_demo")
} elseif (Test-Path -LiteralPath $srcConfigServices -PathType Container) {
    New-Item -ItemType Directory -Path (Join-Path $packageDir "demo/config_demo") -Force | Out-Null
    Copy-DirClean -Source $srcConfigServices -Destination (Join-Path $packageDir "demo/config_demo/services")
}

$binJsRuntimeDemo = Join-Path $binDir "js_runtime_demo"
$srcJsServices = Join-Path $rootDir "src/demo/js_runtime_demo/services"
$srcJsShared = Join-Path $rootDir "src/demo/js_runtime_demo/shared"
if (Test-Path -LiteralPath $binJsRuntimeDemo -PathType Container) {
    Copy-DirClean -Source $binJsRuntimeDemo -Destination (Join-Path $packageDir "demo/js_runtime_demo")
} elseif (Test-Path -LiteralPath $srcJsServices -PathType Container) {
    New-Item -ItemType Directory -Path (Join-Path $packageDir "demo/js_runtime_demo") -Force | Out-Null
    Copy-DirClean -Source $srcJsServices -Destination (Join-Path $packageDir "demo/js_runtime_demo/services")
    Copy-DirClean -Source $srcJsShared -Destination (Join-Path $packageDir "demo/js_runtime_demo/shared")
}

$binServerManagerDemo = Join-Path $binDir "server_manager_demo"
$srcServerManagerDemo = Join-Path $rootDir "src/demo/server_manager_demo"
if (Test-Path -LiteralPath $binServerManagerDemo -PathType Container) {
    Copy-DirClean -Source $binServerManagerDemo -Destination (Join-Path $packageDir "demo/server_manager_demo")
} elseif (Test-Path -LiteralPath $srcServerManagerDemo -PathType Container) {
    Copy-DirClean -Source $srcServerManagerDemo -Destination (Join-Path $packageDir "demo/server_manager_demo")
}

Write-Host "Copying docs..."
Copy-FileIfExists -Source (Join-Path $rootDir "doc/stdiolink_server.md") -DestinationDir (Join-Path $packageDir "doc")
Copy-FileIfExists -Source (Join-Path $rootDir "doc/http_api.md") -DestinationDir (Join-Path $packageDir "doc")
Copy-FileIfExists -Source (Join-Path $rootDir "doc/stdiolink-server-api-requirements.md") -DestinationDir (Join-Path $packageDir "doc")
Copy-FileIfExists -Source (Join-Path $rootDir "doc/milestone/milestone_39_server_manager_demo_and_release.md") -DestinationDir (Join-Path $packageDir "doc")

$manifestPath = Join-Path $packageDir "RELEASE_MANIFEST.txt"
$manifestLines = @()
$manifestLines += "package_name=$packageName"
$manifestLines += "created_at=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
$manifestLines += "git_commit=$(Get-GitRev -RootDir $rootDir -RevArgs @('HEAD'))"
$manifestLines += "build_dir=$buildDirAbs"
$manifestLines += "with_tests=$([int][bool]$withTests)"
$manifestLines += ""
$manifestLines += "[bin]"

$manifestBinEntries = Get-ChildItem -LiteralPath (Join-Path $packageDir "bin") -File |
    Sort-Object Name |
    ForEach-Object { $_.Name }
$manifestLines += $manifestBinEntries
$manifestLines += ""
$manifestLines += "[demo]"
$manifestDemoEntries = Get-ChildItem -LiteralPath (Join-Path $packageDir "demo") -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name |
    ForEach-Object { $_.Name }
$manifestLines += $manifestDemoEntries

Set-Content -LiteralPath $manifestPath -Value $manifestLines -Encoding utf8

Write-Host "Release package created: $packageDir"
Write-Host "Manifest file: $manifestPath"
