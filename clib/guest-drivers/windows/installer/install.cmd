@echo off
:: Zixiao VirtIO Drivers Quick Installer
:: This script provides a simple double-click installation experience

echo ===============================================
echo   Zixiao VirtIO Drivers Installer
echo ===============================================
echo.

:: Check for admin privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script requires Administrator privileges.
    echo.
    echo Please right-click this file and select "Run as administrator"
    echo.
    pause
    exit /b 1
)

:: Run PowerShell installer
echo Starting installation...
echo.

powershell.exe -ExecutionPolicy Bypass -File "%~dp0Install-ZixiaoDrivers.ps1" -DriversPath "%~dp0.."

if %errorLevel% neq 0 (
    echo.
    echo Installation encountered errors. Please check the output above.
    pause
    exit /b 1
)

echo.
echo Installation completed!
pause
