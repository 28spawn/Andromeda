#!/usr/bin/env python3
"""
custom-fw/tools/import_stock_assets.py - the 20 stock amps and 20 stock cabs

Generates custom-fw/src/sys/stock_assets_embedded.inc for device and test builds.
The bundle is found by tools/factory_bundles.py.
The emulator DLL can instead read the bundle from simulated flash at 0x0BF000
(-DPA_STOCK_FLASH_LOADER, src/sys/stock_assets.c).

  python custom-fw/tools/import_stock_assets.py            write the table file
  python custom-fw/tools/import_stock_assets.py --check    exit 1 if it differs from it

Bundle layout:
  20 amp records x 0x2000 at 0x00000 ("AM3\\0" / "AM4\\0", name, 3 voicing bands, DC float,
  GRU weights), then 20 cab records x 0x1000 at 0x28000 ("CAB\\0", name at +4..+20, signed
  24-bit little-endian IR taps from +20 to the end of the record: 1,358 taps).

Amps: tools/amx_gru_convert.py, an independent implementation of the conversion. It reproduces the 20 shipped AM3 blobs bit-exactly; on AM4 the
firmware's GRU then matches reference GRU inference at >= 78 dB SNR (the same as AM3).

Cabs: the taps start at +20. (The AM3 importer started at +26 - still sample-aligned,
but it dropped the first two taps, and several AM4 IRs, e.g. 8FRMAN112 and 14Recto_112,
have their attack there.) The engine runs 1,024 taps; the 334 beyond carry -27..-40 dB of
the energy, so the last 64 kept taps get a raised-cosine fade instead of a hard cut.
Q23 -> Q18 is >> 5, as before.
"""
import argparse
import hashlib
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import amx_gru_convert as amx  # noqa: E402
import factory_bundles  # noqa: E402

DEFAULT_BUNDLE = "stock_am4_assets.bin"
N_AMP, N_CAB = 20, 20
AMP_REC, CAB_REC, CAB_BASE = 0x2000, 0x1000, 0x28000
CAB_TAP0 = 20
CAB_TAPS = 1024
CAB_FADE = 64

OUT = os.path.join(FW, "src", "sys", "stock_assets_embedded.inc")


def s24(b):
    v = b[0] | (b[1] << 8) | (b[2] << 16)
    return v - (1 << 24) if v & 0x800000 else v


def c_str(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def load(bundle_name):
    data = open(factory_bundles.find(bundle_name), "rb").read()
    if len(data) != 0x3C000:
        raise SystemExit(f"{bundle_name}: {len(data)} bytes, expected 245,760")
    amps = []
    for i in range(N_AMP):
        rec = data[i * AMP_REC:(i + 1) * AMP_REC]
        name, eq, dc = amx.record_meta(rec)
        amps.append((name, eq, dc, amx.convert(rec), amx.record_kind(rec)))
    cabs = []
    for i in range(N_CAB):
        rec = data[CAB_BASE + i * CAB_REC:CAB_BASE + (i + 1) * CAB_REC]
        if rec[:4] != b"CAB\0":
            raise SystemExit(f"cab {i}: bad tag {rec[:4]!r}")
        name = rec[4:20].split(b"\0")[0].decode("latin1", "ignore")
        taps = []
        for k in range(CAB_TAPS):
            q18 = s24(rec[CAB_TAP0 + 3 * k:CAB_TAP0 + 3 * k + 3]) >> 5
            j = k - (CAB_TAPS - CAB_FADE)
            if j >= 0:   # raised-cosine fade over the last 64 taps
                q18 = int(round(q18 * 0.5 * (1.0 + math.cos(math.pi * (j + 1) / CAB_FADE))))
            taps.append(q18)
        cabs.append((name, taps))
    return data, amps, cabs


def render(bundle_name, data, amps, cabs):
    sha = hashlib.sha256(data).hexdigest()
    kinds = sorted({k for *_, k in amps})
    gen = "AM" + "/".join(str(k) for k in kinds)
    out = [f"""/*
 * src/sys/stock_assets_embedded.inc - 20 stock amp models and 20 stock cabinet IRs ({gen})
 *
 * GENERATED FILE - do not edit. Regenerate with
 *     python custom-fw/tools/import_stock_assets.py
 * Source: {bundle_name} (sha256 {sha}).
 * Included by src/sys/stock_assets.c for the builds that embed the tables.
 * Amps: Q21 GRU blobs (pa_model.h). Cabs: {CAB_TAPS} taps in Q18.
 */
#define PA_STOCK_EMBEDDED_GENERATION {kinds[-1]}

"""]
    for i, (name, _eq, _dc, blob, _k) in enumerate(amps):
        out.append(f"/* Amp {i:02d}: {name} ({amx.BLOB_BYTES:,} bytes) */\n")
        out.append(f"static const uint8_t s_amp_{i:02d}_blob[{amx.BLOB_BYTES}] = {{\n")
        for r in range(0, amx.BLOB_BYTES, 16):
            out.append("    " + ", ".join(f"0x{b:02x}" for b in blob[r:r + 16]) + ",\n")
        out.append("};\n\n")
    for i, (name, taps) in enumerate(cabs):
        out.append(f"/* Cab {i:02d}: {name} ({CAB_TAPS:,} taps in Q18) */\n")
        out.append(f"static const int32_t s_cab_{i:02d}_taps[{CAB_TAPS}] = {{\n")
        for r in range(0, CAB_TAPS, 8):
            out.append("    " + ", ".join(str(t) for t in taps[r:r + 8]) + ",\n")
        out.append("};\n\n")
    out.append("/* initializers for g_pa_stock_amps / g_pa_stock_cabs (src/sys/stock_assets.c) */\n")
    out.append("#define PA_STOCK_EMBEDDED_AMPS { \\\n")
    for i, (name, eq, dc, _b, _k) in enumerate(amps):
        e = ", ".join(f"{v:.1f}f" for v in eq)
        out.append(f'    {{ "{c_str(name)}", {e}, {dc:.6f}f, s_amp_{i:02d}_blob }}, \\\n')
    out.append("}\n\n")
    out.append("#define PA_STOCK_EMBEDDED_CABS { \\\n")
    for i, (name, _t) in enumerate(cabs):
        out.append(f'    {{ "{c_str(name)}", s_cab_{i:02d}_taps }}, \\\n')
    out.append("}\n")
    return "".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bundle", default=DEFAULT_BUNDLE, help="bundle name (see tools/factory_bundles.py)")
    ap.add_argument("--check", action="store_true", help="compare instead of writing")
    a = ap.parse_args()
    data, amps, cabs = load(a.bundle)
    text = render(a.bundle, data, amps, cabs)
    if a.check:
        if not os.path.exists(OUT) or open(OUT, encoding="utf-8").read() != text:
            print(f"{os.path.relpath(OUT, FW)} differs from the generator (run tools/import_stock_assets.py)")
            sys.exit(1)
        print(f"stock assets match {a.bundle}")
        return
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    for i, (name, eq, dc, _b, k) in enumerate(amps):
        print(f"  amp {i:02d} AM{k} {name:14s} bands {eq[0]:.0f}/{eq[3]:.0f}/{eq[6]:.0f} Hz  dc {dc:+.4f}")
    for i, (name, taps) in enumerate(cabs):
        print(f"  cab {i:02d} {name:14s} peak Q18 {max(abs(t) for t in taps)}")
    print(f"wrote {os.path.relpath(OUT, FW)} from {a.bundle}")


if __name__ == "__main__":
    main()
