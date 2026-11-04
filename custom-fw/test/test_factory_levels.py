"""
custom-fw/test/test_factory_levels.py - the factory presets stay level-matched (QA gate F-16).

The 40 factory presets were calibrated on real recordings (tools/import_factory_presets.py: VOLS, MASTER; method of tools/preset_level_calib.c): each DI at -10 dBFS peak, the mean over three
guitar DIs (one bass DI for a bass preset) of the output RMS in dBFS, left channel. This test repeats that measurement through the emulator DLL and holds the result to the calibration:

    every preset within +-0.75 dB of the target (-23.5 dBFS, the calibration's target), the spread over all 40 at most 1.0 dB, no output peak above -0.3 dBFS.

The recordings are local and not in git; their names live in the local file factory_level_di.cfg (dir=, guitar= x3, bass=). Without it the gate skips this test (exit 3), it does not pass it.
The DLL must be newer than src/sys/factory_presets.inc, so a stale build cannot pass for the current table.

    python custom-fw/test/test_factory_levels.py [path/to/pocket_amp_emu.dll]       exit 0 pass, 1 fail, 3 recordings missing
"""
import ctypes
import importlib.util
import math
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
REPO = os.path.dirname(FW)
CFG_CANDS = [os.path.join(REPO, "local_storage", "factory_level_di.cfg"), os.path.join(REPO, "factory_level_di.cfg"), os.path.join(FW, "factory_level_di.cfg")]
CFG = next((c for c in CFG_CANDS if os.path.exists(c)), CFG_CANDS[0])
GTR, BASS, DI_DIR = [], None, None
if os.path.exists(CFG):
    for line in open(CFG, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = (t.strip() for t in line.split("=", 1))
        if k == "dir":
            DI_DIR = os.path.join(REPO, v)
        elif k == "guitar":
            GTR.append(v)
        elif k == "bass":
            BASS = v
TARGET, TOL, SPREAD, PEAK_MAX = -23.5, 0.75, 1.0, -0.3
BLOCK = 64

missing = [f for f in GTR + ([BASS] if BASS else []) if not (DI_DIR and os.path.exists(os.path.join(DI_DIR, f)))]
if not DI_DIR or len(GTR) != 3 or not BASS or missing:
    print(f"SKIP: the calibration recordings are not configured or not on this machine ({CFG}): {', '.join(missing) or 'no config'}")
    sys.exit(3)

dll_path = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(FW, "emu", "pocket_amp_emu.dll"))
inc = os.path.join(FW, "src", "sys", "factory_presets.inc")
if os.path.getmtime(dll_path) + 1 < os.path.getmtime(inc):
    print(f"FAIL: {os.path.basename(dll_path)} is older than factory_presets.inc: rebuild the emulator DLL (emu/build_dll.ps1 or build.ps1) before the gate")
    sys.exit(1)

spec = importlib.util.spec_from_file_location("imp", os.path.join(FW, "tools", "import_factory_presets.py"))
imp = importlib.util.module_from_spec(spec)
sys.path.insert(0, os.path.join(FW, "tools"))
spec.loader.exec_module(imp)
genre = [row[1] for row in imp.build()[1]]


def load(name):
    w = wave.open(os.path.join(DI_DIR, name), "rb")
    x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float32) / 32768.0
    if w.getnchannels() == 2:
        x = x[::2]
    x = x[: (len(x) // BLOCK) * BLOCK]
    return x * (10 ** (-10 / 20) / float(np.max(np.abs(x))))


d = ctypes.CDLL(dll_path)
F = ctypes.POINTER(ctypes.c_float)
d.emu_init()
d.emu_process_audio_f32.argtypes = [F, F, F, ctypes.c_int]
gtr = [load(n) for n in GTR]
bass = load(BASS)

levels, peaks = [], []
for p in range(40):
    files = [bass] if genre[p] == "PA_GENRE_BASS" else gtr
    dbs, pk = [], -99.0
    for x in files:
        d.emu_select_preset(p)
        sil = np.zeros(8192, np.float32); lo = np.zeros_like(sil); ro = np.zeros_like(sil)
        d.emu_process_audio_f32(sil.ctypes.data_as(F), lo.ctypes.data_as(F), ro.ctypes.data_as(F), len(sil))     # the preset-change fade
        xi = np.ascontiguousarray(x); L = np.zeros_like(xi); R = np.zeros_like(xi)
        d.emu_process_audio_f32(xi.ctypes.data_as(F), L.ctypes.data_as(F), R.ctypes.data_as(F), len(xi))
        dbs.append(20 * math.log10(math.sqrt(float(np.mean(L.astype(np.float64) ** 2))) + 1e-12))
        pk = max(pk, 20 * math.log10(float(np.max(np.abs(L))) + 1e-12))
    levels.append(sum(dbs) / len(dbs))
    peaks.append(pk)

off = [(p + 1, round(l, 2)) for p, l in enumerate(levels) if abs(l - TARGET) > TOL]
hot = [(p + 1, round(k, 2)) for p, k in enumerate(peaks) if k > PEAK_MAX]
spread = max(levels) - min(levels)
print(f"levels over 40 presets: mean {sum(levels) / 40:.2f} dBFS, min {min(levels):.2f}, max {max(levels):.2f}, spread {spread:.2f} dB; loudest peak {max(peaks):.2f} dBFS")
ok = True
for name, good, msg in (("every preset within +-%.2f dB of %.1f dBFS" % (TOL, TARGET), not off, f"outside: {off}"),
                        ("spread over the 40 presets <= %.1f dB" % SPREAD, spread <= SPREAD, f"spread {spread:.2f} dB"),
                        ("no output peak above %.1f dBFS" % PEAK_MAX, not hot, f"hot: {hot}")):
    print(("  ok   " if good else "  FAIL ") + name + ("" if good else "  <- " + msg))
    ok &= good
sys.exit(0 if ok else 1)
