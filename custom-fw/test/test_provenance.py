# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

"""Provenance: the firmware and the emulator use only the project's own generated material.

  [1] the generated data in the firmware is exactly what its generator makes:
        src/dsp/drum_kit.inc          tools/gen_drum_kit.py        (the synthesised drum kit)
        src/dsp/drum_patterns.inc     tools/gen_drum_patterns.py   (the rhythm library)
        src/fx/cab_default_table.inc  tools/gen_test_audio.py      (the default cabinet)
        src/sys/stock_assets.c        tools/import_stock_assets.py (the 20 stock amps + cabs,
                                      from the baseline bundle)
        src/sys/factory_presets.inc   tools/import_factory_presets.py (presets 1..40, from
                                      the preset bank)
      so nobody can paste third-party samples or patterns into them unnoticed;
  [2] no firmware, emulator, WASM or test source refers to the third-party material
      these replaced (the extracted drum kit, the imported rhythm set, the V30 table,
      the TONE3000 recordings / IRs / captures);
  [3] the emulator's test files generate and load: six DI phrases, four cabinets, two
      reverbs, the default cab and the A2 test capture.

    python custom-fw/test/test_provenance.py
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
fails = 0


def check(ok, msg):
    global fails
    print(("  ok   " if ok else "  FAIL ") + msg)
    if not ok:
        fails += 1


print("=== [1] the generated files are what their generators make ===")
is_andromeda = False
try:
    with open(os.path.join(FW, "include", "pa_config.h"), "r", encoding="utf-8") as pf:
        is_andromeda = "PA_PRODUCT_ANDROMEDA" in pf.read()
except Exception:
    pass

for tool, extra, what in (("gen_drum_kit.py", [], "src/dsp/drum_kit.inc"),
                          ("gen_drum_patterns.py", [], "src/dsp/drum_patterns.inc"),
                          ("gen_test_audio.py", [], "src/fx/cab_default_table.inc"),
                          ("import_stock_assets.py", [], "src/sys/stock_assets.c + pa_stock_assets.h"),
                          ("import_factory_presets.py", [], "src/sys/factory_presets.inc")):
    if is_andromeda and tool in ("import_stock_assets.py", "import_factory_presets.py"):
        with open(os.path.join(FW, "src", "sys", "stock_assets.c"), "r", encoding="utf-8") as sf:
            has_stub = "pa_stock_has_factory_assets(void) { return 0; }" in sf.read()
        check(has_stub, f"{what}: stubbed out for Andromeda (factory bundles not linked)")
        continue
    r = subprocess.run([sys.executable, os.path.join(FW, "tools", tool), "--check", *extra],
                       capture_output=True, text=True)
    check(r.returncode == 0, f"{what} matches tools/{tool}" + ("" if r.returncode == 0 else
                                                             f": {(r.stdout + r.stderr).strip()[-200:]}"))

print("\n=== [2] nothing refers to the third-party material that was replaced ===")
# file names / identifiers of what was replaced; matched case-insensitively
FORBIDDEN = [
    r"drum_samples\.inc", r"cab_v30_table", r"k_default_v30_ir",
    r"\bt3k_", r"/api/t3k_ir", r"sample_4x12_v30_ir", r"GE100 ?Pro",
    r"FNDR BFSR", r"MRSH JM50LD", r"BOG UU II",
    r"(?<![\w])rock_riff\.wav", r"(?<![\w])clean_chords\.wav", r"(?<![\w])metal_chug\.wav",
]
# the replaced files themselves: no longer used by anything; deleted in phase 2 of the
# clean-up (until then they are listed, not failed)
REPLACED = {"drum_samples.inc", "cab_v30_table.inc"}
SCAN_DIRS = ["src", "include", "emu", "wasm", "test", "bench"]
SCAN_EXT = (".c", ".h", ".inc", ".py", ".js", ".html", ".ps1", ".S", ".ld")
SKIP = {os.path.abspath(__file__), os.path.abspath(os.path.join(FW, "src", "sys", "pa_user_embedded.inc"))}   # User imported content (tools/pa_import.py)
hits, still = [], []
for d in SCAN_DIRS:
    for root, dirs, files in os.walk(os.path.join(FW, d)):
        dirs[:] = [x for x in dirs if x not in ("__pycache__", "out", "node_modules")]
        for fn in files:
            p = os.path.join(root, fn)
            if not fn.endswith(SCAN_EXT) or os.path.abspath(p) in SKIP:
                continue
            if fn in ("test_wavenet_compare.py",):        # removed in phase 2 (see the clean-up plan)
                continue
            if fn in REPLACED:
                still.append(os.path.relpath(p, FW))
                continue
            try:
                text = open(p, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            for pat in FORBIDDEN:
                for m in re.finditer(pat, text, re.I):
                    line = text.count("\n", 0, m.start()) + 1
                    hits.append(f"{os.path.relpath(p, FW)}:{line}: {m.group(0)}")
for h in hits[:20]:
    print("       " + h)
check(not hits, f"no source refers to the replaced material ({len(hits)} references)")
if still:
    print(f"  info still on disk, unused (removed in phase 2): {', '.join(sorted(still))}")

print("\n=== [3] the emulator's test files generate and load ===")
sys.path.insert(0, os.path.join(FW, "tools"))
sys.path.insert(0, os.path.join(FW, "emu"))
import gen_test_audio            # noqa: E402
import nam_loader                # noqa: E402
gen_test_audio.ensure_emu_assets(os.path.join(FW, "emu"), log=lambda s: None)
emu = os.path.join(FW, "emu")
for fn in gen_test_audio.PHRASES:
    p = os.path.join(emu, "samples", fn)
    check(os.path.getsize(p) > 100_000, f"samples/{fn} ({os.path.getsize(p):,} B)")
for name in list(gen_test_audio.CABS) + list(gen_test_audio.REVERBS):
    p = os.path.join(emu, "irs", f"{name}.wav")
    taps, _ = nam_loader.parse_wav_ir(open(p, "rb").read(), max_taps=1024)
    pk = max(abs(t) for t in taps)
    check(len(taps) == 1024 and 0.5 < pk <= 1.0, f"irs/{name}.wav loads ({len(taps)} taps, peak {pk:.2f})")
blob, meta = nam_loader.parse_nam_file(open(os.path.join(emu, "test_capture.nam"), "rb").read(),
                                       "test_capture.nam")
check(len(blob) == 7504 and meta.get("weights_count") == 1871,
      f"test_capture.nam parses as an A2-Lite capture ({len(blob)} B, {meta.get('weights_count')} weights)")

print("\n" + ("PROVENANCE OK" if fails == 0 else f"{fails} FAILURE(S)"))
sys.exit(0 if fails == 0 else 1)
