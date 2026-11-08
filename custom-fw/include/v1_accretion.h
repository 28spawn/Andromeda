/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* include/v1_accretion.h - ANDROMEDA V1 "ACCRETION": a stereo sound-on-sound tape. Record a phrase, overdub layers, let the layers age (feedback, tone,
 * wow and flutter, saturation), freeze the result into a texture.
 *
 * The long loop is IMA ADPCM, 4 bits per sample, independent left and right: 2,048 blocks of 64 stereo frames, 72 bytes each (8 header + 32 + 32 nibbles)
 * = 144 KiB = 131,072 frames = 2.73 s at 48 kHz. Every block carries its own predictor, step index and a generation counter, so any block decodes alone
 * (random access for reverse and varispeed playback) and a reader on another core can detect a block that was being rewritten (odd generation).
 * A small cache of decoded pages (int16) sits in front of the tape; the page under the write head is rewritten in place and encoded when the head leaves it.
 *
 * Write order matters: blocks are encoded in the order the head reaches them, each starting from the end state of the one before, so the block boundaries
 * are as smooth as the stream itself (no restart-from-header click at every 64 frames).
 *
 * The tape is one position with a read and a write head, like a real tape: SPEED moves both, the write head resamples the input to tape time. Float32 in
 * and out; the caller supplies the dry signal and the mix.
 */
#ifndef V1_ACCRETION_H
#define V1_ACCRETION_H

#include <stdint.h>

#define ACC_BLOCKS       2048
#define ACC_BLK_FRAMES   64
#define ACC_BLK_BYTES    72
#define ACC_MAX_FRAMES   (ACC_BLOCKS * ACC_BLK_FRAMES)
#define ACC_PAGES        6
#define ACC_FRAC_BITS    14                       /* tape position: frames.fraction, 17 + 14 = 31 bits */
#define ACC_MAX_BLOCK    64
#ifndef ACC_LA
#define ACC_LA           64                       /* the write limiter's look-ahead, in tape samples (1.33 ms at 48 kHz) */
#endif

enum { ACC_IDLE = 0, ACC_REC, ACC_PLAY, ACC_OVERDUB };
enum { ACC_CMD_RECPLAY = 0, ACC_CMD_OVERDUB, ACC_CMD_FREEZE, ACC_CMD_CLEAR };

typedef struct {
    float length_s;      /* 0.5 .. 2.7   recording stops by itself at this length (or when told to) */
    float feedback;      /* 0 .. 0.98    what an overdub pass keeps of the old layers */
    float overdub;       /* 0 .. 1       new input level */
    float tone_hz;       /* 100 .. 15000 low-pass in the layer path */
    float speed;         /* 0.5 .. 2     tape speed (playback and overdub) */
    float wow;           /* 0 .. 1       slow speed variation */
    float flutter;       /* 0 .. 1       fast speed variation */
    float degradation;   /* 0 .. 1       saturation and a darker, noisier layer path */
    float width;         /* 0 .. 1       1: left and right layers stay apart; 0: they feed each other into mono */
} acc_params_t;

typedef struct { int16_t s[2][ACC_BLK_FRAMES]; int blk; uint8_t valid, dirty; uint32_t stamp; } acc_page_t;

typedef struct {
    float fs;
    acc_params_t p;
    uint8_t tape[ACC_BLOCKS][ACC_BLK_BYTES];
    acc_page_t pg[ACC_PAGES];
    uint32_t stamp;
    int chain_blk, chain_pred[2], chain_idx[2];     /* end state of the block flushed last */
    int wr_blk;                                     /* the block the write head is in (-1: none) */
    int mode, frozen;
    uint32_t len;                                   /* loop length in frames (a multiple of 64), 0 = empty */
    uint32_t pos;                                   /* tape position, ACC_FRAC_BITS fraction */
    uint32_t rec_frame;                             /* REC: frames written */
    float in_prev[2];
    float lp[2], dc_x[2], dc_y[2];                  /* the layer path's filters, per channel */
    float lp_k, dc_k;
    float wow_s, wow_c, wow_e, flu_s, flu_c, flu_e, walk, walk_t; uint32_t rng;
    uint32_t n_flushed, n_decoded;                  /* statistics */
    uint32_t n_limited;                             /* tape samples whose sum went past full scale before limiting (a clip indicator for the screen) */
    float    wr_peak;                               /* largest sum offered to the tape since it was last cleared by the caller */
    /* the write limiter's look-ahead: samples wait here ACC_LA steps, so the gain can come down before the peak arrives */
    float    la_v[ACC_LA][2], la_p[ACC_LA], la_g[ACC_LA], la_gsum, g_rel;
    uint32_t la_i[ACC_LA]; int la_w, la_n;
    float    lim_g, lim_min;                        /* the write limiter's gain now, and its lowest since cleared (1.0 = untouched): the screen's gain-reduction meter */
} acc_t;

void acc_init(acc_t *a, float fs);
void acc_set(acc_t *a, const acc_params_t *p);
void acc_command(acc_t *a, int cmd);
void acc_process(acc_t *a, const float *inL, const float *inR, float *wetL, float *wetR, int n);
void acc_flush(acc_t *a);                           /* encode every dirty page (tests, and before a hand-off) */
int  acc_block_published(const acc_t *a, int blk);  /* 1 when the block's generation is even (not being rewritten) */
/* codec, exported for the tests */
void acc_blk_decode(const uint8_t *blk, int16_t out[2][ACC_BLK_FRAMES]);
void acc_blk_encode(uint8_t *blk, const int16_t in[2][ACC_BLK_FRAMES], const int start_pred[2], const int start_idx[2], int end_pred[2], int end_idx[2]);

#endif
