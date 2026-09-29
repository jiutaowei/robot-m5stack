@echo off
setlocal
chcp 65001 >nul
title Mibao AI Server - Autostart Setup

REM ============================================================
REM  Register the Mibao AI server to start automatically at logon
REM  (Windows equivalent of the macOS launchd setup)
REM
REM  Usage:
REM    install_autostart.bat            install / reinstall
REM    install_autostart.bat uninstall  remove
REM ============================================================

set "TASKNAME=MibaoAIServer"
set "SCRIPT_DIR=%~dp0"
set "START_BAT=%SCRIPT_DIR%start_server.bat"

if /i "%1"=="uninstall" goto uninstall

if not exist "%START_BAT%" (
    echo [ERROR] start_server.bat not found next to this script.
    pause
    exit /b 1
)

echo Registering autostart task "%TASKNAME%" ...
schtasks /delete /tn "%TASKNAME%" /f >nul 2>&1
schtasks /create /tn "%TASKNAME%" /tr "\"%START_BAT%\"" /sc onlogon /rl highest /f
if errorlevel 1 (
    echo [ERROR] Failed to create the task. Try running this file as administrator.
    pause
    exit /b 1
)

echo.
echo   OK - the server will now start automatically when you log in.
echo        A console window will open (keep it open while using the robot).
echo.
echo   Start it right now without logging out:
echo        schtasks /run /tn "%TASKNAME%"
echo.
echo   Remove autostart:
echo        install_autostart.bat uninstall
echo.
pause
exit /b 0

:uninstall
echo Removing autostart task "%TASKNAME%" ...
schtasks /delete /tn "%TASKNAME%" /f
echo   Done. (The server itself is not stopped: close its window or run
echo   taskkill /f /im python.exe  if needed.)
pause
exit /b 0
