# Andromeda recovery runbook

How to back up the unit's flash, install and verify an Andromeda image, and recover a unit that does not boot, over USB and with no soldering. Installing a build normally goes through [docs/ANDROMEDA-FLASHING.md](docs/ANDROMEDA-FLASHING.md); this page is for backups and for units that are dead.

## The safety net

The chip's mask ROM contains a USB download mode (`UBOOT1.00`, USB `4C4A:8057`, a SCSI disk). It lives in ROM, so nothing in the flash, not a bad update and not a dead application, can disable it. The unit's screen stays **black** in download mode; that is normal. To leave it, switch the unit off and on.

Ways in:

| Way in | When it works |
|---|---|
| `python custom-fw/tools/flash_unit.py --enter-download-mode` | The running firmware answers USB MIDI. It sends the soft key `F0 22 24 35 7D F7`. |
| **PARA held 2.5 s at power-on** | The firmware starts far enough to read the encoder. |
| Automatic | A boot that does not finish within 5 s, repeated failed boots, a CPU fault, or the audio interrupt or main loop frozen for about 3 s. |
| USB-key dongle (an RP2040 board running the Transporter firmware) | Always, even if nothing in the flash runs. The dongle sends the chip's USB key at power-on and the ROM then enters download mode. |

## Flash map (1 MiB, JEDEC `856014`)

| Range | Contents | Written by Andromeda's flash? |
|---|---|---|
| `0x000000`-`0x003FFF` | Boot area: flash header and bootloader (the way back in) | **never** (only with `--boot-area` in an emergency) |
| `0x004000`-`0x085FFF` | App area: file table, `app.bin`, tool configuration | **yes** |
| `0x086000`-`0x0B3FFF` | Store: settings, presets, user data | not by the flash. Andromeda writes its own preset bank here on first boot |
| `0x0B5000`-`0x0BEFFF` | Radio calibration and address, device calibration written at the factory | **never** |
| `0x0BF000`-`0x0FFFFF` | Data written at the factory (read-only for this project) | **never** |

"Never" is enforced by the flashing tools: they refuse writes outside the app area unless an emergency option is given.

## Take a backup before the first flash

A full-chip dump, read twice and compared, is the way back for any failure. Keep it in two places off the PC. `custom-fw/qa/hw_ledger.json` records the baseline hash that the tools check before they write.

```bash
python custom-fw/tools/pa_transporter.py --timeout 300 dump <folder>
```
```bash
python custom-fw/tools/qa_recovery.py verify <folder>/dump1.bin <folder>/dump2.bin --record
```

The first command reads the chip twice and writes `dump1.bin` and `dump2.bin`; the second compares them, hashes them and records the result under `custom-fw/qa/backups/<hash>/`.

## The dongle route (for a unit that does not respond)

What you need:

- A **Seeed XIAO RP2040** with the Transporter firmware ([custom-fw/tools/transporter/README.md](custom-fw/tools/transporter/README.md)).
- A **USB-C pigtail** into the pedal's port, three wires:

  | XIAO pin | Pedal USB |
  |---|---|
  | D6 (GP0) | D+ |
  | D7 (GP1) | D- |
  | GND | GND |
  | none | VBUS: **not connected** |

- The pedal on its **battery** (charged); it is not powered by the dongle.
- Python 3 with `pyserial`.

Before every session:

1. Pedal **off**, pigtail connected to the dongle, the pedal **not** plugged into the PC.
2. **Unplug and replug the dongle.** A dongle that has already run a session sits in host mode and does not send the key again.
3. **Start the command first**, then switch the pedal **on** when it prints `Waiting for the Pocket Amp's ROM download mode`.
4. Expect `UBOOT session up` after 13 to 35 s.
5. Do the work in one go; the pedal does not stay in download mode forever. If a command prints `Waiting...` again, start over from step 1.

In `console.log`, success is `PULSE: D+ high (ROM pull-up)`, then `VID=4C4A PID=8057`, then `UBOOT READY`. Repeated `ACK` / `false ACK, resuming key` lines are normal.

## Restore the app area from your backup

Writes only bytes from a verified backup, and only if that backup's hash is the one in the ledger. Each sector is erased, written, read back and compared.

```bash
python custom-fw/tools/pa_transporter.py --timeout 300 drill <folder>/dump1.bin 0x4000-0x86000 --repair --full-verify
```
```bash
python custom-fw/tools/pa_transporter.py runapp
```

- `--repair` rewrites sectors that differ; without it the drill refuses any sector that differs (a rehearsal with no change).
- `--full-verify` re-reads the whole chip afterwards. Writing the app range takes about half a minute; the read-backs add a few minutes.
- `runapp` boots the unit, or just power-cycle it without the dongle.

To install an Andromeda image instead of restoring a backup, use `flash_unit.py` as described in [docs/ANDROMEDA-FLASHING.md](docs/ANDROMEDA-FLASHING.md).

### A dead unit (black screen, nothing on USB)

The same procedure. If you do not know which sectors are damaged, restore the whole app range.

### The boot area (emergency only)

Only if an update died while writing `0x0`-`0x4000`:

```bash
python custom-fw/tools/pa_transporter.py --timeout 300 drill <backup dump> 0x1000-0x4000 --boot-area --repair --full-verify
```

The ROM download mode does not depend on these sectors, so a failed attempt can be retried. Avoid sector `0x0` unless it is the one that is damaged.

## Recovery test (destructive until repaired)

```bash
python custom-fw/tools/pa_transporter.py --timeout 300 break <backup dump> 0x5000 --i-understand
```

Blanks one app sector. It refuses unless the backup is the ledger baseline and the chip holds the baseline there. Confirm the unit is dead, then restore it as above, and record the route with `qa_recovery.py route usb_key_dongle --status proven --evidence ...`.

## Never

- Never use another project's write tool on this pedal: its flash window and map differ.
- Never use `jl-uboot-tool`'s own `write`, `erase`, `erasechip` or `burnchipkey`.
- Never write from a backup whose hash is not recorded in the ledger.
- Keep diagnostic logs and binary backups in an untracked directory.
