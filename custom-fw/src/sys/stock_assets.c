/*
 * src/sys/stock_assets.c - 20 stock amp models and 20 stock cabinet IRs
 *
 * Provides baseline amplifier models and cabinet impulse responses.
 *
 * Two sources, chosen at build time:
 *   - embedded tables (stock_assets_embedded.inc) for device and test builds
 *   - flash loader (-DPA_STOCK_FLASH_LOADER) for emulator DLL builds
 */

#include "../../include/pa_stock_assets.h"
#include "../../include/pa_user_cab.h"
#include <string.h>
#include "../../include/pa_xip.h"

#ifndef PA_STOCK_FLASH_LOADER
/* ===================================================================== embedded tables */

#if defined(__has_include)
#  if !__has_include("stock_assets_embedded.inc")
#    error "src/sys/stock_assets_embedded.inc is missing: run python custom-fw/tools/import_stock_assets.py"
#  endif
#endif
#include "stock_assets_embedded.inc"

/* static tables: ready before pa_stock_assets_init(), as the old const ones were */
pa_stock_amp_info_t g_pa_stock_amps[PA_STOCK_AMP_COUNT] = PA_STOCK_EMBEDDED_AMPS;
pa_stock_cab_info_t g_pa_stock_cabs[PA_STOCK_CAB_COUNT] = PA_STOCK_EMBEDDED_CABS;

void pa_stock_assets_init(void) { /* nothing to do: the tables are built in */ }

int pa_stock_has_factory_assets(void) { return 1; }

const char *pa_stock_get_amp_name(uint8_t idx) {
    if (idx >= PA_STOCK_AMP_COUNT) return "Unknown";
    return g_pa_stock_amps[idx].name;
}

const char *pa_stock_get_cab_name(uint8_t idx) {
    if (idx >= PA_STOCK_CAB_COUNT) return "Unknown";
    return g_pa_stock_cabs[idx].name;
}

int pa_stock_load_amp(uint8_t idx, pa_nam_model_t *out_model, float *out_dc_bias) {
    if (idx >= PA_STOCK_AMP_COUNT || !out_model) return -1;
    /* Blk-02: the const blob is exactly 3,852 B (weights + biases). Copying
     * sizeof(pa_nam_model_t) (3,876 B) over-read 24 B of the NEXT table into the
     * runtime de-fizz / DC-blocker state -> multi-FS step = click on every amp change. */
    pa_xip_read(out_model, g_pa_stock_amps[idx].model_blob, PA_NAM_BLOB_BYTES);   /* verified: the table is in the flash */
    pa_nam_reset(out_model);               /* zero h_state, gates, de-fizz and DC states */
    out_model->scale_q21 = PA_Q21_ONE;
    out_model->active    = 1;
    if (out_dc_bias) *out_dc_bias = g_pa_stock_amps[idx].dc_bias;
    return 0;
}

int pa_stock_load_cab(uint8_t idx, int32_t *out_taps_q18, uint32_t *out_num_taps) {
    if (idx >= PA_STOCK_CAB_COUNT || !out_taps_q18) return -1;
    pa_xip_read(out_taps_q18, g_pa_stock_cabs[idx].taps_q18, PA_STOCK_CAB_TAPS * sizeof(int32_t));   /* verified: the table is in the flash */
    if (out_num_taps) *out_num_taps = PA_STOCK_CAB_TAPS;
    return 0;
}

#else
/* ===================================================================== flash loader */

#include "../../include/pa_spi_nor.h"
#include "../../include/pa_flash_map.h"
#include <math.h>

#define AMP_REC_SIZE   0x2000u
#define CAB_REC_SIZE   0x1000u
#define CAB_BASE_OFF   0x28000u
#define CAB_TAP0_OFF   20u
#define CAB_FADE_LEN   64u
#define AM3_WEIGHTS    0x43Cu
#define AM4_WEIGHTS    0x83Cu

pa_stock_amp_info_t g_pa_stock_amps[PA_STOCK_AMP_COUNT];
pa_stock_cab_info_t g_pa_stock_cabs[PA_STOCK_CAB_COUNT];

static char s_amp_names[PA_STOCK_AMP_COUNT][24];
static char s_cab_names[PA_STOCK_CAB_COUNT][20];
static int  s_flash_has_assets = 0;

/* Placeholder cabinet: the project's synthesised default IR (512 taps in Q18) */
static const int32_t k_default_cab_taps[512] = {
#include "../fx/cab_default_table.inc"
};

static const char * const k_fallback_amp_names[PA_STOCK_AMP_COUNT] = {
    "1 J900 Clean",   "2 Rat OD",       "3 Tube OD",      "4 J900 Crunch",
    "5 Twin Clean",   "6 Blues OD",     "7 MV Clean",     "8 XC Distortion",
    "9 JC120 Clean",  "10 Blues Drive", "11 Dark DS",     "12 XC Solo",
    "13 Rat Lead",    "14 Rat Solo",    "15 Rat Heavy",   "16 JHS Drive",
    "17 J900 Lead",   "18 High Gain",   "19 FR Lead",     "20 JVM Modern"
};

static const char * const k_fallback_cab_names[PA_STOCK_CAB_COUNT] = {
    "1 Vintage 4x12", "2 1960 4x12",    "3 V30 4x12",     "4 EVH 4x12",
    "5 HiFi Flat",    "6 Boutique 2x12","7 AC30 2x12",    "8 FR 1x12",
    "9 Modern 1x12",  "10 SLD 4x12",    "11 Tweed 2x12",  "12 Recto 4x12",
    "13 British 4x12","14 Mini 1x12",   "15 Jazz 2x12",   "16 Bass 4x10",
    "17 SVT 4x10",    "18 Dark Bass",   "19 Micro Bass",  "20 Bass 4x12"
};

/* The records' floats are read with memcpy, never through a cast pointer: a record offset is
 * not guaranteed 4-byte aligned in the read buffer, and an unaligned float load faults on the
 * pi32v2 core. */
static inline float rec_f32(const uint8_t *rec, uint32_t off) {
    float v;
    memcpy(&v, rec + off, sizeof v);
    return v;
}

/* the DLL's (int)(v * 2097151.0f): a float32 product, truncated (tools/amx_gru_convert.py) */
static inline int32_t q_float(float v) {
    return (int32_t)(v * 2097151.0f);
}

static inline void put_i32(uint8_t *blob, uint32_t off, int32_t v) {
    memcpy(blob + off, &v, sizeof v);
}

static void set_fallback_amp(uint32_t i) {
    g_pa_stock_amps[i].name = k_fallback_amp_names[i];
    g_pa_stock_amps[i].bass_freq = 300.0f;    g_pa_stock_amps[i].bass_min_db = -12.0f;   g_pa_stock_amps[i].bass_max_db = 12.0f;
    g_pa_stock_amps[i].mid_freq = 850.0f;     g_pa_stock_amps[i].mid_min_db = -12.0f;    g_pa_stock_amps[i].mid_max_db = 12.0f;
    g_pa_stock_amps[i].treble_freq = 2000.0f; g_pa_stock_amps[i].treble_min_db = -12.0f; g_pa_stock_amps[i].treble_max_db = 12.0f;
    g_pa_stock_amps[i].dc_bias = 0.0f;
}

void pa_stock_assets_init(void) {
    uint8_t tag[4];
    int err = pa_spi_nor_read(PA_FLASH_FACTORY_CAB_ADDR, tag, 4);
    s_flash_has_assets = (err == 0 && (memcmp(tag, "AM4", 3) == 0 || memcmp(tag, "AM3", 3) == 0));

    for (uint32_t i = 0; i < PA_STOCK_AMP_COUNT; i++) {
        uint8_t meta[64];
        uint32_t addr = PA_FLASH_FACTORY_CAB_ADDR + i * AMP_REC_SIZE;
        if (s_flash_has_assets && pa_spi_nor_read(addr, meta, sizeof meta) == 0) {
            memcpy(s_amp_names[i], &meta[4], 20);
            s_amp_names[i][20] = '\0';
            g_pa_stock_amps[i].name          = s_amp_names[i];
            g_pa_stock_amps[i].bass_freq     = rec_f32(meta, 24);
            g_pa_stock_amps[i].bass_min_db   = rec_f32(meta, 28);
            g_pa_stock_amps[i].bass_max_db   = rec_f32(meta, 32);
            g_pa_stock_amps[i].mid_freq      = rec_f32(meta, 36);
            g_pa_stock_amps[i].mid_min_db    = rec_f32(meta, 40);
            g_pa_stock_amps[i].mid_max_db    = rec_f32(meta, 44);
            g_pa_stock_amps[i].treble_freq   = rec_f32(meta, 48);
            g_pa_stock_amps[i].treble_min_db = rec_f32(meta, 52);
            g_pa_stock_amps[i].treble_max_db = rec_f32(meta, 56);
            g_pa_stock_amps[i].dc_bias       = rec_f32(meta, 60);
        } else {
            set_fallback_amp(i);
        }
        g_pa_stock_amps[i].model_blob = NULL;
    }

    for (uint32_t i = 0; i < PA_STOCK_CAB_COUNT; i++) {
        uint8_t hdr[32];
        uint32_t addr = PA_FLASH_FACTORY_CAB_ADDR + CAB_BASE_OFF + i * CAB_REC_SIZE;
        if (s_flash_has_assets && pa_spi_nor_read(addr, hdr, sizeof hdr) == 0 && memcmp(hdr, "CAB", 3) == 0) {
            memcpy(s_cab_names[i], &hdr[4], 16);
            s_cab_names[i][16] = '\0';
            g_pa_stock_cabs[i].name = s_cab_names[i];
        } else {
            g_pa_stock_cabs[i].name = k_fallback_cab_names[i];
        }
        g_pa_stock_cabs[i].taps_q18 = NULL;
    }
}

int pa_stock_has_factory_assets(void) {
    return s_flash_has_assets;
}

const char *pa_stock_get_amp_name(uint8_t idx) {
    if (idx >= PA_STOCK_AMP_COUNT) return "Unknown";
    return g_pa_stock_amps[idx].name;
}

const char *pa_stock_get_cab_name(uint8_t idx) {
    if (idx >= PA_STOCK_CAB_COUNT) return "Unknown";
    return g_pa_stock_cabs[idx].name;
}

int pa_stock_load_amp(uint8_t idx, pa_nam_model_t *out_model, float *out_dc_bias) {
    if (idx >= PA_STOCK_AMP_COUNT || !out_model) return -1;

    if (s_flash_has_assets) {
        static uint8_t s_rec[AMP_REC_SIZE];
        uint32_t addr = PA_FLASH_FACTORY_CAB_ADDR + idx * AMP_REC_SIZE;
        if (pa_spi_nor_read(addr, s_rec, AMP_REC_SIZE) == 0) {
            const int am4 = (memcmp(s_rec, "AM4", 3) == 0);
            const uint32_t base = am4 ? AM4_WEIGHTS : AM3_WEIGHTS;
            /* AM3: W_out at +0xCC0, no biases. AM4: six bias vectors at +0xCC0..+0xE3F, then
             * W_out at +0xE40 (tools/amx_gru_convert.py). */
            const uint32_t w_out = base + (am4 ? 0xE40u : 0xCC0u);

            uint8_t *blob = (uint8_t *)out_model;
            memset(out_model, 0, sizeof(*out_model));

            for (uint32_t i = 0; i < 16u; i++) {
                for (uint32_t j = 0; j < 16u; j++) {
                    put_i32(blob, 0    + i * 68u + j * 4u, q_float(rec_f32(s_rec, base + 0x0C0u + (i * 16u + j) * 4u)));
                    put_i32(blob, 1088 + i * 68u + j * 4u, q_float(rec_f32(s_rec, base + 0x4C0u + (i * 16u + j) * 4u)));
                    put_i32(blob, 2176 + i * 64u + j * 4u, q_float(rec_f32(s_rec, base + 0x8C0u + (i * 16u + j) * 4u)));
                }
                put_i32(blob, 0    + i * 68u + 64u, q_float(rec_f32(s_rec, base + 0x000u + i * 4u)));  /* W_ir */
                put_i32(blob, 1088 + i * 68u + 64u, q_float(rec_f32(s_rec, base + 0x040u + i * 4u)));  /* W_iz */
                put_i32(blob, 3200 + i * 4u,        q_float(rec_f32(s_rec, base + 0x080u + i * 4u)));  /* W_in */
                put_i32(blob, 3264 + i * 4u,        q_float(rec_f32(s_rec, w_out + i * 4u)));          /* W_out */
                if (am4) {
                    const int32_t b_ir = q_float(rec_f32(s_rec, base + 0xCC0u + i * 4u));
                    const int32_t b_iz = q_float(rec_f32(s_rec, base + 0xD00u + i * 4u));
                    const int32_t b_in = q_float(rec_f32(s_rec, base + 0xD40u + i * 4u));
                    const int32_t b_hr = q_float(rec_f32(s_rec, base + 0xD80u + i * 4u));
                    const int32_t b_hz = q_float(rec_f32(s_rec, base + 0xDC0u + i * 4u));
                    const int32_t b_hn = q_float(rec_f32(s_rec, base + 0xE00u + i * 4u));
                    put_i32(blob, 3592 + i * 4u, b_ir + b_hr);   /* b_r  */
                    put_i32(blob, 3656 + i * 4u, b_iz + b_hz);   /* b_z  */
                    put_i32(blob, 3720 + i * 4u, b_in);          /* b_ni */
                    put_i32(blob, 3784 + i * 4u, b_hn);          /* b_nh */
                }
            }

            pa_nam_reset(out_model);
            out_model->scale_q21 = PA_Q21_ONE;
            out_model->active    = 1;
            if (out_dc_bias) *out_dc_bias = g_pa_stock_amps[idx].dc_bias;
            return 0;
        }
    }

    /* placeholder: a near-transparent single unit */
    memset(out_model, 0, sizeof(*out_model));
    pa_nam_reset(out_model);
    out_model->scale_q21 = PA_Q21_ONE;
    out_model->w_in[0]   = PA_Q21_ONE;
    out_model->w_out[0]  = PA_Q21_ONE;
    out_model->active    = 1;
    if (out_dc_bias) *out_dc_bias = 0.0f;
    return 0;
}

int pa_stock_load_cab(uint8_t idx, int32_t *out_taps_q18, uint32_t *out_num_taps) {
    if (idx >= PA_STOCK_CAB_COUNT || !out_taps_q18) return -1;

    if (s_flash_has_assets) {
        static uint8_t s_cab_raw[CAB_REC_SIZE];
        uint32_t addr = PA_FLASH_FACTORY_CAB_ADDR + CAB_BASE_OFF + idx * CAB_REC_SIZE;
        if (pa_spi_nor_read(addr, s_cab_raw, CAB_REC_SIZE) == 0 && memcmp(s_cab_raw, "CAB", 3) == 0) {
            const uint8_t *raw = s_cab_raw + CAB_TAP0_OFF;
            for (uint32_t k = 0; k < PA_STOCK_CAB_TAPS; k++) {
                int32_t v = (int32_t)(raw[3 * k] | (raw[3 * k + 1] << 8) | ((uint32_t)raw[3 * k + 2] << 16));
                if (v & 0x800000) v -= (1 << 24);
                int32_t q18 = v >> 5;
                int32_t j = (int32_t)k - (int32_t)(PA_STOCK_CAB_TAPS - CAB_FADE_LEN);
                if (j >= 0) {   /* raised-cosine fade over the last 64 taps (as the importer) */
                    double fade = 0.5 * (1.0 + cos(3.14159265358979323846 * (double)(j + 1) / (double)CAB_FADE_LEN));
                    q18 = (int32_t)lrint((double)q18 * fade);
                }
                out_taps_q18[k] = q18;
            }
            if (out_num_taps) *out_num_taps = PA_STOCK_CAB_TAPS;
            return 0;
        }
    }

    /* placeholder: the synthesised default cabinet */
    for (uint32_t i = 0; i < 512u; i++) out_taps_q18[i] = k_default_cab_taps[i];
    for (uint32_t i = 512u; i < PA_STOCK_CAB_TAPS; i++) out_taps_q18[i] = 0;
    if (out_num_taps) *out_num_taps = PA_STOCK_CAB_TAPS;
    return 0;
}

#endif /* PA_STOCK_FLASH_LOADER */

/* ===================================================================== imported user content */

typedef struct { const char *name; uint32_t num_taps; const int32_t *taps; } pa_emb_cab_t;

/* The device build only: the host suites and the emulator must not see user captures (their tests count the registry). A host test of
 * this path builds with -DPA_USER_EMBEDDED_FORCE=1. */
#if (!defined(PA_HOST_SIM) && !defined(PA_HOST_BUILD)) || defined(PA_USER_EMBEDDED_FORCE)
#ifndef PA_USER_EMBEDDED_INC
#  define PA_USER_EMBEDDED_INC "pa_user_embedded.inc"   /* a test points it at a fixture */
#endif
#if defined(__has_include)
#  if __has_include(PA_USER_EMBEDDED_INC)
#    include PA_USER_EMBEDDED_INC      /* generated by tools/pa_import.py: PA_EMB_NAM_N/PA_EMB_CAB_N, s_emb_nam[], s_emb_cab[] */
#    define PA_HAVE_USER_EMBEDDED 1
#  endif
#endif
#endif
#ifndef PA_HAVE_USER_EMBEDDED
#  define PA_EMB_NAM_N 0u
#  define PA_EMB_CAB_N 0u
static const pa_emb_nam_t s_emb_nam[1] = { { 0 } };
static const pa_emb_cab_t s_emb_cab[1] = { { 0 } };
#endif

#ifdef PA_EMB_ALLOW_WAVENET
int pa_emb_allow_wavenet(void) { return 1; }
#else
int pa_emb_allow_wavenet(void) { return 0; }
#endif
uint8_t             pa_emb_nam_count(void)          { return (uint8_t)PA_EMB_NAM_N; }
const pa_emb_nam_t *pa_emb_nam_get(uint8_t i)       { return i < PA_EMB_NAM_N ? &s_emb_nam[i] : NULL; }

/* The stored cabinets come from src/sys/pa_user_cab.c. A harness or the emulator DLL that links the flash-loader variant of this file without it gets none (weak stand-ins). The device build has NO stand-ins:
 * with them, the names of the stored cabinets read back empty on the unit (the compiler had folded the stand-ins into the callers in this file). */
#ifdef PA_STOCK_FLASH_LOADER
__attribute__((weak)) uint8_t pa_user_cab_span(void) { return 0u; }
__attribute__((weak)) uint8_t pa_user_cab_active_count(void) { return 0u; }
__attribute__((weak)) int pa_user_cab_is_active(uint8_t slot) { (void)slot; return 0; }
__attribute__((weak)) const char *pa_user_cab_name(uint8_t slot) { (void)slot; return ""; }
__attribute__((weak)) int pa_user_cab_load(uint8_t slot, int32_t *out, uint32_t cap, uint32_t *n) { (void)slot; (void)out; (void)cap; (void)n; return -1; }
#endif

/* The cabinet types, in the order of the type knob: the 20 stock ones, the ones linked into the image, then the stored ones (pa_user_cab.h) at their slot indices. A deleted stored cabinet
 * is a hole: its index stays (presets name cabinets by it) and pa_cab_is_active() says it is gone. */
uint8_t pa_cab_total_count(void) { return (uint8_t)(PA_STOCK_CAB_COUNT + PA_EMB_CAB_N + pa_user_cab_span()); }

int pa_cab_is_active(uint8_t idx) {
    if (idx < PA_STOCK_CAB_COUNT + PA_EMB_CAB_N) return 1;
    return pa_user_cab_is_active((uint8_t)(idx - PA_STOCK_CAB_COUNT - PA_EMB_CAB_N));
}
/* the cabinets that exist (what the screen and the editor count) */
uint8_t pa_cab_active_count(void) { return (uint8_t)(PA_STOCK_CAB_COUNT + PA_EMB_CAB_N + pa_user_cab_active_count()); }

const char *pa_cab_name(uint8_t idx) {
    if (idx < PA_STOCK_CAB_COUNT) return pa_stock_get_cab_name(idx);
    idx = (uint8_t)(idx - PA_STOCK_CAB_COUNT);
    if (idx < PA_EMB_CAB_N) return s_emb_cab[idx].name;
    idx = (uint8_t)(idx - PA_EMB_CAB_N);
    return pa_user_cab_is_active(idx) ? pa_user_cab_name(idx) : "MISSING";
}

int pa_cab_load(uint8_t idx, int32_t *out_taps_q18, uint32_t *out_num_taps) {
    if (idx < PA_STOCK_CAB_COUNT) return pa_stock_load_cab(idx, out_taps_q18, out_num_taps);
    idx = (uint8_t)(idx - PA_STOCK_CAB_COUNT);
    if (idx >= PA_EMB_CAB_N) {                                    /* a stored cabinet */
        uint32_t t = 0;
        if (!out_taps_q18 || pa_user_cab_load((uint8_t)(idx - PA_EMB_CAB_N), out_taps_q18, PA_STOCK_CAB_TAPS, &t) != 0) return -1;
        if (out_num_taps) *out_num_taps = PA_STOCK_CAB_TAPS;
        return 0;
    }
    if (!out_taps_q18) return -1;
    const uint32_t n = s_emb_cab[idx].num_taps > PA_STOCK_CAB_TAPS ? PA_STOCK_CAB_TAPS : s_emb_cab[idx].num_taps;
    pa_xip_read(out_taps_q18, s_emb_cab[idx].taps, n * sizeof(int32_t));
    for (uint32_t i = n; i < PA_STOCK_CAB_TAPS; i++) out_taps_q18[i] = 0;
    if (out_num_taps) *out_num_taps = PA_STOCK_CAB_TAPS;
    return 0;
}
