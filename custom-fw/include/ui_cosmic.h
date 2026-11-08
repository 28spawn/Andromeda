/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* custom-fw/include/ui_cosmic.h — ANDROMEDA Cosmic Radar UI for 240x240 LCD
 * Targeting M-VAVE Pocket AMP running Onyx Firmware.
 */
#ifndef UI_COSMIC_H
#define UI_COSMIC_H

#include <stdint.h>
#include "pa_ui.h"
#include "pa_granular.h"

/* Preset metadata names */
extern const char* const g_cosmic_preset_names[GRAN_PRESET_COUNT];
extern const char* const g_cosmic_bank_names[AND_FX_COUNT];

/* Cosmic UI rendering function: the play screen. `type` is the effect + variant of the slot (0..11), `slot_no` 1..20, `name` the slot's name (a leading "NN " is dropped). */
void ui_cosmic_render(const gran_telemetry_t *telem,
                      gran_preset_t type,
                      uint8_t selected_axis,
                      const int32_t params[AND_FOCUS_COUNT],   /* 0..3 the effect's macros, 4 MIX, 5 SPACE, 6..9 the layer's macros, 10 its level (Q21) */
                      uint8_t slot_no,
                      const char *name,
                      uint8_t battery_pct);

/* The signal chain page: IN -> EFFECT -> LAYER -> SPACE -> OUT, the slot strip under it */
typedef struct {
    uint8_t     slot_no;            /* 1..20 */
    const char *name;
    uint8_t     type;               /* the effect's type 0..11 (effect * 4 + variant) */
    uint8_t     on[4];              /* EFFECT, LAYER, SPACE, SHIMMER: running */
    uint8_t     shimmer_interval;   /* 0..3: +12, +7, +12 and +7, +19 */
    uint8_t     layer_material;     /* 0..5 */
    uint8_t     effect_material;    /* GLASS: the material of the effect, 0..5 */
    int32_t     level[4];           /* Q21: the effect's MIX, the layer's level, SPACE, the shimmer's feedback */
    uint8_t     focus;              /* 0 EFFECT, 1 LAYER, 2 SPACE, 3 SHIMMER */
    uint32_t    empty_mask;         /* bit n: slot n+1 is EMPTY */
    uint8_t     slot_fx[20];        /* the effect (0..5) of each slot, for the colour of its square */
} and_chain_view_t;

void ui_cosmic_render_chain(const and_chain_view_t *v);

#endif /* UI_COSMIC_H */
