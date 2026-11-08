/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* custom-fw/tools/render_screens.c - render the Andromeda home screen to 240x240 BMP files
 *
 * Uses the firmware's own ui_cosmic.c and 5x7 font (ui_5x7.c), so the pictures are the pixels the
 * panel shows. One screen per bank plus a FROZEN and a clipping state.
 *
 *   clang -O1 -Icustom-fw/include -DPA_HOST_SIM=1 -DPA_PRODUCT_ANDROMEDA=1 \
 *         custom-fw/tools/render_screens.c custom-fw/src/board/ui_cosmic.c custom-fw/src/board/ui_5x7.c \
 *         -o render_screens.exe
 *   render_screens.exe <out dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ui_cosmic.h"

static uint16_t s_fb[240 * 240];

void st7735s_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565) {
    for (uint32_t j = y; j < (uint32_t)y + h; j++)
        for (uint32_t i = x; i < (uint32_t)x + w; i++)
            if (i < 240u && j < 240u) s_fb[j * 240u + i] = rgb565;
}

uint32_t plat_ms(void) {
    static uint32_t s_ms;
    return s_ms += 33u;
}

static void save_bmp(const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const uint32_t w = 240, h = 240, row = w * 3u, size = 54u + row * h;
    unsigned char hd[54] = { 'B', 'M' };
    hd[2] = size & 255; hd[3] = (size >> 8) & 255; hd[4] = (size >> 16) & 255; hd[5] = (size >> 24) & 255;
    hd[10] = 54; hd[14] = 40; hd[18] = w & 255; hd[19] = w >> 8; hd[22] = h & 255; hd[23] = h >> 8; hd[26] = 1; hd[28] = 24;
    fwrite(hd, 1, 54, f);
    unsigned char line[240 * 3];
    for (int y = 239; y >= 0; y--) {
        for (int x = 0; x < 240; x++) {
            const uint16_t c = s_fb[y * 240 + x];
            line[x * 3 + 2] = (unsigned char)(((c >> 11) & 0x1F) << 3);
            line[x * 3 + 1] = (unsigned char)(((c >> 5) & 0x3F) << 2);
            line[x * 3 + 0] = (unsigned char)((c & 0x1F) << 3);
        }
        fwrite(line, 1, sizeof line, f);
    }
    fclose(f);
}

/* a plausible tape: phrases with attacks and decays */
static void fill_tape(gran_telemetry_t *t, unsigned seed, int loud) {
    for (unsigned c = 0; c < GRAN_TAPE_COLS; c++) {
        unsigned ph = (c + seed) % 12u;
        int v = (ph < 2u) ? 230 : 230 - (int)ph * 18;
        v = v * loud / 100 + (int)((c * 37u + seed * 11u) % 23u);
        t->tape_env[c] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
}

static void grains(gran_telemetry_t *t, int n, int spread, int pitch_mode, int rev_every) {
    t->active_count = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        t->voices[i].norm_distance = (uint8_t)(20 + (i * 197 + spread * 13) % 225);
        t->voices[i].norm_pan      = (int8_t)(((i * 89 + spread * 31) % 255) - 128);
        t->voices[i].norm_size     = (uint8_t)(60 + (i * 53) % 190);
        t->voices[i].pitch_cents   = (int16_t)(pitch_mode == 0 ? ((i % 3) == 0 ? 1200 : 0)
                                             : pitch_mode == 1 ? 0
                                             : pitch_mode == 2 ? (i * 350) % 1900
                                             : ((i % 2) ? -1200 : 0));
        t->voices[i].reverse       = rev_every ? ((i % rev_every) == 0) : false;
    }
}

static void frame(int frames_before, const gran_telemetry_t *t, gran_preset_t p, uint8_t focus,
                  const int32_t params[AND_FOCUS_COUNT], uint8_t bpm) {
    for (int k = 0; k < frames_before; k++) ui_cosmic_render(t, p, focus, params, (uint8_t)(p + 1), NULL, 80);   /* advance the animation */
    ui_cosmic_render(t, p, focus, params, (uint8_t)(p + 1), NULL, 80);
    (void)bpm;
}

static void glass_modes(gran_telemetry_t *t, int strength) {
    for (unsigned k = 0; k < GLASS_TELEM_MODES; k++) {
        int v = strength - (int)k * 17 - (int)((k * 29u) % 23u);
        t->mode_lvl[k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    gran_telemetry_t t;
    int32_t prm[AND_FOCUS_COUNT] = { 0 };

    memset(&t, 0, sizeof t); t.effect = AND_FX_GLASS; glass_modes(&t, 255);
    prm[0] = 175000; prm[1] = 1048576; prm[2] = 1150000; prm[3] = 1470000; prm[4] = 1150000; prm[5] = 840000;
    frame(7, &t, GRAN_PRESET_GLASS_CRYSTAL, 0, prm, 110);
    save_bmp(dir, "01_glass_crystal.bmp");

    memset(&t, 0, sizeof t); t.effect = AND_FX_GLASS; glass_modes(&t, 190);
    prm[0] = 520000; prm[1] = 1300000; prm[2] = 1360000; prm[3] = 1260000; prm[4] = 1150000; prm[5] = 840000;
    frame(4, &t, GRAN_PRESET_GLASS_BELL, 2, prm, 120);
    save_bmp(dir, "02_glass_bell.bmp");

    memset(&t, 0, sizeof t); t.effect = AND_FX_BLOOM; grains(&t, 6, 1, 0, 5); fill_tape(&t, 3, 90);
    prm[0] = 940000; prm[1] = 1110000; prm[2] = 1260000; prm[3] = 630000; prm[4] = 1150000; prm[5] = 940000;
    frame(7, &t, GRAN_PRESET_BLOOM_GARDEN, 1, prm, 110);
    save_bmp(dir, "03_bloom_garden.bmp");

    memset(&t, 0, sizeof t); t.effect = AND_FX_BLOOM; grains(&t, 5, 5, 1, 0); fill_tape(&t, 9, 60); t.frozen = 1;
    prm[0] = 1260000; prm[1] = 700000; prm[2] = 1040000; prm[3] = 1470000; prm[4] = 1500000; prm[5] = 940000;
    frame(5, &t, GRAN_PRESET_BLOOM_DUSK, 4, prm, 72);
    save_bmp(dir, "04_bloom_frozen.bmp");

    memset(&t, 0, sizeof t); t.effect = AND_FX_ACCRETION; fill_tape(&t, 5, 80); t.looper_state = 3; t.looper_phase = 680; t.layers = 2; t.gr_pct = 12;
    prm[0] = 1680000; prm[1] = 1150000; prm[2] = 840000; prm[3] = 520000; prm[4] = 1150000; prm[5] = 630000;
    frame(3, &t, GRAN_PRESET_ACC_SOS, 0, prm, 95);
    save_bmp(dir, "05_accretion_dub.bmp");

    memset(&t, 0, sizeof t); t.effect = AND_FX_ACCRETION; fill_tape(&t, 2, 85); t.looper_state = 1; t.looper_phase = 240;
    prm[0] = 1470000; prm[1] = 940000; prm[2] = 1260000; prm[3] = 840000; prm[4] = 1150000; prm[5] = 630000;
    frame(2, &t, GRAN_PRESET_ACC_TAPE, 5, prm, 100);
    save_bmp(dir, "06_accretion_rec.bmp");

    memset(&t, 0, sizeof t); t.effect = AND_FX_CHOIR; t.voices_n = 3; t.voice_env[0] = 255; t.voice_env[1] = 180; t.voice_env[2] = 90; t.voice_semi[0] = 4; t.voice_semi[1] = 7; t.voice_semi[2] = 12; fill_tape(&t, 4, 85);
    prm[0] = 880000; prm[1] = 630000; prm[2] = 1680000; prm[3] = 520000; prm[4] = 1150000; prm[5] = 840000;
    frame(6, &t, GRAN_PRESET_CHOIR_ANGELS, 0, prm, 110);
    save_bmp(dir, "07_choir_angels.bmp");

    memset(&t, 0, sizeof t); t.effect = AND_FX_FRACTURE; grains(&t, 3, 2, 0, 3); fill_tape(&t, 8, 95); t.seq_pos = 5; t.seq_mask = 0x6DB6;
    prm[0] = 1048576; prm[1] = 940000; prm[2] = 420000; prm[3] = 1048576; prm[4] = 1150000; prm[5] = 420000;
    frame(5, &t, GRAN_PRESET_FRACT_CLOCK, 2, prm, 120);
    save_bmp(dir, "08_fracture_clock.bmp");

    memset(&t, 0, sizeof t); t.effect = AND_FX_CONSTELLATION; grains(&t, 3, 6, 0, 6); fill_tape(&t, 6, 90); t.seq_pos = 9;
    for (unsigned q = 0; q < 16; q++) t.seq_pitch[q] = (q % 3u == 1u) ? 0 : (uint8_t)(50 + (q * 53u) % 200u);
    prm[0] = 735000; prm[1] = 130000; prm[2] = 630000; prm[3] = 1260000; prm[4] = 1150000; prm[5] = 735000;
    frame(5, &t, GRAN_PRESET_CONST_STARDUST, 1, prm, 110);
    save_bmp(dir, "09_constellation_stardust.bmp");

    printf("9 screens written to %s\n", dir);
    return 0;
}
