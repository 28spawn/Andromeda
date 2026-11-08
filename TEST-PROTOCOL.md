# Andromeda test protocol and QA gate

> **Safety principle:** no unit is flashed unless the automated gate reports `READY FOR BENCH TEST`, a verified full-chip backup is recorded, and a recovery route has been proven by a real restore ([RECOVERY.md](RECOVERY.md)). A passing gate is not a hardware approval: the bench tests below decide that.

## 1. Running the checks

```powershell
# Build the Andromeda firmware: runs the host suites, links, packages, checks the image
powershell -ExecutionPolicy Bypass -File custom-fw/build.ps1 -Product andromeda

# The full gate (static, build, unit, functional, image, hardware evidence)
python custom-fw/tools/qa_gate.py

# Parts of it
python custom-fw/tools/qa_gate.py --only static
python custom-fw/tools/qa_gate.py --only image
python custom-fw/tools/qa_gate.py --skip-build        # reuse the last build (the emulator DLL must be fresh too)
```

The gate writes `custom-fw/qa/reports/qa-<timestamp>.md` (and `latest.md`).

| Verdict | Exit code | Meaning | Permitted action |
|---|---|---|---|
| **NO-GO** | 1 | An automated check or recorded hardware result failed | Blocked. Fix it before flashing. |
| **FLASH BLOCKED** | 2 | Automated checks pass, but recovery readiness is unproven | Do not flash. Back up and prove a recovery route first. |
| **READY FOR BENCH TEST** | 0 | All automated checks pass and recovery is verified | Flash the bench unit and run section 3. |
| **RELEASE GO** | 0 | The above, and this image's hash has a recorded bench PASS | Release. |

## 2. What the gate checks

| Stage | What it covers |
|---|---|
| **S** (static) | The source manifest matches every build target; build inputs are tracked in git; no oversized files or personal paths; no peripheral register defined twice; no unbounded waits in interrupt paths; the flash partitions fit the chip and never overlap the boot area; the flash driver is wired through its write fence. |
| **U** (unit) | Host C suites. For Andromeda: `test_andromeda_fx` (every effect type at full MIX: audible, with headroom, never at the clamp), `test_andromeda_smp` (the two-core plan and that every cut keeps the sound), `test_andromeda_pool`, the engine suites `test_glass`, `test_bloom`, `test_accretion`, `test_choir`, `test_fracture`, `test_constellation`, `test_fdn`, `test_shimmer`, the measurement channel `test_meas`, and the screen `test_ui_cosmic`. Shared suites: BLE MIDI, SysEx, USB MIDI and descriptors, reverb, dynamics, EQ, modulation, the flash guard. |
| **F** (functional) | The emulator and WebAssembly build driven end to end: UI flows, button and encoder decoding, the editor protocol, SysEx upload, MIDI control, USB mode, battery, level, block-size independence, click-free switching, and the browser workbench tests. `test_andromeda_perf.py` decodes the firmware's own measurement records. |
| **I** (image) | Artifacts are newer than the sources; the app fits its partition; the binary is the linked ELF; the container's header and CRCs; the boot area is untouched; section sizes are within 5 % of the baseline. |
| **R** (recovery) | The hardware ledger (`custom-fw/qa/hw_ledger.json`) has a full-chip backup (two identical dumps, hash recorded), a recovery route proven by a real restore, and a restore drill younger than 90 days. |
| **H / HW** | Bench results recorded per image hash; see section 3. |

The measurement scripts are separate from the gate: `custom-fw/tools/andromeda_perf.py` (CPU and memory on the unit) and `custom-fw/tools/effect_audibility.py` (loudness of each effect against a dry lick on the emulator).

## 3. Bench verification (per candidate)

When the gate reports `READY FOR BENCH TEST`, flash the bench unit ([docs/ANDROMEDA-FLASHING.md](docs/ANDROMEDA-FLASHING.md)) and run the first-boot checklist there. In short:

- [ ] Flash completes and the flashed SHA-256 matches the candidate; cold boot to the play screen, 10 of 10 power cycles.
- [ ] Settings > System shows ANDROMEDA and the expected version and build code.
- [ ] Every effect (P01 to P12, and the 24 types from the chain page) is audible on and bypasses cleanly: GLASS rings on a pick, BLOOM grows from the history, ACCRETION records, plays and overdubs, CHOIR holds a chord, FRACTURE follows the BPM, CONSTELLATION plays a sequence from a chord.
- [ ] Every key, the key combinations and the encoder do the documented action.
- [ ] SPACE and SHIMMER, the GLASS layer, and saving to slots P13 to P20.
- [ ] Switching slots while playing: no click, no hang.
- [ ] The tuner reads within 2 cents at 440 Hz and its mute works.
- [ ] Output safety: maximum gain with a hot input stays under the limiter ceiling.
- [ ] Display: no garbage or offset; brightness steps work.
- [ ] USB MIDI enumerates; BLE MIDI links on each host in use; the MIDI map ([custom-fw/MIDI-MAP-CUSTOM.md](custom-fw/MIDI-MAP-CUSTOM.md)) behaves as documented.
- [ ] 30 minutes idle and a 60 minute run on battery: no reset, no hang, plausible temperature.
- [ ] Power cut during a save, 20 times: the store stays valid.
- [ ] CPU: `andromeda_perf.py matrix` shows no scenario over the block budget (1.451 ms) and core 1 never late.
- [ ] Recovery drill: restore from the backup by the proven route, then re-flash.

Record the results against the image hash with `python custom-fw/tools/qa_recovery.py hw-run ...` (see `--help`); items that are not recorded stay `INFO` in the gate report.
