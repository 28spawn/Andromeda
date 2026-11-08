@echo off
:: SPDX-License-Identifier: GPL-3.0-or-later
:: Copyright (C) 2026 Andromeda contributors
setlocal
cd /d "%~dp0"
echo ========================================================
echo Dumping Andromeda Crash and USB Diagnostics from SRAM
echo ========================================================

if "%DEV%"=="" set DEV=\\.\PHYSICALDRIVE2
if "%JL_TOOL%"=="" (
    if exist "%USERPROFILE%\Downloads\jl-uboot-tool-main\jl-uboot-tool-main" (
        set "JL_TOOL=%USERPROFILE%\Downloads\jl-uboot-tool-main\jl-uboot-tool-main"
    ) else (
        set "JL_TOOL=jl-uboot-tool"
    )
)

echo Device: %DEV% (WL82 UBOOT)
echo Tool:   %JL_TOOL%
echo.

echo --- 1. Dumping Unit Status, Crash Record, and Event Log ---
python custom-fw\tools\jl_ram_read.py --jl-tool "%JL_TOOL%" --device "%DEV%" slots > recovery_log.txt 2>&1
type recovery_log.txt

echo.
echo --- 2. Dumping USB Controller Status ---
python custom-fw\tools\jl_ram_read.py --jl-tool "%JL_TOOL%" --device "%DEV%" usb >> recovery_log.txt 2>&1
python custom-fw\tools\jl_ram_read.py --jl-tool "%JL_TOOL%" --device "%DEV%" usb

echo.
echo --- 3. Dumping Blackbox and Interrupt Stats ---
python custom-fw\tools\jl_ram_read.py --jl-tool "%JL_TOOL%" --device "%DEV%" bb --out blackbox.bin >> recovery_log.txt 2>&1
python custom-fw\tools\jl_ram_read.py --jl-tool "%JL_TOOL%" --device "%DEV%" stats >> recovery_log.txt 2>&1

echo.
echo ========================================================
echo Done! Output saved to recovery_log.txt and blackbox.bin.
echo ========================================================
pause
