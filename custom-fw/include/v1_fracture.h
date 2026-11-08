/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* include/v1_fracture.h - ANDROMEDA V1 "FRACTURE": the guitar becomes a rhythmic electronic instrument. It stutters, repeats, reverses and skips, in time with a tempo.
 *
 * A step clock (tempo and GRID) walks a pattern; each active step takes a SLICE of the recent past from the shared PCM history (no buffer of its own) and plays it with a
 * pitch, a direction and a gate. REPEAT retriggers the slice inside the step (a stutter / beat repeat), a slice longer than its gate is cut, a slice shorter than it
 * loops (a micro-loop). DENSITY decides which steps play, spread evenly; CHAOS only makes the choices vary (a step flips, a slice comes from further back, a
 * transposition, a tape stop), it never changes the underlying structure, so a preset stays playable. The same seed gives the same performance.
 * Float32, wet only: the caller mixes and feeds the shared FDN.
 */
#ifndef V1_FRACTURE_H
#define V1_FRACTURE_H

#include <stdint.h>
#include "v1_hist.h"

#define FRACTURE_MAX_BLOCK 64
#define FRACTURE_VOICES    3
enum { FRACTURE_GRID_4 = 0, FRACTURE_GRID_8, FRACTURE_GRID_16, FRACTURE_GRID_32, FRACTURE_GRID_64, FRACTURE_GRIDS };

typedef struct {
    float bpm;           /* 40 .. 240   the tempo (the unit's, or a MIDI clock's)               */
    int   grid;          /* FRACTURE_GRID_*  1/4 .. 1/64                                        */
    float slice_ms;      /* 5 .. 250    slice length                                            */
    int   repeat;        /* 1 .. 8      retriggers of the slice inside a step                   */
    float chaos;         /* 0 .. 1      variation                                               */
    float reverse;       /* 0 .. 1      probability that a slice plays backwards                */
    float pitch;         /* -24 .. +24  semitones, every slice                                  */
    float gate;          /* 0.05 .. 1   how much of the step a slice sounds                     */
    float swing;         /* 0 .. 0.75   rhythmic displacement of every second step              */
    float density;       /* 0.01 .. 1   how many steps play                                     */
    uint32_t seed;       /* the random seed: the same seed, the same performance                */
} fracture_params_t;

typedef struct {
    uint32_t pos;        /* read cursor, 16.16 in the history ring */
    float    ratio;      /* signed pitch ratio (negative: backwards) */
    float    stop_k;     /* per-sample factor on the ratio: 1 normally, < 1 for a tape stop */
    int32_t  left;       /* output samples left */
    int32_t  total;
    uint32_t span;       /* the slice's length, 16.16 (the loop point) */
    uint32_t walked;     /* how much of it has been read, 16.16 */
    float    gl, gr;
    int      active;
} fracture_voice_t;

typedef struct {
    float fs;
    fracture_params_t p;
    fracture_voice_t v[FRACTURE_VOICES];
    uint32_t rng;
    uint32_t step;       /* the step counter */
    float    phase;      /* samples since the step began */
    float    step_len;   /* the current step's length in samples (with the swing) */
    int      reps_left;  /* retriggers still to come in this step */
    float    rep_acc, rep_len;
    int      step_on;    /* this step plays */
    struct { uint32_t a; float S, ratio, gl, gr; int rev, stop; } cur;   /* the slice this step captured: its repeats play the same one */
    uint32_t n_steps_on, n_steps, n_flipped;   /* statistics for the tests */
} fracture_t;

void fracture_init(fracture_t *f, float fs);
void fracture_set(fracture_t *f, const fracture_params_t *p);
void fracture_reset(fracture_t *f);
/* the history already holds this block (v1_hist_write first); n <= FRACTURE_MAX_BLOCK */
void fracture_process(fracture_t *f, const v1_hist_t *h, float *wetL, float *wetR, int n);

#endif
