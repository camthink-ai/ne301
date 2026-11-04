
#include "lc_bus_config.h"
#include "lc_compat.h"

void lc_bus_config_defaults(lc_bus_config_t *cfg)
{
    if (!cfg) return;
    lc_memset(cfg, 0, sizeof(*cfg));

    lc_strlcpy(cfg->counter_name, "\xe5\xae\xa2\xe6\xb5\x81\xe7\xbb\x9f\xe8\xae\xa1",
               sizeof(cfg->counter_name));
    lc_strlcpy(cfg->target_class_name, "person", sizeof(cfg->target_class_name));
    cfg->line_x1_permille = 200u;
    cfg->line_y1_permille = 500u;
    cfg->line_x2_permille = 800u;
    cfg->line_y2_permille = 500u;
    cfg->outside_x_permille = 500u;
    cfg->outside_y_permille = 200u;
    cfg->conf_threshold_permille = 250u;
    cfg->max_dist_permille = 250u;
    cfg->track_history_k = 8u;
    cfg->max_miss = 5u;
    cfg->k_confirm = 5u;
    cfg->window_minutes = 5u;
    cfg->tracks_report_enable = 1u;
    cfg->heat_grid_enable = 0u;
}

int lc_bus_config_valid(const lc_bus_config_t *cfg)
{
    if (!cfg) return 0;
    if (cfg->counter_name[0] == '\0') return 0;
    if (!lc_bus_utf8_valid(cfg->counter_name)) return 0;
    if (cfg->target_class_name[0] == '\0') return 0;
    if (!lc_bus_utf8_valid(cfg->target_class_name)) return 0;
    if (lc_strlen(cfg->target_class_name) > LC_TARGET_CLASS_NAME_LEN - 1u) return 0;
    if (cfg->line_x1_permille > 1000u || cfg->line_y1_permille > 1000u ||
        cfg->line_x2_permille > 1000u || cfg->line_y2_permille > 1000u ||
        cfg->outside_x_permille > 1000u || cfg->outside_y_permille > 1000u) return 0;
    if (cfg->line_x1_permille == cfg->line_x2_permille &&
        cfg->line_y1_permille == cfg->line_y2_permille) return 0;
    if (cfg->conf_threshold_permille > 1000u) return 0;
    if (cfg->max_dist_permille == 0u || cfg->max_dist_permille > 1000u) return 0;
    if (cfg->track_history_k < 4u || cfg->track_history_k > 16u) return 0;
    if (cfg->max_miss == 0u) return 0;
    if (cfg->k_confirm == 0u) return 0;
    if (cfg->window_minutes == 0u || cfg->window_minutes > 1440u) return 0;
    return 1;
}

int lc_bus_utf8_valid(const char *s)
{
    if (!s) return 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p != 0) {
        uint32_t cp = 0;
        uint8_t need = 0;
        uint8_t len = 0;
        if (*p < 0x80u) {
            p++;
            continue;
        } else if ((*p & 0xE0u) == 0xC0u) {
            need = 1; len = 2; cp = (uint32_t)(*p & 0x1Fu);
        } else if ((*p & 0xF0u) == 0xE0u) {
            need = 2; len = 3; cp = (uint32_t)(*p & 0x0Fu);
        } else if ((*p & 0xF8u) == 0xF0u) {
            need = 3; len = 4; cp = (uint32_t)(*p & 0x07u);
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
        if ((len == 2 && cp < 0x80u) || (len == 3 && cp < 0x800u) ||
            (len == 4 && cp < 0x10000u)) return 0;
    }
    return 1;
}
