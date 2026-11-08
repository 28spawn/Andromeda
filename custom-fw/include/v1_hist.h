/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* include/v1_hist.h - ANDROMEDA V1 shared PCM history: a mono int16 ring that every grain-based algorithm reads
 * (BLOOM, CHOIR, FRACTURE, CONSTELLATION). Header only.
 *
 * 2^16 samples = 128 KiB = 1.37 s at 48 kHz (1.49 s at 44.1 kHz). The ring is exactly 2^16 long so a 32-bit 16.16 read cursor wraps
 * round it by itself. The history is mono because the guitar is mono; a stereo copy for resampling the wet signal is a separate ring
 * (docs/ANDROMEDA-V1-PLAN.md section 2). The caller writes a block first, then lets the engines read it: a grain that stays GUARD samples behind
 * the head can never read what the block is writing.
 */
#ifndef V1_HIST_H
#define V1_HIST_H

#include <stdint.h>
#include <string.h>

#define V1_HIST_BITS   16u
#define V1_HIST_LEN    (1u << V1_HIST_BITS)
#define V1_HIST_MASK   (V1_HIST_LEN - 1u)
#define V1_HIST_GUARD  128u      /* samples kept between any read cursor and the head: more than a 64-frame block */

typedef struct {
    int16_t *buf;                /* V1_HIST_LEN samples, supplied by the caller (static SRAM on the device) */
    uint32_t head;               /* next write index */
} v1_hist_t;

static inline void v1_hist_init(v1_hist_t *h, int16_t *buf) {
    h->buf = buf; h->head = 0u;
    memset(buf, 0, V1_HIST_LEN * sizeof(int16_t));
}

/* append n mono samples (float, +-1.0 = full scale; clipped) */
static inline void v1_hist_write(v1_hist_t *h, const float *x, int n) {
    uint32_t hd = h->head;
    for (int i = 0; i < n; i++) {
        float v = x[i] * 32767.0f;
        v = v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
        h->buf[hd] = (int16_t)v;
        hd = (hd + 1u) & V1_HIST_MASK;
    }
    h->head = hd;
}

#endif
