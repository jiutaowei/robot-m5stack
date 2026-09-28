@echo off
setlocal
chcp 65001 >nul
title Mibao AI Server - Windows Setup

REM ============================================================
REM  Mibao-1 (StackChan) AI server - one-click Windows setup
REM
REM  What it does:
REM    1. check Python 3.10
REM    2. create virtual environment
REM    3. install dependencies (CPU-only PyTorch to save ~2GB)
REM    4. install ffmpeg if missing
REM    5. add firewall rules  TCP 8001/8003, UDP 8004
REM    6. disable sleep so the robot stays connected
REM
REM  Requires administrator rights (it will ask automatically).
REM ============================================================

REM ---------- self elevate ----------
net session >nul 2>&1
if errorlevel 1 (
    echo [INFO] Requesting administrator privileges...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

set "SCRIPT_DIR=%~dp0"
set "SERVER_DIR=%SCRIPT_DIR%..\..\xiaozhi-server"

pushd "%SERVER_DIR%" 2>nul
if errorlevel 1 (
    echo [ERROR] Cannot find xiaozhi-server folder.
    echo         Expected: %SERVER_DIR%
    pause
    exit /b 1
)

echo ============================================================
echo   Mibao AI Server - Windows Setup
echo   Server folder: %CD%
echo ============================================================
echo.

REM ---------- 1/6 python ----------
echo [1/6] Checking Python 3.10 ...
py -3.10 --version >nul 2>&1
if errorlevel 1 (
    echo.
    echo   [ERROR] Python 3.10 not found.
    echo   Download: https://www.python.org/downloads/release/python-31011/
    echo   IMPORTANT: tick "Add python.exe to PATH" while installing.
    echo.
    pause
    exit /b 1
)
py -3.10 --version
echo   OK
echo.

REM ---------- 2/6 venv ----------
echo [2/6] Creating virtual environment .venv ...
if not exist ".venv\Scripts\python.exe" (
    py -3.10 -m venv .venv
    if errorlevel 1 (
        echo   [ERROR] Failed to create venv.
        pause
        exit /b 1
    )
)
echo   OK
echo.

REM ---------- 3/6 dependencies ----------
echo [3/6] Installing dependencies - this may take 10-30 minutes ...
echo       (using Tsinghua mirror for speed in China)
echo.
echo   --- installing CPU-only PyTorch first (about 200MB instead of 2.4GB) ---
".venv\Scripts\python.exe" -m pip install --upgrade pip -i https://pypi.tuna.tsinghua.edu.cn/simple
".venv\Scripts\python.exe" -m pip install torch==2.2.2 torchaudio==2.2.2 --index-url https://download.pytorch.org/whl/cpu
if errorlevel 1 (
    echo   [WARN] CPU PyTorch install failed - the next step will install the default build.
)
echo.
echo   --- installing requirements.txt ---
".venv\Scripts\python.exe" -m pip install -r requirements.txt -i https://pypi.tuna.tsinghua.edu.cn/simple
if errorlevel 1 (
    echo.
    echo   [WARN] Some packages failed to install.
    echo          Retry manually:
    echo            .venv\Scripts\python.exe -m pip install -r requirements.txt
    echo.
)
echo   OK
echo.

REM ---------- 4/6 ffmpeg ----------
echo [4/6] Checking ffmpeg ...
where ffmpeg >nul 2>&1
if errorlevel 1 (
    echo   ffmpeg not found - installing with winget ...
    winget install --id Gyan.FFmpeg -e --accept-source-agreements --accept-package-agreements
    echo   NOTE: close and reopen the terminal after this so PATH updates.
) else (
    echo   OK
)
echo.

REM ---------- 5/6 firewall ----------
echo [5/6] Adding Windows Firewall rules ...
echo       TCP 8001 (AI chat) / TCP 8003 (OTA + notes) / UDP 8004 (auto discovery)
netsh advfirewall firewall delete rule name="Mibao 8001" >nul 2>&1
netsh advfirewall firewall delete rule name="Mibao 8003" >nul 2>&1
netsh advfirewall firewall delete rule name="Mibao 8004" >nul 2>&1
netsh advfirewall firewall add rule name="Mibao 8001" dir=in action=allow protocol=TCP localport=8001 >nul
netsh advfirewall firewall add rule name="Mibao 8003" dir=in action=allow protocol=TCP localport=8003 >nul
netsh advfirewall firewall add rule name="Mibao 8004" dir=in action=allow protocol=UDP localport=8004 >nul
echo   OK
echo.

REM ---------- 6/6 power ----------
echo [6/6] Disabling sleep (the robot disconnects if the PC sleeps) ...
powercfg /change standby-timeout-ac 0
powercfg /change hibernate-timeout-ac 0
powercfg /change monitor-timeout-ac 15
echo   OK
echo.

REM ---------- ensure data folder exists ----------
REM data/ is git-ignored (it holds your API keys), so a fresh clone has no data folder.
if not exist "data" mkdir data
if not exist "data\bin" mkdir "data\bin"

REM ---------- result ----------
echo ============================================================
if not exist "data\.config.yaml" (
    echo   [ACTION REQUIRED] data\.config.yaml is MISSING
    echo.
    echo   This file holds YOUR API keys and is NOT in git,
    echo   so a fresh git clone never contains it.
    echo.
    echo   Take it from your Mac:
    echo     robot-updating/xiaozhi-server/data/.config.yaml
    echo   and put it here:
    echo     %CD%\data\.config.yaml
    echo.
    echo   Tip: to create it by hand in Notepad, save with the file name
    echo        ".config.yaml"   ^(include the double quotes^)
    echo        and encoding UTF-8, otherwise Notepad appends .txt
) else (
    echo   data\.config.yaml found - good.
)
echo.
echo   Next step: double-click  start_server.bat
echo ============================================================
echo.
pause
