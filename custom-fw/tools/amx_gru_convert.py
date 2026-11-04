#!/usr/bin/env python3
"""
custom-fw/tools/amx_gru_convert.py - AM3 / AM4 amp records -> the firmware's GRU blob

Conversion of AM3 / AM4 amp model records to fixed-point GRU weights.


Weight layout:


  The weights start at record + 0x43C (AM3) or record + 0x83C (AM4), float32, a 16-unit GRU:
    +0x000  W_ir[16]   x -> reset gate          +0x0C0  W_hr[16][16]  h -> reset gate
    +0x040  W_iz[16]   x -> update gate         +0x4C0  W_hz[16][16]  h -> update gate
    +0x080  W_in[16]   x -> candidate           +0x8C0  W_hn[16][16]  h -> candidate
  AM3: +0xCC0 W_out[16] (the head). No biases at all.
  AM4: six bias vectors (PyTorch order) and then the head:
    +0xCC0 b_ir  +0xD00 b_iz  +0xD40 b_in  +0xD80 b_hr  +0xDC0 b_hz  +0xE00 b_hn  +0xE40 W_out
  Each value is quantised as (int)(v * 2097151.0f): a float32 product, truncated toward zero.

  The PyTorch GRU recurrence adds: reset gate += b_ir + b_hr; update gate += b_iz + b_hz;
  the candidate's recurrent term += b_hn (inside r * (...)); the candidate += b_in. That is a
  standard PyTorch GRU, and it maps directly onto pa_model.h's four bias slots:
    b_r = b_ir + b_hr,  b_z = b_iz + b_hz,  b_nh = b_hn,  b_ni = b_in,  b_out = 0
  (each half quantized separately before the sum).

  The LMT tag + 16 floats at +0x1EBC / +0x1EC0 represents the hidden state operating point
  after a 1-second calibration run; inference calculates steady-state dynamically.

The blob layout (pa_model.h, 3,852 bytes): W_r rows of 17 (16 h + x) at 0, W_z at 1088,
W_n rows of 16 at 2176, w_in at 3200, w_out at 3264, then the working buffers, then
b_r 3592, b_z 3656, b_ni 3720, b_nh 3784, b_out 3848.
"""
import struct

import numpy as np

HID = 16
BLOB_BYTES = 3852
AM3_WEIGHTS = 0x43C
AM4_WEIGHTS = 0x83C


_SCALE = np.float32(2097151.0)


def _q(v):
    # the DLL's (int)(v * 2097151.0f): a single-precision product, truncated toward zero
    return int(np.float32(v) * _SCALE)


def _floats(rec, off, n):
    return struct.unpack_from(f"<{n}f", rec, off)


def record_kind(rec):
    tag = rec[:4]
    if tag == b"AM3\0":
        return 3
    if tag == b"AM4\0":
        return 4
    raise ValueError(f"not an AM3/AM4 amp record (tag {tag!r})")


def convert(rec):
    """One 0x2000-byte amp record -> the 3,852-byte pa_nam_model_t blob."""
    kind = record_kind(rec)
    base = AM3_WEIGHTS if kind == 3 else AM4_WEIGHTS
    w_ir, w_iz, w_in = (_floats(rec, base + k * 0x40, HID) for k in range(3))
    w_hr = _floats(rec, base + 0x0C0, HID * HID)
    w_hz = _floats(rec, base + 0x4C0, HID * HID)
    w_hn = _floats(rec, base + 0x8C0, HID * HID)
    if kind == 3:
        w_out = _floats(rec, base + 0xCC0, HID)
        zeros = (0.0,) * HID
        b_ir = b_iz = b_in = b_hr = b_hz = b_hn = zeros
    else:
        b_ir, b_iz, b_in, b_hr, b_hz, b_hn = (_floats(rec, base + 0xCC0 + k * 0x40, HID) for k in range(6))
        w_out = _floats(rec, base + 0xE40, HID)

    blob = bytearray(BLOB_BYTES)

    def put(off, ints):
        struct.pack_into(f"<{len(ints)}i", blob, off, *ints)

    for i in range(HID):
        put(0 + i * 68, [_q(w_hr[i * HID + j]) for j in range(HID)] + [_q(w_ir[i])])
        put(1088 + i * 68, [_q(w_hz[i * HID + j]) for j in range(HID)] + [_q(w_iz[i])])
        put(2176 + i * 64, [_q(w_hn[i * HID + j]) for j in range(HID)])
    put(3200, [_q(v) for v in w_in])
    put(3264, [_q(v) for v in w_out])
    put(3592, [_q(a) + _q(b) for a, b in zip(b_ir, b_hr)])     # b_r
    put(3656, [_q(a) + _q(b) for a, b in zip(b_iz, b_hz)])     # b_z
    put(3720, [_q(v) for v in b_in])                            # b_ni
    put(3784, [_q(v) for v in b_hn])                            # b_nh
    put(3848, [0])                                              # b_out
    return bytes(blob)


def record_meta(rec):
    """name, the three (freq, min dB, max dB) voicing bands, and the DC float."""
    name = rec[4:24].split(b"\0")[0].decode("latin1", "ignore")
    eq = struct.unpack_from("<9f", rec, 24)
    dc = struct.unpack_from("<f", rec, 60)[0]
    return name, eq, dc
