/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_fracture.c - ANDROMEDA V1 FRACTURE (src/fx/v1_fracture.c). Build with -DPA_PRODUCT_ANDROMEDA=1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "v1_fracture.h"

#define FS 44118.0f
static int fails = 0;
#define CHECK(ok, ...) do { const int ok_ = !!(ok); printf(ok_ ? "  [PASS] " : "  [FAIL] "); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static double db(double x) { return 20.0 * log10(x + 1e-30); }

static int16_t g_buf[V1_HIST_LEN];
static v1_hist_t g_h;
static fracture_t g_f;
typedef float (*src_fn)(int i);
static float noise_src(int i) { static uint32_t s = 1; (void)i; s = s * 1664525u + 1013904223u; return 0.3f * (float)(int32_t)s / 2147483648.0f; }
static float sine_src(int i) { return 0.3f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / FS); }
static float chord_src(int i) { return 0.25f * (sinf(2.0f * 3.14159265f * 196.0f * (float)i / FS) + 0.7f * sinf(2.0f * 3.14159265f * 330.0f * (float)i / FS)); }
static float saw_src(int i) { return 0.4f * ((float)(i % 100) / 50.0f - 1.0f); }
static float pluck_src(int i) {
    static const double notes[4] = { 196.0, 246.9, 293.7, 392.0 };
    const double t = (double)i / FS; const int k = (int)(t / 0.5); const double tt = t - k * 0.5; const double f = notes[k & 3];
    return (float)(0.4 * exp(-tt * 3.0) * (sin(2 * M_PI * f * tt) + 0.5 * sin(4 * M_PI * f * tt)) * (tt < 0.0005 ? tt / 0.0005 : 1.0));
}
static void fresh(fracture_params_t p) { memset(g_buf, 0, sizeof g_buf); v1_hist_init(&g_h, g_buf); fracture_init(&g_f, FS); fracture_set(&g_f, &p); fracture_reset(&g_f); }
static void run(src_fn f, double secs, float *oL, float *oR) {
    const int n = ((int)(secs * FS)) / 64 * 64;
    for (int at = 0; at < n; at += 64) {
        float in[64], wl[64], wr[64];
        for (int k = 0; k < 64; k++) in[k] = f(at + k);
        v1_hist_write(&g_h, in, 64);
        fracture_process(&g_f, &g_h, wl, wr, 64);
        for (int k = 0; k < 64; k++) { oL[at + k] = wl[k]; oR[at + k] = wr[k]; }
    }
}
static fracture_params_t base(void) { fracture_params_t p = { 120.0f, FRACTURE_GRID_16, 60.0f, 1, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 12345u }; return p; }

int main(void) {
    printf("=================================================================\nANDROMEDA V1 FRACTURE: slice scheduler on the shared history\n=================================================================\n");
    const int N = (int)(20.0 * FS);
    float *L = calloc(N + 64, 4), *R = calloc(N + 64, 4);

    printf("[1] the clock: steps keep the tempo, whatever the grid, the tempo and the swing\n");
    { const float bpms[3] = { 60.0f, 120.0f, 200.0f }; const float swings[2] = { 0.0f, 0.6f }; double worst = 0; int cases = 0;
      for (int g = 0; g < FRACTURE_GRIDS; g++) for (int b = 0; b < 3; b++) for (int s = 0; s < 2; s++) {
          fracture_params_t p = base(); p.grid = g; p.bpm = bpms[b]; p.swing = swings[s]; fresh(p); run(noise_src, 20.0, L, R);
          static const double beats[5] = { 1.0, 0.5, 0.25, 0.125, 0.0625 };
          const double want = (20.0 * (N / 64 * 64) / (double)N) * bpms[b] / 60.0 / beats[g];
          const double err = fabs((double)g_f.n_steps - want); if (err > worst) worst = err; cases++;
      }
      CHECK(worst <= 2.0, "%d cases (5 grids x 3 tempos x swing 0 / 0.6): the step count after 20 s is within %.1f of the tempo's (limit 2)", cases, worst); }

    printf("[2] DENSITY decides which steps play, evenly spread; CHAOS leaves the average alone\n");
    { const float dens[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
      for (int d = 0; d < 4; d++) { fracture_params_t p = base(); p.density = dens[d]; fresh(p); run(noise_src, 20.0, L, R);
          const double frac = (double)g_f.n_steps_on / (double)g_f.n_steps;
          CHECK(fabs(frac - dens[d]) < 0.03, "density %.2f: %.3f of the steps play", dens[d], frac); }
      fracture_params_t p = base(); p.density = 0.5f; p.chaos = 1.0f; fresh(p); run(noise_src, 20.0, L, R);
      const double fr = (double)g_f.n_steps_on / (double)g_f.n_steps;
      CHECK(fr > 0.35 && fr < 0.65 && g_f.n_flipped > 50, "chaos 1 at density 0.5: %.2f of the steps play, %u flipped (the structure stays, the choices vary)", fr, (unsigned)g_f.n_flipped); }

    printf("[3] the same seed gives the same performance, another seed another one\n");
    { fracture_params_t p = base(); p.chaos = 0.8f; p.reverse = 0.4f; p.density = 0.6f;
      float *L2 = calloc(N + 64, 4), *R2 = calloc(N + 64, 4);
      fresh(p); run(pluck_src, 6.0, L, R); fresh(p); run(pluck_src, 6.0, L2, R2);
      int same = 1; for (int i = 0; i < (int)(6.0 * FS) / 64 * 64; i++) if (L[i] != L2[i] || R[i] != R2[i]) { same = 0; break; }
      CHECK(same, "two runs with seed %u are identical, sample for sample", (unsigned)p.seed);
      p.seed = 777u; fresh(p); run(pluck_src, 6.0, L2, R2);
      int diff = 0; for (int i = 0; i < (int)(6.0 * FS) / 64 * 64; i++) if (L[i] != L2[i]) { diff = 1; break; }
      CHECK(diff, "another seed plays differently");
      free(L2); free(R2); }

    printf("[4] a slice is the past, at the asked pitch\n");
    { for (int pass = 0; pass < 2; pass++) {
          fracture_params_t p = base(); p.pitch = pass ? 12.0f : 0.0f; p.slice_ms = 124.0f; p.density = 1.0f; p.gate = pass ? 0.49f : 1.0f;      /* no loop: the slice is read once, in 124 ms at the original pitch, in 62 ms an octave up */ fresh(p); run(sine_src, 6.0, L, R);
          const int a = (int)(2.0 * FS), len = (int)(2.0 * FS); const double want = pass ? 440.0 : 220.0; double best = 0, bf = 0;
          for (double f = want * 0.9; f <= want * 1.1; f += 0.5) { double re = 0, im = 0; for (int i = 0; i < len; i++) { const double w = 0.5 - 0.5 * cos(2 * M_PI * i / (len - 1)); const double ph = 2 * M_PI * f * (a + i) / FS; re += w * L[a + i] * cos(ph); im += w * L[a + i] * sin(ph); }
              const double m = sqrt(re * re + im * im); if (m > best) { best = m; bf = f; } }
          const double amp = best / (len * 0.25);
          CHECK(fabs(1200.0 * log2(bf / want)) < 40.0 && amp > 0.06, "pitch %+.0f st: the strongest tone is %.0f Hz (wanted %.0f), amplitude %.2f", p.pitch, bf, want, amp);
      } }

    printf("[5] REVERSE: slices play backwards (a rising saw comes out falling)\n");
    { for (int pass = 0; pass < 2; pass++) {
          fracture_params_t p = base(); p.reverse = pass ? 1.0f : 0.0f; p.slice_ms = 100.0f; p.density = 1.0f; p.gate = 1.0f; fresh(p); run(saw_src, 6.0, L, R);
          int up = 0, down = 0; for (int i = (int)(2.0 * FS); i < (int)(5.0 * FS); i++) { const float d = L[i] - L[i - 1]; if (d > 0.002f) up++; else if (d < -0.002f) down++; }
          const double upfrac = (double)up / (up + down + 1);
          CHECK(pass ? upfrac < 0.15 : upfrac > 0.85, "reverse %d: %.0f %% of the steps of the wave go up", pass, 100.0 * upfrac);
      } }

    printf("[6] REPEAT: the same slice again inside the step (a stutter)\n");
    { double corr[2];
      for (int pass = 0; pass < 2; pass++) {
          fracture_params_t p = base(); p.grid = FRACTURE_GRID_8; p.bpm = 120.0f; p.repeat = pass ? 4 : 1; p.slice_ms = 30.0f; p.gate = 0.5f; p.density = 1.0f; fresh(p); run(noise_src, 8.0, L, R);
          const int lag0 = (int)(FS * 0.25 / 4.0);                    /* a step is 250 ms at 1/8 and 120 BPM; four repeats of it (the repeat distance is 2756.25 samples: the clock is fractional, so look a few samples either side) */
          double best = -1;
          for (int lag = lag0 - 3; lag <= lag0 + 3; lag++) { double num = 0, e1 = 0, e2 = 0; for (int i = (int)(2.0 * FS); i < (int)(7.0 * FS); i++) { num += (double)L[i] * L[i - lag]; e1 += (double)L[i] * L[i]; e2 += (double)L[i - lag] * L[i - lag]; }
              const double c = num / sqrt(e1 * e2 + 1e-30); if (c > best) best = c; }
          corr[pass] = best;
      }
      CHECK(corr[1] > 0.5 && corr[0] < 0.25, "the correlation at the repeat distance is %.2f with REPEAT 4 and %.2f with REPEAT 1", corr[1], corr[0]); }

    printf("[6b] GATE: how much of the step a slice sounds\n");
    { double duty[2];
      for (int pass = 0; pass < 2; pass++) {
          fracture_params_t p = base(); p.gate = pass ? 0.25f : 1.0f; p.slice_ms = 100.0f; p.density = 1.0f; fresh(p); run(noise_src, 8.0, L, R);
          int on = 0, tot = 0; for (int i = (int)(2.0 * FS); i < (int)(7.0 * FS); i += 16) { double e = 0; for (int k = 0; k < 16; k++) e += fabs(L[i + k]); tot++; if (e / 16 > 0.01) on++; }
          duty[pass] = (double)on / tot;
      }
      CHECK(duty[0] > 0.85 && duty[1] > 0.12 && duty[1] < 0.45, "gate 1: sounding %.0f %% of the time, gate 0.25: %.0f %%", 100 * duty[0], 100 * duty[1]); }

    printf("[7] stability and clicks: every control at its limits\n");
    { int bad = 0; double worst = 0;
      for (int ex = 0; ex < 8; ex++) {
          fracture_params_t p = { ex & 1 ? 240.0f : 40.0f, ex % FRACTURE_GRIDS, ex & 2 ? 250.0f : 5.0f, ex & 4 ? 8 : 1, 1.0f, ex & 1 ? 1.0f : 0.0f, ex & 2 ? 24.0f : -24.0f, ex & 4 ? 1.0f : 0.05f, 0.75f, ex & 2 ? 1.0f : 0.01f, (uint32_t)(ex + 1) };
          fresh(p); run(pluck_src, 6.0, L, R);
          for (int i = 0; i < (int)(6.0 * FS) / 64 * 64; i++) { const double a = fmax(fabs(L[i]), fabs(R[i])); if (!(a == a) || a > 1.5) bad++; worst = fmax(worst, a); }
      }
      CHECK(bad == 0, "8 extreme cases: finite and bounded (loudest sample %.3f)", worst);
      fracture_params_t p = base(); p.chaos = 0.3f; p.reverse = 0.3f; p.density = 0.8f; fresh(p); run(chord_src, 8.0, L, R);
      double d2max = 0, d2rms = 0; int cnt = 0; for (int i = 2; i < (int)(8.0 * FS); i++) { const double d2 = L[i] - 2.0 * L[i - 1] + L[i - 2]; d2max = fmax(d2max, fabs(d2)); d2rms += d2 * d2; cnt++; }
      d2rms = sqrt(d2rms / cnt); CHECK(d2max / d2rms < 80.0, "no clicks on a sustained chord: the largest second difference is %.0f x the RMS (slices fade in and out over 1.5 ms)", d2max / d2rms);
      double pk = 0; for (int i = 0; i < (int)(8.0 * FS); i++) pk = fmax(pk, fabs(L[i]));
      CHECK(pk < 0.9 && db(pk) > -20.0, "level: the loudest wet sample is %.3f against a 0.4 input", pk); }

    free(L); free(R);
    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL FRACTURE TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
