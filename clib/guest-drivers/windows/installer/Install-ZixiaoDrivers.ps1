#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Installs Zixiao VirtIO drivers on Windows guests.

.DESCRIPTION
    This script:
    1. Installs the Zixiao driver signing certificate to Trusted Publishers
    2. Installs the certificate to Trusted Root CA (for self-signed certs)
    3. Installs all VirtIO drivers using PnPUtil

.PARAMETER DriversPath
    Path to the drivers directory. Default: script directory

.PARAMETER SkipCertInstall
    Skip certificate installation (if already installed)

.PARAMETER Force
    Force reinstall even if drivers exist

.PARAMETER Silent
    Run silently without prompts

.EXAMPLE
    .\Install-ZixiaoDrivers.ps1

.EXAMPLE
    .\Install-ZixiaoDrivers.ps1 -Force -Silent

.NOTES
    Must be run as Administrator.
    For production environments, consider using WHQL-certified drivers.
#>
param(
    [string]$DriversPath = $PSScriptRoot,
    [switch]$SkipCertInstall,
    [switch]$Force,
    [switch]$Silent
)

$ErrorActionPreference = "Stop"

function Write-Status {
    param([string]$Message, [string]$Color = "White")
    if (-not $Silent) {
        Write-Host $Message -ForegroundColor $Color
    }
}

Write-Status @"
===============================================
  Zixiao VirtIO Drivers Installer
  Version: 1.0.0
===============================================

"@ -ForegroundColor Cyan

# Detect architecture
$arch = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq "Arm64") {
    "ARM64"
} elseif ([Environment]::Is64BitOperatingSystem) {
    "x64"
} else {
    "x86"
}

Write-Status "Detected architecture: $arch" -Color Gray
Write-Status "Drivers path: $DriversPath" -Color Gray

# Step 1: Install certificate
if (-not $SkipCertInstall) {
    Write-Status "`nStep 1: Installing driver signing certificate..." -Color Yellow

    $certFile = Join-Path $DriversPath "certs\zixiao-driver-signing.cer"

    if (-not (Test-Path $certFile)) {
        # Try alternative locations
        $altPaths = @(
            (Join-Path $DriversPath "zixiao-driver-signing.cer"),
            (Join-Path $DriversPath "..\certs\zixiao-driver-signing.cer")
        )
        foreach ($alt in $altPaths) {
            if (Test-Path $alt) {
                $certFile = $alt
                break
            }
        }
    }

    if (-not (Test-Path $certFile)) {
        Write-Status "  WARNING: Certificate file not found: $certFile" -Color Yellow
        Write-Status "  Drivers may not load without the certificate installed." -Color Yellow

        if (-not $Silent) {
            $response = Read-Host "Continue without certificate? (y/N)"
            if ($response -ne 'y' -and $response -ne 'Y') {
                exit 1
            }
        }
    } else {
        try {
            $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($certFile)

            # Check if certificate is already installed
            $existingCert = Get-ChildItem -Path "Cert:\LocalMachine\TrustedPublisher" |
                Where-Object { $_.Thumbprint -eq $cert.Thumbprint }

            if ($existingCert -and -not $Force) {
                Write-Status "  Certificate already installed (Thumbprint: $($cert.Thumbprint))" -Color Green
            } else {
                # Import to Trusted Publishers
                Write-Status "  Adding to Trusted Publishers..." -Color White
                $store = New-Object System.Security.Cryptography.X509Certificates.X509Store("TrustedPublisher", "LocalMachine")
                $store.Open("ReadWrite")
                $store.Add($cert)
                $store.Close()

                # Import to Root CA (for self-signed certificates)
                Write-Status "  Adding to Trusted Root Certification Authorities..." -Color White
                $store = New-Object System.Security.Cryptography.X509Certificates.X509Store("Root", "LocalMachine")
                $store.Open("ReadWrite")
                $store.Add($cert)
                $store.Close()

                Write-Status "  Certificate installed successfully!" -Color Green
                Write-Status "    Subject: $($cert.Subject)" -Color Gray
                Write-Status "    Thumbprint: $($cert.Thumbprint)" -Color Gray
            }
        } catch {
            Write-Status "  ERROR: Failed to install certificate: $_" -Color Red
            if (-not $Silent) {
                $response = Read-Host "Continue anyway? (y/N)"
                if ($response -ne 'y' -and $response -ne 'Y') {
                    exit 1
                }
            }
        }
    }
} else {
    Write-Status "`nStep 1: Skipping certificate installation" -Color Gray
}

# Step 2: Install drivers
Write-Status "`nStep 2: Installing VirtIO drivers..." -Color Yellow

$drivers = @(
    @{ Name = "zviopci"; Desc = "VirtIO PCI Bus Driver"; Critical = $true },
    @{ Name = "zvioblk"; Desc = "VirtIO Block Driver"; Critical = $false },
    @{ Name = "zvionet"; Desc = "VirtIO Network Driver"; Critical = $false },
    @{ Name = "zviobln"; Desc = "VirtIO Memory Balloon Driver"; Critical = $false }
)

$installedCount = 0
$failedCount = 0

foreach ($driver in $drivers) {
    Write-Status "`n  Installing: $($driver.Desc)..." -Color White

    # Try to find INF file in various locations
    $infPaths = @(
        (Join-Path $DriversPath "$($driver.Name)\$arch\Release\$($driver.Name).inf"),
        (Join-Path $DriversPath "$($driver.Name)\$($driver.Name).inf"),
        (Join-Path $DriversPath "$arch\$($driver.Name)\$($driver.Name).inf"),
        (Join-Path $DriversPath "$($driver.Name).inf")
    )

    $infPath = $null
    foreach ($path in $infPaths) {
        if (Test-Path $path) {
            $infPath = $path
            break
        }
    }

    if (-not $infPath) {
        Write-Status "    WARNING: INF file not found for $($driver.Name)" -Color Yellow
        if ($driver.Critical) {
            $failedCount++
        }
        continue
    }

    Write-Status "    INF: $infPath" -Color Gray

    # Use pnputil to install driver
    $pnpArgs = @("/add-driver", $infPath, "/install")
    if ($Force) {
        $pnpArgs += "/force"
    }

    try {
        $result = & pnputil.exe $pnpArgs 2>&1
        $exitCode = $LASTEXITCODE

        if ($exitCode -eq 0 -or $exitCode -eq 259) {
            # 259 = ERROR_NO_MORE_ITEMS (driver already installed)
            Write-Status "    SUCCESS" -Color Green
            $installedCount++
        } elseif ($exitCode -eq 3010) {
            # Reboot required
            Write-Status "    SUCCESS (Reboot required)" -Color Yellow
            $installedCount++
        } else {
            Write-Status "    FAILED (Exit code: $exitCode)" -Color Red
            Write-Status "    $result" -Color Gray
            $failedCount++
        }
    } catch {
        Write-Status "    ERROR: $_" -Color Red
        $failedCount++
    }
}

# Step 3: Summary
Write-Status @"

===============================================
  Installation Summary
===============================================

"@ -ForegroundColor Cyan

Write-Status "  Installed: $installedCount drivers" -Color Green
if ($failedCount -gt 0) {
    Write-Status "  Failed: $failedCount drivers" -Color Red
}

Write-Status @"

Installed Drivers:
  - zviopci: VirtIO PCI Bus Driver (required)
  - zvioblk: VirtIO Block Device Driver
  - zvionet: VirtIO Network Adapter Driver
  - zviobln: VirtIO Memory Balloon Driver

The drivers will be automatically loaded when
VirtIO devices are detected by Windows PnP.

To verify installation:
  Get-WmiObject Win32_PnPSignedDriver | Where-Object { `$_.DeviceName -like "*Zixiao*" }

"@ -ForegroundColor Cyan

if ($failedCount -gt 0) {
    Write-Status "Some drivers failed to install. Check the output above for details." -Color Yellow
    exit 1
}

Write-Status "Installation completed successfully!" -Color Green
