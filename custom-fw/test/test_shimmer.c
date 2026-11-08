/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_shimmer.c - ANDROMEDA V1 SHIMMER (src/fx/v1_shimmer.c) around the shared reverb (v1_fdn.c). Build with -DPA_PRODUCT_ANDROMEDA=1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "v1_shimmer.h"

#define FS 44118.0f
static int fails = 0;
#define CHECK(ok, ...) do { const int ok_ = !!(ok); printf(ok_ ? "  [PASS] " : "  [FAIL] "); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static double db(double x) { return 20.0 * log10(x + 1e-30); }

static fdn_t g_f, g_f2;
static shimmer_t g_s;
static fdn_params_t fp(float decay) { fdn_params_t p = { 0.5f, decay, 1.0f, 11000.0f, 0.3f, 0.4f, 1.0f, 0.0f }; return p; }
static float burst(int i, double hz, double secs) { return i < (int)(secs * FS) ? (float)(0.3 * sin(2.0 * M_PI * hz * i / FS) * (i < 2000 ? i / 2000.0 : 1.0)) : 0.0f; }

static void run(int use_shimmer, double hz, double burst_s, double secs, float *oL, float *oR, fdn_t *f) {
    const int n = ((int)(secs * FS)) / 64 * 64;
    for (int at = 0; at < n; at += 64) {
        float in[64], wl[64], wr[64];
        for (int k = 0; k < 64; k++) in[k] = burst(at + k, hz, burst_s);
        if (use_shimmer) shimmer_process(&g_s, f, in, in, wl, wr, 64); else fdn_process(f, in, in, wl, wr, 64);
        for (int k = 0; k < 64; k++) { oL[at + k] = wl[k]; oR[at + k] = wr[k]; }
    }
}
static double band(const float *x, int a, int len, double f0, double f1) {      /* energy in a band by a coarse DFT scan */
    double e = 0;
    for (double f = f0; f <= f1; f += 4.0) { double re = 0, im = 0; for (int k = 0; k < len; k += 2) { const double w = 0.5 - 0.5 * cos(2 * M_PI * k / (len - 1)); const double ph = 2 * M_PI * f * (a + k) / FS; re += w * x[a + k] * cos(ph); im += w * x[a + k] * sin(ph); } e += re * re + im * im; }
    return e;
}

int main(void) {
    printf("=================================================================\nANDROMEDA V1 SHIMMER: pitch-shifted paths in the reverb's feedback\n=================================================================\n");
    const int N = (int)(8.0 * FS) / 64 * 64 + 64;
    float *L = calloc(N, 4), *R = calloc(N, 4), *L2 = calloc(N, 4), *R2 = calloc(N, 4);

    printf("[1] FEEDBACK 0 is the plain reverb, bit for bit\n");
    { fdn_params_t p = fp(4.0f); fdn_init(&g_f, FS); fdn_set(&g_f, &p); fdn_init(&g_f2, FS); fdn_set(&g_f2, &p);
      shimmer_init(&g_s, FS); shimmer_params_t sp = { SHIM_OCTAVE, 0.0f, 5000.0f }; shimmer_set(&g_s, &sp);
      run(1, 220.0, 0.5, 3.0, L, R, &g_f); run(0, 220.0, 0.5, 3.0, L2, R2, &g_f2);
      int same = 1; for (int i = 0; i < (int)(3.0 * FS) / 64 * 64; i++) if (L[i] != L2[i] || R[i] != R2[i]) { same = 0; break; }
      CHECK(same, "with the shimmer's feedback at 0 the output is the reverb's, sample for sample"); }

    printf("[2] the tail climbs: a 220 Hz burst grows energy an octave up (and a fifth above that) as the tail recirculates\n");
    { const int ints[3] = { SHIM_OCTAVE, SHIM_FIFTH, SHIM_NINETEEN }; const double hz_up[3] = { 440.0, 330.0, 220.0 * pow(2.0, 19.0 / 12.0) };
      fdn_params_t p = fp(6.0f);
      fdn_init(&g_f2, FS); fdn_set(&g_f2, &p); shimmer_init(&g_s, FS); shimmer_params_t off = { SHIM_OCTAVE, 0.0f, 8000.0f }; shimmer_set(&g_s, &off); run(1, 220.0, 0.4, 6.0, L2, R2, &g_f2);
      const int a = (int)(2.0 * FS), len = (int)(2.5 * FS);
      for (int k = 0; k < 3; k++) {
          fdn_init(&g_f, FS); fdn_set(&g_f, &p); shimmer_init(&g_s, FS); shimmer_params_t sp = { ints[k], 0.8f, 8000.0f }; shimmer_set(&g_s, &sp); run(1, 220.0, 0.4, 6.0, L, R, &g_f);
          const double with = band(L, a, len, hz_up[k] * 0.97, hz_up[k] * 1.03), without = band(L2, a, len, hz_up[k] * 0.97, hz_up[k] * 1.03);
          const double base_with = band(L, a, len, 150.0, 260.0), base_without = band(L2, a, len, 150.0, 260.0);
          printf("         interval %d: energy near %.0f Hz is %.1f dB above the plain reverb's, near 220 Hz %.1f dB\n", ints[k], hz_up[k], db(sqrt(with / without)), db(sqrt(base_with / base_without)));
          CHECK(db(sqrt(with / without)) > db(sqrt(base_with / base_without)) + (ints[k] == SHIM_NINETEEN ? 1.0 : 3.0), "interval %d: the shifted band gains %.1f dB more than the source band", ints[k], db(sqrt(with / without)) - db(sqrt(base_with / base_without)));
      } }

    printf("[3] the loop cannot run away: the longest decay, the most feedback, the brightest feedback path\n");
    { fdn_params_t p = fp(25.0f); fdn_init(&g_f, FS); fdn_set(&g_f, &p); shimmer_init(&g_s, FS); shimmer_params_t sp = { SHIM_OCT_FIFTH, 1.0f, 12000.0f }; shimmer_set(&g_s, &sp);
      run(1, 330.0, 0.5, 8.0, L, R, &g_f);
      double pk = 0, e1 = 0, e2 = 0; int bad = 0;
      for (int i = 0; i < (int)(8.0 * FS) / 64 * 64; i++) { const double v = fmax(fabs(L[i]), fabs(R[i])); if (!(v == v)) bad++; pk = fmax(pk, v); }
      for (int i = (int)(2.0 * FS); i < (int)(4.0 * FS); i++) e1 += (double)L[i] * L[i];
      for (int i = (int)(6.0 * FS); i < (int)(8.0 * FS); i++) e2 += (double)L[i] * L[i];
      printf("         peak %.3f, energy %.1f dB between 2-4 s and 6-8 s\n", pk, db(sqrt(e2 / e1)));
      CHECK(bad == 0 && pk < 1.5, "finite, peak %.3f", pk);
      CHECK(e2 <= e1 * 1.5, "the tail does not grow after the input stops (%.1f dB from 2-4 s to 6-8 s)", db(sqrt(e2 / e1))); }

    printf("[4] level: a moderate shimmer is not much louder than the plain reverb\n");
    { fdn_params_t p = fp(6.0f); double el[2];
      for (int pass = 0; pass < 2; pass++) { fdn_init(&g_f, FS); fdn_set(&g_f, &p); shimmer_init(&g_s, FS); shimmer_params_t sp = { SHIM_OCTAVE, pass ? 0.5f : 0.0f, 6000.0f }; shimmer_set(&g_s, &sp); run(1, 220.0, 0.4, 6.0, L, R, &g_f);
          double e = 0; for (int i = 0; i < (int)(6.0 * FS) / 64 * 64; i++) e += (double)L[i] * L[i]; el[pass] = e; }
      CHECK(db(sqrt(el[1] / el[0])) < 10.0, "FEEDBACK 0.5: %.1f dB louder than the plain reverb (limit 10)", db(sqrt(el[1] / el[0]))); }

    free(L); free(R); free(L2); free(R2);
    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL SHIMMER TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
