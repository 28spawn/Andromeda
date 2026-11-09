@echo off
:: SPDX-License-Identifier: GPL-3.0-or-later
:: Copyright (C) 2026 Andromeda contributors
REM ---------------------------------------------------------------------------
REM  Andromeda - build your own firmware with Bluetooth LE MIDI switched on.
REM
REM  Checks this PC, installs what is missing (Git, Python, LLVM-MinGW, the
REM  JieLi toolchain), gets the latest sources and builds. Nothing is flashed.
REM
REM    Build-Andromeda-BLE.bat              check, install, update, build
REM    Build-Andromeda-BLE.bat -CheckOnly   only report what is missing
REM    Build-Andromeda-BLE.bat -NoUpdate    build the files as they are
REM
REM  The built files are for your own pedal: do not pass them on.
REM ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-Andromeda-BLE.ps1" %*
echo.
pause
