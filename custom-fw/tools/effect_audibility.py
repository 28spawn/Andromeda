# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

"""How audible is each Andromeda effect on a dry lick? Runs the emulator (the real firmware DSP) over a dry WAV, one effect type at a time, effect ALONE at MIX 100 %
(no layer, no SPACE), and reports the wet sound against the dry: level, peak and how much of the lick the effect is actually sounding.
Target (calibrated but perceptible): wet rms between -9 and +1 dB of the dry, peak under -1 dBFS, sounding for at least 60 % of the lick.

  python custom-fw/tools/effect_audibility.py [--wav lead.wav] [--types 0,5,..] [--space] [--layer]
"""
import argparse, ctypes, math, os, tempfile, wave
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); FW = os.path.dirname(HERE)
ar = argparse.ArgumentParser()
ar.add_argument("--wav", default=os.path.join(os.path.dirname(os.path.dirname(FW)), "Alpha", "custom-fw", "qa", "renders", "synth_licks", "00_dry_lead.wav"))
ar.add_argument("--types", default=",".join(map(str, range(24)))); ar.add_argument("--gain", type=float, default=0.3)
ar.add_argument("--space", action="store_true"); ar.add_argument("--layer", action="store_true"); ar.add_argument("--mix", type=int, default=100)
a = ar.parse_args()
NAMES = ["GLASS CRYSTAL", "GLASS BELL", "GLASS KALIMBA", "GLASS MARIMBA", "BLOOM GARDEN", "BLOOM DUSK", "BLOOM DRIFT", "BLOOM RAW", "ACCRETION SOS", "ACCRETION TAPE", "ACCRETION DECAY",
         "ACCRETION MIRROR", "CHOIR ANGELS", "CHOIR MOURN", "CHOIR DEEP", "CHOIR INFINITE", "FRACTURE CLOCK", "FRACTURE WATER", "FRACTURE HEART", "FRACTURE SKIP",
         "CONST STARDUST", "CONST GRAVITY", "CONST DANCE", "CONST ORBIT"]
MACROS = [(8, 50, 55, 70), (25, 50, 65, 60), (42, 62, 45, 65), (92, 38, 35, 50), (45, 53, 60, 30), (60, 47, 80, 40), (70, 58, 50, 70), (30, 33, 20, 15), (80, 55, 40, 25), (70, 45, 60, 40),
          (92, 35, 30, 55), (60, 70, 15, 10), (42, 30, 80, 25), (58, 30, 75, 30), (75, 20, 70, 35), (75, 55, 100, 55), (50, 45, 20, 50), (70, 25, 35, 54), (50, 35, 60, 50), (30, 55, 45, 42),
          (35, 6, 30, 60), (28, 19, 20, 78), (45, 31, 55, 82), (40, 56, 40, 70)]       # keep in step with k_macro_pct in src/sys/preset.c
os.environ["PA_FLASH_FILE"] = os.path.join(tempfile.mkdtemp(prefix="pa_aud_"), "flash.bin")
d = ctypes.CDLL(os.path.join(FW, "emu", "pocket_amp_emu.dll"))
FP = ctypes.POINTER(ctypes.c_float)
d.emu_process_audio_f32.argtypes = [FP, FP, FP, ctypes.c_int]; d.emu_select_preset.argtypes = [ctypes.c_int]
DLY, REV, MOD = 6, 7, 5
try:
    import scipy.io.wavfile as wf
    sr, data = wf.read(a.wav)
    if data.dtype in (np.float32, np.float64):
        x = data.astype(np.float64)
    elif data.dtype == np.int16:
        x = data.astype(np.float64) / 32768.0
    elif data.dtype == np.int32:
        x = data.astype(np.float64) / 2147483648.0
    else:
        x = data.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    x = x * a.gain
except Exception:
    w = wave.open(a.wav); ch = w.getnchannels(); x = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float64).reshape(-1, ch).mean(1) / 32768.0 * a.gain
db = lambda v: 20 * math.log10(max(float(v), 1e-9))
dry_rms = math.sqrt((x ** 2).mean()); N = 64
x = np.concatenate([x, np.zeros(44100 * 3)]); x = np.concatenate([x, np.zeros(-len(x) % N)])
def render(t):
    d.emu_init(); d.emu_set_master_volume(100); d.emu_ui_pot(4095); d.emu_select_preset(0)
    d.emu_set_slot_enabled(DLY, 1); d.emu_set_slot_type(DLY, t)
    for k in range(4): d.emu_set_slot_param(DLY, k, MACROS[t][k])
    d.emu_set_slot_param(DLY, 4, a.mix)
    d.emu_set_slot_enabled(REV, 1 if a.space else 0); d.emu_set_slot_enabled(MOD, 1 if a.layer else 0)
    if a.space: d.emu_set_slot_param(REV, 3, 40)
    if a.layer: d.emu_set_slot_param(MOD, 4, 45)
    ib, ol, orr = (ctypes.c_float * N)(), (ctypes.c_float * N)(), (ctypes.c_float * N)()
    for _ in range(60): d.emu_process_audio_f32(ib, ol, orr, N)
    if 8 <= t < 12: d.emu_andromeda_action.argtypes = [ctypes.c_int]; d.emu_andromeda_action(1)     # ACCRETION: > records the first 2 s of the lick, then it plays
    L = []; 
    for i in range(0, len(x), N):
        for k in range(N): ib[k] = x[i + k]
        d.emu_process_audio_f32(ib, ol, orr, N); L.append(np.frombuffer(ol, dtype=np.float32).copy()); 
    return np.concatenate(L)
print("lead dry rms %.1f dBFS (gain %.2f); effect alone, MIX %d%%%s%s" % (db(dry_rms), a.gain, a.mix, ", +SPACE" if a.space else "", ", +layer" if a.layer else ""))
print("%-16s %9s %9s %9s %9s" % ("effect", "wet rms", "vs dry", "peak", "sounding"))
for t in [int(v) for v in a.types.split(",")]:
    y = render(t)[:len(x) - 44100 * 3]
    rms = math.sqrt((y.astype(np.float64) ** 2).mean()); pk = abs(y).max()
    win = 4410; act = np.mean([math.sqrt((y[i:i + win].astype(np.float64) ** 2).mean()) > dry_rms * 0.05 for i in range(0, len(y) - win, win)])
    d_ = db(rms) - db(dry_rms)
    flag = "QUIET" if d_ < -9 else ("LOUD" if d_ > 1 else "")
    if pk > 0.89: flag += " PEAK"
    if act < 0.6: flag += " SPARSE"
    print("%-16s %8.1f  %+8.1f  %8.1f  %7.0f%%  %s" % (NAMES[t], db(rms), d_, db(pk), act * 100, flag))
if os.environ.get("AUD_DEBUG"):
    a0 = render(0)[:len(x)-44100*3]; a21 = render(21)[:len(x)-44100*3]; xx = x[:len(a0)]
    c = lambda p, q: float(np.dot(p, q) / (np.linalg.norm(p) * np.linalg.norm(q) + 1e-12))
    print("corr(out0,dry)=%.3f corr(out21,dry)=%.3f corr(out0,out21)=%.3f" % (c(a0, xx), c(a21, xx), c(a0, a21)))
