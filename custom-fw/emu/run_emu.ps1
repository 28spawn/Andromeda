# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

# custom-fw/emu/run_emu.ps1 — Build & Launch Andromeda Live Emulator + ASIO Audio Workbench
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $RepoRoot

$clangExe = Get-Command clang.exe -ErrorAction SilentlyContinue
if (-not $clangExe) {
    $clangDir = (Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT*" -Recurse -Filter "clang.exe" | Select-Object -First 1).DirectoryName
    $env:PATH = "$clangDir;$env:PATH"
}

Write-Host "[1/2] Compiling Andromeda Firmware into Native Emulator DLL (andromeda_emu.dll)..." -ForegroundColor Cyan
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build_dll.ps1") -Product andromeda
if ($LASTEXITCODE -ne 0) {
    throw "Failed to compile andromeda_emu.dll"
}

# Pick the first free port in 8080..8099. 8080 is commonly taken by some other
# dev server; before this the emulator just died with "address already in use".
$used = @((Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue).LocalPort)
$port = 8080
while (($used -contains $port) -and ($port -lt 8100)) { $port++ }

Write-Host "[2/2] Starting Low-Latency ASIO Audio Host & Web UI at http://localhost:$port ..." -ForegroundColor Green
Start-Process "http://localhost:$port"
python custom-fw/emu/server.py --port $port
