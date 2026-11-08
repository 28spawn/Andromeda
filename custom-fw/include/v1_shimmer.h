/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* include/v1_shimmer.h - ANDROMEDA V1 SHIMMER: pitch-shifted paths inside the shared reverb's feedback (the "pitch-shifted reverb cascade").
 *
 * The reverb's late output is pitch-shifted (up an octave by default, or a fifth, or an octave and a fifth) by a two-head delay-line shifter with an equal-power crossfade
 * (the signal is a dense wash, so there is no pitch to align), low-passed (BRIGHTNESS: each pass is higher, so each pass must be duller or the tail turns to glass), limited, and fed back
 * to the reverb's input one block later. Every recirculation climbs by the interval, so the tail rises as it decays. FEEDBACK sets how much comes round, the reverb's own DECAY how long it
 * lasts. Wraps an fdn_t: shimmer_process() replaces fdn_process(); with FEEDBACK 0 it is exactly the plain reverb. Float32.
 */
#ifndef V1_SHIMMER_H
#define V1_SHIMMER_H

#include <stdint.h>
#include "v1_fdn.h"

#define SHIM_RING 2048
#define SHIM_MASK (SHIM_RING - 1)
enum { SHIM_OCTAVE = 0, SHIM_FIFTH, SHIM_OCT_FIFTH, SHIM_NINETEEN, SHIM_INTERVALS };

typedef struct {
    int   interval;      /* SHIM_*: +12, +7, +12 and +7 together, +19 */
    float feedback;      /* 0 .. 1   how much of the shifted tail goes back in (0 = a plain reverb) */
    float bright_hz;     /* 1500 .. 12000  low-pass in the feedback path */
} shimmer_params_t;

typedef struct {
    float fs;
    shimmer_params_t p;
    float ring[2][SHIM_RING];
    uint32_t w;                         /* the ring's write index (both channels) */
    float phase[2];                     /* each path's head phase (a second path for OCT_FIFTH) */
    float ratio[2];
    int   n_paths;
    float lp[2];
    float lp_k;
    float fb[2][FDN_MAX_BLOCK];         /* what goes into the reverb in the next block */
    float win;
    float gain;                          /* feedback gain, scaled for safety */
    float decay_s;                       /* the reverb's decay the gain was computed for */
} shimmer_t;

void shimmer_init(shimmer_t *s, float fs);
void shimmer_set(shimmer_t *s, const shimmer_params_t *p);
void shimmer_reset(shimmer_t *s);
/* the reverb with the shimmer path round it: in -> fdn -> out; n <= FDN_MAX_BLOCK */
void shimmer_process(shimmer_t *s, fdn_t *f, const float *inL, const float *inR, float *outL, float *outR, int n);

#endif
