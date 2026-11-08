/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* src/fx/v1_shimmer.c - ANDROMEDA V1 SHIMMER (see include/v1_shimmer.h). Compiled only for Andromeda. */
#include "pa_config.h"
#if PA_PRODUCT_ANDROMEDA
#include <string.h>
#include <math.h>
#include "v1_shimmer.h"

#define GUARD     64.0f
#define WIN_S     0.040f
#define PATH_K    0.6f          /* the feedback gain at FEEDBACK 1 is PATH_K / DECAY: the longer the reverb rings, the more energy a recirculating wash gets back for what it sends, so the loop gain must fall with it */

void shimmer_reset(shimmer_t *s) {
    memset(s->ring, 0, sizeof s->ring); memset(s->fb, 0, sizeof s->fb);
    s->w = 0u; s->phase[0] = 0.0f; s->phase[1] = 0.33f; s->lp[0] = s->lp[1] = 0.0f;
}

/* the feedback gain follows the reverb's decay: called when either changes (shimmer_process checks it at the top of every block) */
static void shimmer_decay_changed(shimmer_t *s) {
    float dec = s->decay_s < 0.5f ? 0.5f : s->decay_s;
    float g = s->p.feedback * PATH_K / dec;
    if (g > 0.35f) g = 0.35f;
    s->gain = g * (s->n_paths == 2 ? 0.7f : 1.0f);
}

void shimmer_set(shimmer_t *s, const shimmer_params_t *p) {
    s->p = *p;
    if (s->p.interval < 0) s->p.interval = 0; if (s->p.interval >= SHIM_INTERVALS) s->p.interval = SHIM_INTERVALS - 1;
    if (s->p.feedback < 0.0f) s->p.feedback = 0.0f; if (s->p.feedback > 1.0f) s->p.feedback = 1.0f;
    if (s->p.bright_hz < 1500.0f) s->p.bright_hz = 1500.0f; if (s->p.bright_hz > 12000.0f) s->p.bright_hz = 12000.0f;
    switch (s->p.interval) {
        case SHIM_FIFTH:      s->ratio[0] = exp2f(7.0f / 12.0f);  s->n_paths = 1; break;
        case SHIM_OCT_FIFTH:  s->ratio[0] = 2.0f; s->ratio[1] = exp2f(7.0f / 12.0f); s->n_paths = 2; break;
        case SHIM_NINETEEN:   s->ratio[0] = exp2f(19.0f / 12.0f); s->n_paths = 1; break;
        default:              s->ratio[0] = 2.0f; s->n_paths = 1; break;
    }
    s->lp_k = 1.0f - expf(-6.2831853f * s->p.bright_hz / s->fs);
    shimmer_decay_changed(s);
}

void shimmer_init(shimmer_t *s, float fs) {
    memset(s, 0, sizeof *s);
    s->fs = fs; s->win = WIN_S * fs;
    s->p.interval = SHIM_OCTAVE; s->p.feedback = 0.0f; s->p.bright_hz = 5000.0f; s->decay_s = 3.0f;
    shimmer_reset(s);
    shimmer_set(s, &s->p);
}

void shimmer_process(shimmer_t *s, fdn_t *f, const float *inL, const float *inR, float *outL, float *outR, int n) {
    if (n > FDN_MAX_BLOCK) n = FDN_MAX_BLOCK;
    float aL[FDN_MAX_BLOCK], aR[FDN_MAX_BLOCK];
    if (f->p.decay_s != s->decay_s) { s->decay_s = f->p.decay_s; shimmer_decay_changed(s); }
    for (int i = 0; i < n; i++) { aL[i] = inL[i] + s->fb[0][i]; aR[i] = inR[i] + s->fb[1][i]; }       /* last block's shifted tail goes into the reverb */
    fdn_process(f, aL, aR, outL, outR, n);
    if (s->gain <= 0.0f) { memset(s->fb, 0, sizeof s->fb); return; }
    const float W = s->win;
    for (int i = 0; i < n; i++) {
        s->ring[0][s->w] = outL[i]; s->ring[1][s->w] = outR[i];
        float yl = 0.0f, yr = 0.0f;
        for (int k = 0; k < s->n_paths; k++) {
            const float phi = s->phase[k];
            float pb = phi + 0.5f; if (pb >= 1.0f) pb -= 1.0f;
            const float tri = 1.0f - fabsf(2.0f * phi - 1.0f);
            const float gA = sqrtf(tri), gB = sqrtf(1.0f - tri);                                      /* equal power: the two copies are not alike */
            const float da = GUARD + phi * W, db = GUARD + pb * W;
            for (int c = 0; c < 2; c++) {
                const float *r = s->ring[c];
                const int ia = (int)da, ib = (int)db; const float fa = da - (float)ia, fb2 = db - (float)ib;
                const float xa = r[(s->w - (uint32_t)ia) & SHIM_MASK], xa1 = r[(s->w - (uint32_t)ia - 1u) & SHIM_MASK];
                const float xb = r[(s->w - (uint32_t)ib) & SHIM_MASK], xb1 = r[(s->w - (uint32_t)ib - 1u) & SHIM_MASK];
                const float y = gA * (xa + fa * (xa1 - xa)) + gB * (xb + fb2 * (xb1 - xb));
                if (c == 0) yl += y; else yr += y;
            }
            float p2 = phi + (1.0f - s->ratio[k]) / W;
            if (p2 >= 1.0f) p2 -= 1.0f; else if (p2 < 0.0f) p2 += 1.0f;
            s->phase[k] = p2;
        }
        s->w = (s->w + 1u) & SHIM_MASK;
        s->lp[0] += s->lp_k * (yl - s->lp[0]); s->lp[1] += s->lp_k * (yr - s->lp[1]);                  /* every pass is duller */
        const float l = s->lp[0], r = s->lp[1];
        s->fb[0][i] = s->gain * l / (1.0f + 0.8f * fabsf(l));                                          /* and limited: it can never run away */
        s->fb[1][i] = s->gain * r / (1.0f + 0.8f * fabsf(r));
    }
}

#endif /* PA_PRODUCT_ANDROMEDA */
