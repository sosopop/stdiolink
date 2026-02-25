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
  --skip-build         Skip C++ build (assume build/bin already exists)
  --skip-webui         Skip WebUI build
  -h, --help           Show this help

Example:
  tools/publish_release.ps1 --build-dir build --output-dir release
  tools/publish_release.ps1 --name my_release
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
$skipBuild = $false
$skipWebui = $false

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
        "--skip-build" {
            $skipBuild = $true
            $i += 1
            continue
        }
        "--skip-webui" {
            $skipWebui = $true
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

# ── C++ build ────────────────────────────────────────────────────────
if (-not $skipBuild) {
    Write-Host "Building C++ project (Release)..."
    $buildBat = Join-Path $rootDir "build.bat"
    if (-not (Test-Path -LiteralPath $buildBat -PathType Leaf)) {
        Write-Error "build.bat not found at $buildBat"
        exit 1
    }

    Push-Location $rootDir
    try {
        & cmd /c "call `"$buildBat`" Release"
        if ($LASTEXITCODE -ne 0) {
            Write-Error "C++ build failed (exit code $LASTEXITCODE)"
            exit 1
        }
    } finally {
        Pop-Location
    }
}

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
Write-Host "  skip webui  : $([int][bool]$skipWebui)"

if (Test-Path -LiteralPath $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}

$dirsToCreate = @(
    "bin",
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

    $base = [System.IO.Path]::GetFileNameWithoutExtension($Name)

    if (-not $WithTests) {
        if ($base -eq "stdiolink_tests" -or $base.StartsWith("test_") -or $base -eq "gtest") {
            return $true
        }
    }

    if ($base -eq "demo_host" -or $base -eq "driverlab") {
        return $true
    }

    $lower = $Name.ToLowerInvariant()
    if ($lower.EndsWith(".log") -or $lower.EndsWith(".tmp") -or $lower.EndsWith(".json")) {
        return $true
    }

    return $false
}

# ── WebUI build ──────────────────────────────────────────────────────
$webuiPackageJson = Join-Path $rootDir "src/webui/package.json"
if (-not $skipWebui -and (Test-Path -LiteralPath $webuiPackageJson -PathType Leaf)) {
    Write-Host "Building WebUI..."
    $webuiDir = Join-Path $rootDir "src/webui"

    $npmCmd = Get-Command npm -ErrorAction SilentlyContinue
    if (-not $npmCmd) {
        Write-Error "npm not found. Install Node.js or use --skip-webui to skip WebUI build."
        exit 1
    }

    Push-Location $webuiDir
    try {
        # Use npm.cmd directly to bypass the npm.ps1 shim which is
        # incompatible with Set-StrictMode -Version Latest.
        $npmExe = Join-Path (Split-Path $npmCmd.Source) "npm.cmd"

        Write-Host "  npm ci ..."
        & $npmExe ci --ignore-scripts 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  npm ci failed, retrying with npm install ..."
            & $npmExe install --ignore-scripts 2>&1
            if ($LASTEXITCODE -ne 0) {
                Write-Error "npm install failed"
                exit 1
            }
        }

        Write-Host "  npm run build ..."
        & $npmExe run build 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Error "WebUI build failed"
            exit 1
        }

        $distDir = Join-Path $webuiDir "dist"
        if (-not (Test-Path -LiteralPath $distDir -PathType Container)) {
            Write-Error "WebUI build succeeded but dist/ is missing"
            exit 1
        }

        $webuiDest = Join-Path $packageDir "data_root/webui"
        New-Item -ItemType Directory -Path $webuiDest -Force | Out-Null
        Copy-Item -Path (Join-Path $distDir "*") -Destination $webuiDest -Recurse -Force
        Write-Host "  WebUI copied to $webuiDest"
    } finally {
        Pop-Location
    }
} elseif (-not $skipWebui) {
    Write-Host "WebUI source not found, skipping WebUI build."
}

# ── Binaries ─────────────────────────────────────────────────────────
Write-Host "Copying binaries..."
$binFiles = Get-ChildItem -LiteralPath $binDir -File
foreach ($file in $binFiles) {
    if (Should-SkipBinary -Name $file.Name -WithTests $withTests) {
        continue
    }
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $packageDir "bin") -Force
}

# Copy Qt plugin subdirectories (tls, platforms, networkinformation, etc.)
Write-Host "Copying Qt plugin directories..."
$pluginDirs = Get-ChildItem -LiteralPath $binDir -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notin @("config_demo", "js_runtime_demo", "server_manager_demo") }
foreach ($dir in $pluginDirs) {
    $dest = Join-Path $packageDir "bin/$($dir.Name)"
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    # Copy only Release DLLs (skip debug 'd' suffix variants)
    foreach ($dll in (Get-ChildItem -LiteralPath $dir.FullName -File -Filter "*.dll")) {
        $stem = [System.IO.Path]::GetFileNameWithoutExtension($dll.Name)
        if ($stem.EndsWith("d") -and (Test-Path -LiteralPath (Join-Path $dir.FullName "$($stem.Substring(0, $stem.Length - 1)).dll"))) {
            continue
        }
        Copy-Item -LiteralPath $dll.FullName -Destination $dest -Force
    }
    Write-Host "  + $($dir.Name)/"
}

# ── Seed demo data ───────────────────────────────────────────────────
Write-Host "Seeding demo data into data_root..."
$demoDataRoot = Join-Path $rootDir "src/demo/server_manager_demo/data_root"
if (Test-Path -LiteralPath $demoDataRoot -PathType Container) {
    $demoServices = Join-Path $demoDataRoot "services"
    if (Test-Path -LiteralPath $demoServices -PathType Container) {
        Copy-Item -Path (Join-Path $demoServices "*") -Destination (Join-Path $packageDir "data_root/services") -Recurse -Force
    }
    $demoProjects = Join-Path $demoDataRoot "projects"
    if (Test-Path -LiteralPath $demoProjects -PathType Container) {
        Copy-Item -Path (Join-Path $demoProjects "*") -Destination (Join-Path $packageDir "data_root/projects") -Recurse -Force
    }
    Write-Host "  Demo services and projects seeded."
} else {
    Write-Host "  WARNING: Demo data_root not found at $demoDataRoot"
}

# Copy driver binaries into data_root/drivers/<name>/<name>.exe
# Scanner expects each driver in its own subdirectory.
Write-Host "Copying drivers into data_root/drivers..."
$driversDest = Join-Path $packageDir "data_root/drivers"
$driversCopied = 0
foreach ($file in (Get-ChildItem -LiteralPath $binDir -File -Filter "*.exe")) {
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    if ($stem.StartsWith("stdio.drv.")) {
        $driverSubDir = Join-Path $driversDest $stem
        New-Item -ItemType Directory -Path $driverSubDir -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $driverSubDir -Force
        # Remove from bin/ to avoid duplication
        $binCopy = Join-Path $packageDir "bin/$($file.Name)"
        if (Test-Path -LiteralPath $binCopy -PathType Leaf) {
            Remove-Item -LiteralPath $binCopy -Force
        }
        Write-Host "  + $stem/$($file.Name)"
        $driversCopied++
    }
}
Write-Host "  $driversCopied driver(s) copied."

# ── Default config.json ──────────────────────────────────────────────
$configPath = Join-Path $packageDir "data_root/config.json"
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    Write-Host "Generating default config.json..."
    $defaultConfig = @{
        host     = "127.0.0.1"
        port     = 18080
        logLevel = "info"
    }
    $defaultConfig | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $configPath -Encoding utf8
}

# ── Startup launcher ─────────────────────────────────────────────────
Write-Host "Generating startup scripts..."

$startBat = Join-Path $packageDir "start.bat"
@"
@echo off
setlocal
set SCRIPT_DIR=%~dp0
"%SCRIPT_DIR%bin\stdiolink_server.exe" --data-root="%SCRIPT_DIR%data_root" %*
"@ | Set-Content -LiteralPath $startBat -Encoding ascii

$startPs1 = Join-Path $packageDir "start.ps1"
@"
#!/usr/bin/env pwsh
`$scriptDir = Split-Path -Parent `$MyInvocation.MyCommand.Path
`$dataRoot  = Join-Path `$scriptDir "data_root"
`$server    = Join-Path `$scriptDir "bin/stdiolink_server.exe"

if (-not (Test-Path -LiteralPath `$server)) {
    Write-Error "Server binary not found: `$server"
    exit 1
}

Write-Host "Starting stdiolink_server..."
Write-Host "  data_root : `$dataRoot"
Write-Host "  args      : `$args"
& `$server --data-root="`$dataRoot" @args
"@ | Set-Content -LiteralPath $startPs1 -Encoding utf8

$manifestPath = Join-Path $packageDir "RELEASE_MANIFEST.txt"
$manifestLines = @()
$manifestLines += "package_name=$packageName"
$manifestLines += "created_at=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
$manifestLines += "git_commit=$(Get-GitRev -RootDir $rootDir -RevArgs @('HEAD'))"
$manifestLines += "build_dir=$buildDirAbs"
$manifestLines += "with_tests=$([int][bool]$withTests)"
$manifestLines += "skip_webui=$([int][bool]$skipWebui)"
$manifestLines += ""
$manifestLines += "[bin]"

$manifestBinEntries = Get-ChildItem -LiteralPath (Join-Path $packageDir "bin") -File |
    Sort-Object Name |
    ForEach-Object { $_.Name }
$manifestLines += $manifestBinEntries
$manifestLines += ""
$manifestLines += "[webui]"
$webuiIndex = Join-Path $packageDir "data_root/webui/index.html"
if (Test-Path -LiteralPath $webuiIndex -PathType Leaf) {
    $manifestLines += "status=bundled"
    $webuiFiles = Get-ChildItem -LiteralPath (Join-Path $packageDir "data_root/webui") -File -Recurse |
        Sort-Object Name |
        ForEach-Object { $_.Name }
    $manifestLines += $webuiFiles
} else {
    $manifestLines += "status=not_included"
}

Set-Content -LiteralPath $manifestPath -Value $manifestLines -Encoding utf8

Write-Host ""
Write-Host "=== Release package created ==="
Write-Host "  Package : $packageDir"
Write-Host "  Manifest: $manifestPath"
Write-Host ""
Write-Host "To start the server:"
Write-Host "  cd $packageDir"
Write-Host "  .\start.bat              (cmd)"
Write-Host "  .\start.ps1              (PowerShell)"
Write-Host "  .\start.bat --port=8080  (custom port)"
