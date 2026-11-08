/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_bloom.c - ANDROMEDA V1 BLOOM (src/fx/v1_bloom.c), measured on the PC.
 *
 *   clang -O2 -Icustom-fw/include -DPA_HOST_SIM=1 -DPA_PRODUCT_ANDROMEDA=1 custom-fw/test/test_bloom.c custom-fw/src/fx/v1_bloom.c -lm -o test_bloom.exe
 *
 * The spec's BLOOM items: no clicks at grain starts and ends (second difference of a sine's cloud against its theoretical bound), pitch where WARP
 * says (and unchanged by reverse), the growth envelope, density and the voice limit, the reverse probability, grain positions that do not repeat,
 * freeze holding without a seam, stability at every extreme, and silence.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "v1_bloom.h"

#define BLOCK 64
#define FS 48000.0f
static int fails = 0;
#define CHECK(ok, ...) do { printf((ok) ? "  [PASS] " : "  [FAIL] "); printf(__VA_ARGS__); printf("\n"); if (!(ok)) fails++; } while (0)
static double db(double x) { return 20.0 * log10(x + 1e-30); }
static int16_t g_ring[V1_HIST_LEN];

static bloom_params_t defaults(void) {
    bloom_params_t p = { 250.0f, 4, 12.0f, 0.0f, 0.0f, 12000.0f, 0.8f, 0.0f, BLOOM_QUANT_OFF, BLOOM_SHAPE_HANN, 0, 0, 4 };
    return p;
}

typedef struct { float *L, *R; int n; double peak; } out_t;

/* Runs `secs` seconds. The history gets `src(t)` (a held sine unless noise); the detector gets `det(t)`. Optionally freezes after `freeze_at` s. */
typedef struct { double f; double amp; int noise; double freeze_at; int click_only; uint32_t rng; } drive_t;

static out_t run(bloom_t *b, v1_hist_t *h, drive_t *d, double secs, void (*each_block)(bloom_t *, int)) {
    const int total = (int)(secs * FS) / BLOCK * BLOCK;
    out_t o = { calloc(total, 4), calloc(total, 4), total, 0 };
    float in[BLOCK], wl[BLOCK], wr[BLOCK], det[BLOCK];
    for (int blk = 0; blk < total / BLOCK; blk++) {
        for (int k = 0; k < BLOCK; k++) {
            const double t = (double)(blk * BLOCK + k) / FS;
            if (d->noise) { d->rng ^= d->rng << 13; d->rng ^= d->rng >> 17; d->rng ^= d->rng << 5; in[k] = (float)(d->amp * (int32_t)d->rng / 2147483648.0); }
            else in[k] = (float)(d->amp * sin(2 * M_PI * d->f * t));
            det[k] = d->click_only ? ((blk == 0 && k == 100) ? 0.5f : (blk == 0 && k == 101 ? -0.5f : 0.0f)) : in[k];
        }
        const int frozen = d->freeze_at >= 0 && (double)(blk * BLOCK) / FS >= d->freeze_at;
        if (frozen && !b->p.freeze) { bloom_params_t p = b->p; p.freeze = 1; bloom_set(b, &p); }
        if (!frozen) v1_hist_write(h, in, BLOCK);
        if (each_block) each_block(b, blk);
        bloom_process(b, h, frozen ? (float[BLOCK]){0} : det, wl, wr, BLOCK);
        for (int k = 0; k < BLOCK; k++) { o.L[blk * BLOCK + k] = wl[k]; o.R[blk * BLOCK + k] = wr[k]; o.peak = fmax(o.peak, fmax(fabs(wl[k]), fabs(wr[k]))); }
    }
    return o;
}
static void fresh(bloom_t *b, v1_hist_t *h, const bloom_params_t *p) { v1_hist_init(h, g_ring); bloom_init(b, FS); bloom_set(b, p); }

static double rms(const float *x, int a, int b) { double e = 0; for (int i = a; i < b; i++) e += (double)x[i] * x[i]; return sqrt(e / (b - a)); }
static double max_d2(const float *x, int a, int b) { double m = 0; for (int i = a + 2; i < b; i++) m = fmax(m, fabs((double)x[i] - 2.0 * x[i - 1] + x[i - 2])); return m; }

static double peak_freq(const float *x, int n, double fs, double f0) {
    int N = 1; while (N < n) N <<= 1;
    double *re = calloc(N, 8), *im = calloc(N, 8);
    for (int i = 0; i < n; i++) re[i] = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / (n - 1)));
    for (int i = 1, j = 0; i < N; i++) { int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (int len = 2; len <= N; len <<= 1) { const double ang = -2 * M_PI / len;
        for (int i = 0; i < N; i += len) for (int k = 0; k < len / 2; k++) { const double wr = cos(ang * k), wi = sin(ang * k);
            const double ur = re[i + k], ui = im[i + k], vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi, vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
            re[i + k] = ur + vr; im[i + k] = ui + vi; re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi; } }
    const int lo = (int)(f0 / 1.5 * N / fs), hi = (int)(f0 * 1.5 * N / fs); int best = lo; double bm = 0;
    for (int i = lo; i <= hi; i++) { const double m = hypot(re[i], im[i]); if (m > bm) { bm = m; best = i; } }
    const double a = hypot(re[best - 1], im[best - 1]), c = hypot(re[best + 1], im[best + 1]), d = 0.5 * (a - c) / (a - 2 * bm + c + 1e-30);
    free(re); free(im); return (best + d) * fs / N;
}

static int g_active_max, g_active_n; static double g_active_sum;
static void sample_active(bloom_t *b, int blk) { if (blk > 150) { g_active_sum += b->active_count; g_active_n++; if (b->active_count > g_active_max) g_active_max = b->active_count; } }

int main(void) {
    printf("=================================================================\nANDROMEDA V1 BLOOM: grain cloud engine\n=================================================================\n");
    bloom_t b; v1_hist_t h; bloom_params_t p;

    printf("[1] no clicks: a sine's cloud, second difference against the theoretical bound, shapes x reverse x warp\n");
    double worst_ratio = 0; int click_cases = 0;
    for (int shape = 0; shape < 3; shape++) for (int ri = 0; ri < 3; ri++) for (int wi = 0; wi < 4; wi++) {
        const float warps[4] = { -12.0f, 0.0f, 7.0f, 24.0f };
        p = defaults(); p.shape = shape; p.reverse = ri * 0.5f; p.warp = warps[wi]; p.drift = 0.5f; p.voices = 8; p.density = 8; p.size_ms = 120.0f;
        fresh(&b, &h, &p); drive_t d = { 220.0, 0.5, 0, -1, 1, 1u };
        out_t o = run(&b, &h, &d, 4.0, NULL);
        const double fmax_hz = 220.0 * pow(2.0, (warps[wi] + 0.4) / 12.0);
        const double a = 2 * M_PI * fmax_hz / FS, bound = o.peak * a * a * 1.6 + 1e-6;     /* sum of sines of at most fmax_hz, each smooth: |d2| <= peak (2 pi f / fs)^2 */
        const double r = max_d2(o.L, (int)(2.2 * FS), o.n) / bound;       /* from 2.2 s: the sine's own start-up kink has left the 1.37 s history */
        if (r > worst_ratio) worst_ratio = r; if (r > 1.0) click_cases++;
        free(o.L); free(o.R);
    }
    CHECK(click_cases == 0, "36 cases: worst second difference = %.2f x the smooth-sine bound (a click would be many times it)", worst_ratio);

    printf("[2] pitch: a 220 Hz sine, drift 0, the cloud's pitch against WARP\n");
    { const float warps[5] = { -12.0f, 0.0f, 7.0f, 12.0f, 19.0f }; double worst = 0;
      for (int wi = 0; wi < 5; wi++) for (int rv = 0; rv < 2; rv++) {
          p = defaults(); p.warp = warps[wi]; p.reverse = (float)rv; p.size_ms = 500.0f; p.shape = BLOOM_SHAPE_TUKEY; p.density = 1; p.voices = 1; p.hq = 1;   /* one long flat-top grain at a time: overlapping or short random-phase grains bias a spectral peak by ~1 Hz */
          fresh(&b, &h, &p); drive_t d = { 220.0, 0.5, 0, -1, 1, 1u };
          out_t o = run(&b, &h, &d, 4.0, NULL);
          const double want = 220.0 * pow(2.0, warps[wi] / 12.0), got = peak_freq(o.L + (int)(1.0 * FS), (int)(2.5 * FS), FS, want);
          const double c = 1200.0 * log2(got / want); if (fabs(c) > fabs(worst)) worst = c;
          free(o.L); free(o.R);
      }
      CHECK(fabs(worst) < 5.0, "worst pitch error %.1f cents over 10 settings, forward and reverse (limit 5)", worst); }
    { p = defaults(); p.warp = 3.2f; p.quant = BLOOM_QUANT_MAJOR; p.hq = 1; fresh(&b, &h, &p); drive_t d = { 220.0, 0.5, 0, -1, 1, 1u };
      out_t o = run(&b, &h, &d, 4.0, NULL);
      const double want = 220.0 * pow(2.0, 4.0 / 12.0), got = peak_freq(o.L + (int)FS, (int)(2.5 * FS), FS, want);
      CHECK(fabs(1200.0 * log2(got / want)) < 12.0, "MAJOR quantiser: WARP 3.2 st sounds at %.1f Hz (E, %.1f Hz)", got, want); free(o.L); free(o.R); }

    printf("[3] growth: the cloud swells after an attack over BLOOM seconds (history steady, detector fired once)\n");
    for (int bi = 0; bi < 2; bi++) {
        p = defaults(); p.bloom_s = bi ? 0.0f : 2.0f; p.size_ms = 250.0f; p.density = 4;
        fresh(&b, &h, &p);
        { float pre[BLOCK]; for (int blk = 0; blk < (int)(2.0 * FS / BLOCK); blk++) { for (int k = 0; k < BLOCK; k++) pre[k] = (float)(0.4 * sin(2 * M_PI * 220.0 * (blk * BLOCK + k) / FS)); v1_hist_write(&h, pre, BLOCK); } }
        /* continue the same sine in the history while the detector sees one click at the start */
        drive_t d = { 220.0, 0.4, 0, -1, 1, 1u };
        out_t o = run(&b, &h, &d, 3.0, NULL);
        const double early = rms(o.L, (int)(0.05 * FS), (int)(0.30 * FS)), late = rms(o.L, (int)(1.8 * FS), (int)(2.5 * FS));
        if (bi == 0) CHECK(db(late / early) > 8.0, "BLOOM 2 s: wet level grows %.1f dB from the first 0.3 s to 1.8-2.5 s (need > 8)", db(late / early));
        else         CHECK(fabs(db(late / early)) < 3.0, "BLOOM 0: no swell (%.1f dB difference, need < 3)", db(late / early));
        free(o.L); free(o.R);
    }

    printf("[4] density and the voice limit\n");
    { const int dens[3] = { 2, 4, 12 }, vox[3] = { 4, 4, 8 };
      for (int i = 0; i < 3; i++) {
          p = defaults(); p.density = dens[i]; p.voices = vox[i]; p.size_ms = 300.0f; p.drift = 0.0f; p.warp = 0.0f;
          fresh(&b, &h, &p); drive_t d = { 220.0, 0.4, 0, -1, 1, 1u }; g_active_max = 0; g_active_n = 0; g_active_sum = 0;
          out_t o = run(&b, &h, &d, 6.0, sample_active); free(o.L); free(o.R);
          const double mean = g_active_sum / g_active_n; const int want = dens[i] < vox[i] ? dens[i] : vox[i];
          CHECK(g_active_max <= vox[i] && mean > want * 0.7 && mean < want * 1.35, "DENSITY %d, voice limit %d: mean %.2f sounding, max %d (clamped to %d)", dens[i], vox[i], mean, g_active_max, want);
      } }

    printf("[5] reverse probability and non-repeating positions\n");
    { const float rv[3] = { 0.0f, 0.5f, 1.0f };
      for (int i = 0; i < 3; i++) {
          p = defaults(); p.reverse = rv[i]; p.size_ms = 100.0f; p.density = 8; p.voices = 8; p.drift = 0.4f;
          fresh(&b, &h, &p); drive_t d = { 220.0, 0.4, 0, -1, 1, 1u };
          out_t o = run(&b, &h, &d, 8.0, NULL); free(o.L); free(o.R);
          const double frac = (double)b.n_reversed / (double)b.n_spawned;
          CHECK(b.n_spawned > 150 && fabs(frac - rv[i]) < 0.12, "REVERSE %.1f: %.0f%% of %u grains played backwards", rv[i], 100.0 * frac, (unsigned)b.n_spawned);
      }
      p = defaults(); p.size_ms = 100.0f; p.density = 8; p.voices = 8; p.drift = 0.0f; fresh(&b, &h, &p);
      { drive_t d = { 220.0, 0.4, 0, -1, 1, 1u }; float in[BLOCK], wl[BLOCK], wr[BLOCK], det[BLOCK] = {0}; uint32_t offs[400]; int no = 0; uint32_t seen = 0;
        for (int blk = 0; blk < (int)(10.0 * FS / BLOCK) && no < 400; blk++) {
            for (int k = 0; k < BLOCK; k++) in[k] = (float)(0.4 * sin(2 * M_PI * 220.0 * (blk * BLOCK + k) / FS));
            v1_hist_write(&h, in, BLOCK); bloom_process(&b, &h, det, wl, wr, BLOCK);
            if (b.n_spawned != seen) { seen = b.n_spawned; for (int v = 0; v < 8; v++) if (b.gr[v].active && b.gr[v].seg == 0 && b.gr[v].left + 2 * BLOCK > b.gr[v].n_rise && no < 400) offs[no++] = (h.head - (b.gr[v].pos >> 16)) & V1_HIST_MASK; }
            (void)d;
        }
        int distinct = 0; for (int i = 0; i < no; i++) { int dup = 0; for (int j = 0; j < i; j++) if (offs[i] / 24 == offs[j] / 24) { dup = 1; break; } if (!dup) distinct++; }
        CHECK(no > 100 && distinct > no * 0.7, "%d distinct start positions of %d grains (positions are random, not a fixed tap)", distinct, no); } }

    printf("[6] freeze: the cloud holds, level and smoothness, no seam where the head stopped\n");
    { p = defaults(); p.warp = 7.0f; p.reverse = 0.5f; p.size_ms = 300.0f; p.density = 4; p.voices = 8; p.density = 6; p.drift = 0.4f;
      fresh(&b, &h, &p); drive_t d = { 220.0, 0.4, 0, 2.0, 1, 1u };
      out_t o = run(&b, &h, &d, 8.0, NULL);
      const double r1 = rms(o.L, (int)(2.2 * FS), (int)(3.2 * FS)), r2 = rms(o.L, (int)(6.5 * FS), (int)(7.9 * FS));
      const double a = 2 * M_PI * 220.0 * pow(2.0, 7.4 / 12.0) / FS, bound = o.peak * a * a * 1.6 + 1e-6, r = max_d2(o.L, (int)(2.2 * FS), o.n) / bound;
      CHECK(fabs(db(r2 / r1)) < 3.0, "level held: %.1f dB between 0.2-1.2 s and 4.5-5.9 s after the freeze (limit 3)", db(r2 / r1));
      CHECK(r < 1.0, "no seam: worst second difference %.2f x the smooth bound while frozen", r);
      free(o.L); free(o.R); }

    printf("[7] stability: noise input, extremes of every parameter, both quantisers and shapes\n");
    { int bad = 0, cases = 0; double worst = 0;
      for (int s = 0; s < 40; s++) {
          uint32_t r = 777u + 31u * s; bloom_params_t q;
          q.size_ms = (s & 1) ? 60.0f : 500.0f; q.density = (s % 3) * 5 + 1; q.warp = (s & 2) ? 24.0f : -12.0f; q.reverse = (float)((s >> 2) & 1);
          q.bloom_s = (s & 4) ? 0.0f : 2.0f; q.tone_hz = (s & 8) ? 200.0f : 12000.0f; q.spread = (float)((s >> 1) & 1); q.drift = (float)((s >> 3) & 1);
          q.quant = s % 6; q.shape = s % 3; q.freeze = 0; q.hq = (s >> 4) & 1; q.voices = (s & 16) ? 8 : 4; (void)r;
          fresh(&b, &h, &q); drive_t d = { 0, 0.8, 1, (s % 5 == 0) ? 1.5 : -1.0, 0, 99u + s };
          out_t o = run(&b, &h, &d, 4.0, NULL);
          int nf = 0; for (int i = 0; i < o.n; i++) if (!isfinite(o.L[i]) || !isfinite(o.R[i])) nf = 1;
          cases++; if (nf || o.peak > 3.0) bad++; worst = fmax(worst, o.peak); free(o.L); free(o.R);
      }
      CHECK(bad == 0, "%d cases finite and bounded, loudest sample %.2f", cases, worst); }

    printf("[8] silence in, silence out\n");
    { p = defaults(); fresh(&b, &h, &p); drive_t d = { 220.0, 0.0, 0, -1, 1, 1u }; out_t o = run(&b, &h, &d, 3.0, NULL);
      CHECK(o.peak < 1e-9, "empty history -> wet peak %.3g", o.peak); free(o.L); free(o.R); }

    printf("[9] level: a held 220 Hz note, the cloud against the note itself (density 4, 250 ms, grown)\n");
    { p = defaults(); p.warp = 0.0f; p.reverse = 0.5f; p.drift = 0.3f; fresh(&b, &h, &p); drive_t d = { 220.0, 0.3, 0, -1, 0, 1u };
      out_t o = run(&b, &h, &d, 5.0, NULL); const double w = rms(o.L, (int)(2.5 * FS), (int)(4.5 * FS)), in_rms = 0.3 / sqrt(2.0);
      CHECK(db(w / in_rms) > -8.0 && db(w / in_rms) < 2.0, "wet RMS %.1f dB relative to the note (want -8 .. +2: audible but not louder than the guitar)", db(w / in_rms)); free(o.L); free(o.R); }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL BLOOM TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
