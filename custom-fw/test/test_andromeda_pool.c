/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_andromeda_pool.c - Andromeda's DSP pool high-water mark, measured by the DEVICE allocator.
 *
 * Andromeda shrinks the effect pool (PA_DSP_POOL_BYTES, pa_config.h) to make room for the granular
 * capture tape in the .pool region. This program creates every block an Andromeda chain can switch
 * on - dynamics, drive, eq, mod, GRANULAR, reverb (the cabinet is a stateless stub, locked off) - and
 * fails if the pool overflows,
 * runs above 95 %, leaks, or if the granular engine ends up without its state or tape (which on the
 * device means the effect silently passes audio through).
 *
 *   clang -O2 -Icustom-fw/include -DPA_HOST_SIM=1 -DPA_POOL_TEST=1 -DPA_PRODUCT_ANDROMEDA=1 \
 *         custom-fw/test/test_andromeda_pool.c <host_dsp sources> -lm -o test_andromeda_pool.exe
 */
#include <stdio.h>
#include <string.h>
#include "pa_config.h"
#include "pa_fx.h"
#include "pa_granular.h"

#if !PA_PRODUCT_ANDROMEDA
#error "build with -DPA_PRODUCT_ANDROMEDA=1"
#endif

extern uint32_t pa_pool_used_bytes(void);

static int fails = 0;
#define CHECK(cond, ...) do {                                    \
        if (!(cond)) {                                           \
            printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); \
            fails++;                                             \
        }                                                        \
    } while (0)

#define N_NODES 7
#define I_GRAN  4
#define I_CAB   6

static fx_node_t *make(int i) {
    switch (i) {
        case 0: return fx_dynamics_new();
        case 1: return fx_drive_new();
        case 2: return fx_eq_new();
        case 3: return fx_andromeda_layer_new();
        case 4: return fx_granular_new();
        case 5: return fx_andromeda_space_new();
        default: return fx_cab_ir_new();
    }
}

static const char *k_name[N_NODES] = {
    "dynamics", "drive", "eq", "mod", "granular", "reverb", "cab"
};

int main(void) {
    fx_node_t *nd[N_NODES];
    uint32_t prev = 0, peak = 0;

    printf("=== Andromeda DSP pool: every block on ===\n");
    printf("   pool budget %u B, effect overlay >= %u B (static, outside the pool)\n",
           (unsigned)PA_DSP_POOL_BYTES, (unsigned)PA_GRAN_SCRATCH_BYTES);

    printf("[1] create every block\n");
    for (int i = 0; i < N_NODES; i++) {
        nd[i] = make(i);
        CHECK(nd[i] != 0, "slot %d (%s) has no node", i, k_name[i]);
        if (!nd[i]) continue;
        if (!nd[i]->state && nd[i]->create) nd[i]->create(nd[i]);
        uint32_t used = pa_pool_used_bytes();
        printf("   + %-9s state %-8s %6u B  (total %6u B)\n", k_name[i],
               nd[i]->state ? "created" : (i == I_CAB ? "stub" : "MISSING"), (unsigned)(used - prev), (unsigned)used);
        if (i == 1 || i == I_CAB) { CHECK(nd[i]->state == 0, "the Andromeda %s must be a stateless stub", k_name[i]); continue; }
        CHECK(nd[i]->state != 0, "%s ran out of pool", k_name[i]);
        if (used > peak) peak = used;
        prev = used;
    }

    if (nd[I_CAB] && nd[I_CAB]->state) {
        fx_cab_ir_set_stereo_width(nd[I_CAB], 100);
        uint32_t used = pa_pool_used_bytes();
        printf("   + stereo    right-IR spectra %6u B  (total %6u B)\n", (unsigned)(used - prev), (unsigned)used);
        if (used > peak) peak = used;
        fx_cab_ir_set_stereo_width(nd[I_CAB], 0);
    }

    printf("   high-water %u B of %u B = %.1f %%\n", (unsigned)peak, (unsigned)PA_DSP_POOL_BYTES,
           100.0 * (double)peak / (double)PA_DSP_POOL_BYTES);
    CHECK(peak <= PA_DSP_POOL_BYTES, "the pool overflowed by %u B", (unsigned)(peak - PA_DSP_POOL_BYTES));
    CHECK(peak <= (uint32_t)((uint64_t)PA_DSP_POOL_BYTES * 95u / 100u),
          "the pool is above 95 %% (%.1f %%): no margin left", 100.0 * (double)peak / (double)PA_DSP_POOL_BYTES);

    printf("[2] the effect host produces sound (GLASS at its defaults, struck by the input)\n");
    if (nd[I_GRAN] && nd[I_GRAN]->state) {
        int32_t L[64], R[64];
        double energy = 0.0;
        nd[I_GRAN]->set_param(nd[I_GRAN], GRAN_PARAM_MIX, 2097152);        /* fully wet */
        for (int b = 0; b < 2000; b++) {
            for (int k = 0; k < 64; k++) { L[k] = R[k] = ((b * 64 + k) % 100 < 50) ? 400000 : -400000; }
            nd[I_GRAN]->run_stereo(nd[I_GRAN], L, R, 64);
            if (b > 1000) for (int k = 0; k < 64; k++) energy += (double)L[k] * L[k];
        }
        CHECK(energy > 0.0, "the granular engine is silent (no tape?)");
        printf("   wet energy %.3g\n", energy);
    }

    printf("[3] destroy: every byte comes back except the granular node descriptor\n");
    uint32_t gran_node_bytes = 0;
    for (int i = 0; i < N_NODES; i++) {
        if (nd[i] && nd[i]->destroy) nd[i]->destroy(nd[i]);
    }
    gran_node_bytes = pa_pool_used_bytes();
    printf("   left allocated: %u B\n", (unsigned)gran_node_bytes);
    CHECK(gran_node_bytes <= 384u, "%u B still allocated after destroying every block", (unsigned)gran_node_bytes);

    printf("[4] 200 create/destroy cycles on the same nodes: no leak, no failures\n");
    int failures = 0;
    for (int k = 0; k < 200; k++) {
        for (int i = 0; i < N_NODES; i++) {
            if (nd[i] && nd[i]->create) nd[i]->create(nd[i]);
            if (!nd[i] || (!nd[i]->state && i != 1 && i != I_CAB)) failures++;
        }
        for (int i = 0; i < N_NODES; i++) {
            if (nd[i] && nd[i]->destroy) nd[i]->destroy(nd[i]);
        }
    }
    CHECK(failures == 0, "%d creations failed", failures);
    CHECK(pa_pool_used_bytes() == gran_node_bytes, "leak: %u B after the cycles (was %u B)",
          (unsigned)pa_pool_used_bytes(), (unsigned)gran_node_bytes);

    printf(fails ? "\n%d FAILURE(S)\n" : "\nAndromeda pool: all checks passed\n", fails);
    return fails ? 1 : 0;
}
