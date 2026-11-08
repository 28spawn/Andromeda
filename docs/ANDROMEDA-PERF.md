# Measuring Andromeda on the unit

A measurement channel in the firmware and a PC tool that reads it over USB MIDI: processing time in milliseconds (the whole audio interrupt and each stage of the chain), SRAM use (static and dynamic, with what is free), a built-in test source so nobody has to play, and a scenario matrix that runs every effect type with the layer and SPACE on and off.

Status: the firmware side, the core allocator and the tool are tested on the PC (`test/test_meas.c` drives the real `pa_diag.c` framing, `test/test_andromeda_perf.py` decodes the firmware's own records and runs the tool against a fake unit). **Nothing has run against the real unit yet**: the first flash is the first real measurement.

## Setup

- Flash the build (`docs/ANDROMEDA-FLASHING.md`). Plug the unit in by USB data cable.
- `pip install python-rtmidi`
- Close the web editor and anything else that holds the unit's MIDI port. The unit shows up as "Custom MIDI" (the tool's default `--port`).

## Commands

```
python custom-fw/tools/andromeda_perf.py info
python custom-fw/tools/andromeda_perf.py watch --csv watch.csv
python custom-fw/tools/andromeda_perf.py matrix --csv matrix.csv --md matrix.md
python custom-fw/tools/andromeda_perf.py matrix --full
python custom-fw/tools/andromeda_perf.py ctl source pluck 40
python custom-fw/tools/andromeda_perf.py ctl effect 8
python custom-fw/tools/andromeda_perf.py ctl block 1 on
python custom-fw/tools/andromeda_perf.py ctl load 6 --one-based
python custom-fw/tools/andromeda_perf.py ctl reset
```

| Command | What it does |
|---|---|
| `info` | One reading: slot, effect, view, the audio interrupt's mean / max / average time, every stage's time, and the SRAM split. |
| `watch` | A line a second (and a CSV) until Ctrl-C. Play, change slots, open pages: it shows what each costs. |
| `matrix` | The scenario matrix. The quick run is about 29 scenarios (about 4 minutes): a baseline with every block off, SPACE alone and with SHIMMER, two variants of each of the six effects (GLASS, BLOOM, ACCRETION playing and overdubbing, CHOIR, FRACTURE, CONSTELLATION), the GLASS layer on each effect, SHIMMER on CHOIR and on BLOOM, and a few with SPACE off. `--full` measures all 24 effect types, each with the layer on and with SPACE off (about 83 scenarios). It prints a table, the worst case, which scenarios went over the budget, and writes CSV and Markdown. |
| `ctl` | Remote control, through the same paths as the front panel: `load N` (0-based, or the slot number as the screen shows it with `--one-based`), `effect T` (0..23: effect * 4 + variant, in the order GLASS, BLOOM, ACCRETION, CHOIR, FRACTURE, CONSTELLATION), `block B on/off` (0 effect, 1 layer, 2 space), `action A` (strike / record / freeze / clear), `param SLOT KNOB VALUE`, `source MODE LEVEL`, `reset`. |

The test source (`ctl source`) replaces the guitar: `pluck` (a note every 0.5 s), `chord` (two voices), `noise` (bursts), `sine` (220 Hz). It costs under 1 % of the block. **It switches itself off 60 s after the last command**, so a crashed test leaves the unit playable. The matrix restores the slot and turns the source off when it finishes, also on an error.

## What the numbers are

- **ISR** is the whole audio interrupt for one 64-sample block: the chain, the codec, the share of the screen and USB, everything the interrupt does. The budget is **1.451 ms** (64 frames at 44,118 Hz). *mean* and *max* run since the last reset (`ctl reset`); the *firmware average* is the unit's own moving average, the number the Performance page shows.
- **Stages** are the chain's own slots timed one by one: EFFECT is the DELAY slot, LAYER the MOD slot, SPACE the REVERB slot. Times are in 24 MHz ticks on the wire and shown in ms.
- **Baseline** (every block off) is the interrupt's fixed work. The matrix prints each scenario's *net* percent: its mean minus that baseline, as a share of the budget.
- A block over the budget is counted (`over`); the governor level shows whether the unit's load governor stepped in during the scenario.
- ACCRETION is measured playing and overdubbing, because recording and overdubbing are what use the ADPCM encoder.

## Two cores

The firmware uses both cores of the chip. A **core allocator** (`src/fx/fx_graph.c`) decides, from what each slot really takes, who runs what:

- **Cut 0**: everything runs on core 0 (the audio interrupt), like before. No extra latency. Used while the whole chain fits comfortably: it is split when core 0 alone would need more than about 0.9 ms (62 % of the block), and goes back to one core when everything fits under about 0.78 ms (the gap between the two keeps it from flip-flopping).
- **Cut N**: core 1 runs the input conditioning and the first N slots of the chain (DYN, EFFECT, EQ, LAYER, SPACE, in that order) on the current block, while core 0 runs the rest and the tail on the previous block's result. This costs **one block (1.45 ms) of extra latency**, dry and wet alike, and splits the load between the cores.
- Every 93 ms it prices each active slot from its measured time (a fast moving average, kept by whichever core ran it) and picks the cut that gives the smaller of the two cores' loads. A new cut has to win by 8 % twice in a row (going back to one core needs only the comfort figure, twice) (no flip-flopping), and a cut that would make core 1 miss its block is left at once. A change waits for core 1 to go idle and drops the block in flight (one silent block, 1.45 ms), so no slot runs twice or not at all on a block.
- A slot cannot be split over the two cores: an effect that needs more than about 1.35 ms by itself (CHOIR ANGELS at its worst) stays on core 0 and is not helped by this. That one needs a cheaper engine.
- If core 1 does not start, misses its block for 64 blocks in a row, refuses a job, or comes within 256 bytes of overflowing its stack, it is switched off for the rest of the session and everything runs on core 0 (the reason code is in the record). Hold `>` while powering on to keep the unit on one core.

`info`, `watch` and the matrix show both cores: **core 0** is the audio interrupt (mean / max, the numbers that were there before), **core 1** is its job time (mean / max, late blocks, stack depth) and the allocator's cut. A scenario counts as over the budget when core 0 passed its block or core 1 was late for its.

`pa_fx_alloc_force_cut(n)` fixes the cut for tests (`test/test_andromeda_smp.c` checks the plan and that every cut keeps the sound: bit-identical, one block later, when nothing is active behind the cut, and the same level when something is).

## SRAM

`info` and the matrix print: SRAM total (512 KB), the static parts (data, bss, the radio's bss, low RAM, RAM code, the whole `.pool` section), the `.pool` region and **how much of it is free**, the effect overlay (the buffer GLASS / BLOOM / ACCRETION share), and the DSP pool (size, in use, peak, largest free block, failed requests). The Performance page's SRAM bar now shows the static SRAM plus the pool in use out of 512 KB; it used to show only the 64 KB pool, which is why it read about 50 KB.

Expected from the last link (build before the measurement channel): data 3.6 KB, bss 78.5 KB, radio bss 8.9 KB, low RAM 13.9 KB, RAM code 3.8 KB, `.pool` 225.6 KB of its 336 KB region: **113 KB free**.

## Using it to decide BLOOM + ACCRETION

Read the matrix: if the worst effect with the layer stays well under the budget and the pool has about 113 KB free, a second large effect needs its own buffer (128 KB for BLOOM's history, 147 KB for ACCRETION's tape) and a shorter history or tape would make it fit. The matrix shows how much CPU is left for the second effect.

## The protocol (for other tools)

`include/pa_meas.h` has the byte layouts. In short: the private diagnostic channel (manufacturer ID 0x7D), frames packed 8-to-7 like the RAM read; `7D 04` timing, `7D 05` memory, `7D 06` reset, `7D 07 <op a b c32>` control; replies `84`, `85`, `86`, `87`.
