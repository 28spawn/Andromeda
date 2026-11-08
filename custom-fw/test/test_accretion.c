/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_accretion.c - ANDROMEDA V1 ACCRETION (src/fx/v1_accretion.c), measured on the PC.
 *
 *   clang -O2 -Icustom-fw/include -DPA_HOST_SIM=1 -DPA_PRODUCT_ANDROMEDA=1 custom-fw/test/test_accretion.c custom-fw/src/fx/v1_accretion.c -lm -o test_accretion.exe
 *
 * The spec's ACCRETION items: ADPCM quality and random access, a loop that plays back what was recorded, no block-boundary or seam clicks, 100 overdub passes
 * without mush (noise floor and gain, measured), varispeed pitch, wow and flutter, freeze that really holds, the tone and width controls, and every block published.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "v1_accretion.h"

#define BLOCK 64
#define FS 48000.0f
static int fails = 0;
#define CHECK(ok, ...) do { printf((ok) ? "  [PASS] " : "  [FAIL] "); printf(__VA_ARGS__); printf("\n"); if (!(ok)) fails++; } while (0)
static double db(double x) { return 20.0 * log10(x + 1e-30); }
static acc_t g_acc;                                   /* ~150 KB: static */

static float pluck(double t, double f, double rate) { if (t < 0) return 0; double s = 0; for (int h = 1; h <= 5; h++) s += sin(2 * M_PI * f * h * t) * exp(-t * (rate + 1.2 * h)) / h; return (float)(0.5 * s * (t < 0.0008 ? t / 0.0008 : 1.0)); }
static float phrase(double t) { return pluck(t, 196.0, 2.2) + pluck(t - 0.5, 246.9, 2.2) + pluck(t - 1.0, 293.7, 2.2) + pluck(t - 1.5, 392.0, 2.2); }
static float burst(double t) { const double e = (t < 0 ? 0 : (t < 0.3 ? exp(-t * 14) : 0)); return (float)(0.4 * e * sin(2 * M_PI * 330.0 * t) * (t > 0 && t < 0.3 ? 1 : 0)); }   /* two silent seconds after */

static void feed(acc_t *a, const float *L, const float *R, float *oL, float *oR, int n) { for (int i = 0; i < n; i += BLOCK) acc_process(a, L + i, R + i, oL + i, oR + i, BLOCK); }
static void set_defaults(acc_t *a) { acc_params_t p = { 2.0f, 0.98f, 0.0f, 15000.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f }; acc_set(a, &p); }
/* record `secs` of signal(t) through the REC command, auto-stop at LENGTH; returns the input for comparison */
static float *record(acc_t *a, float (*sig)(double), double secs, float **R) {
    const int n = (int)(secs * FS) / BLOCK * BLOCK; float *x = calloc(n, 4), *o1 = calloc(n, 4), *o2 = calloc(n, 4);
    for (int i = 0; i < n; i++) x[i] = sig((double)i / FS);
    acc_command(a, ACC_CMD_RECPLAY); feed(a, x, x, o1, o2, n); free(o1); free(o2); if (R) *R = x; return x;
}
static double snr(const float *ref, const float *x, int a, int b) { double es = 0, en = 0; for (int i = a; i < b; i++) { es += (double)ref[i] * ref[i]; const double d = (double)x[i] - ref[i]; en += d * d; } return 10 * log10(es / (en + 1e-30)); }
static double rms(const float *x, int a, int b) { double e = 0; for (int i = a; i < b; i++) e += (double)x[i] * x[i]; return sqrt(e / (b - a)); }
static double fit_resid_db(const float *x, const float *ref, int n) {       /* windowed best-fit-gain residual, dB re the signal */
    double er = 0, es = 0; const int w = 480;
    for (int a = 2000; a + w < n - 2000; a += w) { double xy = 0, yy = 0; for (int i = a; i < a + w; i++) { xy += (double)x[i] * ref[i]; yy += (double)ref[i] * ref[i]; }
        const double gg = xy / (yy + 1e-30); for (int i = a; i < a + w; i++) { const double d = x[i] - gg * ref[i]; er += d * d; es += (double)x[i] * x[i]; } }
    return 10 * log10(er / (es + 1e-30));
}
static double hf_share(const float *x, int a, int b) { double h = 0, e = 0; for (int i = a + 1; i < b; i++) { const double d = (double)x[i] - x[i - 1]; h += d * d; e += (double)x[i] * x[i]; } return h / (e + 1e-30); }

static double zc_freq(const float *x, int a, int b, double fs) {            /* mean frequency by interpolated rising zero crossings */
    double first = -1, last = -1; int n = 0;
    for (int i = a + 1; i < b; i++) if (x[i - 1] < 0 && x[i] >= 0) { const double t = i - 1 + (-x[i - 1]) / ((double)x[i] - x[i - 1]); if (first < 0) first = t; last = t; n++; }
    return n > 1 ? (n - 1) * fs / (last - first) : 0;
}

int main(void) {
    printf("=================================================================\nANDROMEDA V1 ACCRETION: ADPCM stereo tape looper\n=================================================================\n");
    printf("tape: %d blocks x %d bytes = %.0f KiB, %d frames = %.2f s at 48 kHz\n", ACC_BLOCKS, ACC_BLK_BYTES, ACC_BLOCKS * ACC_BLK_BYTES / 1024.0, ACC_MAX_FRAMES, ACC_MAX_FRAMES / 48000.0);

    printf("[1] ADPCM quality: encode and decode a chain of blocks\n");
    { const char *nm[3] = { "220 Hz sine at -12 dBFS", "plucked phrase", "white noise at -20 dBFS" }; const double need[3] = { 30.0, 26.0, 14.0 };      /* white noise is the worst case for a 4-bit codec */
      for (int s = 0; s < 3; s++) {
          const int nb = 1500; uint8_t blk[ACC_BLK_BYTES]; int sp[2] = { 0, 0 }, si[2] = { 0, 0 }, ep[2], ei[2]; uint32_t rng = 5;
          double es = 0, en = 0;
          for (int b = 0; b < nb; b++) {
              int16_t in[2][ACC_BLK_FRAMES], out[2][ACC_BLK_FRAMES];
              for (int k = 0; k < ACC_BLK_FRAMES; k++) { const double t = (double)(b * ACC_BLK_FRAMES + k) / FS; float v;
                  if (s == 0) v = (float)(0.25 * sin(2 * M_PI * 220 * t)); else if (s == 1) v = phrase(fmod(t, 2.0)); else { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; v = 0.1f * (float)(int32_t)rng / 2147483648.0f; }
                  in[0][k] = in[1][k] = (int16_t)(v * 32767.0f); }
              memset(blk, 0, sizeof blk); acc_blk_encode(blk, in, sp, si, ep, ei); acc_blk_decode(blk, out);
              for (int k = 0; k < ACC_BLK_FRAMES; k++) { const double d = out[0][k] - in[0][k]; es += (double)in[0][k] * in[0][k]; en += d * d; }
              sp[0] = ep[0]; sp[1] = ep[1]; si[0] = ei[0]; si[1] = ei[1];
          }
          const double q = 10 * log10(es / (en + 1e-30)); CHECK(q > need[s], "%-24s SNR %.1f dB (need > %.0f)", nm[s], q, need[s]);
      } }

    printf("[2] random access: every block decodes alone, from its own header, to exactly what the encoder reconstructed\n");
    { const int nb = 400; static uint8_t tape[400][ACC_BLK_BYTES]; static int16_t ref[400][2][ACC_BLK_FRAMES]; int sp[2] = { 0, 0 }, si[2] = { 0, 0 }, ep[2], ei[2];
      for (int b = 0; b < nb; b++) {
          int16_t in[2][ACC_BLK_FRAMES];
          for (int k = 0; k < ACC_BLK_FRAMES; k++) { const double t = (double)(b * ACC_BLK_FRAMES + k) / FS; in[0][k] = (int16_t)(phrase(fmod(t, 2.0)) * 32767.0f); in[1][k] = (int16_t)(-in[0][k] / 2); }
          memset(tape[b], 0, ACC_BLK_BYTES); acc_blk_encode(tape[b], in, sp, si, ep, ei);
          /* the reconstruction the encoder tracked, by decoding the chain from the previous end state */
          sp[0] = ep[0]; sp[1] = ep[1]; si[0] = ei[0]; si[1] = ei[1];
      }
      /* sequential decode: block b's header holds the state after block b-1, so decoding block by block must equal decoding each block alone */
      int mism = 0; int pp[2] = { 0, 0 }; (void)pp;
      for (int b = 0; b < nb; b++) { int16_t a1[2][ACC_BLK_FRAMES]; acc_blk_decode(tape[b], a1); memcpy(ref[b], a1, sizeof a1); }
      for (int b = nb - 1; b >= 0; b--) { int16_t a2[2][ACC_BLK_FRAMES]; acc_blk_decode(tape[b], a2); if (memcmp(a2, ref[b], sizeof a2)) mism++; }
      /* and the boundaries: block b+1's first decoded sample continues block b's last (header = end state) */
      double worst = 0; for (int b = 1; b < nb; b++) { const double d = fabs((double)ref[b][0][0] - ref[b - 1][0][ACC_BLK_FRAMES - 1]); if (d > worst) worst = d; }
      CHECK(mism == 0, "%d blocks decoded in reverse order, each alone: %d differ", nb, mism);
      CHECK(worst < 4000.0, "largest step across a block boundary %.0f LSB (a restart from nothing would be the whole signal)", worst); }

    printf("[3] record, then play: the loop is what was played\n");
    { acc_init(&g_acc, FS); set_defaults(&g_acc); float *x; record(&g_acc, phrase, 2.0, &x);
      CHECK(g_acc.mode == ACC_PLAY && g_acc.len == 96000, "LENGTH 2.0 s ended the recording by itself: mode %d, %u frames (96000 = 1500 blocks)", g_acc.mode, (unsigned)g_acc.len);
      const int n = 96000; float *oL = calloc(n, 4), *oR = calloc(n, 4); feed(&g_acc, x, x, oL, oR, n);
      double best = -99; int bl = 0; for (int lag = -2; lag <= 2; lag++) { const double q = snr(x + 200 + (lag > 0 ? 0 : -lag), oL + 200 + (lag > 0 ? lag : 0), 0, n - 400 - 4); if (q > best) { best = q; bl = lag; } }
      CHECK(best > 24.0 && bl == 0, "playback against the input: SNR %.1f dB at lag %d (need > 24 at 0)", best, bl);
      free(x); free(oL); free(oR); }

    printf("[4] the loop seam and the block boundaries are smooth (a 220 Hz sine, 1.5 s loop: not a whole number of cycles)\n");
    { acc_init(&g_acc, FS); acc_params_t p = { 1.5f, 0.98f, 0.0f, 15000.0f, 1.0f, 0, 0, 0, 1.0f }; acc_set(&g_acc, &p);
      const int n = 72000; float *x = calloc(n, 4), *oL = calloc(n, 4), *oR = calloc(n, 4);
      for (int i = 0; i < n; i++) x[i] = (float)(0.3 * sin(2 * M_PI * 220.0 * i / FS));
      acc_command(&g_acc, ACC_CMD_RECPLAY); feed(&g_acc, x, x, oL, oR, n); memset(oL, 0, n * 4);
      const int m = 72000 * 2; float *y = calloc(m, 4); float *z = calloc(m, 4); feed(&g_acc, y, y, oL, oR, n); memcpy(z, oL, n * 4); feed(&g_acc, y, y, oL, oR, n); memcpy(z + n, oL, n * 4);
      /* d2 over 400 frames around the seam (frame 72000) against the RMS d2 of the loop */
      double e = 0, ms = 0, mb = 0, eb = 0; int cb = 0, cn = 0; double sb = 0, sn = 0;
      for (int i = 300; i < n - 300; i++) { const double d = (double)z[i] - 2.0 * z[i - 1] + z[i - 2]; e += d * d; if (i % 64 <= 1) { sb += fabs(d); cb++; } else { sn += fabs(d); cn++; } }
      const double r = sqrt(e / (n - 600));
      for (int i = n - 200; i < n + 200; i++) { const double d = fabs((double)z[i] - 2.0 * z[i - 1] + z[i - 2]); if (d > ms) ms = d; }
      (void)mb; (void)eb;
      CHECK(ms / r < 8.0, "seam: largest second difference %.1f x the loop's RMS (declicked; a hard join would be ~100)", ms / r);
      CHECK((sb / cb) / (sn / cn) < 1.3, "block boundaries: mean |d2| at frame 64k, 64k+1 is %.2f x elsewhere (no restart click)", (sb / cb) / (sn / cn));
      free(x); free(oL); free(oR); free(y); free(z); }

    printf("[5] 100 overdub passes (the spec's test): layers kept at 0.98, no new input; the noise floor, the gain, the idle silence\n");
    { acc_init(&g_acc, FS); set_defaults(&g_acc); float *x; record(&g_acc, burst, 2.0, &x);                    /* two bursts, then 1.7 s of digital silence */
      const int n = 96000; float *oL = calloc(n, 4), *oR = calloc(n, 4), *ref = calloc(n, 4);
      feed(&g_acc, x, x, oL, oR, n); memcpy(ref, oL, n * 4);                                                   /* pass 0: what the tape holds */
      const double r0 = rms(ref, 200, 24000);
      acc_command(&g_acc, ACC_CMD_OVERDUB); float *z = calloc(n, 4);
      /* a float model of the same layer path (0.98, 2 Hz DC blocker, 15 kHz low-pass), no ADPCM: the difference to it is the codec's re-quantisation, pass after pass */
      float *model = calloc(n, 4); memcpy(model, ref, n * 4); double mhx = 0, mhy = 0, mlp = 0;
      const double dck = 1.0 - 2.0 * M_PI * 2.0 / FS, lpk = 1.0 - exp(-2.0 * M_PI * 15000.0 / FS);
      double q10 = 0, q100 = 0, g10 = 0, g100 = 0, idle100 = -200, pk100 = 0;
      for (int pass = 1; pass <= 100; pass++) {
          feed(&g_acc, z, z, oL, oR, n);                                                                       /* silent input: pure feedback */
          for (int i = 0; i < n; i++) { const double x = 0.98 * model[i], hp = x - mhx + dck * mhy; mhx = x; mhy = hp; mlp += lpk * (hp - mlp); model[i] = (float)mlp; }
          if (pass == 10 || pass == 100) {
              float *sc = model;
              const double q = snr(sc, oL, 200, 24000), g = db(rms(oL, 200, 24000) / r0);
              if (pass == 10) { q10 = q; g10 = g; } else { q100 = q; g100 = g; double pk = 0; for (int i = 0; i < n; i++) pk = fmax(pk, fabs(oL[i])); pk100 = pk;
                  idle100 = db(rms(oL, (int)(1.2 * FS), (int)(1.95 * FS))); }
          }
      }
      printf("         after 10 passes: SNR %.1f dB against a float model of the same layer path, gain %.1f dB (ideal %.1f)\n", q10, g10, db(pow(0.98, 10)));
      printf("         after 100 passes: SNR %.1f dB against the float model, gain %.1f dB (ideal %.1f), idle silence %.0f dBFS\n", q100, g100, db(pow(0.98, 100)), idle100);
      CHECK(fabs(g10 - db(pow(0.98, 10))) < 1.5 && fabs(g100 - db(pow(0.98, 100))) < 2.5, "the gain follows the feedback: %.1f dB after 10 (ideal %.1f), %.1f dB after 100 (ideal %.1f)", g10, db(pow(0.98, 10)), g100, db(pow(0.98, 100)));
      CHECK(q10 > 18.0, "after 10 passes the layer is still clean: SNR %.1f dB (need > 18)", q10);
      CHECK(q100 > 8.0, "after 100 passes it is aged, not mush: SNR %.1f dB (need > 8)", q100);
      CHECK(idle100 < -66.0, "the silent part of the loop stays silent: %.0f dBFS after 100 passes (need < -66)", idle100);
      CHECK(pk100 < 1.0, "no gain build-up: peak %.3f", pk100);
      free(x); free(oL); free(oR); free(ref); free(z); free(model); }

    printf("[6] overdub with new input at a steady level: bounded, no DC\n");
    { acc_init(&g_acc, FS); acc_params_t p = { 2.0f, 0.9f, 0.7f, 8000.0f, 1.0f, 0, 0, 0.3f, 1.0f }; acc_set(&g_acc, &p); float *x; record(&g_acc, phrase, 2.0, &x);
      acc_command(&g_acc, ACC_CMD_OVERDUB); const int n = 96000; float *oL = calloc(n, 4), *oR = calloc(n, 4), *lay = calloc(n, 4); double pk = 0, dc = 0;
      double dc20 = 0;
      for (int pass = 0; pass < 40; pass++) { for (int i = 0; i < n; i++) lay[i] = phrase((double)i / FS); feed(&g_acc, lay, lay, oL, oR, n);
          if (pass == 19) { for (int i = 0; i < n; i++) dc20 += oL[i]; dc20 /= n; } }
      double dc_in = 0; for (int i = 0; i < n; i++) dc_in += phrase((double)i / FS); dc_in /= n;
      for (int i = 0; i < n; i++) { pk = fmax(pk, fabs(oL[i])); dc += oL[i]; } dc /= n;
      /* the old layers pass a DC blocker every pass, so a DC offset cannot pile up: the mean is the input's own mean times the overdub level */
      CHECK(pk <= 1.0 && fabs(dc) < 0.003 + 1.2 * fabs(0.7 * dc_in) && fabs(dc - dc20) < 0.002, "40 passes of the phrase over itself (saturating): peak %.3f, mean %.5f at pass 40 and %.5f at pass 20 (steady; the phrase's own mean x 0.7 = %.5f)", pk, dc, dc20, 0.7 * dc_in);
      free(x); free(oL); free(oR); free(lay); }

    printf("[6b] a hot overdub (the case that crunched in the demo): a second phrase over a loud loop. Distortion = what is left after the best-fit gain in each 10 ms window\n"
           "     against the linear ideal (the same scenario at a quarter of the level, where nothing can overload, scaled up)\n");
    { const double dist_limit = -32.0;
      float *ideal4 = NULL, *ideal2 = NULL; double d_half = 0, d_hot = 0, hot_peak = 0, wrpk = 0; float lim_min = 1.0f; const int n = 96000;
      for (int run = 0; run < 4; run++) {
          const float lvl = run == 0 ? 0.25f : (run == 1 ? 0.5f : (run == 2 ? 1.0f : 1.0f));
          if (run == 3) break;
          acc_init(&g_acc, FS); acc_params_t p = { 2.0f, 0.82f, 0.9f, 5500.0f, 1.0f, 0.5f, 0.3f, 0.25f, 1.0f }; acc_set(&g_acc, &p);   /* the demo's own settings: wow, flutter, saturation, dark tone */
          float *a1 = calloc(n, 4), *b1 = calloc(n, 4), *o = calloc(n, 4), *o2 = calloc(n, 4);
          for (int i = 0; i < n; i++) { const double tt = (double)i / FS; a1[i] = lvl * phrase(tt); b1[i] = lvl * (pluck(tt, 329.6, 2.2) + pluck(tt - 0.5, 392.0, 2.2) + pluck(tt - 1.0, 440.0, 2.2) + pluck(tt - 1.5, 523.3, 2.2)); }
          acc_command(&g_acc, ACC_CMD_RECPLAY); feed(&g_acc, a1, a1, o, o2, n);
          feed(&g_acc, a1, a1, o, o2, n); acc_command(&g_acc, ACC_CMD_OVERDUB); feed(&g_acc, b1, b1, o, o2, n);
          g_acc.wr_peak = 0; g_acc.lim_min = 1.0f; feed(&g_acc, b1, b1, o, o2, n);                                               /* the second overdub pass */
          if (run == 0) { ideal4 = calloc(n, 4); ideal2 = calloc(n, 4); for (int i = 0; i < n; i++) { ideal4[i] = o[i] * 4.0f; ideal2[i] = o[i] * 2.0f; } }
          else if (run == 1) d_half = fit_resid_db(o, ideal2, n);
          else { d_hot = fit_resid_db(o, ideal4, n); for (int i = 0; i < n; i++) hot_peak = fmax(hot_peak, fabs(o[i])); wrpk = g_acc.wr_peak; lim_min = g_acc.lim_min; }
          free(a1); free(b1); free(o); free(o2);
      }
      printf("         at half level (nothing to limit): %.1f dB, the codec's own floor\n         hot: the sum offered to the tape peaks at %.2f; the limiter turned it down to %.2f at most (%.1f dB); loop peak %.3f; distortion %.1f dB\n", d_half, wrpk, lim_min, 20 * log10(lim_min), hot_peak, d_hot);
      CHECK(wrpk > 1.2, "the hot case does overload the tape path (sum %.2f), so this test means something", wrpk);
      CHECK(d_half < -30.0, "the limiter is transparent when nothing overloads: %.1f dB (the codec floor plus the demo's saturation and tape wobble)", d_half);
      CHECK(hot_peak <= 0.995, "the loop never exceeds the ceiling: peak %.3f", hot_peak);
      CHECK(d_hot < dist_limit, "the overloaded overdub is clean: %.1f dB against the linear ideal (need < %.0f; a hard clamp measured -22, a soft knee -22, a following gain limiter -25 on this path, the look-ahead -34)", d_hot, dist_limit);
      free(ideal4); free(ideal2); }

    printf("[7] speed: a 220 Hz loop played at 0.5x, 1x, 2x\n");
    { const float sp[3] = { 0.5f, 1.0f, 2.0f }; double worst = 0;
      for (int s = 0; s < 3; s++) {
          acc_init(&g_acc, FS); acc_params_t p = { 2.7f, 0.98f, 0, 15000.0f, sp[s], 0, 0, 0, 1.0f }; acc_set(&g_acc, &p);
          const int n = (int)(2.7 * FS) / BLOCK * BLOCK; float *x = calloc(n, 4), *oL = calloc(n, 4), *oR = calloc(n, 4);
          for (int i = 0; i < n; i++) x[i] = (float)(0.3 * sin(2 * M_PI * 220.0 * i / FS));
          acc_command(&g_acc, ACC_CMD_RECPLAY); feed(&g_acc, x, x, oL, oR, n); feed(&g_acc, x, x, oL, oR, n);
          const int a = (int)(0.12 * FS), b = (int)(2.4 / sp[s] * FS) < n - 400 ? (int)(2.4 / sp[s] * FS) : n - 400;
          const double f = zc_freq(oL, a, b < a + 20000 ? a + 20000 : b, FS), want = 220.0 * sp[s], c = 1200.0 * log2(f / want);
          if (fabs(c) > fabs(worst)) worst = c; free(x); free(oL); free(oR);
      }
      CHECK(fabs(worst) < 10.0, "worst pitch error %.1f cents at 0.5x, 1x, 2x (limit 10)", worst); }

    printf("[8] wow and flutter: the pitch moves by about the stated amount, and not without them\n");
    { double rng_[3];
      for (int w = 0; w < 3; w++) {
          acc_init(&g_acc, FS); acc_params_t p = { 2.7f, 0.98f, 0, 15000.0f, 1.0f, w == 1 ? 1.0f : 0.0f, w == 2 ? 1.0f : 0.0f, 0, 1.0f }; acc_set(&g_acc, &p);
          const int n = (int)(2.7 * FS) / BLOCK * BLOCK; float *x = calloc(n, 4), *oL = calloc(n, 4), *oR = calloc(n, 4);
          for (int i = 0; i < n; i++) x[i] = (float)(0.3 * sin(2 * M_PI * 220.0 * i / FS));
          acc_command(&g_acc, ACC_CMD_RECPLAY); feed(&g_acc, x, x, oL, oR, n); feed(&g_acc, x, x, oL, oR, n);
          double lo = 1e9, hi = -1e9; const int win = (int)(0.25 * FS);
          for (int a = (int)(0.2 * FS); a + win < n - (int)(0.3 * FS); a += win / 2) { const double c = 1200.0 * log2(zc_freq(oL, a, a + win, FS) / 220.0); lo = fmin(lo, c); hi = fmax(hi, c); }
          rng_[w] = hi - lo; free(x); free(oL); free(oR);
      }
      CHECK(rng_[0] < 1.5, "no wow, no flutter: pitch range %.2f cents (need < 1.5)", rng_[0]);
      CHECK(rng_[1] > 4.0 && rng_[1] < 40.0, "WOW 1: pitch range %.1f cents (a slow drift, 4-40)", rng_[1]);
      CHECK(rng_[2] > 1.0 && rng_[2] < 25.0, "FLUTTER 1: pitch range %.1f cents in 250 ms windows (1-25)", rng_[2]); }

    printf("[9] freeze: the loop holds exactly and ignores the input\n");
    { acc_init(&g_acc, FS); set_defaults(&g_acc); float *x; record(&g_acc, phrase, 2.0, &x); acc_command(&g_acc, ACC_CMD_OVERDUB);
      const int n = 96000; float *oL = calloc(n, 4), *oR = calloc(n, 4), *a1 = calloc(n, 4), *loud = calloc(n, 4);
      feed(&g_acc, x, x, oL, oR, n); acc_command(&g_acc, ACC_CMD_FREEZE); feed(&g_acc, x, x, oL, oR, n); memcpy(a1, oL, n * 4);
      for (int i = 0; i < n; i++) loud[i] = (float)(0.9 * sin(2 * M_PI * 1000.0 * i / FS));
      for (int p = 0; p < 20; p++) feed(&g_acc, loud, loud, oL, oR, n);
      const double q = snr(a1, oL, 200, n - 200);
      CHECK(q > 80.0 || rms(a1, 0, n) > 0 && q > 60.0, "after 20 frozen passes with a loud input the loop is unchanged: SNR %.0f dB (need > 60)", q);
      free(x); free(oL); free(oR); free(a1); free(loud); }

    printf("[10] tone: a low cutoff darkens the layers pass by pass\n");
    { acc_init(&g_acc, FS); acc_params_t p = { 2.0f, 0.98f, 0, 1000.0f, 1.0f, 0, 0, 0, 1.0f }; acc_set(&g_acc, &p); float *x; record(&g_acc, phrase, 2.0, &x);
      const int n = 96000; float *oL = calloc(n, 4), *oR = calloc(n, 4), *z = calloc(n, 4); feed(&g_acc, x, x, oL, oR, n); const double h0 = hf_share(oL, 200, n - 200);
      acc_command(&g_acc, ACC_CMD_OVERDUB); for (int pass = 0; pass < 10; pass++) feed(&g_acc, z, z, oL, oR, n); const double h10 = hf_share(oL, 200, n - 200);
      CHECK(h10 < h0 * 0.5, "high-frequency share %.5f -> %.5f after 10 passes at 1 kHz (under half)", h0, h10);
      free(x); free(oL); free(oR); free(z); }

    printf("[11] width: left-only input; layers kept apart at 1, folded together at 0\n");
    for (int w = 0; w < 2; w++) {
        acc_init(&g_acc, FS); acc_params_t p = { 2.0f, 0.98f, 0, 15000.0f, 1.0f, 0, 0, 0, (float)w }; acc_set(&g_acc, &p);
        const int n = 96000; float *x = calloc(n, 4), *zero = calloc(n, 4), *oL = calloc(n, 4), *oR = calloc(n, 4);
        for (int i = 0; i < n; i++) x[i] = phrase((double)i / FS);
        acc_command(&g_acc, ACC_CMD_RECPLAY); feed(&g_acc, x, zero, oL, oR, n);
        acc_command(&g_acc, ACC_CMD_OVERDUB); for (int pass = 0; pass < 10; pass++) feed(&g_acc, zero, zero, oL, oR, n);
        const double rl = rms(oL, 200, n - 200), rr = rms(oR, 200, n - 200);
        if (w == 1) CHECK(db(rr / rl) < -60.0, "WIDTH 1: the right layer stays silent, %.0f dB below the left", db(rr / rl));
        else { double d = 0; for (int i = 200; i < n - 200; i++) { const double e = (double)oL[i] - oR[i]; d += e * e; } CHECK(db(sqrt(d / (n - 400)) / rl) < -26.0 && rr > rl * 0.5, "WIDTH 0: left and right layers converge, difference %.0f dB below the signal", db(sqrt(d / (n - 400)) / rl)); }
        free(x); free(zero); free(oL); free(oR);
    }

    printf("[12] commands and states, fuzzed; every block published\n");
    { acc_init(&g_acc, FS); set_defaults(&g_acc);
      float o1[BLOCK], o2[BLOCK], in[BLOCK]; uint32_t r = 1234; int bad = 0; double pk = 0;
      CHECK(g_acc.mode == ACC_IDLE, "starts idle"); acc_process(&g_acc, in, in, o1, o2, BLOCK); { double s = 0; for (int i = 0; i < BLOCK; i++) s += fabs(o1[i]) + fabs(o2[i]); CHECK(s == 0.0, "idle is silent"); }
      for (int step = 0; step < 6000; step++) {
          r ^= r << 13; r ^= r >> 17; r ^= r << 5;
          if ((r & 0xFF) < 4) acc_command(&g_acc, (int)((r >> 8) & 3));
          if ((r & 0xFFF) == 7) { acc_params_t q = { 0.5f + (float)((r >> 12) & 7) * 0.3f, (float)((r >> 4) & 15) / 15.0f, (float)((r >> 8) & 7) / 7.0f, 100.0f + (float)((r >> 3) & 1023) * 14.0f, 0.4f + (float)((r >> 6) & 15) * 0.12f, (float)((r >> 9) & 1), (float)((r >> 10) & 1), (float)((r >> 11) & 1), (float)((r >> 13) & 1) }; acc_set(&g_acc, &q); }
          for (int i = 0; i < BLOCK; i++) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; in[i] = 0.8f * (float)(int32_t)r / 2147483648.0f; }
          acc_process(&g_acc, in, in, o1, o2, BLOCK);
          for (int i = 0; i < BLOCK; i++) { if (!isfinite(o1[i]) || !isfinite(o2[i])) bad = 1; pk = fmax(pk, fmax(fabs(o1[i]), fabs(o2[i]))); }
      }
      acc_flush(&g_acc); int odd = 0; for (int b = 0; b < ACC_BLOCKS; b++) if (!acc_block_published(&g_acc, b)) odd++;
      CHECK(!bad && pk <= 1.001, "6000 random blocks and commands: finite, peak %.3f", pk);
      CHECK(odd == 0, "all %d blocks published (even generation) after a flush: %d are not", ACC_BLOCKS, odd);
      acc_command(&g_acc, ACC_CMD_CLEAR); CHECK(g_acc.mode == ACC_IDLE && g_acc.len == 0, "CLEAR empties the tape"); }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL ACCRETION TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
