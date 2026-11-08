/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* src/fx/fx_andromeda.c - the DELAY slot of the Andromeda firmware: GLASS, BLOOM, ACCRETION, CHOIR, FRACTURE and CONSTELLATION behind one effect node (see include/pa_granular.h),
 * the GLASS layer in the MOD slot, and the shared reverb (FDN + SHIMMER) in the REVERB slot.
 *
 * The chain hands these nodes Q21 integer blocks (2^21 = 1.0); the V1 engines are float32. Each block is cut into chunks of 64, converted, run, mixed and converted back.
 * The six effects share ONE overlay in .pool (the three that read the recent past also share one PCM history inside it): an effect change (param 10, from the UI thread) is only a
 * request; the audio thread clears the overlay and starts the new engine at the top of the next chunk, so the engine memory is never touched from two threads.
 */
#include "pa_config.h"
#if PA_PRODUCT_ANDROMEDA
#include <string.h>
#include <math.h>
#include "pa_fx.h"
#include "pa_granular.h"
#include "v1_hist.h"
#include "v1_glass.h"
#include "v1_bloom.h"
#include "v1_accretion.h"
#include "v1_choir.h"
#include "v1_fracture.h"
#include "v1_constellation.h"
#include "v1_fdn.h"
#include "v1_shimmer.h"

extern void *pa_alloc(uint32_t);
extern void  pa_free(void *);

volatile uint16_t g_and_tempo_bpm = 120u;     /* the unit's tempo (the BPM setting); FRACTURE follows it. Set by the UI tick. */

#if !defined(PA_HOST_BUILD) && !defined(PA_HOST_SIM) && !defined(PA_EMU_DLL) && !defined(__wasm__)
#define OV_ATTR __attribute__((section(".pool"), aligned(8)))
#else
#define OV_ATTR __attribute__((aligned(8)))
#endif

/* The overlay: whichever effect plays owns it. BLOOM, CHOIR, FRACTURE and CONSTELLATION read the same PCM history, which sits in front of their state. */
typedef union {
    glass_t g;
    struct { v1_hist_t h; int16_t buf[V1_HIST_LEN]; union { bloom_t b; choir_t c; fracture_t f; constellation_t k; } e; } hs;
    acc_t a;
} overlay_t;
static overlay_t s_ov OV_ATTR;
_Static_assert(sizeof(overlay_t) >= PA_GRAN_SCRATCH_BYTES, "the overlay doubles as the scratch memory of the factory reset and the update session");

typedef struct and_state_s {
    uint8_t  fx, variant;              /* what runs now (audio thread) */
    uint8_t  want_fx, want_variant;    /* what was asked for (UI thread) */
    uint8_t  borrowed;
    uint8_t  frozen;                   /* the history (or the loop) is held, for the screen */
    uint8_t  rev_flip;                 /* BLOOM / FRACTURE: the reverse bias flipped by a key */
    uint8_t  layers;
    volatile uint32_t act;             /* bit per AND_ACT_*, set by the UI thread, taken by the audio thread */
    int32_t  macro[4];                 /* Q21 */
    int32_t  mix;                      /* Q21 */
    int32_t  applied[4];               /* the macros the engine was last set to: the engine is only touched when one changes */
    uint8_t  applied_flags;
    uint8_t  have_applied;
    uint8_t  hold;                     /* blocks to wait before the next macro change is applied: a knob turned fast then costs one apply every 4 blocks, not one per block */
    uint16_t applied_bpm;
    /* ACCRETION's strip, kept by the audio thread */
    uint8_t  acc_env[GRAN_TAPE_COLS];
    int      acc_col; uint8_t acc_peak;
    uint32_t lim_hold;                 /* frames the limiter-activity flag stays up */
    /* FRACTURE / CONSTELLATION: which steps played, for the screen */
    uint16_t seq_mask; uint32_t seq_last;
    uint32_t seed_bump;                /* CONSTELLATION: CLEAR rerolls the pattern */
    float    acc_lp[2];                /* ACCRETION: the playback tone filter states */
    uint16_t swap_ramp;                /* short fade-in ramp on swap to kill residual clicks (0 = inactive) */
    uint8_t  wild_backoff;             /* backoff counter if a wild sample was caught */
} and_state_t;

static and_state_t *s_live;

static inline float t01(int32_t q21) { float t = (float)q21 * (1.0f / 2097152.0f); return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

/* ---- the macros -> each engine's parameters ------------------------------------------------------------------------------------------------------------------ */
static void glass_from_macros(glass_t *g, const int32_t m[4], float resonance) {
    glass_params_t p;
    int mat = (int)(t01(m[0]) * 6.0f); if (mat > 5) mat = 5;
    p.material   = mat;
    p.tune_semi  = floorf((t01(m[1]) - 0.5f) * 24.0f + 0.5f);
    p.strike     = 0.4f;                                  /* a softer mallet: less of the click that read as harsh */
    p.brightness = 0.08f + 0.62f * t01(m[3]);             /* the top of the knob is the old 0.7: the glass never gets the hard edge */
    p.decay_s    = 0.20f * powf(35.0f, t01(m[2]));       /* 0.2 s .. 7 s (about 1.2 s at the middle): the top is long but still a glass, not a drone */
    p.inharm     = 0.08f;
    p.resonance  = resonance;
    p.spread     = 0.9f;
    glass_set(g, &p);
}

static void apply_glass(and_state_t *s) { glass_from_macros(&s_ov.g, s->macro, 0.0f); }

static void apply_bloom(and_state_t *s) {
    static const int k_quant[AND_VARIANTS] = { BLOOM_QUANT_MAJOR, BLOOM_QUANT_MINOR, BLOOM_QUANT_PENTA, BLOOM_QUANT_OFF };
    bloom_params_t p;
    p.size_ms = 60.0f + 440.0f * t01(s->macro[0]);
    p.density = 6;
    p.warp    = -7.0f + 19.0f * t01(s->macro[1]);          /* -7 .. +12 semitones */
    p.reverse = s->rev_flip ? 0.8f : 0.2f;
    p.bloom_s = 0.4f + 1.8f * t01(s->macro[2]);            /* never zero: the cloud always has a body */
    p.tone_hz = 6000.0f;
    p.spread  = 0.8f;
    p.drift   = t01(s->macro[3]);
    p.quant   = k_quant[s->variant & 3u];
    p.shape   = BLOOM_SHAPE_HANN;
    p.freeze  = s->frozen;
    p.hq      = 0;
    p.voices  = 6;
    bloom_set(&s_ov.hs.e.b, &p);
}

static void apply_acc(and_state_t *s) {
    acc_params_t p;
    p.length_s    = 2.0f;
    p.feedback    = 0.82f + 0.16f * t01(s->macro[0]);     /* the loop always sustains: 0.82 .. 0.98 */
    p.overdub     = 0.9f;                                 /* each overdub lays the new playing down closer to full level (the look-ahead limiter still guards the tape) */
    p.tone_hz     = 100.0f * powf(150.0f, t01(s->macro[1]));
    p.speed       = 1.0f;
    p.wow         = 0.55f * t01(s->macro[2]);
    p.flutter     = 0.35f * t01(s->macro[2]);
    p.degradation = 0.65f * t01(s->macro[3]);
    p.width       = 1.0f;
    acc_set(&s_ov.a, &p);
}

/* CHOIR: VOICING, DETUNE, SPREAD, ATTACK are the macros; the variant sets the number of voices, the release and the formant colour */
static void apply_choir(and_state_t *s) {
    static const struct { uint8_t voices; float release, formant, det_mul; } k_v[AND_VARIANTS] = { { 3, 0.8f, 0.5f, 1.0f }, { 3, 1.2f, 0.35f, 1.0f }, { 4, 1.5f, 0.2f, 0.8f }, { 4, 2.5f, 0.4f, 1.6f } };
    const int v = s->variant & 3u;
    int vc = (int)(t01(s->macro[0]) * (float)CHOIR_VOICINGS); if (vc >= CHOIR_VOICINGS) vc = CHOIR_VOICINGS - 1;
    choir_params_t p;
    p.voicing = vc; p.voices = k_v[v].voices;
    p.detune_cents = 18.0f * t01(s->macro[1]) * k_v[v].det_mul; if (p.detune_cents > 18.0f) p.detune_cents = 18.0f;
    p.spread = t01(s->macro[2]);
    p.attack_s = 0.05f + 0.70f * t01(s->macro[3]);         /* never an instant attack: no click as the voices come in */
    p.release_s = k_v[v].release; p.formant = k_v[v].formant; p.hq = 0;
    choir_set(&s_ov.hs.e.c, &p);
}

/* FRACTURE: GRID, SLICE, CHAOS, PITCH are the macros; the variant sets the repeats, the reverse bias, the gate, the swing and the density. The tempo is the unit's. */
static void apply_fracture(and_state_t *s) {
    static const struct { uint8_t repeat; float reverse, gate, swing, density; } k_v[AND_VARIANTS] = { { 1, 0.15f, 0.7f, 0.0f, 0.9f }, { 2, 0.30f, 0.9f, 0.2f, 1.0f }, { 4, 0.10f, 0.65f, 0.1f, 0.9f }, { 1, 0.50f, 0.6f, 0.4f, 0.8f } };
    const int v = s->variant & 3u;
    int grid = (int)(t01(s->macro[0]) * (float)FRACTURE_GRIDS); if (grid >= FRACTURE_GRIDS) grid = FRACTURE_GRIDS - 1;
    fracture_params_t p;
    p.bpm = (float)g_and_tempo_bpm;
    p.grid = grid;
    p.slice_ms = 12.0f * powf(18.0f, t01(s->macro[1]));    /* 12 .. 216 ms: rhythmic slices, not grains */
    p.repeat = k_v[v].repeat;
    p.chaos = 0.65f * t01(s->macro[2]);                    /* full chaos is out of reach: the rhythm survives */
    p.reverse = s->rev_flip ? 0.9f : k_v[v].reverse;
    p.pitch = floorf((t01(s->macro[3]) - 0.5f) * 24.0f + 0.5f);   /* +-12 semitones */
    p.gate = k_v[v].gate; p.swing = k_v[v].swing; p.density = k_v[v].density;
    p.seed = 0x2545F491u + (uint32_t)v * 7919u;
    fracture_set(&s_ov.hs.e.f, &p);
}

/* CONSTELLATION: TEMPO, SCALE, MOTION, DENSITY are the macros; the variant sets the pattern, the direction, the range, the division and the probability */
static void apply_constellation(and_state_t *s) {
    static const struct { uint8_t pattern, direction, range, division; float probability; } k_v[AND_VARIANTS] = {
        { 0, CONST_DIR_PROGRAMMED, 2, CONST_DIV_16, 0.95f }, { 3, CONST_DIR_DOWN, 1, CONST_DIV_8, 0.9f }, { 7, CONST_DIR_PENDULUM, 3, CONST_DIV_16, 0.9f }, { 11, CONST_DIR_RANDOM, 2, CONST_DIV_8, 0.85f } };
    const int v = s->variant & 3u;
    int scale = (int)(t01(s->macro[1]) * (float)CONST_SCALES); if (scale >= CONST_SCALES) scale = CONST_SCALES - 1;
    constellation_params_t p;
    p.bpm = 40.0f + 200.0f * t01(s->macro[0]);
    p.division = k_v[v].division; p.pattern = k_v[v].pattern; p.scale = scale; p.root = 0; p.range = k_v[v].range; p.direction = k_v[v].direction;
    p.probability = k_v[v].probability; p.swing = 0.0f; p.motion = t01(s->macro[2]); p.density = t01(s->macro[3]);
    p.seed = 0x1F2E3D4Cu + s->seed_bump * 104729u;
    constellation_set(&s_ov.hs.e.k, &p);
}

static void apply_params(and_state_t *s) {
    switch (s->fx) {
        case AND_FX_GLASS:         apply_glass(s); break;
        case AND_FX_BLOOM:         apply_bloom(s); break;
        case AND_FX_ACCRETION:     apply_acc(s); break;
        case AND_FX_CHOIR:         apply_choir(s); break;
        case AND_FX_FRACTURE:      apply_fracture(s); break;
        default:                   apply_constellation(s); break;
    }
    for (int i = 0; i < 4; i++) s->applied[i] = s->macro[i];
    s->applied_flags = (uint8_t)(s->frozen | (s->rev_flip << 1));
    s->applied_bpm = g_and_tempo_bpm;
    s->have_applied = 1u;
}

/* audio thread: clear the overlay and start the requested engine */
static void start_engine(and_state_t *s) {
    memset(&s_ov, 0, sizeof s_ov);
    s->fx = s->want_fx; s->variant = s->want_variant;
    s->frozen = 0u; s->rev_flip = 0u; s->layers = 0u; s->act = 0u; s->seq_mask = 0u; s->seq_last = 0xFFFFFFFFu; s->seed_bump = 0u;
    memset(s->acc_env, 0, sizeof s->acc_env); s->acc_col = -1; s->acc_peak = 0u; s->lim_hold = 0u; s->acc_lp[0] = s->acc_lp[1] = 0.0f;
    s->swap_ramp = 128u;
    switch (s->fx) {
        case AND_FX_GLASS:         glass_init(&s_ov.g, AND_FS); break;
        case AND_FX_BLOOM:         bloom_init(&s_ov.hs.e.b, AND_FS); v1_hist_init(&s_ov.hs.h, s_ov.hs.buf); break;
        case AND_FX_ACCRETION:     acc_init(&s_ov.a, AND_FS); break;
        case AND_FX_CHOIR:         choir_init(&s_ov.hs.e.c, AND_FS); v1_hist_init(&s_ov.hs.h, s_ov.hs.buf); break;
        case AND_FX_FRACTURE:      fracture_init(&s_ov.hs.e.f, AND_FS); v1_hist_init(&s_ov.hs.h, s_ov.hs.buf); break;
        default:                   constellation_init(&s_ov.hs.e.k, AND_FS); v1_hist_init(&s_ov.hs.h, s_ov.hs.buf); break;
    }
    apply_params(s);
    if (s->fx == AND_FX_FRACTURE) fracture_reset(&s_ov.hs.e.f);
    if (s->fx == AND_FX_CONSTELLATION) constellation_reset(&s_ov.hs.e.k);
}

static void do_actions(and_state_t *s, uint32_t act) {
    const uint32_t P = 1u << AND_ACT_PRIMARY, S2 = 1u << AND_ACT_SECONDARY, F = 1u << AND_ACT_FREEZE, C = 1u << AND_ACT_CLEAR;
    switch (s->fx) {
    case AND_FX_GLASS:
        if (act & P)  glass_strike(&s_ov.g, 1.0f);
        if (act & S2) glass_strike(&s_ov.g, 0.5f);
        if (act & C)  glass_reset(&s_ov.g);
        break;
    case AND_FX_BLOOM:
        if (act & P)       s->rev_flip ^= 1u;
        if (act & (S2 | F)) s->frozen ^= 1u;
        if (act & C)       { bloom_reset(&s_ov.hs.e.b); v1_hist_init(&s_ov.hs.h, s_ov.hs.buf); s->frozen = 0u; }
        break;
    case AND_FX_CHOIR:
        if (act & (P | S2 | F)) s->frozen ^= 1u;                                    /* hold the chord: the history stops, the voices keep singing it */
        if (act & C)       { choir_reset(&s_ov.hs.e.c); v1_hist_init(&s_ov.hs.h, s_ov.hs.buf); s->frozen = 0u; }
        break;
    case AND_FX_FRACTURE:
        if (act & P)       s->rev_flip ^= 1u;                                       /* reverse burst: most slices play backwards until the key is pressed again */
        if (act & (S2 | F)) s->frozen ^= 1u;                                         /* hold what the slices are cut from */
        if (act & C)       { fracture_reset(&s_ov.hs.e.f); v1_hist_init(&s_ov.hs.h, s_ov.hs.buf); s->frozen = 0u; s->rev_flip = 0u; }
        break;
    case AND_FX_CONSTELLATION:
        if (act & P)       constellation_reset(&s_ov.hs.e.k);                       /* restart the sequence from its first step */
        if (act & (S2 | F)) s->frozen ^= 1u;                                         /* hold the chord the sequence plays */
        if (act & C)       { s->seed_bump++; s->have_applied = 0u; s->frozen = 0u; }  /* a new pattern: the seed changes */
        break;
    default:
        if (act & P) {                                                               /* <: with no loop it starts the recording (and ends it early while recording); with a loop it freezes it */
            const int md = s_ov.a.mode;
            if (md == ACC_IDLE || md == ACC_REC) { acc_command(&s_ov.a, ACC_CMD_RECPLAY); if (md == ACC_IDLE) s->layers = 0u; }
            else { acc_command(&s_ov.a, ACC_CMD_FREEZE); s->frozen = s_ov.a.frozen ? 1u : 0u; }
        }
        if (act & F) { acc_command(&s_ov.a, ACC_CMD_FREEZE); s->frozen = s_ov.a.frozen ? 1u : 0u; }
        if (act & S2) {                                                              /* idle -> REC; REC -> PLAY; PLAY -> DUB; DUB -> PLAY */
            const int md = s_ov.a.mode;
            acc_command(&s_ov.a, (md == ACC_IDLE || md == ACC_REC) ? ACC_CMD_RECPLAY : ACC_CMD_OVERDUB);
            if (md == ACC_PLAY) s->layers++;
            if (md == ACC_IDLE) s->layers = 0u;
        }
        if (act & C)       { acc_command(&s_ov.a, ACC_CMD_CLEAR); s->frozen = 0u; s->layers = 0u; memset(s->acc_env, 0, sizeof s->acc_env); }
        break;
    }
}

/* ---- audio ----------------------------------------------------------------------------------------------------------------------------------------------------- */
#define WET_TRIM_GLASS 0.70f            /* the engine's wet sum is limited at 0.45; lowered from 1.6 so the glass sits under the playing instead of on top of it */
#define WET_TRIM_BLOOM 7.0f             /* the cloud's wet peak is 0.125 at its defaults: this puts it near the other two */
#define WET_TRIM_ACC   1.35f            /* the loop plays back at the level it was played, which is well under full scale: this brings it up to the playing */
#define WET_TRIM_CHOIR 2.4f             /* a held chord's choir is about -6 dB under the playing: this brings it up to it */
#define WET_TRIM_FRACT 2.6f             /* slices of the playing, gated: about -7 dB under the playing at 1.0 */
#define WET_TRIM_CONST 3.2f             /* a sparse sequence of windowed grains: this brings it clearly above the playing (was 1.5: too faint) */

static void and_run_stereo(fx_node_t *node, int32_t *buf_L, int32_t *buf_R, int n) {
    and_state_t *s = (and_state_t *)node->state;
    if (!s || s->borrowed) return;                                   /* borrowed: the overlay is scratch memory now, pass dry */

    /* MIX: both at full level at 50 %; below it the wet fades out, above it the dry does (a pedal's usual law) */
    int32_t mix = s->mix;
    if (mix < 0) mix = 0;
    if (mix > 2097152) mix = 2097152;

    /* fully dry — do not run granular/modal engines */
    if (mix == 0) {
        if (s->fx != s->want_fx || s->variant != s->want_variant)
            start_engine(s);   /* still allow pending swap to complete cleanly */
        return;
    }

    const float wet_g = (float)mix * (1.0f / 2097152.0f);
    const float dry_g = (mix <= 1048576) ? 1.0f : (float)(2097152 - mix) * (2.0f / 2097152.0f);
    const float inv = 1.0f / 2097152.0f;

    for (int at = 0; at < n; at += BLOOM_MAX_BLOCK) {
        const int m = (n - at < BLOOM_MAX_BLOCK) ? n - at : BLOOM_MAX_BLOCK;
        int32_t *L = buf_L + at, *R = buf_R + at;
        if (s->fx != s->want_fx || s->variant != s->want_variant) start_engine(s);
        if (s->act) { const uint32_t a = s->act; s->act = 0u; do_actions(s, a); }
        if (s->hold) s->hold--;
        if (!s->have_applied || s->applied_flags != (uint8_t)(s->frozen | (s->rev_flip << 1)) || (s->fx == AND_FX_FRACTURE && s->applied_bpm != g_and_tempo_bpm)) {
            apply_params(s);                                             /* a start, a freeze, a reverse flip, a new tempo: at once */
        } else if (!s->hold && (s->applied[0] != s->macro[0] || s->applied[1] != s->macro[1] || s->applied[2] != s->macro[2] || s->applied[3] != s->macro[3])) {
            apply_params(s); s->hold = 3u;                               /* a knob: at most once in 4 blocks (about 6 ms); the last value always arrives */
        }

        float inL[BLOOM_MAX_BLOCK], inR[BLOOM_MAX_BLOCK], wl[BLOOM_MAX_BLOCK], wr[BLOOM_MAX_BLOCK], mono[BLOOM_MAX_BLOCK];
        for (int i = 0; i < m; i++) { inL[i] = (float)L[i] * inv; inR[i] = (float)R[i] * inv; mono[i] = 0.5f * (inL[i] + inR[i]); }
        float trim = 1.0f;
        switch (s->fx) {
        case AND_FX_GLASS:
            glass_process(&s_ov.g, mono, mono, wl, wr, m); trim = WET_TRIM_GLASS; break;
        case AND_FX_BLOOM:
            if (!s->frozen) v1_hist_write(&s_ov.hs.h, mono, m);              /* frozen: the history stops, the cloud holds */
            bloom_process(&s_ov.hs.e.b, &s_ov.hs.h, mono, wl, wr, m); trim = WET_TRIM_BLOOM; break;
        case AND_FX_CHOIR:
            if (!s->frozen) v1_hist_write(&s_ov.hs.h, mono, m);
            choir_process(&s_ov.hs.e.c, &s_ov.hs.h, mono, wl, wr, m); trim = WET_TRIM_CHOIR; break;
        case AND_FX_FRACTURE: {
            if (!s->frozen) v1_hist_write(&s_ov.hs.h, mono, m);
            fracture_process(&s_ov.hs.e.f, &s_ov.hs.h, wl, wr, m); trim = WET_TRIM_FRACT;
            const fracture_t *fr = &s_ov.hs.e.f;                              /* which steps played, for the screen */
            if (fr->step != s->seq_last) { s->seq_last = fr->step; const uint16_t bit = (uint16_t)(1u << (fr->step & 15u)); if (fr->step_on) s->seq_mask |= bit; else s->seq_mask &= (uint16_t)~bit; }
            break; }
        case AND_FX_CONSTELLATION:
            if (!s->frozen) v1_hist_write(&s_ov.hs.h, mono, m);
            constellation_process(&s_ov.hs.e.k, &s_ov.hs.h, wl, wr, m); trim = WET_TRIM_CONST;
            /* a stack of grains peaks far above its average: a soft ceiling at 0.8 (unity slope below it) lets the average come up without the peaks reaching the clamp */
            for (int i = 0; i < m; i++) {
                float *w2[2] = { &wl[i], &wr[i] };
                for (int c = 0; c < 2; c++) {
                    float u = *w2[c] * trim * (1.0f / 0.8f);
                    if (u > 3.0f) u = 3.0f; else if (u < -3.0f) u = -3.0f;
                    *w2[c] = 0.8f * u * (27.0f + u * u) / (27.0f + 9.0f * u * u) / trim;
                }
            }
            break;
        default: {
            acc_process(&s_ov.a, mono, mono, wl, wr, m); trim = WET_TRIM_ACC;
            {   /* The tape's colour at PLAYBACK: TONE and DEGRADE used to act only on what is written, so the four variants sounded the same until an overdub. Now the loop itself is
                 * darkened (TONE: 1.5 kHz .. 15 kHz) and, with DEGRADE, saturated (the tape's soft knee): each variant is heard from the first pass. */
                const float tone = 1500.0f * powf(10.0f, t01(s->macro[1])), deg = t01(s->macro[3]);
                float k = 1.0f - expf(-6.2831853f * tone / AND_FS); if (k > 1.0f) k = 1.0f;
                const float dr = 1.0f + 5.0f * deg;
                for (int i = 0; i < m; i++) {
                    s->acc_lp[0] += k * (wl[i] - s->acc_lp[0]); s->acc_lp[1] += k * (wr[i] - s->acc_lp[1]);
                    float y[2] = { s->acc_lp[0], s->acc_lp[1] };
                    for (int c = 0; c < 2; c++) {
                        float u = y[c] * 8.0f * dr; if (u > 3.0f) u = 3.0f; else if (u < -3.0f) u = -3.0f;
                        const float sat = u * (27.0f + u * u) / (27.0f + 9.0f * u * u) / (8.0f * dr);
                        y[c] += deg * (sat - y[c]);
                    }
                    wl[i] = y[0]; wr[i] = y[1];
                }
            }
            float pk = 0.0f; for (int i = 0; i < m; i++) { const float a = fabsf(wl[i]); if (a > pk) pk = a; }
            const uint32_t len = s_ov.a.len;
            int col = -1;
            if (len) col = (int)(((uint64_t)(s_ov.a.pos >> ACC_FRAC_BITS) * GRAN_TAPE_COLS) / len);
            else if (s_ov.a.mode == ACC_REC) col = (int)(((uint64_t)s_ov.a.rec_frame * GRAN_TAPE_COLS) / (uint32_t)(2.0f * AND_FS));
            if (col >= (int)GRAN_TAPE_COLS) col = (int)GRAN_TAPE_COLS - 1;
            const uint8_t p8 = (uint8_t)(pk * 255.0f > 255.0f ? 255.0f : pk * 255.0f);
            if (col != s->acc_col) { if (s->acc_col >= 0) s->acc_env[s->acc_col] = s->acc_peak; s->acc_col = col; s->acc_peak = 0u; }
            if (p8 > s->acc_peak) s->acc_peak = p8;
            if (s_ov.a.lim_min < 0.8f) { s->lim_hold = 20000u; }
            else if (s->lim_hold > (uint32_t)m) s->lim_hold -= (uint32_t)m; else s->lim_hold = 0u;
            break; }
        }
        /* Finite guard on wet output & trim scaling */
        uint8_t wild = 0u;
        for (int i = 0; i < m; i++) {
            float l = wl[i] * trim;
            float r = wr[i] * trim;
            if (!(l > -8.0f && l < 8.0f)) { l = 0.0f; wild = 1u; }
            if (!(r > -8.0f && r < 8.0f)) { r = 0.0f; wild = 1u; }
            wl[i] = l;
            wr[i] = r;
        }
        if (wild) {
            if (s->wild_backoff == 0u) {
                start_engine(s);
                s->wild_backoff = 50u;
            }
        }
        if (s->wild_backoff > 0u) s->wild_backoff--;

        /* Soft ramp for first 2-4 ms after an engine swap */
        if (s->swap_ramp > 0u) {
            for (int i = 0; i < m; i++) {
                if (s->swap_ramp > 0u) {
                    float r_scale = (128.0f - (float)s->swap_ramp) * (1.0f / 128.0f);
                    wl[i] *= r_scale;
                    wr[i] *= r_scale;
                    s->swap_ramp--;
                }
            }
        }

        for (int i = 0; i < m; i++) {
            const float ol = dry_g * inL[i] + wet_g * wl[i], orr = dry_g * inR[i] + wet_g * wr[i];
            float fl = ol * 2097152.0f, fr = orr * 2097152.0f;
            if (fl > 2076000.0f) fl = 2076000.0f; else if (fl < -2076000.0f) fl = -2076000.0f;
            if (fr > 2076000.0f) fr = 2076000.0f; else if (fr < -2076000.0f) fr = -2076000.0f;
            L[i] = (int32_t)fl; R[i] = (int32_t)fr;
        }
    }
}

static void and_run_mono(fx_node_t *node, int32_t *buf, int n) {
    int32_t r[PA_BLOCK_N];
    for (int at = 0; at < n; at += PA_BLOCK_N) {
        const int m = (n - at < PA_BLOCK_N) ? n - at : PA_BLOCK_N;
        memcpy(r, buf + at, (size_t)m * sizeof(int32_t));
        and_run_stereo(node, buf + at, r, m);
        for (int i = 0; i < m; i++) buf[at + i] = (buf[at + i] >> 1) + (r[i] >> 1);
    }
}

/* ---- parameters ------------------------------------------------------------------------------------------------------------------------------------------------ */
static void and_set_param(fx_node_t *node, int id, int32_t val) {
    and_state_t *s = (and_state_t *)node->state;
    if (!s) return;
    switch (id) {
        case GRAN_PARAM_MACRO0: case GRAN_PARAM_MACRO1: case GRAN_PARAM_MACRO2: case GRAN_PARAM_MACRO3: s->macro[id] = val; break;
        case GRAN_PARAM_MIX:    s->mix = val; break;
        case GRAN_PARAM_PRESET:
            if (val >= 0 && val < GRAN_PRESET_COUNT) { s->want_fx = (uint8_t)(val / AND_VARIANTS); s->want_variant = (uint8_t)(val % AND_VARIANTS); }
            break;
        case GRAN_PARAM_FREEZE: if (val != (int32_t)s->frozen && (s->fx != AND_FX_GLASS)) s->act |= 1u << AND_ACT_FREEZE; break;
        default: break;
    }
}

static void and_create(fx_node_t *node) {
    and_state_t *s = (and_state_t *)pa_alloc(sizeof(and_state_t));
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->macro[0] = s->macro[1] = s->macro[2] = s->macro[3] = 1048576;
    s->mix = 1048576;
    s->want_fx = AND_FX_GLASS; s->want_variant = 0u;
    s->fx = 255u;                                                    /* "nothing running": the first block starts the engine */
    s_live = s;
    node->state = s;
}

static void and_destroy(fx_node_t *node) {
    and_state_t *s = (and_state_t *)node->state;
    if (s) { if (s_live == s) s_live = 0; pa_free(s); node->state = 0; }
}

static void and_reset(fx_node_t *node) {
    and_state_t *s = (and_state_t *)node->state;
    if (s) s->fx = 255u;                                             /* restart the engine at the next block */
}

fx_node_t *fx_granular_new(void) {
    fx_node_t *node = (fx_node_t *)pa_alloc(sizeof(fx_node_t));
    if (!node) return 0;
    memset(node, 0, sizeof(fx_node_t));
    node->tag        = "ANDR";
    node->run        = and_run_mono;
    node->run_stereo = and_run_stereo;
    node->create     = and_create;
    node->destroy    = and_destroy;
    node->set_param  = and_set_param;
    node->reset      = and_reset;
    node->active     = 1;
    node->stereo_opt = 0;
    and_create(node);
    return node;
}

/* ---- the LAYER: a second GLASS in the MOD slot ---------------------------------------------------------------------------------------------------------- */
/* It sits after the effect in the chain (DYN, effect, EQ, LAYER, REVERB), so it rings from what the effect plays: a loop or a cloud strikes and feeds a crystal, a bell,
 * a kalimba. Its sound is ADDED to the signal (a parallel path); its level is knob 4, and at 0 the node does nothing at all. Memory: 1.6 KB. */
#define LAYER_TRIM 0.6f            /* at the level 0.8 the kalimba and marimba added +6 dB over the input with 1.2: a layer should sit under the effect */
typedef struct {
    glass_t g;
    int32_t macro[4], level;
    int32_t applied[4];
    uint8_t have_applied, active_prev, hold;
} and_layer_t;

static void layer_run_stereo(fx_node_t *node, int32_t *buf_L, int32_t *buf_R, int n) {
    and_layer_t *s = (and_layer_t *)node->state;
    if (!s) return;
    if (s->level <= 0) { s->active_prev = 0u; return; }                       /* off: nothing runs, nothing is added */
    if (!s->active_prev) { glass_reset(&s->g); s->active_prev = 1u; }          /* switched on: start from silence, not from an old ring */
    if (s->hold) s->hold--;
    if (!s->have_applied || (!s->hold && (s->applied[0] != s->macro[0] || s->applied[1] != s->macro[1] || s->applied[2] != s->macro[2] || s->applied[3] != s->macro[3]))) {
        glass_from_macros(&s->g, s->macro, 0.25f);                            /* a little continuous input: the layer sings with what is played, not only on attacks */
        for (int i = 0; i < 4; i++) s->applied[i] = s->macro[i];
        s->have_applied = 1u; s->hold = 3u;                                   /* a knob: at most once in 4 blocks */
    }
    const float lvl = (float)(s->level > 2097152 ? 2097152 : s->level) * (1.0f / 2097152.0f) * LAYER_TRIM;
    const float inv = 1.0f / 2097152.0f;
    for (int at = 0; at < n; at += GLASS_MAX_BLOCK) {
        const int m = (n - at < GLASS_MAX_BLOCK) ? n - at : GLASS_MAX_BLOCK;
        int32_t *L = buf_L + at, *R = buf_R + at;
        float mono[GLASS_MAX_BLOCK], wl[GLASS_MAX_BLOCK], wr[GLASS_MAX_BLOCK];
        for (int i = 0; i < m; i++) mono[i] = 0.5f * ((float)L[i] + (float)R[i]) * inv;
        glass_process(&s->g, mono, mono, wl, wr, m);
        for (int i = 0; i < m; i++) {
            float fl = (float)L[i] + lvl * wl[i] * 2097152.0f, fr = (float)R[i] + lvl * wr[i] * 2097152.0f;
            if (fl > 2076000.0f) fl = 2076000.0f; else if (fl < -2076000.0f) fl = -2076000.0f;
            if (fr > 2076000.0f) fr = 2076000.0f; else if (fr < -2076000.0f) fr = -2076000.0f;
            L[i] = (int32_t)fl; R[i] = (int32_t)fr;
        }
    }
}

static void layer_run_mono(fx_node_t *node, int32_t *buf, int n) {
    int32_t r[PA_BLOCK_N];
    for (int at = 0; at < n; at += PA_BLOCK_N) {
        const int m = (n - at < PA_BLOCK_N) ? n - at : PA_BLOCK_N;
        memcpy(r, buf + at, (size_t)m * sizeof(int32_t));
        layer_run_stereo(node, buf + at, r, m);
        for (int i = 0; i < m; i++) buf[at + i] = (buf[at + i] >> 1) + (r[i] >> 1);
    }
}

static void layer_set_param(fx_node_t *node, int id, int32_t val) {
    and_layer_t *s = (and_layer_t *)node->state;
    if (!s) return;
    if (id >= 0 && id < 4) s->macro[id] = val;
    else if (id == 4) s->level = val;                                          /* id 10 (the MOD slot's "type") and the rest: nothing to do */
}

static void layer_create(fx_node_t *node) {
    and_layer_t *s = (and_layer_t *)pa_alloc(sizeof(and_layer_t));
    if (!s) return;
    memset(s, 0, sizeof *s);
    glass_init(&s->g, AND_FS);
    s->macro[0] = s->macro[1] = s->macro[2] = s->macro[3] = 1048576;
    s->level = 0;
    node->state = s;
}

static void layer_destroy(fx_node_t *node) {
    if (node->state) { pa_free(node->state); node->state = 0; }
}

static void layer_reset(fx_node_t *node) {
    and_layer_t *s = (and_layer_t *)node->state;
    if (s) { glass_reset(&s->g); s->active_prev = 0u; }
}

fx_node_t *fx_andromeda_layer_new(void) {
    fx_node_t *node = (fx_node_t *)pa_alloc(sizeof(fx_node_t));
    if (!node) return 0;
    memset(node, 0, sizeof(fx_node_t));
    node->tag        = "LAYR";
    node->run        = layer_run_mono;
    node->run_stereo = layer_run_stereo;
    node->create     = layer_create;
    node->destroy    = layer_destroy;
    node->set_param  = layer_set_param;
    node->reset      = layer_reset;
    node->active     = 1;
    node->stereo_opt = 0;
    layer_create(node);
    return node;
}

/* ---- SPACE: the shared reverb (FDN) with the SHIMMER path, in the REVERB slot --------------------------------------------------------------------------- */
/* Knobs of the slot: 0 DECAY 0.2 .. 25 s, 1 SIZE, 2 TONE (the damping, 1 .. 16 kHz), 3 SPACE (the send: how much reverb is added; 0 = nothing), 4 SHIMMER feedback, 5 the shimmer interval
 * (a quarter of the range each: +12, +7, +12 and +7, +19). The reverb is ADDED to the signal (a send on a parallel path). The tank and the shimmer are one static object (65 KB in .pool):
 * there is one reverb. A block that is off costs nothing. */
#define SPACE_TRIM 1.2f              /* set by test_andromeda_fx.c: at the default SPACE of 35 % the reverb sits about 5 dB under the dry signal on a pluck */
typedef struct { fdn_t fdn; shimmer_t sh; } space_engine_t;
static space_engine_t s_space OV_ATTR;
typedef struct { int32_t k[6]; int32_t applied[6]; uint8_t started, have_applied; } and_space_t;

static void space_apply(and_space_t *s) {
    fdn_params_t p;
    p.size = t01(s->k[1]);
    p.decay_s = 0.2f * powf(125.0f, t01(s->k[0]));
    p.diffusion = 0.9f;
    p.damp_hz = 1000.0f * powf(16.0f, t01(s->k[2]));
    p.mod = 0.3f; p.mod_rate_hz = 0.4f; p.width = 1.0f; p.predelay_ms = 12.0f;
    fdn_set(&s_space.fdn, &p);
    int iv = (int)(t01(s->k[5]) * (float)SHIM_INTERVALS); if (iv >= SHIM_INTERVALS) iv = SHIM_INTERVALS - 1;
    shimmer_params_t sp = { iv, t01(s->k[4]), 6000.0f };
    shimmer_set(&s_space.sh, &sp);
    for (int i = 0; i < 6; i++) s->applied[i] = s->k[i];
    s->have_applied = 1u;
}

static void space_run_stereo(fx_node_t *node, int32_t *buf_L, int32_t *buf_R, int n) {
    and_space_t *s = (and_space_t *)node->state;
    if (!s) return;
    if (!s->started) { fdn_init(&s_space.fdn, AND_FS); shimmer_init(&s_space.sh, AND_FS); space_apply(s); s->started = 1u; }
    else if (!s->have_applied || memcmp(s->applied, s->k, sizeof s->k) != 0) space_apply(s);
    const float amount = t01(s->k[3]) * SPACE_TRIM;
    const float inv = 1.0f / 2097152.0f;
    for (int at = 0; at < n; at += FDN_MAX_BLOCK) {
        const int m = (n - at < FDN_MAX_BLOCK) ? n - at : FDN_MAX_BLOCK;
        int32_t *L = buf_L + at, *R = buf_R + at;
        float inL[FDN_MAX_BLOCK], inR[FDN_MAX_BLOCK], wl[FDN_MAX_BLOCK], wr[FDN_MAX_BLOCK];
        for (int i = 0; i < m; i++) { inL[i] = (float)L[i] * inv; inR[i] = (float)R[i] * inv; }
        shimmer_process(&s_space.sh, &s_space.fdn, inL, inR, wl, wr, m);
        for (int i = 0; i < m; i++) {
            float fl = (float)L[i] + amount * wl[i] * 2097152.0f, fr = (float)R[i] + amount * wr[i] * 2097152.0f;
            if (fl > 2076000.0f) fl = 2076000.0f; else if (fl < -2076000.0f) fl = -2076000.0f;
            if (fr > 2076000.0f) fr = 2076000.0f; else if (fr < -2076000.0f) fr = -2076000.0f;
            L[i] = (int32_t)fl; R[i] = (int32_t)fr;
        }
    }
}

static void space_run_mono(fx_node_t *node, int32_t *buf, int n) {
    int32_t r[PA_BLOCK_N];
    for (int at = 0; at < n; at += PA_BLOCK_N) {
        const int m = (n - at < PA_BLOCK_N) ? n - at : PA_BLOCK_N;
        memcpy(r, buf + at, (size_t)m * sizeof(int32_t));
        space_run_stereo(node, buf + at, r, m);
        for (int i = 0; i < m; i++) buf[at + i] = (buf[at + i] >> 1) + (r[i] >> 1);
    }
}

static void space_set_param(fx_node_t *node, int id, int32_t val) {
    and_space_t *s = (and_space_t *)node->state;
    if (s && id >= 0 && id < 6) s->k[id] = val;                                 /* id 10 (the slot's "type") and the rest: nothing to do */
}

static void space_create(fx_node_t *node) {
    and_space_t *s = (and_space_t *)pa_alloc(sizeof(and_space_t));
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->k[0] = 1500000; s->k[1] = 1150000; s->k[2] = 1250000; s->k[3] = 735000; s->k[4] = 0; s->k[5] = 0;
    node->state = s;
}

static void space_destroy(fx_node_t *node) { if (node->state) { pa_free(node->state); node->state = 0; } }

static void space_reset(fx_node_t *node) {
    and_space_t *s = (and_space_t *)node->state;
    if (s) s->started = 0u;                                                      /* the tank restarts empty at the next block */
}

fx_node_t *fx_andromeda_space_new(void) {
    fx_node_t *node = (fx_node_t *)pa_alloc(sizeof(fx_node_t));
    if (!node) return 0;
    memset(node, 0, sizeof(fx_node_t));
    node->tag        = "SPCE";
    node->run        = space_run_mono;
    node->run_stereo = space_run_stereo;
    node->create     = space_create;
    node->destroy    = space_destroy;
    node->set_param  = space_set_param;
    node->reset      = space_reset;
    node->active     = 1;
    node->stereo_opt = 0;
    space_create(node);
    return node;
}

/* ---- the screen's view ------------------------------------------------------------------------------------------------------------------------------------------ */
static uint8_t u8_clamp(float v) { return (uint8_t)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v)); }

static void hist_strip(gran_telemetry_t *out, const uint32_t head) {
    const uint32_t seg = V1_HIST_LEN / GRAN_TAPE_COLS, step = seg / 32u;           /* 32 spaced reads per column, on the UI side */
    for (uint32_t col = 0; col < GRAN_TAPE_COLS; col++) {
        const uint32_t start = (head - (GRAN_TAPE_COLS - col) * seg) & V1_HIST_MASK;
        int32_t peak = 0;
        for (uint32_t k = 0; k < seg; k += step) { int32_t v = s_ov.hs.buf[(start + k) & V1_HIST_MASK]; if (v < 0) v = -v; if (v > peak) peak = v; }
        peak >>= 6;
        out->tape_env[col] = (uint8_t)(peak > 255 ? 255 : peak);
    }
}

void fx_granular_get_telemetry(fx_node_t *node, gran_telemetry_t *out) {
    if (!node || !node->state || !out) return;
    and_state_t *s = (and_state_t *)node->state;
    memset(out, 0, sizeof *out);
    const uint8_t fx = s->fx;
    if (fx >= AND_FX_COUNT || s->borrowed) { out->effect = s->want_fx; out->variant = s->want_variant; return; }
    out->effect = fx; out->variant = s->variant; out->frozen = s->frozen;
    if (fx == AND_FX_GLASS) {
        const glass_t *g = &s_ov.g;
        for (int k = 0; k < GLASS_TELEM_MODES; k++) {
            const float y1 = g->y1[0][k], y2 = g->y2[0][k];
            out->mode_lvl[k] = u8_clamp(sqrtf(sqrtf(y1 * y1 + y2 * y2) * fabsf(g->w[0][k]) * (1.0f / 0.64f)) * 255.0f);   /* compressive: the upper modes are quieter by design but should still show */
        }
        out->clipping_alert = 0;
    } else if (fx == AND_FX_BLOOM) {
        const bloom_t *b = &s_ov.hs.e.b;
        const uint32_t head = s_ov.hs.h.head;
        int c = 0;
        for (int i = 0; i < BLOOM_MAX_GRAINS && c < (int)GRANULAR_MAX_VOICES; i++) {
            const bloom_grain_t *gr = &b->gr[i];
            if (!gr->active) continue;
            const uint32_t back = (head - (gr->pos >> 16)) & V1_HIST_MASK;
            out->voices[c].norm_distance = u8_clamp((float)back * (255.0f / (float)V1_HIST_LEN));
            const float sum = gr->gl + gr->gr;
            out->voices[c].norm_pan = (int8_t)(sum > 0.0f ? (gr->gr - gr->gl) / sum * 127.0f : 0.0f);
            const uint32_t len = gr->n_rise + gr->n_hold + gr->n_fall;
            out->voices[c].norm_size = u8_clamp((float)len * (255.0f / (0.5f * AND_FS)));
            const float ratio = (float)(gr->inc < 0 ? -gr->inc : gr->inc) * (1.0f / 65536.0f);
            out->voices[c].pitch_cents = (int16_t)(ratio > 0.01f ? 1200.0f * log2f(ratio) : 0.0f);
            out->voices[c].reverse = gr->inc < 0;
            c++;
        }
        out->active_count = (uint8_t)c;
        hist_strip(out, head);
    } else if (fx == AND_FX_CHOIR) {
        const choir_t *c = &s_ov.hs.e.c;
        out->voices_n = (uint8_t)c->n_active;
        for (int i = 0; i < c->n_active && i < 4; i++) {
            out->voice_env[i] = u8_clamp(c->v[i].env * 255.0f);
            const float r = c->v[i].ratio;
            out->voice_semi[i] = (int8_t)(r > 0.01f ? 12.0f * log2f(r) + (r > 1.0f ? 0.5f : -0.5f) : 0.0f);
        }
        hist_strip(out, s_ov.hs.h.head);
    } else if (fx == AND_FX_FRACTURE) {
        const fracture_t *f = &s_ov.hs.e.f;
        out->seq_pos = (uint8_t)(f->step & 15u); out->seq_mask = s->seq_mask;
        int c = 0;
        for (int i = 0; i < FRACTURE_VOICES && c < (int)GRANULAR_MAX_VOICES; i++) {
            const fracture_voice_t *v = &f->v[i];
            if (!v->active) continue;
            out->voices[c].norm_distance = u8_clamp((float)((s_ov.hs.h.head - (v->pos >> 16)) & V1_HIST_MASK) * (255.0f / (float)V1_HIST_LEN));
            out->voices[c].norm_pan = (int8_t)((v->gr - v->gl) * 100.0f);
            out->voices[c].norm_size = u8_clamp((float)v->total * (255.0f / (0.5f * AND_FS)));
            const float ra = v->ratio < 0.0f ? -v->ratio : v->ratio;
            out->voices[c].pitch_cents = (int16_t)(ra > 0.01f ? 1200.0f * log2f(ra) : 0.0f);
            out->voices[c].reverse = v->ratio < 0.0f;
            c++;
        }
        out->active_count = (uint8_t)c;
        hist_strip(out, s_ov.hs.h.head);
    } else if (fx == AND_FX_CONSTELLATION) {
        const constellation_t *k = &s_ov.hs.e.k;
        out->seq_pos = (uint8_t)(k->step & 15u);
        const int total = (k->p.scale >= 4 && k->p.scale <= 5 ? 5 : (k->p.scale == 7 ? 6 : 7)) * k->p.range;
        for (int i = 0; i < CONST_STEPS; i++)
            out->seq_pitch[i] = ((float)k->st[i].thr < k->p.density * 256.0f) ? (uint8_t)(40 + (k->st[i].degree % total) * 200 / (total > 1 ? total - 1 : 1)) : 0u;
        int c = 0;
        for (int i = 0; i < CONST_VOICES && c < (int)GRANULAR_MAX_VOICES; i++) {
            const const_voice_t *v = &k->v[i];
            if (!v->active) continue;
            out->voices[c].norm_distance = u8_clamp((float)((s_ov.hs.h.head - (v->pos >> 16)) & V1_HIST_MASK) * (255.0f / (float)V1_HIST_LEN));
            out->voices[c].norm_pan = (int8_t)((v->gr - v->gl) * 100.0f);
            out->voices[c].norm_size = u8_clamp((float)v->total * (255.0f / (0.5f * AND_FS)));
            const int32_t inc = v->inc < 0 ? -v->inc : v->inc; const float ra = (float)inc * (1.0f / 65536.0f);
            out->voices[c].pitch_cents = (int16_t)(ra > 0.01f ? 1200.0f * log2f(ra) : 0.0f);
            out->voices[c].reverse = v->inc < 0;
            c++;
        }
        out->active_count = (uint8_t)c;
        hist_strip(out, s_ov.hs.h.head);
    } else {
        const acc_t *a = &s_ov.a;
        out->looper_state = (uint8_t)a->mode;
        if (a->len) out->looper_phase = (uint16_t)(((uint64_t)(a->pos >> ACC_FRAC_BITS) * 1024u) / a->len);
        else if (a->mode == ACC_REC) out->looper_phase = (uint16_t)(((uint64_t)a->rec_frame * 1024u) / (uint32_t)(2.0f * AND_FS));
        out->layers = s->layers;
        const float g = a->lim_g;
        out->gr_pct = u8_clamp((1.0f - (g > 1.0f ? 1.0f : g)) * 100.0f);
        out->clipping_alert = s->lim_hold ? 1u : 0u;
        memcpy(out->tape_env, s->acc_env, sizeof out->tape_env);
    }
}

void fx_andromeda_action(fx_node_t *node, int action) {
    if (!node || !node->state || action < 0 || action > AND_ACT_CLEAR) return;
    ((and_state_t *)node->state)->act |= 1u << action;
}

bool fx_granular_get_freeze(const fx_node_t *node) {
    if (!node || !node->state) return false;
    return ((const and_state_t *)node->state)->frozen != 0u;
}

uint8_t fx_andromeda_get_effect(const fx_node_t *node) {
    if (!node || !node->state) return 0;
    return ((const and_state_t *)node->state)->want_fx;
}

uint32_t fx_andromeda_overlay_bytes(void) { return (uint32_t)sizeof(overlay_t); }

/* The overlay as scratch memory (pa_granular.h). The audio thread and this code share core 0: once the flag is set no later audio block touches the overlay. */
void *pa_gran_tape_borrow(void) {
    if (s_live) s_live->borrowed = 1u;
    return &s_ov;
}

void pa_gran_tape_release(void) {
    memset(&s_ov, 0, sizeof s_ov);
    if (s_live) { s_live->fx = 255u; s_live->borrowed = 0u; }       /* the engine restarts at the next block */
}

#endif /* PA_PRODUCT_ANDROMEDA */
