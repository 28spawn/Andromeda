# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

"""Run dry WAV inputs through the Andromeda emulator, one render per factory slot (P01..P12), and report levels.

Usage: python custom-fw/tools/render_dry_inputs.py [in_dir] [out_dir] [--slots 0,1,..]
Defaults: in_dir = ../Alpha/custom-fw/qa/renders/synth_licks (the three 00_dry_*.wav), out_dir = custom-fw/qa/renders/andromeda_dry
Needs custom-fw/emu/pocket_amp_emu.dll built with build_dll.ps1 -Product andromeda.
"""
import ctypes, math, os, struct, sys, tempfile, wave, glob
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__)); FW = os.path.dirname(HERE)
args = [a for a in sys.argv[1:] if not a.startswith("--")]
slots = list(range(12))
for a in sys.argv[1:]:
    if a.startswith("--slots="): slots = [int(x) for x in a[8:].split(",")]
IN = args[0] if args else os.path.join(os.path.dirname(os.path.dirname(FW)), "Alpha", "custom-fw", "qa", "renders", "synth_licks")
OUT = args[1] if len(args) > 1 else os.path.join(FW, "qa", "renders", "andromeda_dry")
os.makedirs(OUT, exist_ok=True)
os.environ["PA_FLASH_FILE"] = os.path.join(tempfile.mkdtemp(prefix="pa_dry_"), "flash.bin")
d = ctypes.CDLL(os.path.join(FW, "emu", "pocket_amp_emu.dll"))
FP = ctypes.POINTER(ctypes.c_float)
d.emu_process_audio_f32.argtypes = [FP, FP, FP, ctypes.c_int]
d.emu_select_preset.argtypes = [ctypes.c_int]; d.emu_set_master_volume.argtypes = [ctypes.c_int]; d.emu_ui_pot.argtypes = [ctypes.c_uint16]
N = 64

def read_mono(p):
    try:
        import scipy.io.wavfile as wf
        sr, data = wf.read(p)
        if data.dtype == np.float32 or data.dtype == np.float64:
            x = data.astype(np.float32)
        elif data.dtype == np.int16:
            x = data.astype(np.float32) / 32768.0
        elif data.dtype == np.int32:
            x = data.astype(np.float32) / 2147483648.0
        else:
            x = data.astype(np.float32)
        if x.ndim > 1:
            x = x.mean(axis=1)
        return list(x)
    except Exception:
        with wave.open(p) as w:
            ch, sw, n = w.getnchannels(), w.getsampwidth(), w.getnframes(); raw = w.readframes(n)
        if sw == 2:
            s = struct.unpack("<%dh" % (n * ch), raw)
            return [sum(s[i * ch:(i + 1) * ch]) / (32768.0 * ch) for i in range(n)]
        else:
            raise ValueError(f"Unsupported WAV sample width: {sw}")

def render(x, slot):
    d.emu_init(); d.emu_set_master_volume(100); d.emu_ui_pot(4095); d.emu_select_preset(slot)
    ib, ol, orr = (ctypes.c_float * N)(), (ctypes.c_float * N)(), (ctypes.c_float * N)()
    pad = x + [0.0] * (44100 * 2)                       # two seconds of tail
    pad += [0.0] * (-len(pad) % N)
    L, R = [], []
    for _ in range(40): d.emu_process_audio_f32(ib, ol, orr, N)    # let the slot settle
    for i in range(0, len(pad), N):
        for k in range(N): ib[k] = pad[i + k]
        d.emu_process_audio_f32(ib, ol, orr, N); L += ol[:]; R += orr[:]
    return L, R

def db(v): return 20 * math.log10(max(v, 1e-9))
def write(p, L, R):
    with wave.open(p, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
        w.writeframes(b"".join(struct.pack("<hh", int(max(-1, min(1, a)) * 32767), int(max(-1, min(1, b)) * 32767)) for a, b in zip(L, R)))

files = sorted(glob.glob(os.path.join(IN, "00_dry_*.wav")))
if not files:
    files = sorted(glob.glob(os.path.join(IN, "*.wav")))
if not files: sys.exit("no WAV files in " + IN)
print("%-14s %-5s %9s %9s %9s %s" % ("input", "slot", "dry rms", "out rms", "out peak", ""))
bad = 0
for f in files:
    x = read_mono(f); name = os.path.basename(f)[:-4]
    drms = math.sqrt(sum(v * v for v in x) / len(x))
    for s in slots:
        L, R = render(x, s)
        rms = math.sqrt(sum(v * v + w * w for v, w in zip(L, R)) / (2 * len(L))); pk = max(max(map(abs, L)), max(map(abs, R)))
        flag = "CLIP" if pk >= 0.99 else ("SILENT" if rms < 1e-4 else "")
        bad += bool(flag)
        print("%-14s P%02d  %8.1f  %8.1f  %8.1f  %s" % (name, s + 1, db(drms), db(rms), db(pk), flag))
        write(os.path.join(OUT, "%s_P%02d.wav" % (name, s + 1)), L, R)
print("wrote %d files to %s; %d flagged" % (len(files) * len(slots), OUT, bad))
