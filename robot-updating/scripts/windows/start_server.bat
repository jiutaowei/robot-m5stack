@echo off
setlocal
chcp 65001 >nul
title Mibao AI Server

REM ============================================================
REM  Start the Mibao AI server (Windows)
REM  Keep this window open while using the robot.
REM  Close it (or press Ctrl+C) to stop the server.
REM ============================================================

set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%..\..\xiaozhi-server" 2>nul
if errorlevel 1 (
    echo [ERROR] Cannot find xiaozhi-server folder.
    pause
    exit /b 1
)

if not exist ".venv\Scripts\python.exe" (
    echo [ERROR] .venv not found - run setup_windows.bat first.
    pause
    exit /b 1
)

if not exist "data\.config.yaml" (
    echo [WARN] data\.config.yaml is missing - the server will fail to start.
    echo        Copy it from your Mac (it holds your API keys, not in git).
    echo.
)

echo ============================================================
echo   Mibao AI Server starting ...
echo   This PC's IP addresses:
ipconfig | findstr /i "IPv4"
echo.
echo   The server will print the WebSocket / OTA URLs below.
echo   Robot should auto-discover this PC (UDP broadcast, port 8004).
echo ============================================================
echo.

".venv\Scripts\python.exe" app.py

echo.
echo Server stopped.
pause
