@echo off
:: NFC Commander Background Service Uninstaller
title NFC Commander Service Uninstaller

net session >nul 2>&1
if %errorLevel% == 0 (
    goto :run_uninstall
) else (
    echo Requesting Administrator privileges to remove Windows Service...
    powershell -Command "Start-Process '%~dp0NFC_Service.exe' -ArgumentList '-uninstall' -Verb RunAs"
    exit /b
)

:run_uninstall
"%~dp0NFC_Service.exe" -uninstall
echo.
echo ========================================================
echo   NFC Commander Service has been stopped and removed.
echo ========================================================
pause
