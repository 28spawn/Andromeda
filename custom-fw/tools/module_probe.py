#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors
"""module_probe.py - the time of each module (chain slot) while you play, live over USB, and which of the OVERLOAD popup's four conditions fires.

READ-ONLY on the unit (RAM reads over the diagnostic channel, manufacturer ID 0x7D). Needs python-rtmidi; close the web editor.

  python custom-fw/tools/module_probe.py --seconds 60            play, turn knobs, change effects; one line per second
  python custom-fw/tools/module_probe.py --seconds 60 --csv m.csv

Per second it prints, for the modules that ran: the mean time of one block over the last second (us and % of the 1451 us budget) and the longest ever
(since the counters were cleared; python custom-fw/tools/andromeda_perf.py ctl reset clears them), plus the audio interrupt's average, the governor level
and the second core. The modules are the chain's slots: DYN, DRV, AMP, CAB, EQ, MOD (= the LAYER), DLY (= the EFFECT), REV (= SPACE and SHIMMER).

The popup shows when ANY of these is true (pa_audio_core.c): the audio went to pass-through (BYPASS), the stereo governor switched the second channel off,
the load governor reached level 2, or the codec reported a burst of overruns (4 in one second). The first two lines of the summary say which of them you got.
The last two need the variables' addresses: they come from the build's ELF (build_pi32/pocket_amp_custom.elf) and are used only when the ELF matches the
unit; otherwise the line says so. The ELF must be the one that was flashed.
"""
import argparse
import csv
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import perf_monitor_pc as pm

BUDGET_US = 1451.0
SLOT_TABLE = 0x01C7C500
SLOTS = ["DYN", "DRV", "AMP", "CAB", "EQ", "MOD(LAYER)", "DLY(EFFECT)", "REV(SPACE)"]


def read_slots(u):
    w = struct.unpack("<32I", u.ram(SLOT_TABLE, 128))
    return [dict(max=w[4 * i] / 24.0, sum16=w[4 * i + 1], calls=w[4 * i + 2], last=w[4 * i + 3] / 24.0) for i in range(8)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--csv")
    ap.add_argument("--port", default="Custom MIDI")
    ap.add_argument("--elf", default=str(Path(__file__).resolve().parent.parent / "build_pi32" / "pocket_amp_custom.elf"))
    a = ap.parse_args()
    u = pm.m.Unit(a.port)
    print("BUILD", u.build_id(), flush=True)

    import volume_probe as vp                                          # the late-start counters and the ELF checks live there
    syms = None
    try:
        syms = vp.load_syms(a.elf)
        if syms and not vp.check_syms(u, syms):
            syms = None
    except Exception as ex:                                            # the tool must still measure without the symbols
        print("popup variables not available:", ex)
        syms = None
    gap_addr = syms.get("g_pa_isr_gap") if syms else None
    print("popup variables: " + ("read from the ELF (it matches this unit)" if syms else "NOT read (ELF missing or not the flashed build): the stereo governor and the codec overruns are not checked"), flush=True)
    print("late-start counters: " + ("read" if gap_addr else "NOT in this build (flash the build with g_pa_isr_gap)"), flush=True)
    sv_addr = syms.get("g_pa_set_save") if syms else None
    print("settings-save counters: " + ("read" if sv_addr else "NOT in this build"), flush=True)
    prev_sv = list(struct.unpack("<4I", u.ram(sv_addr, 16))) if sv_addr else None
    prev_gap = list(struct.unpack("<10I", u.ram(gap_addr, 40))) if gap_addr else None
    gap_total = [0] * 10

    out = open(a.csv, "w", newline="") if a.csv else None
    wr = csv.writer(out) if out else None
    if wr:
        wr.writerow(["t_s", "isr_avg_us", "gov", "core1_on", "stereo_allow", "xrun_per_s", "bypass"] + ["%s_mean_us" % s.split("(")[0] for s in SLOTS])

    prev_slots = read_slots(u); prev_sym = pm.sym_snap(u, syms) if syms else None
    first = {"bypass": None, "stereo": None, "gov2": None, "xrun": None}
    worst = {s: 0.0 for s in SLOTS}; isr_worst = 0
    t0 = time.time(); n_ev0 = pm.evcount(u)[1]; last_t = t0
    print("%-6s %-6s %-4s %-3s %-6s  %s" % ("t(s)", "ISR us", "gov", "c1", "popup?", "module mean us (% of budget)"), flush=True)
    while time.time() - t0 < a.seconds:
        time.sleep(max(0.0, 1.0 - (time.time() - last_t))); last_t = time.time()
        t = last_t - t0
        s = pm.snap(u); sl = read_slots(u)
        parts = []; means = []
        for i, name in enumerate(SLOTS):
            dc = (sl[i]["calls"] - prev_slots[i]["calls"]) & 0xFFFFFFFF
            ds = (sl[i]["sum16"] - prev_slots[i]["sum16"]) & 0xFFFFFFFF
            mean = (ds * 16.0 / dc / 24.0) if 0 < dc < 0x40000000 else 0.0
            means.append(mean)
            if dc and mean > 0.5:
                parts.append("%s %.0f (%.0f%%)" % (name.split("(")[0], mean, 100 * mean / BUDGET_US))
                worst[name] = max(worst[name], mean)
        popup = []
        xr = 0; st_allow = 1; byp = 0
        if syms:
            y = pm.sym_snap(u, syms)
            xr = (y["xrun"] - prev_sym["xrun"]) & 0xFFFFFFFF; st_allow = y["stereo"]; byp = y["bypass"]
            if byp and first["bypass"] is None: first["bypass"] = t
            if not st_allow and first["stereo"] is None: first["stereo"] = t
            if xr >= 4 and first["xrun"] is None: first["xrun"] = t
            if byp: popup.append("BYPASS")
            if not st_allow: popup.append("STEREO-OFF")
            if xr >= 4: popup.append("XRUN x%d" % xr)
            prev_sym = y
        if s["level"] >= 2:
            popup.append("GOV%d" % s["level"])
            if first["gov2"] is None: first["gov2"] = t
        isr_worst = max(isr_worst, s["avg"])
        late_s = ""
        if gap_addr:
            g = list(struct.unpack("<10I", u.ram(gap_addr, 40)))
            dg = [(x - y) & 0xFFFFFFFF for x, y in zip(g, prev_gap)]
            for i in range(3, 9):
                gap_total[i] += dg[i]
            late_s = "late-start %d  " % sum(dg[5:9])                  # blocks that started 200 us or more late this second
            prev_gap = g; gap_total[2] = g[2]
        if sv_addr:
            sv = list(struct.unpack("<4I", u.ram(sv_addr, 16)))
            if sv[0] != prev_sv[0]:
                late_s += "SAVES +%d (last %d us, longest %d us, failed %d)  " % (sv[0] - prev_sv[0], sv[1], sv[2], sv[3])
            prev_sv = sv
        print("%-6.1f %-6d %-4d %-3s %-6s  %s%s" % (t, s["avg"], s["level"], "on" if s["on"] else "OFF", ",".join(popup) or "-", late_s, "  ".join(parts) or "(nothing running)"), flush=True)
        if wr:
            wr.writerow(["%.1f" % t, s["avg"], s["level"], s["on"], st_allow, xr, byp] + ["%.0f" % m for m in means])
        prev_slots = sl
    print("\nSUMMARY")
    print("  highest 1 s average of the audio interrupt: %d us (%.0f %% of the budget)" % (isr_worst, 100.0 * isr_worst / BUDGET_US))
    ranked = sorted(worst.items(), key=lambda kv: -kv[1])
    print("  module cost (highest 1 s mean): " + ", ".join("%s %.0f us" % (k.split("(")[0], v) for k, v in ranked if v > 0.5))
    for key, label in (("bypass", "the audio went to pass-through (BYPASS)"), ("stereo", "the stereo governor switched the second channel off"),
                       ("gov2", "the load governor reached level 2"), ("xrun", "a burst of codec overruns")):
        if first[key] is not None:
            print("  POPUP CONDITION: %s, first at t=%.1f s" % (label, first[key]))
    if not any(v is not None for v in first.values()):
        print("  no popup condition was seen in this run" + ("" if syms else " (but the stereo governor and the overruns were not readable)"))
    if gap_addr:
        print("  time between the starts of two audio blocks (nominal 1451 us), blocks per interval: " + ", ".join("%s us: %d" % (vp.GAP_LABELS[i], gap_total[3 + i]) for i in range(6))
              + "   longest ever %d us" % gap_total[2])
        print("  blocks that STARTED 200 us or more late: %d of %d" % (sum(gap_total[5:9]), sum(gap_total[3:9])))
    n_ev1 = pm.evcount(u)[1]
    if n_ev1 > n_ev0:
        print("  events during the run:")
        for k in range(max(n_ev0, n_ev1 - pm.CAP), n_ev1):
            e = pm.evread(u, k)
            if e["code"] in (6, 7, 8, 9, 10, 11, 15, 16, 18, 21, 22):
                print("    %s" % vp.describe(e))
    if out:
        out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
