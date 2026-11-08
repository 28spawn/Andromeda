# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

"""The scenario matrix of andromeda_perf.py, but with a dry WAV (default: the lead lick) played into the unit over USB (the re-amp test build, include/pa_reamp.h)
and the unit's output recorded on a second audio input. Per scenario it saves the recording and reports how loud the processed sound is against the dry one.

  python custom-fw/tools/reamp_matrix.py --rec "Line 1/2 (HeadRush" --csv reamp.csv --md reamp.md [--full] [--wav dry.wav] [--only GLASS]
Wav files go to custom-fw/qa/renders/device_matrix/. The unit's output goes to the recording input (cable it; keep that input's gain low)."""
import argparse, csv, math, os, sys, time, wave
from math import gcd
import numpy as np, sounddevice as sd
from scipy.signal import resample_poly
HERE = os.path.dirname(os.path.abspath(__file__)); FW = os.path.dirname(HERE); sys.path.insert(0, HERE)
import andromeda_perf as ap
ar = argparse.ArgumentParser()
ar.add_argument("--rec", required=True); ar.add_argument("--play", default="Speakers (Custom MIDI"); ar.add_argument("--api", default="WASAPI")
ar.add_argument("--wav", default=os.path.join(os.path.dirname(os.path.dirname(FW)), "Alpha", "custom-fw", "qa", "renders", "synth_licks", "00_dry_lead.wav"))
ar.add_argument("--gain", type=float, default=0.3); ar.add_argument("--full", action="store_true"); ar.add_argument("--only", default="")
ar.add_argument("--csv"); ar.add_argument("--md"); ar.add_argument("--port", default="Custom MIDI")
a = ar.parse_args()
def find(sub, inp):
    for i, d in enumerate(sd.query_devices()):
        if sub.lower() in d["name"].lower() and d["max_input_channels" if inp else "max_output_channels"] and a.api in sd.query_hostapis(d["hostapi"])["name"]: return i
    sys.exit("no %s device matching %r" % ("input" if inp else "output", sub))
w = wave.open(a.wav); sr, ch = w.getframerate(), w.getnchannels(); x = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float32).reshape(-1, ch) / 32768.0 * a.gain
if ch == 1: x = np.repeat(x, 2, 1)
pd, rd = find(a.play, False), find(a.rec, True)
psr, rsr = int(sd.query_devices(pd)["default_samplerate"]), int(sd.query_devices(rd)["default_samplerate"])
if psr != sr: g = gcd(psr, sr); x = resample_poly(x, psr // g, sr // g, axis=0).astype(np.float32)
dur = len(x) / psr
db = lambda v: 20 * math.log10(max(float(v), 1e-9))
dry_rms = db(np.sqrt((x[:, 0] ** 2).mean()))
out = os.path.join(FW, "qa", "renders", "device_matrix"); os.makedirs(out, exist_ok=True)
p = ap.Perf(a.port); t0 = p.timing(); slot0 = t0["slot"]
scs = [s for s in ap.scenarios(a.full) if a.only.lower() in s.name.lower()]
print("lead: %.1f s at %.1f dBFS rms; %d scenarios; play dev %d (%d Hz), record dev %d (%d Hz)" % (dur, dry_rms, len(scs), pd, psr, rd, rsr))
rows = []
try:
    for i, sc in enumerate(scs):
        p.ctl("source", ap.SOURCES["off"] if "off" in ap.SOURCES else 0, 0)
        p.ctl("effect", sc.etype); time.sleep(0.4)
        p.ctl("block", 0, 1 if sc.effect_on else 0); p.ctl("block", 1, 1 if sc.layer else 0); p.ctl("block", 2, 1 if sc.space else 0)
        p.ctl("param", 7, 4, int(0.8 * 2097152) if sc.shimmer else 0)
        if sc.shimmer: p.ctl("param", 7, 5, 0)
        acc = ap.is_acc(sc.etype) and sc.effect_on
        if acc: p.ctl("action", 3); time.sleep(0.3)
        n = int((dur + 2.5) * rsr)
        rec = sd.rec(n, samplerate=rsr, device=rd, channels=2, dtype="float32"); time.sleep(0.3)
        sd.play(x, samplerate=psr, device=pd)
        if acc:                                          # the lick plays in; the loop records its first 2 s and then plays
            time.sleep(0.2); p.ctl("action", 1)
            if sc.acc == "dub": time.sleep(2.4); p.ctl("action", 1)
        sd.wait(); sd.wait()
        r = rec.mean(1)
        safe = "".join(c if c.isalnum() else "_" for c in sc.name)[:50]
        with wave.open(os.path.join(out, "%02d_%s.wav" % (i, safe)), "wb") as o:
            o.setnchannels(1); o.setsampwidth(2); o.setframerate(rsr); o.writeframes((np.clip(r, -1, 1) * 32767).astype("<i2").tobytes())
        rms, pk = db(np.sqrt((r ** 2).mean())), db(abs(r).max())
        rows.append({"scenario": sc.name, "rms_dbfs": round(rms, 1), "peak_dbfs": round(pk, 1), "vs_dry_db": round(rms - dry_rms, 1)})
        flag = "SILENT" if pk < -80 else ("CLIP" if pk > -0.5 else "")
        print("%-44s rms %6.1f  peak %6.1f  vs dry %+6.1f dB %s" % (sc.name, rms, pk, rms - dry_rms, flag))
finally:
    p.ctl("source", ap.SOURCES["off"] if "off" in ap.SOURCES else 0, 0)
    try: p.ctl("load", slot0 - 1 if slot0 > 0 else 0)
    except Exception: pass
if a.csv:
    with open(a.csv, "w", newline="") as f: cw = csv.DictWriter(f, fieldnames=list(rows[0])); cw.writeheader(); cw.writerows(rows)
if a.md:
    with open(a.md, "w") as f:
        f.write("| scenario | rms dBFS | peak dBFS | vs dry dB |\n|---|---|---|---|\n")
        for r in rows: f.write("| %s | %s | %s | %+.1f |\n" % (r["scenario"], r["rms_dbfs"], r["peak_dbfs"], r["vs_dry_db"]))
print("wrote %d recordings to %s" % (len(rows), out))
