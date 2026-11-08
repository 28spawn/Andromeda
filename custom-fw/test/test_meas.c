/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* test/test_meas.c - the measurement channel (src/sys/pa_meas.c): the two reports, the command path, the test source, the interrupt counters.
 * Build with -DPA_PRODUCT_ANDROMEDA=1 -DPA_HOST_SIM=1. It also prints the encoded records as hex ("TIMING <hex>", "MEMORY <hex>") for
 * test/test_andromeda_perf.py, which decodes them with the PC tool's own decoder: one layout, checked on both sides. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "pa_config.h"
#include "pa_meas.h"
#include "pa_preset.h"
#include "pa_granular.h"
#include "pa_diag.h"

uint32_t fx_andromeda_overlay_bytes(void) { return 150548u; }
extern void pa_meas_host_set(uint32_t now_ms, uint32_t slot, uint32_t last, uint32_t sum16, uint32_t max, uint32_t calls, uint32_t pool_used, uint32_t pool_peak);
extern volatile uint32_t g_pa_isr_avg_us, g_pa_isr_peak_us;

static int fails = 0;
#define CHECK(ok, ...) do { const int ok_ = !!(ok); printf(ok_ ? "  [PASS] " : "  [FAIL] "); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)   /* the condition is evaluated once: some of them send a request */
static uint32_t rd32(const uint8_t *b, uint32_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24); }

static uint8_t g_rep, g_data[256]; static uint32_t g_len; static int g_n;
static void emit(uint8_t rep, const uint8_t *data, uint32_t len) { g_rep = rep; g_len = len; memcpy(g_data, data, len); g_n++; }
static void hex(const char *tag, const uint8_t *b, uint32_t n) { printf("%s ", tag); for (uint32_t i = 0; i < n; i++) printf("%02x", b[i]); printf("\n"); }
static int req(const uint8_t *msg, uint32_t len) { g_n = 0; const int r = pa_meas_diag(msg, len, emit); return r; }


/* ---- the real diagnostic channel: pa_diag.c fed byte by byte with a packed request, its reply captured ------------------------------------------------------ */
static uint32_t pack78(const uint8_t *in, uint32_t n, uint8_t *out) {
    uint32_t bits = 0, nb = 0, k = 0;
    for (uint32_t i = 0; i < n; i++) { bits |= (uint32_t)in[i] << nb; nb += 8; while (nb >= 7) { out[k++] = bits & 0x7F; bits >>= 7; nb -= 7; } }
    if (nb) out[k++] = bits & 0x7F;
    return k;
}
static uint32_t unpack87(const uint8_t *in, uint32_t n, uint8_t *out) {
    uint32_t bits = 0, nb = 0, k = 0;
    for (uint32_t i = 0; i < n; i++) { bits |= (uint32_t)(in[i] & 0x7F) << nb; nb += 7; if (nb >= 8) { out[k++] = bits & 0xFF; bits >>= 8; nb -= 8; } }
    return k;
}
static uint8_t g_frame[1024]; static uint32_t g_frame_len; static int g_frames;
static void sink(const uint8_t *d, uint32_t n) { memcpy(g_frame, d, n); g_frame_len = n; g_frames++; }
static void feed_request(const uint8_t *inner, uint32_t n) {
    uint8_t pk[64]; const uint32_t k = pack78(inner, n, pk);
    g_frames = 0; g_frame_len = 0;
    pa_diag_feed(0xF0); for (uint32_t i = 0; i < k; i++) pa_diag_feed(pk[i]); pa_diag_feed(0xF7);
}

int main(void) {
    printf("=================================================================\nANDROMEDA measurement channel\n=================================================================\n");

    printf("[1] the timing record: layout and values\n");
    pa_meas_ctx_t cx = { 6, 5, 0xE0, 17, 1 }; pa_meas_set_context(&cx);
    g_pa_isr_avg_us = 612; g_pa_isr_peak_us = 905;
    pa_meas_host_set(123456, 6, 7200, 7200 * 100u / 16u, 9600, 100, 41000, 52000);            /* DLY: last 300 us, mean 300 us (7200 ticks), max 400 us, 100 calls */
    pa_meas_host_set(123456, 7, 2400, 2400 * 50u / 16u, 3000, 50, 41000, 52000);              /* REV: 100 us */
    for (int i = 0; i < 100; i++) pa_meas_isr_sample(i == 50 ? 35000u : 14400u);               /* 600 us blocks, one of 1,458 us: over the 1,451 us budget */
    uint8_t t[PA_MEAS_TIMING_BYTES];
    CHECK(pa_meas_build_timing(t, sizeof t) == PA_MEAS_TIMING_BYTES, "212 bytes");
    CHECK(pa_meas_build_timing(t, 100) == 0, "a buffer that is too small is refused");
    pa_meas_build_timing(t, sizeof t);
    hex("TIMING", t, PA_MEAS_TIMING_BYTES);
    CHECK(t[0] == 1 && t[3] == 6 && t[4] == 5 && t[5] == 0xE0 && t[6] == 17 && t[7] == 1, "version, slot, effect type, enabled blocks, view, governor");
    CHECK(rd32(t, 8) == 123456u && rd32(t, 12) == 44118u && rd32(t, 16) == 64u && rd32(t, 20) == 1451u, "uptime, rate, block, budget");
    CHECK(rd32(t, 24) == 612u && rd32(t, 28) == 905u, "the firmware's own average and peak");
    CHECK(rd32(t, 36) == 100u && rd32(t, 40) == 1u && rd32(t, 32) == 35000u / 24u, "100 blocks, 1 over the budget, max %u us", rd32(t, 32));
    const double mean_us = rd32(t, 44);
    CHECK(fabs(mean_us - (99.0 * 14400 + 35000) / 100.0 / 24.0) < 2.0, "the mean over the blocks: %.0f us", mean_us);
    CHECK(rd32(t, 52 + 16 * 6) == 7200u && rd32(t, 56 + 16 * 6) == 7200u && rd32(t, 60 + 16 * 6) == 9600u && rd32(t, 64 + 16 * 6) == 100u, "the DLY stage: last, mean, max, calls in ticks");
    CHECK(rd32(t, 56 + 16 * 7) == 2400u, "the REV stage: mean 2,400 ticks = 100 us");

    printf("[2] the memory record\n");
    uint8_t m[PA_MEAS_MEMORY_BYTES];
    CHECK(pa_meas_build_memory(m, sizeof m) == PA_MEAS_MEMORY_BYTES, "64 bytes");
    hex("MEMORY", m, PA_MEAS_MEMORY_BYTES);
    CHECK(rd32(m, 4) == 0x80000u && rd32(m, 40) == PA_DSP_POOL_BYTES && rd32(m, 44) == 41000u && rd32(m, 48) == 52000u && rd32(m, 60) == 150548u, "SRAM total, DSP pool size / in use / peak, the overlay");

    printf("[3] the diagnostic channel: requests and replies\n");
    { const uint8_t r_t[] = { 0x7D, PA_MEAS_CMD_TIMING }, r_m[] = { 0x7D, PA_MEAS_CMD_MEMORY }, r_r[] = { 0x7D, PA_MEAS_CMD_RESET }, other[] = { 0x7D, 0x02, 0, 0, 0, 0, 0, 0 }, bad[] = { 0x41, 0x04 };
      CHECK(req(r_t, 2) == 1 && g_n == 1 && g_rep == PA_MEAS_REP_TIMING && g_len == PA_MEAS_TIMING_BYTES, "TIMING is answered with rep 84 and 212 bytes");
      CHECK(req(r_m, 2) == 1 && g_rep == PA_MEAS_REP_MEMORY && g_len == PA_MEAS_MEMORY_BYTES, "MEMORY is answered with rep 85 and 64 bytes");
      CHECK(req(other, 8) == 0 && g_n == 0, "the RAM read (7D 02) is not ours: it falls through to the diagnostic channel");
      CHECK(req(bad, 2) == 0, "another manufacturer's frame is not ours");
      CHECK(req(r_r, 2) == 1 && g_rep == PA_MEAS_REP_RESET, "RESET is acknowledged");
      pa_meas_build_timing(t, sizeof t);
      CHECK(rd32(t, 36) == 0u && rd32(t, 40) == 0u && rd32(t, 32) == 0u && rd32(t, 44) == 0u && rd32(t, 56 + 16 * 6) == 0u, "after the reset every counter is zero (blocks, over, max, mean, the stage table)"); }

    printf("[4] the command queue: arguments are checked, the order is kept, it does not overflow\n");
    { uint8_t c[9] = { 0x7D, PA_MEAS_CMD_CTL, PA_MEAS_OP_LOAD, 19, 0, 0, 0, 0, 0 };
      CHECK(req(c, 9) == 1 && g_data[1] == 0, "LOAD slot 20 (index 19) is accepted");
      c[3] = 20; req(c, 9); CHECK(g_data[1] == 1, "LOAD index 20 is refused (there are 20 slots)");
      c[2] = PA_MEAS_OP_EFFECT; c[3] = 23; req(c, 9); CHECK(g_data[1] == 0, "EFFECT 23 is accepted");
      c[3] = 24; req(c, 9); CHECK(g_data[1] == 1, "EFFECT 24 is refused (there are 24 types)");
      c[2] = PA_MEAS_OP_BLOCK; c[3] = 1; c[4] = 1; req(c, 9); CHECK(g_data[1] == 0, "BLOCK 1 on is accepted");
      c[3] = 3; req(c, 9); CHECK(g_data[1] == 1, "BLOCK 3 is refused");
      c[2] = PA_MEAS_OP_PARAM; c[3] = 6; c[4] = 4; c[5] = 0x00; c[6] = 0x00; c[7] = 0x10; c[8] = 0x00; req(c, 9); CHECK(g_data[1] == 0, "PARAM (slot 6, knob 4, Q21) is accepted");
      c[4] = 6; req(c, 9); CHECK(g_data[1] == 1, "PARAM knob 6 is refused");
      c[2] = PA_MEAS_OP_ACTION; c[3] = 3; c[4] = 0; req(c, 9); CHECK(g_data[1] == 0, "ACTION 3 (clear) is accepted");
      c[3] = 4; req(c, 9); CHECK(g_data[1] == 1, "ACTION 4 is refused");
      c[2] = 99; req(c, 9); CHECK(g_data[1] == 1, "an unknown op is refused");
      pa_meas_cmd_t k; int got = 0, order_ok = 1; const uint8_t want[5] = { PA_MEAS_OP_LOAD, PA_MEAS_OP_EFFECT, PA_MEAS_OP_BLOCK, PA_MEAS_OP_PARAM, PA_MEAS_OP_ACTION };
      while (pa_meas_take_cmd(&k)) { printf("         queued op %d a %d b %d\n", k.op, k.a, k.b); if (got < 5 && k.op != want[got]) order_ok = 0; got++; }
      CHECK(got == 5 && order_ok, "the five accepted commands come out in order (%d)", got);
      pa_meas_cmd_t pk; (void)pk;
      int accepted = 0; c[2] = PA_MEAS_OP_LOAD; c[3] = 1;
      for (int i = 0; i < 12; i++) { req(c, 9); if (g_data[1] == 0) accepted++; }
      CHECK(accepted == 8, "the queue holds 8: the 9th and later are refused, not lost silently (%d accepted)", accepted);
      while (pa_meas_take_cmd(&k)) {}
      c[2] = PA_MEAS_OP_PARAM; c[3] = 6; c[4] = 4; c[5] = 0x78; c[6] = 0x56; c[7] = 0x34; c[8] = 0x12; req(c, 9); pa_meas_take_cmd(&k);
      CHECK(k.c == 0x12345678u && k.a == 6 && k.b == 4, "the u32 value is little endian"); }

    printf("[5] the test source\n");
    const char *nm[5] = { "off", "pluck", "noise", "sine", "chord" };
    for (int mode = 1; mode <= 4; mode++) {
        uint8_t c[9] = { 0x7D, PA_MEAS_CMD_CTL, PA_MEAS_OP_SOURCE, (uint8_t)mode, 40, 0, 0, 0, 0 };
        req(c, 9);
        int32_t L[64], R[64]; double pk = 0, en[8] = { 0 }; int same = 1; long zc = 0; int32_t prev = 0;
        const int blocks = (int)(3.0 * 44118 / 64);
        for (int b = 0; b < blocks; b++) {
            memset(L, 0x55, sizeof L); memset(R, 0xAA, sizeof R);
            pa_meas_inject(L, R, 64);
            for (int i = 0; i < 64; i++) {
                const double v = (double)L[i] / 2097152.0; pk = fmax(pk, fabs(v));
                if (L[i] != R[i]) same = 0;
                if ((prev < 0) != (L[i] < 0) && mode == 3) zc++;
                prev = L[i];
                const int slice = (int)(((long)b * 64 + i) * 8L / (blocks * 64L)); en[slice] += v * v;
            }
        }
        printf("         %-5s peak %.3f of full scale\n", nm[mode], pk);
        CHECK(pk > 0.12 && pk < 0.40, "%s at 40 %%: the peak is %.3f (about 0.36 at the start of a note)", nm[mode], pk);
        CHECK(same, "%s: both channels carry the same signal", nm[mode]);
        if (mode == 3) CHECK(fabs(zc / 2.0 / 3.0 - 220.0) < 4.0, "the sine is 220 Hz (%.1f)", zc / 2.0 / 3.0);
        if (mode == 1 || mode == 4) CHECK(en[0] > 3.0 * en[7] || en[1] > 3.0 * en[7] || en[0] > 0, "%s has notes with energy through the 3 s", nm[mode]);
    }
    { uint8_t off[9] = { 0x7D, PA_MEAS_CMD_CTL, PA_MEAS_OP_SOURCE, 0, 0, 0, 0, 0, 0 }; req(off, 9);
      int32_t L[64], R[64]; for (int i = 0; i < 64; i++) L[i] = R[i] = 1234;
      pa_meas_inject(L, R, 64);
      CHECK(L[10] == 1234 && R[10] == 1234, "source off: the guitar passes untouched"); }
    { uint8_t on[9] = { 0x7D, PA_MEAS_CMD_CTL, PA_MEAS_OP_SOURCE, 3, 30, 0, 0, 0, 0 }; pa_meas_host_set(1000, 0, 0, 0, 0, 0, 0, 0); req(on, 9);
      int32_t L[64] = { 0 }; pa_meas_tick(1000 + 30000); pa_meas_inject(L, 0, 64); int live = 0; for (int i = 0; i < 64; i++) if (L[i]) live = 1;
      CHECK(live, "30 s after the last command the source is still on");
      pa_meas_tick(1000 + 61000); for (int i = 0; i < 64; i++) L[i] = 777; pa_meas_inject(L, 0, 64);
      CHECK(L[3] == 777, "61 s after the last command it has switched itself off: a crashed test leaves the unit playable"); }

    printf("[6] the Performance page's SRAM figure\n");
    CHECK(pa_meas_sram_total_kb() == 512u, "the chip has 512 KB");

    printf("[7] the real diagnostic channel (pa_diag.c): a packed request in, a packed reply out\n");
    { pa_diag_set_reply_sink(sink);
      const uint8_t rq[2] = { 0x7D, PA_MEAS_CMD_TIMING };
      feed_request(rq, 2);
      CHECK(g_frames == 1 && g_frame[0] == 0xF0 && g_frame[g_frame_len - 1] == 0xF7, "TIMING: one frame, F0 ... F7");
      int clean = 1; for (uint32_t i = 1; i + 1 < g_frame_len; i++) if (g_frame[i] & 0x80) clean = 0;
      CHECK(clean, "every data byte of the reply is 7-bit (USB MIDI would corrupt it otherwise)");
      uint8_t body[512]; const uint32_t bn = unpack87(g_frame + 1, g_frame_len - 2, body);
      CHECK(bn >= 4u + PA_MEAS_TIMING_BYTES && body[0] == 0x7D && body[1] == PA_MEAS_REP_TIMING && (body[2] | (body[3] << 8)) == (int)PA_MEAS_TIMING_BYTES, "unpacked: 7D 84, length %d", body[2] | (body[3] << 8));
      CHECK(body[4] == 1 && body[4 + 3] == 6 && body[4 + 4] == 5, "the record arrives intact (version, slot, effect type)");
      printf("         the TIMING reply is %u MIDI bytes on the wire\n", (unsigned)g_frame_len);
      CHECK(g_frame_len < 400u, "a reply is under 400 bytes (a few USB packets)");
      const uint8_t rm[2] = { 0x7D, PA_MEAS_CMD_MEMORY };
      feed_request(rm, 2);
      const uint32_t mn = unpack87(g_frame + 1, g_frame_len - 2, body);
      CHECK(g_frames == 1 && mn >= 4u + PA_MEAS_MEMORY_BYTES && body[1] == PA_MEAS_REP_MEMORY, "MEMORY: rep 85, 64 bytes");
      const uint8_t ram[9] = { 0x7D, 0x02, 0, 0, 0xC0, 0x01, 16, 0, 0 };
      feed_request(ram, 9);
      const uint32_t rn = unpack87(g_frame + 1, g_frame_len - 2, body);
      CHECK(g_frames == 1 && rn >= 4u && body[1] == 0x82, "the RAM read (7D 02) still answers, alongside the measurement commands");
      const uint8_t ctl[9] = { 0x7D, PA_MEAS_CMD_CTL, PA_MEAS_OP_EFFECT, 9, 0, 0, 0, 0, 0 };
      feed_request(ctl, 9);
      const uint32_t cn = unpack87(g_frame + 1, g_frame_len - 2, body);
      pa_meas_cmd_t k; const int got = pa_meas_take_cmd(&k);
      CHECK(cn >= 6u && body[1] == PA_MEAS_REP_CTL && body[4] == PA_MEAS_OP_EFFECT && body[5] == 0 && got && k.op == PA_MEAS_OP_EFFECT && k.a == 9, "CTL effect 9: acknowledged, and queued for the main loop");
      const uint8_t junk[3] = { 0x7D, 0x7F, 0 };
      feed_request(junk, 3);
      CHECK(g_frames == 0, "an unknown 7D command is ignored (no reply)"); }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL MEASUREMENT CHANNEL TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
