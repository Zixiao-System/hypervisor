#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Uninstalls Zixiao VirtIO drivers from Windows.

.DESCRIPTION
    This script:
    1. Removes all Zixiao VirtIO drivers from the driver store
    2. Optionally removes the signing certificate

.PARAMETER RemoveCertificate
    Also remove the Zixiao signing certificate from the certificate stores

.PARAMETER Silent
    Run silently without prompts

.EXAMPLE
    .\Uninstall-ZixiaoDrivers.ps1

.EXAMPLE
    .\Uninstall-ZixiaoDrivers.ps1 -RemoveCertificate -Silent

.NOTES
    Must be run as Administrator.
    A reboot may be required after uninstallation.
#>
param(
    [switch]$RemoveCertificate,
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
  Zixiao VirtIO Drivers Uninstaller
===============================================

"@ -ForegroundColor Cyan

# Confirm uninstallation
if (-not $Silent) {
    Write-Status "This will remove all Zixiao VirtIO drivers from the system." -Color Yellow
    $response = Read-Host "Are you sure you want to continue? (y/N)"
    if ($response -ne 'y' -and $response -ne 'Y') {
        Write-Status "Uninstallation cancelled." -Color Gray
        exit 0
    }
}

# Step 1: Find and remove drivers
Write-Status "`nStep 1: Removing VirtIO drivers..." -Color Yellow

$driverPatterns = @("zviopci", "zvioblk", "zvionet", "zviobln")
$removedCount = 0

# Get list of installed drivers
$drivers = pnputil.exe /enum-drivers 2>&1

foreach ($pattern in $driverPatterns) {
    Write-Status "`n  Looking for $pattern drivers..." -Color White

    # Parse pnputil output to find matching drivers
    $currentDriver = $null
    $linesToProcess = $drivers -split "`n"

    foreach ($line in $linesToProcess) {
        if ($line -match "Published Name\s*:\s*(oem\d+\.inf)") {
            $currentDriver = $Matches[1]
        }

        if ($line -match "Original Name\s*:\s*$pattern\.inf" -and $currentDriver) {
            Write-Status "    Found: $currentDriver" -Color Gray

            try {
                $result = pnputil.exe /delete-driver $currentDriver /force 2>&1
                if ($LASTEXITCODE -eq 0) {
                    Write-Status "    Removed: $currentDriver" -Color Green
                    $removedCount++
                } else {
                    Write-Status "    Failed to remove $currentDriver : $result" -Color Yellow
                }
            } catch {
                Write-Status "    Error removing $currentDriver : $_" -Color Yellow
            }

            $currentDriver = $null
        }
    }
}

Write-Status "`n  Removed $removedCount driver(s) from driver store" -Color Cyan

# Step 2: Remove certificate (optional)
if ($RemoveCertificate) {
    Write-Status "`nStep 2: Removing signing certificate..." -Color Yellow

    $certName = "Zixiao System Driver Signing"
    $removedCerts = 0

    # Remove from Trusted Publishers
    $certs = Get-ChildItem -Path "Cert:\LocalMachine\TrustedPublisher" |
        Where-Object { $_.Subject -like "*$certName*" }

    foreach ($cert in $certs) {
        try {
            Remove-Item -Path $cert.PSPath -Force
            Write-Status "  Removed from TrustedPublisher: $($cert.Thumbprint)" -Color Green
            $removedCerts++
        } catch {
            Write-Status "  Failed to remove from TrustedPublisher: $_" -Color Yellow
        }
    }

    # Remove from Root
    $certs = Get-ChildItem -Path "Cert:\LocalMachine\Root" |
        Where-Object { $_.Subject -like "*$certName*" }

    foreach ($cert in $certs) {
        try {
            Remove-Item -Path $cert.PSPath -Force
            Write-Status "  Removed from Root: $($cert.Thumbprint)" -Color Green
            $removedCerts++
        } catch {
            Write-Status "  Failed to remove from Root: $_" -Color Yellow
        }
    }

    if ($removedCerts -eq 0) {
        Write-Status "  No certificates found to remove" -Color Gray
    }
} else {
    Write-Status "`nStep 2: Skipping certificate removal (use -RemoveCertificate to remove)" -Color Gray
}

# Summary
Write-Status @"

===============================================
  Uninstallation Complete
===============================================

Removed $removedCount driver(s) from the driver store.

NOTE: A system reboot may be required for changes to take full effect.

If VirtIO devices are still present in the VM, Windows may attempt
to reinstall drivers from Windows Update or the driver store.

"@ -ForegroundColor Cyan
