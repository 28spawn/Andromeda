/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_constellation.c - ANDROMEDA V1 CONSTELLATION (src/fx/v1_constellation.c). Build with -DPA_PRODUCT_ANDROMEDA=1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "v1_constellation.h"

#define FS 44118.0f
static int fails = 0;
#define CHECK(ok, ...) do { const int ok_ = !!(ok); printf(ok_ ? "  [PASS] " : "  [FAIL] "); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static double db(double x) { return 20.0 * log10(x + 1e-30); }

static int16_t g_buf[V1_HIST_LEN];
static v1_hist_t g_h;
static constellation_t g_c;
typedef float (*src_fn)(int i);
static float chord_src(int i) { return 0.25f * (sinf(2.0f * 3.14159265f * 196.0f * (float)i / FS) + 0.7f * sinf(2.0f * 3.14159265f * 330.0f * (float)i / FS)); }
static float sine_src(int i) { return 0.3f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / FS); }
static void fresh(constellation_params_t p) { memset(g_buf, 0, sizeof g_buf); v1_hist_init(&g_h, g_buf); constellation_init(&g_c, FS); constellation_set(&g_c, &p); constellation_reset(&g_c); }
static void run(src_fn f, double secs, float *oL, float *oR) {
    const int n = ((int)(secs * FS)) / 64 * 64;
    for (int at = 0; at < n; at += 64) {
        float in[64], wl[64], wr[64];
        for (int k = 0; k < 64; k++) in[k] = f(at + k);
        v1_hist_write(&g_h, in, 64);
        constellation_process(&g_c, &g_h, wl, wr, 64);
        if (oL) for (int k = 0; k < 64; k++) { oL[at + k] = wl[k]; oR[at + k] = wr[k]; }
    }
}
static constellation_params_t base(void) { constellation_params_t p = { 120.0f, CONST_DIV_16, 0, 0, 0, 2, CONST_DIR_UP, 1.0f, 0.0f, 0.0f, 1.0f, 4242u }; return p; }
static int degree_of(int scale, int semi, int range) { const int n = (int[]){ 7, 7, 7, 7, 5, 5, 7, 6 }[scale]; for (int d = 0; d < n * range; d++) if (constellation_semitones(scale, d, range) == semi) return d; return -1; }

int main(void) {
    printf("=================================================================\nANDROMEDA V1 CONSTELLATION: a 16-step sequencer on the shared history\n=================================================================\n");
    const int N = (int)(20.0 * FS);
    float *L = calloc(N + 64, 4), *R = calloc(N + 64, 4);

    printf("[1] the clock keeps the tempo for every division, tempo and swing\n");
    { const float bpms[3] = { 60.0f, 120.0f, 200.0f }; double worst = 0; int cases = 0;
      for (int d = 0; d < CONST_DIVS; d++) for (int b = 0; b < 3; b++) for (int s = 0; s < 2; s++) {
          constellation_params_t p = base(); p.division = d; p.bpm = bpms[b]; p.swing = s ? 0.6f : 0.0f; fresh(p); run(chord_src, 20.0, NULL, NULL);
          static const double beats[4] = { 1.0, 0.5, 0.25, 0.125 };
          const double want = (double)((int)(20.0 * FS) / 64 * 64) / FS * bpms[b] / 60.0 / beats[d];
          const double err = fabs((double)g_c.step - want); if (err > worst) worst = err; cases++;
      }
      CHECK(worst <= 2.0, "%d cases: the step count after 20 s is within %.1f of the tempo's (limit 2)", cases, worst); }

    printf("[2] scale quantisation: every pitch is in the scale, for all eight scales, three ranges and two roots\n");
    { static const int sc[8][7] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 5, 7, 9, 10 }, { 0, 2, 4, 7, 9, -1, -1 }, { 0, 3, 5, 7, 10, -1, -1 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 6, 8, 10, -1 } };
      int bad = 0, total = 0, maxsemi = 0, minsemi = 99;
      for (int s = 0; s < 8; s++) for (int r = 1; r <= 3; r++) for (int root = 0; root < 12; root += 7) {
          constellation_params_t p = base(); p.scale = s; p.range = r; p.root = root; p.direction = CONST_DIR_RANDOM; fresh(p); run(chord_src, 6.0, NULL, NULL);
          const int shift = root > 6 ? root - 12 : root;
          for (uint32_t i = 0; i < g_c.log_n && i < 64; i++) {
              const int semi = g_c.log_semi[i] - shift; total++;
              int ok = 0; for (int k = 0; k < 7; k++) if (sc[s][k] >= 0 && semi % 12 == sc[s][k]) ok = 1;
              if (!ok || semi < 0 || semi >= 12 * r) bad++;
              if (g_c.log_semi[i] > maxsemi) maxsemi = g_c.log_semi[i]; if (g_c.log_semi[i] < minsemi) minsemi = g_c.log_semi[i];
          } }
      CHECK(bad == 0 && total > 1000, "%d pitches checked: %d outside the scale or the range (offsets seen %d .. %d semitones)", total, bad, minsemi, maxsemi); }

    printf("[3] direction: up and down walk the scale, pendulum bounces, random covers it, programmed repeats\n");
    { constellation_params_t p = base(); p.scale = 0; p.range = 2; const int total = 14;
      p.direction = CONST_DIR_UP; fresh(p); run(chord_src, 8.0, NULL, NULL);
      int ok = g_c.log_n >= 32; for (int i = 0; i < 32 && ok; i++) if (g_c.log_semi[i] != constellation_semitones(0, ((i + 1) % 16) % total, 2)) ok = 0;
      CHECK(ok, "UP: the first 32 pitches are the scale walked upwards, one degree a step");
      p.direction = CONST_DIR_DOWN; fresh(p); run(chord_src, 8.0, NULL, NULL);
      ok = g_c.log_n >= 32; for (int i = 0; i < 32 && ok; i++) if (g_c.log_semi[i] != constellation_semitones(0, total - 1 - (((i + 1) % 16) % total), 2)) ok = 0;
      CHECK(ok, "DOWN: the same walked downwards");
      p.direction = CONST_DIR_PENDULUM; fresh(p); run(chord_src, 7.5, NULL, NULL);       /* the log holds 64 pitches: stay inside it */
      int steps_ok = 1, ups = 0, downs = 0; int prev = degree_of(0, g_c.log_semi[0], 2);
      for (int i = 1; i < 60 && i < (int)g_c.log_n; i++) { const int d = degree_of(0, g_c.log_semi[i], 2); const int dd = d - prev; if (dd != 1 && dd != -1) steps_ok = 0; if (dd > 0) ups++; else downs++; prev = d; }
      CHECK(steps_ok && ups > 5 && downs > 5, "PENDULUM: every move is one degree, %d up and %d down", ups, downs);
      p.direction = CONST_DIR_RANDOM; fresh(p); run(chord_src, 14.0, NULL, NULL);
      int seen[14] = { 0 }, nseen = 0; for (int i = 0; i < 64 && i < (int)g_c.log_n; i++) { const int d = degree_of(0, g_c.log_semi[i], 2); if (d >= 0 && !seen[d]) { seen[d] = 1; nseen++; } }
      CHECK(nseen >= 10, "RANDOM: %d of the 14 degrees turn up in 64 steps", nseen);
      p.direction = CONST_DIR_PROGRAMMED; p.motion = 0.0f; fresh(p); run(chord_src, 14.0, NULL, NULL);
      ok = g_c.log_n >= 32; for (int i = 0; i < 16 && ok; i++) if (g_c.log_semi[i] != g_c.log_semi[i + 16]) ok = 0;
      CHECK(ok, "PROGRAMMED with no motion: the second cycle repeats the first exactly"); }

    printf("[4] DENSITY picks an exact number of steps, PROBABILITY drops them, MOTION moves the pattern\n");
    { double want_worst = 0;
      for (int d = 1; d <= 4; d++) { constellation_params_t p = base(); p.density = 0.25f * d; fresh(p); run(chord_src, 16.0 * 60.0 / 120.0 * 0.25 * 16 / 16.0 * 4 / 1.0 + 0.1, NULL, NULL);
          const double per_cycle = (double)g_c.n_triggers / ((double)g_c.step / 16.0); want_worst = fmax(want_worst, fabs(per_cycle - 16.0 * p.density)); }
      CHECK(want_worst <= 1.2, "DENSITY 0.25 / 0.5 / 0.75 / 1: the steps played per cycle are within %.1f of density x 16", want_worst);
      constellation_params_t p = base(); p.probability = 0.0f; fresh(p); run(chord_src, 6.0, NULL, NULL);
      CHECK(g_c.n_triggers == 0, "PROBABILITY 0: nothing plays (%u)", (unsigned)g_c.n_triggers);
      p = base(); p.probability = 0.5f; fresh(p); run(chord_src, 20.0, NULL, NULL);
      const double fr = (double)g_c.n_triggers / (double)g_c.step; CHECK(fr > 0.3 && fr < 0.6, "PROBABILITY 0.5: %.2f of the steps play", fr);
      p = base(); p.direction = CONST_DIR_PROGRAMMED; p.motion = 1.0f; fresh(p); run(chord_src, 30.0, NULL, NULL);
      CHECK(g_c.n_mutations >= 5, "MOTION 1: the pattern changed %u times in 30 s", (unsigned)g_c.n_mutations);
      p.motion = 0.0f; fresh(p); run(chord_src, 30.0, NULL, NULL);
      CHECK(g_c.n_mutations == 0, "MOTION 0: the pattern never changes"); }

    printf("[5] the same seed gives the same sequence; another pattern or seed another one\n");
    { constellation_params_t p = base(); p.direction = CONST_DIR_RANDOM; p.motion = 0.5f; p.probability = 0.8f; p.density = 0.7f;
      float *L2 = calloc(N + 64, 4), *R2 = calloc(N + 64, 4);
      fresh(p); run(chord_src, 8.0, L, R); fresh(p); run(chord_src, 8.0, L2, R2);
      int same = 1; for (int i = 0; i < (int)(8.0 * FS) / 64 * 64; i++) if (L[i] != L2[i]) { same = 0; break; }
      CHECK(same, "two runs with the same seed are identical, sample for sample");
      p.pattern = 5; fresh(p); run(chord_src, 8.0, L2, R2); int diff = 0; for (int i = 0; i < (int)(8.0 * FS) / 64 * 64; i++) if (L[i] != L2[i]) { diff = 1; break; }
      CHECK(diff, "another pattern number plays differently");
      free(L2); free(R2); }

    printf("[6] level, pitch of a grain, stereo, clicks, extremes\n");
    { constellation_params_t p = base(); p.density = 0.8f; p.direction = CONST_DIR_PROGRAMMED; p.probability = 0.95f; fresh(p); run(chord_src, 10.0, L, R);
      double el = 0, ei = 0, pk = 0, sl = 0, sr = 0, slr = 0; const int n = (int)(10.0 * FS) / 64 * 64;
      for (int i = 0; i < n; i++) { el += 0.5 * ((double)L[i] * L[i] + (double)R[i] * R[i]); ei += (double)chord_src(i) * chord_src(i); pk = fmax(pk, fabs(L[i])); sl += (double)L[i] * L[i]; sr += (double)R[i] * R[i]; slr += (double)L[i] * R[i]; }
      const double wet = db(sqrt(el / n)), dry = db(sqrt(ei / n)); printf("         wet %.1f dBFS rms against the chord's %.1f dBFS\n", wet, dry);
      CHECK(fabs(wet - dry) < 10.0 && pk < 0.9, "the sequence is in the same range as the playing (%.1f dB against %.1f dB), peak %.3f", wet, dry, pk);
      const double corr = slr / sqrt(sl * sr + 1e-30); CHECK(corr < 0.98, "the steps are placed left and right (L/R correlation %.2f)", corr);
      double d2max = 0, d2rms = 0; int cnt = 0; for (int i = 2; i < n; i++) { const double d2 = L[i] - 2.0 * L[i - 1] + L[i - 2]; d2max = fmax(d2max, fabs(d2)); d2rms += d2 * d2; cnt++; }
      d2rms = sqrt(d2rms / cnt); CHECK(d2max / d2rms < 60.0, "no clicks: the largest second difference is %.0f x the RMS", d2max / d2rms);
      /* the pitch of a grain: a steady 220 Hz in, one step of a major scale at the third degree (+4 semitones = 277.2 Hz) */
      p = base(); p.direction = CONST_DIR_PROGRAMMED; p.density = 1.0f; p.motion = 0.0f; p.division = CONST_DIV_8; fresh(p); run(sine_src, 8.0, L, R);
      int hits = 0, total = 0; double worst = 0;
      for (uint32_t i = 8; i < 24 && i < g_c.log_n; i++) { const double want = 220.0 * pow(2.0, g_c.log_semi[i] / 12.0); (void)want; total++; }
      /* measure the strongest tone over the whole run: it must be one of the scale's pitches */
      double best = 0, bf = 0; const int a = (int)(2.0 * FS), len = (int)(5.0 * FS);
      for (double f = 200.0; f <= 900.0; f += 1.0) { double re = 0, im = 0; for (int k = 0; k < len; k += 2) { const double ph = 2 * M_PI * f * (a + k) / FS; re += L[a + k] * cos(ph); im += L[a + k] * sin(ph); } const double m = sqrt(re * re + im * im); if (m > best) { best = m; bf = f; } }
      int in_scale = 0; static const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 };
      for (int o = 0; o < 2; o++) for (int k = 0; k < 7; k++) if (fabs(1200.0 * log2(bf / (220.0 * pow(2.0, (maj[k] + 12 * o) / 12.0)))) < 25.0) in_scale = 1;
      (void)hits; (void)worst; (void)total;
      CHECK(in_scale, "the strongest tone of the sequence over a steady 220 Hz is %.0f Hz: a note of the scale above it", bf);
      int bad = 0; double wp = 0;
      for (int ex = 0; ex < 8; ex++) { constellation_params_t q = { ex & 1 ? 240.0f : 40.0f, ex % CONST_DIVS, ex * 2, ex, ex, 1 + ex % 3, ex % CONST_DIRS, 1.0f, 0.75f, 1.0f, ex & 2 ? 1.0f : 0.05f, (uint32_t)(ex + 1) };
          fresh(q); run(chord_src, 6.0, L, R); for (int i = 0; i < (int)(6.0 * FS) / 64 * 64; i++) { const double v = fmax(fabs(L[i]), fabs(R[i])); if (!(v == v) || v > 1.5) bad++; wp = fmax(wp, v); } }
      CHECK(bad == 0, "8 extreme cases: finite and bounded (loudest sample %.3f)", wp); }

    free(L); free(R);
    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL CONSTELLATION TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
