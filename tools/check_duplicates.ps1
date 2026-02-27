#!/usr/bin/env pwsh
# check_duplicates.ps1 — 检查发布目录中同名组件多副本
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDir
)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$errors = 0

# 规则 1: bin/ 下不应存在 stdio.drv.* 可执行文件
$binDir = Join-Path $PackageDir "bin"
if (Test-Path $binDir) {
    $drvInBin = Get-ChildItem -LiteralPath $binDir -File |
        Where-Object { $_.Name -match "^stdio\.drv\." }
    foreach ($f in $drvInBin) {
        Write-Host "ERROR: driver in bin/: $($f.Name) (should be in data_root/drivers/ only)"
        $errors++
    }
}

# 规则 2: data_root/drivers/ 下同名 driver 可执行文件不应出现在多个子目录
$driversDir = Join-Path $PackageDir "data_root/drivers"
if (Test-Path $driversDir) {
    $allDriverFiles = Get-ChildItem -LiteralPath $driversDir -File -Recurse |
        Where-Object { $_.Name -match "^stdio\.drv\." } |
        Group-Object Name
    foreach ($group in $allDriverFiles) {
        if ($group.Count -gt 1) {
            $locations = ($group.Group | ForEach-Object { $_.Directory.Name }) -join ", "
            Write-Host "ERROR: duplicate driver '$($group.Name)' in: $locations"
            $errors++
        }
    }
}

if ($errors -gt 0) {
    Write-Error "$errors duplicate(s) found"
    exit 1
}
Write-Host "No duplicates found."
