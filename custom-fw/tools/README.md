# Tools

Host-side tools for building, testing, measuring and recovering the Andromeda firmware. Run them from the repository root, for example `python custom-fw/tools/qa_gate.py`.

| Tool | Purpose |
|---|---|
| `qa_gate.py` | The QA gate: static, build, unit, functional, image and hardware-evidence checks ([../../TEST-PROTOCOL.md](../../TEST-PROTOCOL.md)). |
| `andromeda_perf.py` | CPU, memory and two-core readouts from the unit over USB MIDI, and the scenario matrix ([../../docs/ANDROMEDA-PERF.md](../../docs/ANDROMEDA-PERF.md)). |
| `effect_audibility.py` | How loud each of the 24 effect types is against a dry lick, on the emulator. |
| `render_screens.c` | Renders the play screens with the firmware's own UI code to BMP files. Build: `clang -O1 -Icustom-fw/include -DPA_HOST_SIM=1 -DPA_EMU_DLL=1 -DPA_PRODUCT_ANDROMEDA=1 custom-fw/tools/render_screens.c custom-fw/src/board/ui_cosmic.c custom-fw/src/board/ui_5x7.c -lm`. |
| `stress_effects.py` | The effect-matrix stress test ([../test/EFFECT_MATRIX.md](../test/EFFECT_MATRIX.md)). |
| `preset_click_probe.c` | `probe <from> <to> <sine dBFS>`: the largest sample step at a preset switch against the normal step size. |
| `ui_screenshot_harness.c` | Renders screens into a 240 x 240 RGB565 framebuffer (`*.raw`). |
| `pack_ufw.py`, `flash_unit.py`, `jl_safe_flash.py`, `pa_transporter.py`, `qa_recovery.py` | Packaging, verified flashing, backups and recovery ([../../RECOVERY.md](../../RECOVERY.md), [../../docs/ANDROMEDA-FLASHING.md](../../docs/ANDROMEDA-FLASHING.md)). |

The C probes are built on a PC together with the firmware sources: `PA_HOST_SIM` replaces the hardware and the audio path runs unchanged, for example:

    SRCS="src/board/main.c src/board/audio_alnk.c src/board/ui_app.c src/board/ui_5x7.c \
          src/board/usb_ble.c src/board/spi_nor.c src/board/gpio_scan.c src/sys/*.c src/fx/*.c src/dsp/*.c"
    clang -O2 -DPA_HOST_SIM=1 -DPA_EMU_DLL=1 -DPA_POOL_TEST=1 -DPA_PRODUCT_ANDROMEDA=1 -Iinclude -Isrc/dsp tools/<tool>.c $SRCS -lm -o tool
