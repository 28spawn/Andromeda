# Andromeda Desktop Emulator (`custom-fw/emu`)

The emulator runs the **real Andromeda firmware** — the same `pa_audio_core_tick()`, the same input processing layer, the exact dual-core DSP and effect engines — compiled into a native 64-bit Windows dynamic library (`andromeda_emu.dll`, with backward-compatible `pocket_amp_emu.dll`). The browser workbench (`custom-fw/wasm`) runs the identical engine in WebAssembly.

```powershell
powershell -ExecutionPolicy Bypass -File custom-fw\emu\run_emu.ps1
```

Compiles the DLL, initializes the ASIO audio host, and serves the interactive HTML5 front panel at <http://localhost:8080>. Connected ASIO audio interfaces (or WASAPI / DirectSound devices via sounddevice) are detected automatically; in absence of physical hardware, the emulator runs safely in offline simulation mode.

---

## Front-Panel Controls & Gestures

The firmware's input layer (`src/sys/pa_input.c`) decodes front-panel physical controls into deterministic action events:

* **Instant buttons (`VOL-`, `VOL+`, `<`, `>`):** Fire on the **press edge** and auto-repeat after 350 ms while held.
* **Rotary Push-Encoder (`PARA`) & `HOME`:** Fire their click action on the **release edge** — a quick press is a click, while a prolonged press is evaluated as a hold.
* **Hold thresholds:**
  * `PARA` held 1.2s $\to$ Cosmic Preset Save Dialog (`PARA_HOLD_MS = 1200`)
  * `PARA` held 3.0s $\to$ Long Tape Buffer Mode toggle (5.94s extended memory)
  * `HOME` held 1.0s $\to$ System Settings Menu
  * `HOME` held 1.5s $\to$ Infinite Buffer Freeze Drone Pad
  * `<` held 0.8s $\to$ Accretion tape reset / clear
  * `VOL-` / `VOL+` held 0.8s $\to$ Preset stepping from within block pages
* **Chords:** Multi-button chords (e.g. `[VOL- + VOL+]` for Chromatic Tuner HUD) atomically consume the participating keys to suppress false individual clicks.

---

## Front-Panel Emulator API

| Call | What it does |
|---|---|
| `emu_ui_tap(mask, hold_ms)` | Executes a complete atomic gesture: press, hold duration, release. |
| `emu_ui_press(mask)` | Depresses button mask (used for interactive mouse-down holds). |
| `emu_ui_release(mask)` | Releases button mask (enforces debounce minimums to guarantee detection). |
| `emu_ui_enc(delta)` | Rotates the PARA rotary encoder (positive clockwise, negative counter-clockwise). |
| `emu_ui_pot(q12)` | Adjusts the MASTER volume pot (0 to 4095, mapping to -40 dB .. 0 dB taper). |
| `emu_ui_input(mask, enc, pot)` | Low-level level setter driven by automated test harnesses. |

### REST & WebSocket API (`POST /api/input`):

```jsonc
{"tap": 1}                  // Tap HOME (switches between Radar HUD and Signal Chain)
{"hold": 32, "ms": 1200}    // Hold PARA for 1.2s (opens Preset Save Dialog)
{"enc_delta": 1}            // Turn PARA clockwise
{"master_pot": 4095}        // Set MASTER volume to 100%
```

---

## Front-Panel UI Elements

| Control | Function in Andromeda |
|---|---|
| **Celestial Radar HUD** | Circular radar plotting live engine telemetry, voice activity, tape playheads, and star clusters. |
| **`[Tuner]` Button** | Chords `[VOL- + VOL+]` to enter the chromatic tuner HUD (< 2 cents accuracy). |
| **`[Settings (1.1s)]`** | Long-presses `HOME` to access system settings. |
| **`[Clear (0.8s)]`** | Clears the active engine state (erases Accretion tape, clears grain history, dampens rings). |
| **`[PARA Click]`** | Cycles active macro coordinate: `ORBIT` $\to$ `WARP` $\to$ `FLUX` $\to$ `GRAV`. |
| **`[◀]` (Reverse)** | Fires Reverse Grain Burst or triggers Accretion tape record. |
| **`[▶]` (SOS Looper)**| Cycles phrase looper state: `STOPPED` $\to$ `REC` $\to$ `PLAY` $\to$ `OVERDUB`. |
| **`[Save (1.2s)]`** | Launches preset save dialog to store user chain into P13–P20. |
| **`[BT State]`** | Toggles simulated Bluetooth LE advertising state. |
| **Pill Bar Badges** | `FREEZE (HOLD 1.5s)`, `PRESET - (DECREMENT)`, `PRESET + (INCREMENT)`, `REVERSE (BURST)`, `SOS LOOP (REC/PLAY/DUB)`. |

---

## Verification Test Suites

```powershell
# Andromeda navigation and gesture regression:
python custom-fw\emu\test_andromeda_nav.py

# Complete 11-gate Andromeda UI visual protocol:
python custom-fw\emu\test_andromeda_ui.py

# Input debouncing and button timing verification:
python custom-fw\emu\test_buttons.py
python custom-fw\emu\test_input.py

# Audio levels and master taper verification:
python custom-fw\emu\test_level.py

# Web MIDI editor protocol verification:
python custom-fw\emu\test_editor.py
```
