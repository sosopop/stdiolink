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
  --skip-build         Skip C++ build (assume runtime_release already exists)
  --skip-webui         Skip WebUI build
  --skip-tests         Skip test execution before packaging
  -h, --help           Show this help

Example:
  tools/publish_release.ps1 --build-dir build_release --output-dir release
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
    }
    catch {
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
$skipTests = $false

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
        "--skip-tests" {
            $skipTests = $true
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
$runtimeDir = Join-Path $buildDirAbs "runtime_release"

# Resolve npm.cmd / npx.cmd early so both WebUI build and test blocks can use them.
# Using .cmd directly bypasses the npm.ps1 shim which is incompatible with StrictMode.
$npmCmd = Get-Command npm -ErrorAction SilentlyContinue
$npmExe = $null
$npxExe = $null
if ($npmCmd) {
    $npmExe = Join-Path (Split-Path $npmCmd.Source) "npm.cmd"
    $npxExe = Join-Path (Split-Path $npmCmd.Source) "npx.cmd"
}

# ── C++ build ────────────────────────────────────────────────────────
if (-not $skipBuild) {
    # 清理旧 runtime 目录，确保不残留
    $runtimeClean = Join-Path $buildDirAbs "runtime_release"
    if (Test-Path -LiteralPath $runtimeClean) {
        Remove-Item -LiteralPath $runtimeClean -Recurse -Force
    }
    Write-Host "Building C++ project (Release)..."
    $buildBat = Join-Path $rootDir "build.bat"
    if (-not (Test-Path -LiteralPath $buildBat -PathType Leaf)) {
        Write-Error "build.bat not found at $buildBat"
        exit 1
    }

    Push-Location $rootDir
    try {
        & cmd /c "call `"$buildBat`" Release --build-dir `"$buildDir`""
        if ($LASTEXITCODE -ne 0) {
            Write-Error "C++ build failed (exit code $LASTEXITCODE)"
            exit 1
        }
    }
    finally {
        Pop-Location
    }
}

if (-not (Test-Path -LiteralPath $runtimeDir -PathType Container)) {
    Write-Error "Runtime directory not found: $runtimeDir"
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
Write-Host "  runtime     : $runtimeDir"
Write-Host "  output root : $outputDirAbs"
Write-Host "  package dir : $packageDir"
Write-Host "  with tests  : $([int][bool]$withTests)"
Write-Host "  skip webui  : $([int][bool]$skipWebui)"
Write-Host "  skip tests  : $([int][bool]$skipTests)"

if (Test-Path -LiteralPath $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}

$dirsToCreate = @(
    "bin",
    "data_root/drivers",
    "data_root/services",
    "data_root/projects",
    "data_root/workspaces",
    "data_root/logs"
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

    if (-not $npmExe) {
        Write-Error "npm not found. Install Node.js or use --skip-webui to skip WebUI build."
        exit 1
    }

    Push-Location $webuiDir
    try {
        Write-Host "  npm ci ..."
        & $npmExe ci --ignore-scripts
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  npm ci failed, retrying with npm install ..."
            & $npmExe install --ignore-scripts
            if ($LASTEXITCODE -ne 0) {
                Write-Error "npm install failed"
                exit 1
            }
        }

        Write-Host "  npm run build ..."
        & $npmExe run build
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
    }
    finally {
        Pop-Location
    }
}
elseif (-not $skipWebui) {
    Write-Host "WebUI source not found, skipping WebUI build."
}

# ── Test execution ────────────────────────────────────────────────────
if (-not $skipTests) {
    Write-Host "=== Running test suites ==="

    # 1. GTest (C++)
    $runtimeBinDir = Join-Path $runtimeDir "bin"
    $gtestBin = Join-Path $runtimeBinDir "stdiolink_tests.exe"
    if (Test-Path -LiteralPath $gtestBin -PathType Leaf) {
        Write-Host "--- GTest (C++) ---"
        & $gtestBin
        if ($LASTEXITCODE -ne 0) {
            Write-Error "GTest failed (exit code $LASTEXITCODE)"
            exit 1
        }
        Write-Host "  GTest passed."
    }
    else {
        Write-Host "WARNING: GTest binary not found at $gtestBin, skipping C++ tests."
    }

    # 2. Vitest (WebUI unit tests)
    $webuiNodeModules = Join-Path $rootDir "src/webui/node_modules"
    if ($npmExe -and (Test-Path -LiteralPath $webuiNodeModules -PathType Container)) {
        Write-Host "--- Vitest (WebUI unit tests) ---"
        Push-Location (Join-Path $rootDir "src/webui")
        try {
            & $npmExe run test
            if ($LASTEXITCODE -ne 0) {
                Write-Error "Vitest failed (exit code $LASTEXITCODE)"
                exit 1
            }
        }
        finally {
            Pop-Location
        }
        Write-Host "  Vitest passed."
    }
    else {
        Write-Host "WARNING: npm or node_modules not available, skipping Vitest."
    }

    # 3. Playwright (E2E)
    if ($npxExe -and (Test-Path -LiteralPath $webuiNodeModules -PathType Container)) {
        Write-Host "--- Playwright (E2E tests) ---"
        Push-Location (Join-Path $rootDir "src/webui")
        try {
            & $npxExe playwright install chromium
            if ($LASTEXITCODE -ne 0) {
                Write-Error "Playwright browser install failed (exit code $LASTEXITCODE)"
                exit 1
            }
            & $npxExe playwright test
            if ($LASTEXITCODE -ne 0) {
                Write-Error "Playwright tests failed (exit code $LASTEXITCODE)"
                exit 1
            }
        }
        finally {
            Pop-Location
        }
        Write-Host "  Playwright passed."
    }
    else {
        Write-Host "WARNING: npx or node_modules not available, skipping Playwright."
    }

    # 4. Smoke tests (Python)
    $pythonCmd = Get-Command python -ErrorAction SilentlyContinue
    if ($pythonCmd) {
        Write-Host "--- Smoke tests (Python) ---"
        $smokeScript = Join-Path $rootDir "src/smoke_tests/run_smoke.py"
        if (Test-Path -LiteralPath $smokeScript -PathType Leaf) {
            & python $smokeScript --plan all
            if ($LASTEXITCODE -ne 0) {
                Write-Error "Smoke tests failed (exit code $LASTEXITCODE)"
                exit 1
            }
            Write-Host "  Smoke tests passed."
        }
        else {
            Write-Host "WARNING: Smoke test script not found at $smokeScript, skipping smoke tests."
        }
    }
    else {
        Write-Host "WARNING: Python not found, skipping smoke tests."
    }

    Write-Host "=== All test suites passed ==="
}

# ── Binaries ─────────────────────────────────────────────────────────
Write-Host "Copying binaries from runtime..."
$runtimeBinDir2 = Join-Path $runtimeDir "bin"
$binFiles = Get-ChildItem -LiteralPath $runtimeBinDir2 -File
foreach ($file in $binFiles) {
    if (Should-SkipBinary -Name $file.Name -WithTests $withTests) {
        continue
    }
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $packageDir "bin") -Force
}

# Copy Qt plugin subdirectories (tls, platforms, networkinformation, etc.)
Write-Host "Copying Qt plugin directories..."
$pluginDirs = Get-ChildItem -LiteralPath $runtimeBinDir2 -Directory -ErrorAction SilentlyContinue
foreach ($dir in $pluginDirs) {
    $dest = Join-Path $packageDir "bin/$($dir.Name)"
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    foreach ($dll in (Get-ChildItem -LiteralPath $dir.FullName -File -Filter "*.dll")) {
        $stem = [System.IO.Path]::GetFileNameWithoutExtension($dll.Name)
        if ($stem.EndsWith("d") -and (Test-Path -LiteralPath (Join-Path $dir.FullName "$($stem.Substring(0, $stem.Length - 1)).dll"))) {
            continue
        }
        Copy-Item -LiteralPath $dll.FullName -Destination $dest -Force
    }
    Write-Host "  + $($dir.Name)/"
}

# ── Copy data_root from runtime ──────────────────────────────────────
Write-Host "Copying data_root from runtime..."
$runtimeDataRoot = Join-Path $runtimeDir "data_root"
if (Test-Path -LiteralPath $runtimeDataRoot -PathType Container) {
    Copy-Item -Path (Join-Path $runtimeDataRoot "*") -Destination (Join-Path $packageDir "data_root") -Recurse -Force
}

# ── Default config.json ──────────────────────────────────────────────
$configPath = Join-Path $packageDir "data_root/config.json"
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    Write-Host "Generating default config.json..."
    $defaultConfig = @{
        host     = "127.0.0.1"
        port     = 6200
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

# ── Development environment launcher ─────────────────────────────────
Write-Host "Generating dev.bat..."

$devBat = Join-Path $packageDir "dev.bat"
$devBatContent = @"
@echo off
setlocal
set SCRIPT_DIR=%~dp0

REM Add bin directory to PATH
set PATH=%SCRIPT_DIR%bin;%PATH%

REM Discover and create doskey aliases for all drivers
"@

$driversDir = Join-Path $packageDir "data_root/drivers"
if (Test-Path -LiteralPath $driversDir -PathType Container) {
    $driverDirs = Get-ChildItem -LiteralPath $driversDir -Directory | Sort-Object Name
    foreach ($driverDir in $driverDirs) {
        $exeFiles = Get-ChildItem -LiteralPath $driverDir.FullName -Filter "*.exe" -File
        foreach ($exe in $exeFiles) {
            $aliasName = [System.IO.Path]::GetFileNameWithoutExtension($exe.Name)
            $relativePath = "data_root\drivers\$($driverDir.Name)\$($exe.Name)"
            $devBatContent += "`ndoskey $aliasName=`"%SCRIPT_DIR%$relativePath`" `$*"
        }
    }
}

$devBatContent += @"


echo.
echo ========================================
echo stdiolink Development Environment
echo ========================================
echo.
echo Environment configured:
echo   - bin\ added to PATH
echo   - Driver aliases created
echo.
echo To list all drivers:
echo   doskey /macros
echo.
echo To run a driver:
echo   [driver-name] --export-meta
echo   [driver-name] [args...]
echo.
echo To start the server:
echo   stdiolink_server --data-root="%SCRIPT_DIR%data_root"
echo.

cmd /k
"@

Set-Content -LiteralPath $devBat -Value $devBatContent -Encoding ascii

# ── Development environment launcher (PowerShell) ────────────────────
Write-Host "Generating dev.ps1..."

$devPs1 = Join-Path $packageDir "dev.ps1"
$devPs1Content = @"
#!/usr/bin/env pwsh
`$global:stdiolinkDevScriptDir = Split-Path -Parent `$MyInvocation.MyCommand.Path
`$global:stdiolinkDevBinDir = Join-Path `$global:stdiolinkDevScriptDir "bin"
`$global:stdiolinkDevDriversDir = Join-Path `$global:stdiolinkDevScriptDir "data_root/drivers"
`$global:stdiolinkDevProjectsDir = Join-Path `$global:stdiolinkDevScriptDir "data_root/projects"
`$global:stdiolinkProjectAliases = @()

# Add bin directory to PATH for current session
`$env:PATH = "`$global:stdiolinkDevBinDir;`$env:PATH"

# Set Qt plugin path
`$env:QT_PLUGIN_PATH = `$global:stdiolinkDevBinDir

# Discover and create function aliases for all drivers
"@

if (Test-Path -LiteralPath $driversDir -PathType Container) {
    $driverDirs = Get-ChildItem -LiteralPath $driversDir -Directory | Sort-Object Name
    foreach ($driverDir in $driverDirs) {
        $exeFiles = Get-ChildItem -LiteralPath $driverDir.FullName -Filter "*.exe" -File
        foreach ($exe in $exeFiles) {
            $aliasName = [System.IO.Path]::GetFileNameWithoutExtension($exe.Name)
            $driverPath = "data_root/drivers/$($driverDir.Name)/$($exe.Name)"
            $devPs1Content += @"

function global:$aliasName {
    `$scriptDir = Split-Path -Parent `$PSCommandPath
    if (-not `$scriptDir) {
        `$scriptDir = Split-Path -Parent (Get-Variable -Name PSScriptRoot -ValueOnly -ErrorAction SilentlyContinue)
    }
    if (-not `$scriptDir) {
        `$scriptDir = `$PWD.Path
    }
    & (Join-Path `$scriptDir "$driverPath") @args
}
"@
        }
    }
}

$devPs1Content += @"


# Read project config explicitly as UTF-8 so Windows PowerShell can parse non-ASCII names.
function Read-ProjectConfig {
    param(
        [Parameter(Mandatory = `$true)]
        [string]`$ConfigPath
    )

    `$json = [System.IO.File]::ReadAllText(`$ConfigPath, [System.Text.Encoding]::UTF8)
    return `$json | ConvertFrom-Json
}

# Discover and create project aliases from saved project configs
if (Test-Path -LiteralPath `$global:stdiolinkDevProjectsDir -PathType Container) {
    foreach (`$projectDir in (Get-ChildItem -LiteralPath `$global:stdiolinkDevProjectsDir -Directory | Sort-Object Name)) {
        `$projectId = `$projectDir.Name
        `$configPath = Join-Path `$projectDir.FullName "config.json"
        `$projectConfig = `$null

        if (Test-Path -LiteralPath `$configPath -PathType Leaf) {
            try {
                `$projectConfig = Read-ProjectConfig -ConfigPath `$configPath
            }
            catch {
                Write-Warning "Skipping project alias '`$projectId': failed to parse `$configPath"
            }
        }

        if (-not `$projectConfig) {
            continue
        }

        `$serviceId = [string]`$projectConfig.serviceId
        if ([string]::IsNullOrWhiteSpace(`$serviceId)) {
            Write-Warning "Skipping project alias '`$projectId': serviceId missing in `$configPath"
            continue
        }

        if (Get-Command -Name `$projectId -ErrorAction SilentlyContinue) {
            Write-Warning "Skipping project alias '`$projectId': command name already exists"
            continue
        }

        `$functionName = "__stdiolink_project_`$(`$projectId -replace '[^A-Za-z0-9_]', '_')"
        `$serviceDir = "data_root/services/`$serviceId"
        `$paramPath = "data_root/projects/`$projectId/param.json"
        `$scriptBlock = {
            & stdiolink_service "`$serviceDir" --data-root="data_root" --config-file="`$paramPath" @args
        }.GetNewClosure()

        Set-Item -Path ("Function:global:{0}" -f `$functionName) -Value `$scriptBlock
        Set-Alias -Name `$projectId -Value `$functionName -Scope Global
        `$global:stdiolinkProjectAliases += [PSCustomObject]@{
            Id = `$projectId
            ServiceId = `$serviceId
        }
    }
}

# Helper function to list all available drivers
function global:drivers {
    Write-Host ""
    Write-Host "Available drivers:"
    Write-Host ""
    Get-Command -CommandType Function | Where-Object {
        `$_.Source -eq '' -and `$_.Name -ne 'drivers' -and `$_.Name -like 'stdio.drv.*'
    } | ForEach-Object {
        Write-Host "  `$(`$_.Name)" -ForegroundColor Cyan
    }
    Write-Host ""
}

# Helper function to list all available services
function global:services {
    `$scriptDir = Split-Path -Parent `$PSCommandPath
    if (-not `$scriptDir) {
        `$scriptDir = Split-Path -Parent (Get-Variable -Name PSScriptRoot -ValueOnly -ErrorAction SilentlyContinue)
    }
    if (-not `$scriptDir) {
        `$scriptDir = `$PWD.Path
    }
    Write-Host ""
    Write-Host "Available services:"
    Write-Host ""
    `$servicesDir = Join-Path `$scriptDir "data_root/services"
    if (Test-Path -LiteralPath `$servicesDir -PathType Container) {
        Get-ChildItem -LiteralPath `$servicesDir -Directory | Sort-Object Name | ForEach-Object {
            Write-Host "  `$(`$_.Name)" -ForegroundColor Green
        }
    } else {
        Write-Host "  (no services found)" -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "Example usage:"
    Write-Host "  stdiolink_service `"data_root/services/[service-name]`" --data-root=`"data_root`" --config-file=`"data_root/projects/[project-id]/param.json`"" -ForegroundColor Gray
    Write-Host ""
}

# Helper function to list all available projects
function global:projects {
    Write-Host ""
    Write-Host "Available projects:"
    Write-Host ""
    `$aliasByProject = @{}
    foreach (`$projectAlias in `$global:stdiolinkProjectAliases) {
        `$aliasByProject[[string]`$projectAlias.Id] = [string]`$projectAlias.ServiceId
    }
    if (Test-Path -LiteralPath `$global:stdiolinkDevProjectsDir -PathType Container) {
        `$projectDirs = Get-ChildItem -LiteralPath `$global:stdiolinkDevProjectsDir -Directory | Sort-Object Name
        if (`$projectDirs.Count -gt 0) {
            foreach (`$projectDir in `$projectDirs) {
                `$projectId = `$projectDir.Name
                `$serviceLabel = "(service unknown)"
                if (`$aliasByProject.ContainsKey(`$projectId)) {
                    `$serviceLabel = `$aliasByProject[`$projectId]
                } else {
                    `$configPath = Join-Path `$projectDir.FullName "config.json"
                    if (Test-Path -LiteralPath `$configPath -PathType Leaf) {
                        try {
                            `$projectConfig = Read-ProjectConfig -ConfigPath `$configPath
                            if (-not [string]::IsNullOrWhiteSpace([string]`$projectConfig.serviceId)) {
                                `$serviceLabel = [string]`$projectConfig.serviceId
                            }
                        }
                        catch {
                        }
                    }
                }

                `$aliasNote = ""
                if (-not (Get-Alias -Name `$projectId -ErrorAction SilentlyContinue)) {
                    `$aliasNote = " (alias unavailable)"
                }

                Write-Host "  `$projectId" -ForegroundColor Magenta -NoNewline
                Write-Host " -> `$serviceLabel`$aliasNote" -ForegroundColor DarkGray
            }
        } else {
            Write-Host "  (no projects found)" -ForegroundColor Yellow
        }
    } else {
        Write-Host "  (no projects found)" -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "To run a project:"
    Write-Host "  [project-id]"
    Write-Host "  [project-id] [extra args]"
    Write-Host ""
}

Write-Host ""
Write-Host "========================================"
Write-Host "stdiolink Development Environment"
Write-Host "========================================"
Write-Host ""
Write-Host "Environment configured:"
Write-Host "  - bin\ added to PATH"
Write-Host "  - Driver aliases created"
Write-Host "  - Project aliases created"
Write-Host ""
Write-Host "To list all drivers:"
Write-Host "  drivers"
Write-Host ""
Write-Host "To list all services:"
Write-Host "  services"
Write-Host ""
Write-Host "To list all projects:"
Write-Host "  projects"
Write-Host ""
Write-Host "To run a driver:"
Write-Host "  [driver-name] --export-meta"
Write-Host "  [driver-name] [args...]"
Write-Host ""
Write-Host "To run a project:"
Write-Host "  [project-id]"
Write-Host "  [project-id] [extra args]"
Write-Host ""
Write-Host "To start the server:"
Write-Host ("  stdiolink_server --data-root=""{0}""" -f (Join-Path `$global:stdiolinkDevScriptDir "data_root"))
Write-Host ""
"@

Set-Content -LiteralPath $devPs1 -Value $devPs1Content -Encoding utf8

$manifestPath = Join-Path $packageDir "RELEASE_MANIFEST.txt"
$manifestLines = @()
$manifestLines += "package_name=$packageName"
$manifestLines += "created_at=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
$manifestLines += "git_commit=$(Get-GitRev -RootDir $rootDir -RevArgs @('HEAD'))"
$manifestLines += "build_dir=$buildDirAbs"
$manifestLines += "with_tests=$([int][bool]$withTests)"
$manifestLines += "skip_webui=$([int][bool]$skipWebui)"
$manifestLines += "skip_tests=$([int][bool]$skipTests)"
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
}
else {
    $manifestLines += "status=not_included"
}

Set-Content -LiteralPath $manifestPath -Value $manifestLines -Encoding utf8

# ── Duplicate check ──────────────────────────────────────────────────
Write-Host "Checking for duplicate components..."
$checkScript = Join-Path $scriptDir "check_duplicates.ps1"
if (Test-Path -LiteralPath $checkScript -PathType Leaf) {
    & pwsh -File $checkScript -PackageDir $packageDir
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Duplicate check failed! See errors above."
        exit 1
    }
}
else {
    Write-Error "check_duplicates.ps1 not found at $checkScript"
    exit 1
}

Write-Host ""
Write-Host "=== Release package created ==="
Write-Host "  Package : $packageDir"
Write-Host "  Manifest: $manifestPath"
Write-Host ""
Write-Host "To start the server:"
Write-Host "  cd $packageDir"
Write-Host "  .\start.bat              (cmd)"
Write-Host "  .\start.ps1              (PowerShell)"
Write-Host "  .\start.bat --port=6200  (custom port)"
Write-Host ""
Write-Host "For development (with driver aliases):"
Write-Host "  .\dev.bat                (opens cmd with configured environment)"
Write-Host "  .\dev.ps1                (PowerShell with configured environment)"

