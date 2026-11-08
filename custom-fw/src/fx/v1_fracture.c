/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* src/fx/v1_fracture.c - ANDROMEDA V1 FRACTURE (see include/v1_fracture.h). Compiled only for Andromeda. */
#include "pa_config.h"
#if PA_PRODUCT_ANDROMEDA
#include <string.h>
#include <math.h>
#include "v1_fracture.h"

#define FRACTURE_GAIN 0.9f
#define GUARD         96u                    /* samples between a slice's end and the write head: more than a block */
#define PI_F          3.14159265f

static inline uint32_t rnd(fracture_t *f) { uint32_t x = f->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; f->rng = x; return x; }
static inline float    rnd01(fracture_t *f) { return (float)(rnd(f) >> 8) * (1.0f / 16777216.0f); }
static inline float    rnd11(fracture_t *f) { return 2.0f * rnd01(f) - 1.0f; }

static float grid_beats(int g) {                            /* a step in beats (a quarter note is one beat) */
    static const float k[FRACTURE_GRIDS] = { 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
    return k[g < 0 ? 0 : (g >= FRACTURE_GRIDS ? FRACTURE_GRIDS - 1 : g)];
}

void fracture_reset(fracture_t *f) {
    for (int i = 0; i < FRACTURE_VOICES; i++) f->v[i].active = 0;
    f->step = 0u; f->reps_left = 0; f->step_on = 0; f->rep_acc = 0.0f;
    f->phase = f->step_len;                                 /* the first step begins at once */
}

void fracture_set(fracture_t *f, const fracture_params_t *p) {
    const uint32_t old_seed = f->p.seed;
    f->p = *p;
    if (f->p.bpm < 40.0f) f->p.bpm = 40.0f; if (f->p.bpm > 240.0f) f->p.bpm = 240.0f;
    if (f->p.slice_ms < 5.0f) f->p.slice_ms = 5.0f; if (f->p.slice_ms > 250.0f) f->p.slice_ms = 250.0f;
    if (f->p.repeat < 1) f->p.repeat = 1; if (f->p.repeat > 8) f->p.repeat = 8;
    if (f->p.chaos < 0.0f) f->p.chaos = 0.0f; if (f->p.chaos > 1.0f) f->p.chaos = 1.0f;
    if (f->p.reverse < 0.0f) f->p.reverse = 0.0f; if (f->p.reverse > 1.0f) f->p.reverse = 1.0f;
    if (f->p.pitch < -24.0f) f->p.pitch = -24.0f; if (f->p.pitch > 24.0f) f->p.pitch = 24.0f;
    if (f->p.gate < 0.05f) f->p.gate = 0.05f; if (f->p.gate > 1.0f) f->p.gate = 1.0f;
    if (f->p.swing < 0.0f) f->p.swing = 0.0f; if (f->p.swing > 0.75f) f->p.swing = 0.75f;
    if (f->p.density < 0.01f) f->p.density = 0.01f; if (f->p.density > 1.0f) f->p.density = 1.0f;
    if (f->p.seed != old_seed) f->rng = f->p.seed ? f->p.seed : 0x9E3779B9u;
}

void fracture_init(fracture_t *f, float fs) {
    memset(f, 0, sizeof *f);
    f->fs = fs;
    f->p.bpm = 120.0f; f->p.grid = FRACTURE_GRID_16; f->p.slice_ms = 90.0f; f->p.repeat = 1; f->p.chaos = 0.2f; f->p.reverse = 0.15f;
    f->p.pitch = 0.0f; f->p.gate = 0.8f; f->p.swing = 0.0f; f->p.density = 0.75f; f->p.seed = 0x2545F491u;
    f->rng = f->p.seed;
    f->step_len = fs * 60.0f / f->p.bpm * grid_beats(f->p.grid);
    fracture_reset(f);
}

/* the slice a step takes from the recent past: where, how long, which way, how fast. Its repeats play THIS slice again, not a newer one. */
static void capture(fracture_t *f, const v1_hist_t *h) {
    const float fs = f->fs, chaos = f->p.chaos;
    float S = f->p.slice_ms * 0.001f * fs * (1.0f + 0.5f * chaos * rnd11(f));
    if (S < 0.005f * fs) S = 0.005f * fs;
    if (S > 0.25f * fs) S = 0.25f * fs;
    float back = S + (float)GUARD;                                                 /* the slice just before the head: a stutter of the last moment */
    if (rnd01(f) < chaos * 0.5f) back += rnd01(f) * (0.85f * (float)V1_HIST_LEN - S - 2.0f * (float)GUARD);   /* a random slice from further back */
    float semis = f->p.pitch;
    if (rnd01(f) < chaos * 0.3f) semis += rnd11(f) * 7.0f * chaos;                  /* a transposed slice now and then */
    f->cur.S = S; f->cur.a = h->head - (uint32_t)back; f->cur.ratio = exp2f(semis / 12.0f);
    f->cur.rev = rnd01(f) < f->p.reverse;
    f->cur.stop = rnd01(f) < chaos * 0.12f;                                          /* a tape stop */
    const float ang = (chaos * 0.7f * rnd11(f) + 1.0f) * (0.25f * PI_F);
    f->cur.gl = cosf(ang); f->cur.gr = sinf(ang);
}

/* the captured slice, into a free (or the shortest) voice, for `rep_len` samples of step */
static void spawn(fracture_t *f, float rep_len) {
    int vi = 0, best = 0x7FFFFFFF;
    for (int i = 0; i < FRACTURE_VOICES; i++) { if (!f->v[i].active) { vi = i; break; } if (f->v[i].left < best) { best = f->v[i].left; vi = i; } }
    fracture_voice_t *v = &f->v[vi];
    int total = (int)(f->p.gate * rep_len);
    if (total < 64) total = 64;
    v->span = (uint32_t)(f->cur.S * 65536.0f);
    v->walked = 0u;
    v->pos = f->cur.rev ? (f->cur.a + (uint32_t)f->cur.S) << 16 : f->cur.a << 16;
    v->ratio = f->cur.rev ? -f->cur.ratio : f->cur.ratio;
    v->stop_k = f->cur.stop ? (1.0f - 4.0f / (float)total) : 1.0f;                  /* the speed falls to about 2 % over the slice */
    v->left = v->total = total;
    v->gl = f->cur.gl; v->gr = f->cur.gr;
    v->active = 1;
}

void fracture_process(fracture_t *f, const v1_hist_t *h, float *wetL, float *wetR, int n) {
    if (n > FRACTURE_MAX_BLOCK) n = FRACTURE_MAX_BLOCK;
    const float base_len = f->fs * 60.0f / f->p.bpm * grid_beats(f->p.grid);
    const float sw = f->p.swing * 0.6667f;
    for (int i = 0; i < n; i++) {
        /* the clock: every second step is longer and the other shorter by the swing, so a pair is always two steps long */
        f->phase += 1.0f;
        if (f->phase >= f->step_len) {
            f->phase -= f->step_len;
            f->step++;
            f->step_len = base_len * ((f->step & 1u) ? (1.0f - sw) : (1.0f + sw));
            const float dens = f->p.density;
            int on = ((uint32_t)((float)(f->step + 1u) * dens) > (uint32_t)((float)f->step * dens));        /* evenly spread: the structure of the pattern */
            if (rnd01(f) < f->p.chaos * 0.4f) { on = !on; f->n_flipped++; }                                    /* chaos flips a step now and then */
            f->step_on = on; f->n_steps++;
            if (on) {
                f->n_steps_on++;
                f->reps_left = f->p.repeat - 1;
                f->rep_len = f->step_len / (float)f->p.repeat;
                f->rep_acc = 0.0f;
                capture(f, h);
                spawn(f, f->rep_len);
            } else f->reps_left = 0;
        } else if (f->step_on && f->reps_left > 0) {
            f->rep_acc += 1.0f;
            if (f->rep_acc >= f->rep_len) { f->rep_acc -= f->rep_len; f->reps_left--; spawn(f, f->rep_len); }   /* the stutter: the same slice again */
        }
        float l = 0.0f, r = 0.0f;
        for (int vi = 0; vi < FRACTURE_VOICES; vi++) {
            fracture_voice_t *v = &f->v[vi];
            if (!v->active) continue;
            const uint32_t idx = v->pos >> 16;
            const float fr = (float)(v->pos & 0xFFFFu) * (1.0f / 65536.0f);
            const float y0 = (float)h->buf[idx & V1_HIST_MASK], y1 = (float)h->buf[(idx + 1u) & V1_HIST_MASK];
            float y = (y0 + fr * (y1 - y0)) * (1.0f / 32768.0f);
            const int done = v->total - v->left;
            int fo = v->total / 3; if (fo > 64) fo = 64; if (fo < 1) fo = 1;
            float g = 1.0f;
            if (done < 64) g = (float)done * (1.0f / 64.0f);                                                      /* 1.5 ms in and out: a slice never clicks */
            if (v->left < fo) { const float go = (float)v->left / (float)fo; if (go < g) g = go; }
            if (v->span) {                                                                                       /* a micro-loop's seam: a 0.7 ms fade either side of the join */
                const uint32_t edge = 32u << 16;
                if (v->walked < edge && v->walked < v->span / 2u) { const float gw = (float)(v->walked >> 8) * (1.0f / (float)(edge >> 8)); if (gw < g) g = gw; }
                else if (v->span - v->walked < edge) { const float gw = (float)((v->span - v->walked) >> 8) * (1.0f / (float)(edge >> 8)); if (gw < g) g = gw; }
            }
            y *= g * FRACTURE_GAIN;
            l += y * v->gl; r += y * v->gr;
            v->ratio *= v->stop_k;
            const int32_t inc = (int32_t)(v->ratio * 65536.0f);
            v->pos += (uint32_t)inc;
            v->walked += (uint32_t)(inc < 0 ? -inc : inc);
            if (v->span && v->walked >= v->span) { v->walked -= v->span; v->pos -= (inc < 0) ? (uint32_t)(-(int32_t)v->span) : v->span; }      /* a short slice under a long gate loops */
            if (--v->left <= 0) v->active = 0;
        }
        wetL[i] = l; wetR[i] = r;
    }
}

#endif /* PA_PRODUCT_ANDROMEDA */
