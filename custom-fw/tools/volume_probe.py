#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors
"""volume_probe.py - what happens inside the unit while you turn the MASTER knob (or anything else): CPU, memory, flash, or something else?

READ-ONLY on the unit (RAM reads over the USB diagnostic channel, manufacturer ID 0x7D). It sends nothing that changes the sound. It reads:
  * the audio interrupt: blocks, the longest block (us) and the blocks that ran over the 1451 us budget (include/pa_evlog.h, SRAM 0x01C7C250)
  * the load governor level and the average block time (0x01C7D4C0), the second core's status and longest job (0x01C7D200)
  * the flash read-check counters (0x01C7D580, pa_xip.h): the code runs from flash, so a flash write or erase can starve the audio interrupt
  * the event log (64 events at 0x01C7D600): ISR_LONG (a late block and the step it was in), BYPASS, GOVERNOR, SMP, SLOW_TICK (a slow UI tick),
    POOL_FAIL (the effect memory ran out), SWAP_REQ / SWAP_DONE, MASTER, ENCODER.
and then says which kind of cause the numbers point at.

Two ways to run it (python-rtmidi needed; close the web editor):

  1. USB connected the whole time (USB itself is a load, so this is the less clean test):
       python custom-fw/tools/volume_probe.py live --seconds 20        <- turn the volume during these 20 seconds

  2. USB UNPLUGGED while you turn the knob (the clean test; the counters live in SRAM and keep counting):
       python custom-fw/tools/volume_probe.py mark                     <- records the counters; then unplug USB
       ... turn the volume hard for about 20 seconds, listen for the problem, note when it happened ...
       python custom-fw/tools/volume_probe.py report                   <- plug USB back in; prints what changed since the mark
     (If the unit power-cycles in between, the counters start again at zero and the report says so.)

Reading the result:
  late blocks / longest block over 1451 us while the knob moved      -> CPU: the audio interrupt itself ran too long
  late blocks, but the ISR_LONG event's step is a pending swap or a flash step -> not the effects: something blocked the interrupt
  POOL_FAIL events, or the pool counters moving                      -> memory
  SLOW_TICK events (a UI tick over its time)                         -> the main loop / the screen, not the audio
  flash-read counters moving                                         -> flash access (a settings save, a store write) starved the audio
  nothing moves at all while you hear the problem                    -> the sound is wrong without being late: the gain path or the codec, not load
"""
import argparse
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import perf_monitor_pc as pm            # the event decoder and the RAM map are the same ones the overload monitor uses

BUDGET_US = 1451
MARK_FILE = Path(__file__).resolve().parent / ".volume_probe_mark.json"
FLASH_CHECK = 0x01C7D580
GAP_LABELS = ["<1500", "1500-1650", "1650-1900", "1900-2400", "2400-3500", ">=3500"]   # us between the starts of two audio blocks (nominal 1451)
pm.EV[22] = "ISR_LATE"
_old_describe = pm.describe


def describe(e):
    if e["code"] == 22:
        return "ISR_LATE    a block STARTED %d us after the previous one (nominal 1451); main loop was at step %#x; the previous block itself took %d us" % (e["a"], e["b"] >> 16, e["b"] & 0xFFFF)
    return _old_describe(e)


def load_syms(elf):
    """addresses of g_pa_isr_gap (late starts) and the popup's own variables, from the build's ELF (it must be the flashed build; checked against the unit)"""
    import subprocess
    if not Path(elf).exists():
        return None
    out = subprocess.run([pm.NM, elf], capture_output=True, text=True).stdout
    want = {"g_pa_audio", "g_pa_stereo_allow", "g_pa_gov_level", "g_pa_isr_gap", "g_pa_set_save"}
    got = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) == 3 and f[2] in want:
            got[f[2]] = int(f[0], 16)
    return got if {"g_pa_audio", "g_pa_stereo_allow", "g_pa_gov_level"} <= set(got) else None


def check_syms(u, y):
    """the ELF belongs to the flashed build when the audio interrupt counter runs at the block rate and the governor level agrees"""
    try:
        c0 = pm.sym_snap(u, y); ta = time.time(); time.sleep(0.3); c1 = pm.sym_snap(u, y)
        rate = (c1["irq"] - c0["irq"]) / (time.time() - ta)
        if not (300 < rate < 1500 and c1["gov"] == pm.snap(u)["level"]):
            return False
        if "g_pa_isr_gap" in y and struct.unpack("<I", u.ram(y["g_pa_isr_gap"], 4))[0] != 0x53504147:
            y.pop("g_pa_isr_gap")                      # an older build without the late-start counters
        return True
    except Exception:
        return False


def snapshot(u, syms=None):
    s = pm.snap(u)
    fl = list(struct.unpack("<4I", u.ram(FLASH_CHECK, 16)))
    magic, n = pm.evcount(u)
    extra = {}
    if syms:
        y = pm.sym_snap(u, syms)
        extra.update(xrun=y["xrun"], stereo=y["stereo"], bypass=y["bypass"])
        if "g_pa_isr_gap" in syms:
            extra["gap"] = list(struct.unpack("<10I", u.ram(syms["g_pa_isr_gap"], 40)))
    return {**extra, "t": time.time(), "avg_us": s["avg"], "isr_max_us": s["isrmax"], "over": s["over"], "entries": s["entries"], "gov": s["level"], "core1_on": s["on"],
            "core1_why": s["why"], "core1_job_us": s["job"], "flash": fl, "ev_magic": magic, "ev_count": n}


def events_between(u, n0, n1):
    first = max(n0, n1 - pm.CAP)
    return [pm.evread(u, k) for k in range(first, n1)], first > n0


def verdict(a, b, evs):
    d_entries = b["entries"] - a["entries"]
    d_over = b["over"] - a["over"]
    by = {}
    for e in evs:
        by.setdefault(pm.EV.get(e["code"], "#%d" % e["code"]), []).append(e)
    lines = []
    if d_entries < 0:
        lines.append("The counters went DOWN: the unit restarted since the mark, so this report only covers the time since power-on.")
    lines.append("audio blocks run: %d (%.1f s at 689 blocks/s)" % (d_entries, d_entries / 689.0))
    lines.append("blocks over the %d us budget: %d   longest block ever (since the counters were last cleared): %.0f us (%.0f %% of budget)" % (BUDGET_US, d_over, b["isr_max_us"], 100.0 * b["isr_max_us"] / BUDGET_US))
    lines.append("average block now: %d us (%.0f %%)   governor level %d" % (b["avg_us"], 100.0 * b["avg_us"] / BUDGET_US, b["gov"]))
    lines.append("second core: %s, longest job %.0f us" % ("on" if b["core1_on"] else "OFF (%s)" % pm.WHY.get(b["core1_why"], b["core1_why"]), b["core1_job_us"]))
    dfl = [y - x for x, y in zip(a["flash"], b["flash"])]
    lines.append("flash read-check counters changed by %s" % dfl)
    lines.append("events since the mark: " + (", ".join("%s x%d" % (k, len(v)) for k, v in sorted(by.items())) or "none"))
    why = []
    if "xrun" in a and "xrun" in b:
        d_xr = (b["xrun"] - a["xrun"]) & 0xFFFFFFFF
        lines.append("codec overruns (the popup's trigger, g_pa_audio.xrun_count): +%d   stereo governor %s" % (d_xr, "on" if b["stereo"] else "OFF"))
        if "gap" in a and "gap" in b and b["gap"][0] == a["gap"][0]:
            dg = [y - x for x, y in zip(a["gap"], b["gap"])]
            lines.append("time between the starts of two audio blocks (nominal 1451 us), blocks per interval: " + ", ".join("%s us: %d" % (GAP_LABELS[i], dg[3 + i]) for i in range(6))
                         + "   longest ever %d us" % b["gap"][2])
            late_starts = sum(dg[5:9])                    # intervals of 1650 us and more: the block started 200 us or more late
            if d_xr > 0 and late_starts > 0:
                markers = sorted({e["b"] >> 16 for e in by.get("ISR_LATE", [])})
                prevd = [e["b"] & 0xFFFF for e in by.get("ISR_LATE", [])]
                why.append("LATE STARTS: %d overruns and %d blocks that started 200 us or more late. The audio interrupt was held off before it began. Main-loop steps at those moments: %s; "
                           "the block before took %s us (normal = a late start, not a long block). Something above the audio interrupt's priority ran, or interrupts were disabled."
                           % (d_xr, late_starts, ", ".join("%#x" % m for m in markers) or "(no events: the log was overwritten)", "%d-%d" % (min(prevd), max(prevd)) if prevd else "?"))
            elif d_xr > 0:
                why.append("OVERRUNS WITHOUT LATE STARTS: %d overruns but every block started on time and none ran long. The overrun flag itself is firing for another reason (the DMA's own pending bit), not load." % d_xr)
            elif late_starts > 0:
                why.append("%d blocks started late but no overrun resulted" % late_starts)
        elif d_xr > 0:
            why.append("%d codec overruns, but this build has no late-start counters (flash the new build) or the ELF was not given: run with --elf <the flashed build's ELF>" % d_xr)
    if d_over > 0 or "ISR_LONG" in by:
        steps = sorted({e["b"] for e in by.get("ISR_LONG", [])})
        why.append("CPU / blocked interrupt: %d late block(s); ISR_LONG steps seen: %s" % (d_over, ", ".join("%#x (%s)" % (s, pm.STEP.get(s, "?")) for s in steps) or "-"))
    if "BYPASS" in by or b["gov"] >= 2:
        why.append("the protections acted (bypass or governor level 2): this is what shows the OVERLOAD popup")
    if "POOL_FAIL" in by:
        why.append("MEMORY: the effect memory could not give a block")
    if "SLOW_TICK" in by:
        why.append("MAIN LOOP: %d slow UI tick(s), worst %d us" % (len(by["SLOW_TICK"]), max(e["a"] for e in by["SLOW_TICK"])))
    if any(dfl):
        why.append("FLASH: the flash counters moved (a settings or store write while the audio ran?)")
    if "SWAP_REQ" in by:
        why.append("a model / preset swap was requested during the window")
    if not why:
        why.append("NOTHING LATE, NOTHING FAILED: if you still heard the problem, it is not load. Suspect the gain path or the codec (the sound is wrong without being late).")
    return lines, why


def show_events(evs, truncated):
    if truncated:
        print("  (the log holds only the last %d events: older ones in the window were overwritten)" % pm.CAP)
    t0 = evs[0]["tick"] if evs else 0
    for e in evs:
        print("  +%8.3f s  %s" % (((e["tick"] - t0) & 0xFFFFFFFF) / 24e6, describe(e)))


def report(u, a, syms=None):
    b = snapshot(u, syms)
    evs, trunc = events_between(u, a["ev_count"], b["ev_count"]) if b["ev_count"] >= a["ev_count"] else ([], False)
    lines, why = verdict(a, b, evs)
    print("\n".join(lines))
    print("\nWHAT IT POINTS AT:")
    for w in why:
        print("  -", w)
    if evs:
        print("\nEVENTS:")
        show_events(evs, trunc)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    lv = sub.add_parser("live"); lv.add_argument("--seconds", type=float, default=20.0)
    sub.add_parser("mark")
    sub.add_parser("report")
    ap.add_argument("--port", default="Custom MIDI")
    ap.add_argument("--elf", default=str(Path(__file__).resolve().parent.parent / "build_pi32" / "pocket_amp_custom.elf"), help="the FLASHED build's ELF: where the overrun count and the late-start counters are")
    a = ap.parse_args()
    u = pm.m.Unit(a.port)
    print("BUILD", u.build_id(), flush=True)
    syms = load_syms(a.elf)
    if syms and not check_syms(u, syms):
        syms = None
    print("overrun and late-start counters: " + ("read from the ELF (it matches this unit)%s" % ("" if syms and "g_pa_isr_gap" in syms else "; the late-start counters are NOT in this build")
                                                   if syms else "NOT read (ELF missing or not the flashed build)"), flush=True)
    if a.cmd == "mark":
        s = snapshot(u, syms)
        if s["ev_magic"] != 0x474C5645:
            print("this build has no event log (magic missing)"); return 2
        MARK_FILE.write_text(json.dumps(s))
        print("marked: %d audio blocks, %d late so far, %d events. Now unplug USB, turn the volume hard for ~20 s, then plug in and run: report" % (s["entries"], s["over"], s["ev_count"]))
        return 0
    if a.cmd == "report":
        if not MARK_FILE.exists():
            print("no mark: run 'mark' first"); return 2
        return report(u, json.loads(MARK_FILE.read_text()), syms)
    s0 = snapshot(u, syms)
    if s0["ev_magic"] != 0x474C5645:
        print("this build has no event log (magic missing)"); return 2
    print("sampling for %.0f s: turn the volume now" % a.seconds, flush=True)
    t0 = time.time(); prev = s0; worst = 0.0
    while time.time() - t0 < a.seconds:
        time.sleep(0.1)
        s = snapshot(u, syms)
        late = s["over"] - prev["over"]
        xr = ((s["xrun"] - prev["xrun"]) & 0xFFFFFFFF) if "xrun" in s and "xrun" in prev else 0
        if late or xr or s["gov"] != prev["gov"] or s["isr_max_us"] > prev["isr_max_us"] or s["ev_count"] != prev["ev_count"]:
            print("  t=%5.1f s  avg %4d us  longest %5.0f us  late +%d  overruns +%d  governor %d  events +%d" % (s["t"] - t0, s["avg_us"], s["isr_max_us"], late, xr, s["gov"], s["ev_count"] - prev["ev_count"]))
        worst = max(worst, s["avg_us"]); prev = s
    print()
    return report(u, s0, syms)


if __name__ == "__main__":
    sys.exit(main())
