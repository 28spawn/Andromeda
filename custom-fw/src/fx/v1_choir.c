/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* src/fx/v1_choir.c - ANDROMEDA V1 CHOIR (see include/v1_choir.h). Compiled only for Andromeda. */
#include "pa_config.h"
#if PA_PRODUCT_ANDROMEDA
#include <string.h>
#include <math.h>
#include "v1_choir.h"

#define CHOIR_GAIN   0.85f              /* overall wet level: set by test_choir.c so a held chord's choir is about as loud as the chord */
#define PI_F         3.14159265f
#define GUARD        160.0f             /* the least delay of a head, samples: more than a 64-frame block plus the interpolator's taps */
#define MARGIN       ((float)CHOIR_ALIGN)  /* added to every head's delay so that an alignment offset of -CHOIR_ALIGN still leaves the guard */
#define WIN_S        0.045f             /* the window of the pitch shifter, seconds */

static const int8_t  k_int[CHOIR_VOICINGS][CHOIR_MAX_VOICES] = { { 12, 0, 0, 0 }, { 7, 12, 0, 0 }, { 4, 7, 12, 0 }, { 3, 7, 12, 0 }, { -12, 7, 12, 19 }, { -2, 2, 7, 12 } };
static const uint8_t k_cnt[CHOIR_VOICINGS]                           = { 1, 2, 3, 3, 4, 4 };
static const float   k_det[CHOIR_MAX_VOICES]                         = { 1.0f, -1.0f, 0.6f, -0.6f };            /* each voice's share of DETUNE, and its direction */
static const float   k_vowel[CHOIR_MAX_VOICES][2]                    = { { 730.0f, 1090.0f }, { 570.0f, 840.0f }, { 530.0f, 1840.0f }, { 300.0f, 870.0f } };   /* ah, oh, eh, oo */

void choir_reset(choir_t *c) {
    for (int i = 0; i < CHOIR_MAX_VOICES; i++) {
        choir_voice_t *v = &c->v[i];
        v->phase = 0.25f * (float)i; v->env = 0.0f; v->offA = v->offB = 0.0f;
        v->f1_x1 = v->f1_x2 = v->f1_y1 = v->f1_y2 = v->f2_x1 = v->f2_x2 = v->f2_y1 = v->f2_y2 = 0.0f;
    }
    c->env_fast = c->env_slow = 0.0f; c->gate = 0.0f;
}

void choir_set(choir_t *c, const choir_params_t *p) {
    c->p = *p;
    int vc = c->p.voicing < 0 ? 0 : (c->p.voicing >= CHOIR_VOICINGS ? CHOIR_VOICINGS - 1 : c->p.voicing);
    int n = c->p.voices < 1 ? 1 : (c->p.voices > CHOIR_MAX_VOICES ? CHOIR_MAX_VOICES : c->p.voices);
    if (n > k_cnt[vc]) n = k_cnt[vc];
    c->n_active = n;
    const float fs = c->fs;
    const float det = c->p.detune_cents < 0.0f ? 0.0f : (c->p.detune_cents > 25.0f ? 25.0f : c->p.detune_cents);
    const float spread = c->p.spread < 0.0f ? 0.0f : (c->p.spread > 1.0f ? 1.0f : c->p.spread);
    const float att = c->p.attack_s < 0.002f ? 0.002f : c->p.attack_s;
    const float rel = c->p.release_s < 0.05f ? 0.05f : c->p.release_s;
    c->rel_k = 1.0f / (rel * fs);
    for (int i = 0; i < n; i++) {
        choir_voice_t *v = &c->v[i];
        v->ratio = exp2f(((float)k_int[vc][i] + k_det[i] * det * 0.01f) / 12.0f);        /* the interval in semitones plus the detune (cents / 100) */
        c->att_k[i] = 1.0f / (att * fs * (1.0f + 0.25f * (float)i));
        /* the voices spread across the stereo field, the first one and the last at the edges; one voice sits in the middle */
        const float pos = (n == 1) ? 0.0f : (-1.0f + 2.0f * (float)i / (float)(n - 1));
        const float ang = (spread * pos + 1.0f) * (0.25f * PI_F);
        v->gl = cosf(ang); v->gr = sinf(ang);
        /* formant band-passes (RBJ, 0 dB at the peak): this voice's vowel */
        for (int k = 0; k < 2; k++) {
            const float w0 = 2.0f * PI_F * k_vowel[i][k] / fs, al = sinf(w0) / (2.0f * 5.0f);
            const float a0 = 1.0f + al;
            v->b0[k] = al / a0; v->a1[k] = -2.0f * cosf(w0) / a0; v->a2[k] = (1.0f - al) / a0;
        }
    }
    c->level = CHOIR_GAIN / sqrtf((float)n);
}

void choir_init(choir_t *c, float fs) {
    memset(c, 0, sizeof *c);
    c->fs = fs; c->win = WIN_S * fs;
    c->p.voicing = CHOIR_MAJOR; c->p.voices = 3; c->p.detune_cents = 8.0f; c->p.attack_s = 0.15f; c->p.release_s = 0.8f; c->p.spread = 0.8f; c->p.formant = 0.3f;
    choir_reset(c);
    choir_set(c, &c->p);
}

/* Catmull-Rom between y1 and y2, t in [0,1) */
static inline float cubic(float y0, float y1, float y2, float y3, float t) {
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

/* The offset (samples, -CHOIR_ALIGN .. +CHOIR_ALIGN) for a head that wraps now: the one at which the waveform it will read (nominal delay d_nom + offset) best matches the
 * waveform the other head is reading (delay d_ref), compared over the ALIGN_LEN samples before the two read points, every other sample. Both heads then move at the same
 * rate, so the match holds through the crossfade. Normalised by the candidate's energy so a loud spot is not preferred for being loud. */
#define ALIGN_LEN 64
/* one candidate: the correlation with the reference (num) and the candidate's energy (en), over the window, every `ks`-th sample (ref holds every 2nd) */
static inline void align_eval(const int16_t *buf, uint32_t base, const float *ref, int d, int ks, float *num_o, float *en_o) {
    const uint32_t p = base - (uint32_t)d;
    float num = 0.0f, en = 1.0f;
    for (int k = 0; k < ALIGN_LEN; k += ks) { const float a = (float)buf[(p - (uint32_t)k) & V1_HIST_MASK]; num += a * ref[k >> 1]; en += a * a; }
    *num_o = num; *en_o = en;
}
/* num / sqrt(en) is the score; comparing num|num| / en gives the same order with no square root and no division (a search is a few hundred of these, in the audio interrupt) */
static inline int align_better(float n1, float e1, float n2, float e2) { return n1 * fabsf(n1) * e2 > n2 * fabsf(n2) * e1; }
/* Search in three steps, so a wrap costs about a third of what the plain scan did (it showed up as a 1.1 ms spike on the unit, in a block that otherwise takes 0.3 ms):
 * every 8th offset on every 4th sample of the window, then every 2nd offset around the best, then the offsets next to it, both on every 2nd sample. */
static float align_offset(const int16_t *buf, uint32_t base, float d_ref, float d_nom) {
    const int ir = (int)d_ref, in0 = (int)d_nom;
    float ref[ALIGN_LEN / 2];
    for (int k = 0; k < ALIGN_LEN; k += 2) ref[k >> 1] = (float)buf[(base - (uint32_t)ir - (uint32_t)k) & V1_HIST_MASK];
    float bn = 0.0f, be = 1.0f; int bd = -CHOIR_ALIGN;
    for (int dl = -CHOIR_ALIGN; dl <= CHOIR_ALIGN; dl += 8) {
        float n, e; align_eval(buf, base, ref, in0 + dl, 4, &n, &e);
        if (dl == -CHOIR_ALIGN || align_better(n, e, bn, be)) { bn = n; be = e; bd = dl; }
    }
    const int c0 = bd;
    align_eval(buf, base, ref, in0 + c0, 2, &bn, &be);                          /* the coarse winner, scored on the finer grid so the next ones compare with it */
    for (int dl = c0 - 6; dl <= c0 + 6; dl += 2) {
        if (dl == c0 || dl < -CHOIR_ALIGN || dl > CHOIR_ALIGN) continue;
        float n, e; align_eval(buf, base, ref, in0 + dl, 2, &n, &e);
        if (align_better(n, e, bn, be)) { bn = n; be = e; bd = dl; }
    }
    const int c1 = bd;
    for (int dl = c1 - 1; dl <= c1 + 1; dl += 2) {
        if (dl < -CHOIR_ALIGN || dl > CHOIR_ALIGN) continue;
        float n, e; align_eval(buf, base, ref, in0 + dl, 2, &n, &e);
        if (align_better(n, e, bn, be)) { bn = n; be = e; bd = dl; }
    }
    return (float)bd;
}

/* the history `d` samples behind `base`, d = integer + fraction: linear (cheap, the default) or cubic (hq) */
static inline float tap_lin(const int16_t *buf, uint32_t base, float d) {
    const int di = (int)d; const float df = d - (float)di;
    const uint32_t i1 = (base - (uint32_t)di) & V1_HIST_MASK;
    const float y1 = (float)buf[i1], y2 = (float)buf[(i1 - 1u) & V1_HIST_MASK];
    return (y1 + df * (y2 - y1)) * (1.0f / 32768.0f);
}
static inline float tap_cub(const int16_t *buf, uint32_t base, float d) {
    const int di = (int)d; const float df = d - (float)di;
    const uint32_t i1 = (base - (uint32_t)di) & V1_HIST_MASK;
    const float y0 = (float)buf[(i1 + 1u) & V1_HIST_MASK], y1 = (float)buf[i1], y2 = (float)buf[(i1 - 1u) & V1_HIST_MASK], y3 = (float)buf[(i1 - 2u) & V1_HIST_MASK];
    return cubic(y0, y1, y2, y3, df) * (1.0f / 32768.0f);
}

void choir_process(choir_t *c, const v1_hist_t *h, const float *in, float *wetL, float *wetR, int n) {
    if (n > CHOIR_MAX_BLOCK) n = CHOIR_MAX_BLOCK;
    /* the playing: a fast follower with a hysteresis gate drives the swell */
    for (int i = 0; i < n; i++) {
        const float ax = in[i] < 0.0f ? -in[i] : in[i];
        c->env_fast += (ax > c->env_fast ? 0.02f : 0.0004f) * (ax - c->env_fast);
    }
    if (c->env_fast > 0.006f) c->gate = 1.0f; else if (c->env_fast < 0.002f) c->gate = 0.0f;
    for (int i = 0; i < n; i++) { wetL[i] = 0.0f; wetR[i] = 0.0f; }
    const uint32_t base0 = h->head - (uint32_t)n;                 /* the block's first sample is at this index in the history */
    const float W = c->win, fa = c->p.formant < 0.0f ? 0.0f : (c->p.formant > 1.0f ? 1.0f : c->p.formant);
    const float lvl = c->level;
    const int hq = c->p.hq;
    for (int vi = 0; vi < c->n_active; vi++) {
        choir_voice_t *v = &c->v[vi];
        const float dphi = (1.0f - v->ratio) / W;
        const float att = c->att_k[vi], rel = c->rel_k;
        float phi = v->phase, env = v->env, offA = v->offA, offB = v->offB;
        float x11 = v->f1_x1, x12 = v->f1_x2, y11 = v->f1_y1, y12 = v->f1_y2, x21 = v->f2_x1, x22 = v->f2_x2, y21 = v->f2_y1, y22 = v->f2_y2;
        const float b01 = v->b0[0], a11 = v->a1[0], a21 = v->a2[0], b02 = v->b0[1], a12 = v->a1[1], a22 = v->a2[1];
        for (int i = 0; i < n; i++) {
            const uint32_t base = base0 + (uint32_t)i;
            float pb = phi + 0.5f; if (pb >= 1.0f) pb -= 1.0f;
            const float gA = 1.0f - fabsf(2.0f * phi - 1.0f);                      /* triangle: 0 where head A wraps, 1 half a window later */
            const float dA = GUARD + MARGIN + phi * W + offA, dB = GUARD + MARGIN + pb * W + offB;
            float s = hq ? gA * tap_cub(h->buf, base, dA) + (1.0f - gA) * tap_cub(h->buf, base, dB) : gA * tap_lin(h->buf, base, dA) + (1.0f - gA) * tap_lin(h->buf, base, dB);
            const float phi_old = phi;
            phi += dphi; if (phi >= 1.0f) phi -= 1.0f; else if (phi < 0.0f) phi += 1.0f;
            {   /* a head that wraps has gain 0 for that moment: it takes the offset that lines it up with the head that is playing */
                const float dd = phi - phi_old;
                if (dd > 0.5f || dd < -0.5f) {                                    /* head A wrapped */
                    offA = align_offset(h->buf, base, dB, GUARD + MARGIN + phi * W);
                } else if ((phi_old < 0.5f) != (phi < 0.5f)) {                      /* head B wrapped (A is at the half window) */
                    float pb2 = phi + 0.5f; if (pb2 >= 1.0f) pb2 -= 1.0f;
                    offB = align_offset(h->buf, base, dA, GUARD + MARGIN + pb2 * W);
                }
            }
            if (c->gate > 0.5f) { env += att; if (env > 1.0f) env = 1.0f; } else { env -= rel; if (env < 0.0f) env = 0.0f; }
            if (fa > 0.0f) {                                                        /* two formants: the voice's vowel */
                const float o1 = b01 * (s - x12) - a11 * y11 - a21 * y12; x12 = x11; x11 = s; y12 = y11; y11 = o1;
                const float o2 = b02 * (s - x22) - a12 * y21 - a22 * y22; x22 = x21; x21 = s; y22 = y21; y21 = o2;
                s += fa * (2.2f * (o1 + 0.8f * o2) - s);
            }
            s *= env * lvl;
            wetL[i] += s * v->gl; wetR[i] += s * v->gr;
        }
        v->phase = phi; v->env = env; v->offA = offA; v->offB = offB;
        v->f1_x1 = x11; v->f1_x2 = x12; v->f1_y1 = y11; v->f1_y2 = y12; v->f2_x1 = x21; v->f2_x2 = x22; v->f2_y1 = y21; v->f2_y2 = y22;
    }
}

#endif /* PA_PRODUCT_ANDROMEDA */
