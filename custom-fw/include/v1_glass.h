/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* include/v1_glass.h - ANDROMEDA V1 "GLASS": the guitar excites a bank of damped resonators, a tuned physical instrument
 * (crystal, bell, kalimba, metal, wood, marimba). Float32, rate-independent (pass the audio rate to glass_init).
 *
 * The engine is wet-only: it returns the resonator sound for a stereo input block. The caller mixes it with the dry signal and sends it to
 * the shared reverb (docs/ANDROMEDA-V1-PLAN.md). 12 modes x 2 channels; the right bank is detuned by up to ~6 cents so a mono guitar
 * still gives a stereo instrument. No sample data: the six materials are tables of frequency ratios, amplitudes and decay multipliers.
 *
 * The input does not run through the resonators continuously (that is a metallic filter, not a struck object). A transient detector
 * fires a short filtered-noise burst, and the modes ring. RESONANCE lets a little of the continuous signal in, for bowed / sustained use.
 */
#ifndef V1_GLASS_H
#define V1_GLASS_H

#include <stdint.h>

#define GLASS_MODES      12
#define GLASS_MATERIALS  6
#define GLASS_MAX_BLOCK  64
#define GLASS_LA         48                     /* the output limiter's look-ahead, samples (1 ms at 48 kHz): this is also the wet signal's latency */
#define GLASS_CEIL       0.45f                  /* the wet output never goes past this (-6.9 dBFS): the caller adds its mix gain on top */

enum { GLASS_CRYSTAL = 0, GLASS_BELL, GLASS_KALIMBA, GLASS_METAL, GLASS_WOOD, GLASS_MARIMBA };

typedef struct {
    int   material;      /* 0..5                                                   */
    float tune_semi;     /* -24 .. +24 semitones from A3 (220 Hz)                  */
    float strike;        /* 0..1   excitation intensity                            */
    float brightness;    /* 0..1   upper-mode energy and burst colour              */
    float decay_s;       /* 0.15 .. 12 s  (T60 of the fundamental)                 */
    float inharm;        /* 0..1   partial stretching                              */
    float resonance;     /* 0..1   continuous input into the modes                 */
    float spread;        /* 0..1   stereo distribution + right-bank detune         */
} glass_params_t;

typedef struct {
    float fs;
    glass_params_t p;
    int   dirty;
    /* coefficients: [channel][mode] */
    float a1[2][GLASS_MODES], a2[2][GLASS_MODES], g[2][GLASS_MODES], gc[2][GLASS_MODES], w[2][GLASS_MODES];   /* g: burst gain, gc: continuous-input gain */
    /* resonator state */
    float y1[2][GLASS_MODES], y2[2][GLASS_MODES];
    /* transient detector and burst */
    float env_fast, env_slow, hp_x1, hp_y1;
    float det_hp, det_af, det_rf, det_as;   /* detector coefficients (control rate) */
    int   lockout, lock_len;
    float burst_env[2], burst_dec, noise_lp[2], noise_coef;
    uint32_t rng[2];
    /* output limiter: repeated strikes add up inside the ringing modes (a note struck over its own ring), so the wet sum is limited, looking ahead */
    float lv[2][GLASS_LA], lpk[GLASS_LA], lg[GLASS_LA], lgsum, lrel, lpm;
    int   lw;
} glass_t;

void glass_init(glass_t *s, float fs);
void glass_set(glass_t *s, const glass_params_t *p);
/* n <= GLASS_MAX_BLOCK stereo frames; wet output only (overwrites wetL / wetR) */
void glass_process(glass_t *s, const float *inL, const float *inR, float *wetL, float *wetR, int n);
void glass_reset(glass_t *s);
/* a struck excitation without the guitar's help (a key press): vel 0..1, same burst as a detected pluck */
void glass_strike(glass_t *s, float vel);

#endif
