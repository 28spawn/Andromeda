/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* src/fx/v1_glass.c - ANDROMEDA V1 GLASS (see include/v1_glass.h). Compiled only for Andromeda. */
#include "pa_config.h"
#if PA_PRODUCT_ANDROMEDA
#include <string.h>
#include <math.h>
#include "v1_glass.h"

typedef struct { float ratio, amp, dec; } glass_mode_t;

/* The six materials. ratio = partial / root, amp = relative level, dec = decay multiplier (1 = the DECAY knob's T60). Flash only. */
static const glass_mode_t k_mat[GLASS_MATERIALS][GLASS_MODES] = {
    /* CRYSTAL: bright, stable, nearly harmonic */
    { {1.000f,1.00f,1.00f}, {2.001f,0.80f,0.85f}, {3.003f,0.62f,0.72f}, {4.006f,0.50f,0.62f}, {5.010f,0.42f,0.54f}, {6.015f,0.35f,0.48f},
      {7.020f,0.30f,0.42f}, {8.030f,0.26f,0.38f}, {9.040f,0.22f,0.34f}, {10.06f,0.19f,0.31f}, {11.07f,0.17f,0.28f}, {12.09f,0.15f,0.26f} },
    /* BELL: hum, prime, tierce, quint, nominal... inharmonic, long */
    { {0.500f,0.70f,1.20f}, {1.000f,1.00f,1.00f}, {1.190f,0.75f,0.90f}, {1.500f,0.55f,0.80f}, {2.000f,0.60f,0.70f}, {2.500f,0.40f,0.60f},
      {2.970f,0.35f,0.52f}, {3.500f,0.28f,0.45f}, {4.100f,0.22f,0.38f}, {4.750f,0.18f,0.32f}, {5.430f,0.14f,0.27f}, {6.200f,0.11f,0.22f} },
    /* KALIMBA: strong fundamental, cantilever tine partials, a little wooden body */
    { {1.000f,1.00f,1.00f}, {2.000f,0.18f,0.50f}, {3.000f,0.10f,0.35f}, {6.270f,0.30f,0.30f}, {17.55f,0.10f,0.12f}, {0.000f,0,0},
      {0.000f,0,0},         {0.000f,0,0},         {0.000f,0,0},         {0.000f,0,0},         {0.000f,0,0},         {0.000f,0,0} },
    /* METAL: dense, inharmonic, long */
    { {1.000f,1.00f,1.00f}, {1.470f,0.85f,0.95f}, {1.980f,0.70f,0.90f}, {2.430f,0.62f,0.85f}, {3.070f,0.55f,0.80f}, {3.610f,0.48f,0.74f},
      {4.180f,0.42f,0.68f}, {4.840f,0.36f,0.62f}, {5.520f,0.30f,0.56f}, {6.310f,0.26f,0.50f}, {7.200f,0.22f,0.44f}, {8.100f,0.18f,0.38f} },
    /* WOOD: rounded, quickly damped */
    { {1.000f,1.00f,0.40f}, {2.570f,0.55f,0.28f}, {4.230f,0.30f,0.18f}, {6.110f,0.16f,0.12f}, {8.400f,0.08f,0.08f}, {0.000f,0,0},
      {0.000f,0,0},         {0.000f,0,0},         {0.000f,0,0},         {0.000f,0,0},         {0.000f,0,0},         {0.000f,0,0} },
    /* MARIMBA: tuned bar, 1 : 4 : 10 */
    { {1.000f,1.00f,1.00f}, {4.000f,0.35f,0.45f}, {9.930f,0.12f,0.22f}, {17.60f,0.04f,0.10f}, {0.000f,0,0}, {0.000f,0,0},
      {0.000f,0,0},         {0.000f,0,0},         {0.000f,0,0},         {0.000f,0,0},         {0.000f,0,0},         {0.000f,0,0} },
};

#define GLASS_ROOT_HZ   220.0f
#define GLASS_DENORM    1e-18f       /* a constant into every resonator: its state never decays into denormal numbers */
#define GLASS_GAIN      0.14f        /* overall wet level: a -12 dBFS guitar pluck gives wet peaks near -6 dBFS (test_glass.c) */
/* per-material trims, measured so the six materials play at the same loudness */
static const float k_trim[GLASS_MATERIALS] = { 1.06f, 1.00f, 0.67f, 1.00f, 1.10f, 0.62f };

static inline float glass_noise(uint32_t *r) {
    uint32_t x = *r; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *r = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

static void glass_limit_reset(glass_t *s) {
    memset(s->lv, 0, sizeof s->lv); memset(s->lpk, 0, sizeof s->lpk);
    for (int i = 0; i < GLASS_LA; i++) s->lg[i] = 1.0f;
    s->lgsum = (float)GLASS_LA; s->lrel = 1.0f; s->lpm = 0.0f; s->lw = 0;
}

/* The resonators are linear, but the strikes are not independent: a note struck while the last one still rings adds to it, and a run of notes at 2 per second
 * with a 3.5 s decay reached 1.0 (the single-pluck figure was 0.5). A look-ahead limiter (windowed peak, gain down at once, up slowly, then averaged over the
 * window so it is a ramp) keeps the wet sum under GLASS_CEIL without a flat top: the first note is untouched, the pile-up is turned down smoothly. */
#define GLASS_REL 2.0e-4f                                       /* per sample: ~100 ms */
static void glass_limit(glass_t *s, float *wl, float *wr, int n) {
    for (int i = 0; i < n; i++) {
        const int w = s->lw;
        const float oL = s->lv[0][w], oR = s->lv[1][w], op = s->lpk[w];
        const float aL = wl[i] < 0.0f ? -wl[i] : wl[i], aR = wr[i] < 0.0f ? -wr[i] : wr[i];
        const float pk = aL > aR ? aL : aR;
        s->lv[0][w] = wl[i]; s->lv[1][w] = wr[i]; s->lpk[w] = pk;
        if (pk >= s->lpm) s->lpm = pk;
        else if (op >= s->lpm) { float m = 0.0f; for (int k = 0; k < GLASS_LA; k++) if (s->lpk[k] > m) m = s->lpk[k]; s->lpm = m; }   /* the max just left: look again */
        const float pmx = s->lpm > op ? s->lpm : op;                      /* the sample leaving now still counts: the gain it gets must be low enough for it */
        const float tg = pmx > GLASS_CEIL ? GLASS_CEIL / pmx : 1.0f;
        if (tg < s->lrel) s->lrel = tg; else s->lrel += GLASS_REL * (1.0f - s->lrel);
        s->lgsum += s->lrel - s->lg[w]; s->lg[w] = s->lrel;
        s->lw = (w + 1 == GLASS_LA) ? 0 : w + 1;
        const float gs = s->lgsum * (1.0f / (float)GLASS_LA);
        wl[i] = oL * gs; wr[i] = oR * gs;
    }
}

void glass_reset(glass_t *s) {
    glass_limit_reset(s);
    memset(s->y1, 0, sizeof s->y1);
    memset(s->y2, 0, sizeof s->y2);
    s->env_fast = s->env_slow = 0.0f; s->hp_x1 = s->hp_y1 = 0.0f; s->lockout = 0;
    s->burst_env[0] = s->burst_env[1] = 0.0f; s->noise_lp[0] = s->noise_lp[1] = 0.0f;
}

void glass_init(glass_t *s, float fs) {
    memset(s, 0, sizeof *s);
    s->fs = fs;
    s->rng[0] = 0x9E3779B9u; s->rng[1] = 0x7F4A7C15u;
    s->p.material = GLASS_CRYSTAL; s->p.tune_semi = 0.0f; s->p.strike = 0.6f; s->p.brightness = 0.6f;
    s->p.decay_s = 2.0f; s->p.inharm = 0.0f; s->p.resonance = 0.0f; s->p.spread = 0.6f;
    glass_limit_reset(s);
    s->dirty = 3;
}

/* dirty is two bits: 1 = the shared constants and the left bank, 2 = the right bank. A block recomputes only one of them (glass_process), so a knob turned fast
 * never puts the whole table (about 60 libm calls per bank) into one audio block. */
void glass_set(glass_t *s, const glass_params_t *p) { s->p = *p; s->dirty = 3; }

/* Control rate (when a parameter changes, not per sample): the resonator coefficients of both banks. libm is fine here. */
static void glass_update(glass_t *s, int all) {
    const int do0 = (s->dirty & 1) != 0, do1 = (s->dirty & 2) != 0 && (all || !do0);
    const glass_params_t *p = &s->p;
    int mat = p->material; if (mat < 0) mat = 0; if (mat >= GLASS_MATERIALS) mat = GLASS_MATERIALS - 1;
    const float fs = s->fs;
    const float root = GLASS_ROOT_HZ * exp2f(p->tune_semi * (1.0f / 12.0f));
    const float t60 = p->decay_s < 0.05f ? 0.05f : p->decay_s;
    const float bright = p->brightness < 0.0f ? 0.0f : (p->brightness > 1.0f ? 1.0f : p->brightness);
    const float spread = p->spread < 0.0f ? 0.0f : (p->spread > 1.0f ? 1.0f : p->spread);
    const float inh = p->inharm < 0.0f ? 0.0f : (p->inharm > 1.0f ? 1.0f : p->inharm);
    /* brightness tilts the mode levels: 0 = the fundamental only, 1 = the table as written, and a little more */
    const float tilt = 2.2f * (1.0f - bright);           /* amp *= ratio^-tilt */

    float norm = 0.0f;
    for (int k = 0; k < GLASS_MODES; k++) norm += k_mat[mat][k].amp * powf(k_mat[mat][k].ratio > 0.0f ? k_mat[mat][k].ratio : 1.0f, -tilt);
    if (norm < 1e-3f) norm = 1.0f;

    for (int c = 0; c < 2; c++) {
        if (c == 0 ? !do0 : !do1) continue;
        for (int k = 0; k < GLASS_MODES; k++) {
            const glass_mode_t *m = &k_mat[mat][k];
            float ratio = m->ratio * sqrtf(1.0f + inh * 0.02f * m->ratio * m->ratio);
            /* the right bank: up to ~6 cents sharp or flat, a different amount for each mode */
            const float det = (c == 0) ? 0.0f : spread * 0.0012f * (1.0f + 0.12f * (float)k) * ((k & 1) ? -1.0f : 1.0f);   /* up to ~2 cents: a width, not a second instrument */
            const float f = root * ratio * (1.0f + det);
            if (m->amp <= 0.0f || f >= 0.45f * fs || f < 15.0f) { s->a1[c][k] = s->a2[c][k] = s->g[c][k] = s->gc[c][k] = 0.0f; continue; }
            const float wc = 6.2831853f * f / fs;
            /* DECAY is the fundamental's T60; a duller setting shortens the upper partials only */
            const float t60k = t60 * m->dec * (1.0f - 0.45f * (1.0f - bright) * (1.0f - 1.0f / (ratio > 1.0f ? ratio : 1.0f)));
            const float r = expf(-6.9077553f / (t60k * fs));
            s->a1[c][k] = 2.0f * r * cosf(wc);
            s->a2[c][k] = -r * r;
            const float a = m->amp * powf(m->ratio, -tilt) / norm;
            s->g[c][k]  = a * sinf(wc) * GLASS_GAIN * k_trim[mat];            /* sin(w): the impulse response then peaks at ~a, whatever the pitch */
            /* continuous input: scaled by the resonator's bandwidth (1 - r), so a sustained input near the mode gives ~a, not a / (1 - r) */
            s->gc[c][k] = 2.0f * a * (1.0f - r) * sinf(wc) * GLASS_GAIN * k_trim[mat];
        }
    }
    /* output weights: bank L feeds the left channel, bank R the right; even/odd modes lean to opposite sides as SPREAD grows */
    for (int c = 0; c < 2; c++) {
        if (c == 0 ? !do0 : !do1) continue;
        for (int k = 0; k < GLASS_MODES; k++) {
            const float lean = ((k & 1) ? 1.0f : -1.0f) * (c == 0 ? -1.0f : 1.0f);   /* +1: this mode is louder in this channel */
            s->w[c][k] = 1.0f + 0.65f * spread * lean;
        }
    }
    if (!do0) { s->dirty &= ~2; return; }                        /* only the right bank was due: the shared constants below were set with the left one */

    /* the burst: its colour follows BRIGHTNESS (a soft mallet to a hard pick), its length ~ 0.6 ms .. 2 ms */
    const float cutoff = 1500.0f + 12000.0f * bright;
    s->noise_coef = 1.0f - expf(-6.2831853f * cutoff / fs);
    s->det_hp = 1.0f - 6.2831853f * 100.0f / fs;                 /* detector: 100 Hz high-pass, 1 ms / 20 ms fast follower, 120 ms slow follower, 25 ms lockout */
    s->det_af = 1.0f - expf(-1.0f / (0.0010f * fs));
    s->det_rf = 1.0f - expf(-1.0f / (0.020f  * fs));
    s->det_as = 1.0f - expf(-1.0f / (0.120f  * fs));
    s->lock_len = (int)(0.025f * fs);
    const float tau = (0.0006f + 0.0014f * (1.0f - bright)) ;
    s->burst_dec = expf(-1.0f / (tau * fs));
    s->dirty &= ~(1 | (do1 ? 2 : 0));
}

void glass_strike(glass_t *s, float vel) {
    if (s->dirty) glass_update(s, 1);
    s->burst_env[0] = s->burst_env[1] = vel * (0.4f + 1.6f * s->p.strike);
    s->lockout = s->lock_len;
}

void glass_process(glass_t *s, const float *inL, const float *inR, float *wetL, float *wetR, int n) {
    if (s->dirty) glass_update(s, 0);
    if (n > GLASS_MAX_BLOCK) n = GLASS_MAX_BLOCK;
    /* 1. transient detector on the mono sum (after a 100 Hz high-pass: the detector should not follow hum and DC) */
    float exL[GLASS_MAX_BLOCK], exR[GLASS_MAX_BLOCK], coL[GLASS_MAX_BLOCK], coR[GLASS_MAX_BLOCK];
    const float hp_a = s->det_hp, af = s->det_af, rf = s->det_rf, as = s->det_as;
    const float gate = 0.004f;                                   /* about -48 dBFS */
    const int   lock_len = s->lock_len;
    const float strike = s->p.strike, reson = s->p.resonance;
    for (int i = 0; i < n; i++) {
        const float x = 0.5f * (inL[i] + inR[i]);
        const float hp = hp_a * (s->hp_y1 + x - s->hp_x1);        /* one-pole high-pass */
        s->hp_x1 = x; s->hp_y1 = hp;
        const float ax = hp < 0.0f ? -hp : hp;
        s->env_fast += (ax > s->env_fast ? af : rf) * (ax - s->env_fast);
        s->env_slow += as * (ax - s->env_slow);
        if (s->lockout > 0) s->lockout--;
        else if (s->env_fast > gate && s->env_fast > 1.6f * s->env_slow) {
            float vel = s->env_fast * 3.0f; if (vel > 1.0f) vel = 1.0f;
            s->burst_env[0] = s->burst_env[1] = vel * (0.4f + 1.6f * strike);
            s->lockout = lock_len;
        }
        /* the burst's noise is partly shared between the channels (a coherent strike) and partly independent (width) */
        const float shared = glass_noise(&s->rng[0]);
        float eL = 0.0f, eR = 0.0f;
        for (int c = 0; c < 2; c++) {
            const float nz = 0.65f * shared + 0.35f * glass_noise(&s->rng[1]);
            s->noise_lp[c] += s->noise_coef * (nz - s->noise_lp[c]);
            const float e = s->burst_env[c] * s->noise_lp[c];
            s->burst_env[c] *= s->burst_dec;
            if (c == 0) eL = e; else eR = e;
        }
        exL[i] = eL + GLASS_DENORM;                              /* the burst, and the anti-denormal constant */
        exR[i] = eR + GLASS_DENORM;
        coL[i] = reson * inL[i];                                 /* RESONANCE: the signal itself, through the continuous gain */
        coR[i] = reson * inR[i];
    }

    /* 2. resonators, mode by mode: a mode's state and coefficients stay in registers for the whole block */
    for (int i = 0; i < n; i++) { wetL[i] = 0.0f; wetR[i] = 0.0f; }
    for (int c = 0; c < 2; c++) {
        const float *ex = (c == 0) ? exL : exR;
        const float *co = (c == 0) ? coL : coR;
        float *out = (c == 0) ? wetL : wetR;
        for (int k = 0; k < GLASS_MODES; k++) {
            const float a1 = s->a1[c][k], a2 = s->a2[c][k], g = s->g[c][k], gc = s->gc[c][k], w = s->w[c][k];
            if (g == 0.0f) continue;
            float y1 = s->y1[c][k], y2 = s->y2[c][k];
            if (reson > 0.0f) {
                for (int i = 0; i < n; i++) {
                    const float y = a1 * y1 + a2 * y2 + g * ex[i] + gc * co[i];
                    y2 = y1; y1 = y;
                    out[i] += w * y;
                }
            } else {
                for (int i = 0; i < n; i++) {                    /* the usual case: struck, no continuous input */
                    const float y = a1 * y1 + a2 * y2 + g * ex[i];
                    y2 = y1; y1 = y;
                    out[i] += w * y;
                }
            }
            s->y1[c][k] = y1; s->y2[c][k] = y2;
        }
    }
    glass_limit(s, wetL, wetR, n);
}

#endif /* PA_PRODUCT_ANDROMEDA */
