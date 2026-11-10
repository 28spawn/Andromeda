# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

"""The web editor's Presets tab (web/Andromeda_Web_Editor.html): its backup and restore code run in Node against the firmware itself (the emulator DLL, with a simulated flash).

The page reads the preset store over the diagnostic channel (7D 01, 16 bytes a call, the same frames the page sends), picks the live bank, writes the JSON file, and puts presets
back through the upload (target 0). Checked here:
  - the page reads the bank the firmware uses, and every record decodes with its own magic and CRC
  - the JSON file rebuilds every record byte for byte (so a restore is exact)
  - a restored preset is accepted by the firmware's own checks, lands in its slot, survives a restart, and leaves the other slots untouched
  - a record with a wrong CRC is refused by the firmware, and the page refuses files it cannot restore

    python custom-fw/test/test_editor_presets.py [path/to/andromeda_emu.dll]
"""
import ctypes
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
os.environ["PA_FLASH_FILE"] = os.path.join(tempfile.mkdtemp(prefix="pa_presets_"), "flash.bin")
_default = os.path.join(ROOT, "emu", "andromeda_emu.dll")
dll = sys.argv[1] if len(sys.argv) > 1 else (_default if os.path.exists(_default) else os.path.join(ROOT, "emu", "pocket_amp_emu.dll"))

page = open(os.path.join(ROOT, "web", "Andromeda_Web_Editor.html"), encoding="utf-8").read()
script = re.search(r"<script>(.*)</script>", page, re.S).group(1)
tmp = tempfile.mkdtemp(prefix="pa_presets_js_")
open(os.path.join(tmp, "page.js"), "w", encoding="utf-8").write(script)
open(os.path.join(tmp, "run.js"), "w", encoding="utf-8").write(
    "const c=require('./page.js');const ops=JSON.parse(require('fs').readFileSync(0,'utf8'));"
    "const out=ops.map(([f,...a])=>{try{return c[f](...a);}catch(e){return {thrown:e.message};}});"
    "console.log(JSON.stringify(out,(k,v)=>ArrayBuffer.isView(v)?Array.from(v):v));")
fails = 0


def check(ok, msg):
    global fails
    print(("  ok   " if ok else "  FAIL ") + msg)
    if not ok:
        fails += 1


def js(*ops):
    r = subprocess.run(["node", os.path.join(tmp, "run.js")], input=json.dumps(list(ops)), capture_output=True, text=True, encoding="utf-8", cwd=tmp)
    if r.returncode:
        print(r.stderr)
        raise SystemExit("node failed")
    return json.loads(r.stdout)


d = ctypes.CDLL(dll)
d.emu_feed_sysex_bytes.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32]
d.emu_next_sysex_reply.argtypes = [ctypes.c_char_p, ctypes.c_int]
d.emu_next_sysex_reply.restype = ctypes.c_int
d.emu_preset_is_empty.argtypes = [ctypes.c_int]
d.emu_preset_is_empty.restype = ctypes.c_int
F = ctypes.POINTER(ctypes.c_float)
d.emu_process_audio_f32.argtypes = [F, F, F, ctypes.c_int]
ib, ol, orr = (ctypes.c_float * 64)(), (ctypes.c_float * 64)(), (ctypes.c_float * 64)()


def run(blocks):
    for _ in range(blocks):
        d.emu_process_audio_f32(ib, ol, orr, 64)


def drain():
    while d.emu_next_sysex_reply(ctypes.create_string_buffer(700), 700) > 0:
        pass


def feed(fr):
    d.emu_feed_sysex_bytes((ctypes.c_uint8 * len(fr)).from_buffer_copy(bytes(fr)), len(fr))


def replies():
    out = []
    while True:
        buf = ctypes.create_string_buffer(700)
        n = d.emu_next_sysex_reply(buf, 700)
        if n <= 0:
            return out
        out.append(list(buf.raw[:n]))


def flash_read(addr, n):
    """The page's own diagnostic reads (diagReadFrame / diagParse), 16 bytes a call, as diagRead does on the unit."""
    spans = [(a, min(16, addr + n - a)) for a in range(addr, addr + n, 16)]
    frames = js(*[["diagReadFrame", a, k] for a, k in spans])
    got = []
    for fr in frames:
        drain()
        feed(fr)
        r = replies()
        if len(r) != 1:
            raise SystemExit(f"the diagnostic read was answered with {len(r)} replies")
        got.append(r[0])
    parsed = js(*[["diagParse", r] for r in got])
    out = []
    for (a, k), p in zip(spans, parsed):
        if not p or p.get("refused") or p["addr"] != a or len(p["data"]) != k:
            raise SystemExit(f"the diagnostic read at {a:#x} failed: {p}")
        out += p["data"]
    return out


C = js(["psBankOrder", [0] * 16, [0] * 16])  # warm-up: the core loads
COUNT, SIZE, HDR = 20, 164, 16
BANKS = {"A": 0x8D000, "B": 0x89000}


def read_store():
    hA, hB = flash_read(BANKS["A"], HDR), flash_read(BANKS["B"], HDR)
    order = js(["psBankOrder", hA, hB])[0]
    for name, hdr in order:
        payload = flash_read(BANKS[name] + HDR, COUNT * SIZE)
        r = js(["psSplitBank", hdr, payload])[0]
        if "error" not in r:
            return name, hdr, r["records"]
    raise SystemExit("no valid preset bank in the flash")


d.emu_init()
run(300)

print("=== the page reads the firmware's live preset bank over the diagnostic channel ===")
bank, hdr, recs = read_store()
check(len(recs) == COUNT and all(len(r) == SIZE for r in recs), f"bank {bank} (seq {hdr['seq']}): {len(recs)} records of {SIZE} bytes")
dec = js(*[["psDecode", r, i + 1] for i, r in enumerate(recs)])
check(all(o["valid"] for o in dec), "every record has the PMP2 magic and its own CRC")
empty_fw = [d.emu_preset_is_empty(i) == 1 for i in range(COUNT)]
check([o["empty"] for o in dec] == empty_fw, "the EMPTY flags the page reads are the firmware's (" + "".join("E" if e else "P" for e in empty_fw) + ")")
check(all(o["name"] for o in dec[:12]), "the factory slots P01..P12 have names: " + ", ".join(o["name"] for o in dec[:4]) + " ...")

print("=== the JSON file rebuilds every record byte for byte ===")
text = js(["psBackupJson", recs, {"firmware": "test", "saved": "2026-11-10T00:00:00Z"}])[0]
j = json.loads(text)
check(j["format"] == "andromeda-presets" and j["version"] == 1 and len(j["presets"]) == COUNT, "the file: format andromeda-presets, version 1, 20 presets")
check("valid" not in j["presets"][0] and j["presets"][0]["slot"] == 1, "a preset carries its slot and its fields (no page-only fields)")
back = js(["psParseJson", text])[0]
check("error" not in back and len(back["presets"]) == COUNT, "the page reads its own file back")
check(all(p["record"] == recs[p["slot"] - 1] for p in back["presets"]), "every rebuilt record equals the flash record byte for byte (magic, seq, fields, CRC)")

print("=== a restore through the upload (target 0): the firmware stores it ===")
edit = json.loads(text)
p2 = next(p for p in edit["presets"] if p["slot"] == 2)
p2["name"] = "RESTORED TWO"
p2["preset_vol"] = 73
p13 = dict(next(p for p in edit["presets"] if p["slot"] == 1))
p13.update({"slot": 13, "name": "FROM BACKUP", "empty": False, "flags": p13["flags"] & ~1})
edit["presets"] = [p for p in edit["presets"] if p["slot"] not in (13,)] + [p13]
parsed = js(["psParseJson", json.dumps(edit)])[0]
check("error" not in parsed, "the edited file parses")
want = {p["slot"]: p["record"] for p in parsed["presets"] if p["slot"] in (2, 13)}
check(d.emu_preset_is_empty(12) == 1, "slot 13 starts EMPTY")
for slot in (2, 13):
    for fr in js(["uploadFrames", want[slot], 0, slot - 1, 200])[0]:
        feed(fr)
        run(2)
    run(40)
drain()
bank2, hdr2, recs2 = read_store()
check(((hdr2["seq"] - hdr["seq"]) & 0xFFFFFFFF) == 2, f"two bank commits (seq {hdr['seq']} -> {hdr2['seq']}, now bank {bank2})")
same = js(*[["psSameContent", recs2[s - 1], want[s]] for s in (2, 13)])
check(same == [True, True], "slots 2 and 13 hold exactly what was sent (the seq and the CRC are the firmware's own)")
d2 = js(["psDecode", recs2[1], 2], ["psDecode", recs2[12], 13])
check(d2[0]["name"] == "RESTORED TWO" and d2[0]["preset_vol"] == 73 and d2[1]["name"] == "FROM BACKUP", "the names and the volume read back: " + d2[0]["name"] + ", " + d2[1]["name"])
check(d2[0]["valid"] and d2[1]["valid"], "both stored records carry a valid CRC")
check(d.emu_preset_is_empty(12) == 0, "slot 13 is no longer EMPTY on the firmware")
check(all(recs2[i] == recs[i] for i in range(COUNT) if i not in (1, 12)), "the other 18 slots are byte for byte as they were")

print("=== the restore survives a restart ===")
d.emu_init()
run(300)
_, _, recs3 = read_store()
check(recs3 == recs2, "after emu_init on the same flash, the bank reads back unchanged")

print("=== a full 24-character name and a Latin-1 one survive the file ===")
for nm in ("X" * 24, "CAFÉ NOIR"):
    e = json.loads(text)
    e["presets"][0]["name"] = nm
    r = js(["psParseJson", json.dumps(e)])[0]
    check("error" not in r and js(["psDecode", r["presets"][0]["record"], 1])[0]["name"] == nm, f"the name {nm!r} goes into the record and comes back out")

print("=== refusals ===")
bad = list(want[2])
bad[20] ^= 0x55  # a changed byte with the old CRC
for fr in js(["uploadFrames", bad, 0, 1, 200])[0]:
    feed(fr)
    run(2)
run(40)
drain()
_, _, recs4 = read_store()
check(recs4 == recs3, "a record whose CRC does not match is refused by the firmware: nothing changes")
errs = js(["psParseJson", "not json"], ["psParseJson", json.dumps({"format": "other", "presets": []})],
          ["psParseJson", json.dumps({"format": "andromeda-presets", "version": 2, "presets": []})])
check(all("error" in e for e in errs), "the page refuses a non-JSON file, another format and a newer version")
dup = json.loads(text)
dup["presets"].append(dict(dup["presets"][0]))
check("twice" in js(["psParseJson", json.dumps(dup)])[0].get("error", ""), "the page refuses a file with a slot twice")
for field, val, what in (("preset_vol", 101, "preset_vol"), ("name", "x" * 25, "name"), ("name", "€ uro", "name"), ("slot_order", [0] * 7, "slot_order"), ("slot", 21, "slot")):
    e = json.loads(text)
    e["presets"][0][field] = val
    r = js(["psParseJson", json.dumps(e)])[0]
    check(what in r.get("error", ""), f"the page refuses {field} = {val!r}: {r.get('error', 'accepted')}")

print(f"\n{'ALL PASSED' if not fails else str(fails) + ' FAILURE(S)'}")
sys.exit(1 if fails else 0)
