/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* custom-fw/include/pa_meas.h - the measurement channel (Andromeda): processing time in ms, SRAM use and a remote control, over USB MIDI.
 *
 * It rides on the private diagnostic channel (pa_diag.h, manufacturer ID 0x7D, frames 8-to-7 packed like the RAM read). Every request is
 *     F0 <pack( 7D <cmd> <args...> )> F7
 * and every reply is
 *     F0 <pack( 7D <rep> <len u16 LE> <record...> )> F7
 *
 *   cmd 04 TIMING  -> rep 84: the timing record (below). Counters run since the last reset (cmd 06).
 *   cmd 05 MEMORY  -> rep 85: the memory record (below).
 *   cmd 06 RESET   -> rep 86 <1 byte status>: zero the timing counters (the per-stage table and the audio interrupt's max / mean / over-budget count).
 *   cmd 07 CTL <op u8> <a u8> <b u8> <c u32 LE> -> rep 87 <op> <status>: status 0 = queued for the main loop, 1 = refused (bad arguments).
 *        op 1 LOAD    a = slot 0..19                         op 2 EFFECT  a = effect type 0..23 (effect * 4 + variant: GLASS, BLOOM, ACCRETION, CHOIR, FRACTURE, CONSTELLATION), the type's own knobs are loaded
 *        op 3 BLOCK   a = 0 EFFECT, 1 LAYER, 2 SPACE, b = 1 on / 0 off
 *        op 4 PARAM   a = slot id (0..7), b = knob id (0..5), c = Q21 value
 *        op 5 ACTION  a = AND_ACT_* (pa_granular.h): strike / freeze / record ...
 *        op 6 SOURCE  a = 0 off, 1 pluck every 0.5 s, 2 noise bursts, 3 sine 220 Hz, 4 pluck chords (two voices), b = level %, replaces the guitar input.
 *                     It switches itself off 60 s after the last meas command, so a crashed test leaves the unit playable.
 *        op 7 RESETCNT (same as cmd 06)
 *
 * TIMING record (little endian, 212 bytes):
 *    0 u8 version (1)      1 u8 flags: b0 device build, b1 test source on, b2 overlay borrowed        2 u8 source mode   3 u8 slot (preset) index 0..19
 *    4 u8 effect type 0..11  5 u8 enabled blocks (bit per slot id: 0 DYN 1 DRV 2 AMP 3 CAB 4 EQ 5 MOD 6 DLY 7 REV)   6 u8 view   7 u8 governor level
 *    8 u32 uptime ms     12 u32 sample rate Hz   16 u32 block frames   20 u32 block budget us
 *   24 u32 audio interrupt average us (the firmware's own moving average)    28 u32 its decaying peak us
 *   32 u32 audio interrupt max us since the reset   36 u32 blocks since the reset   40 u32 blocks over the budget since the reset
 *   44 u32 audio interrupt mean us since the reset (a block's whole time: the chain, the codec, the screen's share, everything the interrupt does)
 *   48 u32 ms since the reset
 *   52.. eight slots in slot order DYN DRV AMP CAB EQ MOD DLY REV, 16 bytes each: u32 last, u32 mean, u32 max, u32 calls, times in 24 MHz ticks (24 ticks = 1 us)
 *  180 u8 core 1 online   181 u8 the core allocator's cut (core 1 runs the conditioning and slot_order[0..cut); 0 = everything on core 0)   182 u8 why core 1 is off (smp_cpu1.c)   183 reserved
 *  184 u32 core 1's last job, ticks   188 u32 its mean since the reset   192 u32 its longest since the reset   196 u32 jobs since the reset   200 u32 blocks core 1 was late since the reset
 *  204 u32 the longest core 0 waited for core 1 (ticks)   208 u32 core 1's deepest stack use, bytes
 *
 * MEMORY record (little endian, 64 bytes):
 *    0 u8 version (1)  1 u8 flags: b0 device build    2..3 reserved
 *    4 u32 SRAM total (0x80000)    8 u32 .data   12 u32 .bss   16 u32 BLE bss   20 u32 low RAM   24 u32 RAM code (the .ramfn + .c1_text regions)
 *   28 u32 .pool static (the overlay and the DSP pool live here)   32 u32 .pool region size   36 u32 .pool region free
 *   40 u32 DSP pool size   44 u32 DSP pool in use   48 u32 DSP pool peak   52 u32 DSP pool largest free block   56 u32 failed pool requests
 *   60 u32 effect overlay bytes (the buffer GLASS / BLOOM / ACCRETION share)
 */
#ifndef PA_MEAS_H
#define PA_MEAS_H

#include <stdint.h>
#include "pa_config.h"

#define PA_MEAS_CMD_TIMING  0x04u
#define PA_MEAS_REP_TIMING  0x84u
#define PA_MEAS_CMD_MEMORY  0x05u
#define PA_MEAS_REP_MEMORY  0x85u
#define PA_MEAS_CMD_RESET   0x06u
#define PA_MEAS_REP_RESET   0x86u
#define PA_MEAS_CMD_CTL     0x07u
#define PA_MEAS_REP_CTL     0x87u

#define PA_MEAS_TIMING_BYTES 212u
#define PA_MEAS_MEMORY_BYTES 64u
#define PA_MEAS_SLOTS        8u

enum { PA_MEAS_OP_LOAD = 1, PA_MEAS_OP_EFFECT, PA_MEAS_OP_BLOCK, PA_MEAS_OP_PARAM, PA_MEAS_OP_ACTION, PA_MEAS_OP_SOURCE, PA_MEAS_OP_RESETCNT };
enum { PA_MEAS_SRC_OFF = 0, PA_MEAS_SRC_PLUCK, PA_MEAS_SRC_NOISE, PA_MEAS_SRC_SINE, PA_MEAS_SRC_CHORD, PA_MEAS_SRC_COUNT };

#define PA_MEAS_BUDGET_TICKS 34813u      /* 64 frames at 44,118 Hz, in 24 MHz ticks */
#define PA_MEAS_BUDGET_US    1451u
#define PA_MEAS_SOURCE_TTL_MS 60000u

typedef struct { uint8_t op, a, b; uint32_t c; } pa_meas_cmd_t;

/* what the main loop knows and the report shows (set by pa_ui_meas_service) */
typedef struct { uint8_t slot, effect_type, enabled_mask, view, gov_level; } pa_meas_ctx_t;

#if PA_PRODUCT_ANDROMEDA
void     pa_meas_isr_sample(uint32_t dt_ticks24);                       /* audio interrupt, once per block */
void     pa_meas_inject(int32_t *buf_L, int32_t *buf_R, int n);        /* audio thread: replaces the guitar by the test source when it is on (Q21) */
void     pa_meas_set_context(const pa_meas_ctx_t *c);
int      pa_meas_take_cmd(pa_meas_cmd_t *out);                          /* main loop: the next queued command, 1 = got one */
void     pa_meas_tick(uint32_t now_ms);                                 /* main loop: the test source's expiry */
void     pa_meas_reset_counters(void);
uint32_t pa_meas_build_timing(uint8_t *out, uint32_t cap);              /* -> PA_MEAS_TIMING_BYTES */
uint32_t pa_meas_build_memory(uint8_t *out, uint32_t cap);              /* -> PA_MEAS_MEMORY_BYTES */
uint32_t pa_meas_sram_used_kb(void);                                    /* the Performance page: everything static + the DSP pool in use, in KB */
uint32_t pa_meas_sram_total_kb(void);

/* the diagnostic channel hands a decoded request here; `emit` sends one reply record. Returns 1 when the command was one of ours. */
typedef void (*pa_meas_emit_fn)(uint8_t rep, const uint8_t *data, uint32_t len);
int      pa_meas_diag(const uint8_t *inner, uint32_t len, pa_meas_emit_fn emit);
#endif

#endif /* PA_MEAS_H */
