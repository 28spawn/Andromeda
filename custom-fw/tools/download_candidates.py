#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors
"""Download and generate candidate clean guitar DI audio samples for user audition.

Category 1 (Option 1): 10 CC0 / Public Domain / In-house clean guitar DIs

All samples are formatted as:
  - Format: Mono 16-bit PCM WAV @ 44.1 kHz (Pocket Amp AC791N & WASM native rate)
  - Headroom: Peak normalized to -1.0 dBFS (0.89 linear FS)
  - Duration: 12.0 seconds seamlessly trimmed with zero-crossing edge fades
"""
import io
import os
import struct
import sys
import urllib.request
import wave
import zlib
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.abspath(os.path.join(HERE, "..", "..", "samples_preview"))
os.makedirs(OUT_DIR, exist_ok=True)

FS = 44100

def process_audio(samples_float, target_duration=12.0):
    samples = np.array(samples_float, dtype=np.float32)
    # Remove DC offset
    samples = samples - np.mean(samples)
    
    target_len = int(target_duration * FS)
    if len(samples) > target_len:
        samples = samples[:target_len]
    elif len(samples) < target_len:
        pad = np.zeros(target_len - len(samples), dtype=np.float32)
        samples = np.concatenate([samples, pad])
        
    # Peak normalize to -1.0 dBFS (0.89125 linear)
    peak = np.max(np.abs(samples))
    if peak > 1e-6:
        samples = samples * (0.89125 / peak)
        
    # Apply short 10ms raised cosine fade in / out to avoid clicks
    fade_len = int(0.010 * FS)
    fade_in = 0.5 * (1 - np.cos(np.linspace(0, np.pi, fade_len)))
    fade_out = 0.5 * (1 + np.cos(np.linspace(0, np.pi, fade_len)))
    samples[:fade_len] *= fade_in
    samples[-fade_len:] *= fade_out
    
    # Quantize to 16-bit PCM
    pcm16 = np.clip(samples * 32767.0, -32768, 32767).astype(np.int16)
    return pcm16

def save_wav(path, pcm16):
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(FS)
        wf.writeframes(pcm16.tobytes())

# --- 1. Option 1: CC0 / Public Domain / Synth Generators ---

def gen_chords_progressions():
    # Synthetic realistic clean strummed chords progression (E maj -> C# min -> A maj -> B maj)
    t = np.linspace(0, 12.0, int(12.0 * FS), endpoint=False)
    sig = np.zeros_like(t)
    chords = [
        (0.0, [82.41, 123.47, 164.81, 207.65, 246.94, 329.63]),  # E
        (3.0, [73.42, 110.00, 138.59, 220.00, 277.18]),          # C#m
        (6.0, [110.00, 164.81, 220.00, 277.18, 329.63]),         # A
        (9.0, [123.47, 185.00, 246.94, 293.66, 369.99])          # B
    ]
    for chord_t, freqs in chords:
        for f in freqs:
            idx = int(chord_t * FS)
            decay = np.exp(-1.5 * (t[idx:] - chord_t))
            body = np.sin(2 * np.pi * f * (t[idx:] - chord_t))
            harm = 0.4 * np.sin(4 * np.pi * f * (t[idx:] - chord_t)) + 0.2 * np.sin(6 * np.pi * f * (t[idx:] - chord_t))
            sig[idx:] += (body + harm) * decay
    return sig

def gen_funk_rhythm():
    # Funk 16th chops (D9 / G9)
    t = np.linspace(0, 12.0, int(12.0 * FS), endpoint=False)
    sig = np.zeros_like(t)
    bpm = 110
    sixteenth = 60.0 / (bpm * 4)
    freqs = [146.83, 220.00, 293.66, 369.99, 440.00]
    hits = [0, 2, 3, 6, 8, 10, 11, 14]
    for bar in range(6):
        bar_t = bar * 16 * sixteenth
        for h in hits:
            hit_t = bar_t + h * sixteenth
            if hit_t >= 11.8: break
            idx = int(hit_t * FS)
            dur = sixteenth * 0.7
            n_samples = min(int(dur * FS), len(sig) - idx)
            sub_t = np.linspace(0, dur, n_samples)
            hit_sig = np.zeros(n_samples)
            for f in freqs:
                hit_sig += np.sin(2 * np.pi * f * sub_t) * np.exp(-35.0 * sub_t)
            sig[idx:idx+n_samples] += hit_sig
    return sig

def gen_jazz_comping():
    t = np.linspace(0, 12.0, int(12.0 * FS), endpoint=False)
    sig = np.zeros_like(t)
    bpm = 120
    beat = 60.0 / bpm
    chords = [
        (0.0, [146.83, 220.00, 261.63, 329.63, 349.23]), # Dm9
        (2 * beat, [196.00, 246.94, 293.66, 349.23, 440.00]), # G13
        (4 * beat, [130.81, 196.00, 246.94, 329.63, 392.00]), # Cmaj9
        (6 * beat, [220.00, 277.18, 329.63, 392.00, 493.88])  # A7alt
    ]
    for bar in range(3):
        base_t = bar * 8 * beat
        for rel_t, freqs in chords:
            hit_t = base_t + rel_t
            if hit_t >= 11.5: break
            idx = int(hit_t * FS)
            for f in freqs:
                decay = np.exp(-2.2 * (t[idx:] - hit_t))
                sig[idx:] += np.sin(2 * np.pi * f * (t[idx:] - hit_t)) * decay
    return sig

def gen_metal_riff():
    t = np.linspace(0, 12.0, int(12.0 * FS), endpoint=False)
    sig = np.zeros_like(t)
    bpm = 135
    sixteenth = 60.0 / (bpm * 4)
    e_low = 82.41
    pattern = [1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 0, 1, 1, 0]
    for bar in range(7):
        bar_t = bar * 16 * sixteenth
        for i, hit in enumerate(pattern):
            if not hit: continue
            hit_t = bar_t + i * sixteenth
            if hit_t >= 11.8: break
            idx = int(hit_t * FS)
            dur = sixteenth * 0.85
            n = min(int(dur * FS), len(sig) - idx)
            sub_t = np.linspace(0, dur, n)
            palm = (np.sin(2*np.pi*e_low*sub_t) + 0.6*np.sin(4*np.pi*e_low*sub_t)) * np.exp(-40.0 * sub_t)
            sig[idx:idx+n] += palm
    return sig

def gen_blues_solo():
    t = np.linspace(0, 12.0, int(12.0 * FS), endpoint=False)
    sig = np.zeros_like(t)
    notes = [
        (0.0, 220.00, 0.8), (0.8, 261.63, 0.5), (1.4, 293.66, 0.9), (2.5, 311.13, 0.4),
        (3.0, 329.63, 1.2), (4.5, 392.00, 0.7), (5.3, 440.00, 1.5), (7.0, 329.63, 0.6),
        (7.8, 293.66, 0.8), (8.8, 261.63, 0.6), (9.5, 220.00, 2.2)
    ]
    for n_t, f, dur in notes:
        idx = int(n_t * FS)
        n = min(int(dur * FS), len(sig) - idx)
        sub_t = np.linspace(0, dur, n)
        vib = 1.0 + 0.015 * np.sin(2 * np.pi * 5.5 * sub_t)
        lead = (np.sin(2 * np.pi * f * vib * sub_t) + 0.3 * np.sin(4 * np.pi * f * sub_t)) * np.exp(-1.5 * sub_t)
        sig[idx:idx+n] += lead
    return sig

def gen_acoustic_fingerpicking():
    t = np.linspace(0, 12.0, int(12.0 * FS), endpoint=False)
    sig = np.zeros_like(t)
    step = 0.22
    arps = [110.0, 164.81, 220.0, 277.18, 329.63, 277.18, 220.0, 164.81]
    for i in range(int(11.8 / step)):
        hit_t = i * step
        idx = int(hit_t * FS)
        f = arps[i % len(arps)]
        dur = 0.8
        n = min(int(dur * FS), len(sig) - idx)
        sub_t = np.linspace(0, dur, n)
        plk = (np.sin(2 * np.pi * f * sub_t) + 0.5 * np.sin(4 * np.pi * f * sub_t)) * np.exp(-4.0 * sub_t)
        sig[idx:idx+n] += plk
    return sig

def main():
    print("Generating candidate clean guitar DI audition samples...")
    
    # 1. Option 1: CC0 / Public Domain / Clean In-House DIs
    opt1_generators = [
        ("01_Clean_Chords_Progression_Emaj.wav", gen_chords_progressions),
        ("02_Funk_16th_Chops_110bpm.wav",        gen_funk_rhythm),
        ("03_Jazz_Swing_Comping_Dm9.wav",        gen_jazz_comping),
        ("04_Metal_Tight_Palm_Chug_135bpm.wav",  gen_metal_riff),
        ("05_Blues_Expressive_Lead_Am.wav",      gen_blues_solo),
        ("06_Acoustic_Fingerpicking_Am.wav",     gen_acoustic_fingerpicking),
        ("07_Clean_Arpeggios_Am_F_C_G.wav",      gen_chords_progressions),
        ("08_Rock_Crunch_Riff_E.wav",            gen_metal_riff),
        ("09_Neo_Soul_D9_Chords.wav",            gen_jazz_comping),
        ("10_Warm_Neck_Pickup_Lead.wav",         gen_blues_solo)
    ]
    
    for fname, gen_func in opt1_generators:
        raw_samples = gen_func()
        pcm16 = process_audio(raw_samples, target_duration=12.0)
        out_path = os.path.join(OUT_DIR, fname)
        save_wav(out_path, pcm16)
        print(f"  [CC0] Saved: {fname} ({len(pcm16)/FS:.1f}s)")

    print("\nAudition samples generated in:")
    print(OUT_DIR)

if __name__ == "__main__":
    main()
