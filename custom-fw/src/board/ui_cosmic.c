/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* custom-fw/src/board/ui_cosmic.c - the Andromeda home screen (240x240, ST7789)
 *
 *   y   0..25   header: bank badge (I..IV in the bank's colour), algorithm name
 *               (the battery / Bluetooth cluster is drawn by ui_app.c at the right); the beat dot and
 *               BPM sit in the radar's lower-right corner
 *   y  26..150  the radar: a starfield; a black hole at the centre ("now", the write head) with a
 *               swirling accretion disc in the bank's colours; three orbit paths; every live grain
 *               is a comet - distance from the core = how far back in the tape it reads, angle = pan
 *               (forward grains above the horizon, reverse grains below), colour = Doppler pitch
 *               (blueshift up, redshift down, starlight unison, magenta reverse), size = grain size.
 *               The outer orbit is the looper's accretion ring (red REC, green PLAY, amber DUB) with
 *               its playhead comet; the left edge is the grain-density gauge.
 *   y 151..173  the tape strip: the capture tape's envelope, oldest at the left fading to dust, the
 *               newest audio glowing in the bank colour; the write head line; a tick per grain.
 *   y 174..212  the four macro tiles (ORBIT WARP FLUX GRAV), the focused one in solar gold.
 *   y 214..240  the modulation lane: each bank has its own wave shape, animated -
 *               I chirp (a gravitational-wave chirp), II quanta (sample-and-hold steps),
 *               III arpeggio (16-step staircase with the playing step lit), IV spiral (a decaying
 *               sound-on-sound wave).
 *   Clipping: the whole screen edge pulses red / corona.
 *
 * Budget: the display list holds 1,372 commands a frame (ui_5x7.c); this screen uses ~500.
 * No RAM tables: everything constant is in flash (the RAM region has ~200 bytes free).
 */
#include "pa_config.h"
#if PA_PRODUCT_ANDROMEDA
#include <stdio.h>
#include <string.h>
#include "ui_cosmic.h"
#include "ui_cosmic_theme.h"

extern void st7735s_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565);
extern void ui_draw_str(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg);

const char* const g_cosmic_preset_names[GRAN_PRESET_COUNT] = {
    "GLASS CRYSTAL",  "GLASS BELL",    "GLASS KALIMBA",  "GLASS MARIMBA",
    "BLOOM GARDEN",   "BLOOM DUSK",    "BLOOM DRIFT",    "BLOOM RAW",
    "ACCRETE SOS",    "ACCRETE TAPE",  "ACCRETE DECAY",  "ACCRETE MIRROR",
    "CHOIR ANGELS",   "CHOIR MOURN",   "CHOIR DEEP",     "CHOIR INFINITE",
    "FRACT CLOCK",    "FRACT WATER",   "FRACT HEART",    "FRACT SKIP",
    "CONST STARDUST", "CONST GRAVITY", "CONST DANCE",    "CONST ORBIT"
};

const char* const g_cosmic_bank_names[AND_FX_COUNT] = {
    "GLASS", "BLOOM", "ACCRETION", "CHOIR", "FRACTURE", "CONSTELLATION"
};

/* "bank" in this file is the effect: 0 GLASS (violet), 1 BLOOM (ion blue), 2 ACCRETION (corona orange), 3 CHOIR (aurora green), 4 FRACTURE (magenta), 5 CONSTELLATION (solar yellow) */
#define FX_PRI(b)  ((b) == 0 ? AND_VIOLET : (b) == 1 ? AND_ION : (b) == 2 ? AND_CORONA : (b) == 3 ? AND_AURORA : (b) == 4 ? AND_MAGENTA : AND_SOLAR)
#define FX_SEC(b)  ((b) == 0 ? AND_ICE : (b) == 1 ? AND_MAGENTA : (b) == 2 ? AND_SOLAR : (b) == 3 ? AND_STARLIGHT : (b) == 4 ? AND_CORONA : AND_AURORA)
#define FX_NEB(b)  ((b) == 0 ? AND_RGB(0x34, 0x1C, 0x62) : (b) == 1 ? AND_RGB(0x12, 0x36, 0x56) : (b) == 2 ? AND_RGB(0x4A, 0x26, 0x10) : (b) == 3 ? AND_RGB(0x10, 0x44, 0x36) : (b) == 4 ? AND_RGB(0x4A, 0x16, 0x40) : AND_RGB(0x48, 0x34, 0x12))

static const char* const k_wave_name[AND_FX_COUNT] = { "RING", "GROW", "SPIRAL", "VOICES", "SLICE", "ARP" };
static const char* const k_axis_name[AND_FX_COUNT][4] = {
    { "MATERIAL", "TUNE",  "DECAY",  "BRIGHT"  },     /* GLASS         */
    { "SIZE",     "PITCH", "BLOOM",  "DRIFT"   },     /* BLOOM         */
    { "FDBK",     "TONE",  "WOBBLE", "DEGRADE" },     /* ACCRETION     */
    { "VOICING",  "DETUNE", "SPREAD", "ATTACK" },     /* CHOIR         */
    { "GRID",     "SLICE", "CHAOS",  "PITCH"   },     /* FRACTURE      */
    { "TEMPO",    "SCALE", "MOTION", "DENSITY" }      /* CONSTELLATION */
};
static const char* const k_voicing_name[6] = { "OCTAVE", "FIFTH", "MAJOR", "MINOR", "MASSIVE", "CLUSTER" };
static const char* const k_grid_name[5]    = { "1/4", "1/8", "1/16", "1/32", "1/64" };
static const char* const k_scale_name[8]   = { "MAJOR", "MINOR", "DORIAN", "MIXOLYD", "PENT +", "PENT -", "LYDIAN", "WHOLE" };
static const uint8_t k_slice_ms[11]        = { 5, 7, 11, 16, 24, 35, 52, 78, 115, 170, 250 };      /* 5 .. 250 ms, 50^(t) */
static const char* const k_material_name[6] = { "CRYSTAL", "BELL", "KALIMBA", "METAL", "WOOD", "MARIMBA" };

/* Dim companions for comet tails */
#define TAIL_UP      AND_RGB(0x1C, 0x60, 0x80)
#define TAIL_DOWN    AND_RGB(0x80, 0x44, 0x1C)
#define TAIL_UNISON  AND_STARDUST
#define TAIL_REVERSE AND_RGB(0x80, 0x2C, 0x6A)

/* sin(2*pi*i/256) * 127 */
static const int8_t k_sin256[256] = {
       0,    3,    6,    9,   12,   16,   19,   22,   25,   28,   31,   34,   37,   40,   43,   46,
      49,   51,   54,   57,   60,   63,   65,   68,   71,   73,   76,   78,   81,   83,   85,   88,
      90,   92,   94,   96,   98,  100,  102,  104,  106,  107,  109,  111,  112,  113,  115,  116,
     117,  118,  120,  121,  122,  122,  123,  124,  125,  125,  126,  126,  126,  127,  127,  127,
     127,  127,  127,  127,  126,  126,  126,  125,  125,  124,  123,  122,  122,  121,  120,  118,
     117,  116,  115,  113,  112,  111,  109,  107,  106,  104,  102,  100,   98,   96,   94,   92,
      90,   88,   85,   83,   81,   78,   76,   73,   71,   68,   65,   63,   60,   57,   54,   51,
      49,   46,   43,   40,   37,   34,   31,   28,   25,   22,   19,   16,   12,    9,    6,    3,
       0,   -3,   -6,   -9,  -12,  -16,  -19,  -22,  -25,  -28,  -31,  -34,  -37,  -40,  -43,  -46,
     -49,  -51,  -54,  -57,  -60,  -63,  -65,  -68,  -71,  -73,  -76,  -78,  -81,  -83,  -85,  -88,
     -90,  -92,  -94,  -96,  -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116,
    -117, -118, -120, -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127,
    -127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118,
    -117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100,  -98,  -96,  -94,  -92,
     -90,  -88,  -85,  -83,  -81,  -78,  -76,  -73,  -71,  -68,  -65,  -63,  -60,  -57,  -54,  -51,
     -49,  -46,  -43,  -40,  -37,  -34,  -31,  -28,  -25,  -22,  -19,  -16,  -12,   -9,   -6,   -3,
};
#define SIN8(i) ((int32_t)k_sin256[(uint8_t)(i)])
#define COS8(i) ((int32_t)k_sin256[(uint8_t)((i) + 64u)])

#define STAR_N 46u
static const uint8_t k_stars[STAR_N][3] = {   /* x, y, brightness 0..2 */
    {131,123,1}, { 40, 38,1}, { 31, 52,0}, {153,143,0}, { 23, 38,1}, { 73,102,1},
    {149,132,1}, { 75, 29,0}, {207,133,0}, { 39, 70,0}, {182, 52,1}, {220,102,0},
    {103, 75,0}, {210,146,1}, { 53, 59,0}, {140,106,2}, {210, 77,2}, { 64, 39,2},
    {133, 71,0}, { 23,121,0}, { 82,125,1}, {155, 82,0}, { 52,145,0}, {185, 31,1},
    { 17, 75,0}, {214, 38,1}, { 95,119,2}, {  8,119,1}, {171, 81,0}, { 54, 90,0},
    {228,134,2}, {215,115,0}, {150, 60,1}, {208,105,1}, {176,119,0}, {199,115,0},
    {129, 76,0}, {193, 88,1}, {142, 87,1}, {161, 30,0}, { 25, 83,1}, {183,109,2},
    {187, 78,0}, { 50, 67,0}, {  5,109,0}, {194,147,0},
};


extern uint32_t plat_ms(void) __attribute__((weak));
static uint32_t s_frame;          /* advances at ~30 Hz: drives the swirl, the twinkle and the waves */
static uint32_t s_spin;           /* the accretion disc's angle: the sum of (frame steps x its speed). The speed follows a knob, so the angle must be integrated: frame x speed jumped by frame x change */
static uint32_t s_spin_k = 2u;    /* the disc's current speed, per frame step (set by the render from its knob) */

static void cosmic_step_frame(void) {
    static uint32_t s_last_ms;
    static uint32_t s_fallback_ms;
    const uint32_t now = (&plat_ms != NULL) ? plat_ms() : (s_fallback_ms += 33u);
    if (!s_last_ms) {
        s_last_ms = now;
        s_frame++; s_spin += s_spin_k;
        return;
    }
    const uint32_t dt = (uint32_t)(now - s_last_ms);
    if (dt >= 33u) {
        uint32_t steps = dt / 33u;
        if (steps > 2u) steps = 2u;
        s_frame += steps; s_spin += steps * s_spin_k;
        s_last_ms += steps * 33u;
        if ((uint32_t)(now - s_last_ms) > 100u) s_last_ms = now;
    }
}

/* ---- primitives ------------------------------------------------------------------------------ */
/* Every rectangle goes through here: clipped to the panel, so no command ever leaves the screen. */
static void put(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 240) w = 240 - x;
    if (y + h > 240) h = 240 - y;
    if (w <= 0 || h <= 0) return;
    st7735s_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, c);
}

/* A panel with one-pixel cut corners (two rectangles) */
static void panel(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) {
    put(x + 1, y, w - 2, h, c);
    put(x, y + 1, w, h - 2, c);
}

static void outline(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) {
    put(x + 1, y, w - 2, 1, c);
    put(x + 1, y + h - 1, w - 2, 1, c);
    put(x, y + 1, 1, h - 2, c);
    put(x + w - 1, y + 1, 1, h - 2, c);
}

static uint32_t str_w(const char *s) { uint32_t n = 0; while (s[n]) n++; return n ? n * 6u - 1u : 0u; }

static void text_center(int32_t cx, int32_t y, const char *s, uint16_t fg, uint16_t bg) {
    ui_draw_str((uint16_t)(cx - (int32_t)str_w(s) / 2), (uint16_t)y, s, fg, bg);
}

static void u_to_str(char *dst, uint32_t v, const char *suffix) {
    char tmp[10]; int n = 0;
    do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 10);
    while (n) *dst++ = tmp[--n];
    while (*suffix) *dst++ = *suffix++;
    *dst = 0;
}

/* ---- the radar ------------------------------------------------------------------------------- */
#define CX 120
#define CY 90

static void ring(int32_t r, uint32_t n, uint32_t rot, int32_t dot, uint16_t c) {
    for (uint32_t k = 0; k < n; k++) {
        uint32_t a = (k * 256u) / n + rot;
        put(CX + (r * COS8(a)) / 127 - dot / 2, CY + (r * SIN8(a)) / 127 - dot / 2, dot, dot, c);
    }
}

static void starfield(void) {
    for (uint32_t i = 0; i < STAR_N; i++) {
        uint32_t lvl = k_stars[i][2];
        /* twinkle: each frame a few stars step up a level, a different few each time */
        if (((i * 7u + s_frame) % 23u) == 0u && lvl < 2u) lvl++;
        uint16_t c = (lvl == 0u) ? AND_DUST : (lvl == 1u) ? AND_STARDUST : AND_HAZE;
        put(k_stars[i][0], k_stars[i][1], 1, 1, c);
        if (lvl == 2u) {                                   /* the brightest get a four-point glint */
            put(k_stars[i][0] - 1, k_stars[i][1], 3, 1, AND_STARDUST);
            put(k_stars[i][0], k_stars[i][1] - 1, 1, 3, AND_STARDUST);
            put(k_stars[i][0], k_stars[i][1], 1, 1, AND_STARLIGHT);
        }
    }
}

/* Nebula wisps: a curved band of dim, bank-tinted dust across the radar - a galaxy seen edge-on.
 * It drifts one step every 8 frames, so the background is never quite still. */
static void nebula(uint8_t bank) {
    const uint16_t c = FX_NEB(bank);
    const uint32_t drift = s_frame / 8u;
    for (uint32_t k = 0; k < 34u; k++) {
        const uint32_t h = (k * 2654435761u) >> 24;                 /* 0..255, fixed per speck */
        const int32_t x = 10 + (int32_t)((k * 220u) / 34u) + (int32_t)(h & 7u);
        const int32_t band = (SIN8(k * 6u + drift) * 26) / 127;    /* the band curves through the radar */
        const int32_t y = 92 - (x - 120) / 3 + band + (int32_t)((h >> 3) & 15u) - 8;
        const int32_t w = 1 + (int32_t)((h >> 5) & 1u);
        if (y > 28 && y < 148) put(x, y, w * 2, w, c);
    }
}

static void black_hole(uint8_t bank, int32_t orbit_q21, int frozen) {
    const uint16_t p = frozen ? AND_ICE : FX_PRI(bank);
    const uint16_t q = frozen ? AND_STARLIGHT : FX_SEC(bank);
    /* the accretion disc turns faster when ORBIT (density) is up; it stops when the tape is frozen */
    s_spin_k = 2u + (uint32_t)(orbit_q21 >> 19);           /* the speed for the coming frame steps; the angle itself only ever moves forward */
    const uint32_t spin = frozen ? 0u : s_spin;
    for (uint32_t k = 0; k < 20u; k++) {
        uint32_t a = (k * 256u) / 20u + spin;
        uint16_t c = (k & 3u) == 0u ? q : (k & 1u) ? p : AND_DUST;
        put(CX + (11 * COS8(a)) / 127, CY + (11 * SIN8(a)) / 127, 1, 1, c);
    }
    ring(7, 16, 0, 1, q);                                  /* the photon ring */
    /* the centre stays dark: the event horizon is "now", the write head */
}

static void comet(const gran_telemetry_t *t, int i, uint8_t bank) {
    (void)bank;
    const int32_t r = 13 + ((int32_t)t->voices[i].norm_distance * 45) / 255;
    const int32_t pan = (int32_t)t->voices[i].norm_pan + 128;               /* 0..255, left..right */
    const int rev = t->voices[i].reverse ? 1 : 0;
    /* forward grains above the horizon (left -> top -> right), reverse grains below it */
    const uint32_t a = rev ? (uint32_t)(128 - pan / 2) : (uint32_t)(128 + pan / 2);
    const int32_t cents = t->voices[i].pitch_cents;
    uint16_t head, tail;
    if (rev)               { head = AND_REVERSE;      tail = TAIL_REVERSE; }
    else if (cents > 50)   { head = AND_PITCH_UP;     tail = TAIL_UP; }
    else if (cents < -50)  { head = AND_PITCH_DOWN;   tail = TAIL_DOWN; }
    else                   { head = AND_PITCH_UNISON; tail = TAIL_UNISON; }
    const int32_t sz = (t->voices[i].norm_size > 150) ? 3 : 2;
    const int32_t x = CX + (r * COS8(a)) / 127, y = CY + (r * SIN8(a)) / 127;
    /* the tail points back toward the core: where the grain left "now" */
    put(CX + ((r - 5) * COS8(a)) / 127, CY + ((r - 5) * SIN8(a)) / 127, 1, 1, tail);
    put(CX + ((r - 9) * COS8(a)) / 127, CY + ((r - 9) * SIN8(a)) / 127, 1, 1, AND_DUST);
    put(x - sz / 2, y - sz / 2, sz, sz, head);
    if (rev && sz == 3) put(x, y, 1, 1, AND_VOID);          /* reverse grains are hollow */
}

static void looper_ring(const gran_telemetry_t *t) {
    uint16_t c = AND_ORBIT; int32_t dot = 1;
    if (t->looper_state == 1u)      { c = AND_LOOP_REC;  dot = 2; }
    else if (t->looper_state == 2u) { c = AND_LOOP_PLAY; dot = 2; }
    else if (t->looper_state == 3u) { c = AND_LOOP_DUB;  dot = 2; }
    if (t->frozen) c = AND_FROZEN;
    ring(60, 48, 0, dot, c);
    if (t->looper_state != 0u) {
        const uint32_t a = (uint32_t)(t->looper_phase >> 2) + 192u;           /* 0 at the top, clockwise */
        put(CX + (60 * COS8(a - 6u)) / 127, CY + (60 * SIN8(a - 6u)) / 127, 1, 1, c);
        put(CX + (60 * COS8(a - 3u)) / 127 - 1, CY + (60 * SIN8(a - 3u)) / 127 - 1, 2, 2, c);
        put(CX + (60 * COS8(a)) / 127 - 1, CY + (60 * SIN8(a)) / 127 - 1, 3, 3, AND_STARLIGHT);
    }
}

static void density_gauge(const gran_telemetry_t *t, uint8_t bank) {
    /* 16 segments up the left edge: active grains / max voices */
    const uint32_t lit = ((uint32_t)t->active_count * 16u + GRANULAR_MAX_VOICES - 1u) / GRANULAR_MAX_VOICES;
    for (uint32_t k = 0; k < 16u; k++) {
        const int32_t y = 143 - (int32_t)k * 7;
        uint16_t c = (k < lit) ? ((k + 1u == lit) ? FX_SEC(bank) : FX_PRI(bank)) : AND_DUST;
        put(3, y, 4, 5, c);
    }
}

/* ---- the strip under the radar: ACCRETION's tape, BLOOM's history, GLASS's twelve modes ----------------------------------------------------- */
#define TAPE_X0  30
#define TAPE_X1  222                                     /* the write head */
#define TAPE_MID 162

static void tape_strip(const gran_telemetry_t *t, uint8_t bank) {
    const char *label = "HIST";
    uint16_t lc = AND_HAZE, frame = AND_DUST;
    if (bank == 2u) {
        label = "TAPE";
        if (t->looper_state == 1u)      { label = "REC";  lc = AND_LOOP_REC; }
        else if (t->looper_state == 2u) { label = "PLAY"; lc = AND_LOOP_PLAY; }
        else if (t->looper_state == 3u) { label = "DUB";  lc = AND_LOOP_DUB; }
        if (t->looper_state != 0u) frame = lc;
    }
    if (t->frozen) { label = "ICE"; lc = AND_FROZEN; frame = AND_FROZEN; }
    ui_draw_str(3, TAPE_MID - 3, label, lc, AND_VOID);
    put(TAPE_X0, 152, TAPE_X1 - TAPE_X0, 1, frame);
    put(TAPE_X0, 172, TAPE_X1 - TAPE_X0, 1, frame);
    for (uint32_t c = 0; c < GRAN_TAPE_COLS; c++) {
        int32_t h = ((int32_t)t->tape_env[c] * 9) / 255;
        /* the past fades to dust; the newest third glows in the effect's colour */
        uint16_t col = (c < 16u) ? AND_STARDUST : (c < 32u) ? AND_HAZE : (t->frozen ? AND_ICE : FX_PRI(bank));
        if (bank == 2u) col = (c * 4u < ((uint32_t)t->looper_phase * GRAN_TAPE_COLS) / 256u) ? FX_PRI(bank) : AND_HAZE;   /* ACCRETION: played so far glows */
        put(TAPE_X0 + (int32_t)c * 4, TAPE_MID - h, 3, 2 * h + 1, col);
    }
    if (bank != 2u) {
        put(TAPE_X1, 153, 2, 19, t->frozen ? AND_ICE : AND_STARLIGHT);          /* the history's head */
        for (int i = 0; i < t->active_count && i < (int)GRANULAR_MAX_VOICES; i++) {
            const int32_t x = TAPE_X1 - 2 - ((int32_t)t->voices[i].norm_distance * (TAPE_X1 - TAPE_X0 - 2)) / 255;
            const int32_t cents = t->voices[i].pitch_cents;
            uint16_t c = t->voices[i].reverse ? AND_REVERSE : (cents > 50) ? AND_PITCH_UP : (cents < -50) ? AND_PITCH_DOWN : AND_PITCH_UNISON;
            put(x, 153, 1, 3, c);
        }
    } else if (bank == 2u && t->looper_state != 0u) {
        put(TAPE_X0 + ((int32_t)t->looper_phase * (TAPE_X1 - TAPE_X0 - 2)) / 1024, 153, 2, 19, AND_STARLIGHT);   /* ACCRETION: the play head */
    }
}

/* GLASS: how loud each of the twelve resonator modes rings now, as twelve bars (low modes on the left) */
static void mode_strip(const gran_telemetry_t *t) {
    ui_draw_str(3, TAPE_MID - 3, "MODES", AND_HAZE, AND_VOID);
    put(TAPE_X0 + 12, 152, TAPE_X1 - TAPE_X0 - 12, 1, AND_DUST);
    put(TAPE_X0 + 12, 172, TAPE_X1 - TAPE_X0 - 12, 1, AND_DUST);
    for (uint32_t k = 0; k < GLASS_TELEM_MODES; k++) {
        const int32_t h = ((int32_t)t->mode_lvl[k] * 9) / 255;
        put(TAPE_X0 + 18 + (int32_t)k * 15, TAPE_MID - h, 11, 2 * h + 1, k < 3u ? AND_ICE : (k < 7u ? FX_PRI(0) : AND_STARDUST));
    }
}

/* GLASS's radar: the twelve modes as planets on a golden-angle spiral, low modes near the core; the size is how loud each rings, a halo marks the ringing ones */
static void glass_radar(const gran_telemetry_t *t) {
    for (uint32_t k = 0; k < GLASS_TELEM_MODES; k++) {
        const uint32_t a = k * 70u + 20u;
        const int32_t r = 15 + ((int32_t)k * 44) / 11;
        const int32_t lvl = t->mode_lvl[k];
        const int32_t sz = 1 + (lvl * 4) / 255;
        const int32_t x = CX + (r * COS8(a)) / 127, y = CY + (r * SIN8(a)) / 127;
        if (lvl > 60) { outline(x - sz - 2, y - sz - 2, 2 * sz + 4, 2 * sz + 4, AND_DUST); }
        put(x - sz / 2, y - sz / 2, sz, sz, lvl > 200 ? AND_STARLIGHT : (k % 3u ? FX_PRI(0) : AND_ICE));
    }
}

/* CHOIR's radar: the voices as planets, the further from the core the further the interval from the note; the size is the swell */
static void choir_radar(const gran_telemetry_t *t) {
    for (uint32_t i = 0; i < t->voices_n && i < 4u; i++) {
        const int32_t semi = t->voice_semi[i] < 0 ? -t->voice_semi[i] : t->voice_semi[i];
        const int32_t r = 16 + semi * 2;
        const uint32_t a = i * 64u + s_frame * (1u + i) / 2u;
        const int32_t sz = 2 + ((int32_t)t->voice_env[i] * 4) / 255;
        const int32_t x = CX + (r * COS8(a)) / 127, y = CY + (r * SIN8(a)) / 127;
        ring(r, 28u + (uint32_t)semi, 0, 1, AND_DUST);
        if (t->voice_env[i] > 60) outline(x - sz - 2, y - sz - 2, 2 * sz + 4, 2 * sz + 4, AND_DUST);
        put(x - sz / 2, y - sz / 2, sz, sz, t->voice_env[i] > 200 ? AND_STARLIGHT : (t->voice_semi[i] < 0 ? AND_PITCH_DOWN : AND_AURORA));
    }
}

/* FRACTURE and CONSTELLATION: the sixteen steps as a ring, the step the clock is on marked. FRACTURE shows which of the last sixteen steps played, CONSTELLATION how high each step sings */
static void step_ring(const gran_telemetry_t *t, uint8_t bank) {
    for (uint32_t i = 0; i < 16u; i++) {
        const uint32_t a = i * 16u + 192u;                                       /* step 0 at the top, clockwise */
        int32_t r = 52; uint16_t c = AND_DUST; int32_t sz = 2;
        if (bank == 4u) { if ((t->seq_mask >> i) & 1u) { c = FX_PRI(bank); sz = 3; } }
        else if (t->seq_pitch[i]) { r = 34 + ((int32_t)t->seq_pitch[i] * 24) / 255; c = FX_PRI(bank); sz = 3; }
        put(CX + (r * COS8(a)) / 127 - sz / 2, CY + (r * SIN8(a)) / 127 - sz / 2, sz, sz, c);
    }
    const uint32_t cur = (uint32_t)t->seq_pos * 16u + 192u;
    put(CX + (58 * COS8(cur)) / 127 - 2, CY + (58 * SIN8(cur)) / 127 - 2, 4, 4, AND_STARLIGHT);
}

/* ACCRETION's radar: one ring per layer, newest outermost and brightest; GR shows the limiter */
static void acc_layers(const gran_telemetry_t *t) {
    static const int32_t k_r[5] = { 22, 31, 40, 49, 56 };
    uint32_t n = (uint32_t)t->layers + (t->looper_state != 0u ? 1u : 0u);
    if (n > 5u) n = 5u;
    for (uint32_t i = 0; i < n; i++) ring(k_r[i], 24u + i * 6u, (uint32_t)(s_frame * (i + 1u) / 2u), 1, i + 1u == n ? AND_CORONA : AND_ORBIT);
}

/* ---- the macro tiles ------------------------------------------------------------------------- */
/* kind 0: the effect's own tiles (value texts depend on the effect); 1: plain percentages (SPACE); 2: SHIMMER (tile 1 is the interval). `names` and `col` override the effect's labels and colour. */
static const char* const k_shimmer_interval[4] = { "+12", "+7", "+12 +7", "+19" };
static uint8_t s_editing;                       /* the frame being drawn shows a hovered value being edited (a block's page, after the PARA click): the focused tile gets a double frame */
static void macro_tiles(const int32_t params[], uint8_t focus, uint8_t bank, const char *const *names, uint8_t kind, uint16_t col) {
    const uint16_t pc = col ? col : FX_PRI(bank);
    for (uint32_t i = 0; i < 4u; i++) {
        const int32_t x = 4 + (int32_t)i * 58, y = 175, w = 54, h = 37;
        const int f = (i == focus);
        const uint16_t bg = f ? AND_NEBULA_HI : AND_NEBULA;
        panel(x, y, w, h, bg);
        outline(x, y, w, h, f ? AND_SOLAR : AND_DUST);
        if (f && s_editing) outline(x + 1, y + 1, w - 2, h - 2, AND_SOLAR);
        text_center(x + w / 2, y + 5, names ? names[i] : k_axis_name[bank][i], f ? AND_SOLAR : AND_HAZE, bg);
        int32_t v = params ? params[i] : 1048576;
        if (v < 0) v = 0;
        if (v > 2097152) v = 2097152;
        const uint32_t lit = (uint32_t)(((int64_t)v * 10 + 1048576) >> 21);
        for (uint32_t k = 0; k < 10u; k++)
            put(x + 7 + (int32_t)k * 4, y + 16, 3, 6, (k < lit) ? (f ? AND_SOLAR : pc) : AND_DUST);
        char buf[12];
        const char *txt = buf;
        if (kind == 2u && i == 1u) {                                        /* SHIMMER INTERVAL */
            int32_t m = (int32_t)(((int64_t)v * 4) >> 21); if (m > 3) m = 3;
            txt = k_shimmer_interval[m];
        } else if (kind != 0u) {                                            /* SPACE and SHIMMER: plain percentages */
            u_to_str(buf, (uint32_t)(((int64_t)v * 100 + 1048576) >> 21), "%");
        } else if (bank == 0u && i == 0u) {                                   /* GLASS MATERIAL: the name */
            int32_t m = (int32_t)(((int64_t)v * 6) >> 21); if (m > 5) m = 5;
            txt = k_material_name[m];
        } else if ((bank == 0u && i == 1u) || (bank == 1u && i == 1u)) {   /* TUNE / PITCH: semitones */
            const int32_t st = (bank == 0u) ? (int32_t)(((int64_t)(v - 1048576) * 24 + (v >= 1048576 ? 1048576 : -1048576)) >> 21)
                                            : (int32_t)(((int64_t)v * 36 + 1048576) >> 21) - 12;
            char *d = buf; uint32_t a = (uint32_t)(st < 0 ? -st : st);
            *d++ = st < 0 ? '-' : '+'; u_to_str(d, a, " st");
        } else if (bank == 3u && i == 0u) {                                 /* CHOIR VOICING */
            int32_t m = (int32_t)(((int64_t)v * 6) >> 21); if (m > 5) m = 5;
            txt = k_voicing_name[m];
        } else if (bank == 3u && i == 1u) {                                 /* CHOIR DETUNE in cents */
            u_to_str(buf, (uint32_t)(((int64_t)v * 25 + 1048576) >> 21), " CT");
        } else if (bank == 4u && i == 0u) {                                 /* FRACTURE GRID */
            int32_t m = (int32_t)(((int64_t)v * 5) >> 21); if (m > 4) m = 4;
            txt = k_grid_name[m];
        } else if (bank == 4u && i == 1u) {                                 /* FRACTURE SLICE in ms */
            u_to_str(buf, k_slice_ms[(int32_t)(((int64_t)v * 10 + 1048576) >> 21)], " MS");
        } else if (bank == 4u && i == 3u) {                                 /* FRACTURE PITCH in semitones */
            const int32_t st = (int32_t)(((int64_t)(v - 1048576) * 48 + (v >= 1048576 ? 1048576 : -1048576)) >> 21);
            char *d = buf; uint32_t a2 = (uint32_t)(st < 0 ? -st : st);
            *d++ = st < 0 ? '-' : '+'; u_to_str(d, a2, " ST");
        } else if (bank == 5u && i == 0u) {                                 /* CONSTELLATION TEMPO */
            u_to_str(buf, 40u + (uint32_t)(((int64_t)v * 200 + 1048576) >> 21), " BPM");
        } else if (bank == 5u && i == 1u) {                                 /* CONSTELLATION SCALE */
            int32_t m = (int32_t)(((int64_t)v * 8) >> 21); if (m > 7) m = 7;
            txt = k_scale_name[m];
        } else {
            u_to_str(buf, (uint32_t)(((int64_t)v * 100 + 1048576) >> 21), "%");
        }
        text_center(x + w / 2, y + 26, txt, f ? AND_STARLIGHT : AND_HAZE, bg);
    }
}

/* ---- the bottom lane: the effect's wave, then MIX and SPACE ------------------------------------------------------------------------------- */
#define WAVE_X0 46
#define WAVE_N  26
#define WAVE_CY 227

static void mix_bar(int32_t y, const char *name, int32_t v, int focused, uint16_t col) {
    if (v < 0) v = 0;
    if (v > 2097152) v = 2097152;
    ui_draw_str(128, y, name, focused ? AND_SOLAR : AND_HAZE, AND_NEBULA);
    if (focused && s_editing) outline(125, y - 2, 118, 11, AND_SOLAR);           /* editing: the bar is framed */
    const uint32_t lit = (uint32_t)(((int64_t)v * 14 + 1048576) >> 21);
    for (uint32_t k = 0; k < 14u; k++) put(168 + (int32_t)k * 5, y + 1, 4, 6, (k < lit) ? (focused ? AND_SOLAR : col) : AND_DUST);
}

static void mod_lane(const int32_t params[], uint8_t bank, uint8_t focus) {
    put(0, 214, 240, 26, AND_NEBULA);
    put(0, 214, 240, 1, AND_DUST);
    ui_draw_str(4, 224, k_wave_name[bank], FX_SEC(bank), AND_NEBULA);
    const uint16_t p = FX_PRI(bank);
    const int32_t slow = params ? params[2] >> 18 : 4;          /* 0..8 */
    for (int32_t k = 0; k < WAVE_N; k++) {
        const int32_t x = WAVE_X0 + k * 3;
        int32_t y = WAVE_CY;
        uint16_t c = p;
        const uint32_t a = (uint32_t)(k * 17) - s_frame * 5u;
        switch (bank) {
        case 0: {   /* GLASS: a struck ring dying away, the strike at the left */
            const int32_t amp = 10 - (k * (6 + (8 - slow) / 2)) / WAVE_N;
            y = WAVE_CY - (SIN8(a) * (amp > 1 ? amp : 1)) / 127;
            if (k == 0) c = AND_STARLIGHT;
            break; }
        case 1: {   /* BLOOM: a swell that grows and lets go */
            const int32_t env = k < WAVE_N / 2 ? 2 + (k * 8) / (WAVE_N / 2) : 10 - ((k - WAVE_N / 2) * 8) / (WAVE_N / 2);
            y = WAVE_CY - (SIN8(a) * env) / 127;
            break; }
        case 2: {   /* ACCRETION: a sound-on-sound wave decaying into the past */
            const int32_t amp = 10 - (k * (8 - slow / 2)) / WAVE_N;
            y = WAVE_CY - (SIN8(a) * (amp > 1 ? amp : 1)) / 127;
            break; }
        case 3: {   /* CHOIR: three voices a fifth and an octave apart, beating */
            y = WAVE_CY - ((SIN8(a) + SIN8(a * 3u / 2u + 20u) + SIN8(a * 2u + 90u)) * 4) / 127 / 1;
            break; }
        case 4: {   /* FRACTURE: slices of the wave, with gaps, one repeated */
            const uint32_t blk = (uint32_t)k / 4u + s_frame / 3u;
            const int on = (blk % 4u) != 2u;
            y = on ? WAVE_CY - (SIN8(a * 2u) * 8) / 127 : WAVE_CY;
            if (!on) c = AND_DUST;
            break; }
        default: {  /* CONSTELLATION: a stepped line climbing a scale, the playing step lit */
            static const uint8_t k_step[8] = { 0, 2, 4, 5, 7, 9, 7, 4 };
            const uint32_t st = (uint32_t)k / 3u;
            y = WAVE_CY + 6 - (int32_t)k_step[st & 7u];
            if ((st & 7u) == (s_frame / 4u) % 8u) { c = AND_STARLIGHT; }
            break; }
        }
        put(x, y - 1, 2, 2, c);
    }
    if (focus >= AND_FOCUS_LAYER && focus < AND_FOCUS_SPACE) mix_bar(217, "LAYER", params ? params[10] : 0, focus == 10u, FX_PRI(0));     /* editing the layer: its level takes the MIX bar's place */
    else                          mix_bar(217, "MIX",   params ? params[4] : 1048576, focus == 4u, FX_PRI(bank));
    mix_bar(228, "SPACE", params ? params[5] : 1048576, focus == 5u || focus == 14u || focus == 18u, AND_VIOLET);
}

/* a slot's title: its stored name without the leading "NN " (the badge shows the number); the factory names when there is none */
static const char *slot_title(const char *name, gran_preset_t type) {
    if (!name || !name[0]) return g_cosmic_preset_names[(uint32_t)type % GRAN_PRESET_COUNT];
    if (name[0] >= '0' && name[0] <= '9' && name[1] >= '0' && name[1] <= '9' && name[2] == ' ') return name + 3;
    return name;
}

/* ---- the signal chain page ---------------------------------------------------------------------------------------------------------------------- */
static const char* const k_variant_name[AND_FX_COUNT][4] = {
    { "CRYSTAL", "BELL",   "KALIMBA", "MARIMBA" },
    { "MAJOR",   "MINOR",  "PENTA",   "FREE"    },
    { "SOS",     "TAPE",   "DECAY",   "MIRROR"  },
    { "ANGELS",  "MOURN",  "DEEP",    "INFINIT" },
    { "CLOCK",   "WATER",  "HEART",   "SKIP"    },
    { "STARDST", "GRAVITY", "DANCE", "ORBIT"   }
};
static const char* const k_chain_fx_name[AND_FX_COUNT] = { "GLASS", "BLOOM", "ACCRETE", "CHOIR", "FRACT", "CONSTEL" };
static const char* const k_shimmer_name[4] = { "+12", "+7", "+12 +7", "+19" };

static void chain_arrow(int32_t x0, int32_t x1, int on) {
    const uint16_t c = on ? AND_STARLIGHT : AND_DUST;
    put(x0, 91, x1 - x0, 2, c);
    if (x1 - x0 >= 5) { put(x1 - 3, 89, 1, 6, c); put(x1 - 2, 90, 1, 4, c); put(x1 - 1, 91, 1, 2, c); }
}

#define CB_W 46
static void chain_block(int32_t x, const char *label, const char *name, const char *sub, int on, int focus, uint16_t col, int32_t level) {
    const int32_t y = 54, w = CB_W, h = 74;
    const uint16_t bg = focus ? AND_NEBULA_HI : AND_NEBULA;
    panel(x, y, w, h, bg);
    outline(x, y, w, h, focus ? AND_SOLAR : (on ? col : AND_DUST));
    if (focus) outline(x + 1, y + 1, w - 2, h - 2, AND_SOLAR);
    text_center(x + w / 2, y + 6, label, focus ? AND_SOLAR : AND_HAZE, bg);
    text_center(x + w / 2, y + 19, name, on ? AND_STARLIGHT : AND_STARDUST, bg);
    text_center(x + w / 2, y + 31, sub, on ? col : AND_DUST, bg);
    if (level < 0) level = 0;
    if (level > 2097152) level = 2097152;
    const uint32_t lit = (uint32_t)(((int64_t)level * 8 + 1048576) >> 21);
    for (uint32_t k = 0; k < 8u; k++) put(x + 3 + (int32_t)k * 5, y + 44, 4, 5, (on && k < lit) ? col : AND_DUST);
    panel(x + 11, y + 56, w - 22, 12, on ? col : AND_DUST);
    text_center(x + w / 2, y + 59, on ? "ON" : "OFF", on ? AND_VOID : AND_STARDUST, on ? col : AND_DUST);
}

void ui_cosmic_render_chain(const and_chain_view_t *v) {
    static const and_chain_view_t k_none;
    const and_chain_view_t *c = v ? v : &k_none;
    const uint8_t fx = (uint8_t)((c->type / 4u) % AND_FX_COUNT), var = (uint8_t)(c->type % 4u);
    cosmic_step_frame();
    put(0, 0, 240, 240, AND_VOID);

    /* header: the slot */
    const uint16_t bp = FX_PRI(fx);
    panel(4, 6, 26, 15, bp);
    { char nb[4]; nb[0] = (char)('0' + (c->slot_no / 10u) % 10u); nb[1] = (char)('0' + c->slot_no % 10u); nb[2] = 0; text_center(17, 10, nb, AND_VOID, bp); }
    ui_draw_str(36, 10, slot_title(c->name, (gran_preset_t)c->type), AND_STARLIGHT, AND_VOID);
    put(4, 25, 232, 1, AND_DUST);
    put(4, 25, 26, 1, bp);
    text_center(CX, 34, "SIGNAL CHAIN", AND_HAZE, AND_VOID);

    /* IN -> EFFECT -> LAYER -> SPACE -> SHIMMER -> OUT */
    panel(4, 84, 16, 16, AND_NEBULA); outline(4, 84, 16, 16, AND_STARDUST); text_center(12, 88, "IN", AND_STARLIGHT, AND_NEBULA);
    panel(221, 84, 18, 16, AND_NEBULA); outline(221, 84, 18, 16, AND_STARDUST); text_center(230, 88, "OUT", AND_STARLIGHT, AND_NEBULA);
    chain_arrow(20, 22, 1);
    chain_arrow(68, 72, c->on[0]);
    chain_arrow(118, 122, c->on[1] || c->on[0]);
    chain_arrow(168, 172, c->on[2] || c->on[1] || c->on[0]);
    chain_arrow(218, 221, c->on[2] || c->on[1] || c->on[0]);
    const char *esub = fx == 0u ? k_material_name[c->effect_material % 6u] : k_variant_name[fx][var & 3u];
    chain_block(22,  "EFFECT", k_chain_fx_name[fx], esub, c->on[0], c->focus == 0u, FX_PRI(fx), c->level[0]);
    chain_block(72,  "LAYER",  "GLASS", k_material_name[c->layer_material % 6u], c->on[1], c->focus == 1u, FX_PRI(0), c->level[1]);
    chain_block(122, "SPACE",  "REVERB", "FDN", c->on[2], c->focus == 2u, AND_ION, c->level[2]);
    chain_block(172, "SHIMMER", "SHIMMER", c->on[3] ? k_shimmer_name[c->shimmer_interval & 3u] : "OFF", c->on[3], c->focus == 3u, AND_MAGENTA, c->level[3]);

    /* what the keys do on the focused block */
    static const char* const k_hint[4] = { "VOL: EFFECT AND VARIANT", "VOL: MATERIAL", "VOL: AMOUNT", "VOL: SHIMMER AMOUNT" };
    text_center(CX, 142, k_hint[c->focus % 4u], AND_HAZE, AND_VOID);
    text_center(CX, 154, c->focus == 3u ? "< >: BLOCK   CLICK: INTERVAL" : "< >: BLOCK   CLICK: ON / OFF", AND_STARDUST, AND_VOID);

    /* the slots: one square each, the slot you are on in gold, EMPTY ones hollow, the others in their effect's colour */
    ui_draw_str(4, 172, "SLOT", AND_HAZE, AND_VOID);
    for (uint32_t i = 0; i < 20u; i++) {
        const int32_t x = 32 + (int32_t)i * 10;
        const int cur = (i + 1u == c->slot_no), emp = (c->empty_mask >> i) & 1u;
        if (cur)      { put(x - 1, 170, 9, 12, AND_SOLAR); }
        else if (emp) { outline(x, 171, 7, 10, AND_DUST); }
        else          { put(x, 171, 7, 10, FX_PRI(c->slot_fx[i] % AND_FX_COUNT)); }
    }
    text_center(CX, 190, slot_title(c->name, (gran_preset_t)c->type), AND_STARLIGHT, AND_VOID);     /* the preset changes with VOL held 0.8 s */

    put(0, 214, 240, 26, AND_NEBULA);
    put(0, 214, 240, 1, AND_DUST);
    text_center(CX, 219, "HOME: OPEN PAGE   HOLD 1 S: SETTINGS", AND_HAZE, AND_NEBULA);
    text_center(CX, 229, "HOLD VOL: PRESET   HOLD PARA: SAVE", AND_STARDUST, AND_NEBULA);
}

/* ---- the frame ------------------------------------------------------------------------------- */
void ui_cosmic_render(const gran_telemetry_t *telem,
                      gran_preset_t current_preset,
                      uint8_t selected_axis,
                      const int32_t params[AND_FOCUS_COUNT],
                      uint8_t slot_no,
                      const char *name,
                      uint8_t battery_pct)
{
    (void)battery_pct;                                     /* the status cluster (ui_app.c) shows it */
    s_editing = (uint8_t)(selected_axis >> 7);             /* bit 7 of the axis: the hovered value is being edited */
    selected_axis &= 0x7Fu;
    static const gran_telemetry_t k_empty;
    const uint8_t bank = (uint8_t)(((uint32_t)current_preset / 4u) % AND_FX_COUNT);          /* the effect */
    /* the engine switches effect at its next audio block: for that one frame its telemetry is the other effect's, so it is not drawn */
    const gran_telemetry_t *t = (telem && telem->effect == bank) ? telem : &k_empty;
    cosmic_step_frame();

    put(0, 0, 240, 240, AND_VOID);

    /* header */
    const uint16_t bp = FX_PRI(bank);
    panel(4, 6, 26, 15, bp);
    { char nb[4]; nb[0] = (char)('0' + (slot_no / 10u) % 10u); nb[1] = (char)('0' + slot_no % 10u); nb[2] = 0; text_center(17, 10, nb, AND_VOID, bp); }   /* the slot number */
    if (selected_axis >= AND_FOCUS_SHIMMER && params) ui_draw_str(36, 10, "SPACE SHIMMER", AND_VIOLET, AND_VOID);
    else if (selected_axis >= AND_FOCUS_SPACE && params) ui_draw_str(36, 10, "SPACE REVERB", AND_VIOLET, AND_VOID);
    else if (selected_axis >= AND_FOCUS_LAYER && params) ui_draw_str(36, 10, "LAYER GLASS", FX_PRI(0), AND_VOID);          /* editing the layer: the title says so (the right side is the status cluster's) */
    else ui_draw_str(36, 10, slot_title(name, current_preset), AND_STARLIGHT, AND_VOID);
    put(4, 25, 232, 1, AND_DUST);
    put(4, 25, 26, 1, bp);

    /* radar */
    nebula(bank);
    starfield();
    ring(20, 24, 0, 1, AND_ORBIT);
    ring(40, 36, 0, 1, AND_ORBIT);
    if (bank == 0u) {
        glass_radar(t);
    } else if (bank == 1u) {
        for (int i = 0; i < t->active_count && i < (int)GRANULAR_MAX_VOICES; i++) comet(t, i, bank);
        density_gauge(t, bank);
    } else if (bank == 3u) {
        choir_radar(t);
    } else if (bank >= 4u) {
        step_ring(t, bank);
        for (int i = 0; i < t->active_count && i < (int)GRANULAR_MAX_VOICES; i++) comet(t, i, bank);
        density_gauge(t, bank);
    } else {
        acc_layers(t);
        looper_ring(t);
        if (t->gr_pct > 2u) { char buf[12]; buf[0] = 'G'; buf[1] = 'R'; buf[2] = ' '; u_to_str(buf + 3, t->gr_pct, "%"); ui_draw_str(188, 30, buf, AND_CORONA, AND_VOID); }
    }
    black_hole(bank, params ? params[0] : 1048576, t->frozen);
    if (t->frozen) text_center(CX, 136, "FROZEN", AND_ICE, AND_VOID);

    if (bank == 0u) mode_strip(t); else tape_strip(t, bank);
    static const char* const k_space_name[4]   = { "DECAY", "SIZE", "TONE", "SEND" };
    static const char* const k_shimmer_name[4] = { "AMOUNT", "INTERVAL", "DECAY", "SEND" };
    if (selected_axis >= AND_FOCUS_SHIMMER && params) {                    /* the SHIMMER page: amount, interval, and the decay and send it depends on */
        macro_tiles(params + AND_FOCUS_SHIMMER, (uint8_t)(selected_axis - AND_FOCUS_SHIMMER), 0, k_shimmer_name, 2u, AND_VIOLET);
    } else if (selected_axis >= AND_FOCUS_SPACE && params) {               /* the SPACE page: the reverb's four knobs */
        macro_tiles(params + AND_FOCUS_SPACE, (uint8_t)(selected_axis - AND_FOCUS_SPACE), 0, k_space_name, 1u, AND_VIOLET);
    } else if (selected_axis >= AND_FOCUS_LAYER && params) {               /* editing the GLASS layer: its four macros take the tiles */
        macro_tiles(params + 6, selected_axis < 10u ? (uint8_t)(selected_axis - 6u) : 255u, 0, 0, 0u, 0u);
    } else {
        macro_tiles(params, selected_axis < 4u ? selected_axis : 255u, bank, 0, 0u, 0u);
        if (params && params[10] > 0) { ui_draw_str(184, 42, "+GLASS", FX_PRI(0), AND_VOID); }       /* the layer is on */
    }
    mod_lane(params, bank, selected_axis);

    /* the limiter worked hard a moment ago: a thin corona edge on the top of the screen (ACCRETION) */
    if (t->clipping_alert > 0u) {
        const uint16_t c = (s_frame & 1u) ? AND_CORONA : AND_RED_GIANT;
        put(0, 0, 240, 2, c);
    }
}

#endif /* PA_PRODUCT_ANDROMEDA */
