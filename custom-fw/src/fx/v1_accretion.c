/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* src/fx/v1_accretion.c - ANDROMEDA V1 ACCRETION (see include/v1_accretion.h). Compiled only for Andromeda. */
#include "pa_config.h"
#if PA_PRODUCT_ANDROMEDA
#include <string.h>
#include <math.h>
#include "v1_accretion.h"

#define PI_F 3.14159265f
#define SEAM_FADE 96u                         /* frames: the playback fades in and out over this at the loop seam (the tape itself is not touched) */

/* ---- IMA ADPCM ---------------------------------------------------------------------------------------------------------------------------------------------- */
static const int16_t k_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const int8_t k_idx[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

/* block layout: [0..1] predictor L (LE), [2] index L, [3] index R, [4..5] predictor R, [6..7] generation, [8..39] nibbles L, [40..71] nibbles R */
static inline int16_t rd16(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static inline void wr16(uint8_t *p, int v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF); }

static inline int dec_nib(int nib, int *pred, int *idx) {
    const int step = k_step[*idx];
    int d = step >> 3;
    if (nib & 4) d += step;
    if (nib & 2) d += step >> 1;
    if (nib & 1) d += step >> 2;
    int p = *pred + ((nib & 8) ? -d : d);
    if (p > 32767) p = 32767; if (p < -32768) p = -32768;
    int i = *idx + k_idx[nib & 7];
    if (i < 0) i = 0; if (i > 88) i = 88;
    *pred = p; *idx = i;
    return p;
}

void acc_blk_decode(const uint8_t *blk, int16_t out[2][ACC_BLK_FRAMES]) {
    for (int c = 0; c < 2; c++) {
        int pred = rd16(blk + (c ? 4 : 0)), idx = blk[c ? 3 : 2];
        if (idx > 88) idx = 88;
        const uint8_t *nb = blk + 8 + 32 * c;
        for (int i = 0; i < ACC_BLK_FRAMES; i += 2) {
            const uint8_t b = nb[i >> 1];
            out[c][i]     = (int16_t)dec_nib(b & 15, &pred, &idx);
            out[c][i + 1] = (int16_t)dec_nib(b >> 4, &pred, &idx);
        }
    }
}

static inline int enc_nib(int sample, int *pred, int *idx) {
    int diff = sample - *pred, sign = 0;
    if (diff < 0) { sign = 8; diff = -diff; }
    int step = k_step[*idx], delta = 0, vp = step >> 3;
    if (diff >= step) { delta = 4; diff -= step; vp += step; }
    step >>= 1; if (diff >= step) { delta |= 2; diff -= step; vp += step; }
    step >>= 1; if (diff >= step) { delta |= 1; vp += step; }
    int p = *pred + (sign ? -vp : vp);
    if (p > 32767) p = 32767; if (p < -32768) p = -32768;
    int i = *idx + k_idx[delta];
    if (i < 0) i = 0; if (i > 88) i = 88;
    *pred = p; *idx = i;
    return delta | sign;
}

void acc_blk_encode(uint8_t *blk, const int16_t in[2][ACC_BLK_FRAMES], const int start_pred[2], const int start_idx[2], int end_pred[2], int end_idx[2]) {
    for (int c = 0; c < 2; c++) {
        int pred = start_pred[c], idx = start_idx[c];
        wr16(blk + (c ? 4 : 0), pred); blk[c ? 3 : 2] = (uint8_t)idx;
        uint8_t *nb = blk + 8 + 32 * c;
        for (int i = 0; i < ACC_BLK_FRAMES; i += 2) {
            const int lo = enc_nib(in[c][i], &pred, &idx);
            const int hi = enc_nib(in[c][i + 1], &pred, &idx);
            nb[i >> 1] = (uint8_t)(lo | (hi << 4));
        }
        end_pred[c] = pred; end_idx[c] = idx;
    }
}

/* ---- pages -------------------------------------------------------------------------------------------------------------------------------------------------- */
static inline uint16_t gen_get(const uint8_t *blk) { return (uint16_t)(blk[6] | (blk[7] << 8)); }
static inline void gen_set(uint8_t *blk, uint16_t g) { blk[6] = (uint8_t)(g & 0xFF); blk[7] = (uint8_t)(g >> 8); }

int acc_block_published(const acc_t *a, int blk) { return (gen_get(a->tape[blk]) & 1u) == 0u; }

static void flush_page(acc_t *a, acc_page_t *p) {
    if (!p->valid || !p->dirty) return;
    uint8_t *blk = a->tape[p->blk];
    int sp[2], si[2], ep[2], ei[2];
    const int nblk = a->len ? (int)(a->len / ACC_BLK_FRAMES) : ACC_BLOCKS;
    if (a->chain_blk >= 0 && p->blk == (a->chain_blk + 1) % nblk) { sp[0] = a->chain_pred[0]; sp[1] = a->chain_pred[1]; si[0] = a->chain_idx[0]; si[1] = a->chain_idx[1]; }
    else { sp[0] = rd16(blk); sp[1] = rd16(blk + 4); si[0] = blk[2]; si[1] = blk[3]; }       /* a random-access rewrite keeps the block's own start state */
    const uint16_t g = gen_get(blk);
    gen_set(blk, (uint16_t)(g | 1u));                                                         /* odd: being rewritten */
    acc_blk_encode(blk, p->s, sp, si, ep, ei);
    gen_set(blk, (uint16_t)((g | 1u) + 1u));                                                  /* even again: published */
    a->chain_blk = p->blk; a->chain_pred[0] = ep[0]; a->chain_pred[1] = ep[1]; a->chain_idx[0] = ei[0]; a->chain_idx[1] = ei[1];
    p->dirty = 0; a->n_flushed++;
}

static acc_page_t *page_for(acc_t *a, int blk) {
    acc_page_t *victim = &a->pg[0];
    for (int i = 0; i < ACC_PAGES; i++) {
        acc_page_t *p = &a->pg[i];
        if (p->valid && p->blk == blk) { p->stamp = ++a->stamp; return p; }
        if (!p->valid) victim = p;
        else if (victim->valid && p->stamp < victim->stamp) victim = p;
    }
    if (victim->valid) flush_page(a, victim);
    for (int tries = 0; tries < 3; tries++) {                                                 /* a block being rewritten (odd generation) is read again */
        const uint16_t g0 = gen_get(a->tape[blk]);
        acc_blk_decode(a->tape[blk], victim->s);
        if (!(g0 & 1u) && g0 == gen_get(a->tape[blk])) break;
    }
    victim->blk = blk; victim->valid = 1; victim->dirty = 0; victim->stamp = ++a->stamp; a->n_decoded++;
    return victim;
}

static void flush_blk(acc_t *a, int blk) {
    for (int i = 0; i < ACC_PAGES; i++) if (a->pg[i].valid && a->pg[i].blk == blk) flush_page(a, &a->pg[i]);
}

static void la_drain(acc_t *a);
void acc_flush(acc_t *a) {
    la_drain(a);                                   /* what the limiter is still holding goes to the tape first */
    /* in block order, so the chain stays continuous */
    for (int b = 0; b < ACC_BLOCKS; b++) flush_blk(a, b);
}

/* ---- control ------------------------------------------------------------------------------------------------------------------------------------------------ */
static void la_reset(acc_t *a);
static void la_drain(acc_t *a);
static void clear_tape(acc_t *a) {
    la_reset(a);
    memset(a->tape, 0, sizeof a->tape);
    for (int i = 0; i < ACC_PAGES; i++) { a->pg[i].valid = 0; a->pg[i].dirty = 0; }
    a->chain_blk = -1; a->wr_blk = -1; a->len = 0; a->pos = 0; a->rec_frame = 0; a->lim_min = 1.0f;
    memset(a->lp, 0, sizeof a->lp); memset(a->dc_x, 0, sizeof a->dc_x); memset(a->dc_y, 0, sizeof a->dc_y);
}

void acc_init(acc_t *a, float fs) {
    memset(a, 0, sizeof *a);
    a->fs = fs; a->rng = 0x1B873593u;
    a->p.length_s = 2.0f; a->p.feedback = 0.85f; a->p.overdub = 0.8f; a->p.tone_hz = 8000.0f; a->p.speed = 1.0f;
    a->p.wow = 0.0f; a->p.flutter = 0.0f; a->p.degradation = 0.0f; a->p.width = 1.0f;
    a->chain_blk = -1; a->wr_blk = -1; a->mode = ACC_IDLE; a->lim_min = 1.0f; la_reset(a);
    a->wow_s = 0.0f; a->wow_c = 1.0f; a->flu_s = 0.0f; a->flu_c = 1.0f;
    a->wow_e = 2.0f * sinf(PI_F * 0.7f / fs); a->flu_e = 2.0f * sinf(PI_F * 7.1f / fs);
    acc_set(a, &a->p);
}

void acc_set(acc_t *a, const acc_params_t *p) {
    a->p = *p;
    float deg = p->degradation < 0.0f ? 0.0f : (p->degradation > 1.0f ? 1.0f : p->degradation);
    float fc = p->tone_hz * (1.0f - 0.75f * deg);
    if (fc < 100.0f) fc = 100.0f; if (fc > 0.45f * a->fs) fc = 0.45f * a->fs;
    a->lp_k = 1.0f - expf(-2.0f * PI_F * fc / a->fs);
    a->dc_k = 1.0f - 2.0f * PI_F * 2.0f / a->fs;          /* 2 Hz: it still zeroes DC every pass, but a recirculating high-pass must not eat the bass (20 Hz lost ~26 dB at 82 Hz over 100 passes) */
}

static void finish_rec(acc_t *a) {
    uint32_t len = (a->rec_frame + ACC_BLK_FRAMES - 1u) / ACC_BLK_FRAMES * ACC_BLK_FRAMES;
    if (len < ACC_BLK_FRAMES) len = ACC_BLK_FRAMES;
    if (len > ACC_MAX_FRAMES) len = ACC_MAX_FRAMES;
    a->len = len;
    if (a->wr_blk >= 0) flush_blk(a, a->wr_blk);
    a->wr_blk = -1; a->pos = 0; a->mode = ACC_PLAY;
}

void acc_command(acc_t *a, int cmd) {
    switch (cmd) {
    case ACC_CMD_RECPLAY:
        if (a->mode == ACC_REC) finish_rec(a);
        else { clear_tape(a); a->mode = ACC_REC; a->frozen = 0; }       /* from idle: record; from a loop: replace it */
        break;
    case ACC_CMD_OVERDUB:
        if (a->mode == ACC_PLAY) { a->mode = ACC_OVERDUB; }
        else if (a->mode == ACC_OVERDUB) { la_drain(a); if (a->wr_blk >= 0) flush_blk(a, a->wr_blk); a->wr_blk = -1; a->mode = ACC_PLAY; }
        break;
    case ACC_CMD_FREEZE:
        if (a->len) { la_drain(a); if (a->wr_blk >= 0) flush_blk(a, a->wr_blk); a->wr_blk = -1; a->frozen = !a->frozen; }
        break;
    case ACC_CMD_CLEAR:
        clear_tape(a); a->mode = ACC_IDLE; a->frozen = 0;
        break;
    }
}

/* ---- audio -------------------------------------------------------------------------------------------------------------------------------------------------- */
/* The layer path is linear at DEGRADATION 0 (a pass must cost nothing but the feedback gain and the tone); saturation blends in with the control. */
static inline float soft_sat(float x, float d, float deg) {
    float t = x * d;
    if (t > 3.0f) t = 3.0f; if (t < -3.0f) t = -3.0f;
    const float s = t * (27.0f + t * t) / (27.0f + 9.0f * t * t) / d;
    return x + deg * (s - x);
}

/* What the tape is asked to store can exceed full scale: new input on top of old layers (a second phrase at 0.9 over a loop already near 1.0 reached 1.4).
 * Measured against the linear ideal (the same scenario a quarter as loud, scaled up), distortion of the three ways to cope:
 *     hard clamp      -22 dB   (flat tops, baked into the tape and recirculated)
 *     soft knee       -22 dB   (no better)
 *     gain limiter    -29 dB   (turns the whole write down: one gain for both channels, the stereo image is kept) - but its gain follows each peak after it has
 *                              arrived, and in the full demo path (wow, flutter, saturation) it measured -25 dB: audible on loud plucks.
 * So the limiter looks ahead. Samples wait ACC_LA steps in a ring; a windowed maximum over the ring sees the peak coming; the gain comes down instantly in the
 * window, goes up slowly (LIM_RELEASE), and is then averaged over the window length, so it ramps down over the ACC_LA samples before the peak instead of
 * snapping at it. The delayed samples are written at the tape index they came with, so nothing shifts in the loop. A soft knee stays as a backstop. */
#ifndef LIM_THRESH
#define LIM_THRESH   0.90f
#endif
#ifndef LIM_RELEASE
#define LIM_RELEASE  3.0e-5f                     /* per tape sample: ~700 ms */
#endif
#ifndef BACK_KNEE
#define BACK_KNEE    0.95f
#endif
#ifndef BACK_CEIL
#define BACK_CEIL    0.995f
#endif
static inline float backstop(float v) {
    const float av = v < 0.0f ? -v : v;
    if (av <= BACK_KNEE) return v;
    float u = (av - BACK_KNEE) * (1.0f / (BACK_CEIL - BACK_KNEE));
    if (u > 3.0f) u = 3.0f;
    const float th = u * (27.0f + u * u) / (27.0f + 9.0f * u * u);            /* tanh(u), within 2% for u <= 3 */
    const float o = BACK_KNEE + (BACK_CEIL - BACK_KNEE) * th;
    return v < 0.0f ? -o : o;
}

static void commit(acc_t *a, uint32_t frame, float vL, float vR) {
    const int blk = (int)(frame / ACC_BLK_FRAMES), idx = (int)(frame % ACC_BLK_FRAMES);
    if (blk != a->wr_blk) { if (a->wr_blk >= 0) flush_blk(a, a->wr_blk); a->wr_blk = blk; }      /* leaving a block publishes it, in order */
    acc_page_t *pg = page_for(a, blk);
    pg->s[0][idx] = (int16_t)(backstop(vL) * 32767.0f);
    pg->s[1][idx] = (int16_t)(backstop(vR) * 32767.0f);
    pg->dirty = 1;
}

static void la_reset(acc_t *a) {
    for (int i = 0; i < ACC_LA; i++) { a->la_g[i] = 1.0f; a->la_p[i] = 0.0f; }
    a->la_gsum = (float)ACC_LA; a->g_rel = 1.0f; a->la_w = 0; a->la_n = 0; a->lim_g = 1.0f;
}

/* write what is still waiting (leaving overdub, freezing, flushing): at the gain the limiter has now */
static void la_drain(acc_t *a) {
    const int w = (a->la_n == ACC_LA) ? a->la_w : 0;
    const float gs = a->la_gsum / (float)ACC_LA;
    for (int k = 0; k < a->la_n; k++) { const int j = (w + k) % ACC_LA; commit(a, a->la_i[j], a->la_v[j][0] * gs, a->la_v[j][1] * gs); }
    a->la_n = 0; a->la_w = 0;
}

static void la_push(acc_t *a, uint32_t frame, const float v[2]) {
    const int w = a->la_w;
    const int have = (a->la_n == ACC_LA);
    float ov0 = 0.0f, ov1 = 0.0f; uint32_t oi = 0;
    if (have) { ov0 = a->la_v[w][0]; ov1 = a->la_v[w][1]; oi = a->la_i[w]; }                        /* the oldest: the one about to leave */
    const float av0 = v[0] < 0.0f ? -v[0] : v[0], av1 = v[1] < 0.0f ? -v[1] : v[1];
    const float pk = av0 > av1 ? av0 : av1;
    a->la_v[w][0] = v[0]; a->la_v[w][1] = v[1]; a->la_i[w] = frame; a->la_p[w] = pk;
    if (a->la_n < ACC_LA) a->la_n++;
    float pm = 0.0f;                                                                                  /* the largest peak in the window (O(window); a monotonic deque would make it O(1)) */
    for (int k = 0; k < ACC_LA; k++) if (a->la_p[k] > pm) pm = a->la_p[k];
    const float tg = pm > LIM_THRESH ? LIM_THRESH / pm : 1.0f;
    if (tg < a->g_rel) a->g_rel = tg; else a->g_rel += LIM_RELEASE * (1.0f - a->g_rel);              /* down at once (it is looking ahead), up slowly */
    a->la_gsum += a->g_rel - a->la_g[w]; a->la_g[w] = a->g_rel;                                       /* averaged over the window: a ramp, not a step */
    a->la_w = (w + 1) % ACC_LA;
    const float gs = a->la_gsum / (float)ACC_LA;
    a->lim_g = gs; if (gs < a->lim_min) a->lim_min = gs;
    if (have) commit(a, oi, ov0 * gs, ov1 * gs);
}

/* one tape sample passes the write head: old layers x feedback through the layer path, plus the new input, then the look-ahead limiter */
static void write_step(acc_t *a, uint32_t frame, float inL, float inR) {
    const int blk = (int)(frame / ACC_BLK_FRAMES), idx = (int)(frame % ACC_BLK_FRAMES);
    acc_page_t *pg = page_for(a, blk);
    const float oL = (float)pg->s[0][idx] * (1.0f / 32768.0f), oR = (float)pg->s[1][idx] * (1.0f / 32768.0f);
    const float c = 0.5f * (1.0f - (a->p.width < 0.0f ? 0.0f : (a->p.width > 1.0f ? 1.0f : a->p.width)));
    const float mix[2] = { (1.0f - c) * oL + c * oR, (1.0f - c) * oR + c * oL };
    const float in[2] = { inL, inR };
    const float fb = a->p.feedback < 0.0f ? 0.0f : (a->p.feedback > 0.98f ? 0.98f : a->p.feedback);
    const float deg = a->p.degradation < 0.0f ? 0.0f : (a->p.degradation > 1.0f ? 1.0f : a->p.degradation), d = 1.0f + 3.0f * deg;
    float v[2];
    for (int ch = 0; ch < 2; ch++) {
        float x = fb * mix[ch];
        const float hp = x - a->dc_x[ch] + a->dc_k * a->dc_y[ch];                       /* DC blocker: layers must not pile up an offset */
        a->dc_x[ch] = x; a->dc_y[ch] = hp; x = hp;
        a->lp[ch] += a->lp_k * (x - a->lp[ch]); x = a->lp[ch];                            /* tone: every pass is a little darker */
        x = soft_sat(x, d, deg);
        v[ch] = x + a->p.overdub * in[ch];
    }
    { const float pk = (v[0] < 0.0f ? -v[0] : v[0]) > (v[1] < 0.0f ? -v[1] : v[1]) ? (v[0] < 0.0f ? -v[0] : v[0]) : (v[1] < 0.0f ? -v[1] : v[1]);
      if (pk > a->wr_peak) a->wr_peak = pk;
      if (pk > 1.0f) a->n_limited++; }
    la_push(a, frame, v);
}

void acc_process(acc_t *a, const float *inL, const float *inR, float *wetL, float *wetR, int n) {
    if (n > ACC_MAX_BLOCK) n = ACC_MAX_BLOCK;
    for (int i = 0; i < n; i++) {
        float oL = 0.0f, oR = 0.0f;
        const float iL = inL[i], iR = inR[i];
        if (a->mode == ACC_REC) {
            const uint32_t f = a->rec_frame, blk = f / ACC_BLK_FRAMES, idx = f % ACC_BLK_FRAMES;
            if ((int)blk != a->wr_blk) { if (a->wr_blk >= 0) flush_blk(a, a->wr_blk); a->wr_blk = (int)blk; }
            acc_page_t *pg = page_for(a, (int)blk);
            float l = iL * 32767.0f, r = iR * 32767.0f;
            l = l > 32767.0f ? 32767.0f : (l < -32768.0f ? -32768.0f : l); r = r > 32767.0f ? 32767.0f : (r < -32768.0f ? -32768.0f : r);
            pg->s[0][idx] = (int16_t)l; pg->s[1][idx] = (int16_t)r; pg->dirty = 1;
            a->rec_frame++;
            uint32_t target = (uint32_t)(a->p.length_s * a->fs); if (target > ACC_MAX_FRAMES) target = ACC_MAX_FRAMES;
            if (a->rec_frame >= target) finish_rec(a);
        } else if ((a->mode == ACC_PLAY || a->mode == ACC_OVERDUB) && a->len) {
            /* the tape's speed: SPEED, wow (slow, with a random walk) and flutter (fast) */
            a->wow_s += a->wow_e * a->wow_c; a->wow_c -= a->wow_e * a->wow_s;
            a->flu_s += a->flu_e * a->flu_c; a->flu_c -= a->flu_e * a->flu_s;
            if (++a->walk_t >= 4800u) { a->walk_t = 0; a->rng ^= a->rng << 13; a->rng ^= a->rng >> 17; a->rng ^= a->rng << 5; a->walk = (float)(int32_t)a->rng / 2147483648.0f; }
            const float mod = 1.0f + 0.006f * a->p.wow * (0.7f * a->wow_s + 0.3f * a->walk) + 0.0025f * a->p.flutter * a->flu_s;
            float sp = a->p.speed < 0.5f ? 0.5f : (a->p.speed > 2.0f ? 2.0f : a->p.speed);
            const uint32_t inc = (uint32_t)(sp * mod * (float)(1u << ACC_FRAC_BITS) + 0.5f);
            const uint32_t frame = a->pos >> ACC_FRAC_BITS, fr_i = a->pos & ((1u << ACC_FRAC_BITS) - 1u);
            const uint32_t next = (frame + 1u >= a->len) ? 0u : frame + 1u;
            const float fr = (float)fr_i * (1.0f / (float)(1u << ACC_FRAC_BITS));
            acc_page_t *p0 = page_for(a, (int)(frame / ACC_BLK_FRAMES));
            const float a0L = p0->s[0][frame % ACC_BLK_FRAMES], a0R = p0->s[1][frame % ACC_BLK_FRAMES];
            acc_page_t *p1 = page_for(a, (int)(next / ACC_BLK_FRAMES));
            const float a1L = p1->s[0][next % ACC_BLK_FRAMES], a1R = p1->s[1][next % ACC_BLK_FRAMES];
            uint32_t dist = frame < a->len - frame ? frame : a->len - frame;
            const float gf = dist >= SEAM_FADE ? 1.0f : (float)dist * (1.0f / (float)SEAM_FADE);
            oL = (a0L + (a1L - a0L) * fr) * (1.0f / 32768.0f) * gf;
            oR = (a0R + (a1R - a0R) * fr) * (1.0f / 32768.0f) * gf;
            const uint32_t p_old = a->pos, p_new = p_old + inc;                          /* unwrapped */
            if (a->mode == ACC_OVERDUB && !a->frozen) {
                for (uint32_t k = (p_old >> ACC_FRAC_BITS) + 1u; k <= (p_new >> ACC_FRAC_BITS); k++) {
                    const float t = (float)((k << ACC_FRAC_BITS) - p_old) / (float)inc;     /* when in this frame the head passes tape sample k */
                    write_step(a, k % a->len, a->in_prev[0] + (iL - a->in_prev[0]) * t, a->in_prev[1] + (iR - a->in_prev[1]) * t);
                }
            }
            const uint32_t end = a->len << ACC_FRAC_BITS;
            a->pos = p_new >= end ? p_new - end : p_new;
        }
        a->in_prev[0] = iL; a->in_prev[1] = iR;
        wetL[i] = oL; wetR[i] = oR;
    }
}

#endif /* PA_PRODUCT_ANDROMEDA */
