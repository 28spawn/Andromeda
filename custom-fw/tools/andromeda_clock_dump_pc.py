#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors
"""
custom-fw/tools/andromeda_clock_dump_pc.py - print the audio-clock registers an Andromeda build dumped at boot (Stage 0 of docs/ANDROMEDA-V1-PLAN.md).
READ-ONLY: it reads SRAM 0x01C7EB00 over the USB diagnostic channel; nothing is written to the unit.

    python custom-fw/tools/andromeda_clock_dump_pc.py [--port "Custom MIDI"]

Needs an Andromeda build with the dump ("AUCK" magic) and no "<" held at power-on. It also reports what the numbers imply: the ISR rate against the 24 MHz timer is
measured separately (perf_monitor_pc.py shows the audio interrupts per second).
"""
import argparse, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ble_matrix_pc as m

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--port", default="Custom MIDI"); a = ap.parse_args()
    u = m.Unit(a.port)
    w = struct.unpack("<40I", u.ram(0x01C7EB00, 160))
    if w[0] != 0x4B435541:
        sys.exit("no AUCK dump: an older build, '<' held at power-on, or a previous dump did not finish")
    names = ["0x10000 clock control", "0x12E00 ALNK0 (CON0 CON1 CON2 CON3 ADR0 ADR1 ADR2 ADR3 LEN SHN ...)", "0x51018 port IO map (0x5101C IOMAP0, 0x51030 clock select)"]
    sizes = [16, 11, 8]
    o = 4
    for n, sz, base in zip(names, sizes, w[1:4]):
        print(f"{n}  (@{base:#x})")
        for i in range(sz):
            print(f"   +{4 * i:02X} {base + 4 * i:#010x}: {w[o + i]:08X}")
        o += sz
    print("\nWhat to look for: a divider field in the ALNK0 words or the clock control block equal to 17 (or 16 / 15 if the source is 48 MHz and the frame is 64 bit clocks).")
    print("48e6 / (17 * 64) =", round(48e6 / (17 * 64), 2), "Hz;  48e6 / (16 * 64) =", 48e6 / (16 * 64), ";  48e6 / (15 * 64) =", 48e6 / (15 * 64))

if __name__ == "__main__":
    main()
