# Andromeda design

What Andromeda is, the hardware limits that shape it, how each effect works, and how it is controlled and shown. For the module map and execution model see [ARCHITECTURE.md](../ARCHITECTURE.md); for what is built and what is left see [ANDROMEDA-V1-PLAN.md](ANDROMEDA-V1-PLAN.md).

## 1. Hardware and its limits

| Resource | Reality | Consequence |
|---|---|---|
| SoC | JieLi AC791N, two pi32v2 cores, single-precision hardware FPU (`double` is not in hardware) | All DSP is `float` or fixed point; engines avoid `double` and `libm` calls in the audio loop. |
| Clock | `PA_CPU_HZ` assumes 360 MHz; the data sheet lists 320 MHz. Not measured on the unit | CPU budgets come from the measurement channel (cycles per block), not from the clock. |
| Audio | Codec at 44,118 Hz, 64-sample blocks (1.451 ms), 24-bit I/O; USB audio is rate-converted | Every time constant derives from `AND_FS` (44,118 Hz). |
| Input | One guitar channel on the left I2S slot; stereo comes only from Andromeda's processing | The history the grain effects read is mono; stereo is created on the way out. |
| SRAM | 512 KB: about 398 KB static, a 336 KB `.pool` region with a 147 KB effect overlay and about 47 KB free | **The binding limit.** One overlay is shared by the six effects, so only the playing one owns it. |
| Flash | The app slot is 531,673 bytes; the 0.11 image is about 200 KB | Room for code, no room for audio assets. |
| Display | 240 x 240 RGB565, SPI with DMA, drawn in 12-row bands from a display list | The only visual output; there are no LEDs. All feedback goes on the screen. |
| Controls | MASTER pot, one PARA push-encoder, five keys (`HOME`, `VOL-`, `VOL+`, `<`, `>`), BLE and USB MIDI | The interface is shallow and gesture driven; footswitches arrive as MIDI. |
| Hardware FFT | Only the matrix-MAC and block-FIR modes of the accelerator are mapped | A spectral effect would use a software FFT. |

## 2. The effects

Six effects, four variants each (type = effect x 4 + variant, 24 types). All share the host in `fx_andromeda.c`: one overlay, four macro knobs plus MIX, a per-effect output trim and the MIX law (both at full level at 50 %; below it the wet fades out, above it the dry does). Every effect is followed by the shared SPACE reverb and can carry a GLASS layer.

- **GLASS**: 12 tuned resonators per channel, struck by a transient detector on the input or by the keys. Six materials (crystal, bell, kalimba, metal, wood, marimba) set the partial ratios, levels and decay shape; BRIGHT tilts the partials and softens the strike; the right bank is detuned a few cents for width. A look-ahead limiter keeps a pile-up of notes under the ceiling.
- **BLOOM**: a grain cloud reading a 128 KB mono history (65,536 samples, about 1.5 s). SIZE sets the grain length (60 to 500 ms), PITCH the transposition (-12 to +24 semitones), BLOOM how long it takes to swell (up to 2 s) and DRIFT how far the playback position wanders. The variants quantise the grain pitches to a major, minor or pentatonic scale or leave them free.
- **ACCRETION**: a 2 s stereo tape stored as ADPCM. `>` records, plays and overdubs; each overdub passes the old layers through FDBK, a tone filter and optional saturation, behind a look-ahead write limiter so layers cannot clip. WOBBLE adds wow and flutter on playback; TONE and DEGRADE also colour the loop as it plays.
- **CHOIR**: pitch-shifted voices (a two-head delay line with an alignment search) that swell in over the note and hold it. VOICING sets the chord (octave, fifth, major, minor, massive, cluster), DETUNE and SPREAD the width, ATTACK the swell; the variant sets the voice count, release and formant colour.
- **FRACTURE**: tempo-synced slices cut from the history, with repeats, reverse bursts, pitch jumps and gating. GRID sets the step division, SLICE the length, CHAOS the randomness. It follows the unit's BPM setting.
- **CONSTELLATION**: a 16-step generative sequencer that plays grains of what you played at the pitches of a scale. TEMPO, SCALE, MOTION (how much the pattern mutates each cycle) and DENSITY (how many steps play); the variant picks the pattern, direction (programmed, down, pendulum, random), range and note division.

**SPACE** is a feedback delay network reverb with a **SHIMMER** path (off, +12, +7, +12 and +7, +19); its knobs are decay, size, tone and the send. **LAYER** is a second GLASS in the chain.

Levels were set against a dry synthetic lead so that each effect alone sits within about -2 and +4 dB of the dry level (`custom-fw/tools/effect_audibility.py`).

## 3. Controls

PARA turns the focused value and clicks to move the focus (four macros, MIX, SPACE); `<` and `>` are the effect's actions; `HOME` moves between a block's page and the signal chain page, and on the chain page it opens the page of the block under the cursor (EFFECT, LAYER, SPACE or SHIMMER, so every block's knobs can be tuned without changing the slot); `VOL-` and `VOL+` step through the slots; `VOL-` with `VOL+` opens the tuner. The full gesture table is in the README. On the chain page PARA picks a block, `<` and `>` change it and a click switches it on or off; the squares at the bottom are the 20 slots, and a PARA hold of 1.2 s saves the chain to the current slot.

## 4. Visual feedback

The play screen (`ui_cosmic.c`) is drawn from the telemetry the audio thread keeps for it:

- A title line with the slot number and effect name, coloured per effect (violet GLASS, cyan BLOOM, orange ACCRETION, green CHOIR, magenta FRACTURE, yellow CONSTELLATION).
- A **radar** that shows the effect's voices as orbiting planets (resonator modes, grains, choir voices, sequence steps) with the ACCRETION loop as a ring that fills while recording.
- An **effect strip**: the 12 mode bars (GLASS), the history (BLOOM, CHOIR, FRACTURE, CONSTELLATION) or the tape waveform (ACCRETION), with a marker for the playhead.
- Four **macro tiles** with the focused one outlined, MIX and SPACE bars, and a small animated line that names the behaviour (RING, GROW, SPIRAL, VOICES, SLICE, ARP).
- State flags: FROZEN, REC, DUB and the tape limiter's `GR %`.

`custom-fw/tools/render_screens.c` renders these screens with the firmware's own UI code; the README pictures come from it.

## 5. Not built

SPECTRAL FREEZE, MIDI clock sync for FRACTURE and CONSTELLATION (they use the unit's BPM), BLOOM and ACCRETION running together, and further shimmer intervals. See [ANDROMEDA-V1-PLAN.md](ANDROMEDA-V1-PLAN.md).
