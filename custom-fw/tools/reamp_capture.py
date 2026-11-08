# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

"""Play a dry WAV into the unit over USB (the re-amp test build, include/pa_reamp.h), record what comes OUT of the unit on a SECOND audio input
(the unit's headphone/line out cabled to an interface), and report levels per slot. The unit's own USB microphone cannot be used: it stops
the moment the PC opens the unit's speaker.

  python custom-fw/tools/reamp_capture.py --list
  python custom-fw/tools/reamp_capture.py --rec "Line 1/2 (HeadRush" --wav custom-fw/qa/renders/usb_test/riff_48k_16bit_stereo.wav --slots 1,5,11
Output: custom-fw/qa/renders/device_reamp/<wav>_P<nn>.wav and a table (rms / peak / vs the dry)."""
import argparse, math, os, subprocess, sys, time, wave
import numpy as np, sounddevice as sd
ap = argparse.ArgumentParser()
ap.add_argument("--list", action="store_true"); ap.add_argument("--rec", help="substring of the capture device name"); ap.add_argument("--play", default="Speakers (Custom MIDI")
ap.add_argument("--wav"); ap.add_argument("--slots", default="1,5,11,12"); ap.add_argument("--gain", type=float, default=0.3); ap.add_argument("--api", default="WASAPI")
a = ap.parse_args()
def find(sub, inp):
    for i, d in enumerate(sd.query_devices()):
        if sub.lower() in d["name"].lower() and d["max_input_channels" if inp else "max_output_channels"] and a.api in sd.query_hostapis(d["hostapi"])["name"]: return i
    sys.exit("no %s device matching %r on %s (try --list)" % ("input" if inp else "output", sub, a.api))
if a.list:
    for i, d in enumerate(sd.query_devices()): print(i, sd.query_hostapis(d["hostapi"])["name"], d["name"], "in", d["max_input_channels"], "out", d["max_output_channels"], d["default_samplerate"])
    sys.exit(0)
if not (a.rec and a.wav): sys.exit("need --rec and --wav (or --list)")
w = wave.open(a.wav); sr = w.getframerate(); ch = w.getnchannels(); sw = w.getsampwidth(); raw = w.readframes(w.getnframes())
x = (np.frombuffer(raw, "<i2").astype(np.float32) / 32768.0) if sw == 2 else None
if x is None: sys.exit("16-bit WAV expected")
x = x.reshape(-1, ch) * a.gain
if ch == 1: x = np.repeat(x, 2, 1)
play_dev, rec_dev = find(a.play, False), find(a.rec, True)
from math import gcd
from scipy.signal import resample_poly
play_sr, rec_sr = int(sd.query_devices(play_dev)["default_samplerate"]), int(sd.query_devices(rec_dev)["default_samplerate"])
if play_sr != sr: g = gcd(play_sr, sr); x = resample_poly(x, play_sr // g, sr // g, axis=0).astype(np.float32)   # the WAV at the speaker's own rate
print("wav %d Hz -> play at %d Hz, record at %d Hz" % (sr, play_sr, rec_sr))
tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "andromeda_perf.py")
out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "qa", "renders", "device_reamp"); os.makedirs(out, exist_ok=True)
db = lambda v: 20 * math.log10(max(v, 1e-9))
name = os.path.splitext(os.path.basename(a.wav))[0]
print("play to %d, record from %d; dry rms %.1f dBFS" % (play_dev, rec_dev, db(float(np.sqrt((x[:, 0] ** 2).mean())))))
for s in [int(v) for v in a.slots.split(",")]:
    subprocess.run([sys.executable, tool, "ctl", "load", str(s), "--one-based"], capture_output=True); time.sleep(0.7)
    n = int((len(x) / play_sr + 2.5) * rec_sr)
    rec = sd.rec(n, samplerate=rec_sr, device=rec_dev, channels=2, dtype="float32"); time.sleep(0.3); sd.play(x, samplerate=play_sr, device=play_dev); sd.wait()
    sd.wait()
    r = rec.mean(1)
    with wave.open(os.path.join(out, "%s_P%02d.wav" % (name, s)), "wb") as o:
        o.setnchannels(1); o.setsampwidth(2); o.setframerate(rec_sr); o.writeframes((np.clip(r, -1, 1) * 32767).astype("<i2").tobytes())
    print("P%02d  recorded rms %.1f dBFS  peak %.1f dBFS" % (s, db(float(np.sqrt((r ** 2).mean()))), db(float(abs(r).max()))))
