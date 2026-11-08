/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* include/v1_fdn.h - ANDROMEDA V1 shared reverb: an 8-line stereo feedback delay network whose late tank runs at half the audio rate.
 *
 *   48 kHz stereo in -> half-band decimator -> stereo pre-delay (int16, 24 kHz) -> 4 allpass diffusers per channel -> 8-line tank at 24 kHz
 *   (Householder feedback, per-line damping, slow per-line modulation, stereo injection and decoding) -> half-band interpolator -> M/S width -> 48 kHz stereo out.
 *
 * Wet only: the caller mixes. BLOOM, GLASS, CHOIR and the rest feed this one engine (the "CLOUD" sound); SHIMMER (pitch paths inside the feedback) and the
 * 48 kHz early reflections are separate steps (docs/ANDROMEDA-V1-PLAN.md). Float32, rate-independent: pass the audio rate (it must be even-divisible: the tank runs at fs/2).
 *
 * Memory (the spec's 48 KiB): tank 7,598 floats 29.7 KiB, pre-delay 2 x 3,604 int16 14.1 KiB, diffusers 616 floats 2.4 KiB = 46.2 KiB, plus ~1 KiB of state.
 * SIZE therefore spans a room to a large hall (lines of 11-52 ms at 24 kHz); a very long DECAY, the diffusion and the modulation make the space "enormous".
 */
#ifndef V1_FDN_H
#define V1_FDN_H

#include <stdint.h>

#define FDN_LINES      8
#define FDN_TANK_TOTAL 7598
#define FDN_AP_TOTAL   616
#define FDN_PRE_MAX    3600          /* 150 ms at 24 kHz */
#define FDN_MAX_BLOCK  64

typedef struct {
    float size;          /* 0 .. 1      line lengths, small room .. large hall        */
    float decay_s;       /* 0.2 .. 25   T60 at low frequencies                        */
    float diffusion;     /* 0 .. 1      input allpass density                         */
    float damp_hz;       /* 1000 .. 16000  high-frequency damping in the tank         */
    float mod;           /* 0 .. 1      delay modulation depth                        */
    float mod_rate_hz;   /* 0.05 .. 3                                                  */
    float width;         /* 0 .. 1.5    1 = the tank's natural image                  */
    float predelay_ms;   /* 0 .. 150                                                   */
} fdn_params_t;

typedef struct {
    float fs, fs_t;
    fdn_params_t p;
    int   started;
    /* tank */
    float    tank[FDN_TANK_TOTAL];
    uint16_t off[FDN_LINES], len_max[FDN_LINES], w[FDN_LINES];
    float    len_cur[FDN_LINES], len_tgt[FDN_LINES], g[FDN_LINES], lp[FDN_LINES];
    float    lp_a, inj, ap_g, width_now;
    float    lp_a_cur, mod_cur, decay_cur, ap_g_cur, width_prev;     /* the smoothed values the audio uses: a control change must not step the loop */
    float    itp_y[FDN_LINES];                   /* allpass interpolator state per line */
    int16_t  prev_di[FDN_LINES];                 /* the integer delay used last sample (-1: none yet): a crossing is corrected for continuity */
    float    g_end[FDN_LINES], inj_end;          /* gains at the end of the previous block: they ramp to this block's across it */
    /* modulation: a coupled oscillator and each line's phase */
    float lfo_s, lfo_c, lfo_e, cph[FDN_LINES], sph[FDN_LINES], mod_depth;
    /* input diffusers: [channel][stage] */
    float    ap[FDN_AP_TOTAL];
    uint16_t ap_off[2][4], ap_len[2][4], ap_i[2][4];
    /* pre-delay */
    int16_t  pre[2][FDN_PRE_MAX + 4];
    uint16_t pre_w;
    float    pre_cur, pre_tgt;
    /* half-band resampling */
    float H[16];
    float dec_hist[2][32]; uint8_t dec_i; int dec_phase;
    float itp_hist[2][16]; uint8_t itp_i;
} fdn_t;

void fdn_init(fdn_t *f, float fs);
void fdn_set(fdn_t *f, const fdn_params_t *p);
void fdn_reset(fdn_t *f);
/* n frames (even, <= FDN_MAX_BLOCK); wet only */
void fdn_process(fdn_t *f, const float *inL, const float *inR, float *outL, float *outR, int n);

#endif
