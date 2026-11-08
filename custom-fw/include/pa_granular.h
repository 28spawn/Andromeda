/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* custom-fw/include/pa_granular.h - ANDROMEDA effect host: the three V1 flagships in the DELAY slot (the file keeps its old name; it was the granular engine's header).
 *
 * GLASS (a tuned resonator bank struck by the guitar), BLOOM (a grain cloud that grows from the guitar's history) and ACCRETION (a layered ADPCM tape loop that ages)
 * share ONE overlay in the .pool region: only the effect that is playing owns it, so the region is as big as the biggest of them (ACCRETION, 147 KB), not their sum.
 * An effect change is handed to the audio thread, which clears the overlay and starts the new engine at the next block.
 *
 * The 12 preset slots are 3 effects x 4 variants: preset type = effect * 4 + variant. The four macro knobs of a preset (ids 0..3) and MIX (id 4) mean different
 * things per effect (docs/ANDROMEDA-V1-PLAN.md); SPACE is the CLOUD reverb slot's mix.
 */
#ifndef PA_GRANULAR_H
#define PA_GRANULAR_H

#include <stdint.h>
#include <stdbool.h>
#include "pa_fx.h"

#define AND_FS 44118.0f                  /* the codec's real rate (48 MHz / (17 x 64)) */

enum { AND_FX_GLASS = 0, AND_FX_BLOOM = 1, AND_FX_ACCRETION = 2, AND_FX_CHOIR = 3, AND_FX_FRACTURE = 4, AND_FX_CONSTELLATION = 5, AND_FX_COUNT = 6 };
#define AND_VARIANTS 4

/* 24 effect types: 6 effects x 4 variants (type = effect * 4 + variant). The twelve factory slots use a selection of them; the chain page reaches all 24. */
typedef enum {
    GRAN_PRESET_GLASS_CRYSTAL = 0, GRAN_PRESET_GLASS_BELL, GRAN_PRESET_GLASS_KALIMBA, GRAN_PRESET_GLASS_MARIMBA,
    GRAN_PRESET_BLOOM_GARDEN,      GRAN_PRESET_BLOOM_DUSK, GRAN_PRESET_BLOOM_DRIFT,   GRAN_PRESET_BLOOM_RAW,
    GRAN_PRESET_ACC_SOS,           GRAN_PRESET_ACC_TAPE,   GRAN_PRESET_ACC_DECAY,     GRAN_PRESET_ACC_MIRROR,
    GRAN_PRESET_CHOIR_ANGELS,      GRAN_PRESET_CHOIR_MOURN, GRAN_PRESET_CHOIR_DEEP,   GRAN_PRESET_CHOIR_INFINITE,
    GRAN_PRESET_FRACT_CLOCK,       GRAN_PRESET_FRACT_WATER, GRAN_PRESET_FRACT_HEART,  GRAN_PRESET_FRACT_SKIP,
    GRAN_PRESET_CONST_STARDUST,    GRAN_PRESET_CONST_GRAVITY, GRAN_PRESET_CONST_DANCE, GRAN_PRESET_CONST_ORBIT,
    GRAN_PRESET_COUNT = 24
} gran_preset_t;
#define AND_TYPE_COUNT 24u

/* Parameter IDs (the chain's conventions, fx_graph.c): 0..3 macros, 4 MIX, 10 effect/variant ("explicit type"). FREEZE and LOOP_MODE are not recorded by the chain. */
enum {
    GRAN_PARAM_MACRO0 = 0, GRAN_PARAM_MACRO1 = 1, GRAN_PARAM_MACRO2 = 2, GRAN_PARAM_MACRO3 = 3,
    GRAN_PARAM_MIX    = 4,
    GRAN_PARAM_PRESET = 10,
    GRAN_PARAM_FREEZE = 11
};

/* What the front-panel keys do, per effect (fx_andromeda_action) */
enum {
    AND_ACT_PRIMARY = 0,   /* < : GLASS strike, BLOOM flip the reverse bias, ACCRETION record (no loop yet; ends it early while recording) or freeze (a loop is there) */
    AND_ACT_SECONDARY,     /* > : GLASS soft strike, BLOOM freeze, ACCRETION PLAY <-> DUB (REC from idle, and REC -> PLAY, still work) */
    AND_ACT_FREEZE,        /* freeze (BLOOM, ACCRETION): the measurement channel and the PARAM_FREEZE control */
    AND_ACT_CLEAR          /* < held 0.8 s (or PARA held 3 s): ACCRETION erases the tape, BLOOM clears its history, CONSTELLATION rerolls its pattern, GLASS stops ringing */
};

/* Controls of the LAYER (slot MOD): knobs 0..3 = MATERIAL, TUNE, DECAY, BRIGHT, knob 4 = its level (0 = off). The PARA click walks 0..3 effect macros, 4 MIX, 5 SPACE, 6..9 layer macros, 10 layer level. */
/* The focus axes are four pages (HOME on the signal chain page opens the block's page): 0..5 the EFFECT (4 macros, MIX, SPACE send), 6..10 the LAYER (4 macros, level),
 * 11..14 SPACE (reverb DECAY, SIZE, TONE, SEND = reverb knobs 0..3), 15..18 SHIMMER (AMOUNT = reverb knob 4, INTERVAL = knob 5, DECAY = knob 0, SEND = knob 3). The PARA click walks the page it is on. */
#define AND_FOCUS_COUNT 19
#define AND_FOCUS_LAYER 6
#define AND_FOCUS_SPACE 11
#define AND_FOCUS_SHIMMER 15

/* what the home screen draws (filled on the UI side, about 15 times a second) */
#define GRAN_TAPE_COLS 48u                 /* the strip: peak envelope in 48 columns, oldest first */
#define GRANULAR_MAX_VOICES 8              /* BLOOM's grains */
#define GLASS_TELEM_MODES 12
typedef struct {
    uint8_t  effect;                /* AND_FX_* */
    uint8_t  variant;               /* 0..3 */
    uint8_t  active_count;          /* BLOOM: grains sounding */
    struct {
        uint8_t  norm_distance;     /* 0..255 (offset from the head) */
        int8_t   norm_pan;          /* -128 (L) .. +127 (R) */
        uint8_t  norm_size;         /* 0..255 (grain length) */
        int16_t  pitch_cents;       /* -2400 .. +2400 */
        bool     reverse;
    } voices[GRANULAR_MAX_VOICES];
    uint8_t  looper_state;          /* ACCRETION: 0 idle, 1 rec, 2 play, 3 overdub */
    uint16_t looper_phase;          /* 0..1023 around the loop */
    uint8_t  layers;                /* ACCRETION: overdub passes so far */
    uint8_t  gr_pct;                /* ACCRETION / GLASS: how far the limiter is turning the signal down, % */
    uint8_t  clipping_alert;        /* > 0 for a moment after the limiter worked hard */
    uint8_t  frozen;                /* 1 = frozen */
    uint8_t  long_mode;             /* unused (the old LONG tape); 0 */
    uint8_t  mode_lvl[GLASS_TELEM_MODES];   /* GLASS: how loud each of the 12 modes rings now, 0..255 */
    uint8_t  voices_n;              /* CHOIR: voices active */
    uint8_t  voice_env[4];          /* CHOIR: each voice's swell 0..255 */
    int8_t   voice_semi[4];         /* CHOIR: each voice's interval in semitones */
    uint8_t  seq_pos;               /* FRACTURE / CONSTELLATION: the step the clock is on, 0..15 */
    uint16_t seq_mask;              /* FRACTURE: which of the last 16 steps played (bit per step) */
    uint8_t  seq_pitch[16];         /* CONSTELLATION: each step's height (0 = the step does not play) */
    uint8_t  tape_env[GRAN_TAPE_COLS];      /* 0..255 peak of each column; the last column is the newest audio */
} gran_telemetry_t;

/* Public API */
fx_node_t* fx_granular_new(void);
/* The LAYER: a second GLASS that runs in parallel with the effect, in the (otherwise unused) MOD slot. It hears the effect's output, its wet sound is added to the signal. */
fx_node_t* fx_andromeda_layer_new(void);
/* The SPACE: the shared reverb (an 8-line FDN) with the SHIMMER path, in the REVERB slot. Knobs: 0 decay, 1 size, 2 tone, 3 the send (SPACE), 4 shimmer feedback, 5 shimmer interval. */
fx_node_t* fx_andromeda_space_new(void);
extern volatile uint16_t g_and_tempo_bpm;          /* the unit's tempo, set by the UI tick: FRACTURE follows it */
void       fx_granular_get_telemetry(fx_node_t *node, gran_telemetry_t *out_telem);
void       fx_andromeda_action(fx_node_t *node, int action);
bool       fx_granular_get_freeze(const fx_node_t *node);
uint8_t    fx_andromeda_get_effect(const fx_node_t *node);

/* The overlay as scratch memory for rare jobs that are not audio (a factory reset, a firmware update's staging): the engine stops touching it and passes the
 * signal dry until the release, which clears it. Returns the overlay (at least PA_GRAN_SCRATCH_BYTES). Main loop only. */
#define PA_GRAN_SCRATCH_BYTES 131072u
void      *pa_gran_tape_borrow(void);
void       pa_gran_tape_release(void);
uint32_t   fx_andromeda_overlay_bytes(void);       /* the size of the buffer the three effects share (the memory report) */

#endif /* PA_GRANULAR_H */
