# Andromeda MIDI map

What the firmware does with MIDI over USB and Bluetooth LE. Both transports share one handler (`src/board/usb_ble.c`); `test/test_midi_control.py` (gate F-17) drives that handler end to end. The unit reacts on any MIDI channel (omni).

## 1. Transports

- **USB MIDI**: a class-compliant device (4-byte event packets), shown to the host as "Custom MIDI".
- **BLE MIDI 1.0**: service `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`, characteristic `7772E5DB-3868-4112-A1A9-F2669D106BF3` (notify and write without response). The Bluetooth stack is described in [NOTICE.md](../NOTICE.md).

## 2. Program change

`Cn pp` selects a slot: `pp` = 0 to 19 loads P01 to P20. Values above the last slot are not used.

## 3. Control change

A button or toggle acts when the value is 64 or more; a lower value is ignored.

| CC | Function |
|---|---|
| 7 | Preset volume, 0 to 127 maps to 0 to 100 % |
| 20 | Toggle DYN |
| 22 | Toggle LAYER (the GLASS layer) |
| 23 | Toggle EFFECT (the six-effect host) |
| 24 | Toggle SPACE (the reverb) |
| 25 | Toggle SHIMMER (replaces CAB in Andromeda) |
| 27 | Toggle EQ |
| 28 to 35 | Set a block on (64 or more) or off (below 64), in the order DYN, AMP, LAYER, EFFECT, SPACE, SHIMMER, DRIVE, EQ |
| 64 | `HOME` button |
| 65 | `PARA` click |
| 66 | `PARA` turn counter-clockwise |
| 67 | `PARA` turn clockwise |
| 68 | `Footswitch <` (Primary Action: Strike GLASS / Flip BLOOM Reverse / Tap ACCRETION) |
| 69 | `Footswitch >` (Secondary Action: Soft Strike GLASS / Freeze BLOOM / Rec-Play-Dub ACCRETION) |
| 70 | `HOME hold` (Freeze) |
| 71 | `PARA hold 3s` (Clear: Stop ringing / Clear history / Erase tape) |
| 80 / 81 | Next / previous slot (wraps within P01 to P20) |
| 85 | Tuner on (64 or more) or off |
| 88 | Save the current sound into the current slot (64 or more) |
| 98, 99, 6, 38, 100, 101 | NRPN, section 4 |
| 102 to 105 | Direct EFFECT Macros 0 to 3 |
| 106 | Direct EFFECT MIX |
| 107 to 110 | Direct LAYER Macros 0 to 3 (MATERIAL, TUNE, DECAY, BRIGHT) |
| 111 | Direct LAYER LEVEL |
| 112 to 115 | Direct SPACE Knobs 0 to 3 (DECAY, SIZE, TONE, SEND) |
| 116 to 117 | Direct SHIMMER Knobs (AMOUNT, INTERVAL) |
| 118 | Direct EFFECT SPACE SEND (send level into Space FDN reverb) |

CC 20 to 35 address the chain's blocks. Andromeda repurposes AMP and DRIVE, keeping them off. CC 25 and 33 control SHIMMER. CC 86 addresses features Andromeda does not have. CC 90 to 94 are reserved for bench diagnostics. All other numbers are unused.

## 4. NRPN: every parameter, full resolution

Select with `CC 99 = module` and `CC 98 = parameter`, then send the value with `CC 6` (high 7 bits) and optionally `CC 38` (low 7 bits). The 14-bit value `D = CC6 x 128 + CC38` maps to the knob as a percentage `round(D x 100 / 16383)`; with `CC 6` alone, `round(CC6 x 100 / 127)`. RPN null (`CC 101` and `CC 100` both 127) clears the selection.

| Module (CC 99) | Block | What it is in Andromeda |
|---|---|---|
| 0 | DYN | Dynamics |
| 2 | LAYER | The second GLASS: knobs 0 to 3 are MATERIAL, TUNE, DECAY, BRIGHT; knob 4 is its level (0 switches the block off) |
| 3 | EFFECT | The six-effect host: knobs 0 to 3 are the four macros of the current effect; knob 4 is MIX; knob 5 is SPACE SEND |
| 4 | SPACE / SHIMMER | The reverb: knobs 0 to 3 are decay, size, tone, send; knobs 4 and 5 are SHIMMER feedback amount and interval |
| 7 | EQ | The equaliser |
| 8 | GLOBAL | General settings, parameters 0 to 17 (data in `CC 6` alone when below 128, or `CC 6` = D >> 7 and `CC 38` = D & 127 above that) |
| 10 | PRESET | Parameter 0 is the preset volume |

| Parameter (CC 98) | Meaning |
|---|---|
| 0 to 5, or 0x60 to 0x65 | The block's knob 0 to 5 (both ranges address the same native knobs) |
| 0x40 | Type. For the EFFECT module this is the effect type 0 to 23 (effect x 4 + variant: GLASS 0 to 3, BLOOM 4 to 7, ACCRETION 8 to 11, CHOIR 12 to 15, FRACTURE 16 to 19, CONSTELLATION 20 to 23); values of 0x20 and above are the firmware's own type id plus 0x20 |
| 0x41 | Enable (64 or more is on) |

A parameter the block does not have is ignored. A change of a general setting is saved to the store about two seconds later; the master pot and a MIDI level follow the last change.

## 5. SysEx

- **Identity query** `F0 00 32 45 00 00 00 40 7F F7`: answered over USB and BLE.
- **Download mode** `F0 22 24 35 7D F7`: the running firmware reboots into the ROM download mode (used by `flash_unit.py --enter-download-mode`).
- **Editor protocol**: the web editor reads and writes presets, settings and the update package with framed SysEx messages (`src/sys/sysex.c`; tests `test_sysex_*`, `test_editor_protocol.py`).
- **Diagnostic and measurement channel**: manufacturer ID `0x7D`, frames packed 8-to-7: `7D 01` flash read, `7D 02` RAM read, `7D 03` profile, `7D 04` timing, `7D 05` memory, `7D 06` reset, `7D 07` control (layouts in `include/pa_diag.h` and `include/pa_meas.h`; the readouts are in [../docs/ANDROMEDA-PERF.md](../docs/ANDROMEDA-PERF.md)).
