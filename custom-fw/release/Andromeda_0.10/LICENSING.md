# Andromeda licensing

Andromeda is free software. Its original code is licensed under the **GNU General Public License**,
version 3 or later (`GPL-3.0-or-later`, or `GPL-3.0-only` for modules derived from GPL-3.0-only upstreams;
full text in [LICENSE](LICENSES/LICENSE)).

Andromeda is an independent custom firmware, DSP engine, desktop emulator, and tooling platform authored for the
**M-VAVE / Donner Pocket AMP** (JieLi AC791N SoC: dual pi32v2 cores, 512 KB SRAM, 1 MB SPI NOR flash, ST7735S 240x240 LCD).
It provides an ambient, granular, and generative sound architecture (the GLASS, BLOOM, ACCRETION, CHOIR, FRACTURE, and CONSTELLATION engines,
alongside a feedback-delay-network reverb, shimmer, and cosmic radar UI).

To maintain verifiable legal provenance, modular interoperability, and clean separation of concerns, Andromeda builds upon
foundational work and permissive/open modules from the wider JieLi custom firmware ecosystemâ€”most notably **Pendel** and **pendel-bsp**
(Leo Kuroshita, HÃ¼gelton Instruments), **Optimist** (Leo Kuroshita, hdavid, and the Optimist contributors), **Melodee** (Kerem Kilic),
and the public **JieLi AC79 AIoT SDK** (Zhuhai Jieli Technology Co., Ltd., Apache-2.0).

---

## Table of contents

1. [What is code (GPL-3.0)](#what-is-code-gpl-30)
2. [The update loader and OTA engine (MPL-2.0)](#the-update-loader-and-ota-engine-mpl-20)
3. [The Bluetooth LE stack (GPL-3.0-only OR MPL-2.0)](#the-bluetooth-le-stack-gpl-30-only-or-mpl-20)
4. [Hardware configuration tables & SDK data (Apache-2.0)](#hardware-configuration-tables--sdk-data-apache-20)
5. [Audio assets and dry guitar captures](#audio-assets-and-dry-guitar-captures)
6. [How we got our build working](#how-we-got-our-build-working)
7. [Third-party material table](#third-party-material-table)
8. [Contributions](#contributions)
9. [Trademarks](#trademarks)
10. [Radio notice](#radio-notice)
11. [Hardware safety & disclaimers](#hardware-safety--disclaimers)

---

## What is code (GPL-3.0)

Every original file in this repository carrying an `SPDX-License-Identifier: GPL-3.0-or-later` or `GPL-3.0-only` header is governed by the GPL:

- **The firmware application layer:** `custom-fw/src/` (audio core `pa_audio_core.c`, effect graph `fx_graph.c`, effect host `fx_andromeda.c`, the six generative/granular engines `v1_*.c`, FDN reverb `v1_fdn.c`, shimmer `v1_shimmer.c`, cosmic play screen `ui_cosmic.c`, front panel state machine `ui_app.c`, board drivers `st7735s.c`, `audio_alnk.c`, `gpio_scan.c`, `spi_nor.c`).
- **The linker scripts:** `custom-fw/app_andromeda.ld`, `custom-fw/app.ld`.
- **The host verification suites:** `custom-fw/test/` (`test_andromeda_fx.c`, `test_andromeda_smp.c`, `test_andromeda_pool.c`, `test_glass.c`, `test_bloom.c`, `test_accretion.c`, `test_choir.c`, `test_fracture.c`, `test_constellation.c`, `test_fdn.c`, `test_shimmer.c`, `test_meas.c`, `test_ui_cosmic.c`, and all regression tests).
- **The desktop emulator and tools:** `custom-fw/emu/` (native DLL harness `pocket_amp_emu.dll`, web frontend server `server.py`, `ui.html`), `custom-fw/wasm/` (WebAssembly audio core), and `custom-fw/web/` (browser web editor).
- **The build and QA pipeline:** `custom-fw/build.ps1`, `custom-fw/tools/qa_gate.py`, `custom-fw/tools/stress_effects.py`, `custom-fw/tools/effect_audibility.py`, `custom-fw/tools/andromeda_perf.py`, `custom-fw/tools/check_sources.py`.
- **Packaging utilities:** `custom-fw/tools/onyx_pkg_make.py` and `custom-fw/tools/lz4blk.py` (derived from Pendel's `tools/pa_pkg_make.py`, GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita, HÃ¼gelton Instruments).

You may study, modify, and distribute it under the terms of the GNU General Public License. If you distribute binaries built from this repository or derivative firmware, you must make the complete corresponding source code available under the same license.

---

## The update loader and OTA engine (MPL-2.0)

The update loader and device-side OTA session handler are licensed under the **Mozilla Public License 2.0** (`MPL-2.0`, full text in [custom-fw/LICENSES/MPL-2.0.txt](LICENSES/MPL-2.0.txt)):

- `custom-fw/third_party/pendel_loader/`: The complete **pendel-bsp** hardware abstraction and update loader layer (<https://github.com/hugelton/pendel-bsp>), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), HÃ¼gelton Instruments, imported unmodified with its original notice and license files.
- `custom-fw/loader/onyx_loader.c`: Andromeda's staging loader, derived from `boards/pocketamp/pa_loader.c` (MPL-2.0).
- `custom-fw/src/sys/pa_ota_session.c`: The firmware's in-app SysEx OTA receiver, derived from `chip/ota.c` (MPL-2.0).

**License compatibility:** The pendel-bsp files carry no "Incompatible With Secondary Licenses" notice. Under MPL-2.0 Section 3.3, MPL-2.0 files may be combined in a Larger Work with GPL-3.0 code while each individual MPL file retains its file-level MPL-2.0 licensing.

---

## The Bluetooth LE stack (GPL-3.0-only OR MPL-2.0)

The low-level Bluetooth Low Energy MIDI stack for the AC791N / WL82 integrated radio in `custom-fw/third_party/optimist_ble/` is imported from **Optimist** (<https://github.com/w0ts/optimist>, commit `fcdc4881e8788365c62ea8be7fa11b4d82966bac`, pinned in `UPSTREAM-COMMIT.txt`):

- **Authorship:** Authored for Optimist by **hdavid**; Optimist is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), HÃ¼gelton Instruments, and the Optimist contributors.
- **Licensing choice:** Offered under either `GPL-3.0-only OR MPL-2.0`, at the recipient's choice. Andromeda consumes the files under the **MPL-2.0** option.
- **Scope of import:** Link layer, baseband hardware driver, security manager, and framing (`src/ble_hw_wl82.c`, `src/ble_ll.c`, `src/ble_smp.c`, `hal/fm1_ble.h`, `hal/fm1_ble_rf.h`, and tests in `tests/`).
- **Marked modifications (`PA-MOD`):** To ensure total auditability against upstream, all modifications in `custom-fw/third_party/optimist_ble/` are explicitly tagged with `/* PA-MOD: ... */`:
  - `hal/fm1_ble_rf.h`: Wrapped `FM1_VCO_EDGE_US` and `FM1_VCO_SETTLE_US` in `#ifndef` to allow run-time tuning; added optional hooks `FM1_VCO_STOCK_SCAN()` and `FM1_LUT_W1()`.
  - `src/ble_ll.c`: Added `CONNECTION_UPDATE_IND`, `CHANNEL_MAP_IND`, `TERMINATE_IND`, and param requests to diagnostic event logging.
  - `src/ble_ll.c`: Added instant tolerance for `LL_CHANNEL_MAP_IND` and `LL_CONNECTION_UPDATE_IND` (if an instant passed by < 64 events due to Windows Bluetooth scheduler latency, apply two events ahead rather than terminating the link with error `0x28`).
  - `src/ble_hw_wl82.c`: Added `pa_ble_static_addr()` call to generate a deterministic, static random address derived from hardware calibration words so the pedal retains its Bluetooth identity across reboots.
  - `src/ble_hw_wl82.c`: Implemented software channel remapping (`PA_BLE_SOFT_REMAP`) to overcome hardware baseband packet loss on sparse Windows channel maps (measured 99.7% capture vs 6.7% native baseband).
  - `src/ble_hw_wl82.c`: Added `HW_WIDEN_X` multiplier to adjust the connection receive window dynamically.

---

## Hardware configuration tables & SDK data (Apache-2.0)

- **Radio configuration parameters:** `custom-fw/third_party/optimist_ble/hal/ble_rf_tables_v15.h` contains hardware radio register initialization sequences and lookup tables. As documented by Optimist, these values originate from JieLi (Zhuhai Jieli Technology Co., Ltd.) and M-VAVE under the **Apache-2.0** license (`SPDX-License-Identifier: Apache-2.0`, text in [custom-fw/LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt)). They represent factual hardware configuration parameters, not executable code.
- **Boot and EQ configuration binaries:** `uboot.boot`, `cfg_tool.bin`, and `cfg/eq_cfg_hw.bin` are fetched on demand from the official public JieLi AC79 repository (`https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK`, commit `5fa84a9b2f723a5e9337ad79dc2ad741fc98fe2d`, Apache-2.0) by `custom-fw/tools/get_sdk_files.py`. They are SHA-256 validated and packaged into the flash container as separate data partitions. **They are not stored in git and no proprietary SDK C sources are linked into the firmware.**

---

## Audio assets and dry guitar captures

The native desktop emulator (`custom-fw/emu/`) and WebAssembly test harness include clean direct-input (DI) electric guitar waveforms used for interactive front-panel auditioning, automated effect audibility measurements, dynamic range validation, and DSP regression testing:

- **Dry Guitar Captures (`jh_*.wav`):** Original, self-captured dry electric guitar direct input (DI) recordings (chords 1â€“5, lead licks 1â€“5, rhythm riffs 1â€“5) in `custom-fw/emu/samples/`, courtesy of **Jonas Hemmersbach**. Provided and used with permission.
- **Synthesised DI Phrases (`di_*.wav`):** Procedural plucked-string simulation model phrases generated on the host by `custom-fw/tools/gen_test_audio.py`.

---

## How we got our build working

Porting a modern dual-core audio DSP environment to the JieLi AC791N SoC (pi32v2 architecture) on a Windows host revealed severe challenges across toolchain idiosyncrasies, linker quirks, memory layouts, and execution environments. Below is the technical breakdown of the issues discovered and how our build system was engineered to solve them:

### 1. Fail-fast error propagation in PowerShell (`Invoke-Checked`)
- **The problem:** In Windows PowerShell, `$ErrorActionPreference = "Stop"` applies exclusively to internal PowerShell cmdlets. When invoking external native executables (`clang.exe`, `pi32v2-ld.exe`, `python.exe`, or compiled host test binaries), a non-zero exit code does not trigger a PowerShell exception. Consequently, earlier build scripts silently ignored failing unit tests or compilation errors, proceeding straight to container generation and outputting a deceptive, freshly dated `.fwsc` firmware package built from whichever stale object files happened to be on disk.
- **The solution:** Implemented a mandatory wrapper function `Invoke-Checked` in `custom-fw/build.ps1`:
  ```powershell
  function Invoke-Checked {
      param([string]$What, [scriptblock]$Cmd)
      & $Cmd
      if ($LASTEXITCODE -ne 0) {
          throw ("{0} FAILED (exit {1}) - the build stops here, and no image is written." -f $What, $LASTEXITCODE)
      }
  }
  ```
  Every single compile step, host test execution, linking step, and packaging script is wrapped in `Invoke-Checked`. Flashable binary outputs (`Pocket_AMP_Andromeda.bin`, `.ufw`, `.fwsc`) are purged at the very start of the script and are written *only* when the pipeline passes 100% of its verification stages.

### 2. Eliminating stale compilation artifacts
- **The problem:** When switching between targets (e.g. Onyx amp-modelling vs Andromeda granular) or changing header macros, stale object files in the build cache led to undefined symbol combinations and phantom memory bugs.
- **The solution:** `custom-fw/build_pi32` is deleted and recreated on every build invocation. Object files are regenerated fresh from source, preventing stale dependencies from ever entering the linker.

### 3. Source manifest drift guard (`sources.txt`)
- **The problem:** Different sub-projects (the device firmware, host test runners, native emulator DLL, and WebAssembly engine) originally maintained their own hand-edited lists of C files. Adding a new DSP file inevitably resulted in silent omission from one or more targets (the emulator broke three separate times due to forgotten source entries).
- **The solution:** Centralized module declaration into a single manifest: `custom-fw/sources.txt`. `tools/Read-Sources.ps1` dynamically parses this manifest for PowerShell, and `custom-fw/tools/check_sources.py` runs as stage `[1/5]` of the build, immediately aborting the build if any source file in `custom-fw/src/` is untracked or duplicated.

### 4. Circular toolchain library dependencies (`--start-group`)
- **The problem:** The JieLi compiler toolchain uses `pi32v2-ld`. When compiling floating-point intensive DSP modules (such as `tanhf()` or transcendental functions in the effect engines), `libm.a` internally invokes soft-float helper routines (such as `__subdf3`). These helpers are implemented in `libcompiler_rt.a`. In standard linear linker order, scanning `libcompiler_rt.a` before `libm.a` failed because the symbol had not yet been referenced; scanning `libm.a` before `libcompiler_rt.a` failed on reverse references, resulting in `undefined reference to __subdf3`.
- **The solution:** Grouped the runtime libraries together using circular resolution syntax:
  ```text
  pi32v2-ld.exe -T app_andromeda.ld --gc-sections $objs --start-group "$pi32Lib\libcompiler_rt.a" "$pi32Lib\libm.a" "$pi32Lib\libc.a" --end-group
  ```

### 5. Flash image section displacement & RAM-code packing
- **The problem:** The JieLi bootloader copies initialized data (`.data`) from flash into SRAM based on linker load addresses (`_data_load`). Previously, firmware packaging scripts naively concatenated `.text` + `.data`. However, the linker script places critical RAM-executed functions (`.ram_text`, `.c1_text`, `.ramfn`, and `.flashfn`) between `.text` and `.data` in the load map. Blind concatenation placed `.data` 48 bytes too early in the flash image. At runtime, the startup code copied bytes from the shifted offset, resulting in every global variable and factory lookup table being corrupted by 48 bytes (e.g. amp models and cabinet IR tables read off-by-one or off-by-six, and the final structures read past EOF into uninitialized memory). Furthermore, `.flashfn` (the SPI flash controller probe) was completely missing from the binary, causing the CPU to copy zeroes into SRAM and jump into an immediate crash.
- **The solution:** Re-architected binary image construction to follow the linker symbol map. `build.ps1` uses `llvm-nm.exe` to parse the exact load addresses (`_rt_load`, `_c1_load`, `_rf_load`, `_ff_load`, `_data_load`), extracts each section via `llvm-objcopy -O=binary`, and copies each into a unified byte buffer positioned strictly at `(symbol_load_address - 0x02000120)`. The script includes explicit assertion gates verifying that `.flashfn` is non-zero, does not collide with `.data`, and matches the symbol map to the byte.

### 6. Object file name collisions in flat build directories
- **The problem:** Compiling a multi-directory tree (`src/board/`, `src/dsp/`, `src/sys/`, `src/fx/`) into a single build directory caused files sharing the same name (such as `main.c` or driver stubs) to overwrite each other's `.o` files.
- **The solution:** Object filenames are derived deterministically from their relative source paths by replacing directory separators with underscores (e.g. `src/board/main.c` becomes `_board_main.c.o`).

### 7. Dual-core SMP concurrency and execution safety
- **The problem:** Core 1 shares the 512 KB SRAM and executes audio DSP jobs asynchronously. If Core 1 encounters an unaligned access, memory bus contention, or fails to complete its assigned DSP block within the 1.45 ms audio deadline, the pedal would lock up or emit digital noise.
- **The solution:** Created a measured-cost allocator in `fx_graph.c` and a hardened SMP worker in `smp_cpu1.c`. An exponential moving average tracks the cycle execution cost of every active DSP block. Chains that fit within 0.9 ms stay entirely on Core 0 with zero added latency. Heavy chains dynamically split the load, moving early slots to Core 1 at the cost of exactly one block (1.45 ms) of buffer latency. If Core 1 misses a block deadline 64 consecutive times, encounters a stack breach, or fails to acknowledge an IPC job, it is automatically fenced off for the rest of the session, falling back gracefully to Core 0.

### 8. USB bulk transfer terminal chunking
- **The problem:** During SysEx diagnostic transfers and memory dumps, reads that happened to be exact multiples of 64 bytes (such as 32-byte or 240-byte block boundaries) caused Windows host USB drivers to hang indefinitely awaiting a short packet or zero-length packet (ZLP).
- **The solution:** Implemented strict packetizing logic in `usb_ble.c` ensuring no bulk transfer terminates on an unclosed 64-byte boundary, guarded by an automated host verification suite (`pa_test_usb_chunk.c`).

### 9. Flash write fencing and power-loss safety
- **The problem:** Flashing a corrupted image or losing power during a preset save could overwrite the mask ROM boot vector or partition headers (`0x000000..0x003FFF`), bricking the unit.
- **The solution:** 
  - Enforced strict hardware write fencing in `spi_nor.c` and flashing tools: addresses `< 0x004000` are blocked in hardware and rejected by software tools unless explicitly overridden in an emergency.
  - Implemented an A/B double-buffering scheme for preset and settings storage at `0x086000`, validated by automated regression suites (`test_robustness.c`) simulating torn writes across simulated power drops.

### 10. Vendor-free release packaging
- **The problem:** Distributing flashable firmware updates without illegally bundling proprietary M-VAVE software or closed vendor development kits.
- **The solution:** Adopted the open container structure from the Pendel project (`tools/onyx_pkg_make.py`), packaging only the newly compiled application binary, the MPL-2.0 open-source update loader, and public Apache-2.0 JieLi SDK configuration blobs fetched directly from upstream repositories and validated by cryptographic SHA-256 digests.

---

## Third-party material table

| Component / Material | Original author / upstream | License | Path in repository | Notes / Provenance |
|---|---|---|---|---|
| **Optimist BLE Stack** | hdavid, Leo Kuroshita (@kurogedelic), HÃ¼gelton Instruments, Optimist contributors (<https://github.com/w0ts/optimist>) | GPL-3.0-only OR MPL-2.0 | `custom-fw/third_party/optimist_ble/` | Pinned at commit `fcdc4881e8788365c62ea8be7fa11b4d82966bac`. Consumed under MPL-2.0. Contains marked `PA-MOD` modifications for static MAC address derivation, Windows late instant tolerance, and soft channel remapping. |
| **Radio initialization tables** | Zhuhai Jieli Technology Co., Ltd. & M-VAVE | Apache-2.0 | `custom-fw/third_party/optimist_ble/hal/ble_rf_tables_v15.h` | Factual RF hardware register constants and lookup tables captured from stock V15 environment; no proprietary code. |
| **pendel-bsp (Loader & HAL)** | Leo Kuroshita (@kurogedelic), HÃ¼gelton Instruments (<https://github.com/hugelton/pendel-bsp>) | MPL-2.0 | `custom-fw/third_party/pendel_loader/` | Hardware abstraction, crt0, and update loader. Carries no secondary license restrictions; compatible with GPL-3.0 under MPL-2.0 Section 3.3. |
| **Onyx update loader** | Leo Kuroshita, adapted for Pocket AMP | MPL-2.0 | `custom-fw/loader/onyx_loader.c` | Derived from `boards/pocketamp/pa_loader.c` in pendel-bsp. |
| **In-app OTA session** | Leo Kuroshita, adapted for Andromeda | MPL-2.0 | `custom-fw/src/sys/pa_ota_session.c` | Derived from `chip/ota.c` in pendel-bsp. |
| **USB MIDI host & SMP code** | Adrien Tibi (Recursive Field Instruments), Kerem Kilic (Ellic Studio) (<https://github.com/keremimo/melodee>) | MPL-2.0 | `custom-fw/third_party/pendel_loader/chip/usb_host.c`, `fm1_cpu1.*` | Relicensed from Melodee to MPL-2.0 by permission of authors in Felucca-BSP PR #3, Issue #5, and Melodee PR #75. |
| **Package builder & LZ4 tools** | Leo Kuroshita, HÃ¼gelton Instruments (<https://github.com/hugelton/Pendel>) | GPL-3.0-only | `custom-fw/tools/onyx_pkg_make.py`, `custom-fw/tools/lz4blk.py` | Adapted from Pendel's `tools/pa_pkg_make.py` for vendor-free release containers. |
| **JieLi AC79 AIoT SDK binaries** | Zhuhai Jieli Technology Co., Ltd. (<https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK>) | Apache-2.0 | `custom-fw/third_party/jieli_sdk_files/` (`uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`) | Fetched on demand by `custom-fw/tools/get_sdk_files.py` from commit `5fa84a9b2f723a5e9337ad79dc2ad741fc98fe2d`. SHA-256 verified; not stored in git. |
| **Dry Guitar Captures** | Jonas Hemmersbach | Courtesy of Jonas Hemmersbach (Used with permission) | `custom-fw/emu/samples/` (`jh_*.wav`) | Clean direct input (DI) electric guitar recordings (chords 1-5, licks 1-5, riffs 1-5) used in emulator and workbench for auditioning, level checks, and regression tests. |
| **BLE-MIDI client reference** | flutter_midi_command authors (pub.dev) | MIT / BSD-3-Clause | Reference only | Used as clean-room specification reference for standard MIDI service UUIDs and packet framing. |

---

## Contributions

Contributions to Andromeda are welcomed under the **GNU General Public License v3.0 or later** (`GPL-3.0-or-later`).
By submitting a contribution, you agree that your code will be licensed under these terms.

Contributions intended for files within `custom-fw/third_party/optimist_ble/` or `custom-fw/third_party/pendel_loader/` must adhere to their respective upstream licensing terms (`GPL-3.0-only OR MPL-2.0` or `MPL-2.0`).

---

## Trademarks

- "M-VAVE" and "Pocket AMP" are trademarks or trade names of Zhuhai Shengke Electronics Co., Ltd.
- "JieLi" is a trademark of Zhuhai Jieli Technology Co., Ltd.
- "Roland", "Fender", "Marshall", "Vox", "Celestion", and other musical equipment manufacturer names appearing in documentation, test scripts, or model identifiers are property of their respective owners and are used purely for historical, descriptive, and sonic reference.

Andromeda is an independent, community-developed research project. It is **not** affiliated with, endorsed by, or supported by Zhuhai Shengke, M-VAVE, Donner, or Zhuhai Jieli Technology.

---

## Radio notice

Standard builds of Andromeda do not broadcast or receive RF signals unless the Bluetooth LE switch is specifically enabled. When BLE MIDI is enabled, the radio hardware operates using startup configuration data derived under Apache-2.0 from public register specifications. Wi-Fi functionality is deliberately held disabled and unpowered.

---

## Hardware Safety & Disclaimers

> [!WARNING]
> Flashing custom firmware involves writing to the physical SPI-NOR flash memory of your device. While Andromeda enforces rigorous partition write protection (`pa_flash_rw_ok()`) and includes automated recovery mechanisms, flashing firmware is undertaken at your own risk. The authors and contributors accept no liability for hardware damage, warranty invalidation, or data loss. Ensure your device battery is adequately charged before initiating firmware flashing operations.
