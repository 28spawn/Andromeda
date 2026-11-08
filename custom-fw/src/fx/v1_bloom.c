/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* src/fx/v1_bloom.c - ANDROMEDA V1 BLOOM (see include/v1_bloom.h). Compiled only for Andromeda. */
#include "pa_config.h"
#if PA_PRODUCT_ANDROMEDA
#include <string.h>
#include <math.h>
#include "v1_bloom.h"

#define BLOOM_GAIN   0.90f        /* overall wet level: set by test_bloom.c so a held note's cloud is about as loud as the note */
#define PI_F         3.14159265f

static inline uint32_t rnd(bloom_t *b) { uint32_t x = b->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; b->rng = x; return x; }
static inline float    rnd01(bloom_t *b) { return (float)(rnd(b) >> 8) * (1.0f / 16777216.0f); }           /* [0, 1) */
static inline float    rnd11(bloom_t *b) { return 2.0f * rnd01(b) - 1.0f; }

void bloom_reset(bloom_t *b) {
    for (int i = 0; i < BLOOM_MAX_GRAINS; i++) b->gr[i].active = 0;
    b->spawn_acc = 0.0f; b->env_fast = b->env_slow = 0.0f; b->hp_x1 = b->hp_y1 = 0.0f; b->lockout = 0;
    b->bloom_env = 0.0f; b->lp_l = b->lp_r = 0.0f; b->active_count = 0;
}

void bloom_init(bloom_t *b, float fs) {
    memset(b, 0, sizeof *b);
    b->fs = fs; b->rng = 0x2545F491u;
    b->p.size_ms = 250.0f; b->p.density = 4; b->p.warp = 12.0f; b->p.reverse = 0.5f; b->p.bloom_s = 1.0f;
    b->p.tone_hz = 6000.0f; b->p.spread = 0.8f; b->p.drift = 0.3f; b->p.quant = BLOOM_QUANT_OFF;
    b->p.shape = BLOOM_SHAPE_HANN; b->p.freeze = 0; b->p.hq = 0; b->p.voices = 4;
    bloom_set(b, &b->p);
    b->next_interval = 0.25f * fs / 4.0f;
}

void bloom_set(bloom_t *b, const bloom_params_t *p) {
    if (p->freeze && !b->p.freeze) {               /* freeze engaged: the grains playing were placed for a moving head and could run into the frozen seam: fade them out (~8 ms) */
        const float fd = expf(-1.0f / (0.008f * b->fs));
        for (int i = 0; i < BLOOM_MAX_GRAINS; i++) if (b->gr[i].active) b->gr[i].fdec = fd;
    }
    b->p = *p;
    if (b->p.voices < 1) b->p.voices = 1;
    if (b->p.voices > BLOOM_MAX_GRAINS) b->p.voices = BLOOM_MAX_GRAINS;
    if (b->p.density < 1) b->p.density = 1;
    if (b->p.density > b->p.voices) b->p.density = b->p.voices;
    const float fc = b->p.tone_hz < 200.0f ? 200.0f : (b->p.tone_hz > 12000.0f ? 12000.0f : b->p.tone_hz);
    b->tone_k = 1.0f - expf(-2.0f * PI_F * fc / b->fs);
    b->bloom_k = b->p.bloom_s > 0.01f ? 1.0f / (b->p.bloom_s * b->fs / 3.0f) : 1.0f;     /* ~95 % grown after bloom_s seconds */
    b->duck_step = 1.0f / (0.008f * b->fs);
}

/* WARP after quantising: semitones, to the grid the player chose */
static float quantise(float semi, int mode) {
    static const uint16_t masks[6] = { 0xFFFu, 0xFFFu, 0xAB5u, 0x5ADu, 0x295u, 0x555u };   /* bit n set: semitone n of the octave is allowed */
    if (mode == BLOOM_QUANT_OFF) return semi;
    const float oct = floorf(semi / 12.0f);
    const float in_oct = semi - oct * 12.0f;
    float best = 0.0f, bd = 1e9f;
    for (int k = 0; k <= 12; k++) {
        if (!(masks[mode] & (1u << (k % 12)))) continue;
        const float d = fabsf(in_oct - (float)k);
        if (d < bd) { bd = d; best = (float)k; }
    }
    return oct * 12.0f + best;
}

static void spawn(bloom_t *b, const v1_hist_t *h) {
    bloom_grain_t *g = 0;
    for (int i = 0; i < b->p.voices; i++) if (!b->gr[i].active) { g = &b->gr[i]; break; }
    if (!g) return;
    const float fs = b->fs;
    const bloom_params_t *p = &b->p;

    /* pitch: the quantised WARP plus up to +-40 cents of drift */
    const float semi = quantise(p->warp, p->quant) + 0.40f * p->drift * rnd11(b);
    const float ratio = exp2f(semi * (1.0f / 12.0f));
    const int   rev = (rnd01(b) < p->reverse) ? 1 : 0;

    /* size, with a little random variation that grows with DRIFT, limited to what the ring can honour at this pitch and direction: a forward grain
     * gains (ratio - 1) x size on the head (all of ratio x size when the head is frozen); a reverse grain travels (ratio + 1) x size into older audio. */
    float size = (p->size_ms * 0.001f * fs) * (1.0f + 0.25f * p->drift * rnd11(b));
    {
        const float room = (float)V1_HIST_LEN - 2.0f * (float)V1_HIST_GUARD;
        const float per_sample = rev ? ratio + (p->freeze ? 0.0f : 1.0f) : (p->freeze ? ratio : ratio - 1.0f);
        if (per_sample > 1e-3f && size > room / per_sample) size = room / per_sample;
    }
    if (size < 64.0f) size = 64.0f;
    const uint32_t N = (uint32_t)size;

    /* where it reads: a random point between 60 ms and 0.7 s behind the head, then pushed back until the whole grain stays clear of the head (forward and
     * fast: the cursor gains on the head; reverse: it travels into older audio; frozen: the head does not move) */
    const float span = ratio * (float)N;
    float off = 0.06f * fs + rnd01(b) * (0.64f * fs);
    float need;
    if (rev) need = (float)V1_HIST_GUARD;
    else     need = (b->p.freeze ? span : (ratio - 1.0f) * (float)N) + (float)V1_HIST_GUARD;
    if (off < need) { off = need; b->n_forced_pos++; }
    float limit = (float)V1_HIST_LEN - (float)V1_HIST_GUARD - (rev ? (ratio + (b->p.freeze ? 0.0f : 1.0f)) * (float)N : 0.0f);
    if (limit < need) limit = need;
    if (off > limit) off = limit;
    const uint32_t start = (h->head - (uint32_t)off) & V1_HIST_MASK;
    g->pos = start << 16;
    int32_t inc = (int32_t)(ratio * 65536.0f);
    if (rev) inc = -inc;
    g->inc = inc;

    /* the window: rise / hold / fall, as sample counts */
    uint32_t nr, nh, nf;
    switch (p->shape) {
        case BLOOM_SHAPE_TUKEY: nr = N / 4u; nf = N / 4u; nh = N - nr - nf; break;
        case BLOOM_SHAPE_ASYM:  nr = (N * 2u) / 10u; nf = N - nr; nh = 0u; break;
        default:                nr = N / 2u; nf = N - nr; nh = 0u; break;
    }
    g->n_rise = nr; g->n_hold = nh; g->n_fall = nf;
    g->e_rise = nr ? 2.0f * sinf(0.25f * PI_F / (float)nr) : 0.0f;     /* phase 0 -> pi/2 over the rise */
    g->e_fall = nf ? 2.0f * sinf(0.25f * PI_F / (float)nf) : 0.0f;     /* pi/2 -> pi over the fall */
    g->seg = 0; g->left = nr;
    g->s = 0.0f; g->c = 1.0f;

    /* constant-power pan, SPREAD wide */
    const float phi = 0.25f * PI_F + p->spread * rnd11(b) * 0.25f * PI_F;
    g->gl = cosf(phi); g->gr = sinf(phi);
    g->fade = 1.0f; g->fdec = 1.0f;
    g->active = 1;
    b->n_spawned++; if (rev) b->n_reversed++;
}

/* one grain over [0, len) of the block, added into wl / wr */
static void render(bloom_grain_t *g, const int16_t *buf, float *wl, float *wr, int len, int hq) {
    int i = 0;
    while (i < len && g->active) {
        if (g->left == 0u) {                          /* next segment */
            if (g->seg == 0) { g->seg = 1; g->left = g->n_hold; g->s = 1.0f; g->c = 0.0f; }          /* the peak: resync exactly */
            else if (g->seg == 1) { g->seg = 2; g->left = g->n_fall; g->s = 1.0f; g->c = 0.0f; }
            else { g->active = 0; break; }
            continue;
        }
        int m = len - i; if ((uint32_t)m > g->left) m = (int)g->left;
        uint32_t pos = g->pos; const int32_t inc = g->inc;
        const float gl = g->gl, gr = g->gr, fdec = g->fdec; float fade = g->fade;
        if (g->seg == 1) {                            /* hold: window = 1 */
            for (int k = 0; k < m; k++) {
                const uint32_t idx = pos >> 16; const float f = (float)(pos & 0xFFFFu) * (1.0f / 65536.0f);
                float y;
                if (!hq) { const float a = buf[idx], c = buf[(idx + 1u) & V1_HIST_MASK]; y = a + (c - a) * f; }
                else {
                    const float ym = buf[(idx - 1u) & V1_HIST_MASK], y0 = buf[idx], y1 = buf[(idx + 1u) & V1_HIST_MASK], y2 = buf[(idx + 2u) & V1_HIST_MASK];
                    const float c1 = 0.5f * (y1 - ym), c2 = ym - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym) + 1.5f * (y0 - y1);
                    y = ((c3 * f + c2) * f + c1) * f + y0;
                }
                const float yf = y * fade; fade *= fdec;
                wl[i + k] += gl * yf; wr[i + k] += gr * yf;
                pos += (uint32_t)inc;
            }
        } else {                                      /* rise or fall: window = s^2, s by the sine recursion */
            const float e = (g->seg == 0) ? g->e_rise : g->e_fall;
            float s = g->s, c = g->c;
            for (int k = 0; k < m; k++) {
                const uint32_t idx = pos >> 16; const float f = (float)(pos & 0xFFFFu) * (1.0f / 65536.0f);
                float y;
                if (!hq) { const float a = buf[idx], c = buf[(idx + 1u) & V1_HIST_MASK]; y = a + (c - a) * f; }
                else {
                    const float ym = buf[(idx - 1u) & V1_HIST_MASK], y0 = buf[idx], y1 = buf[(idx + 1u) & V1_HIST_MASK], y2 = buf[(idx + 2u) & V1_HIST_MASK];
                    const float c1 = 0.5f * (y1 - ym), c2c = ym - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym) + 1.5f * (y0 - y1);
                    y = ((c3 * f + c2c) * f + c1) * f + y0;
                }
                const float w = s * s * fade; fade *= fdec;
                wl[i + k] += gl * w * y; wr[i + k] += gr * w * y;
                s += e * c; c -= e * s;                       /* the magic circle: a rotation by `step` per sample */
                pos += (uint32_t)inc;
            }
            g->s = s; g->c = c;
        }
        g->fade = fade;
        g->pos = pos; g->left -= (uint32_t)m; i += m;
        if (fade < 1e-3f) g->active = 0;
    }
}

void bloom_process(bloom_t *b, const v1_hist_t *h, const float *in, float *wetL, float *wetR, int n) {
    if (n > BLOOM_MAX_BLOCK) n = BLOOM_MAX_BLOCK;
    const float fs = b->fs;

    /* 1. attack detector (100 Hz high-passed): an attack restarts the growth envelope */
    const float hp_a = 1.0f - 2.0f * PI_F * 100.0f / fs;
    const float af = 1.0f - expf(-1.0f / (0.001f * fs)), rf = 1.0f - expf(-1.0f / (0.020f * fs)), as = 1.0f - expf(-1.0f / (0.120f * fs));
    int onset = 0;
    for (int i = 0; i < n; i++) {
        const float hp = hp_a * (b->hp_y1 + in[i] - b->hp_x1); b->hp_x1 = in[i]; b->hp_y1 = hp;
        const float ax = hp < 0.0f ? -hp : hp;
        b->env_fast += (ax > b->env_fast ? af : rf) * (ax - b->env_fast);
        b->env_slow += as * (ax - b->env_slow);
        if (b->lockout > 0) b->lockout--;
        else if (b->env_fast > 0.004f && b->env_fast > 1.6f * b->env_slow) { onset = 1; b->lockout = (int)(0.08f * fs); }
    }
    if (onset && !b->p.freeze && b->bloom_env > 0.001f) b->ducking = 1;     /* an attack: duck the cloud (smoothly), then let it grow again */

    /* 2. spawn: DENSITY grains alive at once means one new grain every size / density */
    const float size_s = b->p.size_ms * 0.001f * fs;
    b->spawn_acc += (float)n;
    while (b->spawn_acc >= b->next_interval) {
        b->spawn_acc -= b->next_interval;
        spawn(b, h);
        b->next_interval = (size_s / (float)b->p.density) * (1.0f + 0.5f * b->p.drift * rnd11(b));
        if (b->next_interval < 32.0f) b->next_interval = 32.0f;
    }

    /* 3. render the grains, voice by voice */
    float tl[BLOOM_MAX_BLOCK], tr[BLOOM_MAX_BLOCK];
    for (int i = 0; i < n; i++) { tl[i] = 0.0f; tr[i] = 0.0f; }
    int act = 0;
    for (int v = 0; v < b->p.voices; v++) if (b->gr[v].active) { render(&b->gr[v], h->buf, tl, tr, n, b->p.hq); act++; }
    b->active_count = act;

    /* 4. level: the grains are uncorrelated, so their sum grows as sqrt(n); divide by it (smoothed). The growth envelope and the wet low-pass last. */
    const float want = BLOOM_GAIN * (1.0f / 32768.0f) / sqrtf((float)(act > 1 ? act : 1) * 0.6f);
    const float g0 = b->gain_smooth;                     /* glide the level: the ramp runs across the block, so the gain never steps at a block boundary */
    b->gain_smooth += 0.15f * (want - b->gain_smooth);
    const float dg = (b->gain_smooth - g0) / (float)n;
    float gain = g0;
    for (int i = 0; i < n; i++) {
        gain += dg;
        if (b->ducking) { b->bloom_env -= b->duck_step; if (b->bloom_env <= 0.0f) { b->bloom_env = 0.0f; b->ducking = 0; } }
        else b->bloom_env += b->bloom_k * (1.0f - b->bloom_env);
        const float e = b->bloom_env * gain;
        b->lp_l += b->tone_k * (e * tl[i] - b->lp_l);
        b->lp_r += b->tone_k * (e * tr[i] - b->lp_r);
        wetL[i] = b->lp_l; wetR[i] = b->lp_r;
    }
}

#endif /* PA_PRODUCT_ANDROMEDA */
