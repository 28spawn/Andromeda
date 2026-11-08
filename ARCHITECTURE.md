# Andromeda architecture

How the Andromeda firmware is put together: modules, execution contexts, the audio path, memory and the host tooling. The firmware targets the JieLi AC791N (two pi32v2 cores, 512 KB SRAM) and is built with `custom-fw/build.ps1 -Product andromeda` (`PA_PRODUCT_ANDROMEDA=1`, linker script `custom-fw/app_andromeda.ld`). Numbers marked *measured* come from the measurement channel ([docs/ANDROMEDA-PERF.md](docs/ANDROMEDA-PERF.md)).

## 1. Layers

`custom-fw/include/pa_plat.h` is the hardware seam: portable code in `src/dsp`, `src/fx` and `src/sys` never touches registers; only `src/board` does.

| Directory | Contents |
|---|---|
| `src/board/` | Entry code and interrupt vectors, the audio link (`audio_alnk.c`), display driver (`st7735s.c`), buttons and encoder (`gpio_scan.c`), flash (`spi_nor.c`), USB device and MIDI/audio (`usb_dev.c`, `usb_ble.c`), BLE MIDI (`ble_*`), the UI (`ui_app.c`, the radar-style play screen in `ui_cosmic.c`, the 5x7 font in `ui_5x7.c`), the second core's start (`cpu1.c`) and `main.c` (integration and the interrupt body). |
| `src/dsp/` | Fixed-point and filter primitives (`pa_biquad.c`, `fixedpoint.h`, oversampling), the tuner and the hardware accelerator wrapper. |
| `src/fx/` | The effect graph (`fx_graph.c`: slot chain, core allocator), the effect host (`fx_andromeda.c`), the six engines (`v1_glass.c`, `v1_bloom.c`, `v1_accretion.c`, `v1_choir.c`, `v1_fracture.c`, `v1_constellation.c`), the shared reverb (`v1_fdn.c`) and shimmer (`v1_shimmer.c`), plus the dynamics, EQ and modulation blocks. |
| `src/sys/` | The audio core (`pa_audio_core.c`), presets and settings, SysEx and the diagnostic channel (`sysex.c`, `pa_diag.c`), the measurement channel (`pa_meas.c`), the core 1 worker (`smp_cpu1.c`), the pool allocator, the CPU meter, the update session. |
| `include/` | Public headers: `pa_config.h` (constants and the product switch), `pa_fx.h` (nodes and the chain), `pa_granular.h` (the effect host's types), `pa_preset.h`, `pa_ui.h`, `pa_meas.h`. |

Everything specific to Andromeda is under `#if PA_PRODUCT_ANDROMEDA`; the other product configuration of this source tree is not described here.

## 2. Execution contexts

| Context | Entry | Rate | Work |
|---|---|---|---|
| **Audio interrupt (core 0)** | `isr_alnk0` in `entry.S`, then the audio core | one 64-frame block per interrupt (44,118 Hz, 1.45 ms) | The effect chain's core-0 share, the USB audio mix, the output stage. |
| **Core 1 worker** | `smp_cpu1.c` | per block, when the chain is split | Input conditioning and the first slots of the chain (see section 4). |
| **UI thread** | `pa_main_loop` | 60 Hz | Display, UI state, presets, settings, flash writes, SysEx. |
| **Poll loop** | `pa_main_loop` | 1 kHz | Button and encoder scanning. |

State shared between the UI thread and the interrupt is handed over at block boundaries: effect changes, preset swaps and effect-host actions are posted by the UI thread and taken by the audio thread, which owns the effect memory.

## 3. Audio path

One block through `pa_audio_core_process_block`:

1. Apply a pending preset or effect swap (the output is faded to zero first, so nothing clicks).
2. The measurement channel's test source, when on, replaces the guitar.
3. The tuner takes the dry guitar when it is open.
4. The chain runs in stereo, in slot order: **DYN, EFFECT, EQ, LAYER, SPACE** (the other three slots of the graph are locked off). EFFECT is the effect host (`fx_andromeda.c`) running the selected engine; LAYER is a second GLASS; SPACE is the FDN reverb with the shimmer path.
5. USB playback from the host is mixed after the chain, and the output goes through a soft ceiling and a DC blocker.
6. The master volume is applied on a linear slew with dither, then the block goes to the DAC.

### The effect host

`fx_andromeda.c` owns one 147 KB overlay in the `.pool` region, shared by the six engines through a union: only the effect that is playing owns it. A change of effect is requested by the UI thread; the audio thread clears the overlay and starts the new engine at the next block. The four macro knobs and MIX come in as parameters 0 to 4 and are mapped to each engine's own parameters (`glass_from_macros`, `apply_bloom`, `apply_acc`, `apply_choir`, `apply_fracture`, `apply_constellation`). Each engine's wet output gets a per-effect level trim, then the MIX law (both at full level at 50 %, the wet fading out below it and the dry above it) and a clamp.

| Engine | State |
|---|---|
| GLASS | 12 resonators per channel struck by a transient detector or the keys; look-ahead limiter on the sum. |
| BLOOM | A grain cloud reading a 128 KB mono history. |
| ACCRETION | A 2 s stereo ADPCM tape (the 147 KB overlay's largest user) with a look-ahead write limiter; tone and saturation colour it on playback. |
| CHOIR | Pitch-shifting voices (two-head delay line) reading the history. |
| FRACTURE | Tempo-synced slices cut from the history. |
| CONSTELLATION | A 16-step generative sequencer playing grains from the history. |

## 4. Two cores

The chain can be split across the two cores. The **allocator** (`fx_graph.c`) prices every active slot from its measured time (a moving average kept by whichever core ran it) and every 64 blocks chooses a *cut*:

- **Cut 0**: everything runs on core 0, no added latency. Used while the chain fits (it splits above about 0.9 ms of core-0 work and returns below about 0.78 ms).
- **Cut N**: core 1 runs the input conditioning and the first N slots on the current block while core 0 runs the rest and the output stage on the previous block's result, which costs one block (1.45 ms) of added latency.

A candidate cut has to beat running on one core by 8 % for two evaluations in a row. A slot is never split. If core 1 does not start, misses its block 64 times in a row, refuses a job or nears its stack limit, it is switched off for the session and everything runs on core 0; holding `>` at power-on forces one core. Details and the readouts are in [docs/ANDROMEDA-PERF.md](docs/ANDROMEDA-PERF.md).

## 5. Memory

512 KB of SRAM: about 398 KB static, with a 336 KB `.pool` region (289 KB used, about 47 KB free; *measured*). The `.pool` region holds the effect overlay (147 KB), the reverb and modulation state, and a 64 KB DSP pool (`PA_DSP_POOL_BYTES`) for the allocator. The factory reset and the update staging borrow the overlay as scratch memory while the effect passes the signal dry (`pa_gran_tape_borrow`). `test/test_andromeda_pool.c` measures the pool's high-water mark with the device allocator.

Flash (1 MB): the boot area `0x0`-`0x3FFF` is never written; the app area `0x4000`-`0x85FFF` holds the firmware (about 410 KB of its 531 KB); the store at `0x86000` holds settings and the preset bank. See [docs/ANDROMEDA-FLASHING.md](docs/ANDROMEDA-FLASHING.md).

## 6. Control and data

- **Controls**: the 1 kHz scanner decodes the five keys, the PARA encoder (turn, click, holds) and the MASTER pot; the UI thread turns them into macro changes, effect actions and navigation.
- **MIDI and SysEx**: USB MIDI and BLE MIDI share one handler (program change, CC, SysEx). A private diagnostic channel (manufacturer ID 0x7D) carries flash and RAM reads and the measurement protocol (timing, memory, control).
- **Persistence**: presets and settings are written to the store area with an A/B scheme, so a power cut during a save leaves the previous copy valid. The factory bank is written on first boot; slots P13 to P20 are saved from the chain page.
- **USB audio**: capture sends the output stage's signal to the host; playback from the host is mixed after the chain through a rate-adapting ring. The standard firmware has no USB-to-input path.

## 7. Host tooling

| Tool | Purpose |
|---|---|
| `custom-fw/emu/` | Native emulator: the firmware compiled as `pocket_amp_emu.dll`, served by `server.py` with a browser front panel (the LCD framebuffer, encoder, meters). Used by the UI, level and screenshot tests. |
| `custom-fw/wasm/` | WebAssembly build of the same core for the browser workbench and its tests. |
| `custom-fw/test/` | C and Python regression suites. The Andromeda ones include `test_andromeda_fx`, `test_andromeda_smp`, `test_andromeda_pool`, `test_glass`, `test_bloom`, `test_accretion`, `test_choir`, `test_fracture`, `test_constellation`, `test_fdn`, `test_shimmer`, `test_meas` and `test_ui_cosmic`. |
| `custom-fw/tools/qa_gate.py` | The QA gate (see [TEST-PROTOCOL.md](TEST-PROTOCOL.md)). |
| `custom-fw/tools/andromeda_perf.py` | CPU, memory and core readouts and the scenario matrix on the unit. |
| `custom-fw/tools/effect_audibility.py` | Effect loudness against a dry lick, on the emulator. |
| `custom-fw/tools/render_screens.c` | Renders the play screens with the firmware's own UI code (the README pictures). |
| `custom-fw/tools/pack_ufw.py`, `flash_unit.py`, `jl_safe_flash.py` | Packaging and verified flashing. |

## 8. Build and release

`build.ps1 -Product andromeda` runs the host test suites, cross-compiles the device build, links with `app_andromeda.ld`, checks the image against the app slot, packages the release (`custom-fw/release/Pocket_AMP_Andromeda_<version>/`: `.bin`, `.fwsc`, checksums) and finishes with the effect matrix check. The QA gate then adds the static, functional and image-integrity checks and the recorded hardware evidence. A passing gate means *ready for bench test*, not release approval.
