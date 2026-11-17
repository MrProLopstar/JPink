@echo off
setlocal
cd /d "%~dp0"
chcp 65001 >nul 2>&1
title JPink Firmware Flasher

echo ========================================================
echo       JPink (CrossPoint Fork) - Live Flasher
echo ========================================================
echo.

where py >nul 2>&1
if %errorlevel% equ 0 (
    set "PYTHON_CMD=py -3"
    goto :run
)

where python >nul 2>&1
if %errorlevel% equ 0 (
    set "PYTHON_CMD=python"
    goto :run
)

echo [ERROR] Python was not found in PATH!
echo Please install Python 3 from https://www.python.org/
echo Make sure to check "Add python.exe to PATH" during install.
echo.
pause
exit /b 1

:run
%PYTHON_CMD% tools\flash_tool.py %*
if %errorlevel% neq 0 (
    echo.
    echo Flash tool exited with code %errorlevel%.
)
echo.
pause
