/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* custom-fw/test/test_ui_cosmic.c - the Andromeda home screen (ui_cosmic.c) on a 240x240 framebuffer
 *
 * Build with -DPA_PRODUCT_ANDROMEDA=1. Checks, over every algorithm and every looper / freeze state:
 *   - no drawing command leaves the panel,
 *   - a frame stays inside the display-list budget (ui_5x7.c holds 1,372 commands; we allow 900),
 *   - the states are visible: the bank colour in the badge, red on the ring while recording, ice
 *     while frozen, a red/corona edge while clipping, nothing crashes on empty or missing telemetry.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ui_cosmic.h"
#include "ui_cosmic_theme.h"

static uint16_t s_fb[240 * 240];
static int s_violations = 0;
static int s_cmds = 0;
static int s_plus_glass = 0, s_layer_title = 0, s_str_hits = 0;
static char s_last_strs[64][24];
static int fails = 0;

#define CHECK(ok, ...) do { printf((ok) ? "  [PASS] " : "  [FAIL] "); printf(__VA_ARGS__); printf("\n"); if (!(ok)) fails++; } while (0)
#define CMD_BUDGET 900

void st7735s_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565) {
    s_cmds++;
    if ((uint32_t)x + w > 240u || (uint32_t)y + h > 240u || w == 0 || h == 0) { s_violations++; return; }
    for (uint32_t j = y; j < (uint32_t)y + h; j++)
        for (uint32_t i = x; i < (uint32_t)x + w; i++) s_fb[j * 240u + i] = rgb565;
}
void ui_draw_str(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg) {
    s_cmds++;                                              /* one display-list command per string */
    if (s && s_str_hits < 64) { strncpy(s_last_strs[s_str_hits], s, 23); s_last_strs[s_str_hits][23] = 0; }
    s_str_hits++;
    if (s && strcmp(s, "+GLASS") == 0) s_plus_glass++;
    if (s && strcmp(s, "LAYER GLASS") == 0) s_layer_title++;
    size_t n = strlen(s);
    if (x + n * 6u > 240u + 1u || y + 8u > 240u) s_violations++;
    (void)fg; (void)bg;
}

uint32_t plat_ms(void) {
    static uint32_t s_ms;
    return s_ms += 33u;
}

static int count_colour(uint16_t c, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (s_fb[y * 240 + x] == c) n++;
    return n;
}

static int render(const gran_telemetry_t *t, gran_preset_t p, uint8_t focus, const int32_t prm[AND_FOCUS_COUNT]) {
    s_cmds = 0;
    ui_cosmic_render(t, p, focus, prm, (uint8_t)(p + 1), NULL, 80);
    return s_cmds;
}

int main(void) {
    printf("=================================================================\n");
    printf("ANDROMEDA home screen (ui_cosmic.c), 240x240: GLASS, BLOOM, ACCRETION\n");
    printf("=================================================================\n");

    gran_telemetry_t t;
    memset(&t, 0, sizeof t);
    t.active_count = GRANULAR_MAX_VOICES;
    for (int i = 0; i < GRANULAR_MAX_VOICES; i++) {
        t.voices[i].norm_distance = (uint8_t)(i * 31);
        t.voices[i].norm_pan      = (int8_t)(i * 29 - 120);
        t.voices[i].norm_size     = (uint8_t)(i * 33);
        t.voices[i].pitch_cents   = (int16_t)((i % 3) ? 1200 : -1200);
        t.voices[i].reverse       = (i % 4) == 0;
    }
    for (unsigned c = 0; c < GRAN_TAPE_COLS; c++) t.tape_env[c] = (uint8_t)(c * 5u);
    for (unsigned k = 0; k < GLASS_TELEM_MODES; k++) t.mode_lvl[k] = (uint8_t)(255u - k * 20u);
    t.layers = 7; t.gr_pct = 31;                            /* more layers than rings, on purpose */
    const int32_t prm[AND_FOCUS_COUNT] = { 0, 2097152, 1048576, 2200000 /* out of range on purpose */, 1048576, 700000, 400000, 1048576, 1500000, 2097152, 900000 };

    int worst = 0;
    for (int frame = 0; frame < 40; frame++) {             /* the animation phases too */
        for (int p = 0; p < GRAN_PRESET_COUNT; p++) {
            for (uint8_t ls = 0; ls < 4; ls++) {
                t.effect = (uint8_t)(p / 4); t.variant = (uint8_t)(p % 4); t.voices_n = 4; for (int q = 0; q < 4; q++) { t.voice_env[q] = (uint8_t)(60 * q + 40); t.voice_semi[q] = (int8_t)(q * 5 - 5); } t.seq_pos = (uint8_t)(frame & 15); t.seq_mask = (uint16_t)(0xA5A5 ^ frame); for (int q = 0; q < 16; q++) t.seq_pitch[q] = (uint8_t)((q * 37) & 255);
                t.looper_state = ls; t.looper_phase = (uint16_t)(frame * 97 % 1024);
                t.frozen = (uint8_t)(p % 2); t.clipping_alert = (uint8_t)(frame % 3);
                int n = render(&t, (gran_preset_t)p, (uint8_t)(frame % AND_FOCUS_COUNT), prm);
                if (n > worst) worst = n;
            }
        }
    }
    CHECK(s_violations == 0, "no command leaves the 240x240 panel (%d violations)", s_violations);
    CHECK(worst <= CMD_BUDGET, "busiest frame uses %d display-list commands (budget %d of 1372)", worst, CMD_BUDGET);

    t.looper_state = 0; t.frozen = 0; t.clipping_alert = 0;
    const uint16_t eff_col[6] = { AND_VIOLET, AND_ION, AND_CORONA, AND_AURORA, AND_MAGENTA, AND_SOLAR };
    for (int e = 0; e < 6; e++) {
        t.effect = (uint8_t)e;
        render(&t, (gran_preset_t)(e * 4), 0, prm);
        CHECK(count_colour(eff_col[e], 4, 6, 30, 21) > 200, "effect %d badge is drawn in its own colour", e + 1);
    }

    t.effect = AND_FX_ACCRETION; t.looper_state = 1; render(&t, GRAN_PRESET_ACC_SOS, 0, prm);
    CHECK(count_colour(AND_LOOP_REC, 50, 26, 190, 151) > 40, "ACCRETION recording: the loop ring turns red");
    CHECK(count_colour(AND_LOOP_REC, 30, 150, 223, 174) > 100, "ACCRETION recording: the tape strip frame turns red");

    t.effect = AND_FX_BLOOM; t.looper_state = 0; t.frozen = 1; render(&t, GRAN_PRESET_BLOOM_GARDEN, 0, prm);
    CHECK(count_colour(AND_FROZEN, 0, 26, 240, 174) > 60, "BLOOM frozen: the radar and the history turn to ice");

    t.effect = AND_FX_GLASS; t.frozen = 0; render(&t, GRAN_PRESET_GLASS_BELL, 0, prm);
    CHECK(count_colour(AND_STARLIGHT, 60, 40, 180, 150) > 10, "GLASS: loud modes show as bright planets");
    int lit = count_colour(AND_ICE, 30, 150, 223, 174) + count_colour(AND_VIOLET, 30, 150, 223, 174);
    CHECK(lit > 200, "GLASS: the twelve mode bars are drawn (%d px)", lit);

    t.effect = AND_FX_ACCRETION; t.clipping_alert = 5; render(&t, GRAN_PRESET_ACC_DECAY, 0, prm);
    int edge = count_colour(AND_RED_GIANT, 0, 0, 240, 2) + count_colour(AND_CORONA, 0, 0, 240, 2);
    CHECK(edge >= 200, "the limiter working hard: the top edge flares");

    t.clipping_alert = 0; t.effect = AND_FX_GLASS;
    render(&t, GRAN_PRESET_GLASS_CRYSTAL, 4, prm);                       /* MIX focused */
    int mix_on = count_colour(AND_SOLAR, 126, 214, 240, 226);
    render(&t, GRAN_PRESET_GLASS_CRYSTAL, 0, prm);
    int mix_off = count_colour(AND_SOLAR, 126, 214, 240, 226);
    CHECK(mix_on > mix_off, "focus on MIX lights the MIX bar in gold (%d px against %d)", mix_on, mix_off);
    render(&t, GRAN_PRESET_GLASS_CRYSTAL, 5, prm);
    CHECK(count_colour(AND_SOLAR, 126, 226, 240, 240) > 0, "focus on SPACE lights the SPACE bar in gold");

    s_plus_glass = 0; t.effect = AND_FX_BLOOM; t.frozen = 0; render(&t, GRAN_PRESET_BLOOM_GARDEN, 0, prm);                  /* the layer is on (level > 0): the home screen says so */
    int plus = s_plus_glass;
    render(&t, GRAN_PRESET_BLOOM_GARDEN, 6, prm);                        /* focus on the layer: its own header and tiles */
    CHECK(s_layer_title > 0, "layer focus: the title says LAYER GLASS");
    CHECK(plus > 0, "the layer is on: +GLASS shows on the radar");
    render(&t, GRAN_PRESET_BLOOM_GARDEN, 10, prm);                       /* focus on the layer level */
    CHECK(count_colour(AND_SOLAR, 126, 214, 240, 226) > 0, "focus on the layer level lights its bar in gold");

    t.effect = AND_FX_BLOOM;                                             /* telemetry of another effect than the preset: not drawn, no crash */
    render(&t, GRAN_PRESET_GLASS_BELL, 1, prm);

    t.frozen = 0; t.clipping_alert = 0; t.looper_state = 0;
    t.effect = AND_FX_CHOIR; render(&t, GRAN_PRESET_CHOIR_ANGELS, 0, prm);
    CHECK(count_colour(AND_AURORA, 60, 40, 180, 150) > 8, "CHOIR: the voices are drawn as planets in the effect's colour");
    t.effect = AND_FX_FRACTURE; t.seq_mask = 0xFFFF; render(&t, GRAN_PRESET_FRACT_CLOCK, 0, prm);
    CHECK(count_colour(AND_MAGENTA, 60, 36, 180, 150) > 60, "FRACTURE: the steps that played are lit on the ring");
    t.seq_mask = 0; render(&t, GRAN_PRESET_FRACT_CLOCK, 0, prm);
    CHECK(count_colour(AND_STARLIGHT, 60, 30, 180, 150) >= 16, "FRACTURE: the step the clock is on is marked");
    t.effect = AND_FX_CONSTELLATION; render(&t, GRAN_PRESET_CONST_STARDUST, 0, prm);
    CHECK(count_colour(AND_SOLAR, 60, 36, 180, 150) > 60, "CONSTELLATION: the steps that play are stars on the ring");

    gran_telemetry_t empty; memset(&empty, 0, sizeof empty);
    render(&empty, GRAN_PRESET_GLASS_CRYSTAL, 3, NULL);
    render(NULL, GRAN_PRESET_ACC_MIRROR, 1, prm);
    CHECK(s_violations == 0, "empty telemetry, missing telemetry, a mismatched effect and missing params draw safely");

    printf("[chain] the signal chain page: every type, every focus, blocks on and off, every slot, EMPTY slots\n");
    { and_chain_view_t v; memset(&v, 0, sizeof v); int worst = 0; s_violations = 0;
      for (int ty = 0; ty < 24; ty++) for (int f = 0; f < 4; f++) for (int on = 0; on < 8; on++) for (int slot = 1; slot <= 20; slot += 7) {
          v.slot_no = (uint8_t)slot; v.name = (slot % 2) ? "13 USER SLOT NAME" : "EMPTY"; v.type = (uint8_t)ty; v.focus = (uint8_t)f;
          v.on[0] = on & 1; v.on[1] = (on >> 1) & 1; v.on[2] = (on >> 2) & 1; v.on[3] = (on >> 1) & 1; v.shimmer_interval = (uint8_t)(on & 3); for (int q = 0; q < 20; q++) v.slot_fx[q] = (uint8_t)((q + ty) % 6); v.layer_material = (uint8_t)(ty % 6); v.effect_material = (uint8_t)((ty + 1) % 6);
          v.level[0] = 1048576; v.level[1] = 838860 * (on & 1); v.level[2] = 2200000 /* out of range on purpose */; v.level[3] = 700000; v.empty_mask = 0xFF000u;
          s_cmds = 0; ui_cosmic_render_chain(&v); if (s_cmds > worst) worst = s_cmds;
      }
      CHECK(s_violations == 0, "the chain page never draws outside the panel (%d violations)", s_violations);
      CHECK(worst <= CMD_BUDGET, "busiest chain frame: %d commands (budget %d)", worst, CMD_BUDGET);
      ui_cosmic_render_chain(NULL);
      CHECK(s_violations == 0, "no view draws safely");
      s_str_hits = 0; v.slot_no = 7; v.name = "07 DRIFT"; v.type = 6; v.focus = 1; v.on[0] = v.on[1] = v.on[2] = 1; ui_cosmic_render_chain(&v);
      int has_title = 0, has_numbered = 0, has_effect = 0, has_hint = 0;
      for (int i = 0; i < s_str_hits && i < 64; i++) { if (!strcmp(s_last_strs[i], "DRIFT")) has_title = 1; if (!strcmp(s_last_strs[i], "07 DRIFT")) has_numbered = 1; if (!strcmp(s_last_strs[i], "BLOOM")) has_effect = 1; if (!strcmp(s_last_strs[i], "VOL: MATERIAL")) has_hint = 1; }
      CHECK(has_title && !has_numbered, "the title drops the number the badge already shows");
      CHECK(has_effect, "type 6 reads BLOOM on the chain page");
      CHECK(has_hint, "the hint names what VOL- / VOL+ do on the focused block"); }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL HOME SCREEN TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
