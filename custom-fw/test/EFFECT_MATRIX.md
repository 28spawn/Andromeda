# The effect matrix: how the chain is stress-tested

The method behind `custom-fw/tools/stress_effects.py`. It exists so the same test can be re-run after any change to the DSP, and so a failure can be understood rather than guessed at. `build.ps1 -Product andromeda` runs it as step 3, with `--andromeda`.

```
python custom-fw/tools/stress_effects.py --dll custom-fw/emu/andromeda_emu.dll --andromeda
python custom-fw/tools/stress_effects.py --dll custom-fw/emu/andromeda_emu.dll --types --andromeda    # every type of every block
python custom-fw/tools/stress_effects.py --quick --andromeda                                         # only the single-effect cases
```

Exit code 0 means every case passed.

## What runs

The test drives the **real firmware** through the emulator DLL (`custom-fw/emu/andromeda_emu.dll`, built with `emu/build_dll.ps1 -Product andromeda`): the device's audio path, so what is measured is what the device would produce. Each case configures a set of blocks, an order and a knob setting, feeds a signal (a sine, a synthetic pluck, a recording) and measures the output.

`--andromeda` tells the test about Andromeda's chain: the MOD slot is the GLASS layer (it rings from attacks and adds nothing to a steady sine), and the checks that belong to blocks Andromeda does not use are skipped.

## What is checked

**Sanity** (every case): no NaN or silence below -60 dB, no more than 24 dB of gain, no more than 40 % of the samples at the rail, no DC offset above 2 % of the peak, no runaway growth relative to the input's own trend, and a mono input stays mono (left and right equal within 2 %).

**Behaviour** (each block does its job):
- the dynamics block's gate closes on a quiet signal;
- the EQ set flat is flat across the band;
- the delay and the reverb leave a tail;
- a block keeps its settings when it is bypassed and switched on again.

**Order**: the cases are run in different slot orders, because an order that only misbehaves in one arrangement is the kind of bug a single fixed order hides.

The master volume is backed off by 20 dB during the matrix so that the criterion "no more than 40 % of samples at the rail" measures the effects and not the output stage's overload handling; the level law and the output stage are measured on their own in `custom-fw/emu/test_level.py`.

## What it does not check

How loud and how audible each Andromeda effect is, whether the two-core split keeps the sound, and whether the allocator behaves are covered by other tests:

- `test/test_andromeda_fx.c`: every effect type at full MIX is audible, with headroom, and never at the clamp;
- `test/test_andromeda_smp.c`: the allocator's plan and that every cut keeps the sound;
- `tools/effect_audibility.py`: each effect's level against a dry lick, on the emulator;
- `tools/andromeda_perf.py matrix`: CPU, memory and core load on the unit.
