/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* src/fx/v1_constellation.c - ANDROMEDA V1 CONSTELLATION (see include/v1_constellation.h). Compiled only for Andromeda. */
#include "pa_config.h"
#if PA_PRODUCT_ANDROMEDA
#include <string.h>
#include <math.h>
#include "v1_constellation.h"

#define CONST_GAIN 2.2f       /* set by test_constellation.c: grains are windowed and sparse, so the sequence needs this to sit near the playing */
#define GUARD      96u
#define PI_F       3.14159265f

static inline uint32_t rnd(constellation_t *c) { uint32_t x = c->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; c->rng = x; return x; }
static inline float    rnd01(constellation_t *c) { return (float)(rnd(c) >> 8) * (1.0f / 16777216.0f); }
static inline uint32_t prnd(constellation_t *c) { uint32_t x = c->pat_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; c->pat_rng = x; return x; }

static const uint8_t k_scale[CONST_SCALES][7] = {
    { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 5, 7, 9, 10 },
    { 0, 2, 4, 7, 9, 0, 0 },  { 0, 3, 5, 7, 10, 0, 0 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 6, 8, 10, 0 } };
static const uint8_t k_notes[CONST_SCALES] = { 7, 7, 7, 7, 5, 5, 7, 6 };

int constellation_semitones(int scale, int degree, int range) {
    if (scale < 0) scale = 0; if (scale >= CONST_SCALES) scale = CONST_SCALES - 1;
    if (range < 1) range = 1; if (range > 3) range = 3;
    const int n = k_notes[scale], total = n * range;
    degree = ((degree % total) + total) % total;
    return (int)k_scale[scale][degree % n] + 12 * (degree / n);
}

/* the pattern is generated from its number (and the seed): 16 steps of metadata, no table */
static void gen_pattern(constellation_t *c) {
    c->pat_rng = (uint32_t)(c->p.pattern + 1) * 2654435761u ^ c->p.seed ^ 0x9E3779B9u;
    if (!c->pat_rng) c->pat_rng = 1u;
    uint8_t rank[CONST_STEPS];
    for (int i = 0; i < CONST_STEPS; i++) rank[i] = (uint8_t)i;
    for (int i = CONST_STEPS - 1; i > 0; i--) { const int j = (int)(prnd(c) % (uint32_t)(i + 1)); const uint8_t t = rank[i]; rank[i] = rank[j]; rank[j] = t; }   /* a permutation: DENSITY then picks an exact number of steps */
    for (int i = 0; i < CONST_STEPS; i++) {
        const_step_t *s = &c->st[i];
        s->thr = (uint8_t)(rank[i] * 16 + (int)(prnd(c) % 16u));
        s->degree = (int8_t)(prnd(c) % 21u);
        s->gate = (uint8_t)(100u + prnd(c) % 140u);
        s->vel = (uint8_t)(140u + prnd(c) % 115u);
        s->prob = (uint8_t)(200u + prnd(c) % 56u);
        s->rev = (uint8_t)((prnd(c) % 100u) < 12u);
        s->back = (uint16_t)(100u + prnd(c) % 924u);
    }
}

void constellation_reset(constellation_t *c) {
    for (int i = 0; i < CONST_VOICES; i++) c->v[i].active = 0;
    c->step = 0u; c->walk = 0; c->pend_dir = 1; c->phase = c->step_len;
    c->rng = c->p.seed ? c->p.seed : 0x2545F491u;
    gen_pattern(c);
    c->n_triggers = c->n_mutations = 0u; c->log_n = 0u;
}

void constellation_set(constellation_t *c, const constellation_params_t *p) {
    const int regen = (p->pattern != c->p.pattern) || (p->seed != c->p.seed);
    c->p = *p;
    if (c->p.bpm < 40.0f) c->p.bpm = 40.0f; if (c->p.bpm > 240.0f) c->p.bpm = 240.0f;
    if (c->p.division < 0) c->p.division = 0; if (c->p.division >= CONST_DIVS) c->p.division = CONST_DIVS - 1;
    if (c->p.pattern < 0) c->p.pattern = 0; if (c->p.pattern > 15) c->p.pattern = 15;
    if (c->p.scale < 0) c->p.scale = 0; if (c->p.scale >= CONST_SCALES) c->p.scale = CONST_SCALES - 1;
    if (c->p.root < 0) c->p.root = 0; if (c->p.root > 11) c->p.root = 11;
    if (c->p.range < 1) c->p.range = 1; if (c->p.range > 3) c->p.range = 3;
    if (c->p.direction < 0) c->p.direction = 0; if (c->p.direction >= CONST_DIRS) c->p.direction = CONST_DIRS - 1;
    if (c->p.probability < 0.0f) c->p.probability = 0.0f; if (c->p.probability > 1.0f) c->p.probability = 1.0f;
    if (c->p.swing < 0.0f) c->p.swing = 0.0f; if (c->p.swing > 0.75f) c->p.swing = 0.75f;
    if (c->p.motion < 0.0f) c->p.motion = 0.0f; if (c->p.motion > 1.0f) c->p.motion = 1.0f;
    if (c->p.density < 0.0f) c->p.density = 0.0f; if (c->p.density > 1.0f) c->p.density = 1.0f;
    if (regen) { const uint32_t keep = c->rng; gen_pattern(c); c->rng = keep; }
}

void constellation_init(constellation_t *c, float fs) {
    memset(c, 0, sizeof *c);
    c->fs = fs;
    c->p.bpm = 110.0f; c->p.division = CONST_DIV_16; c->p.pattern = 0; c->p.scale = 0; c->p.root = 0; c->p.range = 2; c->p.direction = CONST_DIR_PROGRAMMED;
    c->p.probability = 0.9f; c->p.swing = 0.0f; c->p.motion = 0.3f; c->p.density = 0.6f; c->p.seed = 0x1F2E3D4Cu;
    static const float beats[CONST_DIVS] = { 1.0f, 0.5f, 0.25f, 0.125f };
    c->step_len = fs * 60.0f / c->p.bpm * beats[c->p.division];
    constellation_reset(c);
}

static int degree_for(constellation_t *c, int in_cycle) {
    const int n = k_notes[c->p.scale], total = n * c->p.range;
    switch (c->p.direction) {
        case CONST_DIR_UP:        return in_cycle % total;
        case CONST_DIR_DOWN:      return total - 1 - (in_cycle % total);
        case CONST_DIR_RANDOM:    return (int)(rnd(c) % (uint32_t)total);
        case CONST_DIR_PENDULUM: {
            if (total <= 1) return 0;
            const int d = c->walk;
            c->walk += c->pend_dir;
            if (c->walk >= total - 1) { c->walk = total - 1; c->pend_dir = -1; }
            if (c->walk <= 0)         { c->walk = 0;         c->pend_dir = 1; }
            return d;
        }
        default:                  return c->st[in_cycle].degree % total;
    }
}

static void spawn(constellation_t *c, const v1_hist_t *h, const const_step_t *s, int semis, float step_len) {
    const float fs = c->fs;
    const float ratio = exp2f((float)semis / 12.0f);
    int total = (int)((float)s->gate * (1.0f / 255.0f) * step_len);
    if (total < (int)(0.03f * fs)) total = (int)(0.03f * fs);
    if (total > (int)(1.2f * fs)) total = (int)(1.2f * fs);
    float back = (float)s->back * (1.0f / 1024.0f) * 1.2f * fs;
    const float need = ratio * (float)total + (float)GUARD;
    if (back < need) back = need;
    if (back > 0.9f * (float)V1_HIST_LEN) back = 0.9f * (float)V1_HIST_LEN;
    int vi = 0, best = 0x7FFFFFFF;
    for (int i = 0; i < CONST_VOICES; i++) { if (!c->v[i].active) { vi = i; break; } if (c->v[i].left < best) { best = c->v[i].left; vi = i; } }
    const_voice_t *v = &c->v[vi];
    const uint32_t a = h->head - (uint32_t)back;
    if (s->rev) { v->pos = (a + (uint32_t)(ratio * (float)total)) << 16; v->inc = -(int32_t)(ratio * 65536.0f); }
    else        { v->pos = a << 16;                                   v->inc = (int32_t)(ratio * 65536.0f); }
    v->left = v->total = total;
    v->amp = (float)s->vel * (1.0f / 255.0f);
    const float pan = ((c->step & 1u) ? 0.45f : -0.45f) * (0.5f + 0.5f * rnd01(c));
    const float ang = (pan + 1.0f) * (0.25f * PI_F);
    v->gl = cosf(ang); v->gr = sinf(ang);
    v->active = 1;
}

void constellation_process(constellation_t *c, const v1_hist_t *h, float *wetL, float *wetR, int n) {
    if (n > CONST_MAX_BLOCK) n = CONST_MAX_BLOCK;
    static const float beats[CONST_DIVS] = { 1.0f, 0.5f, 0.25f, 0.125f };
    const float base_len = c->fs * 60.0f / c->p.bpm * beats[c->p.division];
    const float sw = c->p.swing * 0.6667f;
    for (int i = 0; i < n; i++) {
        c->phase += 1.0f;
        if (c->phase >= c->step_len) {
            c->phase -= c->step_len;
            c->step++;
            c->step_len = base_len * ((c->step & 1u) ? (1.0f - sw) : (1.0f + sw));
            const int in_cycle = (int)(c->step % CONST_STEPS);
            const const_step_t *s = &c->st[in_cycle];
            const int degree = degree_for(c, in_cycle);
            /* PROBABILITY 1 always plays the steps DENSITY picked; below 1 each step has its own liking for dropping out */
            const float pr = (c->p.probability >= 0.999f) ? 1.0f : c->p.probability * (0.6f + 0.4f * (float)s->prob * (1.0f / 255.0f));
            const int on = ((float)s->thr < c->p.density * 256.0f) && (pr >= 1.0f || rnd01(c) < pr);
            if (on) {
                const int root = c->p.root > 6 ? c->p.root - 12 : c->p.root;
                const int semis = constellation_semitones(c->p.scale, degree, c->p.range) + root;
                spawn(c, h, s, semis, c->step_len);
                c->log_semi[c->log_n & 63u] = (int8_t)semis; c->log_n++; c->n_triggers++;
            }
            if (in_cycle == CONST_STEPS - 1 && rnd01(c) < c->p.motion) {                 /* the end of a cycle: the pattern may move */
                const int k = (int)(rnd(c) % CONST_STEPS);
                const_step_t *m = &c->st[k];
                switch (rnd(c) % 4u) {
                    case 0: m->degree = (int8_t)(m->degree + ((rnd(c) & 1u) ? 1 : -1)); if (m->degree < 0) m->degree = 0; if (m->degree > 20) m->degree = 20; break;
                    case 1: { const int j = (int)(rnd(c) % CONST_STEPS); const uint8_t t = m->thr; m->thr = c->st[j].thr; c->st[j].thr = t; break; }    /* swapping two steps' thresholds moves a hit, the density stays */
                    case 2: m->back = (uint16_t)(100u + rnd(c) % 924u); break;
                    default: m->rev = (uint8_t)!m->rev; break;
                }
                c->n_mutations++;
            }
        }
        float l = 0.0f, r = 0.0f;
        for (int vi = 0; vi < CONST_VOICES; vi++) {
            const_voice_t *v = &c->v[vi];
            if (!v->active) continue;
            const uint32_t idx = v->pos >> 16;
            const float fr = (float)(v->pos & 0xFFFFu) * (1.0f / 65536.0f);
            const float y0 = (float)h->buf[idx & V1_HIST_MASK], y1 = (float)h->buf[(idx + 1u) & V1_HIST_MASK];
            const float t = (float)(v->total - v->left) / (float)v->total;
            const float u = 1.0f - fabsf(2.0f * t - 1.0f);
            const float w = u * u * (3.0f - 2.0f * u);                                       /* a smooth hump: the grain starts and ends silent */
            const float y = (y0 + fr * (y1 - y0)) * (1.0f / 32768.0f) * w * v->amp * CONST_GAIN;
            l += y * v->gl; r += y * v->gr;
            v->pos += (uint32_t)v->inc;
            if (--v->left <= 0) v->active = 0;
        }
        wetL[i] = l; wetR[i] = r;
    }
}

#endif /* PA_PRODUCT_ANDROMEDA */
