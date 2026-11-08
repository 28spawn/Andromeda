# Andromeda firmware: source tree

The firmware source for the Pocket AMP's JieLi AC791N (pi32v2, two cores; `PA_CPU_HZ` in `include/pa_config.h` assumes 360 MHz, the data sheet lists 320 MHz). The project overview, the effects and the installation steps are in the [root README](../README.md); the module map is in [../ARCHITECTURE.md](../ARCHITECTURE.md).

## Layout

| Directory | Contents |
|---|---|
| `include/` | Interfaces and configuration (`pa_config.h` holds the product switch). |
| `src/board/` | Hardware drivers, the UI, USB and BLE, the interrupt body. |
| `src/dsp/` | Filters, fixed-point helpers, tuner. |
| `src/fx/` | The effect graph and allocator, the effect host and the six engines, reverb, shimmer. |
| `src/sys/` | Audio core, presets, settings, SysEx, diagnostic and measurement channels, core 1 worker. |
| `test/` | C and Python regression suites. |
| `emu/` | The native emulator and browser front panel (`pocket_amp_emu.dll`, `server.py`, `ui.html`). |
| `wasm/` | WebAssembly build and tests. |
| `tools/` | Build, packaging, QA, measurement and recovery tools. |
| `web/` | The web editor. |
| `qa/` | Hardware ledger, baselines and gate reports. |

## Audio

- 64-sample blocks at the codec's 44,118 Hz (1.45 ms). Audio inside the effects is single-precision float; the block interface and the chain's dry path use Q21 fixed point (`1 << 21` = full scale).
- The effects are the six engines in `src/fx/v1_*.c`, hosted by `src/fx/fx_andromeda.c` (see the architecture document), followed by the shared reverb with shimmer.
- Both cores are used: an allocator in `src/fx/fx_graph.c` splits the chain when one core would not fit it (see [../docs/ANDROMEDA-PERF.md](../docs/ANDROMEDA-PERF.md)).

## Front panel

A 240 x 240 display, five keys (`HOME`, `VOL-`, `VOL+`, `<`, `>`), the PARA push-encoder and the MASTER pot. The gestures are in the root README; `src/board/ui_app.c` is the UI state machine and `src/board/ui_cosmic.c` draws the play screen.

## Building and verifying

```powershell
# Standard build (Bluetooth LE disabled by default):
powershell -ExecutionPolicy Bypass -File build.ps1 -Product andromeda

# Optional: Build with Bluetooth LE enabled:
powershell -ExecutionPolicy Bypass -File build.ps1 -Product andromeda -EnableBle
```

The script:

1. Compiles and runs the host test suites.
2. Cross-compiles the device build with the JieLi clang and links it with `app_andromeda.ld` (with `-DPA_BLE=0` by default, or `-DPA_BLE=1` with `-EnableBle`).
3. Checks the image against the app slot, runs the effect matrix on the emulator DLL, and packages the release to `release/Andromeda_<version>/`.

Then `python tools/qa_gate.py` runs the full gate ([../TEST-PROTOCOL.md](../TEST-PROTOCOL.md)).

## Emulator

```powershell
powershell -ExecutionPolicy Bypass -File emu/build_dll.ps1 -Product andromeda
powershell -ExecutionPolicy Bypass -File emu/run_emu.ps1
```

The first builds the emulator DLL from the same sources as the firmware; the second serves the front panel at `http://localhost:8080`. See [emu/README.md](emu/README.md).
