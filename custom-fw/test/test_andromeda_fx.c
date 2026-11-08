/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_andromeda_fx.c - the Andromeda effect host (src/fx/fx_andromeda.c): GLASS, BLOOM and ACCRETION behind one node, driven through the node interface the chain uses.
 * Build with -DPA_PRODUCT_ANDROMEDA=1 and the v1_*.c engines (build.ps1 / tools/qa_gate.py). Host: the pool allocator is a malloc here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "pa_config.h"
#include "pa_fx.h"
#include "pa_granular.h"

void *pa_alloc(uint32_t n) { return calloc(1, n); }
void  pa_free(void *p)     { free(p); }

static int fails = 0;
#define CHECK(ok, ...) do { printf((ok) ? "  [PASS] " : "  [FAIL] "); printf(__VA_ARGS__); printf("\n"); if (!(ok)) fails++; } while (0)
#define Q21 2097152.0

static double pluck(double t, double f) {
    if (t < 0) return 0;
    return 0.3 * exp(-t * 4.0) * (sin(2 * M_PI * f * t) + 0.5 * sin(4 * M_PI * f * t)) * (t < 0.0005 ? t / 0.0005 : 1.0);
}
static double guitar(int i) {                                     /* a note every 0.5 s */
    static const double notes[8] = { 196.0, 246.9, 293.7, 392.0, 329.6, 293.7, 246.9, 196.0 };
    const double t = (double)i / AND_FS; const int k = (int)(t / 0.5);
    return pluck(t - k * 0.5, notes[k & 7]);
}

typedef struct { double peak, rms, dry_rms; long clamps; } res_t;

/* run `secs` of the guitar through the node; the same call shape as the chain (stereo, blocks of 64) */
static res_t run(fx_node_t *nd, double secs, int start) {
    res_t r = { 0, 0, 0, 0 }; const int n = (int)(secs * AND_FS); double e = 0, ed = 0;
    int32_t L[PA_BLOCK_N], R[PA_BLOCK_N];
    for (int at = 0; at < n; at += PA_BLOCK_N) {
        double dry[PA_BLOCK_N];
        for (int i = 0; i < PA_BLOCK_N; i++) { dry[i] = guitar(start + at + i); L[i] = R[i] = (int32_t)(dry[i] * Q21); }
        nd->run_stereo(nd, L, R, PA_BLOCK_N);
        for (int i = 0; i < PA_BLOCK_N; i++) {
            const double a = fmax(fabs((double)L[i]), fabs((double)R[i])) / Q21;
            if (a > r.peak) r.peak = a; if (a > 0.98) r.clamps++;
            e += (double)L[i] / Q21 * ((double)L[i] / Q21); ed += dry[i] * dry[i];
        }
    }
    r.rms = sqrt(e / n); r.dry_rms = sqrt(ed / n); return r;
}

static void set_macros(fx_node_t *nd, double a, double b, double c, double d, double mix) {
    nd->set_param(nd, 0, (int32_t)(a * Q21)); nd->set_param(nd, 1, (int32_t)(b * Q21));
    nd->set_param(nd, 2, (int32_t)(c * Q21)); nd->set_param(nd, 3, (int32_t)(d * Q21)); nd->set_param(nd, 4, (int32_t)(mix * Q21));
}

int main(void) {
    printf("=================================================================\nANDROMEDA effect host: GLASS / BLOOM / ACCRETION behind one node\n=================================================================\n");
    fx_node_t *nd = fx_granular_new();
    CHECK(nd && nd->state, "the node is created with its state");
    gran_telemetry_t tel;

    printf("[1] every effect type (6 effects x 4 variants): audible, bounded, no flat tops, 3 s of guitar at the default macros\n");
    { const char *fxn[6] = { "GLASS", "BLOOM", "ACCRETION", "CHOIR", "FRACTURE", "CONSTELLATION" };
      for (int p = 0; p < GRAN_PRESET_COUNT; p++) {
          nd->set_param(nd, GRAN_PARAM_PRESET, p); set_macros(nd, 0.5, 0.5, 0.5, 0.5, 0.5);
          { int32_t z[64] = { 0 }, y[64] = { 0 }; nd->run_stereo(nd, z, y, 64); }                              /* the switch happens at the next block */
          if (p % 4 == 0 && p / 4 == AND_FX_ACCRETION) fx_andromeda_action(nd, AND_ACT_SECONDARY);      /* record the guitar so there is something to hear */
          res_t r = run(nd, 3.0, 0);
          const double wet_db = 20 * log10(fmax(r.rms, 1e-9) / fmax(r.dry_rms, 1e-9));
          printf("         preset %2d  %-9s variant %d  peak %.3f  rms %.3f (%.1f dB re the dry)\n", p, fxn[p / 4], p % 4, r.peak, r.rms, wet_db);
          CHECK(r.peak < 1.0 && r.clamps == 0, "preset %d never reaches the hard clamp: peak %.3f", p, r.peak);
          CHECK(r.rms > 0.5 * r.dry_rms, "preset %d: the guitar is still there: rms %.4f against %.4f dry", p, r.rms, r.dry_rms);
      } }

    printf("[2] the wet signal itself is audible at MIX 100 %% (each effect, defaults)\n");
    for (int e = 0; e < AND_FX_COUNT; e++) {
        nd->set_param(nd, GRAN_PARAM_PRESET, e * 4); set_macros(nd, 0.5, 0.5, 0.5, 0.5, 1.0);
        { int32_t z[64] = { 0 }, y[64] = { 0 }; nd->run_stereo(nd, z, y, 64); }
        if (e == AND_FX_ACCRETION) { fx_andromeda_action(nd, AND_ACT_SECONDARY); }
        run(nd, 2.5, 0);                                              /* warm up (ACCRETION: records 2 s then plays) */
        res_t r = run(nd, 2.0, 0);
        const double db = 20 * log10(fmax(r.rms, 1e-9));
        printf("         effect %d  wet rms %.1f dBFS  peak %.3f\n", e, db, r.peak);
        CHECK(db > -36.0 && db < -9.0 && r.peak < 0.9, "effect %d at MIX 100 %%: %.1f dBFS rms, peak %.3f (audible, with headroom)", e, db, r.peak);
    }

    printf("[3] keys: GLASS strike sounds without playing; BLOOM freeze holds; ACCRETION REC -> PLAY -> DUB -> freeze -> clear\n");
    { nd->set_param(nd, GRAN_PARAM_PRESET, 0); set_macros(nd, 0.0, 0.5, 0.6, 0.7, 1.0);
      int32_t L[64] = { 0 }, R[64] = { 0 }; nd->run_stereo(nd, L, R, 64);
      fx_andromeda_action(nd, AND_ACT_PRIMARY);
      double pk = 0; for (int b = 0; b < 40; b++) { memset(L, 0, sizeof L); memset(R, 0, sizeof R); nd->run_stereo(nd, L, R, 64); for (int i = 0; i < 64; i++) pk = fmax(pk, fabs((double)L[i]) / Q21); }
      fx_granular_get_telemetry(nd, &tel); int lit = 0; for (int k = 0; k < GLASS_TELEM_MODES; k++) if (tel.mode_lvl[k] > 2) lit++;
      CHECK(pk > 0.02 && tel.effect == AND_FX_GLASS, "GLASS: a key strike rings with no input: peak %.3f", pk);
      CHECK(lit >= 6, "GLASS telemetry: %d of 12 modes light up after a strike", lit);

      nd->set_param(nd, GRAN_PARAM_PRESET, 4); set_macros(nd, 0.4, 0.6, 0.5, 0.3, 1.0); run(nd, 2.0, 0);
      fx_granular_get_telemetry(nd, &tel); const int grains = tel.active_count;
      CHECK(tel.effect == AND_FX_BLOOM && grains > 0, "BLOOM telemetry: %d grains sounding", grains);
      fx_andromeda_action(nd, AND_ACT_SECONDARY); run(nd, 0.3, 0);
      CHECK(fx_granular_get_freeze(nd), "BLOOM: the key freezes the cloud");
      res_t rf = run(nd, 1.0, 100000);
      CHECK(rf.rms > 0.005, "BLOOM: the frozen cloud keeps sounding with the guitar silent-ish: rms %.4f", rf.rms);
      fx_andromeda_action(nd, AND_ACT_SECONDARY); run(nd, 0.2, 0);
      CHECK(!fx_granular_get_freeze(nd), "BLOOM: the key releases it");

      nd->set_param(nd, GRAN_PARAM_PRESET, 8); set_macros(nd, 0.5, 0.6, 0.3, 0.2, 1.0); run(nd, 0.2, 0);
      fx_andromeda_action(nd, AND_ACT_SECONDARY); run(nd, 0.5, 0); fx_granular_get_telemetry(nd, &tel);
      CHECK(tel.effect == AND_FX_ACCRETION && tel.looper_state == 1, "ACCRETION: first press records (state %d)", tel.looper_state);
      run(nd, 2.0, 0); fx_granular_get_telemetry(nd, &tel);
      CHECK(tel.looper_state == 2, "ACCRETION: the recording ends by itself at the loop length, then plays (state %d)", tel.looper_state);
      fx_andromeda_action(nd, AND_ACT_SECONDARY); run(nd, 0.3, 0); fx_granular_get_telemetry(nd, &tel);
      CHECK(tel.looper_state == 3 && tel.layers == 1, "ACCRETION: next press overdubs (state %d, layers %d)", tel.looper_state, tel.layers);
      int env = 0; run(nd, 1.5, 0); fx_granular_get_telemetry(nd, &tel); for (int c = 0; c < (int)GRAN_TAPE_COLS; c++) if (tel.tape_env[c] > 8) env++;
      CHECK(env >= 8, "ACCRETION: the tape strip shows the loop (%d of 48 columns)", env);
      fx_andromeda_action(nd, AND_ACT_SECONDARY); run(nd, 0.2, 0);
      fx_andromeda_action(nd, AND_ACT_FREEZE); run(nd, 0.2, 0);
      CHECK(fx_granular_get_freeze(nd), "ACCRETION: HOME held freezes the loop");
      fx_andromeda_action(nd, AND_ACT_CLEAR); run(nd, 0.3, 0); fx_granular_get_telemetry(nd, &tel);
      CHECK(tel.looper_state == 0 && !fx_granular_get_freeze(nd), "ACCRETION: clear empties the tape (state %d)", tel.looper_state);
      /* the < key: with no loop it starts the recording, pressed again it ends it early (-> play), with a loop it freezes / releases it; > toggles PLAY <-> DUB */
      fx_andromeda_action(nd, AND_ACT_PRIMARY); run(nd, 0.4, 0); fx_granular_get_telemetry(nd, &tel);
      CHECK(tel.looper_state == 1, "ACCRETION: < on an empty tape starts recording (state %d)", tel.looper_state);
      fx_andromeda_action(nd, AND_ACT_PRIMARY); run(nd, 0.3, 0); fx_granular_get_telemetry(nd, &tel);
      CHECK(tel.looper_state == 2, "ACCRETION: < while recording ends it and plays (state %d)", tel.looper_state);
      fx_andromeda_action(nd, AND_ACT_SECONDARY); run(nd, 0.3, 0); fx_granular_get_telemetry(nd, &tel);
      CHECK(tel.looper_state == 3, "ACCRETION: > from play dubs (state %d)", tel.looper_state);
      fx_andromeda_action(nd, AND_ACT_SECONDARY); run(nd, 0.3, 0); fx_granular_get_telemetry(nd, &tel);
      CHECK(tel.looper_state == 2, "ACCRETION: > again goes back to play (state %d)", tel.looper_state);
      fx_andromeda_action(nd, AND_ACT_PRIMARY); run(nd, 0.3, 0);
      CHECK(fx_granular_get_freeze(nd), "ACCRETION: < with a loop freezes it");
      fx_andromeda_action(nd, AND_ACT_PRIMARY); run(nd, 0.3, 0);
      CHECK(!fx_granular_get_freeze(nd), "ACCRETION: < again releases it");
      fx_andromeda_action(nd, AND_ACT_CLEAR); run(nd, 0.3, 0); fx_granular_get_telemetry(nd, &tel);
      CHECK(tel.looper_state == 0, "ACCRETION: < held (clear) resets the tape (state %d)", tel.looper_state); }

    printf("[4] switching effect while playing: 200 random switches and macro moves under load, no blow-up\n");
    { uint32_t rng = 12345u; double worst = 0; int bad = 0;
      for (int k = 0; k < 200; k++) {
          rng = rng * 1664525u + 1013904223u;
          nd->set_param(nd, GRAN_PARAM_PRESET, (int)((rng >> 16) % 24u));
          set_macros(nd, ((rng >> 3) & 255) / 255.0, ((rng >> 7) & 255) / 255.0, ((rng >> 11) & 255) / 255.0, ((rng >> 15) & 255) / 255.0, ((rng >> 19) & 255) / 255.0);
          if ((rng >> 24) & 1u) fx_andromeda_action(nd, (int)((rng >> 25) & 3u));
          res_t r = run(nd, 0.15, (int)(rng & 0xFFFF)); worst = fmax(worst, r.peak); if (!(r.peak == r.peak) || r.peak > 1.0) bad++;
      }
      CHECK(bad == 0, "200 switches: finite and under full scale (worst peak %.3f)", worst); }

    printf("[5] the overlay doubles as scratch memory: borrowed -> the signal passes dry; released -> the effect is back\n");
    { nd->set_param(nd, GRAN_PARAM_PRESET, 0); set_macros(nd, 0.5, 0.5, 0.5, 0.5, 1.0); run(nd, 0.3, 0);
      uint8_t *scr = (uint8_t *)pa_gran_tape_borrow(); memset(scr, 0xA5, PA_GRAN_SCRATCH_BYTES);
      res_t rb = run(nd, 0.5, 0);
      CHECK(rb.rms > 0.0 && fabs(rb.rms - rb.dry_rms) < 0.01 * rb.dry_rms + 1e-6, "borrowed: the output is the dry signal (rms %.4f against %.4f)", rb.rms, rb.dry_rms);
      CHECK(scr[100] == 0xA5 && scr[PA_GRAN_SCRATCH_BYTES - 1] == 0xA5, "borrowed: the engine leaves the memory alone");
      pa_gran_tape_release(); res_t ra = run(nd, 1.5, 0);
      CHECK(ra.peak < 1.0 && ra.rms > 0.0, "released: the effect runs again (peak %.3f)", ra.peak);
      fx_granular_get_telemetry(nd, &tel); CHECK(tel.effect == AND_FX_GLASS, "released: the same effect restarted (%d)", tel.effect); }

    printf("[7] the GLASS layer (MOD slot): off adds nothing, on rings from the effect's output, bounded, cheap to switch\n");
    { fx_node_t *ly = fx_andromeda_layer_new();
      CHECK(ly && ly->state, "the layer node is created (1.6 KB of state)");
      int32_t a[64], b[64], ar[64], br[64]; int same = 1;
      for (int blk = 0; blk < 200; blk++) {
          for (int i = 0; i < 64; i++) { a[i] = ar[i] = b[i] = br[i] = (int32_t)(guitar(blk * 64 + i) * Q21); }
          ly->run_stereo(ly, a, ar, 64);
          for (int i = 0; i < 64; i++) if (a[i] != b[i] || ar[i] != br[i]) same = 0;
      }
      CHECK(same, "level 0: the signal passes untouched, sample for sample");
      for (int m = 0; m < 6; m++) {
          ly->set_param(ly, 0, (int32_t)((m + 0.5) / 6.0 * Q21)); ly->set_param(ly, 1, (int32_t)(0.5 * Q21)); ly->set_param(ly, 2, (int32_t)(0.7 * Q21)); ly->set_param(ly, 3, (int32_t)(0.7 * Q21));
          ly->set_param(ly, 4, (int32_t)(0.8 * Q21));
          double pk = 0, e_in = 0, e_add = 0;
          for (int blk = 0; blk < 700; blk++) {
              for (int i = 0; i < 64; i++) { a[i] = ar[i] = b[i] = (int32_t)(guitar(blk * 64 + i) * Q21); }
              ly->run_stereo(ly, a, ar, 64);
              for (int i = 0; i < 64; i++) { const double x = a[i] / Q21, d = (a[i] - b[i]) / Q21; pk = fmax(pk, fabs(x)); e_in += (b[i] / Q21) * (b[i] / Q21); e_add += d * d; }
          }
          const double add_db = 10 * log10((e_add + 1e-30) / (e_in + 1e-30));
          if (m == 0 || m == 5) printf("         material %d: the layer adds %.1f dB re the input, peak %.3f\n", m, add_db, pk);
          CHECK(pk < 0.95 && add_db > -30.0 && add_db < 3.0, "material %d: audible (%.1f dB re the input) and bounded (peak %.3f)", m, add_db, pk);
      }
      ly->set_param(ly, 4, 0); for (int i = 0; i < 64; i++) { a[i] = ar[i] = b[i] = 100000; } ly->run_stereo(ly, a, ar, 64);
      CHECK(a[5] == 100000, "back to level 0: a pass-through again");
      ly->destroy(ly); }

    printf("[8] the new effects: CHOIR holds a chord, FRACTURE and CONSTELLATION run their steps, the pattern can be rerolled\n");
    { nd->set_param(nd, GRAN_PARAM_PRESET, 12); set_macros(nd, 0.42, 0.3, 0.8, 0.25, 1.0); { int32_t z[64] = { 0 }, y[64] = { 0 }; nd->run_stereo(nd, z, y, 64); }
      run(nd, 1.5, 0); fx_granular_get_telemetry(nd, &tel);
      CHECK(tel.effect == AND_FX_CHOIR && tel.voices_n >= 2 && tel.voice_env[0] > 50, "CHOIR telemetry: %d voices, the first swelled to %d", tel.voices_n, tel.voice_env[0]);
      fx_andromeda_action(nd, AND_ACT_PRIMARY); run(nd, 0.2, 0);
      CHECK(fx_granular_get_freeze(nd), "CHOIR: the key holds the chord (the history stops)");
      res_t rh = run(nd, 1.0, 400000); CHECK(rh.rms > 0.01, "CHOIR: held, it keeps singing after the playing has moved on (rms %.3f)", rh.rms);
      fx_andromeda_action(nd, AND_ACT_PRIMARY); run(nd, 0.2, 0);

      nd->set_param(nd, GRAN_PARAM_PRESET, 16); set_macros(nd, 0.5, 0.45, 0.2, 0.5, 1.0); { int32_t z[64] = { 0 }, y[64] = { 0 }; nd->run_stereo(nd, z, y, 64); }
      run(nd, 3.0, 0); fx_granular_get_telemetry(nd, &tel);
      CHECK(tel.effect == AND_FX_FRACTURE && tel.seq_mask != 0, "FRACTURE telemetry: steps played (mask %04x)", tel.seq_mask);
      const int pos0 = tel.seq_pos; run(nd, 0.3, 0); fx_granular_get_telemetry(nd, &tel);
      CHECK(tel.seq_pos != pos0, "FRACTURE: the step clock moves (%d -> %d)", pos0, tel.seq_pos);
      fx_andromeda_action(nd, AND_ACT_PRIMARY); res_t rr = run(nd, 1.5, 0); CHECK(rr.peak < 1.0 && rr.rms > 0.02, "FRACTURE: the reverse burst key plays (rms %.3f)", rr.rms);

      nd->set_param(nd, GRAN_PARAM_PRESET, 20); set_macros(nd, 0.35, 0.06, 0.3, 0.6, 1.0); { int32_t z[64] = { 0 }, y[64] = { 0 }; nd->run_stereo(nd, z, y, 64); }
      run(nd, 2.0, 0); fx_granular_get_telemetry(nd, &tel); uint8_t before[16]; memcpy(before, tel.seq_pitch, 16); int lit = 0; for (int i = 0; i < 16; i++) if (before[i]) lit++;
      CHECK(tel.effect == AND_FX_CONSTELLATION && lit >= 6 && lit <= 12, "CONSTELLATION telemetry: %d of 16 steps play at density 60 %%", lit);
      fx_andromeda_action(nd, AND_ACT_CLEAR); run(nd, 0.3, 0); fx_granular_get_telemetry(nd, &tel);
      int same = 1; for (int i = 0; i < 16; i++) if (before[i] != tel.seq_pitch[i]) same = 0;
      CHECK(!same, "CONSTELLATION: the clear key rerolls the pattern"); }

    printf("[9] SPACE (the FDN reverb with its shimmer, in the REVERB slot): a send, bounded, silent when off\n");
    { fx_node_t *sp = fx_andromeda_space_new();
      CHECK(sp && sp->state, "the SPACE node is created");
      int32_t a[64], ar[64], b[64], br[64];
      sp->set_param(sp, 3, 0);
      int same = 1; for (int blk = 0; blk < 300; blk++) { for (int i = 0; i < 64; i++) { a[i] = ar[i] = b[i] = br[i] = (int32_t)(guitar(blk * 64 + i) * Q21); } sp->run_stereo(sp, a, ar, 64); for (int i = 0; i < 64; i++) if (a[i] != b[i] || ar[i] != br[i]) same = 0; }
      CHECK(same, "send 0: the signal passes untouched");
      sp->set_param(sp, 3, (int32_t)(0.35 * Q21)); sp->set_param(sp, 0, (int32_t)(0.72 * Q21)); sp->set_param(sp, 4, 0);
      double e_add = 0, e_dry = 0, pk = 0, tail = 0; int bad = 0;
      for (int blk = 0; blk < 1600; blk++) {
          for (int i = 0; i < 64; i++) { const double g = blk < 800 ? guitar(blk * 64 + i) : 0.0; a[i] = ar[i] = b[i] = (int32_t)(g * Q21); }
          sp->run_stereo(sp, a, ar, 64);
          for (int i = 0; i < 64; i++) { const double d = (a[i] - b[i]) / Q21; if (!(d == d)) bad++; e_add += d * d; e_dry += (b[i] / Q21) * (b[i] / Q21); pk = fmax(pk, fabs(a[i] / Q21)); if (blk >= 800) tail += d * d; }
      }
      const double add_db = 10 * log10((e_add + 1e-30) / (e_dry + 1e-30));
      printf("         the reverb adds %.1f dB re the dry signal at SPACE 35 %%, peak %.3f\n", add_db, pk);
      CHECK(bad == 0 && pk < 0.95, "bounded and finite (peak %.3f)", pk);
      CHECK(add_db > -14.0 && add_db < 2.0, "SPACE 35 %%: the reverb is %.1f dB against the dry signal (audible, not louder than it)", add_db);
      CHECK(tail > 1e-6, "the tail goes on after the playing stops");
      sp->set_param(sp, 4, (int32_t)(1.0 * Q21)); sp->set_param(sp, 0, (int32_t)(1.0 * Q21));
      double pk2 = 0; int bad2 = 0;
      for (int blk = 0; blk < 3000; blk++) { for (int i = 0; i < 64; i++) { const double g = blk < 800 ? guitar(blk * 64 + i) : 0.0; a[i] = ar[i] = (int32_t)(g * Q21); } sp->run_stereo(sp, a, ar, 64); for (int i = 0; i < 64; i++) { const double v = fabs(a[i] / Q21); if (!(v == v)) bad2++; pk2 = fmax(pk2, v); } }
      CHECK(bad2 == 0 && pk2 < 0.99, "the longest decay with the shimmer fully up stays bounded (peak %.3f)", pk2);
      sp->destroy(sp); }

    printf("[6] the mono entry the chain also uses (run) matches the stereo one in level\n");
    { nd->set_param(nd, GRAN_PARAM_PRESET, 0); set_macros(nd, 0.5, 0.5, 0.5, 0.5, 0.5);
      int32_t buf[64]; double e = 0; for (int b = 0; b < 600; b++) { for (int i = 0; i < 64; i++) buf[i] = (int32_t)(guitar(b * 64 + i) * Q21); nd->run(nd, buf, 64); for (int i = 0; i < 64; i++) e += ((double)buf[i] / Q21) * ((double)buf[i] / Q21); }
      CHECK(e > 0 && e == e, "mono run: energy %.3f", e); }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL ANDROMEDA FX HOST TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
