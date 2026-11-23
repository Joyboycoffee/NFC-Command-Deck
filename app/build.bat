@echo off
echo ===================================================
echo   COMPILING NFC COMMAND DECK AND BACKGROUND SERVICE
echo ===================================================

taskkill /F /IM NFC_Command_Deck.exe 2>nul
taskkill /F /IM NFC_Service.exe 2>nul

set PATH=C:\MinGW\bin;%PATH%
set CXX=g++
set FLAGS=-std=c++11 -O2 -static -static-libgcc -static-libstdc++
set GUI_LIBS=-lgdiplus -lgdi32 -lwininet -lwinmm -lcomctl32 -lcomdlg32 -lole32 -lshlwapi -luuid -lwtsapi32 -luxtheme
set SVC_LIBS=-lsetupapi -lwtsapi32 -ladvapi32 -luser32

windres resource.rc -O coff -o resource.res

echo [1/2] Compiling NFC_Command_Deck.exe (Studio GUI)...
%CXX% %FLAGS% -mwindows src\main.cpp resource.res -o NFC_Command_Deck.exe %GUI_LIBS%
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to compile NFC_Command_Deck.exe!
    exit /b %ERRORLEVEL%
)

echo [2/2] Compiling NFC_Service.exe (Background Daemon)...
%CXX% %FLAGS% src\service_main.cpp -o NFC_Service.exe %SVC_LIBS%
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to compile NFC_Service.exe!
    exit /b %ERRORLEVEL%
)

echo.
echo ===================================================
echo   BUILD SUCCESSFUL! Created Both Executables:
echo     1. NFC_Command_Deck.exe - Interactive Studio GUI
echo     2. NFC_Service.exe      - Windows Background Daemon
echo ===================================================
