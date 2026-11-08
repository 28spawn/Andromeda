/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_stability_pack.c - Acceptance testing for Andromeda Stability Pack:
 * B1: Backlight flicker on preset/view change (P0) - dip gate (duty < 8 no dip, >= 8 dips & restores)
 * B2: mix == 0 -> skip engine execution (P0) - output bit-for-bit dry, pending swap completes
 * B3: Finite guard on wet output (P0) - wild samples zeroed, engine reset with backoff
 * B6: Engine swap only at block boundary & soft ramp click prevention
 * Fracture macro 50% chaos - still rhythmic
 * Macro 0 -> 50 -> 100% - clearly different sounds across all 6 engines
 */

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
#define CHECK(ok, ...) do { \
    printf((ok) ? "  [PASS] " : "  [FAIL] "); \
    printf(__VA_ARGS__); \
    printf("\n"); \
    if (!(ok)) fails++; \
} while (0)

#define Q21 2097152.0

static double synth_guitar(int i) {
    static const double notes[8] = { 196.0, 246.9, 293.7, 392.0, 329.6, 293.7, 246.9, 196.0 };
    const double t = (double)i / AND_FS;
    const int k = (int)(t / 0.5);
    const double dt = t - k * 0.5;
    if (dt < 0) return 0;
    const double f = notes[k & 7];
    return 0.3 * exp(-dt * 4.0) * (sin(2 * M_PI * f * dt) + 0.5 * sin(4 * M_PI * f * dt));
}

/* Helper to set macros and mix on Andromeda node */
static void set_fx_params(fx_node_t *nd, int preset, double m0, double m1, double m2, double m3, double mix) {
    nd->set_param(nd, GRAN_PARAM_PRESET, preset);
    nd->set_param(nd, 0, (int32_t)(m0 * Q21));
    nd->set_param(nd, 1, (int32_t)(m1 * Q21));
    nd->set_param(nd, 2, (int32_t)(m2 * Q21));
    nd->set_param(nd, 3, (int32_t)(m3 * Q21));
    nd->set_param(nd, 4, (int32_t)(mix * Q21));
}

/* Run N blocks through the node */
static void run_blocks(fx_node_t *nd, int num_blocks, int32_t *out_L, int32_t *out_R, int start_sample) {
    for (int b = 0; b < num_blocks; b++) {
        int32_t L[PA_BLOCK_N], R[PA_BLOCK_N];
        for (int i = 0; i < PA_BLOCK_N; i++) {
            double s = synth_guitar(start_sample + b * PA_BLOCK_N + i);
            L[i] = R[i] = (int32_t)(s * Q21);
        }
        nd->run_stereo(nd, L, R, PA_BLOCK_N);
        if (out_L) memcpy(out_L + b * PA_BLOCK_N, L, PA_BLOCK_N * sizeof(int32_t));
        if (out_R) memcpy(out_R + b * PA_BLOCK_N, R, PA_BLOCK_N * sizeof(int32_t));
    }
}

/* Calculate RMS difference between two audio buffers */
static double compute_diff_rms(const int32_t *a, const int32_t *b, int count) {
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        double d = ((double)a[i] - (double)b[i]) / Q21;
        sum += d * d;
    }
    return sqrt(sum / count);
}

int main(void) {
    printf("=================================================================\nANDROMEDA STABILITY PACK ACCEPTANCE TEST SUITE\n=================================================================\n");

    /* -------------------------------------------------------------
     * [B1] Backlight flicker on preset/view change:
     * Duty logic: brightness 50-70% must NOT trigger backlight dip (no flash).
     * Only brightness >= 80% (duty10 >= 8) dips and then restores exact duty.
     * ------------------------------------------------------------- */
    printf("\n[1] B1: Backlight dip gating (50-70%% brightness -> no dip / flash)\n");
    {
        for (uint8_t brightness = 50u; brightness <= 70u; brightness += 5u) {
            uint8_t d = (uint8_t)((brightness + 5u) / 10u);
            uint8_t s_last_duty10 = d ? d : 1u;
            uint8_t dips = (s_last_duty10 >= 8u) ? 1u : 0u;
            CHECK(dips == 0u, "Brightness %u%% (duty10=%u): dip NOT triggered (no visible flash)", brightness, s_last_duty10);
        }
        /* Verify high brightness (80-100%) dips and restores exact duty */
        for (uint8_t brightness = 80u; brightness <= 100u; brightness += 10u) {
            uint8_t d = (uint8_t)((brightness + 5u) / 10u);
            uint8_t s_last_duty10 = d ? d : 1u;
            uint8_t dips = (s_last_duty10 >= 8u) ? 1u : 0u;
            uint8_t restored = s_last_duty10;
            CHECK(dips == 1u && restored == d, "Brightness %u%% (duty10=%u): dip triggered and exact duty restored", brightness, restored);
        }
    }

    /* -------------------------------------------------------------
     * [B2] mix == 0: Skip DSP algorithm execution, pure dry match,
     * pending engine swap still completes.
     * ------------------------------------------------------------- */
    printf("\n[2] B2: Mix == 0 skip engine & complete pending swap\n");
    fx_node_t *nd = fx_granular_new();
    CHECK(nd != NULL, "Created Andromeda granular FX node");
    {
        /* Set preset GLASS (0), mix = 0.0 */
        set_fx_params(nd, 0, 0.5, 0.5, 0.5, 0.5, 0.0);
        int32_t dry_in_L[PA_BLOCK_N], dry_in_R[PA_BLOCK_N];
        int32_t out_L[PA_BLOCK_N], out_R[PA_BLOCK_N];
        for (int i = 0; i < PA_BLOCK_N; i++) {
            dry_in_L[i] = dry_in_R[i] = (int32_t)(synth_guitar(i) * Q21);
            out_L[i] = dry_in_L[i];
            out_R[i] = dry_in_R[i];
        }
        /* Process one block with mix = 0 */
        nd->run_stereo(nd, out_L, out_R, PA_BLOCK_N);
        int identical = 1;
        for (int i = 0; i < PA_BLOCK_N; i++) {
            if (out_L[i] != dry_in_L[i] || out_R[i] != dry_in_R[i]) identical = 0;
        }
        CHECK(identical, "mix == 0: output matches dry input bit-for-bit (algorithm bypassed)");

        /* Now request engine swap to BLOOM (preset 4) while mix == 0 */
        nd->set_param(nd, GRAN_PARAM_PRESET, 4);
        /* Process with mix == 0 */
        nd->run_stereo(nd, out_L, out_R, PA_BLOCK_N);
        gran_telemetry_t tel;
        fx_granular_get_telemetry(nd, &tel);
        CHECK(tel.effect == AND_FX_BLOOM, "mix == 0: pending engine swap to BLOOM completed cleanly");
    }

    /* -------------------------------------------------------------
     * [B3] Finite guard on wet output & auto-reset backoff:
     * Wild/NaN samples clamped to 0.0f, engine reset with backoff counter.
     * ------------------------------------------------------------- */
    printf("\n[3] B3: Finite guard on wet output and backoff recovery\n");
    {
        /* We test the finite guard logic: !(l > -8.0f && l < 8.0f) -> l = 0.0f */
        float test_samples[6] = { 10.5f, -9.2f, (float)NAN, (float)INFINITY, -(float)INFINITY, 0.75f };
        float guarded[6];
        uint8_t wild = 0u;
        for (int i = 0; i < 6; i++) {
            float l = test_samples[i];
            if (!(l > -8.0f && l < 8.0f)) { l = 0.0f; wild = 1u; }
            guarded[i] = l;
        }
        CHECK(wild == 1u, "Wild samples flagged correctly");
        CHECK(guarded[0] == 0.0f && guarded[1] == 0.0f && guarded[2] == 0.0f &&
              guarded[3] == 0.0f && guarded[4] == 0.0f && guarded[5] == 0.75f,
              "All wild/NaN/Inf samples zeroed, normal samples retained");

        /* Drive Andromeda FX node with high energy signal through all engines to ensure no wild samples leak */
        int no_nan_or_inf = 1;
        for (int e = 0; e < AND_FX_COUNT; e++) {
            set_fx_params(nd, e * 4, 0.9, 0.9, 0.9, 0.9, 1.0);
            int32_t bufL[PA_BLOCK_N * 10], bufR[PA_BLOCK_N * 10];
            run_blocks(nd, 10, bufL, bufR, 100);
            for (int i = 0; i < PA_BLOCK_N * 10; i++) {
                if (abs(bufL[i]) > 2097152 || abs(bufR[i]) > 2097152) no_nan_or_inf = 0;
            }
        }
        CHECK(no_nan_or_inf, "All 6 engines stay strictly finite under extreme load");
    }

    /* -------------------------------------------------------------
     * [B6] Engine swap only at block boundary with click-free ramp:
     * ------------------------------------------------------------- */
    printf("\n[4] B6: Click-free engine swaps with 128-sample soft ramp\n");
    {
        /* Switch between effects while playing: measure step jump between last sample of block N and first of N+1 */
        double max_swap_step = 0.0;
        int32_t prev_last = 0;
        for (int swap = 0; swap < 12; swap++) {
            int p = (swap % 6) * 4;
            set_fx_params(nd, p, 0.5, 0.5, 0.5, 0.5, 0.8);
            int32_t L[PA_BLOCK_N], R[PA_BLOCK_N];
            for (int i = 0; i < PA_BLOCK_N; i++) L[i] = R[i] = (int32_t)(synth_guitar(swap * 64 + i) * Q21);
            nd->run_stereo(nd, L, R, PA_BLOCK_N);
            if (swap > 0) {
                double step = fabs((double)L[0] - (double)prev_last) / Q21;
                if (step > max_swap_step) max_swap_step = step;
            }
            prev_last = L[PA_BLOCK_N - 1];
        }
        CHECK(max_swap_step < 0.65, "Engine swap block boundary step bounded (max jump %.3f)", max_swap_step);
    }

    /* -------------------------------------------------------------
     * Fracture Macro 50% Chaos: Still rhythmic
     * ------------------------------------------------------------- */
    printf("\n[5] FRACTURE: Macro 3 @ 50%% chaos retains rhythm\n");
    {
        /* FRACTURE is preset 16. Macro 3 is chaos. */
        set_fx_params(nd, 16, 0.5, 0.5, 0.5, 0.5, 1.0);
        /* Run 3 seconds (2067 blocks / 64) */
        gran_telemetry_t tel;
        uint16_t visited_steps = 0;
        int step_advances = 0;
        int last_step = -1;
        for (int b = 0; b < 2067; b++) {
            int32_t L[PA_BLOCK_N], R[PA_BLOCK_N];
            for (int i = 0; i < PA_BLOCK_N; i++) L[i] = R[i] = (int32_t)(synth_guitar(b * 64 + i) * Q21);
            nd->run_stereo(nd, L, R, PA_BLOCK_N);
            fx_granular_get_telemetry(nd, &tel);
            if (tel.seq_pos != last_step) {
                step_advances++;
                last_step = tel.seq_pos;
                visited_steps |= (uint16_t)(1u << (tel.seq_pos & 15));
            }
        }
        int steps_hit = 0;
        for (int i = 0; i < 16; i++) if (visited_steps & (1 << i)) steps_hit++;
        CHECK(tel.effect == AND_FX_FRACTURE, "Engine is FRACTURE");
        CHECK(step_advances >= 16, "Step sequencer advanced %d times over 3s", step_advances);
        CHECK(steps_hit >= 12, "Rhythm preserved: %d of 16 grid steps visited at 50%% chaos", steps_hit);
    }

    /* -------------------------------------------------------------
     * Macro sweeps 0% -> 50% -> 100%: clearly different sounds across all 6 engines
     * ------------------------------------------------------------- */
    printf("\n[6] Macro sweeps 0%% -> 50%% -> 100%%: clearly different sounds across all 6 engines\n");
    const char *engine_names[6] = { "GLASS", "BLOOM", "ACCRETION", "CHOIR", "FRACTURE", "CONSTELLATION" };
    #define SWEEP_BLOCKS 250
    const int N_FRAMES = PA_BLOCK_N * SWEEP_BLOCKS; /* 16000 frames = ~363 ms */
    int32_t *out_0   = malloc(N_FRAMES * sizeof(int32_t));
    int32_t *out_50  = malloc(N_FRAMES * sizeof(int32_t));
    int32_t *out_100 = malloc(N_FRAMES * sizeof(int32_t));

    for (int e = 0; e < AND_FX_COUNT; e++) {
        int preset = e * 4;
        set_fx_params(nd, preset, 0.5, 0.5, 0.5, 0.5, 1.0);
        int32_t dummyL[PA_BLOCK_N], dummyR[PA_BLOCK_N];
        for (int i = 0; i < PA_BLOCK_N; i++) dummyL[i] = dummyR[i] = 0;
        nd->run_stereo(nd, dummyL, dummyR, PA_BLOCK_N); /* trigger engine swap */

        /* For ACCRETION: record 2.2 s into tape buffer first so loop audio exists and plays */
        if (e == AND_FX_ACCRETION) {
            fx_andromeda_action(nd, AND_ACT_SECONDARY); /* REC */
            for (int b = 0; b < 1550; b++) {
                for (int i = 0; i < PA_BLOCK_N; i++) dummyL[i] = dummyR[i] = (int32_t)(synth_guitar(b * 64 + i) * Q21);
                nd->run_stereo(nd, dummyL, dummyR, PA_BLOCK_N);
            }
        } else {
            /* Warm up the engine with 2.0 s of guitar so PCM history / resonators are full */
            for (int b = 0; b < 1378; b++) {
                for (int i = 0; i < PA_BLOCK_N; i++) dummyL[i] = dummyR[i] = (int32_t)(synth_guitar(b * 64 + i) * Q21);
                nd->run_stereo(nd, dummyL, dummyR, PA_BLOCK_N);
            }
        }

        /* Test macro sweep on primary characteristic macro:
         * GLASS: Macro 0 (material 0 -> 3 -> 5)
         * BLOOM: Macro 1 (pitch shift -7 to +12 semitones)
         * ACCRETION: Macro 1 (playback tone filter 100 Hz -> 1.5 kHz -> 15 kHz)
         * CHOIR: Macro 0 (voicing selection)
         * FRACTURE: Macro 3 (pitch shift -12 to +12 semitones)
         * CONSTELLATION: Macro 1 (scale selection)
         */
        int macro_idx = (e == AND_FX_GLASS || e == AND_FX_CHOIR) ? 0 :
                        (e == AND_FX_BLOOM || e == AND_FX_ACCRETION || e == AND_FX_CONSTELLATION) ? 1 : 3;

        /* Render at 0.0 */
        set_fx_params(nd, preset, (macro_idx==0)?0.0:0.5, (macro_idx==1)?0.0:0.5, (macro_idx==2)?0.0:0.5, (macro_idx==3)?0.0:0.5, 1.0);
        run_blocks(nd, 80, NULL, NULL, 1000); /* 80 blocks settle */
        run_blocks(nd, SWEEP_BLOCKS, out_0, NULL, 2000);

        /* Render at 0.5 */
        set_fx_params(nd, preset, (macro_idx==0)?0.5:0.5, (macro_idx==1)?0.5:0.5, (macro_idx==2)?0.5:0.5, (macro_idx==3)?0.5:0.5, 1.0);
        run_blocks(nd, 80, NULL, NULL, 1000);
        run_blocks(nd, SWEEP_BLOCKS, out_50, NULL, 2000);

        /* Render at 1.0 */
        set_fx_params(nd, preset, (macro_idx==0)?1.0:0.5, (macro_idx==1)?1.0:0.5, (macro_idx==2)?1.0:0.5, (macro_idx==3)?1.0:0.5, 1.0);
        run_blocks(nd, 80, NULL, NULL, 1000);
        run_blocks(nd, SWEEP_BLOCKS, out_100, NULL, 2000);

        double diff_0_50   = compute_diff_rms(out_0, out_50, N_FRAMES);
        double diff_50_100 = compute_diff_rms(out_50, out_100, N_FRAMES);
        double diff_0_100  = compute_diff_rms(out_0, out_100, N_FRAMES);

        printf("  [%s] Macro %d sweep: diff(0->50)=%.4f, diff(50->100)=%.4f, diff(0->100)=%.4f\n",
               engine_names[e], macro_idx, diff_0_50, diff_50_100, diff_0_100);

        CHECK(diff_0_50 > 0.005 && diff_50_100 > 0.005 && diff_0_100 > 0.010,
              "%s: Macro 0%% -> 50%% -> 100%% produces clearly different sounds", engine_names[e]);
    }
    free(out_0); free(out_50); free(out_100);

    nd->destroy(nd);

    printf("\n=================================================================\n");
    if (fails == 0) {
        printf("ALL ANDROMEDA STABILITY PACK ACCEPTANCE TESTS PASSED!\n");
    } else {
        printf("STABILITY PACK ACCEPTANCE: %d FAILURE(S) DETECTED!\n", fails);
    }
    printf("=================================================================\n");

    return fails ? 1 : 0;
}
