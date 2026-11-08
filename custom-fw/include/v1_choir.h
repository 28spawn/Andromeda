/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* include/v1_choir.h - ANDROMEDA V1 "CHOIR": one guitar becomes an ensemble of harmonically related voices.
 *
 * Four pitch voices, each with TWO read heads that crossfade (the classic delay-line pitch shifter: the read point drifts through the shared history at
 * 1 - ratio samples per sample, two heads half a window apart take turns so the jump of the one that wraps is silent).
 * The two heads are ALIGNED: when a head wraps (its gain is 0) it picks the offset, within +-CHOIR_ALIGN samples, at which its waveform best matches the head that is
 * playing, so the crossfade adds two copies in phase instead of cancelling a steady note (a plain crossfade of two half-window-apart copies does that to any note
 * whose period divides the half window badly: the low strings). No pitch detection: it works on whatever is
 * played, a note or a chord. A voice's interval comes from the VOICING table; DETUNE pulls the voices a few cents apart for the ensemble's thickness;
 * ATTACK and RELEASE swell the voices in after an attack and let them go when the playing stops; FORMANT colours each voice with two band-pass formants, a
 * different vowel per voice (much cheaper than a spectral formant shifter, and it sounds like a choir). Wet only: the caller mixes and feeds the shared FDN.
 * Float32, reads the shared PCM history (v1_hist.h).
 */
#ifndef V1_CHOIR_H
#define V1_CHOIR_H

#include <stdint.h>
#include "v1_hist.h"

#define CHOIR_MAX_VOICES 4
#define CHOIR_MAX_BLOCK  64
#define CHOIR_ALIGN      300      /* the alignment search range in samples either way: more than half the period of the lowest guitar note (82 Hz = 537 samples) */
enum { CHOIR_OCTAVE = 0, CHOIR_FIFTH, CHOIR_MAJOR, CHOIR_MINOR, CHOIR_MASSIVE, CHOIR_CLUSTER, CHOIR_VOICINGS };

typedef struct {
    int   voicing;       /* CHOIR_*: the arrangement of intervals                                  */
    int   voices;        /* 1 .. 4   how many of the voicing's voices sound                         */
    float detune_cents;  /* 0 .. 25  ensemble thickness                                             */
    float attack_s;      /* 0 .. 1   voice swell after an attack                                    */
    float release_s;     /* 0.05 .. 3  voice fade when the playing stops                            */
    float spread;        /* 0 .. 1   stereo placement                                               */
    float formant;       /* 0 .. 1   voice colour (0 = none)                                        */
    int   hq;            /* 1: cubic interpolation (about twice the cost of the default linear)     */
} choir_params_t;

typedef struct {
    float phase;         /* 0 .. 1 position of head A in the window; head B is half a window away */
    float offA, offB;    /* each head's alignment offset in samples (+-CHOIR_ALIGN): chosen when the head wraps, while its gain is 0 */
    float ratio;         /* the voice's pitch ratio with its detune                                */
    float env;           /* the swell, 0 .. 1                                                      */
    float gl, gr;        /* constant-power pan, with the level of the voice                        */
    float f1_x1, f1_x2, f1_y1, f1_y2, f2_x1, f2_x2, f2_y1, f2_y2;       /* the two formant band-pass filters */
    float b0[2], a1[2], a2[2];                                           /* their coefficients (b1 = 0, b2 = -b0)  */
} choir_voice_t;

typedef struct {
    float fs;
    choir_params_t p;
    choir_voice_t v[CHOIR_MAX_VOICES];
    int   n_active;
    float win;                   /* the window in samples */
    float env_fast, env_slow;    /* the input follower */
    float gate;                  /* 0 / 1: the playing is on */
    float att_k[CHOIR_MAX_VOICES], rel_k;
    float level;                 /* 1 / sqrt(active voices), smoothed */
} choir_t;

void choir_init(choir_t *c, float fs);
void choir_set(choir_t *c, const choir_params_t *p);
/* the history already holds this block (v1_hist_write first); n <= CHOIR_MAX_BLOCK; the input only drives the swell */
void choir_process(choir_t *c, const v1_hist_t *h, const float *in, float *wetL, float *wetR, int n);
void choir_reset(choir_t *c);

#endif
