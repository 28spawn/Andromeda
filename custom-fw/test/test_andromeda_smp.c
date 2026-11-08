/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_andromeda_smp.c - Andromeda's two-core allocator (src/fx/fx_graph.c): the plan it makes from the slot costs, and that a hand-over at any cut changes only
 * WHEN the audio comes out (one block later), never WHAT it is. A synchronous stand-in plays core 1 (the same trick as test_smp_pipeline.c).
 * Build with -DPA_PRODUCT_ANDROMEDA=1 -DPA_HOST_SIM=1 and the fx / dsp sources (build.ps1, tools/qa_gate.py). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "pa_config.h"
#include "pa_fx.h"
#include "pa_cab.h"
#include "pa_granular.h"

void *pa_alloc(uint32_t n) { return calloc(1, n); }
void  pa_free(void *p)     { free(p); }

static int fails = 0;
#define CHECK(ok, ...) do { printf((ok) ? "  [PASS] " : "  [FAIL] "); printf(__VA_ARGS__); printf("\n"); if (!(ok)) fails++; } while (0)

/* ---- the plan --------------------------------------------------------------------------------------------------------------------------------------------- */
static void plan(void) {
    printf("[1] the plan (slot costs in the chain's order DYN, EFFECT, EQ, LAYER, SPACE, then the three that are off)\n");
    uint32_t m = 0;
    { const uint32_t c[8] = { 45, 250, 100, 0, 200, 0, 0, 0 };
      CHECK(pa_fx_alloc_best_cut(c, 0, &m) == 0, "a light chain (%u us of slots) stays on core 0: no hand-over, no extra latency (cut 0)", 45 + 250 + 100 + 200); }
    { const uint32_t c[8] = { 45, 250, 100, 0, 205, 0, 0, 0 };            /* 830 us with the base: between the figure to leave a split (780) and the one to enter it (900) */
      CHECK(pa_fx_alloc_best_cut(c, 0, &m) == 0, "a chain of %u us on one core stays on one core", 230 + 45 + 250 + 100 + 205);
      CHECK(pa_fx_alloc_best_cut(c, 2, &m) != 0, "...and the same chain, already split, stays split (hysteresis)"); }
    { const uint32_t c[8] = { 45, 150, 60, 0, 195, 0, 0, 0 };             /* 680 us: comfortable */
      CHECK(pa_fx_alloc_best_cut(c, 2, &m) == 0, "a split chain that now fits comfortably (%u us) goes back to one core", 230 + 45 + 150 + 60 + 195); }
    { const uint32_t c[8] = { 45, 1000, 190, 300, 350, 0, 0, 0 };
      const int cut = pa_fx_alloc_best_cut(c, 0, &m);
      CHECK(cut == 2, "a heavy effect (1.0 ms) with layer and space: core 1 takes DYN + EFFECT (cut %d), the slower core is %u us of the 1451", cut, m); }
    { const uint32_t c[8] = { 45, 1600, 190, 300, 350, 0, 0, 0 };
      CHECK(pa_fx_alloc_best_cut(c, 0, &m) == 0, "an effect too big for core 1 on its own (1.6 ms) is not handed over (cut 0)"); }
    { const uint32_t c[8] = { 45, 800, 150, 400, 500, 0, 0, 0 };
      const int cut = pa_fx_alloc_best_cut(c, 0, &m);
      CHECK(cut >= 2 && cut <= 4 && m < 1451u, "a loaded chain (1.9 ms of slots) is split with both cores under the block (cut %d, %u us)", cut, m); }
}

/* ---- the pipeline ----------------------------------------------------------------------------------------------------------------------------------------- */
#define K 96
static int32_t out_single[K][2][PA_BLOCK_N], out_pipe[K][2][PA_BLOCK_N];
static int32_t c1_L[PA_BLOCK_N], c1_R[PA_BLOCK_N], c1_dryL[PA_BLOCK_N], c1_dryR[PA_BLOCK_N];
static void hook(pa_fx_chain_t *chain, int32_t *L, int32_t *R, int n, int to, int lo) {
    for (int i = 0; i < n; i++) {
        int32_t t = c1_L[i]; c1_L[i] = L[i]; L[i] = t;
        if (R) { t = c1_R[i]; c1_R[i] = R[i]; R[i] = t; }
    }
    pa_fx_chain_run_front(chain, c1_L, R ? c1_R : 0, n, lo, to, c1_dryL, c1_dryR);
}
static void feed(int32_t *L, int32_t *R, int k) {
    for (int i = 0; i < PA_BLOCK_N; i++) {
        const double t = (double)(k * PA_BLOCK_N + i) / 44118.0, ph = fmod(t, 0.5);
        const double v = 0.3 * exp(-ph * 4.0) * (sin(2.0 * M_PI * 246.9 * t) + 0.5 * sin(4.0 * M_PI * 246.9 * t));
        L[i] = R[i] = (int32_t)(v * 2097152.0);
    }
}
static void setup(pa_fx_chain_t *chain, int type) {
    pa_fx_chain_init(chain, 0, fx_cab_ir_new());
    pa_fx_chain_set_slot_enabled_now(chain, PA_SLOT_DELAY, 1);
    if (!getenv("NO_MOD")) pa_fx_chain_set_slot_enabled_now(chain, PA_SLOT_MOD, 1);
    if (!getenv("NO_REV")) pa_fx_chain_set_slot_enabled_now(chain, PA_SLOT_REVERB, 1);
    fx_node_t *e = chain->slots[PA_SLOT_DELAY];
    e->set_param(e, GRAN_PARAM_PRESET, type);
    for (int i = 0; i < 4; i++) e->set_param(e, GRAN_PARAM_MACRO0 + i, 1048576);
    e->set_param(e, GRAN_PARAM_MIX, 1572864);
    fx_node_t *l = chain->slots[PA_SLOT_MOD];
    l->set_param(l, 4, 1048576);                         /* the layer's level */
    fx_node_t *r = chain->slots[PA_SLOT_REVERB];
    r->set_param(r, 3, 1048576);                         /* the send */
}

static void pipeline(int type, int cut) {
    pa_fx_chain_t chain;
    setup(&chain, type);
    pa_fx_alloc_force_cut(0);
    for (int k = 0; k < K; k++) { feed(out_single[k][0], out_single[k][1], k); pa_fx_chain_run_stereo(&chain, out_single[k][0], out_single[k][1], PA_BLOCK_N); }
    setup(&chain, type);
    memset(c1_L, 0, sizeof c1_L); memset(c1_R, 0, sizeof c1_R);
    pa_fx_chain_set_post_handoff(hook);
    pa_fx_alloc_force_cut(cut);
    for (int k = 0; k < K; k++) { feed(out_pipe[k][0], out_pipe[k][1], k); pa_fx_chain_run_stereo(&chain, out_pipe[k][0], out_pipe[k][1], PA_BLOCK_N); }
    pa_fx_chain_set_post_handoff(0);
    pa_fx_alloc_force_cut(-1);
    /* The pipeline hands the rear its first block as silence, so a node on the rear sees one extra leading block of nothing: its own clocks (modulation, step counters)
     * start one block later than in the single-core run. So the blocks are not bit-identical when an active node sits behind the cut. What must hold: with nothing active
     * behind the cut they ARE identical (k vs k-1), and with something behind it the level and the shape stay the same (RMS within 2 dB, no clipping, no silence). */
    int mism = 0, nonzero = 0; double ep = 0.0, es = 0.0, pk = 0.0;
    for (int k = 1; k < K; k++) for (int ch = 0; ch < 2; ch++) for (int i = 0; i < PA_BLOCK_N; i++) {
        if (out_pipe[k][ch][i] != out_single[k - 1][ch][i]) mism++;
        if (out_single[k - 1][ch][i]) nonzero++;
        if (k >= 24) { const double a = out_pipe[k][ch][i], b = out_single[k - 1][ch][i]; ep += a * a; es += b * b; if (fabs(a) > pk) pk = fabs(a); }
    }
    const double db = 10.0 * log10((ep + 1.0) / (es + 1.0));
    if (cut >= 5) CHECK(mism == 0 && nonzero > 1000, "type %2d, cut %d: everything in front: the pipelined block k is the single-core block k-1 (%d differing samples, %d non-zero)", type, cut, mism, nonzero);
    else CHECK(fabs(db) < 2.0 && pk < 2097152.0 * 0.99 && nonzero > 1000, "type %2d, cut %d: slots behind the cut: level %+.2f dB against the single core, peak %.2f of full scale", type, cut, db, pk / 2097152.0);
}

int main(void) {
    printf("=================================================================\nANDROMEDA two-core allocator\n=================================================================\n");
    plan();
    printf("[2] a hand-over at any cut changes only when the audio comes out\n");
    static const int types[3] = { 12 /* CHOIR ANGELS */, 0 /* GLASS */, 20 /* CONSTELLATION */ };
    for (int t = 0; t < 3; t++) for (int cut = 1; cut <= 5; cut++) pipeline(types[t], cut);
    printf(fails ? "\nRESULT: %d FAILED\n" : "\nRESULT: ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
