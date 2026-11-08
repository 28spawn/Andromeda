# Flashing Andromeda

How to put an Andromeda build on a Pocket AMP, check it, and get back to a known state. For a dead unit, see [RECOVERY.md](../RECOVERY.md).

## What gets written

Andromeda goes into the **app area only**: flash `0x4000`–`0x85FFF`. The 0.10 image is about 410 KB of
the 531 KB slot. Nothing else is touched:

| Region | | Flash of Andromeda |
|---|---|---|
| `0x000000`–`0x003FFF` | boot area (the way back in) | never |
| `0x004000`–`0x085FFF` | app area | **written** |
| `0x086000`–`0x0B3FFF` | store: settings and presets | not by the flash. On first boot Andromeda writes **its own preset bank** here (it ignores a bank with a different signature) |
| `0x0B5000`–`0x0BEFFF` | radio calibration and address, device calibration written at the factory | never |
| `0x0BF000`–`0x0FFFFF` | data written at the factory | never (read-only, Andromeda does not use it) |

## The safety net (check before you start)

1. **The ROM download mode cannot be lost.** The chip's mask ROM enters it whatever the flash
   holds. Ways in:
   - from running Andromeda firmware: `flash_unit.py --enter-download-mode`,
     which sends the soft key `F0 22 24 35 7D F7` over USB MIDI;
   - **PARA held 2.5 s at power-on**;
   - automatically: a boot that does not finish in 5 s, repeated failed boots, a CPU fault, or the
     audio interrupt / main loop frozen for ~3 s;
   - the XIAO RP2040 USB-key dongle (RECOVERY.md). This works even if nothing in the flash runs.
2. **A full-chip backup** whose SHA-256 is in `custom-fw/qa/hw_ledger.json` (how to take one: RECOVERY.md). Keep a copy off this PC.
3. **A way back**: the backup above, or an earlier Andromeda package from `custom-fw/release/`.
4. Battery charged, a USB **data** cable, the unit plugged straight into the PC (no hub).

## Route A - web editor, over USB MIDI (simplest)

No administrator rights, no download mode, no dongle. The running firmware checks the package,
stages the update loader, and the loader writes the app area only.

1. Build the package (or use the one already built):
   ```
   powershell -ExecutionPolicy Bypass -File custom-fw\build.ps1 -Product andromeda
   ```
   It writes `custom-fw/release/Pocket_AMP_Andromeda_0.10/Pocket_AMP_Andromeda_0.10.fwsc`.
2. Open the web editor, `custom-fw/web/Onyx_Web_Editor.html`, in **Chrome or Edge** and allow MIDI with SysEx.
   Close any other program that uses the unit's MIDI port.
3. Firmware tab > *Install a firmware* > pick **`Pocket_AMP_Andromeda_0.10.fwsc`** from the
   release folder. Use the release-folder `.fwsc`: it carries the update loader, which writes the app
   area only. The `Pocket_AMP_Andromeda.fwsc` next to `build.ps1` is a development container; its install can also erase stored user data.
4. *Install this firmware*. Do not unplug. If it is cut off, the unit stays in update mode and
   the same button continues.
5. The unit restarts into Andromeda.

## Route B - PC tool, in the download mode (verified sector by sector, with a snapshot)

Every sector is erased, written, read back and compared; the whole chip is read back at the end;
a time-stamped snapshot of the chip is taken first.

Run in an **Administrator** terminal, from the repository root:

```
python custom-fw\tools\flash_unit.py --image custom-fw\Pocket_AMP_Andromeda_app_area.bin --ref <backup folder>\full_dump.bin --jl-tool <path to jl-uboot-tool> --enter-download-mode
```

1. `--enter-download-mode` reboots the running unit into the ROM download mode: the screen goes
   black, and Windows sees a disk "WL82 UBOOT1.00". Already in the download mode? Leave the
   option out.
2. The tool checks the chip (key `0x980F`, flash `0x856014`), dumps the chip twice, and shows the
   dry run. Expect a range inside `0x004000`–`0x085FFF` (about 0x4000–0x67FFF for this build). Anything outside the app area: stop.
3. Type `WRITE`. It takes well under a minute to write, plus the read-backs.
4. Power-cycle the unit (or let the tool restart it). It boots into Andromeda.

Rehearse first with no device (same plan, simulated flash):
```
python custom-fw\tools\jl_safe_flash.py --mock-flash <copy of full_dump.bin> write custom-fw\Pocket_AMP_Andromeda_app_area.bin --at 0x4000 --ref <copy of full_dump.bin>
```

## First-boot checks

- [ ] Boots within a few seconds into the home screen, `I GLASS CRYSTAL` (violet badge). The first boot writes Andromeda's own factory presets: presets saved by an earlier build with a different bank signature are gone.
- [ ] Settings > System shows **ANDROMEDA**, version **0.10**, a new build code.
- [ ] GLASS (P01-P04): play a note; the 12 mode planets and bars light up and ring. `<` strikes by itself. Turn PARA: MATERIAL steps through crystal, bell, kalimba, metal, wood, marimba.
- [ ] VOL+ to P05 (BLOOM): a cloud grows from what you play; grains show on the radar. `>` freezes the cloud (ice), again releases it.
- [ ] The first ACCRETION slot (step with VOL+): `<` starts the recording of 2 s (red), it plays by itself when the time is over (`<` while recording ends it early), `>` overdubs (orange), `>` again goes back to play. With a loop in place `<` freezes it and `<` again releases it. `<` held 0.8 s erases the tape. GR % on the radar is the limiter: it should be 0 at normal levels and rise only on loud overdubs.
- [ ] HOME opens the SIGNAL CHAIN page (IN, EFFECT, LAYER, SPACE, OUT); HOME again goes back. HOME held 1 s, from either page, opens Settings; HOME in Settings returns to the play screen.
- [ ] Chain page: `<` `>` (or the PARA turn) move the cursor between the blocks; VOL- / VOL+ change the block under it (EFFECT steps through the 24 effect/variant types, LAYER the material, SPACE and SHIMMER the amount); PARA click switches the block on or off; the squares at the bottom are the 20 slots, VOL- / VOL+ held 0.8 s move between them (they wrap); PARA held 1 s opens the save dialog.
- [ ] The six effects (VOL+ through the slots, or step the EFFECT block on the chain page: 24 types):
  - P07 CHOIR ANGELS: play a chord; voices swell in over the note (a major arrangement), the radar shows them as planets, `<` or `>` holds the chord. The voicing is the first tile (OCTAVE, FIFTH, MAJOR, MINOR, MASSIVE, CLUSTER).
  - P09 FRACTURE CLOCK: slices of what you just played repeat in time with the unit's tempo (the BPM setting), the ring shows the 16 steps; `<` is a reverse burst, `>` holds the audio the slices are cut from.
  - P11 CONSTELLATION STARDUST: play a chord and a melodic sequence from it plays on its own (tempo, scale, motion and density are the tiles), `<` restarts the sequence, `>` holds the chord, `<` held 0.8 s rerolls the pattern.
- [ ] SPACE is the shared FDN reverb: its knob on the play screen is the send. On the chain page the SHIMMER block (after SPACE) steps off / +12 / +7 / +12 and +7 / +19 with the click and sets the amount with `<` `>`. P07 and P04 ship with a shimmer.
- [ ] Performance page: note the CPU for each of the six effects; `python custom-fw/tools/andromeda_perf.py matrix` measures them all (docs/ANDROMEDA-PERF.md). CHOIR with four voices and the FDN reverb are the ones to watch.
- [ ] Block pages: on the chain page, move the cursor to a block with `<` `>` and press HOME. EFFECT opens the play screen; LAYER shows its four macros and level; SPACE shows DECAY, SIZE, TONE, SEND; SHIMMER shows AMOUNT, INTERVAL, DECAY, SEND. On these pages VOL- / VOL+ step through the blocks that are switched on in the preset (a block that is off is skipped); the PARA turn only hovers; a PARA click selects the hovered control (its frame doubles) and the turn then changes the value; a second click goes back to hovering; VOL- / VOL+ held 0.8 s change the preset; HOME returns to the chain page; the slot does not change.
- [ ] Edit knobs on all four pages, hold PARA to save, switch slot and come back: every value is as saved.
- [ ] Slots 13-20 are EMPTY (all blocks off): build a chain on the chain page, hold PARA, save to the slot.
- [ ] On the plain play screen (the one after power-on or Settings) the PARA turn changes the focused value and the PARA click moves the focus: 4 macros, MIX, SPACE (SPACE = the reverb's mix). VOL- / VOL+ change the preset.
- [ ] Reset: `<` held 0.8 s resets the effect (ACCRETION erases the tape). While you turn the MASTER pot there is no overload screen and no crackle.
- [ ] Switching presets between the three effects while playing: no click, no hang.
- [ ] Settings > Performance: note the CPU % for each effect. This is the first real measurement on the device.
- [ ] Leave it running 10 minutes with audio: no reset, no freeze.

If it does not boot, it should drop into the download mode by itself within ~5 s (black screen,
"WL82 UBOOT1.00" on the PC). Restore your backup with Route B (without `--enter-download-mode`) or by the procedure in RECOVERY.md.
