/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* custom-fw/include/ui_cosmic_theme.h - the Andromeda colour system (RGB565, 240x240 IPS)
 *
 * One palette for every Andromeda screen. pa_ui.h maps the shared ONYX_COL_* tokens onto it in
 * Andromeda builds, so the settings, tuner and dialogs inherit it without being rewritten.
 *
 * The rules:
 *  - Space is never pure black: the background is a blue-black void, panels are indigo nebula.
 *  - Greys are cool lavenders (dust -> stardust -> haze -> starlight), never neutral.
 *  - Pitch is coloured by the Doppler effect: a grain pitched UP is blueshifted (ion blue), a grain
 *    pitched DOWN is redshifted (corona red), unison is white starlight, reverse (time running
 *    backwards) is magenta.
 *  - Each bank owns a pair of stellar colours (primary / secondary), used for its badge, its
 *    accretion glow, its density arc and its modulation wave.
 */
#ifndef UI_COSMIC_THEME_H
#define UI_COSMIC_THEME_H

#include <stdint.h>

#define AND_RGB(r, g, b) ((uint16_t)((((uint16_t)(r) & 0xF8u) << 8) | (((uint16_t)(g) & 0xFCu) << 3) | ((uint16_t)(b) >> 3)))

/* ---- space ---------------------------------------------------------------------------------- */
#define AND_VOID        AND_RGB(0x04, 0x04, 0x10)   /* background: blue-black deep space */
#define AND_NEBULA      AND_RGB(0x12, 0x0E, 0x30)   /* panels, tiles */
#define AND_NEBULA_HI   AND_RGB(0x22, 0x1A, 0x4E)   /* selected / pressed panel */
#define AND_DUST        AND_RGB(0x2E, 0x2A, 0x5E)   /* dividers, empty segments */
#define AND_ORBIT       AND_RGB(0x44, 0x40, 0x84)   /* orbit paths on the radar */
#define AND_STARDUST    AND_RGB(0x5A, 0x58, 0x9A)   /* dim lines, distant stars */
#define AND_HAZE        AND_RGB(0x96, 0x98, 0xCC)   /* labels, captions */
#define AND_STARLIGHT   AND_RGB(0xEE, 0xF2, 0xFF)   /* values, names */

/* ---- stellar accents ------------------------------------------------------------------------ */
#define AND_SOLAR       AND_RGB(0xFF, 0xCC, 0x4C)   /* focus / cursor: a yellow dwarf */
#define AND_CORONA      AND_RGB(0xFF, 0x8E, 0x3C)   /* warning, redshift */
#define AND_RED_GIANT   AND_RGB(0xFF, 0x3E, 0x5C)   /* clipping, recording, low battery */
#define AND_AURORA      AND_RGB(0x40, 0xF6, 0xB2)   /* ok, in tune, playing */
#define AND_ION         AND_RGB(0x3A, 0xD8, 0xFF)   /* blueshift, Bluetooth */
#define AND_VIOLET      AND_RGB(0x9E, 0x6E, 0xFF)   /* nebula violet */
#define AND_MAGENTA     AND_RGB(0xFF, 0x5E, 0xD4)   /* reverse grains */
#define AND_ICE         AND_RGB(0xB0, 0xEC, 0xFF)   /* frozen tape */
#define AND_PULSAR_PINK AND_RGB(0xFF, 0x8C, 0xCC)   /* EQ / tone */

/* ---- meaning -------------------------------------------------------------------------------- */
#define AND_PITCH_UP     AND_ION
#define AND_PITCH_DOWN   AND_CORONA
#define AND_PITCH_UNISON AND_STARLIGHT
#define AND_REVERSE      AND_MAGENTA

#define AND_LOOP_REC     AND_RED_GIANT
#define AND_LOOP_PLAY    AND_AURORA
#define AND_LOOP_DUB     AND_CORONA
#define AND_FROZEN       AND_ICE

/* The four banks: I EVENT HORIZON, II QUANTUM STUTTER, III COSMIC ARP, IV RELATIVITY LOOP */
#define AND_BANK_PRIMARY(b)   ((b) == 0 ? AND_VIOLET : (b) == 1 ? AND_ION : (b) == 2 ? AND_SOLAR : AND_AURORA)
#define AND_BANK_SECONDARY(b) ((b) == 0 ? AND_MAGENTA : (b) == 1 ? AND_STARLIGHT : (b) == 2 ? AND_CORONA : AND_ICE)

/* A dark tint of each bank's colour: the nebula wisps behind the radar */
#define AND_BANK_NEBULA(b)    ((b) == 0 ? AND_RGB(0x34, 0x1C, 0x62) : (b) == 1 ? AND_RGB(0x12, 0x36, 0x56) : (b) == 2 ? AND_RGB(0x48, 0x34, 0x12) : AND_RGB(0x10, 0x44, 0x36))

/* High-contrast (outdoor) variant of the three runtime greys: line, dim, mute */
#define AND_HC_LINE     AND_RGB(0x4C, 0x48, 0x8C)
#define AND_HC_DIM      AND_RGB(0x86, 0x84, 0xC4)
#define AND_HC_MUTE     AND_RGB(0xC8, 0xCA, 0xEE)

#endif /* UI_COSMIC_THEME_H */
