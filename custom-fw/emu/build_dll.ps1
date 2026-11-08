# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

[CmdletBinding()]
param(
    [string]$Board = "pocket_amp",
    [ValidateSet("andromeda", "onyx")][string]$Product = "andromeda"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $RepoRoot

$BoardDef = if ($Board -eq "blackbox") { "-DBOARD_BLACKBOX=1" } else { "-DBOARD_POCKET_AMP=1" }
$ProductDef = if ($Product -eq "andromeda") { "-DPA_PRODUCT_ANDROMEDA=1" } else { "-DPA_PRODUCT_ANDROMEDA=0" }

$clangExe = Get-Command clang.exe -ErrorAction SilentlyContinue
if (-not $clangExe) {
    $clangDir = (Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT*" -Recurse -Filter "clang.exe" | Select-Object -First 1).DirectoryName
    $env:PATH = "$clangDir;$env:PATH"
}

Write-Host "Compiling Andromeda Firmware into Native Emulator DLL (andromeda_emu.dll)..." -ForegroundColor Cyan

# ---- the source list -------------------------------------------------------
# Globbed through custom-fw/sources.txt, so a new module can never be
# silently forgotten by one target. The only difference between the targets is
# which board files they replace themselves, and that is written down ONCE.
. custom-fw\tools\Read-Sources.ps1
$manifest = Read-SourceManifest
$sources = @(Get-SourceFiles -Target "emu" -Sections $manifest | ForEach-Object { "custom-fw/" + $_ })

# PA_STOCK_FLASH_LOADER: the emulator reads the stock amps and cabs from its simulated flash
# (0x0BF000, loaded via src/board/spi_nor.c); the device build
# keeps the embedded tables (src/sys/stock_assets.c says why)
clang -O3 -shared -Wall -Wextra -Icustom-fw/include $BoardDef $ProductDef -DPA_HOST_SIM=1 -DPA_EMU_DLL=1 -DPA_STOCK_FLASH_LOADER=1 `
  custom-fw/emu/emu_bridge.c `
  $sources `
  -o custom-fw/emu/andromeda_emu.dll
    Copy-Item -Force "custom-fw/emu/andromeda_emu.dll" "custom-fw/emu/pocket_amp_emu.dll"

if ($LASTEXITCODE -eq 0) {
    Write-Host "SUCCESS: andromeda_emu.dll compiled!" -ForegroundColor Green
} else {
    Write-Error "FAILED to compile andromeda_emu.dll"
}
