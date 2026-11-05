#!/usr/bin/env python3
"""
custom-fw/tools/import_factory_presets.py - factory presets 1..40

Generates custom-fw/src/sys/factory_presets.inc (included by src/sys/preset.c).
The 40 baseline presets for the Pocket Amp.

  python custom-fw/tools/import_factory_presets.py           write the table
  python custom-fw/tools/import_factory_presets.py --check   exit 1 if it differs
  python custom-fw/tools/import_factory_presets.py --dump    print the decoded records

The record (92 bytes, 80 per bank + "PATCHEND"; 41..80 are blank templates).
Field format:

  0x03..0x07  module order, a permutation of 1..5                       (ignored: the firmware
              keeps its own signal chain)
  0x08..0x0D  on/off: FX, AMP, MOD, DLY, REV, CAB                        high
  0x0E..0x13  type:   FX (0 gate, 1 boost, 2 comp), AMP 0..19, MOD 0..4, DLY 0..4, REV 0..5,
              CAB 0..19                                                  high
  0x14..      16-bit knob values 0..100, six per module:
              FX  [0..5]   threshold, 2nd, 3rd, level                    medium
              AMP [6..11]  gain, LEVEL, bass, mid, treble                high (matched value for value
                           against baseline reference values; this line used to read
                           gain, bass, mid, treble, volume, which shifted the three tone knobs)
              MOD [12..17] rate, depth, mix, 4th                         medium
              DLY [18..23] time, feedback, mix, 4th                      medium
              REV [24..29] decay, mix, (3 more, not mapped)              medium
              CAB [30..35] level, low-cut, high-cut                      high
The records carry no names: each preset is named after its amp.

The mapping onto this firmware's knobs (same order as the stock modules unless noted):
  FX -> DYN (types 0..2 are the same types). The stock LEVEL sits at 50 on almost every
        preset: 50 is taken as unity, so DYN LEVEL = 2 x stock (capped at 100 %, our unity);
        a 0 (an unused knob on the gate-only records) is unity too.
  AMP -> AMP: gain, bass, mid, treble (stock slots 0, 2, 3, 4); PRES 50 %; the stock LEVEL
        (slot 1) is not copied - its law in the original is not known - the level of each
        preset is set by its preset volume, measured (see VOLS).
  CAB -> CAB: low-cut as is; high-cut inverted (stock 100 = open, ours 0 = 18 kHz);
        level 100 % (unity; the preset volume does the level).
  MOD / DLY -> MOD / DLY: the four knobs as they are.
  REV -> REVERB: DECAY and MIX; LOWCUT 10 %, TONE 50 % (the stock's other three knobs are not
        identified).
VOLS: preset_vol per preset, measured with tools/preset_level_calib.c on the T3K recordings
(t3k_mayer_clean, t3k_brit_rock, t3k_metalcore_chug; t3k_downtown_bass for bass presets) to
~-23 dBFS RMS. AMP MASTER is 50 % (unity) unless MASTER below says otherwise: it takes the rest
where the 30..100 preset-volume range cannot reach the target.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
import factory_bundles  # noqa: E402
BANK = factory_bundles.find("stock_am4_presets.bin")
AMPS = factory_bundles.find("stock_am4_assets.bin")
OUT = os.path.join(FW, "src", "sys", "factory_presets.inc")
N = 40
REC = 92

# slots (include/pa_fx.h)
DYN, DRV, AMP, CAB, EQ, MOD, DLY, REV = range(8)

# genre by the AM4 amp (measured drive characteristics)
GENRE = {**{a: "PA_GENRE_CLEAN" for a in (0, 1, 2)}, **{a: "PA_GENRE_CRUNCH" for a in (3, 4, 5, 6)},
         **{a: "PA_GENRE_METAL" for a in (7, 8, 9, 10, 12)}, **{a: "PA_GENRE_LEAD" for a in (11, 13, 14)},
         **{a: "PA_GENRE_BASS" for a in (15, 16, 17, 18, 19)}}

VOLS = [40, 32, 45, 39, 30, 53, 46, 71, 30, 52, 79, 62, 100, 30, 36, 47, 30, 39, 43, 40, 32, 42, 38, 42, 80, 30, 100, 86, 100, 100, 89, 100, 43, 66, 49, 52, 38, 99, 46, 100]
MASTER = {5: 41, 9: 41, 13: 59, 14: 46, 17: 40, 26: 45, 27: 58, 29: 60, 30: 62, 32: 85, 40: 57}


def amp_names():
    d = open(AMPS, "rb").read()
    out = []
    for i in range(20):
        n = d[i * 0x2000 + 4:i * 0x2000 + 24].split(b"\0")[0].decode("latin1")
        out.append(n.lstrip("0123456789"))
    return out


def decode(rec):
    on = list(rec[0x08:0x0E])
    ty = list(rec[0x0E:0x14])
    u = struct.unpack_from("<36H", rec, 0x14)
    return {"order": list(rec[3:8]), "on": on, "type": ty,
            "fx": u[0:6], "amp": u[6:12], "mod": u[12:18], "dly": u[18:24], "rev": u[24:30], "cab": u[30:36]}


def clamp(v):
    return max(0, min(100, int(v)))


def dyn_level(stock):
    """The stock FX level -> DYN LEVEL. 50 is the stock's unity (it sits there on almost every
    preset), so x2, capped at our unity (100 %). 0 means the knob is unused: the gate-only FX
    records (presets 25..28, 37) carry zeros in knobs 2..4 - that is unity, not silence."""
    return 100 if stock == 0 else clamp(2 * stock)


def build():
    data = open(BANK, "rb").read()
    if len(data) != 80 * REC + 10 or not data.endswith(b"PATCHEND\0\0"):
        raise SystemExit("stock_am4_presets.bin: unexpected size or trailer")
    names = amp_names()
    rows = []
    for i in range(N):
        r = decode(data[i * REC:(i + 1) * REC])
        on_fx, on_amp, on_mod, on_dly, on_rev, on_cab = r["on"]
        t_fx, t_amp, t_mod, t_dly, t_rev, t_cab = r["type"]
        if t_amp > 19 or t_cab > 19 or t_fx > 2 or t_mod > 4 or t_dly > 4 or t_rev > 5:
            raise SystemExit(f"record {i + 1}: a type out of range {r['type']}")
        mask = 0
        for flag, slot in ((on_fx, DYN), (on_amp, AMP), (on_mod, MOD), (on_dly, DLY), (on_rev, REV), (on_cab, CAB)):
            if flag:
                mask |= 1 << slot
        types = [0] * 8
        types[DYN], types[AMP], types[CAB], types[MOD], types[DLY], types[REV] = t_fx, t_amp, t_cab, t_mod, t_dly, t_rev
        pct = {}
        f = r["fx"]
        pct[DYN] = [clamp(f[0]), clamp(f[1]), clamp(f[2]), dyn_level(f[3]), 50, 50]
        a = r["amp"]
        pct[AMP] = [clamp(a[0]), clamp(a[2]), clamp(a[3]), clamp(a[4]), 50, MASTER.get(i + 1, 50)]   # a[1] is the stock LEVEL
        c = r["cab"]
        pct[CAB] = [100, clamp(c[1]), clamp(100 - c[2]), 50, 50, 50]
        m = r["mod"]
        pct[MOD] = [clamp(m[0]), clamp(m[1]), clamp(m[2]), clamp(m[3]), 50, 50]
        dl = r["dly"]
        pct[DLY] = [clamp(dl[0]), clamp(dl[1]), clamp(dl[2]), clamp(dl[3]), 50, 50]
        rv = r["rev"]
        pct[REV] = [clamp(rv[0]), 10, 50, clamp(rv[1]), 50, 50]
        name = f"{i + 1:02d} {names[t_amp]}"
        rows.append((name, GENRE[t_amp], VOLS[i], t_amp, t_cab, mask, types, pct, r))
    return data, rows


def render(rows, data):
    import hashlib
    sha = hashlib.sha256(data).hexdigest()
    out = [f"""/* custom-fw/src/sys/factory_presets.inc - factory presets 1..40
 *
 * GENERATED FILE - do not edit. Regenerate with
 *     python custom-fw/tools/import_factory_presets.py
 * Source: preset bank (sha256 {sha}).
 * 40 baseline presets configured across stock amp and cab models.
 *
 * set: the slots whose knobs the row sets (the others keep the neutral defaults).
 * pct: knob percentages, slot order DYN DRV AMP CAB EQ MOD DLY REV, 6 knobs each.
 */
typedef struct {{
    const char *name;
    uint8_t genre, vol, amp, cab, mask, set;
    uint8_t type[PA_SLOT_COUNT];
    uint8_t pct[PA_SLOT_COUNT][PA_PRESET_NPARAMS];
}} pa_factory_t;

static const pa_factory_t k_factory[PA_PRESET_FACTORY] = {{
"""]
    set_mask = (1 << DYN) | (1 << AMP) | (1 << CAB) | (1 << MOD) | (1 << DLY) | (1 << REV)
    for name, genre, vol, amp, cab, mask, types, pct, _r in rows:
        p = []
        for s in range(8):
            v = pct.get(s, [50] * 6)
            p.append("{" + ",".join(str(x) for x in v) + "}")
        out.append(f'    {{ "{name}", {genre}, {vol}, {amp}, {cab}, 0x{mask:02X}, 0x{set_mask:02X},\n'
                   f'      {{{",".join(str(t) for t in types)}}},\n'
                   f'      {{{",".join(p)}}} }},\n')
    out.append("};\n")
    return "".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--dump", action="store_true")
    a = ap.parse_args()
    data, rows = build()
    if a.dump:
        for name, genre, vol, amp, cab, mask, types, pct, r in rows:
            print(f"{name:20s} {genre:16s} vol {vol:3d} mask {mask:08b} types {types} on {r['on']} order {r['order']}")
        return
    text = render(rows, data)
    if a.check:
        if not os.path.exists(OUT) or open(OUT, encoding="utf-8").read() != text:
            print("src/sys/factory_presets.inc differs from tools/import_factory_presets.py")
            sys.exit(1)
        print("factory presets match baseline preset table")
        return
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {os.path.relpath(OUT, FW)} ({len(rows)} presets)")


if __name__ == "__main__":
    main()
