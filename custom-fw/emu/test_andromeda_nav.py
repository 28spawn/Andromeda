# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

"""Andromeda navigation: slots, the signal chain page, HOME / HOME held, the chain edits, the block pages (VOL walks the blocks that are on, PARA hovers and clicks to edit) and the tape keys,
driven through the firmware's own input path in the emulator DLL.
Run after emu/build_dll.ps1 -Product andromeda.  python custom-fw/emu/test_andromeda_nav.py [snapshot dir]"""
import ctypes, sys
from pathlib import Path

EMU = Path(__file__).resolve().parent
dll_p = EMU / "andromeda_emu.dll"
if not dll_p.exists(): dll_p = EMU / "pocket_amp_emu.dll"
dll = ctypes.CDLL(str(dll_p))
BTN_HOME, BTN_VOLM, BTN_VOLP, BTN_LEFT, BTN_RIGHT, BTN_PARA = 1, 2, 4, 8, 16, 32
VIEW_PLAY, VIEW_SETTINGS, VIEW_SAVE, VIEW_CHAIN = 0, 8, 5, 17

class Tel(ctypes.Structure):
    _fields_ = [("view", ctypes.c_uint32), ("preset_index", ctypes.c_uint32), ("preset_volume", ctypes.c_uint32), ("master_volume", ctypes.c_uint32),
                ("selected_slot", ctypes.c_uint32), ("selected_param", ctypes.c_uint32), ("slot_active_mask", ctypes.c_uint32), ("sram_used_bytes", ctypes.c_uint32),
                ("sram_peak_bytes", ctypes.c_uint32), ("cpu_total_permille", ctypes.c_uint32), ("cpu_stage_permille", ctypes.c_uint32 * 8),
                ("tuner_active", ctypes.c_uint32), ("tuner_locked", ctypes.c_uint32), ("tuner_note", ctypes.c_uint32), ("tuner_octave", ctypes.c_int32),
                ("tuner_cents", ctypes.c_int32), ("tuner_freq_tenths", ctypes.c_uint32), ("tuner_dots", ctypes.c_uint32), ("drum_playing", ctypes.c_uint32),
                ("drum_bpm", ctypes.c_uint32), ("drum_pattern", ctypes.c_uint32), ("in_peak", ctypes.c_float), ("out_peak", ctypes.c_float),
                ("bt_state", ctypes.c_uint32), ("cursor_slot", ctypes.c_uint32), ("editor_cursor", ctypes.c_uint32)]

dll.emu_ui_tap.argtypes = [ctypes.c_uint32, ctypes.c_uint32]; dll.emu_ui_enc.argtypes = [ctypes.c_int32]
dll.emu_get_telemetry.argtypes = [ctypes.POINTER(Tel)]
dll.emu_get_lcd_rgba.argtypes = [ctypes.POINTER(ctypes.c_uint8)]
dll.emu_process_audio_f32.argtypes = [ctypes.POINTER(ctypes.c_float)] * 3 + [ctypes.c_int]
dll.emu_get_slot_param.argtypes = [ctypes.c_int, ctypes.c_int]; dll.emu_get_slot_type.argtypes = [ctypes.c_int]
dll.emu_get_menu_cursors.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
LAYER_SLOT, EFFECT_SLOT, SPACE_SLOT = 5, 6, 7
N = 64; _a = (ctypes.c_float * N)(); _b = (ctypes.c_float * N)(); _c = (ctypes.c_float * N)()
def tick(n=1):
    for _ in range(n * 11): dll.emu_process_audio_f32(_a, _b, _c, N)
def tap(m): dll.emu_ui_tap(m, 0); tick(25)
def hold(m, ms): dll.emu_ui_tap(m, ms); tick(int(ms / 15.96) + 25)
def enc(d): dll.emu_ui_enc(d); tick(6)
def chain_cursor():
    a = (ctypes.c_uint32 * 3)(); dll.emu_get_menu_cursors(a); return a[1]
def ui_state(): return dll.emu_andromeda_ui_state()          # bit 0: a block's page (opened from the chain page), bit 1: editing the hovered value
def tel():
    t = Tel(); dll.emu_get_telemetry(ctypes.byref(t)); return t
shots = Path(sys.argv[1]) if len(sys.argv) > 1 else None
def snap(name):
    if not shots: return
    from PIL import Image
    shots.mkdir(exist_ok=True)
    buf = (ctypes.c_uint8 * (240 * 240 * 4))(); dll.emu_get_lcd_rgba(buf)
    Image.frombytes("RGBA", (240, 240), bytes(buf)).convert("RGB").save(shots / (name + ".png"))

fails = 0
def check(ok, msg):
    global fails
    print(("  [PASS] " if ok else "  [FAIL] ") + msg)
    if not ok: fails += 1

dll.emu_init(); tick(10)
t = tel(); check(t.view == VIEW_PLAY and t.preset_index == 0, "boots on the play screen, slot 1")

print("[1] HOME: play <-> chain; HOME held 1 s: settings")
tap(BTN_HOME); check(tel().view == VIEW_CHAIN, "HOME on the play screen opens the signal chain")
snap("chain_p01")
tap(BTN_HOME); check(tel().view == VIEW_PLAY and ui_state() == 1, "HOME on the chain opens the page of the block under the cursor (a block's page, hovering)")
hold(BTN_HOME, 1100); check(tel().view == VIEW_SETTINGS, "HOME held 1 s on the play screen opens Settings")
tap(BTN_HOME); check(tel().view == VIEW_PLAY and ui_state() == 0, "HOME in Settings returns to the plain play screen")
tap(BTN_HOME); hold(BTN_HOME, 1100); check(tel().view == VIEW_SETTINGS, "HOME held 1 s on the chain page opens Settings")
tap(BTN_HOME); check(tel().view == VIEW_PLAY, "and HOME comes back to the play screen")

def plain():
    """the plain play screen (VOL- / VOL+ change the preset): HOME held 1 s opens Settings from the play screen or the chain page, HOME comes back"""
    hold(BTN_HOME, 1100); tap(BTN_HOME)
    assert tel().view == VIEW_PLAY and ui_state() == 0

print("[2] slots: 20, VOL- / VOL+ wrap on the plain play screen; held 0.8 s they change the preset on the chain page and on a block's page")
plain()
for _ in range(11): tap(BTN_VOLP)
check(tel().preset_index == 11, "VOL+ x11: slot 12")
tap(BTN_VOLP); check(tel().preset_index == 12, "slot 13, the first user slot")
for _ in range(7): tap(BTN_VOLP)
check(tel().preset_index == 19, "slot 20 is the last")
tap(BTN_VOLP); check(tel().preset_index == 0, "VOL+ wraps to slot 1")
tap(BTN_VOLM); check(tel().preset_index == 19, "VOL- wraps to slot 20")
hold(BTN_VOLM, 900); check(tel().preset_index == 18 and tel().view == VIEW_PLAY, "VOL- held 0.8 s on the play screen: slot 19")
tap(BTN_HOME); hold(BTN_VOLM, 900); check(tel().view == VIEW_CHAIN and tel().preset_index == 17, "VOL- held on the chain page: slot 18")
t0 = dll.emu_get_slot_type(EFFECT_SLOT); i0 = tel().preset_index
tap(BTN_VOLP); check(dll.emu_get_slot_type(EFFECT_SLOT) == (t0 + 1) % 24 and tel().preset_index == i0, "VOL+ on the chain page steps the effect and its variant, not the preset (type %d -> %d)" % (t0, dll.emu_get_slot_type(EFFECT_SLOT)))
tap(BTN_VOLM); check(dll.emu_get_slot_type(EFFECT_SLOT) == t0, "and VOL- steps it back")
snap("chain_p19_empty")
c0 = chain_cursor(); tap(BTN_RIGHT); check(chain_cursor() == (c0 + 1) % 4 and tel().view == VIEW_CHAIN, "> moves the chain cursor to the next block")
tap(BTN_LEFT); check(chain_cursor() == c0, "< moves it back")

print("[3] chain edits: effect type, layer, space, on / off")
plain()
while tel().preset_index != 0: tap(BTN_VOLP)
tap(BTN_HOME)                                   # the chain page of slot 1, cursor on EFFECT (the play screen was on its first page)
check(chain_cursor() == 0, "the cursor starts on EFFECT")
t0 = dll.emu_get_slot_type(EFFECT_SLOT)
for _ in range(4): tap(BTN_VOLP)
check(dll.emu_get_slot_type(EFFECT_SLOT) == (t0 + 4) % 24, "VOL+ x4 steps the effect type four times (%d -> %d)" % (t0, dll.emu_get_slot_type(EFFECT_SLOT)))
snap("chain_effect_stepped")
tap(BTN_PARA); snap("chain_effect_toggled")
tap(BTN_RIGHT); check(chain_cursor() == 1, "> selects LAYER")
m0 = dll.emu_get_slot_param(LAYER_SLOT, 0); tap(BTN_VOLP); snap("chain_layer_material")
check(dll.emu_get_slot_param(LAYER_SLOT, 0) != m0, "VOL+ on LAYER changes the material (%d %% -> %d %%)" % (m0, dll.emu_get_slot_param(LAYER_SLOT, 0)))
tap(BTN_PARA); snap("chain_layer_toggled")
tap(BTN_RIGHT); a0 = dll.emu_get_slot_param(SPACE_SLOT, 3); tap(BTN_VOLP); tap(BTN_VOLP); snap("chain_space")
check(dll.emu_get_slot_param(SPACE_SLOT, 3) != a0, "VOL+ on SPACE changes the amount (%d %% -> %d %%)" % (a0, dll.emu_get_slot_param(SPACE_SLOT, 3)))
check(tel().view == VIEW_CHAIN, "the chain page stays up through the edits")
hold(BTN_PARA, 1300); check(tel().view == VIEW_SAVE, "PARA held opens the save dialog from the chain page")
snap("chain_save_dialog")
tap(BTN_HOME)

print("[4] the new effects and the shimmer block")
tap(BTN_HOME) if tel().view != VIEW_PLAY else None
plain()
for _ in range(20): tap(BTN_VOLP)                      # back round to a known slot
while tel().preset_index != 0: tap(BTN_VOLP)
snap("slot01_glass")
for i in range(6): tap(BTN_VOLP)
snap("slot07_choir")
check(tel().preset_index == 6, "slot 7 is the first CHOIR chain")
for i in range(2): tap(BTN_VOLP)
snap("slot09_fracture")
for i in range(2): tap(BTN_VOLP)
snap("slot11_constellation")
tap(BTN_HOME)                                            # the chain page of a CONSTELLATION slot
for _ in range(3): tap(BTN_RIGHT)                        # focus the SHIMMER block
check(chain_cursor() == 3, "> x3 reaches SHIMMER")
tap(BTN_PARA); tap(BTN_PARA)                             # click: +12 on, click: +7
snap("chain_shimmer_on")
check(tel().view == VIEW_CHAIN, "the chain page takes the shimmer block's clicks")
tap(BTN_RIGHT)                                           # wraps to EFFECT
check(chain_cursor() == 0, "> wraps from SHIMMER to EFFECT")
for _ in range(14): tap(BTN_VOLP)                        # step the effect type across the new effects
snap("chain_effect_stepped_far")
tap(BTN_HOME)

# ---- the block pages: HOME on the chain page opens the page of the block under the cursor ----
print("[5] block pages: opened from the chain page, VOL walks the blocks that are on, PARA hovers and clicks to edit")
FOCUS = {"effect": 0, "layer": 6, "space": 11, "shimmer": 15}
BLOCKS = ["effect", "layer", "space", "shimmer"]
def group(axis): return 3 if axis >= 15 else (2 if axis >= 11 else (1 if axis >= 6 else 0))
def goto(block):
    """from any page: HOME opens the chain page on the block we were on, > steps to the wanted block, HOME opens its page"""
    g = group(tel().selected_param)
    tap(BTN_HOME)
    for _ in range((BLOCKS.index(block) - g) % 4): tap(BTN_RIGHT)
    tap(BTN_HOME)
def chain_select(block):
    if tel().view != VIEW_CHAIN: tap(BTN_HOME)
    while chain_cursor() != BLOCKS.index(block): tap(BTN_RIGHT)
plain()
while tel().preset_index != 12: tap(BTN_VOLP)                     # slot 13: the first empty user slot
tap(BTN_HOME); tap(BTN_HOME)
check(tel().view == VIEW_PLAY and tel().selected_param == 0 and ui_state() == 1, "HOME, HOME: the effect's own page, first macro hovered, not editing")
for block in ("layer", "space", "shimmer", "effect"):
    goto(block)
    check(tel().view == VIEW_PLAY and tel().selected_param == FOCUS[block], "HOME on the %s block opens its page (focus %d)" % (block, FOCUS[block]))
check(tel().preset_index == 12, "the slot did not change while visiting the pages")

goto("effect"); v0 = dll.emu_get_slot_param(EFFECT_SLOT, 0)
enc(1); check(tel().selected_param == 1 and dll.emu_get_slot_param(EFFECT_SLOT, 0) == v0 and ui_state() == 1, "hovering: the PARA turn moves along the controls and changes no value")
enc(-1)
tap(BTN_PARA); check(ui_state() == 3, "PARA click: editing the hovered value")
snap("page_effect_editing")
enc(3); v1 = dll.emu_get_slot_param(EFFECT_SLOT, 0)
check(v1 != v0 and tel().selected_param == 0, "editing: the PARA turn changes the value (%d %% -> %d %%) and the hover stays" % (v0, v1))
tap(BTN_PARA); check(ui_state() == 1, "PARA click: back to hovering")
enc(2); check(dll.emu_get_slot_param(EFFECT_SLOT, 0) == v1 and tel().selected_param == 2, "and the turn hovers again, the value is kept")
enc(-2)
for block, slot, knob in (("effect", EFFECT_SLOT, 0), ("layer", LAYER_SLOT, 0), ("space", SPACE_SLOT, 0), ("shimmer", SPACE_SLOT, 4)):
    goto(block); before = dll.emu_get_slot_param(slot, knob); tap(BTN_PARA); enc(-3 if before >= 90 else 3); after = dll.emu_get_slot_param(slot, knob); tap(BTN_PARA)
    check(after != before, "the %s page: click, turn changes slot %d knob %d (%d %% -> %d %%)" % (block, slot, knob, before, after))
for block, first, n in (("space", 11, 4), ("shimmer", 15, 4), ("effect", 0, 6)):
    goto(block); seen = []
    for _ in range(n + 1): seen.append(tel().selected_param); enc(1)
    check(seen == list(range(first, first + n)) + [first], "on the %s page the PARA turn hovers %s and wraps" % (block, seen))
goto("effect"); enc(-1); check(tel().selected_param == 5, "hovering back from the first control wraps to the last (SPACE send)")
enc(1)
goto("shimmer"); enc(1); tap(BTN_PARA)                                    # the interval
vals = []
for _ in range(4): enc(-1); vals.append(dll.emu_get_slot_param(SPACE_SLOT, 5))
tap(BTN_PARA)
check(len(set(vals)) >= 3, "the shimmer interval moves in steps %s" % vals)
for b in BLOCKS:
    goto(b); snap("page_" + b)

# the blocks that are on: VOL- / VOL+ cycle through them
chain_select("layer"); tap(BTN_PARA)                                     # LAYER on
chain_select("space"); tap(BTN_PARA)                                     # SPACE on
chain_select("shimmer")
if dll.emu_get_slot_param(SPACE_SLOT, 4) == 0: tap(BTN_PARA)             # SHIMMER on (amount up, interval +12) unless the edits above left it up
check(dll.emu_get_slot_param(SPACE_SLOT, 4) > 0, "the shimmer is on")
tap(BTN_HOME)
goto("effect")
seen = []
for _ in range(5): tap(BTN_VOLP); seen.append(tel().selected_param)
check(seen == [6, 11, 15, 0, 6], "VOL+ walks the blocks that are on: LAYER, SPACE, SHIMMER, EFFECT, LAYER %s" % seen)
seen = []
for _ in range(4): tap(BTN_VOLM); seen.append(tel().selected_param)
check(seen == [0, 15, 11, 6], "VOL- walks them backwards %s" % seen)
check(tel().preset_index == 12, "and the preset did not change")
tap(BTN_PARA); check(ui_state() == 3, "editing...")
tap(BTN_VOLP); check(ui_state() == 1, "VOL leaves the editing: the new page starts hovering")
chain_select("space"); tap(BTN_PARA)                                     # SPACE off: the SHIMMER page has nothing to show either
tap(BTN_HOME); goto("effect")
seen = []
for _ in range(3): tap(BTN_VOLP); seen.append(tel().selected_param)
check(seen == [6, 0, 6], "a block that is off is skipped: SPACE and SHIMMER are left out %s" % seen)
chain_select("space"); tap(BTN_PARA); tap(BTN_HOME)                      # SPACE on again
goto("effect")
hold(BTN_VOLP, 900); check(tel().preset_index == 13 and tel().view == VIEW_PLAY and tel().selected_param == 0 and ui_state() == 1, "VOL+ held 0.8 s on a block's page: the next preset, back on its first page")
hold(BTN_VOLM, 900); check(tel().preset_index == 12, "VOL- held: back to slot 13")

# ---- save captures what every page set; away and back restores it ----
print("[6] save and load: every page's values come back")
want = {}
goto("effect"); enc(1); tap(BTN_PARA); enc(5); tap(BTN_PARA); want[(EFFECT_SLOT, 1)] = dll.emu_get_slot_param(EFFECT_SLOT, 1)
goto("layer"); enc(4); tap(BTN_PARA); enc(5); tap(BTN_PARA); want[(LAYER_SLOT, 4)] = dll.emu_get_slot_param(LAYER_SLOT, 4)
goto("space"); enc(1); tap(BTN_PARA); enc(-5); tap(BTN_PARA); want[(SPACE_SLOT, 1)] = dll.emu_get_slot_param(SPACE_SLOT, 1)
goto("shimmer"); tap(BTN_PARA); enc(6); tap(BTN_PARA); want[(SPACE_SLOT, 4)] = dll.emu_get_slot_param(SPACE_SLOT, 4)
enc(1); tap(BTN_PARA); enc(1); tap(BTN_PARA); want[(SPACE_SLOT, 5)] = dll.emu_get_slot_param(SPACE_SLOT, 5)
etype = dll.emu_get_slot_type(EFFECT_SLOT)
hold(BTN_PARA, 1300)
check(tel().view == VIEW_SAVE, "PARA held opens the save dialog")
tap(BTN_PARA); tick(300)
check(tel().view != VIEW_SAVE, "the save completes")
plain()
tap(BTN_VOLP); tap(BTN_VOLP); tap(BTN_VOLM); tap(BTN_VOLM)
check(tel().preset_index == 12, "back on slot 13 after leaving and returning")
for (slot, knob), v in sorted(want.items()):
    got = dll.emu_get_slot_param(slot, knob)
    check(abs(got - v) <= 1, "slot %d knob %d restored (saved %d %%, loaded %d %%)" % (slot, knob, v, got))
check(dll.emu_get_slot_type(EFFECT_SLOT) == etype, "the effect type is restored")

# ---- the tape keys: < records, > plays / dubs, < held resets ----
print("[7] ACCRETION keys: < records, > plays and dubs, < freezes a loop, < held resets")
plain()
for _ in range(20):
    if dll.emu_get_slot_type(EFFECT_SLOT) // 4 == 2: break         # the first ACCRETION slot
    tap(BTN_VOLP)
check(dll.emu_get_slot_type(EFFECT_SLOT) // 4 == 2, "slot %d is an ACCRETION chain" % (tel().preset_index + 1))
check(dll.emu_andromeda_looper_state() == 0, "its tape starts empty")
tap(BTN_LEFT); tick(20); check(dll.emu_andromeda_looper_state() == 1, "< starts the recording")
tick(170); check(dll.emu_andromeda_looper_state() == 2, "when the time is over it plays by itself")
tap(BTN_RIGHT); tick(10); check(dll.emu_andromeda_looper_state() == 3, "> dubs")
tap(BTN_RIGHT); tick(10); check(dll.emu_andromeda_looper_state() == 2, "> again goes back to play")
tap(BTN_LEFT); tick(10); check(dll.emu_andromeda_frozen() == 1, "< with a loop freezes it")
tap(BTN_LEFT); tick(10); check(dll.emu_andromeda_frozen() == 0, "< again releases it")
hold(BTN_LEFT, 900); tick(10); check(dll.emu_andromeda_looper_state() == 0, "< held 0.8 s resets the recording")
tap(BTN_LEFT); tick(20); check(dll.emu_andromeda_looper_state() == 1, "and < records again")
hold(BTN_LEFT, 900); tick(10)
i0 = tel().preset_index; hold(BTN_VOLP, 900); check(tel().preset_index == (i0 + 1) % 20, "VOL+ held on the plain play screen also steps the preset")

print("\n%d FAILURE(S)" % fails if fails else "\nALL ANDROMEDA NAVIGATION TESTS PASSED")
sys.exit(1 if fails else 0)
