# Andromeda status and roadmap

What is built, what has been measured on the unit, and what is left. Design: [ANDROMEDA-DESIGN.md](ANDROMEDA-DESIGN.md). Measuring on the unit: [ANDROMEDA-PERF.md](ANDROMEDA-PERF.md).

## 1. Built (release 0.10)

| Area | State |
|---|---|
| Effects | GLASS, BLOOM, ACCRETION, CHOIR, FRACTURE, CONSTELLATION: 24 types, each with a host test. |
| SPACE and SHIMMER | The shared FDN reverb and the shimmer path (off, +12, +7, +12 and +7, +19). |
| LAYER | A second GLASS in the chain. |
| Presets | 20 slots: P01 to P12 factory chains (two per effect), P13 to P20 empty and saveable from the chain page. |
| Screen | The radar-style play screen, the signal chain page, settings, performance, tuner. |
| Two cores | A measured-cost allocator splits the chain across both cores when it would not fit on one (docs/ANDROMEDA-PERF.md). |
| Measurement | The private diagnostic channel, `andromeda_perf.py` (`info`, `watch`, `matrix`, `ctl`). |
| Calibration | Every effect sits within about -2 and +4 dB of a dry lead (`effect_audibility.py`). |
| Tests | Host suites for each engine, the host and the allocator, the pool, the screen and the measurement protocol; the QA gate passes (READY FOR BENCH TEST). |

## 2. Measured on the unit

From the scenario matrix on a bench unit: a baseline of about 0.2 ms per block (14 % of the 1.451 ms budget) for the fixed work; no scenario over the budget with the allocator on; core 1 never late; the worst case about 66 to 68 % of the budget. CHOIR spikes on core 1 at about 1.4 to 1.7 ms before the cheaper alignment search; that change and the allocator's final thresholds (900 / 780 µs) are not yet re-measured.

## 3. Not yet verified on the unit

- The faster CHOIR alignment search and the latest allocator thresholds.
- Glitches when a slot change moves a node between cores, and when a factory reset or an update borrows the overlay while core 1 is mid-block.
- The one-block spike when an effect is started (the overlay is cleared on the audio thread).
- The effect level calibration by ear (it was set on the emulator against a dry lick).
- The DSP pool's "peak" readout, which reports more than the pool's size and is unexplained.
- Listening checks of the ACCRETION tone and saturation on playback.

## 4. Not built

| Item | Notes |
|---|---|
| SPECTRAL FREEZE | Would use a software FFT; the hardware FFT mode is not mapped. |
| MIDI clock sync | FRACTURE and CONSTELLATION follow the unit's BPM setting. |
| BLOOM and ACCRETION together | The two effects share one overlay, so they cannot run at once; a second buffer would not fit in the 47 KB of free `.pool` memory. |
| More shimmer intervals | Only the five combinations listed above. |

## 5. Constraints that decide future work

- SRAM is the limit: about 47 KB of the `.pool` region is free, and the effect overlay is 147 KB.
- Block budget: 1.451 ms per 64 samples, shared with the codec, display and USB work (about 0.2 ms).
- A slot cannot be split across the two cores, so one effect must fit on a single core (about 1.35 ms on core 1).
- Splitting the chain adds one block (1.45 ms) of latency; the allocator only does it when needed.
