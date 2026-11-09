# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

# Build-Andromeda-BLE.ps1 - one command: check the PC, install what is missing, get the latest sources, build Andromeda WITH Bluetooth LE MIDI.
#
# Released Andromeda packages have Bluetooth LE switched off. This script builds your own copy with it switched on, on your own PC,
# from the public sources. The result is for your own pedal: do not pass the built files on.
#
#   Double-click Build-Andromeda-BLE.bat, or:
#     powershell -ExecutionPolicy Bypass -File Build-Andromeda-BLE.ps1
#
#   Without a copy of the repository (the script clones it next to itself, or into the current folder):
#     irm https://raw.githubusercontent.com/28spawn/Andromeda/main/Build-Andromeda-BLE.ps1 | iex
#
#   -CheckOnly      report what is installed and what is missing; install nothing, build nothing
#   -Yes            do not ask before installing the missing tools
#   -NoUpdate       build the files as they are; do not pull the latest commit
#   -Pi32Root DIR   the JieLi pi32 toolchain folder (the one that holds bin\clang.exe), if it is not in a standard place
#   -Board NAME     pocket_amp (default) or blackbox
#
# What it needs, and installs with winget when missing: Git, Python 3.10 or later, LLVM-MinGW (the PC compiler for the test suites).
# The JieLi pi32v2 toolchain cannot be installed by winget: the script downloads JieLi's own installer from JieLi's site and starts it.
# Nothing is flashed. The script ends with the folder that holds the firmware files.

[CmdletBinding()]
param(
    [switch]$CheckOnly,
    [switch]$Yes,
    [switch]$NoUpdate,
    [string]$Pi32Root = "",
    [ValidateSet("pocket_amp", "blackbox")][string]$Board = "pocket_amp"
)

$RepoUrl      = "https://github.com/28spawn/Andromeda.git"
$ToolchainUrl = "https://pkgman.jieliapp.com/s/win-toolchain"      # JieLi's Windows toolchain installer (the page: https://doc.zh-jieli.com/Tools/zh-cn/other_info/index.html)
$MinPython    = [version]"3.10"

function Say([string]$t, [string]$c = "Gray") { Write-Host $t -ForegroundColor $c }
function Step([string]$t) { Write-Host ""; Write-Host "=== $t ===" -ForegroundColor Cyan }

function Update-SessionPath {
    # A tool installed a moment ago is on the registry PATH, not on this window's PATH.
    $m = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $u = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = (@($m, $u) | Where-Object { $_ }) -join ";"
}

function Test-Tool([string]$name) { [bool](Get-Command $name -ErrorAction SilentlyContinue) }

function Get-PythonVersion {
    # The Microsoft Store placeholder also answers to "python": it has to run and print a version to count.
    try {
        $v = & python -c "import sys;print('%d.%d' % sys.version_info[:2])" 2>$null
        if ($LASTEXITCODE -eq 0 -and $v) { return [version]("$v".Trim()) }
    } catch { }
    return $null
}

function Find-MinGwClangDir {
    $roots = @()
    if ($env:LOCALAPPDATA) { $roots += (Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages") }
    if ($env:ProgramFiles) { $roots += (Join-Path $env:ProgramFiles "WinGet\Packages") }
    foreach ($r in $roots) {
        if (-not (Test-Path $r)) { continue }
        foreach ($d in (Get-ChildItem $r -Directory -Filter "MartinStorsjo.LLVM-MinGW.UCRT*" -ErrorAction SilentlyContinue)) {
            $c = Get-ChildItem $d.FullName -Recurse -Filter "clang.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($c) { return $c.DirectoryName }
        }
    }
    return $null
}

function Test-ClangHasLibc {
    # The plain LLVM package for Windows has clang but no stdlib.h / math.h: the test suites do not compile with it.
    if (-not (Test-Tool "clang")) { return $false }
    $tmp = Join-Path $env:TEMP ("andromeda_cc_{0}" -f $PID)
    New-Item -ItemType Directory -Force $tmp | Out-Null
    $src = Join-Path $tmp "t.c"
    [System.IO.File]::WriteAllText($src, "#include <stdlib.h>`n#include <string.h>`n#include <math.h>`nint main(void){return 0;}`n")
    $ok = $false
    try {
        & cmd /c "clang `"$src`" -o `"$tmp\t.exe`" >nul 2>nul"
        $ok = ($LASTEXITCODE -eq 0)
    } catch { }
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    return $ok
}

function Find-Pi32Root {
    $cands = @()
    if ($Pi32Root)      { $cands += $Pi32Root }
    if ($env:PI32_ROOT) { $cands += $env:PI32_ROOT }
    if ($env:USERPROFILE) { $cands += (Join-Path $env:USERPROFILE 'tools\jieli\C$\JL\pi32') }
    $cands += "C:\JL\pi32", "C:\jieli\pi32"
    foreach ($c in $cands) {
        if ($c -and (Test-Path (Join-Path $c "bin\clang.exe")) -and (Test-Path (Join-Path $c "bin\pi32v2-ld.exe")) -and (Test-Path (Join-Path $c "pi32v2-lib\r3\libc.a"))) {
            return (Resolve-Path $c).Path
        }
    }
    return $null
}

function Confirm-Install([string]$what) {
    if ($Yes) { return $true }
    $a = Read-Host "$what  Install now? [Y/n]"
    return ($a -eq "" -or $a -match "^[yY]")
}

function Install-WithWinget([string]$id, [string]$label) {
    if (-not (Test-Tool "winget")) {
        throw "$label is missing and winget is not available to install it. Install 'App Installer' from the Microsoft Store (it provides winget), or install $label by hand, then run this script again."
    }
    Say "    installing $label (winget: $id) ..." "Yellow"
    & winget install --id $id --exact --source winget --accept-package-agreements --accept-source-agreements
    # winget answers with a non-zero code when the package is already there; the check after this call decides.
    Update-SessionPath
}

function Install-Pi32Toolchain {
    $exe = Join-Path $env:TEMP "jieli_win_toolchain_setup.exe"
    Say "    downloading JieLi's toolchain installer from $ToolchainUrl ..." "Yellow"
    $old = $ProgressPreference; $ProgressPreference = "SilentlyContinue"
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $ToolchainUrl -OutFile $exe -UseBasicParsing
    } finally { $ProgressPreference = $old }
    $head = [byte[]](Get-Content $exe -Encoding Byte -TotalCount 2)
    if ($head.Length -lt 2 -or $head[0] -ne 0x4D -or $head[1] -ne 0x5A) {
        throw "the file from $ToolchainUrl is not a Windows installer. Download the Windows toolchain by hand from https://doc.zh-jieli.com/Tools/zh-cn/other_info/index.html, install it, and run this script again."
    }
    $sig = Get-AuthenticodeSignature $exe
    Say ("    installer : {0} ({1:N1} MB)" -f $exe, ((Get-Item $exe).Length / 1MB))
    Say ("    signature : {0} {1}" -f $sig.Status, $(if ($sig.SignerCertificate) { $sig.SignerCertificate.Subject } else { "(not signed)" }))
    Say "    The JieLi installer opens now. Keep its default folder (C:\JL), finish it, and this script continues." "Yellow"
    Start-Process -FilePath $exe -Wait
}

function Invoke-Main {
    $ErrorActionPreference = "Stop"
    if ($env:OS -ne "Windows_NT") { throw "this script is for Windows 10 / 11 (the JieLi toolchain used here is the Windows one)." }

    # ---- where the sources are -------------------------------------------------
    $here = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
    $inRepo = Test-Path (Join-Path $here "custom-fw\build.ps1")
    $repo = if ($inRepo) { $here } else { Join-Path $here "Andromeda" }

    Step "1/4  Checking this PC"
    Update-SessionPath
    $mingw = Find-MinGwClangDir
    if ($mingw) { $env:Path = "$mingw;$env:Path" }

    $need = @()
    $hasGit = Test-Tool "git"
    $pyVer = Get-PythonVersion
    $hasPy = ($pyVer -and $pyVer -ge $MinPython)
    $hasCc = ([bool]$mingw) -or (Test-ClangHasLibc)
    $pi32 = Find-Pi32Root

    Say ("    Git                    : {0}" -f $(if ($hasGit) { "found" } else { "MISSING" }))
    Say ("    Python {0}+           : {1}" -f $MinPython, $(if ($hasPy) { "found ($pyVer)" } elseif ($pyVer) { "too old ($pyVer)" } else { "MISSING" }))
    Say ("    PC compiler (LLVM-MinGW): {0}" -f $(if ($mingw) { "found ($mingw)" } elseif ($hasCc) { "found (clang on PATH, with a C library)" } else { "MISSING" }))
    Say ("    JieLi pi32v2 toolchain : {0}" -f $(if ($pi32) { "found ($pi32)" } else { "MISSING" }))
    Say ("    Sources                : {0}" -f $(if ($inRepo) { "this folder ($repo)" } elseif (Test-Path (Join-Path $repo "custom-fw\build.ps1")) { "found ($repo)" } else { "not here; will be cloned to $repo" }))

    # Git is only needed to clone or update; a copy unpacked from a ZIP builds without it.
    $needGit = (-not $hasGit) -and ((-not (Test-Path (Join-Path $repo "custom-fw\build.ps1"))) -or (Test-Path (Join-Path $repo ".git")))
    if ($needGit)    { $need += "Git" }
    if (-not $hasPy) { $need += "Python 3.12" }
    if (-not $hasCc) { $need += "LLVM-MinGW" }
    if (-not $pi32)  { $need += "the JieLi pi32v2 toolchain (JieLi's own installer, from JieLi's site)" }

    if ($CheckOnly) {
        if ($need.Count) { Say ("`n    missing: " + ($need -join "; ")) "Yellow" } else { Say "`n    everything needed is installed." "Green" }
        return
    }

    # ---- install what is missing -----------------------------------------------
    if ($need.Count) {
        Step "2/4  Installing what is missing"
        Say ("    to install: " + ($need -join "; "))
        if (-not (Confirm-Install "    These are installed on this PC (each installer shows its own licence).")) {
            throw "nothing was installed. Install the items listed above and run this script again."
        }
        if ($needGit) {
            Install-WithWinget "Git.Git" "Git"
            if (-not (Test-Tool "git")) { throw "Git was installed but is not on PATH yet. Close this window, open a new one and run the script again." }
        }
        if (-not $hasPy) {
            Install-WithWinget "Python.Python.3.12" "Python 3.12"
            $pyVer = Get-PythonVersion
            if (-not ($pyVer -and $pyVer -ge $MinPython)) { throw "Python was installed but 'python' does not start it yet. Close this window, open a new one and run the script again. (If it still fails: Settings > Apps > Advanced app settings > App execution aliases, switch off 'python.exe'.)" }
        }
        if (-not $hasCc) {
            Install-WithWinget "MartinStorsjo.LLVM-MinGW.UCRT" "LLVM-MinGW"
            $mingw = Find-MinGwClangDir
            if (-not $mingw) { throw "LLVM-MinGW was installed but its clang.exe was not found under the WinGet packages folder. Run this script again in a new window." }
        }
        if (-not $pi32) {
            Install-Pi32Toolchain
            $pi32 = Find-Pi32Root
            if (-not $pi32) { throw "the JieLi toolchain was not found after its installer ran. If you installed it to another folder, run this script again with  -Pi32Root <the folder that holds bin\clang.exe>." }
        }
    } else {
        Step "2/4  Nothing to install"
    }
    if ($mingw) { $env:Path = "$mingw;$env:Path" }      # in front of any other clang: the plain LLVM one has no C library
    $env:PI32_ROOT = $pi32

    # ---- the latest sources ----------------------------------------------------
    Step "3/4  Getting the latest sources"
    if (-not (Test-Path (Join-Path $repo "custom-fw\build.ps1"))) {
        if (Test-Path $repo) { throw "$repo exists but is not a copy of the Andromeda repository. Move it away, or run this script from another folder." }
        & git clone $RepoUrl $repo
        if ($LASTEXITCODE -ne 0) { throw "git clone failed (exit $LASTEXITCODE)." }
    } elseif ($NoUpdate) {
        Say "    -NoUpdate: building the files as they are."
    } elseif (-not (Test-Path (Join-Path $repo ".git"))) {
        Say "    this copy is not a git clone (a ZIP download?): it is built as it is. Clone the repository to get updates." "Yellow"
    } else {
        # pa_build_id.h is rewritten by every build: it is put back first, so an earlier build does not count as a local change or block the update.
        & git -C $repo checkout -- custom-fw/include/pa_build_id.h 2>$null | Out-Null
        $dirty = (& git -C $repo status --porcelain --untracked-files=no)
        if ($dirty) {
            Say "    this copy has local changes: it is built as it is, not updated." "Yellow"
        } else {
            & git -C $repo pull --ff-only
            if ($LASTEXITCODE -ne 0) { Say "    the update failed (no network, or the branch has no upstream): building the files that are here." "Yellow" }
        }
    }
    $rev = ""
    if (Test-Path (Join-Path $repo ".git")) { try { $rev = (& git -C $repo rev-parse --short HEAD) } catch { } }
    if ($rev) { Say "    commit $rev" }

    # ---- the build ---------------------------------------------------------------
    Step "4/4  Building Andromeda with Bluetooth LE (about 5 to 10 minutes)"
    $outRoot = Join-Path $repo "ble-build"
    New-Item -ItemType Directory -Force $outRoot | Out-Null
    $log = Join-Path $outRoot "build.log"
    $buildPs1 = Join-Path $repo "custom-fw\build.ps1"
    # Through cmd, so the compilers' error text goes to the screen and the log in order (Windows PowerShell turns a native program's stderr into error records).
    $ErrorActionPreference = "Continue"
    & cmd /c "powershell -NoProfile -ExecutionPolicy Bypass -File `"$buildPs1`" -Product andromeda -Board $Board -EnableBle 2>&1" | Tee-Object -FilePath $log
    $code = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    try { [System.IO.File]::WriteAllLines($log, [string[]](Get-Content $log)) } catch { }      # Tee-Object writes UTF-16; UTF-8 is easier to attach and search
    if ($code -ne 0) { throw "the build failed (exit $code). The full output is in $log - attach that file when you ask for help." }

    # The build writes its package to custom-fw\release\<name>, the folder the published (Bluetooth-off) package lives in.
    # The Bluetooth build is moved out of it, under a name that says what it is.
    $relRoot = Join-Path $repo "custom-fw\release"
    $rel = Get-ChildItem $relRoot -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $rel -or -not (Get-ChildItem $rel.FullName -Filter "*.fwsc")) { throw "the build finished but no package was found under $relRoot. See $log." }
    $out = Join-Path $outRoot ($rel.Name + "_BLE")
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    Move-Item $rel.FullName $out
    if ((Test-Path (Join-Path $repo ".git")) -and (Test-Tool "git")) {
        # put the repository's own release files back as they are in the commit
        $tracked = (& git -C $repo ls-files -- ("custom-fw/release/" + $rel.Name))
        if ($tracked) { & git -C $repo checkout -- ("custom-fw/release/" + $rel.Name) 2>$null | Out-Null }
        & git -C $repo checkout -- custom-fw/include/pa_build_id.h 2>$null | Out-Null      # the generated build stamp
    }
    $note = @(
        "Andromeda with Bluetooth LE MIDI - built on this PC" + $(if ($rev) { " from commit $rev" } else { "" }) + ", " + (Get-Date -Format "yyyy-MM-dd HH:mm"),
        "",
        "This build has the Bluetooth LE radio switched on. It is for your own pedal.",
        "Do not publish it or pass it on: released Andromeda packages are built with Bluetooth off.",
        "",
        "Install: open custom-fw\web\Andromeda_Web_Editor.html in Chrome or Edge, Connect MIDI, Firmware tab,",
        "choose the .bin (or .fwsc) file in this folder, Flash. Take a full backup first (see RECOVERY.md).",
        "The unit advertises as Andromeda_BLE."
    ) -join "`r`n"
    [System.IO.File]::WriteAllText((Join-Path $out "READ-ME-BLE.txt"), $note)

    Write-Host ""
    Say "DONE - Andromeda with Bluetooth LE was built." "Green"
    Say "    folder : $out"
    foreach ($f in (Get-ChildItem $out -File | Where-Object { $_.Extension -in ".bin", ".fwsc" })) { Say ("    file   : {0} ({1:N0} bytes)" -f $f.Name, $f.Length) }
    Say "    log    : $log"
    Say "    Install it with the Web Editor (custom-fw\web\Andromeda_Web_Editor.html, Firmware tab). Back up the pedal first: RECOVERY.md."
    Say "    This build is for your own pedal: please do not share the built files." "Yellow"
}

$exitCode = 0
try { Invoke-Main }
catch {
    Write-Host ""
    Write-Host ("STOPPED: " + $_.Exception.Message) -ForegroundColor Red
    $exitCode = 1
}
# "exit" would close the window of someone who ran the script with  irm ... | iex
if ($MyInvocation.MyCommand.Path) { exit $exitCode }
