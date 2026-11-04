
#include "lc_cfgcodec.h"
#include "lc_stateblob.h"
#include "lc_app_abi_v2.h"
#include "lc_compat.h"

#define LC_CFG_OFF_TOTAL_LEN        8u
#define LC_CFG_OFF_FLAGS            12u
#define LC_CFG_OFF_TARGET_LEN       16u
#define LC_CFG_OFF_TARGET           17u
#define LC_CFG_OFF_COUNTER_LEN      49u
#define LC_CFG_OFF_COUNTER          50u
#define LC_CFG_OFF_LINE_X1          114u
#define LC_CFG_OFF_MAX_DIST         128u
#define LC_CFG_OFF_TRACK_K          130u
#define LC_CFG_OFF_MAX_MISS         131u
#define LC_CFG_OFF_K_CONFIRM        132u
#define LC_CFG_OFF_TOGGLES          133u
#define LC_CFG_OFF_WINDOW_MIN       134u
#define LC_CFG_OFF_RESERVED         136u

static void wr_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int name_chars_ok(const char *s, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)s[i];
        if (c == '"' || c == '\\' || c == 0x7Fu) return 0;
    }
    const unsigned char *p = (const unsigned char *)s;
    while (*p != 0) {
        uint32_t cp = 0;
        uint8_t need = 0;
        uint8_t clen = 0;
        if (*p < 0x80u) {
            if (*p < 0x20u) return 0;
            p++;
            continue;
        } else if ((*p & 0xE0u) == 0xC0u) {
            need = 1; clen = 2; cp = (uint32_t)(*p & 0x1Fu);
        } else if ((*p & 0xF0u) == 0xE0u) {
            need = 2; clen = 3; cp = (uint32_t)(*p & 0x0Fu);
        } else if ((*p & 0xF8u) == 0xF0u) {
            need = 3; clen = 4; cp = (uint32_t)(*p & 0x07u);
        } else {
            return 0;
        }
        p++;
        while (need > 0) {
            if ((*p & 0xC0u) != 0x80u) return 0;
            cp = (cp << 6) | (uint32_t)(*p & 0x3Fu);
            p++;
            need--;
        }
        if (cp > 0x10FFFFu) return 0;
        if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;
        if ((clen == 2 && cp < 0x80u) || (clen == 3 && cp < 0x800u) ||
            (clen == 4 && cp < 0x10000u)) return 0;
        if (cp < 0x20u || (cp >= 0x7Fu && cp <= 0x9Fu)) return 0;
    }
    return 1;
}

uint32_t lc_cfg_encode(uint8_t *out, uint32_t cap, const lc_bus_config_t *cfg)
{
    if (!out || !cfg || cap < LC_CFG_CAND_SIZE) return 0;
    uint32_t tlen = (uint32_t)lc_strlen(cfg->target_class_name);
    uint32_t clen = (uint32_t)lc_strlen(cfg->counter_name);
    if (tlen == 0u || tlen >= LC_TARGET_CLASS_NAME_LEN) return 0;
    if (clen == 0u || clen >= LC_COUNTER_NAME_LEN) return 0;

    lc_memset(out, 0, LC_CFG_CAND_SIZE);
    wr_u32(out + 0, LC_CFG_MAGIC);
    wr_u32(out + 4, LC_CFG_SCHEMA);
    wr_u32(out + LC_CFG_OFF_TOTAL_LEN, LC_CFG_CAND_SIZE);
    wr_u32(out + LC_CFG_OFF_FLAGS, 0u);
    out[LC_CFG_OFF_TARGET_LEN] = (uint8_t)tlen;
    lc_memcpy(out + LC_CFG_OFF_TARGET, cfg->target_class_name, tlen);
    out[LC_CFG_OFF_COUNTER_LEN] = (uint8_t)clen;
    lc_memcpy(out + LC_CFG_OFF_COUNTER, cfg->counter_name, clen);
    wr_u16(out + LC_CFG_OFF_LINE_X1 + 0, cfg->line_x1_permille);
    wr_u16(out + LC_CFG_OFF_LINE_X1 + 2, cfg->line_y1_permille);
    wr_u16(out + LC_CFG_OFF_LINE_X1 + 4, cfg->line_x2_permille);
    wr_u16(out + LC_CFG_OFF_LINE_X1 + 6, cfg->line_y2_permille);
    wr_u16(out + LC_CFG_OFF_LINE_X1 + 8, cfg->outside_x_permille);
    wr_u16(out + LC_CFG_OFF_LINE_X1 + 10, cfg->outside_y_permille);
    wr_u16(out + LC_CFG_OFF_MAX_DIST - 2u, cfg->conf_threshold_permille);
    wr_u16(out + LC_CFG_OFF_MAX_DIST, cfg->max_dist_permille);
    out[LC_CFG_OFF_TRACK_K] = cfg->track_history_k;
    out[LC_CFG_OFF_MAX_MISS] = cfg->max_miss;
    out[LC_CFG_OFF_K_CONFIRM] = cfg->k_confirm;
    out[LC_CFG_OFF_TOGGLES] = (uint8_t)((cfg->tracks_report_enable ? 0x01u : 0u) |
                                        (cfg->heat_grid_enable ? 0x02u : 0u));
    wr_u16(out + LC_CFG_OFF_WINDOW_MIN, cfg->window_minutes);
    return LC_CFG_CAND_SIZE;
}

lc_cfg_status_t lc_cfg_decode(const uint8_t *in, uint32_t len, lc_bus_config_t *out)
{
    if (!in || !out) return LC_CFG_ERR_SIZE;
    if (len >= 8u && lc_rd_u32(in + 0) == LC_ST_BLOB_MAGIC) return LC_CFG_ERR_STATE_BLOB;
    if (len != LC_CFG_CAND_SIZE) return LC_CFG_ERR_SIZE;
    if (lc_rd_u32(in + 0) != LC_CFG_MAGIC) return LC_CFG_ERR_MAGIC;
    if (lc_rd_u32(in + 4) != LC_CFG_SCHEMA) return LC_CFG_ERR_SCHEMA;
    if (lc_rd_u32(in + LC_CFG_OFF_TOTAL_LEN) != LC_CFG_CAND_SIZE) return LC_CFG_ERR_SIZE;
    if (lc_rd_u32(in + LC_CFG_OFF_FLAGS) != 0u) return LC_CFG_ERR_FLAGS;
    if (lc_rd_u32(in + LC_CFG_OFF_RESERVED) != 0u) return LC_CFG_ERR_FLAGS;
    if (lc_rd_u32(in + LC_CFG_OFF_RESERVED + 4u) != 0u) return LC_CFG_ERR_FLAGS;

    uint32_t tlen = in[LC_CFG_OFF_TARGET_LEN];
    uint32_t clen = in[LC_CFG_OFF_COUNTER_LEN];
    if (tlen == 0u || tlen >= LC_TARGET_CLASS_NAME_LEN) return LC_CFG_ERR_NAME;
    if (clen == 0u || clen >= LC_COUNTER_NAME_LEN) return LC_CFG_ERR_NAME;
    for (uint32_t i = tlen; i < LC_TARGET_CLASS_NAME_LEN; i++) {
        if (in[LC_CFG_OFF_TARGET + i] != 0u) return LC_CFG_ERR_NAME;
    }
    for (uint32_t i = clen; i < LC_COUNTER_NAME_LEN; i++) {
        if (in[LC_CFG_OFF_COUNTER + i] != 0u) return LC_CFG_ERR_NAME;
    }

    lc_memset(out, 0, sizeof(*out));
    lc_memcpy(out->target_class_name, in + LC_CFG_OFF_TARGET, tlen);
    out->target_class_name[tlen] = '\0';
    lc_memcpy(out->counter_name, in + LC_CFG_OFF_COUNTER, clen);
    out->counter_name[clen] = '\0';
    if (!name_chars_ok(out->target_class_name, tlen)) return LC_CFG_ERR_NAME;
    if (!name_chars_ok(out->counter_name, clen)) return LC_CFG_ERR_NAME;

    out->line_x1_permille = rd_u16(in + LC_CFG_OFF_LINE_X1 + 0);
    out->line_y1_permille = rd_u16(in + LC_CFG_OFF_LINE_X1 + 2);
    out->line_x2_permille = rd_u16(in + LC_CFG_OFF_LINE_X1 + 4);
    out->line_y2_permille = rd_u16(in + LC_CFG_OFF_LINE_X1 + 6);
    out->outside_x_permille = rd_u16(in + LC_CFG_OFF_LINE_X1 + 8);
    out->outside_y_permille = rd_u16(in + LC_CFG_OFF_LINE_X1 + 10);
    out->conf_threshold_permille = rd_u16(in + LC_CFG_OFF_MAX_DIST - 2u);
    out->max_dist_permille = rd_u16(in + LC_CFG_OFF_MAX_DIST);
    out->track_history_k = in[LC_CFG_OFF_TRACK_K];
    out->max_miss = in[LC_CFG_OFF_MAX_MISS];
    out->k_confirm = in[LC_CFG_OFF_K_CONFIRM];
    uint8_t tg = in[LC_CFG_OFF_TOGGLES];
    if (tg & ~0x03u) return LC_CFG_ERR_FLAGS;
    out->tracks_report_enable = (uint8_t)(tg & 0x01u ? 1 : 0);
    out->heat_grid_enable = (uint8_t)(tg & 0x02u ? 1 : 0);
    out->window_minutes = rd_u16(in + LC_CFG_OFF_WINDOW_MIN);

    if (!lc_bus_config_valid(out)) return LC_CFG_ERR_CONTENT;
    return LC_CFG_OK;
}

const char *lc_cfg_status_name(lc_cfg_status_t st)
{
    switch (st) {
    case LC_CFG_OK: return "ok";
    case LC_CFG_ERR_STATE_BLOB: return "state_blob_shape";
    case LC_CFG_ERR_SIZE: return "size";
    case LC_CFG_ERR_MAGIC: return "magic";
    case LC_CFG_ERR_SCHEMA: return "schema";
    case LC_CFG_ERR_FLAGS: return "flags";
    case LC_CFG_ERR_NAME: return "name";
    case LC_CFG_ERR_CONTENT: return "content";
    default: return "unknown";
    }
}
