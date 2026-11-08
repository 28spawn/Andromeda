# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

"""The PC side of the measurement channel (tools/andromeda_perf.py) against the firmware's own encoder (src/sys/pa_meas.c).

 1. builds and runs test/test_meas.c, which prints the two records it encodes, and decodes them with the tool's decoders: one layout, checked on both sides;
 2. drives the tool's Perf class through a fake MIDI unit that answers the way the firmware does (8-to-7 packed frames, other SysEx in between), so the request /
    reply loop, the control encoding and the refusal handling are exercised without the hardware;
 3. runs the scenario list and the table printer on those answers.
Run: python custom-fw/test/test_andromeda_perf.py   (needs clang on the PATH)"""
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import andromeda_perf as ap                       # noqa: E402
from hw_test import pack_8to7, unpack_7to8        # noqa: E402

fails = 0


def check(ok, msg):
    global fails
    print(("  [PASS] " if ok else "  [FAIL] ") + msg)
    if not ok:
        fails += 1


def build_and_run() -> dict:
    exe = ROOT / "test" / "_test_meas_tmp.exe"
    cmd = ["clang", "-O1", "-I" + str(ROOT / "include"), "-DPA_PRODUCT_ANDROMEDA=1", "-DPA_HOST_SIM=1", str(ROOT / "test" / "test_meas.c"), str(ROOT / "src" / "sys" / "pa_meas.c"), str(ROOT / "src" / "sys" / "pa_diag.c"), "-o", str(exe), "-lm"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr)
        raise SystemExit("could not build test_meas.c")
    out = subprocess.run([str(exe)], capture_output=True, text=True).stdout
    exe.unlink(missing_ok=True)
    recs = {}
    for line in out.splitlines():
        if line.startswith("TIMING "):
            recs["timing"] = bytes.fromhex(line.split()[1])
        elif line.startswith("MEMORY "):
            recs["memory"] = bytes.fromhex(line.split()[1])
    return recs


class FakeUnit:
    """What Unit (tools/hw_test.py) offers, answering like the firmware: every frame is F0 <packed 7D cmd args> F7, every reply F0 <packed 7D rep len16 record> F7."""
    def __init__(self, timing: bytes, memory: bytes, noise: bool = True):
        self.timing, self.memory, self.noise = timing, memory, noise
        self.out = []
        self.log = []
        self.refuse = set()

    def drain(self): self.out.clear()

    def send(self, msg):
        assert msg[0] == 0xF0 and msg[-1] == 0xF7, "a SysEx frame"
        assert all(b < 0x80 for b in msg[1:-1]), "7-bit clean on the wire"
        inner = unpack_7to8(bytes(msg[1:-1]))
        assert inner[0] == 0x7D
        cmd = inner[1]
        self.log.append(inner)
        def reply(rep, data): self.out.append([0xF0] + list(pack_8to7(bytes([0x7D, rep, len(data) & 255, len(data) >> 8]) + data)) + [0xF7])
        if self.noise:
            self.out.append([0xF0, 0x00, 0x32, 0x45, 0x01, 0xF7])        # an unrelated SysEx first (another device's reply)
        if cmd == ap.CMD_TIMING: reply(ap.REP_TIMING, self.timing)
        elif cmd == ap.CMD_MEMORY: reply(ap.REP_MEMORY, self.memory)
        elif cmd == ap.CMD_RESET: reply(ap.REP_RESET, b"\x00")
        elif cmd == ap.CMD_CTL: reply(ap.REP_CTL, bytes([inner[2], 1 if inner[2] in self.refuse else 0]))

    def sysex(self, timeout=1.0):
        return self.out.pop(0) if self.out else None


def make_perf(unit) -> ap.Perf:
    p = ap.Perf.__new__(ap.Perf)
    p.u, p.pack, p.unpack = unit, pack_8to7, unpack_7to8
    return p


def main() -> int:
    print("=================================================================\nANDROMEDA performance tool (PC side)\n=================================================================")
    recs = build_and_run()
    print("[1] the firmware's records, decoded by the tool")
    t = ap.decode_timing(recs["timing"])
    check(t["slot"] == 6 and t["effect"] == 5 and t["view"] == 17 and t["gov"] == 1, "slot %d, effect %d (%s), view %s, governor %d" % (t["slot"], t["effect"], ap.EFFECT_NAMES[t["effect"]], ap.VIEWS[t["view"]], t["gov"]))
    check(t["budget_us"] == 1451 and t["rate"] == 44118 and t["block"] == 64, "budget %d us, %d Hz, %d frames" % (t["budget_us"], t["rate"], t["block"]))
    check(t["blocks"] == 100 and t["over"] == 1 and t["isr_max_us"] == 1458 and abs(t["isr_mean_us"] - 608) <= 2, "ISR: %d blocks, %d over, max %d us, mean %d us" % (t["blocks"], t["over"], t["isr_max_us"], t["isr_mean_us"]))
    check(abs(t["stages"]["DLY"]["mean_us"] - 300.0) < 0.01 and abs(t["stages"]["DLY"]["max_us"] - 400.0) < 0.01 and t["stages"]["DLY"]["calls"] == 100, "the DLY stage: mean %.0f us, max %.0f us" % (t["stages"]["DLY"]["mean_us"], t["stages"]["DLY"]["max_us"]))
    check(abs(t["stages"]["REV"]["mean_us"] - 100.0) < 0.01, "the REV stage: %.0f us" % t["stages"]["REV"]["mean_us"])
    check(t["stages"]["DLY"]["on"] and t["stages"]["REV"]["on"] and t["stages"]["MOD"]["on"] and not t["stages"]["DYN"]["on"], "the enabled-block mask 0xE0 = MOD, DLY, REV")
    m = ap.decode_memory(recs["memory"])
    check(m["sram_total"] == 0x80000 and m["dsp_pool"] == 65536 and m["dsp_used"] == 41000 and m["dsp_peak"] == 52000 and m["overlay"] == 150548, "memory: SRAM %d, DSP pool %d / %d in use / %d peak, overlay %d" % (m["sram_total"], m["dsp_pool"], m["dsp_used"], m["dsp_peak"], m["overlay"]))

    print("[2] the request / reply loop through a fake unit")
    u = FakeUnit(recs["timing"], recs["memory"])
    p = make_perf(u)
    check(p.timing()["blocks"] == 100, "timing: the reply is found behind an unrelated SysEx")
    check(p.memory()["overlay"] == 150548, "memory")
    p.reset(); check(u.log[-1][1] == ap.CMD_RESET, "reset sends command 06")
    p.ctl("load", 7); check(u.log[-1][1:4] == bytes([ap.CMD_CTL, 1, 7]), "ctl load 7: op 1, a 7")
    p.ctl("param", 6, 4, 1048576); check(u.log[-1][2:4] == bytes([4, 6]) and struct.unpack("<I", u.log[-1][5:9])[0] == 1048576, "ctl param: slot 6, knob 4, the Q21 value little endian")
    p.ctl("source", ap.SOURCES["chord"], 35); check(u.log[-1][2:5] == bytes([6, 4, 35]), "ctl source chord 35 %")
    u.refuse.add(2)
    try:
        p.ctl("effect", 99); check(False, "a refused control raises")
    except ValueError:
        check(True, "a refused control raises ValueError")
    silent = FakeUnit(recs["timing"], recs["memory"]); silent.timing = recs["timing"]
    silent.send = lambda msg: None
    try:
        make_perf(silent).request(bytes([ap.CMD_TIMING]), ap.REP_TIMING, timeout=0.2); check(False, "no answer raises")
    except TimeoutError as e:
        check("measurement channel" in str(e), "no answer raises TimeoutError with a hint")

    print("[3] the scenario list and the matrix on the fake unit")
    quick, full = ap.scenarios(False), ap.scenarios(True)
    check(len(quick) >= 25 and len(full) > len(quick), "%d scenarios quick, %d full" % (len(quick), len(full)))
    check(quick[0].effect_on is False and quick[0].space is False and quick[0].layer is False, "the first scenario is the baseline with every block off")
    check({s.etype // 4 for s in quick} == set(range(6)), "every one of the 6 effects is measured (two variants each in the quick run)")
    check({s.etype for s in full} == set(range(24)), "the full run measures all 24 effect types")
    check(any(s.layer for s in quick) and any(not s.space for s in quick) and any(s.shimmer for s in quick), "the layer, SPACE-off and SHIMMER cases are in")
    import io, contextlib
    class A: pass
    a = A(); a.full = False; a.source = "pluck"; a.level = 40; a.settle = 0.0; a.secs = 0.0; a.csv = str(ROOT / "test" / "_perf_tmp.csv"); a.md = str(ROOT / "test" / "_perf_tmp.md")
    sleeps = []
    ap.time.sleep = lambda s: sleeps.append(s)
    real_time = ap.time.time
    clock = [0.0]
    ap.time.time = lambda: (clock.__setitem__(0, clock[0] + 0.25), clock[0])[1]
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = ap.cmd_matrix(make_perf(FakeUnit(recs["timing"], recs["memory"], noise=False)), a)
    ap.time.time = real_time
    out = buf.getvalue()
    check(rc == 0 and "baseline (every block off)" in out and "worst case:" in out, "the matrix runs and prints its summary")
    csvp, mdp = Path(a.csv), Path(a.md)
    rows = csvp.read_text().strip().splitlines()
    check(len(rows) == len(quick) + 1 and rows[0].startswith("scenario,isr_mean_ms"), "the CSV has a header and one row per scenario (%d rows)" % (len(rows) - 1))
    check("| scenario |" in mdp.read_text() and "SRAM:" in mdp.read_text(), "the markdown table and the SRAM line are written")
    csvp.unlink(missing_ok=True); mdp.unlink(missing_ok=True)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ap.print_info(t, m)
    txt = buf.getvalue()
    check("EFFECT" in txt and "LAYER" in txt and "SPACE" in txt and "FREE" in txt, "info prints the stages by their screen names and the free .pool memory")

    print("\n%d FAILURE(S)" % fails if fails else "\nALL PERFORMANCE TOOL TESTS PASSED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
