"""Factory presets 1..40: baseline preset bank, imported.

  [1] src/sys/factory_presets.inc is exactly what tools/import_factory_presets.py makes from
      sk17_am4_preset.bin (nobody edits the bank by hand)
  [2] the bank decodes cleanly: 40 configured records + 40 blank templates, every type in
      range, the bass presets on the bass amps and cabs, the clean ones on the clean amps
  [3] through the firmware (the emulator DLL): each preset loads the record's amp, cab,
      FX / MOD / DLY / REV types and on/off, and its knobs land where the record puts them
      (with the documented mapping: the cab's high-cut inverted, the FX level x2)
  [4] each preset plays: the DI phrase of its kind (clean chords, a rock riff, a metal chug,
      a bass line) comes out audible, under 0 dBFS, and every preset sits within 8 dB of the
      bank's median level (a guard against gross errors; the calibration is on real recordings)

    python custom-fw/test/test_factory_am4.py [path/to/pocket_amp_emu.dll]
"""
import ctypes
import os
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(FW, "tools"))
import import_factory_presets as imp  # noqa: E402

fails = 0


def check(ok, msg):
    global fails
    print(("  ok   " if ok else "  FAIL ") + msg)
    if not ok:
        fails += 1


print("=== [1] the table is the generator's ===")
r = subprocess.run([sys.executable, os.path.join(FW, "tools", "import_factory_presets.py"), "--check"],
                   capture_output=True, text=True)
check(r.returncode == 0, "src/sys/factory_presets.inc matches baseline preset table" +
      ("" if r.returncode == 0 else ": " + (r.stdout + r.stderr).strip()))

print("=== [2] the bank decodes cleanly ===")
data, rows = imp.build()
blank = all(not any(imp.decode(data[i * imp.REC:(i + 1) * imp.REC])["on"]) for i in range(40, 80))
check(blank, "records 41..80 are blank templates (nothing on)")
bass_ok = True
for _n, genre, _v, amp, cab, mask, *_ in rows:
    cab_on = bool(mask & (1 << imp.CAB))
    if genre == "PA_GENRE_BASS":
        bass_ok &= amp >= 15 and (cab >= 15 or not cab_on)
    else:
        bass_ok &= amp < 15 and (cab < 15 or not cab_on)
check(bass_ok, "bass presets use the bass amps (16..20) and bass cabs; the rest the guitar ones")
check(all(row[5] & (1 << imp.AMP) for row in rows), "every preset has its amp on")

print("=== [3] through the firmware ===")
dll_path = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(FW, "emu", "pocket_amp_emu.dll"))
# The emulator keeps its presets in a flash image it finds relative to the working folder
# (custom-fw/emu/pocket_amp_flash.bin, shared with the other suites and the user's own
# sessions, where a saved factory slot rightly stays a user preset). Run from an empty folder:
# no image is found, the flash is a fresh one in memory, and the presets are the factory ones.
import tempfile  # noqa: E402
os.chdir(tempfile.mkdtemp(prefix="pa_factory_"))
# the emulator reads the stock amps and cabs from its simulated flash, which it fills from the
# baseline bundle - tell it where that is (it only searches relative paths otherwise)
import mefcs_bundles  # noqa: E402
os.environ["PA_STOCK_BUNDLE"] = mefcs_bundles.find("sk17_am4_ampCab.bin")
d = ctypes.CDLL(dll_path)
F = ctypes.POINTER(ctypes.c_float)
d.emu_process_audio_f32.argtypes = [F, F, F, ctypes.c_int]
d.emu_init()
bad_t, bad_k = [], []
SLOT_NAMES = {imp.DYN: "FX", imp.AMP: "AMP", imp.CAB: "CAB", imp.MOD: "MOD", imp.DLY: "DLY", imp.REV: "REV"}
for i, (name, _g, _v, amp, cab, mask, types, pct, rec) in enumerate(rows):
    d.emu_select_preset(i)
    for s in SLOT_NAMES:
        if d.emu_get_slot_type(s) != types[s]:
            bad_t.append(f"P{i + 1} {SLOT_NAMES[s]} {d.emu_get_slot_type(s)} != {types[s]}")
    # The record's AMP slots are Gain, LEVEL, Bass, Mid, Treble (matched value for value against screenshots of the original app, 2026-11-03; qa/preset-decode/DECODED-FIELDS.md).
    # The firmware's knobs 0..3 are GAIN, BASS, MID, TREBLE, so they come from record slots 0, 2, 3, 4; the stock Level (slot 1) is not copied (the preset volume carries the level).
    want = [(imp.AMP, 0, rec["amp"][0]), (imp.AMP, 1, rec["amp"][2]), (imp.AMP, 2, rec["amp"][3]), (imp.AMP, 3, rec["amp"][4])] + \
           [(imp.CAB, 1, rec["cab"][1]), (imp.CAB, 2, 100 - rec["cab"][2])] + \
           [(imp.DLY, k, rec["dly"][k]) for k in range(3)] + \
           [(imp.REV, 0, rec["rev"][0]), (imp.REV, 3, rec["rev"][1])] + \
           [(imp.MOD, k, rec["mod"][k]) for k in range(3)] + \
           [(imp.DYN, 0, rec["fx"][0]), (imp.DYN, 3, imp.dyn_level(rec["fx"][3]))]
    for s, k, v in want:
        got = d.emu_get_slot_param(s, k)
        if abs(got - imp.clamp(v)) > 1:
            bad_k.append(f"P{i + 1} {SLOT_NAMES[s]} knob {k}: {got} != {v}")
check(not bad_t, f"40 presets load the record's types ({'; '.join(bad_t[:4]) or 'all match'})")
check(not bad_k, f"the knobs land where the record puts them ({'; '.join(bad_k[:4]) or 'all match'})")


def load_wav(fn):
    w = wave.open(os.path.join(FW, "emu", "samples", fn))
    x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float32) / 32768.0
    return x * (10 ** (-10 / 20) / max(np.abs(x).max(), 1e-9))     # peak -10 dBFS, as the calibration


def play(x):
    n = len(x) - len(x) % 64
    L = np.zeros(n, np.float32)
    R = np.zeros(n, np.float32)
    for k in range(0, n, 64):
        xi = np.ascontiguousarray(x[k:k + 64])
        d.emu_process_audio_f32(xi.ctypes.data_as(F), L[k:].ctypes.data_as(F), R[k:].ctypes.data_as(F), 64)
    return L


print("=== [4] each preset plays ===")
# each preset on the DI phrase of its kind (the bank is level-matched on real recordings of
# each kind; a clean patch fed a rock riff, or a metal one fed chords, is not a fair reading)
DI = {"PA_GENRE_CLEAN": "di_clean_chords.wav", "PA_GENRE_BASS": "di_bass_line.wav",
      "PA_GENRE_METAL": "di_metal_chug.wav"}
phrases = {g: load_wav(f)[:44100 * 6] for g, f in DI.items()}
phrases["other"] = load_wav("di_rock_riff.wav")[:44100 * 6]
levels, quiet, hot = [], [], []
for i, row in enumerate(rows):
    d.emu_select_preset(i)
    play(np.zeros(22050, np.float32))                       # the preset-change fade
    y = play(phrases.get(row[1], phrases["other"]))
    rms = 20 * np.log10(np.sqrt(np.mean(y[22050:].astype(np.float64) ** 2)) + 1e-12)
    pk = 20 * np.log10(np.abs(y).max() + 1e-12)
    levels.append(rms)
    if rms < -50:
        quiet.append(f"P{i + 1} {rms:.0f}")
    if pk >= -0.01:
        hot.append(f"P{i + 1} {pk:.1f}")
med = float(np.median(levels))
# The bank is level-matched to ~1 dB on real recordings (tools/import_factory_presets.py,
# VOLS); the project's synthetic DI phrases differ from real playing in density and crest, so
# this is a guard against gross errors (a muted or 20 dB-off preset), not the calibration.
TOL_DB = 8.0
off = [f"P{i + 1} {lv - med:+.1f}" for i, lv in enumerate(levels) if abs(lv - med) > TOL_DB]
check(not quiet, f"every preset is audible ({', '.join(quiet) or 'all'})")
check(not hot, f"no preset hits 0 dBFS ({', '.join(hot) or 'none'})")
check(not off, f"all within {TOL_DB:.0f} dB of the median {med:.1f} dBFS ({', '.join(off) or 'all'}; "
               f"spread {min(levels) - med:+.1f} .. {max(levels) - med:+.1f} dB)")

print("\n" + ("FACTORY AM4 OK" if fails == 0 else f"{fails} FAILURE(S)"))
sys.exit(1 if fails else 0)
