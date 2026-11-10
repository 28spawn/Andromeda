/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/*
 * test/test_smp_pipeline.c - the two-core pipeline's logic, with a synchronous stand-in for core 1.
 *
 * With the hook installed, pa_fx_chain_run_stereo gives each block to the hook, which runs the front of the chain (input
 * conditioning and the slots up to the amp) and returns the PREVIOUS block's result; the caller then runs the slots after the amp
 * and the tail. Run on the same input, the pipelined output for block k must be bit-for-bit the single-core output for block k-1,
 * and block 0 must be silent. That proves the split, the swap and the carried state (stereo, dry scratch) do not change the
 * audio, only delay it by one block. The last two cases put a stereo effect in front of the amp, on core 0: the mono block that
 * follows on core 1 must get the same L+R mix as in the single-core chain, not the left channel alone.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pa_config.h"
#include "pa_cab.h"
#include "pa_fx.h"

void *pa_alloc(uint32_t n) { return calloc(1, n); }
void  pa_free(void *p)     { free(p); }

#define K 48
static int32_t out_single[K][2][PA_BLOCK_N], out_pipe[K][2][PA_BLOCK_N], in_blk[K][PA_BLOCK_N];

/* the stand-in for core 1: its own block buffers and its own dry scratch, run synchronously */
static int32_t c1_L[PA_BLOCK_N], c1_R[PA_BLOCK_N], c1_dryL[PA_BLOCK_N], c1_dryR[PA_BLOCK_N];
static int hook_stereo;                                  /* samples handed to "core 1" with L != R: the block arrived in stereo */
static void hook(pa_fx_chain_t *chain, int32_t *L, int32_t *R, int n, int to, int lo) {
    if (R) for (int i = 0; i < n; i++) if (L[i] != R[i]) hook_stereo++;
    for (int i = 0; i < n; i++) {                       /* swap: the caller gets the finished block, "core 1" gets the new one */
        int32_t t = c1_L[i]; c1_L[i] = L[i]; L[i] = t;
        if (R) { t = c1_R[i]; c1_R[i] = R[i]; R[i] = t; }
    }
    /* "core 1" runs the front on its buffer. In the device the swap above happens after core 1 finished the previous block, so the
     * buffer holds the previous block's front output; here the front of the NEW block runs after the swap, as on the device. */
    pa_fx_chain_run_front(chain, c1_L, R ? c1_R : 0, n, lo, to, c1_dryL, c1_dryR);
}

static void feed(int32_t *L, int32_t *R, int k) {
    for (int i = 0; i < PA_BLOCK_N; i++) { L[i] = in_blk[k][i]; R[i] = in_blk[k][i]; }
}

/* A stand-in amp: a mono block with no state, so one node serves both chains. What it is fed shows in what it puts out. */
static void amp_stub_run(fx_node_t *nd, int32_t *buf, int n) {
    (void)nd;
    for (int i = 0; i < n; i++) { int32_t x = buf[i] - (buf[i] >> 2); buf[i] = x > PA_Q21_ONE ? PA_Q21_ONE : x < -PA_Q21_ONE ? -PA_Q21_ONE : x; }
}
static fx_node_t s_amp_stub;

/* front: 0 = the default order. 1 = MOD, DYN, AMP with the stand-in amp: the stereo modulation runs before the hand-over, the dynamics (mono) is the first
 * block after it. 2 = MOD, AMP: the amp itself is the first block after it. In the single-core chain the mono block gets the L+R mix; the split chain must
 * give it the same. 3 = as 2 with the amp and the cabinet bypassed and the delay on: nothing mono runs, so the block must stay in stereo across the hand-over. */
static void setup(pa_fx_chain_t *chain, int drive_on, int front) {
    if (front) { memset(&s_amp_stub, 0, sizeof s_amp_stub); s_amp_stub.run = amp_stub_run; }
    pa_fx_chain_init(chain, front ? &s_amp_stub : 0, fx_cab_ir_new());
    if (drive_on) pa_fx_chain_set_slot_enabled_now(chain, PA_SLOT_DRIVE, 1);
    if (front) {
        pa_fx_chain_swap_order(chain, 0, 5);                              /* MOD DRV AMP CAB EQ DYN DLY REV */
        pa_fx_chain_set_slot_enabled_now(chain, PA_SLOT_MOD, 1);
    }
    if (front == 3) {                                                     /* the amp bypassed, a stereo delay behind it: the stereo must reach the delay */
        pa_fx_chain_set_slot_enabled_now(chain, PA_SLOT_AMP, 0);
        pa_fx_chain_set_slot_enabled_now(chain, PA_SLOT_CAB, 0);
        pa_fx_chain_set_slot_enabled_now(chain, PA_SLOT_DELAY, 1);
    }
    if (front == 1) {
        pa_fx_chain_swap_order(chain, 1, 5);                              /* MOD DYN AMP CAB EQ DRV DLY REV */
        pa_fx_chain_set_slot_enabled_now(chain, PA_SLOT_DYNAMICS, 1);
    }
}

static int run_case(const char *label, uint32_t budget_us, int drive_on, int front) {
    int fails = 0;
    pa_fx_chain_set_core1_budget_us(budget_us);
    printf("--- %s ---\n", label);
    srand(7);
    for (int k = 0; k < K; k++)
        for (int i = 0; i < PA_BLOCK_N; i++)
            in_blk[k][i] = (int32_t)(sin(2.0 * 3.14159265 * 330.0 * (k * PA_BLOCK_N + i) / 44100.0) * (PA_Q21_ONE / 4))
                         + (rand() % 20000) - 10000;

    {   /* single core */
        pa_fx_chain_t chain;
        setup(&chain, drive_on, front);
        for (int k = 0; k < K; k++) {
            feed(out_single[k][0], out_single[k][1], k);
            pa_fx_chain_run_stereo(&chain, out_single[k][0], out_single[k][1], PA_BLOCK_N);
        }
    }
    {   /* pipelined (a fresh chain, a fresh cabinet: the convolver state is a singleton) */
        pa_fx_chain_t chain;
        setup(&chain, drive_on, front);
        memset(c1_L, 0, sizeof c1_L); memset(c1_R, 0, sizeof c1_R);
        hook_stereo = 0;
        pa_fx_chain_set_post_handoff(hook);
        for (int k = 0; k < K; k++) {
            feed(out_pipe[k][0], out_pipe[k][1], k);
            pa_fx_chain_run_stereo(&chain, out_pipe[k][0], out_pipe[k][1], PA_BLOCK_N);
        }
        pa_fx_chain_set_post_handoff(0);
    }

    int silent0 = 1, mismatches = 0, nonzero = 0;
    for (int i = 0; i < PA_BLOCK_N; i++) if (out_pipe[0][0][i] || out_pipe[0][1][i]) silent0 = 0;
    for (int k = 1; k < K; k++)
        for (int ch = 0; ch < 2; ch++)
            for (int i = 0; i < PA_BLOCK_N; i++) {
                if (out_pipe[k][ch][i] != out_single[k - 1][ch][i]) mismatches++;
                if (out_single[k - 1][ch][i]) nonzero++;
            }
    printf("  block 0 of the pipeline is silent: %s\n", silent0 ? "yes" : "NO");
    printf("  pipelined block k vs single-core block k-1: %d mismatching samples of %d (%d non-zero)\n",
           mismatches, (K - 1) * 2 * PA_BLOCK_N, nonzero);
    if (!silent0) fails++;
    if (mismatches) fails++;
    if (front == 3) {                                   /* the stereo must come out of the chain: nothing mono runs behind the modulation */
        int wide = 0;
        for (int k = 0; k < K; k++) for (int i = 0; i < PA_BLOCK_N; i++) if (out_single[k][0][i] != out_single[k][1][i]) wide++;
        printf("  the single-core output is stereo: %d samples with L != R\n", wide);
        if (wide < 100) { printf("  FAIL: the chain did not put out a stereo signal, so the case proves nothing\n"); fails++; }
    } else if (front) {
        printf("  the block reaches the hand-over in stereo: %d samples with L != R\n", hook_stereo);
        if (hook_stereo < 100) { printf("  FAIL: the effect in front of the amp did not make a stereo signal, so the case proves nothing\n"); fails++; }
    }
    if (nonzero < 100) { printf("  FAIL: the test signal produced (almost) no output\n"); fails++; }
    return fails;
}

int main(void) {
    int fails = 0;
    fails += run_case("amp and everything in front on core 1 (budget 1300 us)", 1300u, 0, 0);
    fails += run_case("drive in front of the amp, run on core 0 (budget 1300 us)", 1300u, 1, 0);
    fails += run_case("the amp alone on core 1 (budget 1 us), drive on", 1u, 1, 0);
    fails += run_case("stereo MOD on core 0, then the dynamics and the amp on core 1 (budget 1300 us)", 1300u, 0, 1);
    fails += run_case("stereo MOD on core 0, then the amp alone on core 1 (budget 1 us)", 1u, 0, 2);
    fails += run_case("stereo MOD on core 0, the amp bypassed, a stereo delay behind it (budget 1 us)", 1u, 0, 3);
    fails += run_case("stereo MOD on core 1 (budget 5000 us), the amp bypassed, a stereo delay behind it", 5000u, 0, 3);
    printf(fails ? "RESULT: FAILED\n" : "RESULT: ALL PASSED\n");
    return fails ? 1 : 0;
}
