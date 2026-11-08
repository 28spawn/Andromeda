# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

import ctypes
import os
import sys
import tempfile
import math

os.environ["PA_FLASH_FILE"] = os.path.join(tempfile.mkdtemp(prefix="pa_overload_"), "flash.bin")

HERE = os.path.dirname(os.path.abspath(__file__))
DLL_PATH = os.path.join(HERE, "..", "emu", "pocket_amp_emu.dll")

d = ctypes.CDLL(DLL_PATH)
d.emu_init()

d.emu_ui_pot.argtypes = [ctypes.c_uint16]
d.emu_ui_pot.restype = None
d.emu_set_master_volume.argtypes = [ctypes.c_int]
d.emu_set_master_volume.restype = None
d.emu_process_audio_f32.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
                                    ctypes.POINTER(ctypes.c_float), ctypes.c_int]

N = 64
inp = (ctypes.c_float * N)()
outL = (ctypes.c_float * N)()
outR = (ctypes.c_float * N)()

for i in range(N):
    inp[i] = 0.3 * math.sin(2.0 * math.pi * 440.0 * i / 44100.0)

class Telemetry(ctypes.Structure):
    _fields_ = [
        ("view", ctypes.c_uint32),
        ("preset_index", ctypes.c_uint32),
        ("preset_volume", ctypes.c_uint32),
        ("master_volume", ctypes.c_uint32),
        ("selected_slot", ctypes.c_uint32),
        ("selected_param", ctypes.c_uint32),
        ("slot_active_mask", ctypes.c_uint32),
        ("sram_used_bytes", ctypes.c_uint32),
        ("sram_peak_bytes", ctypes.c_uint32),
        ("cpu_total_permille", ctypes.c_uint32),
        ("cpu_stage_permille", ctypes.c_uint32 * 8),
        ("tuner_active", ctypes.c_uint32),
        ("tuner_locked", ctypes.c_uint32),
        ("tuner_note", ctypes.c_uint32),
        ("tuner_octave", ctypes.c_int32),
        ("tuner_cents", ctypes.c_int32),
        ("tuner_freq_tenths", ctypes.c_uint32),
        ("tuner_dots", ctypes.c_uint32),
        ("drum_playing", ctypes.c_uint32),
        ("drum_bpm", ctypes.c_uint32),
        ("drum_pattern", ctypes.c_uint32),
        ("in_peak", ctypes.c_float),
        ("out_peak", ctypes.c_float),
        ("bt_state", ctypes.c_uint32),
        ("cursor_slot", ctypes.c_uint32),
        ("editor_cursor", ctypes.c_uint32),
    ]

d.emu_get_telemetry.argtypes = [ctypes.POINTER(Telemetry)]
d.emu_get_telemetry.restype = None

d.emu_get_lcd_rgba.argtypes = [ctypes.POINTER(ctypes.c_uint8)]

def check_overload_popup():
    buf = (ctypes.c_uint8 * (240 * 240 * 4))()
    d.emu_get_lcd_rgba(buf)
    # Check for red border along y=92
    red_count = 0
    # RGBA format: (y * 240 + x) * 4
    for x in range(30, 200):
        idx = (92 * 240 + x) * 4
        r = buf[idx]
        g = buf[idx + 1]
        b = buf[idx + 2]
        if r > 200 and g < 100:
            red_count += 1
    return red_count > 100

def get_telem():
    t = Telemetry()
    d.emu_get_telemetry(ctypes.byref(t))
    return t

print("Initial state:")
t = get_telem()
print(f"View: {t.view}, Master: {t.master_volume}, CPU: {t.cpu_total_permille}")

# Run 100 audio blocks
for _ in range(100):
    d.emu_process_audio_f32(inp, outL, outR, N)

t = get_telem()
print(f"After 100 blocks: Popup: {check_overload_popup()}, CPU: {t.cpu_total_permille}")

# Test 1: Rapidly sweeping master pot 0 -> 4095
print("\n--- Test 1: Rapidly sweeping master pot 0 -> 4095 ---")
triggered = False
for sweep in range(5):
    for p in range(0, 4096, 40):
        d.emu_ui_pot(p)
        for _ in range(2):
            d.emu_process_audio_f32(inp, outL, outR, N)
        if check_overload_popup():
            print(f"OVERLOAD POPUP TRIGGERED at sweep {sweep}, pot={p}!")
            triggered = True
            break
    if triggered:
        break

t = get_telem()
print(f"Result Test 1: Popup: {check_overload_popup()}, Master: {t.master_volume}")

# Test 2: Rapidly calling emu_set_master_volume(0) -> 100
print("\n--- Test 2: Jump 0 -> 100 master volume ---")
for jump in range(10):
    d.emu_set_master_volume(0)
    for _ in range(5):
        d.emu_process_audio_f32(inp, outL, outR, N)
    d.emu_set_master_volume(100)
    for _ in range(5):
        d.emu_process_audio_f32(inp, outL, outR, N)
    if check_overload_popup():
        print(f"OVERLOAD POPUP TRIGGERED at jump {jump}!")
        break

t = get_telem()
print(f"Result Test 2: Popup: {check_overload_popup()}, Master: {t.master_volume}")
