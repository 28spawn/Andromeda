/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* include/v1_constellation.h - ANDROMEDA V1 "CONSTELLATION": play one chord and the pedal generates an evolving melodic sequence from it. The melodic and rhythmic
 * counterpart of BLOOM: instead of one grain over and over, a 16-step sequencer picks different places in the recent past, pitches from a scale, and gates.
 *
 * The pattern is tiny metadata (16 steps of: threshold, scale degree, gate, velocity, probability, reverse, source position), generated from the PATTERN number, so
 * there is no stored table and no buffer of its own: the audio is the shared PCM history. DIRECTION chooses how the degrees are walked (up, down, random, pendulum,
 * or as programmed in the pattern). DENSITY decides how many steps play, PROBABILITY drops steps at random, MOTION slowly mutates the pattern between cycles. The key and
 * scale are chosen by hand (no chord recognition); the same seed gives the same sequence. Float32, wet only.
 */
#ifndef V1_CONSTELLATION_H
#define V1_CONSTELLATION_H

#include <stdint.h>
#include "v1_hist.h"

#define CONST_MAX_BLOCK 64
#define CONST_VOICES    4
#define CONST_STEPS     16
#define CONST_SCALES    8
enum { CONST_DIR_UP = 0, CONST_DIR_DOWN, CONST_DIR_RANDOM, CONST_DIR_PENDULUM, CONST_DIR_PROGRAMMED, CONST_DIRS };
enum { CONST_DIV_4 = 0, CONST_DIV_8, CONST_DIV_16, CONST_DIV_32, CONST_DIVS };

typedef struct {
    float bpm;           /* 40 .. 240   */
    int   division;      /* CONST_DIV_*: 1/4 .. 1/32 */
    int   pattern;       /* 0 .. 15     */
    int   scale;         /* 0 .. 7: major, minor, dorian, mixolydian, pentatonic major, pentatonic minor, lydian, whole tone */
    int   root;          /* 0 .. 11     the sequence is transposed by this many semitones (the tonic, relative to what you play) */
    int   range;         /* 1 .. 3 octaves */
    int   direction;     /* CONST_DIR_* */
    float probability;   /* 0 .. 1      */
    float swing;         /* 0 .. 0.75   */
    float motion;        /* 0 .. 1      how much the pattern mutates from cycle to cycle */
    float density;       /* 0 .. 1      how many of the 16 steps play */
    uint32_t seed;
} constellation_params_t;

typedef struct { uint8_t thr, gate, vel, prob; int8_t degree; uint8_t rev; uint16_t back; } const_step_t;     /* back: how far into the past the step plays from, in 1/1024 of 1.2 s */

typedef struct {
    uint32_t pos;        /* 16.16 in the history */
    int32_t  inc;
    int32_t  left, total;
    float    gl, gr, amp;
    int      active;
} const_voice_t;

typedef struct {
    float fs;
    constellation_params_t p;
    const_step_t st[CONST_STEPS];
    const_voice_t v[CONST_VOICES];
    uint32_t rng, pat_rng;
    uint32_t step;       /* counts steps */
    float    phase, step_len;
    int      pend_dir;   /* pendulum direction */
    int      walk;       /* the walked degree position */
    /* statistics and a log for the tests */
    uint32_t n_triggers, n_mutations;
    int8_t   log_semi[64];   uint32_t log_n;
} constellation_t;

void constellation_init(constellation_t *c, float fs);
void constellation_set(constellation_t *c, const constellation_params_t *p);
void constellation_reset(constellation_t *c);
void constellation_process(constellation_t *c, const v1_hist_t *h, float *wetL, float *wetR, int n);
/* the semitone offset of a degree in a scale (the walked position, wrapped to the range) */
int  constellation_semitones(int scale, int degree, int range);

#endif
