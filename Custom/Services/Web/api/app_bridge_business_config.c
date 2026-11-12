#include <string.h>
#include <stdint.h>

#include "app_bridge_business_config.h"

#include "cJSON.h"
#include "aicam_types.h"
#include "app_host_control.h"
#include "ai_service.h"

static void app_bridge_cfg_put_u16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void app_bridge_cfg_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static aicam_bool_t app_bridge_cfg_name(const cJSON *obj, const char *key,
                                        uint8_t *dst, uint32_t slot,
                                        uint8_t *len_out)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    const char *text;
    uint32_t len;
    uint32_t i;

    if (item == NULL || !cJSON_IsString(item)) {
        return AICAM_FALSE;
    }
    text = item->valuestring;
    len = (uint32_t)strlen(text);
    if (len == 0u || len >= slot) {
        return AICAM_FALSE;
    }
    for (i = 0u; i < len; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c == '"' || c == '\\' || c == 0x7Fu) {
            return AICAM_FALSE;
        }
    }
    memset(dst, 0, slot);
    memcpy(dst, text, len);
    *len_out = (uint8_t)len;
    return AICAM_TRUE;
}

static aicam_bool_t app_bridge_cfg_u32(const cJSON *obj, const char *key,
                                       uint32_t lo, uint32_t hi, uint32_t *out)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);

    if (item == NULL || !cJSON_IsNumber(item)) {
        return AICAM_FALSE;
    }
    if (item->valuedouble < (double)lo || item->valuedouble > (double)hi ||
        item->valuedouble != (double)(uint32_t)item->valuedouble) {
        return AICAM_FALSE;
    }
    *out = (uint32_t)item->valuedouble;
    return AICAM_TRUE;
}

static aicam_bool_t app_bridge_cfg_encode(const cJSON *candidate, uint8_t out[APP_BRIDGE_LC_CFG_SIZE])
{
    uint32_t schema;
    uint32_t x1, y1, x2, y2, ox, oy;
    uint32_t conf, dist, track_k, max_miss, k_confirm, window_min;
    const cJSON *line_item;
    const cJSON *flag;
    uint8_t target_len = 0u;
    uint8_t counter_len = 0u;
    uint8_t toggles = 0u;

    if (!app_bridge_cfg_u32(candidate, "schema", 1u, 1u, &schema)) {
        return AICAM_FALSE;
    }
    line_item = cJSON_GetObjectItem(candidate, "line");
    if (line_item == NULL || !cJSON_IsObject(line_item)) {
        return AICAM_FALSE;
    }
    if (!app_bridge_cfg_u32(line_item, "x1", 0u, 1000u, &x1) ||
        !app_bridge_cfg_u32(line_item, "y1", 0u, 1000u, &y1) ||
        !app_bridge_cfg_u32(line_item, "x2", 0u, 1000u, &x2) ||
        !app_bridge_cfg_u32(line_item, "y2", 0u, 1000u, &y2) ||
        !app_bridge_cfg_u32(line_item, "outside_x", 0u, 1000u, &ox) ||
        !app_bridge_cfg_u32(line_item, "outside_y", 0u, 1000u, &oy)) {
        return AICAM_FALSE;
    }
    if (x1 == x2 && y1 == y2) {
        return AICAM_FALSE;
    }
    if (!app_bridge_cfg_u32(candidate, "conf_threshold_permille", 0u, 1000u, &conf) ||
        !app_bridge_cfg_u32(candidate, "max_dist_permille", 1u, 1000u, &dist) ||
        !app_bridge_cfg_u32(candidate, "track_history_k", 4u, 16u, &track_k) ||
        !app_bridge_cfg_u32(candidate, "max_miss", 1u, 255u, &max_miss) ||
        !app_bridge_cfg_u32(candidate, "k_confirm", 1u, 255u, &k_confirm) ||
        !app_bridge_cfg_u32(candidate, "window_minutes", 1u, 1440u, &window_min)) {
        return AICAM_FALSE;
    }
    flag = cJSON_GetObjectItem(candidate, "tracks_report_enable");
    if (flag == NULL || !cJSON_IsBool(flag)) {
        return AICAM_FALSE;
    }
    if (cJSON_IsTrue(flag)) {
        toggles |= 0x01u;
    }
    flag = cJSON_GetObjectItem(candidate, "heat_grid_enable");
    if (flag == NULL || !cJSON_IsBool(flag)) {
        return AICAM_FALSE;
    }
    if (cJSON_IsTrue(flag)) {
        toggles |= 0x02u;
    }

    memset(out, 0, APP_BRIDGE_LC_CFG_SIZE);
    if (!app_bridge_cfg_name(candidate, "target_class_name",
                             out + 17u, 32u, &target_len)) {
        return AICAM_FALSE;
    }
    if (!app_bridge_cfg_name(candidate, "counter_name",
                             out + 50u, 64u, &counter_len)) {
        return AICAM_FALSE;
    }
    app_bridge_cfg_put_u32(out + 0u, 0x3343434Cu);
    app_bridge_cfg_put_u32(out + 4u, 1u);
    app_bridge_cfg_put_u32(out + 8u, APP_BRIDGE_LC_CFG_SIZE);
    app_bridge_cfg_put_u32(out + 12u, 0u);
    out[16u] = target_len;
    out[49u] = counter_len;
    app_bridge_cfg_put_u16(out + 114u, x1);
    app_bridge_cfg_put_u16(out + 116u, y1);
    app_bridge_cfg_put_u16(out + 118u, x2);
    app_bridge_cfg_put_u16(out + 120u, y2);
    app_bridge_cfg_put_u16(out + 122u, ox);
    app_bridge_cfg_put_u16(out + 124u, oy);
    app_bridge_cfg_put_u16(out + 126u, conf);
    app_bridge_cfg_put_u16(out + 128u, dist);
    out[130u] = (uint8_t)track_k;
    out[131u] = (uint8_t)max_miss;
    out[132u] = (uint8_t)k_confirm;
    out[133u] = toggles;
    app_bridge_cfg_put_u16(out + 134u, window_min);
    return AICAM_TRUE;
}

aicam_bool_t app_bridge_business_config_encode(const cJSON *candidate,
                                               uint8_t *out, size_t out_cap)
{
    if (candidate == NULL || out == NULL || out_cap < (size_t)APP_BRIDGE_LC_CFG_SIZE) {
        return AICAM_FALSE;
    }
    return app_bridge_cfg_encode(candidate, out);
}

uint32_t app_bridge_business_config_conf_permille(const uint8_t *blob,
                                                  size_t len)
{
    if (blob == NULL || len < (size_t)APP_BRIDGE_LC_CFG_SIZE) {
        return 0u;
    }
    return (uint32_t)blob[126u] | ((uint32_t)blob[127u] << 8);
}

#define APP_BRIDGE_CONF_MIRROR_RID_BASE 0x88000000u

static uint32_t g_app_bridge_conf_mirror_rid = APP_BRIDGE_CONF_MIRROR_RID_BASE;

static uint32_t app_bridge_conf_host_permille(void)
{
    uint32_t percent = ai_get_confidence_threshold();

    if (percent > 100u) {
        percent = 100u;
    }
    return percent * 10u;
}

static aicam_bool_t app_bridge_cfg_move_num(const cJSON *src, const char *src_key,
                                            cJSON *dst, const char *dst_key)
{
    const cJSON *item = cJSON_GetObjectItem(src, src_key);

    if (item == NULL || !cJSON_IsNumber(item)) {
        return AICAM_FALSE;
    }
    return (cJSON_AddNumberToObject(dst, dst_key, item->valuedouble) != NULL)
               ? AICAM_TRUE
               : AICAM_FALSE;
}

static aicam_bool_t app_bridge_cfg_move_str(const cJSON *src, const char *key,
                                            cJSON *dst)
{
    const cJSON *item = cJSON_GetObjectItem(src, key);

    if (item == NULL || !cJSON_IsString(item)) {
        return AICAM_FALSE;
    }
    return (cJSON_AddStringToObject(dst, key, item->valuestring) != NULL)
               ? AICAM_TRUE
               : AICAM_FALSE;
}

static aicam_bool_t app_bridge_cfg_move_bool(const cJSON *src, const char *key,
                                             cJSON *dst)
{
    const cJSON *item = cJSON_GetObjectItem(src, key);

    if (item == NULL || !cJSON_IsBool(item)) {
        return AICAM_FALSE;
    }
    return (cJSON_AddBoolToObject(dst, key, cJSON_IsTrue(item) ? 1 : 0) != NULL)
               ? AICAM_TRUE
               : AICAM_FALSE;
}

static void app_bridge_conf_mirror_register(const cJSON *active,
                                            uint32_t expected_gen)
{
    const cJSON *line = cJSON_GetObjectItem(active, "line");
    const cJSON *outside = cJSON_GetObjectItem(active, "outside");
    cJSON *candidate;
    cJSON *cand_line;
    uint8_t blob[APP_BRIDGE_LC_CFG_SIZE];
    app_control_request_t req;
    aicam_bool_t ok;

    if (line == NULL || !cJSON_IsObject(line) ||
        outside == NULL || !cJSON_IsObject(outside)) {
        return;
    }
    candidate = cJSON_CreateObject();
    cand_line = cJSON_CreateObject();
    if (candidate == NULL || cand_line == NULL) {
        cJSON_Delete(candidate);
        cJSON_Delete(cand_line);
        return;
    }
    ok = app_bridge_cfg_move_num(line, "x1_permille", cand_line, "x1");
    ok = ok && app_bridge_cfg_move_num(line, "y1_permille", cand_line, "y1");
    ok = ok && app_bridge_cfg_move_num(line, "x2_permille", cand_line, "x2");
    ok = ok && app_bridge_cfg_move_num(line, "y2_permille", cand_line, "y2");
    ok = ok && app_bridge_cfg_move_num(outside, "x_permille", cand_line, "outside_x");
    ok = ok && app_bridge_cfg_move_num(outside, "y_permille", cand_line, "outside_y");
    ok = ok && (cJSON_AddNumberToObject(candidate, "schema", 1.0) != NULL);
    ok = ok && app_bridge_cfg_move_str(active, "counter_name", candidate);
    ok = ok && app_bridge_cfg_move_str(active, "target_class_name", candidate);
    ok = ok && app_bridge_cfg_move_num(active, "conf_threshold_permille", candidate,
                                       "conf_threshold_permille");
    ok = ok && app_bridge_cfg_move_num(active, "max_dist_permille", candidate,
                                       "max_dist_permille");
    ok = ok && app_bridge_cfg_move_num(active, "track_history_k", candidate,
                                       "track_history_k");
    ok = ok && app_bridge_cfg_move_num(active, "max_miss", candidate, "max_miss");
    ok = ok && app_bridge_cfg_move_num(active, "k_confirm", candidate, "k_confirm");
    ok = ok && app_bridge_cfg_move_num(active, "window_minutes", candidate,
                                       "window_minutes");
    ok = ok && app_bridge_cfg_move_bool(active, "tracks_report_enable", candidate);
    ok = ok && app_bridge_cfg_move_bool(active, "heat_grid_enable", candidate);
    if (ok == AICAM_TRUE) {
        if (cJSON_AddItemToObject(candidate, "line", cand_line) != 0) {
            cand_line = NULL;
        } else {
            ok = AICAM_FALSE;
        }
    }
    if (ok == AICAM_TRUE &&
        app_bridge_cfg_encode(candidate, blob) == AICAM_TRUE) {
        memset(&req, 0, sizeof(req));
        g_app_bridge_conf_mirror_rid++;
        req.request_id = g_app_bridge_conf_mirror_rid;
        req.msg_type = APP_CONTROL_MSG_TYPE_CONFIG_APPLY;
        req.payload = blob;
        req.payload_len = APP_BRIDGE_LC_CFG_SIZE;
        req.expected_config_generation = expected_gen;
        (void)app_host_control_register(&req);
    }
    cJSON_Delete(candidate);
    cJSON_Delete(cand_line);
}

char *app_bridge_business_snapshot_readthrough(uint8_t *payload_buf,
                                               uint32_t payload_len,
                                               uint32_t config_generation)
{
    cJSON *parsed;
    cJSON *active;
    cJSON *conf;
    char *text = NULL;
    uint32_t host_permille;

    (void)payload_len;
    parsed = cJSON_Parse((const char *)payload_buf);
    if (parsed == NULL) {
        return NULL;
    }
    active = cJSON_GetObjectItem(parsed, "active_config");
    conf = (active != NULL && cJSON_IsObject(active))
               ? cJSON_GetObjectItem(active, "conf_threshold_permille")
               : NULL;
    if (conf != NULL && cJSON_IsNumber(conf)) {
        host_permille = app_bridge_conf_host_permille();
        if ((uint32_t)conf->valueint != host_permille) {
            if (cJSON_ReplaceItemInObject(
                    active, "conf_threshold_permille",
                    cJSON_CreateNumber((double)host_permille)) == 1) {
                app_bridge_conf_mirror_register(active, config_generation);
                text = cJSON_PrintUnformatted(parsed);
            }
        }
    }
    cJSON_Delete(parsed);
    return text;
}

void app_bridge_business_snapshot_target_class(const char *json, char *out,
                                               size_t cap, size_t max_len)
{
    cJSON *root;
    cJSON *config_item;
    cJSON *class_item;
    size_t len;

    out[0] = '\0';
    if (json == NULL) {
        return;
    }
    root = cJSON_Parse(json);
    if (root == NULL) {
        return;
    }
    config_item = cJSON_GetObjectItem(root, "active_config");
    class_item = (config_item != NULL && cJSON_IsObject(config_item))
                     ? cJSON_GetObjectItem(config_item, "target_class_name") : NULL;
    if (class_item == NULL || !cJSON_IsString(class_item)) {
        config_item = cJSON_GetObjectItem(root, "config");
        class_item = (config_item != NULL && cJSON_IsObject(config_item))
                         ? cJSON_GetObjectItem(config_item, "target_class_name") : NULL;
    }
    if (class_item != NULL && cJSON_IsString(class_item) && class_item->valuestring != NULL) {
        len = strlen(class_item->valuestring);
        if (len >= 1u && len <= max_len && len < cap) {
            memcpy(out, class_item->valuestring, len + 1u);
        }
    }
    cJSON_Delete(root);
}
