/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_glass.c - ANDROMEDA V1 GLASS (src/fx/v1_glass.c), measured on the PC.
 *
 *   clang -O2 -Icustom-fw/include -DPA_HOST_SIM=1 -DPA_PRODUCT_ANDROMEDA=1 custom-fw/test/test_glass.c custom-fw/src/fx/v1_glass.c -lm -o test_glass.exe
 *
 * Checks (the spec's GLASS items): stable at every setting and at both rates (48,000 and 44,118 Hz), silent without input, tuned to the TUNE knob,
 * decaying at the DECAY knob, the six materials at a matching level, a real stereo image that still sums to mono, and what a block costs.
 * Prints the numbers it checks so a change in the sound shows up in the log.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "v1_glass.h"

#define BLOCK 64
static int fails = 0;
#define CHECK(ok, ...) do { printf((ok) ? "  [PASS] " : "  [FAIL] "); printf(__VA_ARGS__); printf("\n"); if (!(ok)) fails++; } while (0)

/* a plucked guitar-like note: sharp attack, harmonics, ~0.5 s decay, peak ~ -12 dBFS */
static float pluck(double t, double f) {
    if (t < 0.0) return 0.0f;
    const double e = exp(-t * 6.0);
    return (float)(0.25 * e * (sin(2 * M_PI * f * t) + 0.5 * sin(4 * M_PI * f * t) + 0.25 * sin(6 * M_PI * f * t)) * (t < 0.0005 ? t / 0.0005 : 1.0));
}

typedef struct { double rms_play, rms_tail, peak; float *L, *R; int n; } run_t;

/* plays `notes` plucks 1 s apart, then `tail_s` of silence; returns the wet signal (caller frees) */
static run_t run(glass_t *g, float fs, int notes, double tail_s) {
    const int total = (int)((notes * 1.0 + tail_s) * fs) / BLOCK * BLOCK;
    run_t r = { 0, 0, 0, calloc(total, sizeof(float)), calloc(total, sizeof(float)), total };
    float in[BLOCK], wl[BLOCK], wr[BLOCK];
    double ep = 0, et = 0; long np = 0, nt = 0;
    for (int b = 0; b < total / BLOCK; b++) {
        for (int k = 0; k < BLOCK; k++) {
            const double t = (double)(b * BLOCK + k) / fs;
            const int idx = (int)t;
            in[k] = (idx < notes) ? pluck(t - idx, idx % 2 ? 247.0 : 196.0) : 0.0f;
        }
        glass_process(g, in, in, wl, wr, BLOCK);
        for (int k = 0; k < BLOCK; k++) {
            const int i = b * BLOCK + k;
            r.L[i] = wl[k]; r.R[i] = wr[k];
            const double a = fmax(fabs(wl[k]), fabs(wr[k]));
            if (a > r.peak) r.peak = a;
            if (i < (int)(notes * fs)) { ep += (double)wl[k] * wl[k] + (double)wr[k] * wr[k]; np += 2; }
            else { et += (double)wl[k] * wl[k] + (double)wr[k] * wr[k]; nt += 2; }
        }
    }
    r.rms_play = sqrt(ep / (np ? np : 1)); r.rms_tail = sqrt(et / (nt ? nt : 1));
    return r;
}

static double db(double x) { return 20.0 * log10(x + 1e-30); }

/* radix-2 FFT magnitude peak (parabolic interpolation) of x[0..n) around [f0 / 1.5, f0 * 1.5] */
static double peak_freq(const float *x, int n, double fs, double f0) {
    int N = 1; while (N < n) N <<= 1;
    double *re = calloc(N, sizeof(double)), *im = calloc(N, sizeof(double));
    for (int i = 0; i < n; i++) re[i] = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / (n - 1)));
    for (int i = 1, j = 0; i < N; i++) { int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (int len = 2; len <= N; len <<= 1) {
        const double ang = -2 * M_PI / len;
        for (int i = 0; i < N; i += len)
            for (int k = 0; k < len / 2; k++) {
                const double wr = cos(ang * k), wi = sin(ang * k);
                const double ur = re[i + k], ui = im[i + k];
                const double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi, vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k] = ur + vr; im[i + k] = ui + vi; re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
            }
    }
    const int lo = (int)(f0 / 1.5 * N / fs), hi = (int)(f0 * 1.5 * N / fs);
    int best = lo; double bm = 0;
    for (int i = lo; i <= hi; i++) { const double m = hypot(re[i], im[i]); if (m > bm) { bm = m; best = i; } }
    const double a = hypot(re[best - 1], im[best - 1]), b = bm, c = hypot(re[best + 1], im[best + 1]);
    const double d = 0.5 * (a - c) / (a - 2 * b + c + 1e-30);
    free(re); free(im);
    return (best + d) * fs / N;
}

int main(void) {
    printf("=================================================================\nANDROMEDA V1 GLASS: modal resonator engine\n=================================================================\n");
    const float rates[2] = { 48000.0f, 44118.0f };
    static const char *names[GLASS_MATERIALS] = { "CRYSTAL", "BELL", "KALIMBA", "METAL", "WOOD", "MARIMBA" };

    printf("[1] stability: 6 materials x tune -24/+24 x decay 0.15/12 s x inharm 0/1 x resonance 1, both rates, a loud noise burst then silence\n");
    int unstable = 0; double worst_peak = 0; int cases = 0;
    for (int ri = 0; ri < 2; ri++)
        for (int m = 0; m < GLASS_MATERIALS; m++)
            for (int ti = 0; ti < 2; ti++)
                for (int di = 0; di < 2; di++)
                    for (int ii = 0; ii < 2; ii++) {
                        glass_t g; glass_init(&g, rates[ri]);
                        glass_params_t p = { m, ti ? 24.0f : -24.0f, 1.0f, 1.0f, di ? 12.0f : 0.15f, (float)ii, 1.0f, 1.0f };
                        glass_set(&g, &p);
                        uint32_t rng = 12345u; float in[BLOCK], wl[BLOCK], wr[BLOCK]; double peak = 0; int bad = 0;
                        const int blocks = (int)(rates[ri] * (1.0 + 14.0) / BLOCK);
                        for (int b = 0; b < blocks; b++) {
                            for (int k = 0; k < BLOCK; k++) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; in[k] = (b < (int)(rates[ri] / BLOCK)) ? (float)(int32_t)rng / 2147483648.0f : 0.0f; }
                            glass_process(&g, in, in, wl, wr, BLOCK);
                            for (int k = 0; k < BLOCK; k++) { if (!isfinite(wl[k]) || !isfinite(wr[k])) bad = 1; peak = fmax(peak, fmax(fabs(wl[k]), fabs(wr[k]))); }
                        }
                        cases++; if (bad || peak > 8.0) unstable++; if (peak > worst_peak) worst_peak = peak;
                    }
    CHECK(unstable == 0, "%d cases: all finite and bounded; loudest sample %.2f (%.1f dBFS)", cases, worst_peak, db(worst_peak));

    printf("[2] silence in, silence out\n");
    { glass_t g; glass_init(&g, 48000.0f); float z[BLOCK] = {0}, wl[BLOCK], wr[BLOCK]; double pk = 0;
      for (int b = 0; b < 4000; b++) { glass_process(&g, z, z, wl, wr, BLOCK); for (int k = 0; k < BLOCK; k++) pk = fmax(pk, fmax(fabs(wl[k]), fabs(wr[k]))); }
      CHECK(pk < 1e-6, "no input -> output peak %.3g (%.0f dBFS)", pk, db(pk)); }

    printf("[3] tuning: the loudest partial sits on 220 Hz x 2^(tune/12), at 48,000 Hz and 44,118 Hz\n");
    double worst_cents = 0;
    for (int ri = 0; ri < 2; ri++)
        for (int ti = 0; ti < 5; ti++) {
            const float tune = -12.0f + 6.0f * ti;
            glass_t g; glass_init(&g, rates[ri]);
            glass_params_t p = { GLASS_CRYSTAL, tune, 0.8f, 0.2f, 6.0f, 0.0f, 0.0f, 0.0f };   /* dull: the fundamental dominates */
            glass_set(&g, &p);
            run_t r = run(&g, rates[ri], 1, 3.0);
            const double want = 220.0 * pow(2.0, tune / 12.0);
            const double got = peak_freq(r.L + (int)(0.2 * rates[ri]), (int)(2.0 * rates[ri]), rates[ri], want);
            const double cents = 1200.0 * log2(got / want);
            if (fabs(cents) > fabs(worst_cents)) worst_cents = cents;
            free(r.L); free(r.R);
        }
    CHECK(fabs(worst_cents) < 6.0, "worst tuning error %.2f cents over 10 settings (limit 6)", worst_cents);

    printf("[4] decay: the tail follows the DECAY knob (T60 of the fundamental)\n");
    for (int di = 0; di < 3; di++) {
        const float want = di == 0 ? 0.5f : (di == 1 ? 2.0f : 6.0f);
        glass_t g; glass_init(&g, 48000.0f);
        glass_params_t p = { GLASS_CRYSTAL, 0.0f, 0.8f, 0.2f, want, 0.0f, 0.0f, 0.0f };
        glass_set(&g, &p);
        run_t r = run(&g, 48000.0f, 1, want * 1.6 + 1.0);
        /* envelope in 50 ms windows after the note; find when it falls 60 dB below its first window */
        const int win = 2400; double first = 0; double t60 = -1;
        for (int w0 = (int)(0.05 * 48000); w0 + win < r.n; w0 += win) {
            double e = 0; for (int i = 0; i < win; i++) e += (double)r.L[w0 + i] * r.L[w0 + i];
            e = sqrt(e / win);
            if (first == 0) first = e;
            else if (db(e / first) < -60.0) { t60 = (double)(w0 - 0.05 * 48000) / 48000.0; break; }
        }
        CHECK(t60 > 0 && t60 > want * 0.55 && t60 < want * 1.45, "DECAY %.1f s -> measured -60 dB at %.2f s", want, t60);
        free(r.L); free(r.R);
    }

    printf("[5] level: the six materials play at a matching loudness (guitar pluck, 3 s)\n");
    double lv[GLASS_MATERIALS], lo = 1e9, hi = -1e9, peak_hi = -1e9;
    for (int m = 0; m < GLASS_MATERIALS; m++) {
        glass_t g; glass_init(&g, 48000.0f);
        glass_params_t p = { m, 0.0f, 0.6f, 0.6f, 2.5f, 0.0f, 0.0f, 0.6f };
        glass_set(&g, &p);
        run_t r = run(&g, 48000.0f, 3, 0.5);
        lv[m] = db(r.rms_play); lo = fmin(lo, lv[m]); hi = fmax(hi, lv[m]); peak_hi = fmax(peak_hi, db(r.peak));
        printf("         %-8s wet RMS %6.1f dBFS   peak %6.1f dBFS\n", names[m], lv[m], db(r.peak));
        free(r.L); free(r.R);
    }
    CHECK(hi - lo < 6.0, "materials within %.1f dB of each other (limit 6)", hi - lo);
    CHECK(lo > -30.0 && peak_hi < -1.0, "wet RMS %.1f .. %.1f dBFS, loudest peak %.1f dBFS: audible against a -12 dBFS pluck, with headroom", lo, hi, peak_hi);

    printf("[6] stereo: a mono guitar gives a real image that sums to mono without cancelling\n");
    { glass_t g; glass_init(&g, 48000.0f);
      glass_params_t p = { GLASS_BELL, 0.0f, 0.7f, 0.7f, 3.0f, 0.2f, 0.0f, 1.0f }; glass_set(&g, &p);
      run_t r = run(&g, 48000.0f, 3, 1.0);
      double sl = 0, sr = 0, slr = 0, sm = 0;
      for (int i = 0; i < r.n; i++) { sl += (double)r.L[i] * r.L[i]; sr += (double)r.R[i] * r.R[i]; slr += (double)r.L[i] * r.R[i]; const double m = 0.5 * (r.L[i] + r.R[i]); sm += m * m; }
      const double corr = slr / sqrt(sl * sr + 1e-30);
      const double mono_loss = db(sqrt(2 * sm / (sl + sr)));
      CHECK(corr < 0.95 && corr > 0.15, "L/R correlation %.2f (a real image: below 0.95, not unrelated: above 0.15)", corr);
      CHECK(mono_loss > -3.0, "mono sum keeps %.1f dB of the stereo power (limit -3)", mono_loss);
      free(r.L); free(r.R); }

    printf("[7] cost (host x86: relative only; the device figure comes from the pi32 inner-loop count)\n");
    { glass_t g; glass_init(&g, 48000.0f);
      glass_params_t p = { GLASS_METAL, 0.0f, 0.7f, 1.0f, 8.0f, 0.5f, 0.3f, 1.0f }; glass_set(&g, &p);
      run_t r = run(&g, 48000.0f, 4, 1.0);
      free(r.L); free(r.R);
      printf("         METAL, 12 modes x 2 channels: %d mode-channel resonators per sample\n", 2 * GLASS_MODES); }

    printf("[8] repeated strikes pile up inside the ringing modes (the first-second clipping): 2 notes per second, every material, a normal and a loud player\n");
    { const double notes[8] = { 196.0, 246.9, 293.7, 392.0, 329.6, 293.7, 246.9, 196.0 };
      double worst = 0; int worst_flat = 0, worst_m = 0; const int n = 48000 * 4;
      for (int m = 0; m < GLASS_MATERIALS; m++) for (int lvl = 0; lvl < 2; lvl++) {
          glass_t g; glass_init(&g, 48000.0f);
          glass_params_t p = { m, 0.0f, 0.7f, 0.7f, m == 4 ? 0.8f : (m == 5 ? 1.2f : 3.5f), 0.15f, 0.0f, 0.9f }; glass_set(&g, &p);
          const float amp = lvl ? 3.0f : 1.0f;                                                   /* 0.3 and 0.9 peak pluck */
          float *in = calloc(n, 4), *wl = calloc(n, 4), *wr = calloc(n, 4);
          for (int i = 0; i < n; i++) { const double t = (double)i / 48000.0; const int k = (int)(t / 0.5); in[i] = amp * pluck(t - k * 0.5, notes[(k + m) % 8]); }
          for (int i = 0; i < n; i += 64) glass_process(&g, in + i, in + i, wl + i, wr + i, 64);
          int flat = 0, run_len = 0;
          for (int i = 0; i < n; i++) { const double a = fmax(fabs(wl[i]), fabs(wr[i])); worst = fmax(worst, a);
              if (i > 0 && a > 0.9 * GLASS_CEIL && fabs(wl[i] - wl[i-1]) < 1e-6) { run_len++; if (run_len > flat) flat = run_len; } else run_len = 0; }
          if (flat > worst_flat) { worst_flat = flat; worst_m = m; }
          free(in); free(wl); free(wr);
      }
      CHECK(worst <= GLASS_CEIL * 1.01, "the wet output never passes the ceiling: loudest sample %.5f (ceiling %.2f; before the limiter a run of notes reached 1.0)", worst, GLASS_CEIL);
      CHECK(worst_flat <= 2, "no flat tops: the longest run of identical samples near the ceiling is %d (a clamp repeats the same value) (material %d)", worst_flat, worst_m); }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL GLASS TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
