#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Generates a self-signed code signing certificate for Zixiao VirtIO drivers.

.DESCRIPTION
    Creates a self-signed certificate that can be used to sign kernel-mode drivers.
    The certificate is valid for 10 years and uses SHA256.

.PARAMETER CertName
    The subject name for the certificate. Default: "Zixiao System Driver Signing"

.PARAMETER OutputPath
    Path to export the certificate files. Default: ..\certs

.PARAMETER ExportPassword
    Password for the exported PFX file. If not provided, will prompt.

.EXAMPLE
    .\generate-cert.ps1

.EXAMPLE
    .\generate-cert.ps1 -ExportPassword (ConvertTo-SecureString "MyPassword" -AsPlainText -Force)

.NOTES
    This certificate is for development/testing purposes.
    For production, use EV code signing certificate or WHQL certification.
#>
param(
    [string]$CertName = "Zixiao System Driver Signing",
    [string]$OutputPath = "..\certs",
    [SecureString]$ExportPassword
)

$ErrorActionPreference = "Stop"

Write-Host @"
===============================================
  Zixiao Driver Signing Certificate Generator
===============================================

"@ -ForegroundColor Cyan

# Create output directory
$OutputPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $OutputPath))
New-Item -ItemType Directory -Force -Path $OutputPath | Out-Null
Write-Host "Output directory: $OutputPath" -ForegroundColor Gray

# Certificate parameters for kernel-mode driver signing
$certParams = @{
    Subject           = "CN=$CertName, O=Zixiao System, C=CN"
    Type              = "CodeSigningCert"
    KeySpec           = "Signature"
    KeyUsage          = "DigitalSignature"
    KeyExportPolicy   = "Exportable"
    KeyLength         = 4096
    HashAlgorithm     = "SHA256"
    NotAfter          = (Get-Date).AddYears(10)
    CertStoreLocation = "Cert:\CurrentUser\My"
    # Extended Key Usage: Code Signing + Kernel Mode Code Signing
    TextExtension     = @(
        "2.5.29.37={text}1.3.6.1.5.5.7.3.3,1.3.6.1.4.1.311.10.3.6"
    )
}

# Check if certificate already exists
$existingCert = Get-ChildItem -Path "Cert:\CurrentUser\My" -CodeSigningCert |
    Where-Object { $_.Subject -like "*$CertName*" }

if ($existingCert) {
    Write-Host "Found existing certificate with same name:" -ForegroundColor Yellow
    Write-Host "  Subject: $($existingCert.Subject)"
    Write-Host "  Thumbprint: $($existingCert.Thumbprint)"
    Write-Host "  Expires: $($existingCert.NotAfter)"

    $response = Read-Host "Do you want to create a new certificate anyway? (y/N)"
    if ($response -ne 'y' -and $response -ne 'Y') {
        Write-Host "Using existing certificate." -ForegroundColor Green
        $cert = $existingCert
    } else {
        Write-Host "`nGenerating new self-signed certificate..." -ForegroundColor Cyan
        $cert = New-SelfSignedCertificate @certParams
    }
} else {
    Write-Host "Generating self-signed certificate..." -ForegroundColor Cyan
    $cert = New-SelfSignedCertificate @certParams
}

Write-Host "Certificate created successfully!" -ForegroundColor Green
Write-Host "  Subject: $($cert.Subject)"
Write-Host "  Thumbprint: $($cert.Thumbprint)"
Write-Host "  Valid until: $($cert.NotAfter)"

# Prompt for password if not provided
if (-not $ExportPassword) {
    Write-Host "`nEnter password for PFX export (will be used for signing):" -ForegroundColor Yellow
    $ExportPassword = Read-Host -AsSecureString

    Write-Host "Confirm password:" -ForegroundColor Yellow
    $confirmPassword = Read-Host -AsSecureString

    # Convert to compare
    $BSTR1 = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($ExportPassword)
    $BSTR2 = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($confirmPassword)
    $plain1 = [System.Runtime.InteropServices.Marshal]::PtrToStringAuto($BSTR1)
    $plain2 = [System.Runtime.InteropServices.Marshal]::PtrToStringAuto($BSTR2)

    if ($plain1 -ne $plain2) {
        throw "Passwords do not match!"
    }

    # Clear plaintext passwords from memory
    $plain1 = $null
    $plain2 = $null
    [System.Runtime.InteropServices.Marshal]::ZeroFreeBSTR($BSTR1)
    [System.Runtime.InteropServices.Marshal]::ZeroFreeBSTR($BSTR2)
}

# Export files
$pfxPath = Join-Path $OutputPath "zixiao-driver-signing.pfx"
$cerPath = Join-Path $OutputPath "zixiao-driver-signing.cer"
$thumbprintPath = Join-Path $OutputPath "thumbprint.txt"

Write-Host "`nExporting certificate files..." -ForegroundColor Cyan

# Export PFX (with private key)
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $ExportPassword | Out-Null
Write-Host "  PFX (private key): $pfxPath" -ForegroundColor Green

# Export CER (public key only - for distribution)
Export-Certificate -Cert $cert -FilePath $cerPath -Type CERT | Out-Null
Write-Host "  CER (public key):  $cerPath" -ForegroundColor Green

# Save thumbprint for CI/CD
$cert.Thumbprint | Out-File -FilePath $thumbprintPath -Encoding ASCII -NoNewline
Write-Host "  Thumbprint file:   $thumbprintPath" -ForegroundColor Green

Write-Host @"

===============================================
  Certificate Generation Complete!
===============================================

Certificate Details:
  Subject:     $($cert.Subject)
  Thumbprint:  $($cert.Thumbprint)
  Valid From:  $($cert.NotBefore)
  Valid Until: $($cert.NotAfter)

Exported Files:
  PFX (private key): $pfxPath
  CER (public key):  $cerPath
  Thumbprint:        $thumbprintPath

IMPORTANT SECURITY NOTES:
  - Keep the PFX file secure - it contains the private key
  - The CER file will be distributed with the driver package
  - Add *.pfx to .gitignore to prevent committing private keys
  - Use the thumbprint or PFX for signing drivers

Next Steps:
  1. Sign drivers: .\sign-drivers.ps1 -PfxPath "$pfxPath"
  2. Create package: ..\installer\Create-DriverPackage.ps1

"@ -ForegroundColor Cyan
