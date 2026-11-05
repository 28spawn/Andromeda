"""Stock amps and cabs conversion verification.

  [1] the converter (tools/amx_gru_convert.py) reproduces the 20 AM3 blobs the firmware
      baseline AM3 blobs bit-exactly
  [2] every AM4 model carries its biases (b_r, b_z, b_ni, b_nh: the six AM4 bias vectors
      folded into the four slots), and none is left out
  [3] every AM4 model runs in the firmware's GRU (src/nam/nam_engine.c): a guitar-level
      sine comes out finite, not silent, and the hidden state stays in range
  [4] every stock cab IR starts at its attack (taps read from +20, not +26) and is faded
      at its end (no hard cut at 1,024 taps)
  [5] the flash loader (src/sys/stock_assets.c, -DPA_STOCK_FLASH_LOADER: the emulator's
      source of the stock amps and cabs) gives exactly the embedded tables' models when the
      AM4 bundle is at 0x0BF000, and the verified conversion for an AM3 bundle too (its
      output weights at +0xCC0, not AM4's +0xE40)


    python custom-fw/test/test_stock_am4.py
"""
import ctypes
import hashlib
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(FW, "tools"))
import amx_gru_convert as C  # noqa: E402
import factory_bundles  # noqa: E402

AM3 = factory_bundles.find("pocket-amp_am3_ampCab.bin")
AM4 = factory_bundles.find("stock_am4_assets.bin")
AM3_SHIPPED_SHA = "3745c9028e4d5225b44a99ddd310c44a3cefcb46eeb27c7a3fba857c503114d7"
fails = 0


def check(ok, msg):
    global fails
    print(("  ok   " if ok else "  FAIL ") + msg)
    if not ok:
        fails += 1


am3 = open(AM3, "rb").read()
am4 = open(AM4, "rb").read()
blobs4 = [C.convert(am4[i * 0x2000:(i + 1) * 0x2000]) for i in range(20)]
names4 = [C.record_meta(am4[i * 0x2000:(i + 1) * 0x2000])[0] for i in range(20)]

print("=== [1] the converter against the shipped AM3 blobs ===")
h = hashlib.sha256(b"".join(C.convert(am3[i * 0x2000:(i + 1) * 0x2000]) for i in range(20))).hexdigest()
check(h == AM3_SHIPPED_SHA, f"20 AM3 blobs bit-exact (sha256 {h[:16]}...)")

print("=== [2] the AM4 biases are there ===")
empty = []
for n, b in zip(names4, blobs4):
    v = np.frombuffer(b[3592:3848], np.int32)
    if np.count_nonzero(v[0:16]) < 8 or np.count_nonzero(v[16:32]) < 8 or np.count_nonzero(v[32:48]) < 8 \
            or np.count_nonzero(v[48:64]) < 8:
        empty.append(n)
check(not empty, f"b_r, b_z, b_ni, b_nh filled on all 20 models ({', '.join(empty) or 'none empty'})")

print("=== [3] every AM4 model runs in the firmware's GRU ===")
tmp = tempfile.mkdtemp(prefix="pa_am4_")
harness = os.path.join(tmp, "gru_dump.c")
open(harness, "w").write(r'''
#include <stdio.h>
#include <string.h>
#include "pa_model.h"
void pa_hw_accel_init(void); void pa_activation_init(void);
int main(int argc, char **argv) {
    static pa_nam_model_t m;
    FILE *fb = fopen(argv[1], "rb"); if (fread(&m, 1, PA_NAM_BLOB_BYTES, fb) != PA_NAM_BLOB_BYTES) return 2; fclose(fb);
    pa_hw_accel_init(); pa_activation_init(); pa_nam_reset(&m);
    m.scale_q21 = PA_Q21_ONE; m.active = 1;
    FILE *fi = fopen(argv[2], "rb"), *fo = fopen(argv[3], "wb"); int32_t x;
    while (fread(&x, 4, 1, fi) == 1) { int32_t y = nam_step_q21(&m, x); fwrite(&y, 4, 1, fo); fwrite(m.h_state, 4, 16, fo); }
    fclose(fi); fclose(fo); return 0;
}
''')
exe = os.path.join(tmp, "gru_dump.exe")
cc = subprocess.run(["clang", "-O2", "-w", "-I" + os.path.join(FW, "include"), "-DPA_HOST_SIM=1", harness,
                     os.path.join(FW, "src", "nam", "nam_engine.c"), os.path.join(FW, "src", "dsp", "hw_accel.c"),
                     os.path.join(FW, "src", "dsp", "activation.c"), "-lm", "-o", exe], capture_output=True, text=True)
check(cc.returncode == 0, "the GRU harness builds" + ("" if cc.returncode == 0 else ": " + cc.stderr[-200:]))
N = 22050
x = (0.3 * np.sin(2 * np.pi * 110 * np.arange(N) / 44100) * 2097151).astype(np.int32)
xf = os.path.join(tmp, "x.bin"); x.tofile(xf)
bad = []
for n, b in zip(names4, blobs4):
    bf, of = os.path.join(tmp, "b.bin"), os.path.join(tmp, "o.bin")
    open(bf, "wb").write(b)
    if subprocess.run([exe, bf, xf, of]).returncode != 0:
        bad.append(n + " (crashed)"); continue
    o = np.fromfile(of, np.int32).reshape(N, 17)
    y, hs = o[:, 0] / 2 ** 21, o[:, 1:] / 2 ** 21
    rms = np.sqrt(np.mean(y[N // 2:] ** 2))
    if not np.all(np.isfinite(y)) or rms < 1e-4 or np.abs(hs).max() > 1.01:
        bad.append(f"{n} (rms {rms:.2g}, |h| {np.abs(hs).max():.2f})")
check(not bad, f"20 models: finite, audible, |h| <= 1 ({', '.join(bad) or 'all fine'})")

print("=== [4] the stock cab IRs ===")
inc_path = os.path.join(FW, "src", "sys", "stock_assets_embedded.inc")
tab_cabs = []
if os.path.exists(inc_path):
    src = open(inc_path, encoding="utf-8").read()
    for i in range(20):
        m = re.search(r"s_cab_%02d_taps\[1024\] = \{(.*?)\};" % i, src, re.S)
        tab_cabs.append(np.array([int(v) for v in m.group(1).replace("\n", " ").split(",") if v.strip()], np.int64))
else:
    import import_stock_assets as ISA
    _, _, isa_cabs = ISA.load("stock_am4_assets.bin")
    tab_cabs = [np.array(c[1], dtype=np.int64) for c in isa_cabs]

cut, late = [], []
for i in range(20):
    t = tab_cabs[i].astype(float)
    raw = am4[0x28000 + i * 0x1000 + 20:0x28000 + i * 0x1000 + 26]
    first = [int.from_bytes(raw[k:k + 3], "little", signed=True) >> 5 for k in (0, 3)]
    if list(t[:2].astype(int)) != first:
        late.append(str(i + 1))
    if abs(t[-1]) > 1e-4 * np.abs(t).max() + 1:
        cut.append(str(i + 1))
check(not late, f"all 20 start at tap 0 of the record (+20) ({', '.join(late) or 'all'})")
check(not cut, f"all 20 fade to zero at tap 1,024 ({', '.join(cut) or 'all'})")


print("=== [5] the flash loader against the tables ===")
loader_c = os.path.join(tmp, "loader_dump.c")
open(loader_c, "w").write(r"""
/* the flash loader, on a simulated flash holding the bundle at 0x0BF000: dumps every amp blob
 * and cab. spi_nor.c is included so the test can fill its flash buffer directly (the run's
 * folder has no flash image, so nothing on disk is touched). */
#include <stdio.h>
#include <stdlib.h>
#include "../src/board/spi_nor.c"
#include "pa_stock_assets.h"
int plat_battery_is_brownout(void) { return 0; }
int main(int argc, char **argv) {
    pa_spi_nor_init();
    FILE *b = fopen(argv[1], "rb");
    if (!b || fread(&g_sim_flash[PA_FLASH_FACTORY_CAB_ADDR], 1, PA_FLASH_FACTORY_CAB_SIZE, b) != PA_FLASH_FACTORY_CAB_SIZE) return 2;
    fclose(b);
    pa_stock_assets_init();
    if (!pa_stock_has_factory_assets()) return 3;
    FILE *o = fopen(argv[2], "wb");
    static pa_nam_model_t m; static int32_t taps[PA_STOCK_CAB_TAPS]; uint32_t n;
    for (int i = 0; i < 20; i++) { pa_stock_load_amp((uint8_t)i, &m, 0); fwrite(&m, 1, PA_NAM_BLOB_BYTES, o); }
    for (int i = 0; i < 20; i++) { pa_stock_load_cab((uint8_t)i, taps, &n); fwrite(taps, 4, PA_STOCK_CAB_TAPS, o); }
    fclose(o);
    return 0;
}
""")
# a copy of the harness next to the sources' include path ("../src/board/spi_nor.c" from test/)
harness5 = os.path.join(HERE, "_loader_dump_tmp.c")
open(harness5, "w").write(open(loader_c).read())
exe5 = os.path.join(tmp, "loader_dump.exe")
cc = subprocess.run(["clang", "-O2", "-w", "-I" + os.path.join(FW, "include"), "-DPA_HOST_SIM=1",
                     "-DPA_STOCK_FLASH_LOADER=1", harness5, os.path.join(FW, "src", "sys", "stock_assets.c"),
                     os.path.join(FW, "src", "nam", "nam_engine.c"), os.path.join(FW, "src", "dsp", "hw_accel.c"),
                     os.path.join(FW, "src", "dsp", "activation.c"), "-lm", "-o", exe5], capture_output=True, text=True)
os.remove(harness5)
check(cc.returncode == 0, "the flash-loader harness builds" + ("" if cc.returncode == 0 else ": " + cc.stderr[-300:]))


def loader_dump(bundle):
    out = os.path.join(tmp, "dump.bin")
    r = subprocess.run([exe5, bundle, out], cwd=tmp)
    if r.returncode != 0:
        return None
    d = open(out, "rb").read()
    amps = [d[i * 3852:(i + 1) * 3852] for i in range(20)]
    cabs = [np.frombuffer(d[20 * 3852 + i * 4096:20 * 3852 + (i + 1) * 4096], np.int32) for i in range(20)]
    return amps, cabs


got4 = loader_dump(AM4)
if got4 is None:
    check(False, "the loader read the AM4 bundle from the simulated flash")
else:
    amp_ok = sum(got4[0][i] == blobs4[i] for i in range(20))
    cab_ok = sum(np.array_equal(got4[1][i].astype(np.int64), tab_cabs[i]) for i in range(20))
    check(amp_ok == 20, f"AM4 at 0x0BF000: the loader's 20 amps are reference ({amp_ok}/20 bit-exact)")
    check(cab_ok == 20, f"AM4 at 0x0BF000: the loader's 20 cabs are reference ({cab_ok}/20 bit-exact)")
got3 = loader_dump(AM3)
if got3 is None:
    check(False, "the loader read an AM3 bundle from the simulated flash")
else:
    ok3 = sum(got3[0][i] == C.convert(am3[i * 0x2000:(i + 1) * 0x2000]) for i in range(20))
    check(ok3 == 20, f"AM3 at 0x0BF000: the loader's 20 amps are the verified conversion ({ok3}/20 bit-exact)")

print("\n" + ("STOCK AM4 OK" if fails == 0 else f"{fails} FAILURE(S)"))
sys.exit(1 if fails else 0)
