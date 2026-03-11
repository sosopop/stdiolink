#!/usr/bin/env pwsh
$global:stdiolinkDevScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$global:stdiolinkDevBinDir = Join-Path $global:stdiolinkDevScriptDir "bin"
$global:stdiolinkDevDataRoot = Join-Path $global:stdiolinkDevScriptDir "data_root"
$global:stdiolinkDevDriversDir = Join-Path $global:stdiolinkDevDataRoot "drivers"
$global:stdiolinkDevServicesDir = Join-Path $global:stdiolinkDevDataRoot "services"
$global:stdiolinkDevProjectsDir = Join-Path $global:stdiolinkDevDataRoot "projects"
$global:stdiolinkDriverAliases = @()
$global:stdiolinkProjectAliases = @()

function New-StdiolinkFunctionName {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Prefix,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    return "__stdiolink_{0}_{1}" -f $Prefix, ($Name -replace "[^A-Za-z0-9_]", "_")
}

function Test-StdiolinkAliasCanRegister {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $existing = Get-Command -Name $Name -ErrorAction SilentlyContinue
    if (-not $existing) {
        return $true
    }

    if ($existing.CommandType -eq "Alias" -and $existing.Definition -like "__stdiolink_*") {
        return $true
    }

    return $false
}

# Read project config explicitly as UTF-8 so Windows PowerShell can parse non-ASCII names.
function global:Read-ProjectConfig {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ConfigPath
    )

    $json = [System.IO.File]::ReadAllText($ConfigPath, [System.Text.Encoding]::UTF8)
    return $json | ConvertFrom-Json
}

function global:Register-StdiolinkDriverAliases {
    if (-not (Test-Path -LiteralPath $global:stdiolinkDevDriversDir -PathType Container)) {
        return
    }

    foreach ($driverDir in (Get-ChildItem -LiteralPath $global:stdiolinkDevDriversDir -Directory | Sort-Object Name)) {
        foreach ($exe in (Get-ChildItem -LiteralPath $driverDir.FullName -Filter "*.exe" -File | Sort-Object Name)) {
            $aliasName = [System.IO.Path]::GetFileNameWithoutExtension($exe.Name)
            if (-not (Test-StdiolinkAliasCanRegister -Name $aliasName)) {
                Write-Warning "Skipping driver alias '$aliasName': command name already exists"
                continue
            }

            $functionName = New-StdiolinkFunctionName -Prefix "driver" -Name $aliasName
            $driverPath = $exe.FullName
            $scriptBlock = { & $driverPath @args }.GetNewClosure()

            Set-Item -Path ("Function:global:{0}" -f $functionName) -Value $scriptBlock -Force
            Set-Alias -Name $aliasName -Value $functionName -Scope Global -Force
            $global:stdiolinkDriverAliases += [PSCustomObject]@{
                Name = $aliasName
                Path = $driverPath
            }
        }
    }
}

function global:Register-StdiolinkProjectAliases {
    if (-not (Test-Path -LiteralPath $global:stdiolinkDevProjectsDir -PathType Container)) {
        return
    }

    foreach ($projectDir in (Get-ChildItem -LiteralPath $global:stdiolinkDevProjectsDir -Directory | Sort-Object Name)) {
        $projectId = $projectDir.Name
        $configPath = Join-Path $projectDir.FullName "config.json"
        $projectConfig = $null

        if (Test-Path -LiteralPath $configPath -PathType Leaf) {
            try {
                $projectConfig = Read-ProjectConfig -ConfigPath $configPath
            }
            catch {
                Write-Warning "Skipping project alias '$projectId': failed to parse $configPath"
            }
        }

        if (-not $projectConfig) {
            continue
        }

        $serviceId = [string]$projectConfig.serviceId
        if ([string]::IsNullOrWhiteSpace($serviceId)) {
            Write-Warning "Skipping project alias '$projectId': serviceId missing in $configPath"
            continue
        }

        if (-not (Test-StdiolinkAliasCanRegister -Name $projectId)) {
            Write-Warning "Skipping project alias '$projectId': command name already exists"
            continue
        }

        $functionName = New-StdiolinkFunctionName -Prefix "project" -Name $projectId
        $serviceDir = Join-Path $global:stdiolinkDevServicesDir $serviceId
        $paramPath = Join-Path $projectDir.FullName "param.json"
        $dataRoot = $global:stdiolinkDevDataRoot
        $scriptBlock = {
            & stdiolink_service $serviceDir --data-root="$dataRoot" --config-file="$paramPath" @args
        }.GetNewClosure()

        Set-Item -Path ("Function:global:{0}" -f $functionName) -Value $scriptBlock -Force
        Set-Alias -Name $projectId -Value $functionName -Scope Global -Force
        $global:stdiolinkProjectAliases += [PSCustomObject]@{
            Id = $projectId
            ServiceId = $serviceId
        }
    }
}

function global:drivers {
    Write-Host ""
    Write-Host "Available drivers:"
    Write-Host ""
    if ($global:stdiolinkDriverAliases.Count -gt 0) {
        $global:stdiolinkDriverAliases | Sort-Object Name | ForEach-Object {
            Write-Host ("  {0}" -f $_.Name) -ForegroundColor Cyan
        }
    }
    else {
        Write-Host "  (no drivers found)" -ForegroundColor Yellow
    }
    Write-Host ""
}

function global:services {
    Write-Host ""
    Write-Host "Available services:"
    Write-Host ""
    if (Test-Path -LiteralPath $global:stdiolinkDevServicesDir -PathType Container) {
        $serviceDirs = Get-ChildItem -LiteralPath $global:stdiolinkDevServicesDir -Directory | Sort-Object Name
        if ($serviceDirs.Count -gt 0) {
            $serviceDirs | ForEach-Object {
                Write-Host ("  {0}" -f $_.Name) -ForegroundColor Green
            }
        }
        else {
            Write-Host "  (no services found)" -ForegroundColor Yellow
        }
    }
    else {
        Write-Host "  (no services found)" -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "Example usage:"
    Write-Host ("  stdiolink_service ""{0}"" --data-root=""{1}"" --config-file=""{2}""" -f (Join-Path $global:stdiolinkDevServicesDir "[service-name]"), $global:stdiolinkDevDataRoot, (Join-Path $global:stdiolinkDevProjectsDir "[project-id]/param.json")) -ForegroundColor Gray
    Write-Host ""
}

function global:projects {
    Write-Host ""
    Write-Host "Available projects:"
    Write-Host ""
    $aliasByProject = @{}
    foreach ($projectAlias in $global:stdiolinkProjectAliases) {
        $aliasByProject[[string]$projectAlias.Id] = [string]$projectAlias.ServiceId
    }

    if (Test-Path -LiteralPath $global:stdiolinkDevProjectsDir -PathType Container) {
        $projectDirs = Get-ChildItem -LiteralPath $global:stdiolinkDevProjectsDir -Directory | Sort-Object Name
        if ($projectDirs.Count -gt 0) {
            foreach ($projectDir in $projectDirs) {
                $projectId = $projectDir.Name
                $serviceLabel = "(service unknown)"
                if ($aliasByProject.ContainsKey($projectId)) {
                    $serviceLabel = $aliasByProject[$projectId]
                }
                else {
                    $configPath = Join-Path $projectDir.FullName "config.json"
                    if (Test-Path -LiteralPath $configPath -PathType Leaf) {
                        try {
                            $projectConfig = Read-ProjectConfig -ConfigPath $configPath
                            if (-not [string]::IsNullOrWhiteSpace([string]$projectConfig.serviceId)) {
                                $serviceLabel = [string]$projectConfig.serviceId
                            }
                        }
                        catch {
                        }
                    }
                }

                $aliasNote = ""
                if (-not (Get-Alias -Name $projectId -ErrorAction SilentlyContinue)) {
                    $aliasNote = " (alias unavailable)"
                }

                Write-Host ("  {0}" -f $projectId) -ForegroundColor Magenta -NoNewline
                Write-Host (" -> {0}{1}" -f $serviceLabel, $aliasNote) -ForegroundColor DarkGray
            }
        }
        else {
            Write-Host "  (no projects found)" -ForegroundColor Yellow
        }
    }
    else {
        Write-Host "  (no projects found)" -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "To run a project:"
    Write-Host "  [project-id]"
    Write-Host "  [project-id] [extra args]"
    Write-Host ""
}

$env:PATH = "$global:stdiolinkDevBinDir;$env:PATH"
$env:QT_PLUGIN_PATH = $global:stdiolinkDevBinDir

Register-StdiolinkDriverAliases
Register-StdiolinkProjectAliases

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
Write-Host ("  stdiolink_server --data-root=""{0}""" -f $global:stdiolinkDevDataRoot)
Write-Host ""
