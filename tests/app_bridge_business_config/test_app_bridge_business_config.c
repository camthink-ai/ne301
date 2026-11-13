#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "app_bridge_business_config.h"
#include "app_host_control.h"
#include "cJSON.h"

static int g_total;
static int g_fail;

static uint32_t g_host_threshold = 50u;
static uint32_t g_register_calls;
static app_control_request_t g_last_req;
static uint8_t g_last_payload[APP_CONTROL_MSG_RAM_MAX];

#define CHECK(cond) \
    do { \
        g_total++; \
        if (!(cond)) { \
            g_fail++; \
            printf("FAIL app_bridge_business_config:%d:%d %s\n", g_total, __LINE__, #cond); \
        } \
    } while (0)

uint32_t ai_get_confidence_threshold(void)
{
    return g_host_threshold;
}

app_control_register_result_t app_host_control_register(const app_control_request_t *req)
{
    g_register_calls++;
    memcpy(&g_last_req, req, sizeof(*req));
    memset(g_last_payload, 0, sizeof(g_last_payload));
    if (req->payload != NULL && req->payload_len <= APP_CONTROL_MSG_RAM_MAX) {
        memcpy(g_last_payload, req->payload, req->payload_len);
    }
    return APP_CONTROL_REG_OK;
}

static void reset_all(void)
{
    g_host_threshold = 50u;
    g_register_calls = 0u;
    memset(&g_last_req, 0, sizeof(g_last_req));
    memset(g_last_payload, 0, sizeof(g_last_payload));
}

static const char CANDIDATE_FULL[] =
    "{\"schema\":1,\"counter_name\":\"Counter One\",\"target_class_name\":\"person\","
    "\"line\":{\"x1\":250,\"y1\":128,\"x2\":750,\"y2\":500,\"outside_x\":500,"
    "\"outside_y\":350},\"conf_threshold_permille\":550,\"max_dist_permille\":350,"
    "\"track_history_k\":8,\"max_miss\":12,\"k_confirm\":2,\"tracks_report_enable\":true,"
    "\"heat_grid_enable\":false,\"window_minutes\":60}";

static uint16_t peek_u16(const uint8_t *b, size_t off)
{
    return (uint16_t)((uint16_t)b[off] | ((uint16_t)b[off + 1u] << 8));
}

static uint32_t peek_u32(const uint8_t *b, size_t off)
{
    return (uint32_t)b[off] | ((uint32_t)b[off + 1u] << 8) |
           ((uint32_t)b[off + 2u] << 16) | ((uint32_t)b[off + 3u] << 24);
}

static cJSON *parse_candidate(const char *text)
{
    cJSON *root = cJSON_Parse(text);

    CHECK(root != NULL);
    return root;
}

static void test_encode_golden_layout(void)
{
    cJSON *candidate = parse_candidate(CANDIDATE_FULL);
    uint8_t blob[APP_BRIDGE_LC_CFG_SIZE];
    size_t i;

    memset(blob, 0xAA, sizeof(blob));
    CHECK(app_bridge_business_config_encode(candidate, blob, sizeof(blob)) == 1);
    CHECK(peek_u32(blob, 0u) == 0x3343434Cu);
    CHECK(peek_u32(blob, 4u) == 1u);
    CHECK(peek_u32(blob, 8u) == 144u);
    CHECK(peek_u32(blob, 12u) == 0u);
    CHECK(blob[16u] == 6u);
    CHECK(memcmp(blob + 17u, "person", 6u) == 0);
    for (i = 23u; i < 49u; i++) {
        CHECK(blob[i] == 0u);
    }
    CHECK(blob[49u] == 11u);
    CHECK(memcmp(blob + 50u, "Counter One", 11u) == 0);
    for (i = 61u; i < 114u; i++) {
        CHECK(blob[i] == 0u);
    }
    CHECK(peek_u16(blob, 114u) == 250u);
    CHECK(peek_u16(blob, 116u) == 128u);
    CHECK(peek_u16(blob, 118u) == 750u);
    CHECK(peek_u16(blob, 120u) == 500u);
    CHECK(peek_u16(blob, 122u) == 500u);
    CHECK(peek_u16(blob, 124u) == 350u);
    CHECK(peek_u16(blob, 126u) == 550u);
    CHECK(peek_u16(blob, 128u) == 350u);
    CHECK(blob[130u] == 8u);
    CHECK(blob[131u] == 12u);
    CHECK(blob[132u] == 2u);
    CHECK(blob[133u] == 0x01u);
    CHECK(peek_u16(blob, 134u) == 60u);
    for (i = 136u; i < 144u; i++) {
        CHECK(blob[i] == 0u);
    }
    cJSON_Delete(candidate);
}

static void test_encode_toggles(void)
{
    cJSON *candidate = parse_candidate(
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":0,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":false,\"heat_grid_enable\":true,"
        "\"window_minutes\":1440}");
    uint8_t blob[APP_BRIDGE_LC_CFG_SIZE];

    CHECK(app_bridge_business_config_encode(candidate, blob, sizeof(blob)) == 1);
    CHECK(blob[133u] == 0x02u);
    CHECK(peek_u16(blob, 126u) == 0u);
    CHECK(peek_u16(blob, 134u) == 1440u);
    cJSON_Delete(candidate);
}

static void test_encode_rejections(void)
{
    const char *bad[] = {
        "{\"schema\":2,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":5,\"y1\":5,\"x2\":5,\"y2\":5,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1001,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":1001,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":0,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":3,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":17,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":0,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":256,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":0}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1441}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10.5,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":\"yes\",\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"target_class_name\":\"t\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"per\\\"son\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"counter_name\":\"C\",\"target_class_name\":\"per\\\\son\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"target_class_name\":\"012345678901234567890123456789012\","
        "\"counter_name\":\"C\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}",
        "{\"schema\":1,\"target_class_name\":\"t\","
        "\"counter_name\":\"0123456789012345678901234567890123456789"
        "012345678901234567890123\","
        "\"line\":{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"outside_x\":5,"
        "\"outside_y\":6},\"conf_threshold_permille\":10,\"max_dist_permille\":1,"
        "\"track_history_k\":4,\"max_miss\":1,\"k_confirm\":1,"
        "\"tracks_report_enable\":true,\"heat_grid_enable\":true,"
        "\"window_minutes\":1}"
    };
    uint8_t blob[APP_BRIDGE_LC_CFG_SIZE];
    size_t i;

    for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
        cJSON *candidate = cJSON_Parse(bad[i]);

        CHECK(candidate != NULL);
        if (candidate != NULL) {
            CHECK(app_bridge_business_config_encode(candidate, blob,
                                                    sizeof(blob)) == 0);
            cJSON_Delete(candidate);
        }
    }
    CHECK(app_bridge_business_config_encode(NULL, blob, sizeof(blob)) == 0);
    {
        cJSON *candidate = parse_candidate(CANDIDATE_FULL);

        CHECK(app_bridge_business_config_encode(candidate, NULL, sizeof(blob)) == 0);
        CHECK(app_bridge_business_config_encode(
                  candidate, blob, APP_BRIDGE_LC_CFG_SIZE - 1u) == 0);
        cJSON_Delete(candidate);
    }
}

static void test_conf_permille(void)
{
    uint8_t blob[APP_BRIDGE_LC_CFG_SIZE];

    memset(blob, 0, sizeof(blob));
    blob[126u] = 0x26u;
    blob[127u] = 0x02u;
    CHECK(app_bridge_business_config_conf_permille(blob, sizeof(blob)) == 550u);
    CHECK(app_bridge_business_config_conf_permille(NULL, sizeof(blob)) == 0u);
    CHECK(app_bridge_business_config_conf_permille(blob, 143u) == 0u);
}

static const char SNAPSHOT_MIRROR[] =
    "{\"schema\":\"line-crossing/1\",\"config_generation\":6,"
    "\"active_config\":{\"counter_name\":\"Counter One\","
    "\"target_class_name\":\"person\",\"conf_threshold_permille\":550,"
    "\"max_dist_permille\":350,\"track_history_k\":8,\"max_miss\":12,"
    "\"k_confirm\":2,\"tracks_report_enable\":true,\"heat_grid_enable\":false,"
    "\"window_minutes\":60,"
    "\"line\":{\"x1_permille\":250,\"y1_permille\":128,\"x2_permille\":750,"
    "\"y2_permille\":500},"
    "\"outside\":{\"x_permille\":500,\"y_permille\":350}}}";

static void test_readthrough_rewrites_and_mirrors(void)
{
    uint8_t buf[512];
    char *text;
    cJSON *parsed;
    cJSON *conf;

    reset_all();
    memcpy(buf, SNAPSHOT_MIRROR, sizeof(SNAPSHOT_MIRROR));
    text = app_bridge_business_snapshot_readthrough(buf, (uint32_t)strlen(SNAPSHOT_MIRROR), 6u);
    CHECK(text != NULL);
    if (text != NULL) {
        parsed = cJSON_Parse(text);
        CHECK(parsed != NULL);
        conf = cJSON_GetObjectItem(cJSON_GetObjectItem(parsed, "active_config"),
                                   "conf_threshold_permille");
        CHECK(conf != NULL && cJSON_IsNumber(conf) && conf->valueint == 500);
        cJSON_Delete(parsed);
        free(text);
    }
    CHECK(g_register_calls == 1u);
    CHECK(g_last_req.msg_type == APP_CONTROL_MSG_TYPE_CONFIG_APPLY);
    CHECK(g_last_req.payload_len == APP_BRIDGE_LC_CFG_SIZE);
    CHECK(g_last_req.expected_config_generation == 6u);
    CHECK(g_last_req.request_id > 0x88000000u);
    CHECK(g_last_payload[16u] == 6u);
    CHECK(peek_u16(g_last_payload, 114u) == 250u);
    CHECK(peek_u16(g_last_payload, 126u) == 500u);
    CHECK(peek_u16(g_last_payload, 134u) == 60u);
    CHECK(memcmp(g_last_payload + 17u, "person", 6u) == 0);
}

static void test_readthrough_equal_returns_null(void)
{
    uint8_t buf[512];
    char *text;

    reset_all();
    g_host_threshold = 55u;
    memcpy(buf, SNAPSHOT_MIRROR, sizeof(SNAPSHOT_MIRROR));
    text = app_bridge_business_snapshot_readthrough(buf, (uint32_t)strlen(SNAPSHOT_MIRROR), 6u);
    CHECK(text == NULL);
    CHECK(g_register_calls == 0u);
}

static void test_readthrough_no_rewrite_paths(void)
{
    uint8_t buf[512];
    const char *no_conf =
        "{\"schema\":\"line-crossing/1\",\"active_config\":{\"counter_name\":\"C\"}}";
    const char *no_active =
        "{\"schema\":\"line-crossing/1\",\"config\":{}}";
    const char *not_json = "not-json";
    char *text;

    reset_all();
    memcpy(buf, no_conf, strlen(no_conf) + 1u);
    text = app_bridge_business_snapshot_readthrough(buf, (uint32_t)strlen(no_conf), 6u);
    CHECK(text == NULL);
    CHECK(g_register_calls == 0u);

    memcpy(buf, no_active, strlen(no_active) + 1u);
    text = app_bridge_business_snapshot_readthrough(buf, (uint32_t)strlen(no_active), 6u);
    CHECK(text == NULL);
    CHECK(g_register_calls == 0u);

    memcpy(buf, not_json, strlen(not_json) + 1u);
    text = app_bridge_business_snapshot_readthrough(buf, (uint32_t)strlen(not_json), 6u);
    CHECK(text == NULL);
    CHECK(g_register_calls == 0u);
}

static void test_readthrough_host_clamp(void)
{
    uint8_t buf[512];
    char *text;

    reset_all();
    g_host_threshold = 250u;
    memcpy(buf, SNAPSHOT_MIRROR, sizeof(SNAPSHOT_MIRROR));
    text = app_bridge_business_snapshot_readthrough(buf, (uint32_t)strlen(SNAPSHOT_MIRROR), 9u);
    CHECK(text != NULL);
    if (text != NULL) {
        cJSON *parsed = cJSON_Parse(text);
        cJSON *conf = cJSON_GetObjectItem(
            cJSON_GetObjectItem(parsed, "active_config"), "conf_threshold_permille");
        CHECK(conf != NULL && cJSON_IsNumber(conf) && conf->valueint == 1000);
        cJSON_Delete(parsed);
        free(text);
    }
    CHECK(g_register_calls == 1u);
    CHECK(g_last_req.expected_config_generation == 9u);
    CHECK(peek_u16(g_last_payload, 126u) == 1000u);
}

#define TEST_TARGET_CLASS_CAP 65u

static void test_target_class_projection(void)
{
    char out[TEST_TARGET_CLASS_CAP];

    app_bridge_business_snapshot_target_class(
        "{\"active_config\":{\"target_class_name\":\"person\"}}",
        out, sizeof(out), 64u);
    CHECK(strcmp(out, "person") == 0);

    app_bridge_business_snapshot_target_class(
        "{\"config\":{\"target_class_name\":\"car\"}}", out, sizeof(out), 64u);
    CHECK(strcmp(out, "car") == 0);

    app_bridge_business_snapshot_target_class(
        "{\"active_config\":{\"target_class_name\":\"person\"},"
        "\"config\":{\"target_class_name\":\"car\"}}", out, sizeof(out), 64u);
    CHECK(strcmp(out, "person") == 0);

    app_bridge_business_snapshot_target_class("{\"active_config\":{}}",
                                              out, sizeof(out), 64u);
    CHECK(out[0] == '\0');

    app_bridge_business_snapshot_target_class(NULL, out, sizeof(out), 64u);
    CHECK(out[0] == '\0');

    app_bridge_business_snapshot_target_class("bad json", out, sizeof(out), 64u);
    CHECK(out[0] == '\0');

    app_bridge_business_snapshot_target_class(
        "{\"active_config\":{\"target_class_name\":\"\"}}",
        out, sizeof(out), 64u);
    CHECK(out[0] == '\0');

    app_bridge_business_snapshot_target_class(
        "{\"active_config\":{\"target_class_name\":"
        "\"012345678901234567890123456789012345678901234567890123456789012345\"}}",
        out, sizeof(out), 64u);
    CHECK(out[0] == '\0');

    app_bridge_business_snapshot_target_class(
        "{\"active_config\":{\"target_class_name\":\"0123456789012345678901234567890\"}}",
        out, sizeof(out), 64u);
    CHECK(strcmp(out, "0123456789012345678901234567890") == 0);

    app_bridge_business_snapshot_target_class(
        "{\"active_config\":{\"target_class_name\":7}}", out, sizeof(out), 64u);
    CHECK(out[0] == '\0');

    app_bridge_business_snapshot_target_class(
        "{\"active_config\":{\"target_class_name\":\"person\"}}",
        out, 7u, 64u);
    CHECK(strcmp(out, "person") == 0);

    app_bridge_business_snapshot_target_class(
        "{\"active_config\":{\"target_class_name\":\"person\"}}",
        out, 6u, 64u);
    CHECK(out[0] == '\0');
}

int main(void)
{
    test_encode_golden_layout();
    test_encode_toggles();
    test_encode_rejections();
    test_conf_permille();
    test_readthrough_rewrites_and_mirrors();
    test_readthrough_equal_returns_null();
    test_readthrough_no_rewrite_paths();
    test_readthrough_host_clamp();
    test_target_class_projection();
    printf("app_bridge_business_config: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
