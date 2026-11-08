/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_choir.c - ANDROMEDA V1 CHOIR (src/fx/v1_choir.c). Build with -DPA_PRODUCT_ANDROMEDA=1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "v1_choir.h"

#define FS 44118.0f
static int fails = 0;
#define CHECK(ok, ...) do { const int ok_ = !!(ok); printf(ok_ ? "  [PASS] " : "  [FAIL] "); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static double db(double x) { return 20.0 * log10(x + 1e-30); }

static int16_t g_buf[V1_HIST_LEN];
static v1_hist_t g_h;
static choir_t g_c;

/* run `secs` of an input through the engine; fill the outputs. The input function gives sample i */
typedef float (*src_fn)(int i, void *ctx);
static void run(src_fn f, void *ctx, double secs, float *oL, float *oR, float *xin) {
    const int n = (int)(secs * FS);
    for (int at = 0; at < n; at += 64) {
        const int m = (n - at < 64) ? n - at : 64;
        float in[64], wl[64], wr[64];
        for (int k = 0; k < m; k++) in[k] = f(at + k, ctx);
        v1_hist_write(&g_h, in, m);
        choir_process(&g_c, &g_h, in, wl, wr, m);
        for (int k = 0; k < m; k++) { oL[at + k] = wl[k]; oR[at + k] = wr[k]; if (xin) xin[at + k] = in[k]; }
    }
}
static float sine_src(int i, void *ctx) { const double f = *(double *)ctx; return (float)(0.3 * sin(2.0 * M_PI * f * i / FS)); }
static float pluck_src(int i, void *ctx) {
    static const double notes[4] = { 196.0, 246.9, 293.7, 392.0 };
    (void)ctx; const double t = (double)i / FS; const int k = (int)(t / 0.6); const double tt = t - k * 0.6; const double f = notes[k & 3];
    return (float)(0.4 * exp(-tt * 3.0) * (sin(2 * M_PI * f * tt) + 0.5 * sin(4 * M_PI * f * tt) + 0.25 * sin(6 * M_PI * f * tt)) * (tt < 0.0005 ? tt / 0.0005 : 1.0));
}
static void fresh(choir_params_t p) { memset(g_buf, 0, sizeof g_buf); v1_hist_init(&g_h, g_buf); choir_init(&g_c, FS); choir_set(&g_c, &p); }

/* the strongest frequency near `f0` (+-8 %) by a 0.25 Hz scan with a Hann window; returns it and its level */
static double peak_near(const float *x, int n, double f0, double *level) {
    double best = 0, bf = f0;
    for (double f = f0 * 0.92; f <= f0 * 1.08; f += 0.25) {
        double re = 0, im = 0;
        for (int i = 0; i < n; i++) { const double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1)); const double a = 2 * M_PI * f * i / FS; re += w * x[i] * cos(a); im += w * x[i] * sin(a); }
        const double m = sqrt(re * re + im * im);
        if (m > best) { best = m; bf = f; }
    }
    if (level) *level = best / (n * 0.25);
    return bf;
}
static double cents(double f, double ref) { return 1200.0 * log2(f / ref); }

int main(void) {
    printf("=================================================================\nANDROMEDA V1 CHOIR: pitch voices on a shared history\n=================================================================\n");
    const int N = (int)(2.5 * FS);
    float *L = calloc(N, 4), *R = calloc(N, 4), *X = calloc(N, 4);

    printf("[1] pitch: every interval, at a low, a middle and a high input note (detune 0)\n");
    { const double inputs[3] = { 110.0, 330.0, 880.0 };
      for (int vc = 0; vc < CHOIR_VOICINGS; vc++) {
          static const int iv[6][4] = { { 12, 0, 0, 0 }, { 7, 12, 0, 0 }, { 4, 7, 12, 0 }, { 3, 7, 12, 0 }, { -12, 7, 12, 19 }, { -2, 2, 7, 12 } };
          static const int cnt[6] = { 1, 2, 3, 3, 4, 4 };
          double worst = 0;
          for (int ii = 0; ii < 3; ii++) {
              choir_params_t p = { vc, 4, 0.0f, 0.01f, 0.5f, 0.0f, 0.0f };
              fresh(p);
              double f = inputs[ii]; run(sine_src, &f, 2.5, L, R, X);
              const int a = (int)(1.2 * FS), len = (int)(1.0 * FS);
              for (int k = 0; k < cnt[vc]; k++) {
                  const double want = f * pow(2.0, iv[vc][k] / 12.0);
                  if (want > FS / 2.4) continue;
                  double lv; const double got = peak_near(L + a, len, want, &lv);
                  const double e = fabs(cents(got, want)); if (e > worst) worst = e;
                  if (lv < 0.01) { printf("         weak partial: voicing %d in %.0f Hz, +%d st: level %.4f\n", vc, f, iv[vc][k], lv); worst = 999; }
              }
          }
          CHECK(worst < 6.0, "voicing %d: every voice within %.1f cents of its interval over the three inputs (limit 6)", vc, worst);
      } }

    printf("[2] a steady note stays steady: no flutter from the crossfade (octave and massive)\n");
    { const int vcs[2] = { CHOIR_OCTAVE, CHOIR_MASSIVE }; double f = 220.0;
      for (int k = 0; k < 2; k++) {
          choir_params_t p = { vcs[k], 4, 0.0f, 0.01f, 0.5f, 0.0f, 0.0f }; fresh(p); run(sine_src, &f, 2.5, L, R, X);
          double lo = 1e9, hi = 0; const int w = (int)(0.01 * FS);
          for (int a = (int)(1.0 * FS); a + w < N; a += w) { double e = 0; for (int i = a; i < a + w; i++) e += (double)L[i] * L[i]; e = sqrt(e / w); lo = fmin(lo, e); hi = fmax(hi, e); }
          const double rip = db(hi / lo);
          CHECK(rip < (vcs[k] == CHOIR_OCTAVE ? 1.5 : 4.0), "%s: the level of a steady note moves %.2f dB over 10 ms windows", k ? "MASSIVE (4 voices, beating)" : "OCTAVE", rip);
      } }

    printf("[3] level: the choir is about as loud as the playing\n");
    { choir_params_t p = { CHOIR_MAJOR, 3, 6.0f, 0.05f, 0.8f, 0.8f, 0.3f }; fresh(p); run(pluck_src, NULL, 2.5, L, R, X);
      double el = 0, er = 0, ex = 0; for (int i = 0; i < N; i++) { el += (double)L[i] * L[i]; er += (double)R[i] * R[i]; ex += (double)X[i] * X[i]; }
      const double wet = db(sqrt((el + er) / (2.0 * N))), dry = db(sqrt(ex / N));
      printf("         wet %.1f dBFS rms, input %.1f dBFS rms\n", wet, dry);
      CHECK(fabs(wet - dry) < 6.0, "wet %.1f dB against input %.1f dB (within 6 dB)", wet, dry);
      double pk = 0; for (int i = 0; i < N; i++) pk = fmax(pk, fmax(fabs(L[i]), fabs(R[i])));
      CHECK(pk < 0.9, "no clipping risk: wet peak %.3f", pk); }

    printf("[4] swell: ATTACK sets how fast the voices come in, RELEASE how fast they go\n");
    { for (int pass = 0; pass < 2; pass++) {
          const double att = pass ? 0.8 : 0.02; choir_params_t p = { CHOIR_FIFTH, 2, 0.0f, (float)att, 0.5f, 0.0f, 0.0f }; fresh(p);
          double f = 330.0; run(sine_src, &f, 2.0, L, R, X);
          double t63 = -1, final = 0; for (int i = (int)(1.9 * FS); i < (int)(2.0 * FS); i++) final = fmax(final, fabs(L[i]));
          const int w = (int)(0.005 * FS);
          for (int a = 0; a + w < (int)(2.0 * FS); a += w) { double m = 0; for (int i = a; i < a + w; i++) m = fmax(m, fabs(L[i])); if (m > 0.63 * final) { t63 = (double)a / FS; break; } }
          printf("         ATTACK %.2f s: the voices reach 63 %% after %.3f s\n", att, t63);
          if (pass == 0) CHECK(t63 >= 0 && t63 < 0.25, "a short attack (20 ms): 63 %% after %.3f s", t63);
          else CHECK(t63 > 0.35 && t63 < 1.4, "a long attack (0.8 s): 63 %% after %.3f s", t63);
      }
      choir_params_t p = { CHOIR_OCTAVE, 1, 0.0f, 0.01f, 0.3f, 0.0f, 0.0f }; fresh(p);
      { const int n = (int)(2.5 * FS) / 64 * 64; double f = 330.0;
        for (int at = 0; at < n; at += 64) { float in[64], wl[64], wr[64]; for (int k = 0; k < 64; k++) in[k] = (at + k) < (int)(1.0 * FS) ? sine_src(at + k, &f) : 0.0f;
            v1_hist_write(&g_h, in, 64); choir_process(&g_c, &g_h, in, wl, wr, 64); for (int k = 0; k < 64; k++) { L[at + k] = wl[k]; R[at + k] = wr[k]; } }
        double m_in = 0, m_late = 0; for (int i = (int)(0.8 * FS); i < (int)(1.0 * FS); i++) m_in = fmax(m_in, fabs(L[i]));
        for (int i = (int)(2.2 * FS); i < n; i++) m_late = fmax(m_late, fabs(L[i]));
        CHECK(m_in > 0.1 && db(m_late / m_in) < -40.0, "RELEASE 0.3 s: after the playing stops the choir is %.0f dB down 1.2 s later", db(m_late / m_in)); } }

    printf("[5] stereo: SPREAD 0 is centred, SPREAD 1 puts the voices apart\n");
    { for (int pass = 0; pass < 2; pass++) {
          choir_params_t p = { CHOIR_MASSIVE, 4, 6.0f, 0.02f, 0.5f, pass ? 1.0f : 0.0f, 0.0f }; fresh(p); run(pluck_src, NULL, 2.5, L, R, X);
          double sl = 0, sr = 0, slr = 0; for (int i = 0; i < N; i++) { sl += (double)L[i] * L[i]; sr += (double)R[i] * R[i]; slr += (double)L[i] * R[i]; }
          const double corr = slr / sqrt(sl * sr + 1e-30);
          printf("         SPREAD %d: L/R correlation %.2f\n", pass, corr);
          if (pass == 0) CHECK(corr > 0.999, "SPREAD 0: left and right are the same signal (%.4f)", corr);
          else CHECK(corr < 0.97 && corr > -0.2, "SPREAD 1: a real image (%.2f), and it sums to mono without cancelling", corr);
      } }

    printf("[6] formant: colours the voices without changing the level much\n");
    { double e[2], cen[2];
      for (int pass = 0; pass < 2; pass++) {
          choir_params_t p = { CHOIR_MAJOR, 3, 0.0f, 0.02f, 0.5f, 0.5f, pass ? 1.0f : 0.0f }; fresh(p); run(pluck_src, NULL, 2.5, L, R, X);
          double s = 0, zc = 0; for (int i = 0; i < N; i++) { s += (double)L[i] * L[i]; if (i && (L[i] < 0) != (L[i - 1] < 0)) zc++; }
          e[pass] = db(sqrt(s / N)); cen[pass] = zc;
      }
      printf("         level %.1f -> %.1f dB, zero crossings %.0f -> %.0f\n", e[0], e[1], cen[0], cen[1]);
      CHECK(fabs(e[1] - e[0]) < 7.0, "FORMANT 1 changes the level by %.1f dB (limit 7)", e[1] - e[0]);
      CHECK(fabs(cen[1] - cen[0]) / cen[0] > 0.02, "the colour changed the spectrum (zero crossings %.0f -> %.0f)", cen[0], cen[1]); }

    printf("[7] stability and clicks: every voicing, every control at its limits, a pluck\n");
    { double worst = 0; int bad = 0;
      for (int vc = 0; vc < CHOIR_VOICINGS; vc++) for (int ex = 0; ex < 4; ex++) {
          choir_params_t p = { vc, ex == 0 ? 1 : 4, ex == 1 ? 25.0f : 0.0f, ex == 2 ? 0.0f : 1.0f, ex == 3 ? 0.05f : 3.0f, ex == 1 ? 1.0f : 0.0f, ex == 2 ? 1.0f : 0.5f, ex & 1 };
          fresh(p); run(pluck_src, NULL, 2.5, L, R, X);
          for (int i = 0; i < N; i++) { const double a = fmax(fabs(L[i]), fabs(R[i])); if (!(a == a) || a > 1.5) bad++; worst = fmax(worst, a); }
      }
      CHECK(bad == 0, "%d extreme cases: finite and bounded (loudest sample %.3f)", 24, worst);
      choir_params_t p = { CHOIR_OCTAVE, 1, 0.0f, 0.01f, 0.5f, 0.0f, 0.0f }; fresh(p); run(pluck_src, NULL, 2.5, L, R, X);
      double d2max = 0, d2rms = 0; for (int i = 2; i < N; i++) { const double d2 = L[i] - 2.0 * L[i - 1] + L[i - 2]; d2max = fmax(d2max, fabs(d2)); d2rms += d2 * d2; }
      d2rms = sqrt(d2rms / N); CHECK(d2max / d2rms < 60.0, "no clicks: the largest second difference is %.0f x the RMS", d2max / d2rms); }

    free(L); free(R); free(X);
    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL CHOIR TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
