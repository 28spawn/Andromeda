/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* src/sys/pa_meas.c - the measurement channel (include/pa_meas.h). Andromeda only: it compiles to nothing in the Onyx firmware. */
#include "pa_config.h"
#if PA_PRODUCT_ANDROMEDA
#include <string.h>
#include <math.h>
#include "pa_meas.h"
#include "pa_preset.h"
#include "pa_granular.h"

#if !defined(PA_HOST_BUILD) && !defined(PA_HOST_SIM)
#define MEAS_DEVICE 1
#else
#define MEAS_DEVICE 0
#endif

#define MEAS_FS 44118.0f

/* ---- what the device keeps (fixed addresses and linker symbols), and stand-ins for the host tests ---------------------------------------------------------- */
#if MEAS_DEVICE
extern volatile uint32_t g_pa_isr_avg_us, g_pa_isr_peak_us;      /* audio_alnk.c: the firmware's own moving average and decaying peak */
extern volatile uint8_t  g_pa_gov_level;                          /* pa_audio_core.c */
extern uint32_t plat_ms(void);
extern char _data_start[], _data_end[], _bss_start[], _bss_end[], _bleb_start[], _bleb_end[], _lowram_start[], _lowram_end[];
extern char _pool_start[], _pool_end[], _pool_limit[], _rf_start[], _rf_end[], _rt_start[], _c1_end[];
#define SLOT_TABLE   ((volatile uint32_t *)(uintptr_t)0x01C7C500u)    /* fx_graph.c: per slot 4 words: max, sum / 16, calls, last (24 MHz ticks) */
#define POOL_STATS   ((volatile uint32_t *)(uintptr_t)0x01C7E780u)    /* pa_pool.c: [1] in use, [2] largest free, [3] failed requests, [5] peak */
#define NOW_MS()     plat_ms()
#else
volatile uint32_t g_pa_isr_avg_us, g_pa_isr_peak_us;
static uint32_t s_slot_table[PA_MEAS_SLOTS * 4u], s_pool_stats[8];
static uint32_t s_now_ms;
#define SLOT_TABLE   s_slot_table
#define POOL_STATS   s_pool_stats
#define NOW_MS()     s_now_ms
void pa_meas_host_set(uint32_t now_ms, uint32_t slot, uint32_t last, uint32_t sum16, uint32_t max, uint32_t calls, uint32_t pool_used, uint32_t pool_peak) {
    s_now_ms = now_ms;
    if (slot < PA_MEAS_SLOTS) { s_slot_table[slot * 4u] = max; s_slot_table[slot * 4u + 1u] = sum16; s_slot_table[slot * 4u + 2u] = calls; s_slot_table[slot * 4u + 3u] = last; }
    s_pool_stats[1] = pool_used; s_pool_stats[5] = pool_peak;
}
#endif

/* ---- the audio interrupt's counters ---------------------------------------------------------------------------------------------------------------------- */
static volatile uint32_t s_blocks, s_over, s_max, s_sum16, s_reset_ms;

void pa_meas_isr_sample(uint32_t dt) {
    s_blocks++;
    if (dt > s_max) s_max = dt;
    if (dt > PA_MEAS_BUDGET_TICKS) s_over++;
    s_sum16 += dt >> 4;
    if (s_sum16 >= 0xC0000000u) { s_sum16 >>= 1; s_blocks = (s_blocks + 1u) >> 1; s_over = (s_over + 1u) >> 1; }     /* the sum and the count keep their ratio; the same guard as the per-stage table */
}

__attribute__((weak)) void pa_smp_meas_reset(void) {}               /* smp_cpu1.c: core 1's counters */
__attribute__((weak)) int  pa_fx_alloc_cut(void) { return 0; }       /* fx_graph.c: the core allocator's cut */
void pa_meas_reset_counters(void) {
    for (uint32_t i = 0; i < PA_MEAS_SLOTS * 4u; i++) SLOT_TABLE[i] = 0u;
    s_blocks = 0u; s_over = 0u; s_max = 0u; s_sum16 = 0u;
    s_reset_ms = NOW_MS();
#if MEAS_DEVICE
    g_pa_isr_peak_us = 0u;
    pa_smp_meas_reset();
#endif
}

/* ---- the context the main loop publishes ----------------------------------------------------------------------------------------------------------------- */
static pa_meas_ctx_t s_ctx;
void pa_meas_set_context(const pa_meas_ctx_t *c) { s_ctx = *c; }

/* ---- the test source ------------------------------------------------------------------------------------------------------------------------------------ */
/* It replaces the guitar so a measurement is repeatable and needs no player or audio interface: plucks, noise bursts or a sine. The oscillators are coupled
 * (two multiply-adds per sample, no sinf in the loop), so the source costs well under 1 % of the block and does not colour what is being measured. */
#define SRC_NOTE_SAMPLES 22059u                    /* 0.5 s */
typedef struct { float s, c, e; } osc_t;
static volatile uint8_t  s_src_mode, s_src_level;
static volatile uint32_t s_src_last_cmd_ms;
static uint8_t  s_src_run_mode;                    /* the mode the generator state belongs to */
static uint32_t s_src_pos, s_src_note, s_src_rng;
static float    s_src_env, s_src_decay, s_src_att;
static osc_t    s_osc[6];

static void osc_set(osc_t *o, float hz) { o->e = 2.0f * sinf(3.14159265f * hz / MEAS_FS); o->s = 0.0f; o->c = 1.0f; }
static inline float osc_next(osc_t *o) { o->s += o->e * o->c; o->c -= o->e * o->s; return o->s; }

static void src_note_start(uint8_t mode) {
    static const float k_notes[4] = { 196.0f, 246.94f, 293.66f, 392.0f };
    const float f = k_notes[s_src_note & 3u];
    const int two = (mode == PA_MEAS_SRC_CHORD);
    osc_set(&s_osc[0], f); osc_set(&s_osc[1], 2.0f * f); osc_set(&s_osc[2], 3.0f * f);
    if (two) { osc_set(&s_osc[3], f * 1.5f); osc_set(&s_osc[4], 3.0f * f); osc_set(&s_osc[5], 4.5f * f); }
    s_src_env = 1.0f; s_src_att = 0.0f;
    s_src_decay = (mode == PA_MEAS_SRC_NOISE) ? 0.99978f : 0.99992f;           /* time constants of about 0.1 s and 0.28 s */
    s_src_note++;
}

void pa_meas_inject(int32_t *buf_L, int32_t *buf_R, int n) {
    const uint8_t mode = s_src_mode;
    if (mode == PA_MEAS_SRC_OFF || mode >= PA_MEAS_SRC_COUNT) return;
    if (mode != s_src_run_mode) {                                           /* a new mode: start its generator from the beginning of a note */
        s_src_run_mode = mode; s_src_pos = 0u; s_src_note = 0u; s_src_rng = 0x1234567u;
        osc_set(&s_osc[0], 220.0f);
        if (mode != PA_MEAS_SRC_SINE) src_note_start(mode);
    }
    const float amp = (float)s_src_level * (0.01f * 0.9f * 2097152.0f);
    for (int i = 0; i < n; i++) {
        float v;
        if (mode == PA_MEAS_SRC_SINE) {
            v = osc_next(&s_osc[0]);
        } else {
            if (s_src_pos >= SRC_NOTE_SAMPLES) { s_src_pos = 0u; src_note_start(mode); }
            s_src_pos++;
            s_src_env *= s_src_decay;
            if (s_src_att < 1.0f) { s_src_att += 1.0f / 22.0f; if (s_src_att > 1.0f) s_src_att = 1.0f; }         /* a 0.5 ms attack: a pluck, not a click */
            if (mode == PA_MEAS_SRC_NOISE) {
                s_src_rng = s_src_rng * 1664525u + 1013904223u;
                v = ((float)(int32_t)s_src_rng * (1.0f / 2147483648.0f)) * s_src_env * s_src_att;
            } else {
                float x = osc_next(&s_osc[0]) + 0.5f * osc_next(&s_osc[1]) + 0.25f * osc_next(&s_osc[2]);
                if (mode == PA_MEAS_SRC_CHORD) x = 0.6f * (x + osc_next(&s_osc[3]) + 0.5f * osc_next(&s_osc[4]) + 0.25f * osc_next(&s_osc[5]));
                v = x * (1.0f / 1.75f) * s_src_env * s_src_att;
            }
        }
        const int32_t q = (int32_t)(v * amp);
        buf_L[i] = q;
        if (buf_R && buf_R != buf_L) buf_R[i] = q;
    }
}

/* ---- commands from the host ------------------------------------------------------------------------------------------------------------------------------ */
#define CMD_Q 8u
static pa_meas_cmd_t s_q[CMD_Q];
static volatile uint32_t s_q_head, s_q_tail;           /* the diagnostic channel writes the head, the main loop the tail */

static int q_push(const pa_meas_cmd_t *c) {
    const uint32_t h = s_q_head;
    if (h - s_q_tail >= CMD_Q) return 0;
    s_q[h % CMD_Q] = *c;
    s_q_head = h + 1u;
    return 1;
}

int pa_meas_take_cmd(pa_meas_cmd_t *out) {
    const uint32_t t = s_q_tail;
    if (t == s_q_head) return 0;
    *out = s_q[t % CMD_Q];
    s_q_tail = t + 1u;
    return 1;
}

void pa_meas_tick(uint32_t now) {
    if (s_src_mode != PA_MEAS_SRC_OFF && (uint32_t)(now - s_src_last_cmd_ms) > PA_MEAS_SOURCE_TTL_MS) s_src_mode = PA_MEAS_SRC_OFF;       /* the tool is gone: give the guitar back */
}

/* ---- the reports ------------------------------------------------------------------------------------------------------------------------------------------ */
static void put32(uint8_t *o, uint32_t at, uint32_t v) { o[at] = (uint8_t)v; o[at + 1] = (uint8_t)(v >> 8); o[at + 2] = (uint8_t)(v >> 16); o[at + 3] = (uint8_t)(v >> 24); }

uint32_t pa_meas_build_timing(uint8_t *o, uint32_t cap) {
    if (cap < PA_MEAS_TIMING_BYTES) return 0u;
    memset(o, 0, PA_MEAS_TIMING_BYTES);
    const uint32_t now = NOW_MS();
    o[0] = 1u;
    o[1] = (uint8_t)((MEAS_DEVICE ? 1u : 0u) | ((s_src_mode != PA_MEAS_SRC_OFF) ? 2u : 0u));
    o[2] = s_src_mode; o[3] = s_ctx.slot; o[4] = s_ctx.effect_type; o[5] = s_ctx.enabled_mask; o[6] = s_ctx.view; o[7] = s_ctx.gov_level;
    put32(o, 8, now); put32(o, 12, 44118u); put32(o, 16, 64u); put32(o, 20, PA_MEAS_BUDGET_US);
    put32(o, 24, g_pa_isr_avg_us); put32(o, 28, g_pa_isr_peak_us);
    const uint32_t blocks = s_blocks;
    put32(o, 32, s_max / 24u); put32(o, 36, blocks); put32(o, 40, s_over);
    put32(o, 44, blocks ? (uint32_t)(((uint64_t)s_sum16 * 16u) / blocks) / 24u : 0u);
    put32(o, 48, now - s_reset_ms);
    for (uint32_t s = 0; s < PA_MEAS_SLOTS; s++) {
        const uint32_t max = SLOT_TABLE[s * 4u], sum16 = SLOT_TABLE[s * 4u + 1u], calls = SLOT_TABLE[s * 4u + 2u], last = SLOT_TABLE[s * 4u + 3u];
        put32(o, 52u + s * 16u, last);
        put32(o, 56u + s * 16u, calls ? (uint32_t)(((uint64_t)sum16 * 16u) / calls) : 0u);
        put32(o, 60u + s * 16u, max);
        put32(o, 64u + s * 16u, calls);
    }
#if MEAS_DEVICE
    {   /* core 1 (smp_cpu1.c keeps the words; see SMPS there) */
        volatile uint32_t *C1 = (volatile uint32_t *)(uintptr_t)0x01C7D200u;
        const uint32_t jobs = C1[11];
        o[180] = (uint8_t)(C1[0] == 0x53504D53u && C1[1] ? 1u : 0u);
        o[181] = (uint8_t)pa_fx_alloc_cut();
        o[182] = (uint8_t)C1[6];
        put32(o, 184, C1[3]);
        put32(o, 188, jobs ? (uint32_t)(((uint64_t)C1[10] * 16u) / jobs) : 0u);
        put32(o, 192, C1[12]); put32(o, 196, jobs); put32(o, 200, C1[13]); put32(o, 204, C1[5]); put32(o, 208, C1[8]);
    }
#endif
    return PA_MEAS_TIMING_BYTES;
}

uint32_t pa_meas_build_memory(uint8_t *o, uint32_t cap) {
    if (cap < PA_MEAS_MEMORY_BYTES) return 0u;
    memset(o, 0, PA_MEAS_MEMORY_BYTES);
    o[0] = 1u; o[1] = MEAS_DEVICE ? 1u : 0u;
    put32(o, 4, 0x80000u);
#if MEAS_DEVICE
    put32(o, 8,  (uint32_t)(_data_end - _data_start));
    put32(o, 12, (uint32_t)(_bss_end - _bss_start));
    put32(o, 16, (uint32_t)(_bleb_end - _bleb_start));
    put32(o, 20, (uint32_t)(_lowram_end - _lowram_start));
    put32(o, 24, (uint32_t)(_rf_end - _rf_start) + (uint32_t)(_c1_end - _rt_start));
    put32(o, 28, (uint32_t)(_pool_end - _pool_start));
    put32(o, 32, (uint32_t)(_pool_limit - _pool_start));
    put32(o, 36, (uint32_t)(_pool_limit - _pool_end));
#endif
    put32(o, 40, PA_DSP_POOL_BYTES);
    put32(o, 44, POOL_STATS[1]); put32(o, 48, POOL_STATS[5]); put32(o, 52, POOL_STATS[2]); put32(o, 56, POOL_STATS[3]);
    put32(o, 60, fx_andromeda_overlay_bytes());
    return PA_MEAS_MEMORY_BYTES;
}

uint32_t pa_meas_sram_total_kb(void) { return 512u; }

uint32_t pa_meas_sram_used_kb(void) {
#if MEAS_DEVICE
    /* everything that is placed in SRAM for good (data, bss, the radio's bss, low RAM, the RAM code, the whole .pool section incl. the overlay and the DSP pool) */
    const uint32_t bytes = (uint32_t)(_data_end - _data_start) + (uint32_t)(_bss_end - _bss_start) + (uint32_t)(_bleb_end - _bleb_start) + (uint32_t)(_lowram_end - _lowram_start)
                         + (uint32_t)(_rf_end - _rf_start) + (uint32_t)(_c1_end - _rt_start) + (uint32_t)(_pool_end - _pool_start);
    return (bytes + 512u) / 1024u;
#else
    return 0u;
#endif
}

/* ---- the diagnostic channel's entry ----------------------------------------------------------------------------------------------------------------------- */
int pa_meas_diag(const uint8_t *in, uint32_t len, pa_meas_emit_fn emit) {
    if (len < 2u || in[0] != 0x7Du) return 0;
    const uint8_t cmd = in[1];
    uint8_t rec[PA_MEAS_TIMING_BYTES];
    if (cmd != PA_MEAS_CMD_TIMING && cmd != PA_MEAS_CMD_MEMORY && cmd != PA_MEAS_CMD_RESET && cmd != PA_MEAS_CMD_CTL) return 0;
    s_src_last_cmd_ms = NOW_MS();                                          /* any command keeps the test source alive */
    if (cmd == PA_MEAS_CMD_TIMING) { const uint32_t n = pa_meas_build_timing(rec, sizeof rec); emit(PA_MEAS_REP_TIMING, rec, n); }
    else if (cmd == PA_MEAS_CMD_MEMORY) { const uint32_t n = pa_meas_build_memory(rec, sizeof rec); emit(PA_MEAS_REP_MEMORY, rec, n); }
    else if (cmd == PA_MEAS_CMD_RESET) { pa_meas_reset_counters(); rec[0] = 0u; emit(PA_MEAS_REP_RESET, rec, 1u); }
    else {
        pa_meas_cmd_t c; c.op = len > 2u ? in[2] : 0u; c.a = len > 3u ? in[3] : 0u; c.b = len > 4u ? in[4] : 0u;
        c.c = (len >= 9u) ? ((uint32_t)in[5] | ((uint32_t)in[6] << 8) | ((uint32_t)in[7] << 16) | ((uint32_t)in[8] << 24)) : 0u;
        uint8_t status = 1u;
        switch (c.op) {
            case PA_MEAS_OP_LOAD:   if (c.a < PA_PRESET_COUNT) status = q_push(&c) ? 0u : 1u; break;
            case PA_MEAS_OP_EFFECT: if (c.a < AND_TYPE_COUNT) status = q_push(&c) ? 0u : 1u; break;
            case PA_MEAS_OP_BLOCK:  if (c.a < 3u && c.b <= 1u) status = q_push(&c) ? 0u : 1u; break;
            case PA_MEAS_OP_PARAM:  if (c.a < PA_SLOT_COUNT && c.b < 6u) status = q_push(&c) ? 0u : 1u; break;
            case PA_MEAS_OP_ACTION: if (c.a <= AND_ACT_CLEAR) status = q_push(&c) ? 0u : 1u; break;
            case PA_MEAS_OP_SOURCE:
                if (c.a < PA_MEAS_SRC_COUNT) {
                    s_src_level = (uint8_t)(c.b > 100u ? 100u : (c.b ? c.b : 40u));
                    s_src_mode = c.a;
                    status = 0u;
                }
                break;
            case PA_MEAS_OP_RESETCNT: pa_meas_reset_counters(); status = 0u; break;
            default: break;
        }
        rec[0] = c.op; rec[1] = status;
        emit(PA_MEAS_REP_CTL, rec, 2u);
    }
    return 1;
}

#endif /* PA_PRODUCT_ANDROMEDA */
