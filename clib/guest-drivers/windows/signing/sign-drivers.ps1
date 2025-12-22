#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Signs all Zixiao VirtIO drivers with the specified certificate.

.DESCRIPTION
    Signs .sys and .cat files for all drivers using SignTool from Windows SDK.
    Supports both certificate store (thumbprint) and PFX file signing.

.PARAMETER CertThumbprint
    Thumbprint of certificate in the certificate store.

.PARAMETER PfxPath
    Path to PFX file for signing.

.PARAMETER PfxPassword
    Password for PFX file. If not provided, will prompt.

.PARAMETER DriversPath
    Path to drivers directory. Default: parent directory

.PARAMETER Configuration
    Build configuration. Default: Release

.PARAMETER Platform
    Target platform. Default: x64

.PARAMETER TimestampServer
    RFC 3161 timestamp server URL. Default: http://timestamp.digicert.com

.EXAMPLE
    .\sign-drivers.ps1 -PfxPath ..\certs\zixiao-driver-signing.pfx

.EXAMPLE
    .\sign-drivers.ps1 -CertThumbprint "ABCD1234..."

.EXAMPLE
    .\sign-drivers.ps1 -PfxPath ..\certs\zixiao-driver-signing.pfx -Platform ARM64
#>
param(
    [string]$CertThumbprint,
    [string]$PfxPath,
    [SecureString]$PfxPassword,
    [string]$DriversPath = "..",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64",
    [string]$TimestampServer = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"

Write-Host @"
===============================================
  Zixiao VirtIO Driver Signing Tool
===============================================

"@ -ForegroundColor Cyan

# Validate parameters
if (-not $CertThumbprint -and -not $PfxPath) {
    # Try to find thumbprint file
    $thumbprintFile = Join-Path $PSScriptRoot "..\certs\thumbprint.txt"
    if (Test-Path $thumbprintFile) {
        $CertThumbprint = Get-Content $thumbprintFile -Raw
        $CertThumbprint = $CertThumbprint.Trim()
        Write-Host "Using thumbprint from file: $CertThumbprint" -ForegroundColor Gray
    } else {
        throw "Either -CertThumbprint or -PfxPath must be specified, or thumbprint.txt must exist in certs folder"
    }
}

# Find SignTool
Write-Host "Locating SignTool..." -ForegroundColor Cyan

$sdkPaths = @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22000.0\x64\signtool.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe"
)

$signtool = $null
foreach ($path in $sdkPaths) {
    if (Test-Path $path) {
        $signtool = $path
        break
    }
}

# Fallback: search for any signtool
if (-not $signtool) {
    $signtool = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\x64\*" } |
        Sort-Object { $_.FullName } -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

if (-not $signtool) {
    throw "SignTool not found. Please install Windows SDK."
}

Write-Host "  Found: $signtool" -ForegroundColor Green

# Find Inf2Cat
$inf2catDir = Split-Path $signtool -Parent
$inf2cat = Join-Path $inf2catDir "inf2cat.exe"
if (-not (Test-Path $inf2cat)) {
    Write-Warning "Inf2Cat not found at $inf2cat. Catalog files will not be regenerated."
    $inf2cat = $null
}

# Build signing arguments
$signArgs = @(
    "sign",
    "/v",
    "/fd", "SHA256",
    "/tr", $TimestampServer,
    "/td", "SHA256"
)

if ($PfxPath) {
    $PfxPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $PfxPath))
    if (-not (Test-Path $PfxPath)) {
        throw "PFX file not found: $PfxPath"
    }

    if (-not $PfxPassword) {
        Write-Host "Enter PFX password:" -ForegroundColor Yellow
        $PfxPassword = Read-Host -AsSecureString
    }

    $BSTR = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($PfxPassword)
    $PlainPassword = [System.Runtime.InteropServices.Marshal]::PtrToStringAuto($BSTR)

    $signArgs += @("/f", $PfxPath, "/p", $PlainPassword)
    Write-Host "Using PFX file: $PfxPath" -ForegroundColor Gray
} else {
    $signArgs += @("/sha1", $CertThumbprint)
    Write-Host "Using certificate thumbprint: $CertThumbprint" -ForegroundColor Gray
}

# Driver projects
$drivers = @(
    @{ Name = "zviopci"; Desc = "VirtIO PCI Bus Driver" },
    @{ Name = "zvioblk"; Desc = "VirtIO Block Driver" },
    @{ Name = "zvionet"; Desc = "VirtIO Network Driver" },
    @{ Name = "zviobln"; Desc = "VirtIO Memory Balloon Driver" }
)

$DriversPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $DriversPath))
Write-Host "`nDrivers path: $DriversPath" -ForegroundColor Gray
Write-Host "Configuration: $Configuration" -ForegroundColor Gray
Write-Host "Platform: $Platform" -ForegroundColor Gray

$signedCount = 0
$failedCount = 0

foreach ($driver in $drivers) {
    Write-Host "`n----------------------------------------" -ForegroundColor Gray
    Write-Host "Signing: $($driver.Desc) ($($driver.Name))" -ForegroundColor Cyan

    $driverDir = Join-Path $DriversPath "$($driver.Name)\$Platform\$Configuration"

    if (-not (Test-Path $driverDir)) {
        # Try alternative path without platform/config
        $driverDir = Join-Path $DriversPath $driver.Name
    }

    if (-not (Test-Path $driverDir)) {
        Write-Warning "  Driver directory not found: $driverDir"
        $failedCount++
        continue
    }

    # Find .sys file
    $sysFile = Get-ChildItem -Path $driverDir -Filter "*.sys" -Recurse | Select-Object -First 1

    if (-not $sysFile) {
        Write-Warning "  No .sys file found in $driverDir"
        $failedCount++
        continue
    }

    # Sign .sys file
    Write-Host "  Signing: $($sysFile.Name)" -ForegroundColor White
    $sysArgs = $signArgs + @($sysFile.FullName)

    $result = & $signtool $sysArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  FAILED to sign $($sysFile.Name)" -ForegroundColor Red
        Write-Host $result -ForegroundColor Red
        $failedCount++
        continue
    }
    Write-Host "  SUCCESS: $($sysFile.Name)" -ForegroundColor Green

    # Find or create .cat file
    $infFile = Join-Path $DriversPath "$($driver.Name)\$($driver.Name).inf"
    $catFile = Join-Path $sysFile.DirectoryName "$($driver.Name).cat"

    if ($inf2cat -and (Test-Path $infFile)) {
        Write-Host "  Creating catalog file..." -ForegroundColor White

        # Determine OS version string for inf2cat
        $osVersion = "10_X64"
        if ($Platform -eq "ARM64") {
            $osVersion = "10_ARM64"
        }

        $inf2catArgs = @(
            "/driver:$($sysFile.DirectoryName)",
            "/os:$osVersion",
            "/verbose"
        )

        $result = & $inf2cat $inf2catArgs 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "  Failed to create catalog file (non-fatal)"
        }
    }

    # Sign .cat file if exists
    if (Test-Path $catFile) {
        Write-Host "  Signing: $($driver.Name).cat" -ForegroundColor White
        $catArgs = $signArgs + @($catFile)

        $result = & $signtool $catArgs 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "  Failed to sign catalog file (non-fatal)"
        } else {
            Write-Host "  SUCCESS: $($driver.Name).cat" -ForegroundColor Green
        }
    }

    $signedCount++
}

# Clear password from memory
if ($PlainPassword) {
    $PlainPassword = $null
    [System.Runtime.InteropServices.Marshal]::ZeroFreeBSTR($BSTR)
}

Write-Host "`n========================================" -ForegroundColor Gray
Write-Host "Signing Complete!" -ForegroundColor Cyan
Write-Host "  Signed: $signedCount drivers" -ForegroundColor Green
if ($failedCount -gt 0) {
    Write-Host "  Failed: $failedCount drivers" -ForegroundColor Red
}

Write-Host @"

Next Steps:
  1. Test drivers in a VM with the certificate installed
  2. Create driver package: ..\installer\Create-DriverPackage.ps1

"@ -ForegroundColor Cyan

if ($failedCount -gt 0) {
    exit 1
}
