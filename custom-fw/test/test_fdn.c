/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_fdn.c - ANDROMEDA V1 shared FDN reverb (src/fx/v1_fdn.c), measured on the PC.
 *
 *   clang -O2 -Icustom-fw/include -DPA_HOST_SIM=1 -DPA_PRODUCT_ANDROMEDA=1 custom-fw/test/test_fdn.c custom-fw/src/fx/v1_fdn.c -lm -o test_fdn.exe
 *
 * The spec's FDN items: stable, controllable tails (RT60 from the backward-integrated impulse response against the DECAY knob), a dense tail (normalised
 * echo density), a stereo image that sums to mono, a pre-delay that is where the knob says, a level that does not depend on the decay, silence without
 * input, and no clicks when the controls move.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "v1_fdn.h"

#define BLOCK 64
#define FS 48000.0f
static int fails = 0;
#define CHECK(ok, ...) do { printf((ok) ? "  [PASS] " : "  [FAIL] "); printf(__VA_ARGS__); printf("\n"); if (!(ok)) fails++; } while (0)
static double db(double x) { return 20.0 * log10(x + 1e-30); }
static fdn_t g_fdn;                                   /* ~48 KiB: static */

static fdn_params_t defaults(void) { fdn_params_t p = { 0.5f, 3.0f, 1.0f, 11000.0f, 0.3f, 0.4f, 1.0f, 0.0f }; return p; }
typedef struct { float *L, *R; int n; } sig_t;

/* impulse (or noise) in, wet out */
static sig_t run_impulse(const fdn_params_t *p, double secs, int in_l_only) {
    const int n = (int)(secs * FS) / BLOCK * BLOCK;
    sig_t s = { calloc(n, 4), calloc(n, 4), n };
    fdn_init(&g_fdn, FS); fdn_set(&g_fdn, p);
    float iL[BLOCK], iR[BLOCK];
    for (int b = 0; b < n / BLOCK; b++) {
        memset(iL, 0, sizeof iL); memset(iR, 0, sizeof iR);
        if (b == 0) { iL[0] = 1.0f; if (!in_l_only) iR[0] = 1.0f; }
        fdn_process(&g_fdn, iL, iR, s.L + b * BLOCK, s.R + b * BLOCK, BLOCK);
    }
    return s;
}
static double edc_t60(const sig_t *s) {              /* Schroeder backward integration; T60 = 2 x (time from -5 to -35 dB) */
    double *e = malloc(s->n * sizeof(double)); double acc = 0;
    for (int i = s->n - 1; i >= 0; i--) { acc += (double)s->L[i] * s->L[i] + (double)s->R[i] * s->R[i]; e[i] = acc; }
    double t5 = -1, t35 = -1;
    for (int i = 0; i < s->n; i++) { const double d = 10 * log10(e[i] / e[0] + 1e-30); if (t5 < 0 && d <= -5) t5 = i / FS; if (t35 < 0 && d <= -35) { t35 = i / FS; break; } }
    free(e); return (t5 > 0 && t35 > 0) ? 2.0 * (t35 - t5) : -1.0;
}

int main(void) {
    printf("=================================================================\nANDROMEDA V1 shared FDN reverb (24 kHz tank)\n=================================================================\n");
    fdn_params_t p;

    printf("[1] RT60: the backward-integrated impulse response against DECAY (damping 11 kHz, diffusion 1)\n");
    { const float dec[4] = { 0.5f, 2.0f, 6.0f, 15.0f };
      for (int i = 0; i < 4; i++) {
          p = defaults(); p.decay_s = dec[i]; sig_t s = run_impulse(&p, dec[i] * 1.7 + 1.0, 1);
          const double t = edc_t60(&s);
          CHECK(t > dec[i] * 0.7 && t < dec[i] * 1.3, "DECAY %.1f s -> measured RT60 %.2f s (within 30%%)", dec[i], t);
          free(s.L); free(s.R);
      } }

    printf("[2] damping: the high frequencies die sooner than the lows\n");
    { p = defaults(); p.decay_s = 6.0f; p.damp_hz = 2000.0f; sig_t a = run_impulse(&p, 6.0, 1);
      p.damp_hz = 11000.0f; sig_t b = run_impulse(&p, 6.0, 1);
      /* the share of the late tail (2-4 s) above ~4 kHz, from the first difference: a dark tank has much less of it */
      double ha = 0, hb = 0, ea = 0, eb = 0;
      for (int i = (int)(2.0 * FS); i < (int)(4.0 * FS); i++) { const double da = (double)a.L[i] - a.L[i - 1], dbb = (double)b.L[i] - b.L[i - 1]; ha += da * da; hb += dbb * dbb; ea += (double)a.L[i] * a.L[i]; eb += (double)b.L[i] * b.L[i]; }
      const double sa = ha / ea, sb = hb / eb;
      CHECK(sa < sb * 0.5, "high-frequency share of the 2-4 s tail: %.4f at 2 kHz damping vs %.4f at 11 kHz (the dark tank has under half)", sa, sb);
      (void)edc_t60;
      free(a.L); free(a.R); free(b.L); free(b.R); }

    printf("[3] density: normalised echo density of the tail (1.0 = Gaussian: no discrete echoes)\n");
    { p = defaults(); p.size = 0.5f; sig_t s = run_impulse(&p, 1.5, 1); const int w = 960;
      double dens_early = 0, dens_late = 0; int ne = 0, nl = 0;
      for (int w0 = 0; w0 + w < s.n; w0 += w) {
          double e = 0; for (int i = 0; i < w; i++) e += (double)s.L[w0 + i] * s.L[w0 + i]; const double sd = sqrt(e / w); int cnt = 0;
          for (int i = 0; i < w; i++) if (fabs(s.L[w0 + i]) > sd) cnt++;
          const double nd = (double)cnt / w / 0.3173, t = w0 / FS;
          if (t > 0.06 && t < 0.12) { dens_early += nd; ne++; } if (t > 0.30 && t < 1.0) { dens_late += nd; nl++; }
      }
      dens_early /= ne; dens_late /= nl;
      CHECK(dens_late > 0.8, "echo density %.2f at 60-120 ms, %.2f from 300 ms to 1 s (need > 0.8 late)", dens_early, dens_late);
      free(s.L); free(s.R); }

    printf("[4] silence in, silence out\n");
    { p = defaults(); sig_t s = run_impulse(&p, 0.01, 1); fdn_init(&g_fdn, FS); fdn_set(&g_fdn, &p); float z[BLOCK] = {0}, oL[BLOCK], oR[BLOCK]; double pk = 0;
      for (int b = 0; b < 3000; b++) { fdn_process(&g_fdn, z, z, oL, oR, BLOCK); for (int k = 0; k < BLOCK; k++) pk = fmax(pk, fmax(fabs(oL[k]), fabs(oR[k]))); }
      CHECK(pk < 1e-9, "no input -> output peak %.3g", pk); free(s.L); free(s.R); }

    printf("[5] stereo: an impulse on the left only; the tail's image\n");
    { p = defaults(); p.width = 1.0f; sig_t s = run_impulse(&p, 2.0, 1);
      double sl = 0, sr = 0, slr = 0; for (int i = (int)(0.3 * FS); i < s.n; i++) { sl += (double)s.L[i] * s.L[i]; sr += (double)s.R[i] * s.R[i]; slr += (double)s.L[i] * s.R[i]; }
      const double corr = slr / sqrt(sl * sr + 1e-30), mono = db(sqrt((sl + sr + 2 * slr) / 4.0 / ((sl + sr) / 2.0)));
      CHECK(fabs(corr) < 0.4, "L/R tail correlation %.2f (a wide image: |r| < 0.4)", corr);
      CHECK(mono > -4.5, "mono sum keeps %.1f dB of the average channel power (no severe cancellation: > -4.5)", mono);
      free(s.L); free(s.R);
      p.width = 0.0f; s = run_impulse(&p, 1.5, 1); double dm = 0, pk = 0; for (int i = (int)(0.4 * FS); i < s.n; i++) { dm = fmax(dm, fabs(s.L[i] - s.R[i])); pk = fmax(pk, fabs(s.L[i])); }   /* after the width control has settled */
      CHECK(dm < 1e-4 * pk + 1e-9, "WIDTH 0: left and right identical (max difference %.2g of peak %.3g)", dm, pk);
      free(s.L); free(s.R); }

    printf("[6] pre-delay: the first output moves by the knob's value\n");
    { p = defaults(); p.predelay_ms = 0.0f; sig_t a = run_impulse(&p, 0.5, 1); p.predelay_ms = 100.0f; sig_t b = run_impulse(&p, 0.5, 1);
      int oa = -1, ob = -1; for (int i = 0; i < a.n; i++) { if (oa < 0 && fabs(a.L[i]) + fabs(a.R[i]) > 1e-4) oa = i; if (ob < 0 && fabs(b.L[i]) + fabs(b.R[i]) > 1e-4) ob = i; }
      const double ms = (ob - oa) / (double)FS * 1000.0;
      CHECK(oa > 0 && ob > 0 && fabs(ms - 100.0) < 2.0, "pre-delay 100 ms moved the onset by %.2f ms (onsets at %d and %d samples)", ms, oa, ob);
      free(a.L); free(a.R); free(b.L); free(b.R); }

    printf("[7] level: steady stereo noise in, the wet RMS against the input (should not depend on DECAY)\n");
    { const float dec[3] = { 0.5f, 3.0f, 12.0f }; double lv[3];
      for (int i = 0; i < 3; i++) {
          p = defaults(); p.decay_s = dec[i]; fdn_init(&g_fdn, FS); fdn_set(&g_fdn, &p);
          uint32_t r = 55u + i; float iL[BLOCK], iR[BLOCK], oL[BLOCK], oR[BLOCK]; double ei = 0, eo = 0;
          const int blocks = (int)(dec[i] * 4 * FS / BLOCK) + (int)(3 * FS / BLOCK);
          for (int b = 0; b < blocks; b++) {
              for (int k = 0; k < BLOCK; k++) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; iL[k] = 0.1f * (float)(int32_t)r / 2147483648.0f; r ^= r << 13; r ^= r >> 17; r ^= r << 5; iR[k] = 0.1f * (float)(int32_t)r / 2147483648.0f; }
              fdn_process(&g_fdn, iL, iR, oL, oR, BLOCK);
              if (b > blocks - (int)(2 * FS / BLOCK)) for (int k = 0; k < BLOCK; k++) { ei += (double)iL[k] * iL[k] + (double)iR[k] * iR[k]; eo += (double)oL[k] * oL[k] + (double)oR[k] * oR[k]; }
          }
          lv[i] = db(sqrt(eo / ei)); printf("         DECAY %5.1f s: wet %.1f dB relative to the input\n", dec[i], lv[i]);
      }
      const double spread = fmax(lv[0], fmax(lv[1], lv[2])) - fmin(lv[0], fmin(lv[1], lv[2]));
      CHECK(spread < 4.0, "the three decays differ by %.1f dB (limit 4)", spread);
      CHECK(fabs(lv[1]) < 6.0, "DECAY 3 s: %.1f dB against the input (within 6)", lv[1]); }

    printf("[8] stability: noise, extremes of every control, then silence: finite, bounded, and the tail dies\n");
    { int bad = 0, cases = 0; double worst = 0, worst_tail = -200;
      for (int s = 0; s < 24; s++) {
          fdn_params_t q = { (float)(s & 1), (s & 2) ? 25.0f : 0.2f, (float)((s >> 2) & 1), (s & 8) ? 1000.0f : 16000.0f, (float)((s >> 3) & 1), (s & 16) ? 3.0f : 0.05f, (s & 4) ? 1.5f : 0.0f, (s % 3) * 75.0f };
          fdn_init(&g_fdn, FS); fdn_set(&g_fdn, &q); uint32_t r = 9u + s; float iL[BLOCK], iR[BLOCK], oL[BLOCK], oR[BLOCK]; double pk = 0, tailpk = 0; int nf = 0;
          const int blocks = (int)((2.0 + (q.decay_s > 10 ? 40.0 : 12.0)) * FS / BLOCK);
          for (int b = 0; b < blocks; b++) {
              for (int k = 0; k < BLOCK; k++) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; iL[k] = (b < (int)(2.0 * FS / BLOCK)) ? (float)(int32_t)r / 2147483648.0f : 0.0f; iR[k] = -iL[k] * 0.7f; }
              fdn_process(&g_fdn, iL, iR, oL, oR, BLOCK);
              for (int k = 0; k < BLOCK; k++) { if (!isfinite(oL[k]) || !isfinite(oR[k])) nf = 1; pk = fmax(pk, fmax(fabs(oL[k]), fabs(oR[k]))); if (b > blocks - (int)(FS / BLOCK)) tailpk = fmax(tailpk, fmax(fabs(oL[k]), fabs(oR[k]))); }
          }
          cases++; if (nf || pk > 20.0) bad++; worst = fmax(worst, pk);
          const double tail_db = db(tailpk / (pk + 1e-30)); if (q.decay_s < 10 && tail_db > worst_tail) worst_tail = tail_db;
      }
      CHECK(bad == 0, "%d cases finite and bounded, loudest sample %.2f", cases, worst);
      CHECK(worst_tail < -30.0, "after the input stops and 12 s pass, the short-decay tails are %.1f dB below their peak (need < -30)", worst_tail); }

    printf("[9] no clicks when the controls move: a 220 Hz sine; each control changed alone at 3 s, then all together.\n"
           "    Click metric: the largest second difference over its surrounding RMS (3-4.5 for a smooth noise-like tail; a click is far above)\n");
    { const char *names[8] = { "SIZE 0.5 -> 1", "DECAY 3 -> 12 s", "DAMPING 11k -> 2k", "PRE-DELAY 0 -> 120 ms", "WIDTH 1 -> 0.2", "MOD 0.3 -> 1", "DIFFUSION 1 -> 0.2", "ALL together" };
      double worst = 0;
      for (int c = 0; c < 8; c++) {
          p = defaults(); fdn_init(&g_fdn, FS); fdn_set(&g_fdn, &p); const int blocks = (int)(8.0 * FS / BLOCK);
          float iL[BLOCK], iR[BLOCK], oL[BLOCK], oR[BLOCK]; float *o = calloc(blocks * BLOCK, 4);
          for (int b2 = 0; b2 < blocks; b2++) {
              if (b2 == (int)(3.0 * FS / BLOCK)) { fdn_params_t q = p; const int all = (c == 7);
                  if (c == 0 || all) q.size = 1.0f;
                  if (c == 1 || all) q.decay_s = 12.0f;
                  if (c == 2 || all) q.damp_hz = 2000.0f;
                  if (c == 3 || all) q.predelay_ms = 120.0f;
                  if (c == 4 || all) q.width = 0.2f;
                  if (c == 5 || all) q.mod = 1.0f;
                  if (c == 6 || all) q.diffusion = 0.2f;
                  fdn_set(&g_fdn, &q); }
              for (int k = 0; k < BLOCK; k++) { const double tt = (double)(b2 * BLOCK + k) / FS; iL[k] = iR[k] = (float)(0.3 * sin(2 * M_PI * 220.0 * tt)); }
              fdn_process(&g_fdn, iL, iR, oL, oR, BLOCK); memcpy(o + b2 * BLOCK, oL, sizeof oL);
          }
          double e = 0, m = 0; const int a0 = (int)(3.0 * FS), a1 = (int)(7.0 * FS);
          for (int i = a0 + 2; i < a1; i++) { const double d = (double)o[i] - 2.0 * o[i - 1] + o[i - 2]; e += d * d; if (fabs(d) > m) m = fabs(d); }
          const double ratio = m / sqrt(e / (a1 - a0 - 2) + 1e-30); if (ratio > worst) worst = ratio;
          printf("         %-22s spike ratio %.1f\n", names[c], ratio); free(o);
      }
      CHECK(worst < 8.0, "worst click ratio over the controls %.1f (limit 8)", worst); }


    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL FDN TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
