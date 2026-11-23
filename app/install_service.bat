@echo off
:: NFC Commander Background Service Installer
title NFC Commander Service Installer

net session >nul 2>&1
if %errorLevel% == 0 (
    goto :run_install
) else (
    echo Requesting Administrator privileges to register Windows Service...
    powershell -Command "Start-Process '%~dp0NFC_Service.exe' -ArgumentList '-install' -Verb RunAs"
    exit /b
)

:run_install
"%~dp0NFC_Service.exe" -install
echo.
echo ========================================================
echo   NFC Commander Service is now registered and active!
echo   It will automatically start at boot with zero CPU/RAM.
echo ========================================================
pause
