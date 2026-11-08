#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors
"""andromeda_perf.py - measure the Andromeda firmware on the unit over USB MIDI: processing time in ms, SRAM use, and a repeatable scenario matrix.

It speaks the measurement channel of the firmware (include/pa_meas.h, on the private diagnostic channel, manufacturer ID 0x7D). Needs python-rtmidi
(pip install python-rtmidi) and the unit plugged in by USB. Close the web editor and anything else that holds the unit's MIDI port.

  python custom-fw/tools/andromeda_perf.py info                      one reading: timing, per-stage ms, SRAM
  python custom-fw/tools/andromeda_perf.py watch [--csv log.csv]     a reading every second until Ctrl-C
  python custom-fw/tools/andromeda_perf.py matrix [--full] [--csv m.csv] [--md m.md]
                                                                      the scenario matrix: every effect type, the layer and SPACE on / off, ACCRETION
                                                                      playing and overdubbing; the test source (a synthetic guitar) drives it, nobody has to play
  python custom-fw/tools/andromeda_perf.py ctl load 6               remote control: load N | effect T | block B on|off | source MODE [LEVEL] | action A | reset
  python custom-fw/tools/andromeda_perf.py ctl source pluck 40

Times: "ISR" is the whole audio interrupt per 64-sample block (the chain, the codec, everything it does), the number the 1.451 ms budget is about.
The per-stage times are the chain's own slots (EFFECT = DLY, LAYER = MOD, SPACE = REV). "net" subtracts the baseline measured with every block off.
"""
from __future__ import annotations

import argparse
import csv
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

CMD_TIMING, CMD_MEMORY, CMD_RESET, CMD_CTL = 0x04, 0x05, 0x06, 0x07
REP_TIMING, REP_MEMORY, REP_RESET, REP_CTL = 0x84, 0x85, 0x86, 0x87
OP = {"load": 1, "effect": 2, "block": 3, "param": 4, "action": 5, "source": 6, "resetcnt": 7}
SOURCES = {"off": 0, "pluck": 1, "noise": 2, "sine": 3, "chord": 4}
SLOT_NAMES = ["DYN", "DRV", "AMP", "CAB", "EQ", "MOD", "DLY", "REV"]
STAGE_LABEL = {"MOD": "LAYER", "DLY": "EFFECT", "REV": "SPACE"}
VIEWS = {0: "PLAY", 1: "MOVING", 2: "PARAM", 3: "BANK", 4: "TUNER", 5: "SAVE", 6: "SAVED", 7: "DRUMS", 8: "SETTINGS", 9: "GLOBAL_EQ", 10: "I/O",
         11: "DISPLAY", 12: "PERF", 13: "SYSTEM", 14: "RESET", 15: "STOMP", 16: "PLAY", 17: "CHAIN"}
EFFECT_NAMES = ["GLASS CRYSTAL", "GLASS BELL", "GLASS KALIMBA", "GLASS MARIMBA", "BLOOM GARDEN", "BLOOM DUSK", "BLOOM DRIFT", "BLOOM RAW",
                "ACCRETION SOS", "ACCRETION TAPE", "ACCRETION DECAY", "ACCRETION MIRROR", "CHOIR ANGELS", "CHOIR MOURN", "CHOIR DEEP", "CHOIR INFINITE",
                "FRACTURE CLOCK", "FRACTURE WATER", "FRACTURE HEART", "FRACTURE SKIP", "CONSTELLATION STARDUST", "CONSTELLATION GRAVITY",
                "CONSTELLATION DANCE", "CONSTELLATION ORBIT"]
TICKS_PER_US = 24.0
TIMING_BYTES, TIMING_BYTES_V0, MEMORY_BYTES = 212, 180, 64          # 180 bytes = the first firmware (no core 1 fields)


# ---- the record decoders (the layout is in include/pa_meas.h; test/test_andromeda_perf.py checks them against the firmware's own encoder) ------------------------
def decode_timing(b: bytes) -> dict:
    if len(b) < TIMING_BYTES_V0 or b[0] != 1:
        raise ValueError("not a version 1 timing record (%d bytes, version %s)" % (len(b), b[0] if b else "-"))
    u32 = lambda o: struct.unpack_from("<I", b, o)[0]
    d = {
        "version": b[0], "device": bool(b[1] & 1), "source_on": bool(b[1] & 2), "source": b[2], "slot": b[3], "effect": b[4], "enabled": b[5], "view": b[6], "gov": b[7],
        "uptime_ms": u32(8), "rate": u32(12), "block": u32(16), "budget_us": u32(20),
        "isr_avg_us": u32(24), "isr_peak_us": u32(28), "isr_max_us": u32(32), "blocks": u32(36), "over": u32(40), "isr_mean_us": u32(44), "since_ms": u32(48),
        "stages": {},
    }
    for i, name in enumerate(SLOT_NAMES):
        last, mean, mx, calls = struct.unpack_from("<4I", b, 52 + 16 * i)
        d["stages"][name] = {"last_us": last / TICKS_PER_US, "mean_us": mean / TICKS_PER_US, "max_us": mx / TICKS_PER_US, "calls": calls,
                             "on": bool(d["enabled"] >> i & 1)}
    d["c1"] = {"online": False, "cut": 0, "off_reason": 0, "last_us": 0.0, "mean_us": 0.0, "max_us": 0.0, "jobs": 0, "late": 0, "wait_max_us": 0.0, "stack_used": 0}
    if len(b) >= TIMING_BYTES:                          # core 1: online, the allocator's cut, its job times (ticks), late blocks, the longest core-0 wait, stack depth
        d["c1"] = {"online": bool(b[180]), "cut": b[181], "off_reason": b[182], "last_us": u32(184) / TICKS_PER_US, "mean_us": u32(188) / TICKS_PER_US, "max_us": u32(192) / TICKS_PER_US,
                   "jobs": u32(196), "late": u32(200), "wait_max_us": u32(204) / TICKS_PER_US, "stack_used": u32(208)}
    return d


def decode_memory(b: bytes) -> dict:
    if len(b) < MEMORY_BYTES or b[0] != 1:
        raise ValueError("not a version 1 memory record (%d bytes)" % len(b))
    f = struct.unpack_from("<15I", b, 4)
    keys = ["sram_total", "data", "bss", "ble_bss", "low_ram", "ram_code", "pool_static", "pool_region", "pool_free", "dsp_pool", "dsp_used", "dsp_peak",
            "dsp_largest_free", "dsp_failed", "overlay"]
    d = dict(zip(keys, f))
    d["device"] = bool(b[1] & 1)
    d["static_total"] = d["data"] + d["bss"] + d["ble_bss"] + d["low_ram"] + d["ram_code"] + d["pool_static"]
    d["in_use"] = d["static_total"] - d["dsp_pool"] + d["dsp_used"]          # the DSP pool counts as in use only as far as it is allocated
    return d


# ---- the unit ---------------------------------------------------------------------------------------------------------------------------------------------
class Perf:
    def __init__(self, port: str = "Custom MIDI"):
        from hw_test import Unit, pack_8to7, unpack_7to8
        self.u = Unit(port)
        self.pack, self.unpack = pack_8to7, unpack_7to8

    def request(self, inner: bytes, rep: int, timeout: float = 1.5) -> bytes:
        self.u.drain()
        self.u.send([0xF0] + list(self.pack(b"\x7d" + inner)) + [0xF7])
        t0 = time.time()
        while time.time() - t0 < timeout:
            r = self.u.sysex(max(0.05, timeout - (time.time() - t0)))
            if not r:
                break
            body = self.unpack(bytes(r[1:-1]))
            if len(body) >= 4 and body[0] == 0x7D and body[1] == rep:
                n = body[2] | body[3] << 8
                return body[4:4 + n]
        raise TimeoutError("no answer (reply %#x): is this the Andromeda build with the measurement channel, and is the port free?" % rep)

    def timing(self) -> dict: return decode_timing(self.request(bytes([CMD_TIMING]), REP_TIMING))
    def memory(self) -> dict: return decode_memory(self.request(bytes([CMD_MEMORY]), REP_MEMORY))
    def reset(self) -> None: self.request(bytes([CMD_RESET]), REP_RESET)

    def ctl(self, op: str, a: int = 0, b: int = 0, c: int = 0) -> None:
        r = self.request(bytes([CMD_CTL, OP[op], a & 255, b & 255]) + struct.pack("<I", c & 0xFFFFFFFF), REP_CTL)
        if len(r) < 2 or r[1] != 0:
            raise ValueError("the unit refused %s(%d, %d, %d)" % (op, a, b, c))


# ---- text -------------------------------------------------------------------------------------------------------------------------------------------------
def fmt_ms(us: float) -> str: return "%5.3f" % (us / 1000.0)


def print_info(t: dict, m: dict) -> None:
    b = t["budget_us"] or 1451
    print("unit: slot %d  %s  view %s  uptime %.1f s  governor level %d  source %s" % (t["slot"] + 1, EFFECT_NAMES[t["effect"] % 24], VIEWS.get(t["view"], t["view"]), t["uptime_ms"] / 1000.0, t["gov"],
                                                                                    next((k for k, v in SOURCES.items() if v == t["source"]), "?")))
    print("audio interrupt per %d-frame block (budget %s ms):  mean %s ms (%.0f %%)   max %s ms (%.0f %%)   firmware average %s ms   %d blocks, %d over the budget, since reset %.1f s ago"
          % (t["block"], fmt_ms(b), fmt_ms(t["isr_mean_us"]), 100.0 * t["isr_mean_us"] / b, fmt_ms(t["isr_max_us"]), 100.0 * t["isr_max_us"] / b, fmt_ms(t["isr_avg_us"]), t["blocks"], t["over"], t["since_ms"] / 1000.0))
    c1 = t["c1"]
    if c1["online"]:
        print("core 0: ISR mean %s ms (%.0f %%)  max %s ms (%.0f %%)  |  core 1: job mean %s ms (%.0f %%)  max %s ms (%.0f %%), %d jobs, %d late blocks, stack %d of 8192 B  |  allocator: %s"
              % (fmt_ms(t["isr_mean_us"]), 100.0 * t["isr_mean_us"] / b, fmt_ms(t["isr_max_us"]), 100.0 * t["isr_max_us"] / b, fmt_ms(c1["mean_us"]), 100.0 * c1["mean_us"] / b,
                 fmt_ms(c1["max_us"]), 100.0 * c1["max_us"] / b, c1["jobs"], c1["late"], c1["stack_used"],
                 ("cut %d: core 1 runs the first %d slots of the chain (one block more latency)" % (c1["cut"], c1["cut"])) if c1["cut"] else "everything on core 0 (no hand-over, no extra latency)"))
    else:
        print("core 1: OFF (reason code %d: 1 not started, 2 held off at power-on, 3 start failed, 4 missed its block, 5 bad job, 6 stack nearly full), everything runs on core 0" % c1["off_reason"])
    print("  stage          on    last ms   mean ms    max ms")
    for n, s in t["stages"].items():
        if s["calls"] or s["on"]:
            print("  %-6s %-7s %-3s  %8s  %8s  %8s" % (n, STAGE_LABEL.get(n, ""), "on" if s["on"] else "off", fmt_ms(s["last_us"]), fmt_ms(s["mean_us"]), fmt_ms(s["max_us"])))
    kb = lambda v: "%6.1f KB" % (v / 1024.0)
    print("SRAM of %s: static %s (data %s, bss %s, BLE %s, low %s, code %s, .pool %s)" % (kb(m["sram_total"]).strip(), kb(m["static_total"]).strip(), kb(m["data"]).strip(), kb(m["bss"]).strip(),
                                                                                         kb(m["ble_bss"]).strip(), kb(m["low_ram"]).strip(), kb(m["ram_code"]).strip(), kb(m["pool_static"]).strip()))
    print("  .pool region %s: %s used, %s FREE;  effect overlay %s;  DSP pool %s: %s in use (peak %s, largest free block %s, %d failed requests)"
          % (kb(m["pool_region"]).strip(), kb(m["pool_static"]).strip(), kb(m["pool_free"]).strip(), kb(m["overlay"]).strip(), kb(m["dsp_pool"]).strip(), kb(m["dsp_used"]).strip(),
             kb(m["dsp_peak"]).strip(), kb(m["dsp_largest_free"]).strip(), m["dsp_failed"]))


# ---- the scenario matrix ----------------------------------------------------------------------------------------------------------------------------------
class Scenario:
    def __init__(self, name, etype, effect_on=True, layer=False, space=True, acc="", shimmer=False, note=""):
        self.name, self.etype, self.effect_on, self.layer, self.space, self.acc, self.shimmer, self.note = name, etype, effect_on, layer, space, acc, shimmer, note


def is_acc(t: int) -> bool:
    return 8 <= t < 12                                 # ACCRETION: the effect that has a loop to record


def scenarios(full: bool) -> list:
    out = [Scenario("baseline: every block off", 0, effect_on=False, layer=False, space=False)]
    out.append(Scenario("SPACE alone (the FDN reverb)", 0, effect_on=False, layer=False, space=True))
    out.append(Scenario("SPACE alone + SHIMMER", 0, effect_on=False, layer=False, space=True, shimmer=True))
    for t in range(24):
        fx = EFFECT_NAMES[t]
        if is_acc(t):
            out.append(Scenario(fx + " (playing)", t, acc="play"))
            if t in (8, 11) or full:
                out.append(Scenario(fx + " (overdubbing)", t, acc="dub"))
        elif t % 4 < 2 or full:                         # the quick run measures two variants of every effect; --full all four
            out.append(Scenario(fx, t))
    for t in ((0, 4, 8, 12, 16, 20) if not full else range(24)):
        out.append(Scenario(EFFECT_NAMES[t] + " + GLASS layer", t, layer=True, acc="play" if is_acc(t) else ""))
    out.append(Scenario(EFFECT_NAMES[12] + " + SHIMMER", 12, shimmer=True))
    out.append(Scenario(EFFECT_NAMES[4] + " + layer + SHIMMER", 4, layer=True, shimmer=True))
    out.append(Scenario(EFFECT_NAMES[8] + " + layer, no SPACE", 8, layer=True, space=False, acc="play"))
    out.append(Scenario(EFFECT_NAMES[4] + " + layer, no SPACE", 4, layer=True, space=False))
    if full:
        for t in range(24):
            out.append(Scenario(EFFECT_NAMES[t] + ", no SPACE", t, space=False, acc="play" if is_acc(t) else ""))
    return out


def run_scenario(p: Perf, sc: Scenario, source: str, level: int, settle: float, secs: float) -> dict:
    p.ctl("effect", sc.etype)
    time.sleep(0.4)
    p.ctl("block", 0, 1 if sc.effect_on else 0)
    p.ctl("block", 1, 1 if sc.layer else 0)
    p.ctl("block", 2, 1 if sc.space else 0)
    p.ctl("param", 7, 4, int(0.8 * 2097152) if sc.shimmer else 0)        # slot 7 = the reverb: knob 4 is the shimmer feedback (0 = off)
    if sc.shimmer: p.ctl("param", 7, 5, 0)                                  # interval +12
    p.ctl("source", SOURCES[source], level)
    if is_acc(sc.etype) and sc.effect_on:              # ACCRETION: record a loop first so there is something to play
        p.ctl("action", 3)                              # clear (AND_ACT_CLEAR)
        time.sleep(0.3)
        p.ctl("action", 1)                              # REC
        time.sleep(2.4)                                 # the loop is 2.0 s: it ends by itself and plays
        if sc.acc == "dub":
            p.ctl("action", 1)                          # DUB
    time.sleep(settle)
    p.reset()
    peak_governor = 0
    samples = []
    t0 = time.time()
    while time.time() - t0 < secs:
        time.sleep(0.5)
        t = p.timing()
        samples.append(t)
        peak_governor = max(peak_governor, t["gov"])
    if not samples:
        samples.append(p.timing())
    t = samples[-1]
    m = p.memory()
    row = {"scenario": sc.name, "isr_mean_ms": t["isr_mean_us"] / 1000.0, "isr_max_ms": t["isr_max_us"] / 1000.0, "budget_ms": t["budget_us"] / 1000.0,
           "cpu_mean_pct": 100.0 * t["isr_mean_us"] / t["budget_us"], "cpu_max_pct": 100.0 * t["isr_max_us"] / t["budget_us"],
           "blocks": t["blocks"], "over_budget": t["over"], "governor_max": peak_governor,
           "effect_mean_ms": t["stages"]["DLY"]["mean_us"] / 1000.0, "effect_max_ms": t["stages"]["DLY"]["max_us"] / 1000.0,
           "layer_mean_ms": t["stages"]["MOD"]["mean_us"] / 1000.0, "layer_max_ms": t["stages"]["MOD"]["max_us"] / 1000.0,
           "space_mean_ms": t["stages"]["REV"]["mean_us"] / 1000.0, "space_max_ms": t["stages"]["REV"]["max_us"] / 1000.0,
           "c1_online": t["c1"]["online"], "c1_cut": t["c1"]["cut"], "c1_mean_ms": t["c1"]["mean_us"] / 1000.0, "c1_max_ms": t["c1"]["max_us"] / 1000.0,
           "c1_pct": 100.0 * t["c1"]["mean_us"] / t["budget_us"], "c1_late": t["c1"]["late"],
           "dsp_pool_used_kb": m["dsp_used"] / 1024.0, "pool_free_kb": m["pool_free"] / 1024.0, "view": VIEWS.get(t["view"], t["view"])}
    return row


def cmd_matrix(p: Perf, a) -> int:
    t0 = p.timing()
    original_slot = t0["slot"]
    m0 = p.memory()
    scs = scenarios(a.full)
    print("%d scenarios, %.1f s settle + %.1f s measured each (about %.0f s). Test source: %s at %d %%.\n" % (len(scs), a.settle, a.secs, len(scs) * (a.settle + a.secs + 3.5), a.source, a.level))
    rows = []
    try:
        for sc in scs:
            r = run_scenario(p, sc, a.source, a.level, a.settle, a.secs)
            rows.append(r)
            print("%-44s core0 mean %s ms (%3.0f %%) max %s ms (%3.0f %%) | core1 %s | EFFECT %s  LAYER %s  SPACE %s ms%s"
                  % (sc.name, fmt_ms(r["isr_mean_ms"] * 1000), r["cpu_mean_pct"], fmt_ms(r["isr_max_ms"] * 1000), r["cpu_max_pct"],
                     ("%s ms (%3.0f %%) cut %d" % (fmt_ms(r["c1_mean_ms"] * 1000), r["c1_pct"], r["c1_cut"])) if r["c1_online"] and r["c1_cut"] else ("idle" if r["c1_online"] else "OFF"),
                     fmt_ms(r["effect_mean_ms"] * 1000), fmt_ms(r["layer_mean_ms"] * 1000),
                     fmt_ms(r["space_mean_ms"] * 1000), "   OVER BUDGET x%d" % r["over_budget"] if r["over_budget"] else ""))
    finally:
        try:
            p.ctl("source", 0, 0)                       # give the guitar back, whatever happened
            p.ctl("load", original_slot)
        except Exception as e:                           # the test source also expires by itself after 60 s
            print("could not restore the unit:", e)
    base = rows[0]["isr_mean_ms"] if rows else 0.0
    for r in rows:
        r["net_mean_ms"] = r["isr_mean_ms"] - base
        r["net_cpu_pct"] = 100.0 * r["net_mean_ms"] / r["budget_ms"]
    print("\nbaseline (every block off): %s ms = %.0f %% of the %s ms block - the interrupt's fixed work (codec, screen, USB, the meters)." % (fmt_ms(base * 1000), 100.0 * base / rows[0]["budget_ms"], fmt_ms(rows[0]["budget_ms"] * 1000)))
    worst = max(rows, key=lambda r: r["cpu_max_pct"])
    print("worst case: %s, max %s ms = %.0f %% of the budget" % (worst["scenario"], fmt_ms(worst["isr_max_ms"] * 1000), worst["cpu_max_pct"]))
    over = [r for r in rows if r["over_budget"] or r["c1_late"]]      # core 0 past its block, or core 1 late for its
    print("scenarios that went over the budget: %s" % (", ".join(r["scenario"] for r in over) if over else "none"))
    m1 = p.memory()
    print("SRAM: %.1f KB static, .pool region %.1f KB with %.1f KB free; effect overlay %.1f KB; DSP pool peak %.1f KB of %.1f KB"
          % (m1["static_total"] / 1024.0, m1["pool_region"] / 1024.0, m1["pool_free"] / 1024.0, m1["overlay"] / 1024.0, m1["dsp_peak"] / 1024.0, m1["dsp_pool"] / 1024.0))
    if a.csv:
        with open(a.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)
        print("wrote", a.csv)
    if a.md:
        with open(a.md, "w") as f:
            f.write("# Andromeda performance matrix\n\nTest source: %s at %d %%. Budget %.3f ms per 64-sample block. Baseline (every block off) %.3f ms.\n\n" % (a.source, a.level, rows[0]["budget_ms"], base))
            f.write("| scenario | core 0 mean ms | core 0 max ms | mean % | max % | net % | core 1 ms | core 1 % | cut | EFFECT ms | LAYER ms | SPACE ms | over budget |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n")
            for r in rows:
                f.write("| %s | %.3f | %.3f | %.0f | %.0f | %.0f | %.3f | %.0f | %d | %.3f | %.3f | %.3f | %d |\n" % (r["scenario"], r["isr_mean_ms"], r["isr_max_ms"], r["cpu_mean_pct"], r["cpu_max_pct"], r["net_cpu_pct"], r["c1_mean_ms"], r["c1_pct"], r["c1_cut"],
                                                                                               r["effect_mean_ms"], r["layer_mean_ms"], r["space_mean_ms"], r["over_budget"]))
            f.write("\nSRAM: %.1f KB static of %.0f KB; .pool region %.1f KB, %.1f KB free; effect overlay %.1f KB; DSP pool peak %.1f KB of %.1f KB.\n"
                    % (m1["static_total"] / 1024.0, m1["sram_total"] / 1024.0, m1["pool_region"] / 1024.0, m1["pool_free"] / 1024.0, m1["overlay"] / 1024.0, m1["dsp_peak"] / 1024.0, m1["dsp_pool"] / 1024.0))
        print("wrote", a.md)
    return 0


def cmd_watch(p: Perf, a) -> int:
    w = None
    f = open(a.csv, "w", newline="") if a.csv else None
    if f:
        w = csv.writer(f)
        w.writerow(["uptime_s", "slot", "effect", "view", "isr_mean_ms", "isr_max_ms", "isr_avg_ms", "over_budget", "blocks", "effect_ms", "layer_ms", "space_ms", "dsp_used_kb", "pool_free_kb"])
    print("Ctrl-C to stop. Counters run since the last reset (python andromeda_perf.py ctl reset).")
    try:
        while True:
            t, m = p.timing(), p.memory()
            row = (t["uptime_ms"] / 1000.0, t["slot"] + 1, EFFECT_NAMES[t["effect"] % 24], VIEWS.get(t["view"], t["view"]), t["isr_mean_us"] / 1000.0, t["isr_max_us"] / 1000.0, t["isr_avg_us"] / 1000.0,
                   t["over"], t["blocks"], t["stages"]["DLY"]["mean_us"] / 1000.0, t["stages"]["MOD"]["mean_us"] / 1000.0, t["stages"]["REV"]["mean_us"] / 1000.0, m["dsp_used"] / 1024.0, m["pool_free"] / 1024.0)
            print("%8.1fs slot %2d %-17s %-8s ISR mean %5.3f max %5.3f ms (budget %.3f) over %d | EFFECT %5.3f LAYER %5.3f SPACE %5.3f ms | DSP %.1f KB, .pool free %.1f KB" % (row[0], row[1], row[2], row[3], row[4], row[5], t["budget_us"] / 1000.0, row[7], row[9], row[10], row[11], row[12], row[13]))
            if w:
                w.writerow(["%.3f" % x if isinstance(x, float) else x for x in row])
                f.flush()
            time.sleep(a.interval)
    except KeyboardInterrupt:
        return 0
    finally:
        if f:
            f.close()


def cmd_ctl(p: Perf, a) -> int:
    what = a.what
    args = a.args
    if what == "load": p.ctl("load", int(args[0]) - 1 if a.one_based else int(args[0]))
    elif what == "effect": p.ctl("effect", int(args[0]))
    elif what == "block": p.ctl("block", int(args[0]), 1 if args[1].lower() in ("1", "on", "true") else 0)
    elif what == "action": p.ctl("action", int(args[0]))
    elif what == "source": p.ctl("source", SOURCES[args[0]] if args[0] in SOURCES else int(args[0]), int(args[1]) if len(args) > 1 else 40)
    elif what == "param": p.ctl("param", int(args[0]), int(args[1]), int(float(args[2]) * 2097152))
    elif what == "reset": p.reset()
    else: raise SystemExit("unknown control " + what)
    print("ok")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="Custom MIDI", help="part of the unit's MIDI port name (default %(default)s)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info")
    w = sub.add_parser("watch"); w.add_argument("--interval", type=float, default=1.0); w.add_argument("--csv")
    m = sub.add_parser("matrix")
    m.add_argument("--full", action="store_true", help="every effect with the layer and SPACE on and off (about 5 minutes)")
    m.add_argument("--source", choices=[s for s in SOURCES if s != "off"], default="pluck")
    m.add_argument("--level", type=int, default=40, help="test source level, percent of full scale")
    m.add_argument("--settle", type=float, default=1.5); m.add_argument("--secs", type=float, default=4.0)
    m.add_argument("--csv"); m.add_argument("--md")
    c = sub.add_parser("ctl"); c.add_argument("what"); c.add_argument("args", nargs="*"); c.add_argument("--one-based", action="store_true", help="'load' takes the slot number as the screen shows it (1..20)")
    a = ap.parse_args()
    try:
        p = Perf(a.port)
        if a.cmd == "info":
            print_info(p.timing(), p.memory()); return 0
        if a.cmd == "watch": return cmd_watch(p, a)
        if a.cmd == "matrix": return cmd_matrix(p, a)
        return cmd_ctl(p, a)
    except (TimeoutError, ValueError) as e:
        print("error:", e); return 1
    except Exception as e:
        print("error: %s: %s" % (type(e).__name__, e)); return 1


if __name__ == "__main__":
    sys.exit(main())
