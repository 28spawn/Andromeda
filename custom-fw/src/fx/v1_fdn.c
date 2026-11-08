/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* src/fx/v1_fdn.c - ANDROMEDA V1 shared FDN reverb (see include/v1_fdn.h). Compiled only for Andromeda. */
#include "pa_config.h"
#if PA_PRODUCT_ANDROMEDA
#include <string.h>
#include <math.h>
#include "v1_fdn.h"

#define PI_F 3.14159265f
#define FDN_INJ_C 1.0f       /* injection constant: set by test_fdn.c so a steady input comes out at about its own level whatever the decay */

static const uint16_t k_len[FDN_LINES] = { 673, 751, 829, 911, 983, 1063, 1151, 1237 };   /* primes: no two lines share a common echo period */
static const uint16_t k_apl[2][4] = { { 41, 59, 83, 113 }, { 43, 61, 89, 127 } };
#define MOD_MAX_SAMPLES 10.0f                                                              /* peak delay swing at MOD = 1, in tank samples */
#define LEN_HEADROOM    14u                                                                /* the modulation never reads past the line's end */

/* signs that make the eight lines see, and give, different mixes of the two channels */
static const float k_inj_sign[FDN_LINES] = { 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f };
static const float k_out_sign[FDN_LINES] = { 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f };

void fdn_reset(fdn_t *f) {
    memset(f->tank, 0, sizeof f->tank); memset(f->ap, 0, sizeof f->ap); memset(f->pre, 0, sizeof f->pre);
    memset(f->lp, 0, sizeof f->lp); memset(f->itp_y, 0, sizeof f->itp_y);
    for (int i = 0; i < FDN_LINES; i++) f->prev_di[i] = -1; memset(f->dec_hist, 0, sizeof f->dec_hist); memset(f->itp_hist, 0, sizeof f->itp_hist);
    for (int i = 0; i < FDN_LINES; i++) f->w[i] = 0;
    for (int c = 0; c < 2; c++) for (int s = 0; s < 4; s++) f->ap_i[c][s] = 0;
    f->pre_w = 0; f->dec_i = 0; f->dec_phase = 0; f->itp_i = 0;
}

void fdn_init(fdn_t *f, float fs) {
    memset(f, 0, sizeof *f);
    f->fs = fs; f->fs_t = fs * 0.5f;
    uint16_t o = 0;
    for (int i = 0; i < FDN_LINES; i++) { f->off[i] = o; f->len_max[i] = k_len[i]; o = (uint16_t)(o + k_len[i]); }
    uint16_t a = 0;
    for (int c = 0; c < 2; c++) for (int s = 0; s < 4; s++) { f->ap_off[c][s] = a; f->ap_len[c][s] = k_apl[c][s]; a = (uint16_t)(a + k_apl[c][s]); }
    /* the half-band low-pass: 31 taps, h[0] = 0.5, odd taps sin(pi j / 2) / (pi j) under a Kaiser window (beta 8), normalised to unity at DC */
    float h[16]; float sum = 0.5f;
    for (int j = 0; j < 16; j++) h[j] = 0.0f;
    h[0] = 0.5f;
    for (int j = 1; j <= 15; j += 2) {
        const float x = (float)j / 16.0f; const float arg = 8.0f * sqrtf(1.0f - x * x);
        float i0 = 1.0f, t = 1.0f; for (int k = 1; k < 30; k++) { t *= (arg * 0.5f / (float)k) * (arg * 0.5f / (float)k); i0 += t; }
        float i0b = 1.0f; t = 1.0f; for (int k = 1; k < 30; k++) { t *= (4.0f / (float)k) * (4.0f / (float)k); i0b += t; }
        h[j] = ((j & 2) ? -1.0f : 1.0f) / (PI_F * (float)j) * (i0 / i0b);          /* sin(pi j / 2) = +1 for j = 1, 5, ...; -1 for j = 3, 7, ... */
        sum += 2.0f * h[j];
    }
    for (int j = 0; j < 16; j++) f->H[j] = h[j] / sum;
    for (int i = 0; i < FDN_LINES; i++) { const float ph = 2.0f * PI_F * (float)i / (float)FDN_LINES + 0.37f * (float)i; f->cph[i] = cosf(ph); f->sph[i] = sinf(ph); }
    f->lfo_s = 0.0f; f->lfo_c = 1.0f;
    f->p.size = 0.6f; f->p.decay_s = 3.0f; f->p.diffusion = 0.8f; f->p.damp_hz = 6000.0f; f->p.mod = 0.3f; f->p.mod_rate_hz = 0.4f; f->p.width = 1.0f; f->p.predelay_ms = 0.0f;
    for (int i = 0; i < FDN_LINES; i++) f->prev_di[i] = -1;
    f->started = 0;
    fdn_set(f, &f->p);
    f->started = 0;                        /* the first fdn_set from the caller snaps every smoothed value */
}

static void update_gains(fdn_t *f);
void fdn_set(fdn_t *f, const fdn_params_t *p) {
    f->p = *p;
    const float size = p->size < 0.0f ? 0.0f : (p->size > 1.0f ? 1.0f : p->size);
    for (int i = 0; i < FDN_LINES; i++) f->len_tgt[i] = (float)(f->len_max[i] - LEN_HEADROOM) * (0.18f + 0.82f * size);
    float pd = p->predelay_ms * 0.001f * f->fs_t; if (pd < 0.0f) pd = 0.0f; if (pd > (float)(FDN_PRE_MAX - 2)) pd = (float)(FDN_PRE_MAX - 2);
    f->pre_tgt = pd;
    float fc = p->damp_hz < 1000.0f ? 1000.0f : (p->damp_hz > 11000.0f ? 11000.0f : p->damp_hz);
    f->lp_a = expf(-2.0f * PI_F * fc / f->fs_t);
    f->ap_g = 0.7f * (p->diffusion < 0.0f ? 0.0f : (p->diffusion > 1.0f ? 1.0f : p->diffusion));
    f->mod_depth = MOD_MAX_SAMPLES * (p->mod < 0.0f ? 0.0f : (p->mod > 1.0f ? 1.0f : p->mod));
    float rate = p->mod_rate_hz < 0.02f ? 0.02f : (p->mod_rate_hz > 5.0f ? 5.0f : p->mod_rate_hz);
    f->lfo_e = 2.0f * sinf(PI_F * rate / f->fs_t);
    if (!f->started) {
        for (int i = 0; i < FDN_LINES; i++) f->len_cur[i] = f->len_tgt[i];
        f->pre_cur = f->pre_tgt; f->width_now = p->width; f->lp_a_cur = f->lp_a; f->mod_cur = f->mod_depth; f->decay_cur = f->p.decay_s; f->ap_g_cur = f->ap_g; f->width_prev = p->width; f->started = 1;
        update_gains(f);                              /* the first block starts at its own gains, not at zero */
        for (int i = 0; i < FDN_LINES; i++) f->g_end[i] = f->g[i];
        f->inj_end = f->inj;
    }
}

/* the per-line feedback gain for the decay time at the current lengths; and the input level that keeps the output near the input's level */
static void update_gains(fdn_t *f) {
    float t60 = f->decay_cur < 0.1f ? 0.1f : f->decay_cur, gsum = 0.0f;
    for (int i = 0; i < FDN_LINES; i++) { f->g[i] = expf(-6.9077553f * f->len_cur[i] / (t60 * f->fs_t)); gsum += f->g[i]; }
    const float gm = gsum / (float)FDN_LINES;
    /* sqrt(1 - g^2) keeps the low frequencies at the input's level whatever the decay; the longer tails are also darker (the damping acts on every pass),
     * which takes broadband level off, measured as ~1.3 / 2.9 / 5.3 dB at 0.5 / 3 / 12 s: a gentle log compensation puts it back */
    f->inj = FDN_INJ_C * sqrtf(1.0f - gm * gm) * (1.0f + 0.3f * logf(1.0f + t60));
}

void fdn_process(fdn_t *f, const float *inL, const float *inR, float *outL, float *outR, int n) {
    if (n > FDN_MAX_BLOCK) n = FDN_MAX_BLOCK;
    n &= ~1;
    /* controls that move gradually: line lengths (a length change is a pitch change, so it is slewed), the pre-delay, the width */
    float len_s[FDN_LINES], len_d[FDN_LINES];        /* the length at the start of the block and its per-sample step: a per-block step would be a delay jump */
    for (int i = 0; i < FDN_LINES; i++) {
        const float d = f->len_tgt[i] - f->len_cur[i], lim = 0.002f * (float)(n / 2);
        len_s[i] = f->len_cur[i];
        f->len_cur[i] += d > lim ? lim : (d < -lim ? -lim : d);
        len_d[i] = (f->len_cur[i] - len_s[i]) / (float)(n / 2);
    }
    {   /* decay, damping and modulation depth glide (a few percent a block): a step in any of them would step the feedback loop */
        f->decay_cur += 0.04f * (f->p.decay_s - f->decay_cur);
        f->lp_a_cur  += 0.10f * (f->lp_a - f->lp_a_cur);
        f->mod_cur   += 0.03f * (f->mod_depth - f->mod_cur);
    }
    const float apg_s = f->ap_g_cur;                                        /* the diffuser coefficient ramps across the block, from here to ... */
    f->ap_g_cur += 0.05f * (f->ap_g - f->ap_g_cur);
    const float apg_d = (f->ap_g_cur - apg_s) / (float)(n / 2);
    float apg = apg_s;
    update_gains(f);
    float gcur[FDN_LINES], dg[FDN_LINES]; const float inv = 1.0f / (float)(n / 2);
    for (int i = 0; i < FDN_LINES; i++) { gcur[i] = f->g_end[i]; dg[i] = (f->g[i] - f->g_end[i]) * inv; }
    float injcur = f->inj_end; const float dinj = (f->inj - f->inj_end) * inv;
    const float wtgt = f->p.width; f->width_prev = f->width_now; f->width_now += 0.1f * (wtgt - f->width_now);
    const float ad = f->lp_a_cur, bd = 1.0f - f->lp_a_cur;

    for (int m = 0; m < n / 2; m++) {
        /* 1. decimate: two input samples -> one tank sample, per channel */
        float x2[2];
        for (int c = 0; c < 2; c++) {
            const float *in = (c == 0) ? inL : inR;
            f->dec_hist[c][(f->dec_i) & 31] = in[2 * m];
            f->dec_hist[c][(f->dec_i + 1) & 31] = in[2 * m + 1];
        }
        {
            const uint8_t n1 = (uint8_t)(f->dec_i + 1);                /* index of the newest sample */
            for (int c = 0; c < 2; c++) {
                float y = f->H[0] * f->dec_hist[c][(n1 - 15) & 31];
                for (int j = 1; j <= 15; j += 2) y += f->H[j] * (f->dec_hist[c][(n1 - 15 - j) & 31] + f->dec_hist[c][(n1 - 15 + j) & 31]);
                x2[c] = y;
            }
            f->dec_i = (uint8_t)(f->dec_i + 2);
        }
        /* 2. pre-delay (int16 ring, linear fractional read; the delay slews at most 0.02 samples per sample so a knob move is a gentle bend, not a jump) */
        {
            const float d = f->pre_tgt - f->pre_cur, lim = 0.02f;
            f->pre_cur += d > lim ? lim : (d < -lim ? -lim : d);
        }
        float pl, pr;
        {
            for (int c = 0; c < 2; c++) {
                float v = x2[c] * 32767.0f; v = v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
                f->pre[c][f->pre_w] = (int16_t)v;
            }
            const int di = (int)f->pre_cur; const float fr = f->pre_cur - (float)di;
            int r0 = (int)f->pre_w - di; if (r0 < 0) r0 += FDN_PRE_MAX; int r1 = r0 - 1; if (r1 < 0) r1 += FDN_PRE_MAX;
            pl = ((float)f->pre[0][r0] + ((float)f->pre[0][r1] - (float)f->pre[0][r0]) * fr) * (1.0f / 32768.0f);
            pr = ((float)f->pre[1][r0] + ((float)f->pre[1][r1] - (float)f->pre[1][r0]) * fr) * (1.0f / 32768.0f);
            f->pre_w = (uint16_t)(f->pre_w + 1u); if (f->pre_w >= FDN_PRE_MAX) f->pre_w = 0;
        }
        /* 3. input diffusers: four Schroeder allpasses per channel, w = x + g wd, y = wd - g w */
        apg += apg_d;
        float ch[2] = { pl, pr };
        for (int c = 0; c < 2; c++) {
            float x = ch[c];
            for (int s = 0; s < 4; s++) {
                float *buf = &f->ap[f->ap_off[c][s]]; const uint16_t i = f->ap_i[c][s];
                const float wd = buf[i], w = x + apg * wd;
                x = wd - apg * w; buf[i] = w;
                f->ap_i[c][s] = (uint16_t)(i + 1u >= f->ap_len[c][s] ? 0u : i + 1u);
            }
            ch[c] = x;
        }
        /* 4. the tank */
        f->lfo_s += f->lfo_e * f->lfo_c; f->lfo_c -= f->lfo_e * f->lfo_s;      /* coupled oscillator */
        float d[FDN_LINES], fb[FDN_LINES], sum = 0.0f;
        for (int i = 0; i < FDN_LINES; i++) {
            const float modo = f->mod_cur * (f->lfo_s * f->cph[i] + f->lfo_c * f->sph[i]);
            len_s[i] += len_d[i];
            const float dly = len_s[i] + modo;
            /* the fractional part is kept in [0.5, 1.5): the first-order allpass interpolator is most accurate there. An allpass is flat in magnitude; a
             * linear interpolator is a low-pass (3 dB down at 6 kHz for a half-sample fraction, on every pass round the loop), which would be uncontrolled damping. */
            const int di = (int)(dly - 0.5f);
            const float *buf = &f->tank[f->off[i]]; const int L = f->len_max[i];
            /* when the integer part moves by one the allpass state belongs to the old alignment; for that one sample use the old alignment (the fraction is just outside
             * its best range, which is harmless) so the stream stays continuous; the next sample's recursion then follows the new alignment from this output */
            const int dd = (f->prev_di[i] >= 0 && f->prev_di[i] != di) ? f->prev_di[i] : di;
            f->prev_di[i] = (int16_t)di;
            const float fr = dly - (float)dd;
            const float eta = (1.0f - fr) / (1.0f + fr);
            int r0 = (int)f->w[i] - dd; if (r0 < 0) r0 += L; int r1 = r0 - 1; if (r1 < 0) r1 += L;
            const float v = eta * buf[r0] + buf[r1] - eta * f->itp_y[i];
            f->itp_y[i] = v;
            f->lp[i] = ad * f->lp[i] + bd * v;                                  /* damping: one pole, unity at DC */
            d[i] = f->lp[i];
            gcur[i] += dg[i];
            fb[i] = gcur[i] * d[i]; sum += fb[i];
        }
        injcur += dinj;
        const float hh = 0.25f * sum;                                           /* Householder: x - (2/N) sum(x), N = 8 */
        const float inj = injcur;
        float yl = 0.0f, yr = 0.0f;
        for (int i = 0; i < FDN_LINES; i++) {
            const float in = (i & 1) ? ch[1] : ch[0];
            f->tank[f->off[i] + f->w[i]] = fb[i] - hh + inj * k_inj_sign[i] * in;
            f->w[i] = (uint16_t)(f->w[i] + 1u >= f->len_max[i] ? 0u : f->w[i] + 1u);
            if (i & 1) yr += k_out_sign[i] * d[i]; else yl += k_out_sign[i] * d[i];
        }
        yl *= 0.5f; yr *= 0.5f;
        /* 5. interpolate back to 48 kHz: the half-band again, as two polyphase outputs per tank sample */
        const float u[2] = { yl, yr };
        for (int c = 0; c < 2; c++) {
            f->itp_hist[c][f->itp_i & 15] = u[c];
            const uint8_t im = f->itp_i;
            float ev = 0.0f;
            for (int q = -8; q <= 7; q++) { const int j = 2 * q + 1; const int aj = j < 0 ? -j : j; ev += f->H[aj] * f->itp_hist[c][(uint8_t)(im - (uint8_t)(8 + q)) & 15]; }
            float *out = (c == 0) ? outL : outR;
            out[2 * m]     = 2.0f * ev;
            out[2 * m + 1] = f->itp_hist[c][(uint8_t)(im - 7) & 15];
        }
        f->itp_i = (uint8_t)(f->itp_i + 1);
    }
    /* 6. width, mid/side */
    for (int i = 0; i < FDN_LINES; i++) f->g_end[i] = f->g[i];
    f->inj_end = f->inj;
    {   /* the width ramps across the block: a constant per block would step at every block boundary */
        float wd = f->width_prev; const float dw = (f->width_now - f->width_prev) / (float)n;
        for (int i = 0; i < n; i++) {
            wd += dw;
            const float mid = 0.5f * (outL[i] + outR[i]), side = 0.5f * (outL[i] - outR[i]);
            outL[i] = mid + wd * side; outR[i] = mid - wd * side;
        }
    }
}

#endif /* PA_PRODUCT_ANDROMEDA */
