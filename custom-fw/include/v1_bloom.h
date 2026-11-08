/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* include/v1_bloom.h - ANDROMEDA V1 "BLOOM": a guitar note grows into a wide, evolving pad. A cloud of overlapping grains reads the
 * recent past (reversed and/or pitch-warped), each grain windowed so it starts and ends silent, and a growth envelope swells the cloud
 * after every attack. Float32; wet output only (the caller mixes the dry guitar and feeds the shared FDN).
 *
 * Grain windows are made with recursive sine oscillators (a rise, an optional hold, a fall), so there are no window tables. A grain's whole
 * life is a few phase constants set at spawn.
 */
#ifndef V1_BLOOM_H
#define V1_BLOOM_H

#include <stdint.h>
#include "v1_hist.h"

#define BLOOM_MAX_GRAINS 8
#define BLOOM_MAX_BLOCK  64

enum { BLOOM_QUANT_OFF = 0, BLOOM_QUANT_SEMI, BLOOM_QUANT_MAJOR, BLOOM_QUANT_MINOR, BLOOM_QUANT_PENTA, BLOOM_QUANT_WHOLE };
enum { BLOOM_SHAPE_HANN = 0, BLOOM_SHAPE_TUKEY, BLOOM_SHAPE_ASYM };

typedef struct {
    float size_ms;       /* 60 .. 500   grain length                                   */
    int   density;       /* 1 .. 12     grains sounding at once (clamped to the voice limit) */
    float warp;          /* -12 .. +24  semitones                                      */
    float reverse;       /* 0 .. 1      probability that a grain plays backwards       */
    float bloom_s;       /* 0 .. 2      growth time after an attack                    */
    float tone_hz;       /* 200 .. 12000 wet low-pass                                  */
    float spread;        /* 0 .. 1      stereo distribution                            */
    float drift;         /* 0 .. 1      random pitch and timing movement               */
    int   quant;         /* BLOOM_QUANT_*  (applied to WARP)                           */
    int   shape;         /* BLOOM_SHAPE_*                                              */
    int   freeze;        /* 1: the history is not written (the caller's job) and the cloud holds at full level */
    int   hq;            /* 1: cubic interpolation, 0: linear                          */
    int   voices;        /* grain voice limit: 4 standard, up to BLOOM_MAX_GRAINS      */
} bloom_params_t;

typedef struct {
    uint32_t pos;                /* read cursor, 16.16 in the history ring (wraps by itself) */
    int32_t  inc;                /* 16.16 per sample, negative = reverse */
    int      seg;                /* 0 rise, 1 hold, 2 fall */
    uint32_t left;               /* samples left in this segment */
    uint32_t n_rise, n_hold, n_fall;
    float    s, c, e_rise, e_fall;       /* window oscillator: (sin, cos) of the phase and 2 sin(step / 2) per segment (coupled form: exact for tiny steps, unlike 2cos(step) in float32) */
    float    gl, gr;             /* constant-power pan */
    float    fade, fdec;         /* 1 / 1 normally; on a freeze every grain already playing fades out (exp), it was spawned under the moving-head rules */
    int      active;
} bloom_grain_t;

typedef struct {
    float fs;
    bloom_params_t p;
    bloom_grain_t gr[BLOOM_MAX_GRAINS];
    uint32_t rng;
    float spawn_acc;             /* samples since the last grain */
    float next_interval;
    float env_fast, env_slow, hp_x1, hp_y1; int lockout;     /* attack detector */
    float bloom_env, bloom_k, duck_step; int ducking;      /* an attack ducks the cloud over ~8 ms, then it grows again */
    float tone_k, lp_l, lp_r;
    float gain_smooth;
    /* statistics for the tests */
    uint32_t n_spawned, n_reversed, n_forced_pos;
    int      active_count;
} bloom_t;

void bloom_init(bloom_t *b, float fs);
void bloom_set(bloom_t *b, const bloom_params_t *p);
/* the history already holds this block (v1_hist_write first); n <= BLOOM_MAX_BLOCK; input is used only for the attack detector */
void bloom_process(bloom_t *b, const v1_hist_t *h, const float *in, float *wetL, float *wetR, int n);
void bloom_reset(bloom_t *b);

#endif
