#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors
"""
custom-fw/test/test_stability_pack.py - Andromeda Stability Pack Acceptance Suite
Tests:
  - B1: Backlight flicker on preset/view change (P0)
  - B2: Mix == 0 skip engine (P0)
  - B3: Finite guard on wet output (P0)
  - B6: Engine swap only at block boundary & soft ramp click prevention
  - Fracture macro 50% chaos retains rhythm
  - Macro 0 -> 50 -> 100% clearly different sounds across all 6 engines
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    c_src = ROOT / "test" / "test_stability_pack.c"
    exe = ROOT / "test" / "test_stability_pack.exe"

    deps = [
        ROOT / "src" / "fx" / "fx_andromeda.c",
        ROOT / "src" / "fx" / "v1_glass.c",
        ROOT / "src" / "fx" / "v1_bloom.c",
        ROOT / "src" / "fx" / "v1_accretion.c",
        ROOT / "src" / "fx" / "v1_choir.c",
        ROOT / "src" / "fx" / "v1_fracture.c",
        ROOT / "src" / "fx" / "v1_constellation.c",
        ROOT / "src" / "fx" / "v1_fdn.c",
        ROOT / "src" / "fx" / "v1_shimmer.c",
    ]

    cmd = [
        "clang", "-O1", "-Wall",
        "-I" + str(ROOT / "include"),
        "-DPA_PRODUCT_ANDROMEDA=1",
        str(c_src),
        *[str(d) for d in deps],
        "-o", str(exe),
        "-lm"
    ]

    print("Compiling Andromeda Stability Pack acceptance suite...")
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("Compilation failed:")
        print(res.stderr)
        return 1

    print("Running Andromeda Stability Pack acceptance suite...")
    run_res = subprocess.run([str(exe)], capture_output=False)
    return run_res.returncode

if __name__ == "__main__":
    sys.exit(main())
