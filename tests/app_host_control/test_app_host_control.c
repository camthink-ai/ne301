#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "app_host_control.h"
#include "app_host_session.h"
#include "app_admission.h"
#include "app_installer.h"
#include "app_host.h"
#include "app_trust_anchor.h"
#include "app_state_store.h"
#include "app_report_service.h"
#include "app_report_service_device.h"
#include "control_vectors.h"
#include "session_vectors.h"
#include "mem_io_backend.h"

#define TARGET_BASE 0x93E00000u
#define STAGE_CAP (256u * 1024u)

static int g_total;
static int g_fail;
static aicam_bool_t g_report_worker_active;
static uint32_t g_enable_precommit_calls;
static uint32_t g_enable_receipt_calls;
static int32_t g_enable_precommit_result;
static int32_t g_enable_receipt_result;

aicam_result_t app_report_service_device_start(void)
{
    g_report_worker_active = AICAM_TRUE;
    return AICAM_OK;
}

aicam_result_t app_report_service_device_stop(void)
{
    g_report_worker_active = AICAM_FALSE;
    (void)app_report_service_set_session(NULL);
    app_report_service_restart();
    return AICAM_OK;
}

aicam_bool_t app_report_service_device_running(void)
{
    return g_report_worker_active;
}

static void check_impl(int cond, int line)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL control:%d line=%d\n", g_total, line);
    }
}

static void check_msg_impl(int cond, int line, const char *what)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL control:%d line=%d: %s\n", g_total, line, what);
    }
}

#define CHECK(cond) check_impl((cond), __LINE__)
#define CHECK_MSG(cond, what) check_msg_impl((cond), __LINE__, (what))

static void sleep_ms_short(uint32_t ms)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

static volatile uint32_t g_now_ms;

static uint32_t fake_now_ms(void *ctx)
{
    (void)ctx;
    return g_now_ms;
}

static void fake_advance(uint32_t ms)
{
    g_now_ms += ms;
}

static void *fake_mutex_create(void *ctx)
{
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));

    (void)ctx;
    pthread_mutex_init(m, NULL);
    return m;
}

static void fake_mutex_destroy(void *ctx, void *mtx)
{
    (void)ctx;
    if (mtx != NULL) {
        pthread_mutex_destroy((pthread_mutex_t *)mtx);
        free(mtx);
    }
}

static int fake_mutex_lock(void *ctx, void *mtx)
{
    (void)ctx;
    return pthread_mutex_lock((pthread_mutex_t *)mtx);
}

static void fake_mutex_unlock(void *ctx, void *mtx)
{
    (void)ctx;
    pthread_mutex_unlock((pthread_mutex_t *)mtx);
}

static void ctl_init_fake(void)
{
    app_control_backend_t b;

    memset(&b, 0, sizeof(b));
    b.now_ms = fake_now_ms;
    b.mutex_create = fake_mutex_create;
    b.mutex_destroy = fake_mutex_destroy;
    b.mutex_lock = fake_mutex_lock;
    b.mutex_unlock = fake_mutex_unlock;
    app_host_control_init(&b);
}

static int32_t fake_enable_precommit(void *ctx, uint32_t session_generation,
                                     uint32_t request_id, uint32_t config_generation,
                                     uint32_t expected_state_revision,
                                     uint32_t desired_state)
{
    (void)ctx;
    CHECK(session_generation != 0u);
    CHECK(request_id != 0u);
    CHECK(config_generation != 0u);
    CHECK(expected_state_revision < UINT32_MAX);
    CHECK(desired_state == APP_CONTROL_ENABLE_STATE_ENABLED ||
          desired_state == APP_CONTROL_ENABLE_STATE_PAUSED);
    g_enable_precommit_calls++;
    return g_enable_precommit_result;
}

static int32_t fake_enable_receipt_check(void *ctx, uint32_t session_generation,
                                         uint32_t request_id,
                                         uint32_t config_generation,
                                         uint32_t state_revision,
                                         uint32_t desired_state)
{
    (void)ctx;
    CHECK(session_generation != 0u);
    CHECK(request_id != 0u);
    CHECK(config_generation != 0u);
    CHECK(state_revision != 0u);
    CHECK(desired_state == APP_CONTROL_ENABLE_STATE_ENABLED ||
          desired_state == APP_CONTROL_ENABLE_STATE_PAUSED);
    g_enable_receipt_calls++;
    return g_enable_receipt_result;
}

static void ctl_init_enable_fake(void)
{
    app_control_backend_t b;

    memset(&b, 0, sizeof(b));
    b.now_ms = fake_now_ms;
    b.mutex_create = fake_mutex_create;
    b.mutex_destroy = fake_mutex_destroy;
    b.mutex_lock = fake_mutex_lock;
    b.mutex_unlock = fake_mutex_unlock;
    b.enable_precommit = fake_enable_precommit;
    b.enable_receipt_check = fake_enable_receipt_check;
    g_enable_precommit_calls = 0u;
    g_enable_receipt_calls = 0u;
    g_enable_precommit_result = APP_CONTROL_OK;
    g_enable_receipt_result = APP_CONTROL_OK;
    app_host_control_init(&b);
}

static uint32_t rd32(const uint8_t *p, uint32_t off)
{
    return (uint32_t)p[off] | ((uint32_t)p[off + 1u] << 8) |
           ((uint32_t)p[off + 2u] << 16) | ((uint32_t)p[off + 3u] << 24);
}

static uint32_t rd16(const uint8_t *p, uint32_t off)
{
    return (uint32_t)p[off] | ((uint32_t)p[off + 1u] << 8);
}

static int32_t ctl_open(uint32_t caps, uint32_t control_max, uint32_t snapshot_max,
                        uint32_t *gen)
{
    app_control_session_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.caps = caps;
    cfg.control_max = control_max;
    cfg.snapshot_max = snapshot_max;
    strcpy(cfg.publisher_id, "test-publisher");
    strcpy(cfg.app_id, "line-crossing");
    return app_host_control_session_open(&cfg, gen);
}

static void build_cmd_header(uint8_t *buf, uint32_t type, uint32_t total,
                             uint32_t gen, uint32_t id, uint32_t cgen, uint32_t flags)
{
    memset(buf, 0, APP_CONTROL_HEADER_SIZE);
    buf[0] = 0x4Eu; buf[1] = 0x46u; buf[2] = 0x43u; buf[3] = 0x33u;
    buf[4] = 0x01u; buf[5] = 0x00u;
    buf[6] = (uint8_t)(type & 0xFFu);
    buf[7] = (uint8_t)((type >> 8) & 0xFFu);
    buf[8] = (uint8_t)(total & 0xFFu);
    buf[9] = (uint8_t)((total >> 8) & 0xFFu);
    buf[10] = (uint8_t)((total >> 16) & 0xFFu);
    buf[11] = (uint8_t)((total >> 24) & 0xFFu);
    buf[12] = (uint8_t)(gen & 0xFFu);
    buf[13] = (uint8_t)((gen >> 8) & 0xFFu);
    buf[14] = (uint8_t)((gen >> 16) & 0xFFu);
    buf[15] = (uint8_t)((gen >> 24) & 0xFFu);
    buf[16] = (uint8_t)(id & 0xFFu);
    buf[17] = (uint8_t)((id >> 8) & 0xFFu);
    buf[18] = (uint8_t)((id >> 16) & 0xFFu);
    buf[19] = (uint8_t)((id >> 24) & 0xFFu);
    buf[20] = (uint8_t)(cgen & 0xFFu);
    buf[21] = (uint8_t)((cgen >> 8) & 0xFFu);
    buf[22] = (uint8_t)((cgen >> 16) & 0xFFu);
    buf[23] = (uint8_t)((cgen >> 24) & 0xFFu);
    buf[28] = (uint8_t)(flags & 0xFFu);
    buf[29] = (uint8_t)((flags >> 8) & 0xFFu);
    buf[30] = (uint8_t)((flags >> 16) & 0xFFu);
    buf[31] = (uint8_t)((flags >> 24) & 0xFFu);
}

static void build_receipt(uint8_t *buf, uint32_t gen, uint32_t id, uint32_t new_gen,
                          uint32_t rev, uint32_t status)
{
    build_cmd_header(buf, APP_CONTROL_MSG_TYPE_RECEIPT, APP_CONTROL_HEADER_SIZE,
                     gen, id, new_gen, status);
    buf[24] = (uint8_t)(rev & 0xFFu);
    buf[25] = (uint8_t)((rev >> 8) & 0xFFu);
    buf[26] = (uint8_t)((rev >> 16) & 0xFFu);
    buf[27] = (uint8_t)((rev >> 24) & 0xFFu);
}

static uint32_t build_snapshot(uint8_t *buf, uint32_t buf_cap, const char *json,
                               uint32_t cgen, uint32_t rev, uint32_t sg,
                               uint32_t rid, uint32_t flags, uint32_t total_override)
{
    uint32_t plen = (uint32_t)strlen(json);
    uint32_t total = APP_CONTROL_HEADER_SIZE + plen;

    (void)buf_cap;
    memset(buf, 0, APP_CONTROL_HEADER_SIZE + plen);
    build_cmd_header(buf, APP_CONTROL_MSG_TYPE_SNAPSHOT,
                     (total_override != 0u) ? total_override : total,
                     sg, rid, cgen, flags);
    buf[24] = (uint8_t)(rev & 0xFFu);
    buf[25] = (uint8_t)((rev >> 8) & 0xFFu);
    buf[26] = (uint8_t)((rev >> 16) & 0xFFu);
    buf[27] = (uint8_t)((rev >> 24) & 0xFFu);
    if (plen > 0u) {
        memcpy(buf + APP_CONTROL_HEADER_SIZE, json, plen);
    }
    return (total_override != 0u) ? total_override : total;
}

static void test_json_validator(void)
{
    CHECK_MSG(app_host_control_json_object_valid("{\"a\":1}", 7) == AICAM_TRUE,
              "json_simple_object_valid");
    CHECK_MSG(app_host_control_json_object_valid("{}", 2) == AICAM_TRUE,
              "json_empty_object_valid");
    {
        const char nested[] = "{\"a\":{\"b\":[1,2,{\"c\":null}]},\"d\":true,\"e\":\"s\",\"f\":-1.5e-3}";
        CHECK_MSG(app_host_control_json_object_valid(nested, (uint32_t)sizeof(nested) - 1u) == AICAM_TRUE,
                  "json_nested_valid");
    }
    {
        const char multibyte[] = "{\"counter_name\":\"\xe5\xae\xa2\xe6\xb5\x81\xe7\xbb\x9f\xe8\xae\xa1\",\"in\":12}";
        CHECK_MSG(app_host_control_json_object_valid(multibyte, (uint32_t)sizeof(multibyte) - 1u) == AICAM_TRUE,
                  "json_utf8_multibyte_valid");
    }
    {
        const char escapes[] = "{\"a\":\"\\u00e9\\ud83d\\ude00\"}";
        CHECK_MSG(app_host_control_json_object_valid(escapes, (uint32_t)sizeof(escapes) - 1u) == AICAM_TRUE,
                  "json_escapes_valid");
    }
    {
        const char trail_ws[] = "{\"a\":1} ";
        CHECK_MSG(app_host_control_json_object_valid(trail_ws, (uint32_t)sizeof(trail_ws) - 1u) == AICAM_TRUE,
                  "json_trailing_ws_valid");
    }
    {
        const char dupkey[] = "{\"a\":1,\"a\":2}";
        CHECK_MSG(app_host_control_json_object_valid(dupkey, (uint32_t)sizeof(dupkey) - 1u) == AICAM_FALSE,
              "json_duplicate_key_rejected");
    }
    {
        const char dupesc[] = "{\"\\u0041\":1,\"A\":2}";
        CHECK_MSG(app_host_control_json_object_valid(dupesc, (uint32_t)sizeof(dupesc) - 1u) == AICAM_FALSE,
              "json_duplicate_key_escaped_rejected");
    }
    {
        const char twobj[] = "{\"o\":{\"k\":1},\"p\":{\"k\":2}}";
        CHECK_MSG(app_host_control_json_object_valid(twobj, (uint32_t)sizeof(twobj) - 1u) == AICAM_TRUE,
              "json_same_key_different_objects_valid");
    }
    {
        const char dup_nested[] = "{\"o\":{\"k\":1,\"k\":2}}";
        CHECK_MSG(app_host_control_json_object_valid(dup_nested,
                                                     (uint32_t)sizeof(dup_nested) - 1u) == AICAM_FALSE,
                  "json_duplicate_key_nested_rejected");
    }
    {
        const char nan_case[] = "{\"a\":NaN}";
        const char inf_case[] = "{\"a\":Infinity}";
        const char ninf_case[] = "{\"a\":-Infinity}";
        CHECK_MSG(app_host_control_json_object_valid(nan_case,
                                                     (uint32_t)sizeof(nan_case) - 1u) == AICAM_FALSE,
                  "json_nan_rejected");
        CHECK_MSG(app_host_control_json_object_valid(inf_case,
                                                     (uint32_t)sizeof(inf_case) - 1u) == AICAM_FALSE,
                  "json_infinity_rejected");
        CHECK_MSG(app_host_control_json_object_valid(ninf_case,
                                                     (uint32_t)sizeof(ninf_case) - 1u) == AICAM_FALSE,
                  "json_neg_infinity_rejected");
    }
    CHECK_MSG(app_host_control_json_object_valid("{\"a\":1}x", 8) == AICAM_FALSE,
              "json_trailing_data_rejected");
    CHECK_MSG(app_host_control_json_object_valid("[1]", 3) == AICAM_FALSE,
              "json_top_array_rejected");
    CHECK_MSG(app_host_control_json_object_valid("\"s\"", 3) == AICAM_FALSE,
              "json_top_string_rejected");
    CHECK_MSG(app_host_control_json_object_valid("1", 1) == AICAM_FALSE,
              "json_top_number_rejected");
    CHECK_MSG(app_host_control_json_object_valid("{\"a\":01}", 8) == AICAM_FALSE,
              "json_leading_zero_rejected");
    CHECK_MSG(app_host_control_json_object_valid("{\"a\":+1}", 8) == AICAM_FALSE,
              "json_plus_number_rejected");
    CHECK_MSG(app_host_control_json_object_valid("{\"a\":.5}", 8) == AICAM_FALSE,
              "json_dot_number_rejected");
    CHECK_MSG(app_host_control_json_object_valid("{\"a\":1.}", 8) == AICAM_FALSE,
              "json_trailing_dot_rejected");
    CHECK_MSG(app_host_control_json_object_valid("{\"a\":1e}", 8) == AICAM_FALSE,
              "json_bare_exponent_rejected");
    CHECK_MSG(app_host_control_json_object_valid("{\"a\":,}", 8) == AICAM_FALSE,
              "json_garbage_rejected");
    {
        const char bad_escape[] = "{\"a\":\"\\x\"}";
        const char lone_surrogate[] = "{\"a\":\"\\ud800\"}";
        const char lone_low[] = "{\"a\":\"\\udc00\"}";
        CHECK_MSG(app_host_control_json_object_valid(bad_escape,
                                                     (uint32_t)sizeof(bad_escape) - 1u) == AICAM_FALSE,
                  "json_bad_escape_rejected");
        CHECK_MSG(app_host_control_json_object_valid(lone_surrogate,
                                                     (uint32_t)sizeof(lone_surrogate) - 1u) == AICAM_FALSE,
                  "json_lone_high_surrogate_rejected");
        CHECK_MSG(app_host_control_json_object_valid(lone_low,
                                                     (uint32_t)sizeof(lone_low) - 1u) == AICAM_FALSE,
                  "json_lone_low_surrogate_rejected");
    }
    {
        const uint8_t raw_ctrl[] = { 0x7B, 0x22, 0x61, 0x22, 0x3A, 0x22, 0x01, 0x22, 0x7D };
        const uint8_t bad_utf8[] = { 0x7B, 0x22, 0x61, 0x22, 0x3A, 0x22, 0xFF, 0x22, 0x7D };
        const uint8_t overlong[] = { 0x7B, 0x22, 0x61, 0x22, 0x3A, 0x22, 0xC0, 0x80, 0x22, 0x7D };
        CHECK_MSG(app_host_control_json_object_valid(raw_ctrl, sizeof(raw_ctrl)) == AICAM_FALSE,
                  "json_raw_control_char_rejected");
        CHECK_MSG(app_host_control_json_object_valid(bad_utf8, sizeof(bad_utf8)) == AICAM_FALSE,
                  "json_invalid_utf8_rejected");
        CHECK_MSG(app_host_control_json_object_valid(overlong, sizeof(overlong)) == AICAM_FALSE,
                  "json_overlong_utf8_rejected");
    }
    {
        char deep_ok[160];
        char deep_bad[176];
        size_t i;
        size_t pos = 0;

        for (i = 0u; i < 16u; i++) {
            deep_ok[pos++] = '{';
            deep_ok[pos++] = '"';
            deep_ok[pos++] = 'a';
            deep_ok[pos++] = '"';
            deep_ok[pos++] = ':';
        }
        deep_ok[pos++] = '1';
        for (i = 0u; i < 16u; i++) {
            deep_ok[pos++] = '}';
        }
        deep_ok[pos] = '\0';
        CHECK_MSG(app_host_control_json_object_valid(deep_ok, (uint32_t)pos) == AICAM_TRUE,
                  "json_depth_16_valid");

        pos = 0;
        for (i = 0u; i < 17u; i++) {
            deep_bad[pos++] = '{';
            deep_bad[pos++] = '"';
            deep_bad[pos++] = 'a';
            deep_bad[pos++] = '"';
            deep_bad[pos++] = ':';
        }
        deep_bad[pos++] = '1';
        for (i = 0u; i < 17u; i++) {
            deep_bad[pos++] = '}';
        }
        deep_bad[pos] = '\0';
        CHECK_MSG(app_host_control_json_object_valid(deep_bad, (uint32_t)pos) == AICAM_FALSE,
                  "json_depth_17_rejected");
    }
    CHECK_MSG(app_host_control_json_object_valid(NULL, 4) == AICAM_FALSE,
              "json_null_rejected");
    CHECK_MSG(app_host_control_json_object_valid("{}", 0) == AICAM_FALSE,
              "json_empty_rejected");
}

static void test_open_and_generation(void)
{
    uint32_t gen = 0u;
    app_control_session_cfg_t empty_cfg;

    ctl_init_fake();
    memset(&empty_cfg, 0, sizeof(empty_cfg));
    CHECK(app_host_control_session_open(NULL, &gen) == APP_CONTROL_INVALID_ARGUMENT);
    CHECK(app_host_control_session_open(&empty_cfg, NULL) == APP_CONTROL_INVALID_ARGUMENT);
    CHECK(ctl_open(0xC0u, 0u, 0u, &gen) == APP_CONTROL_INVALID_ARGUMENT);
    CHECK(ctl_open(0xC0u, APP_CONTROL_MSG_RAM_MAX + 1u, 0u, &gen) ==
          APP_CONTROL_INVALID_ARGUMENT);
    CHECK(ctl_open(0xC0u, 256u, APP_CONTROL_SNAPSHOT_RAM_MAX + 1u, &gen) ==
          APP_CONTROL_INVALID_ARGUMENT);
    CHECK(ctl_open(0xC0u, 256u, 1024u, &gen) == APP_CONTROL_OK);
    CHECK_MSG(gen == 1u, "first_session_generation_is_one");
    {
        uint32_t gen2 = 0u;

        CHECK(app_host_control_session_open(&empty_cfg, &gen2) ==
              APP_CONTROL_INVALID_ARGUMENT);
    }
    app_host_control_session_close();
    CHECK(ctl_open(0xC0u, 256u, 1024u, &gen) == APP_CONTROL_OK);
    CHECK_MSG(gen == 2u, "generation_monotonic_per_open");
    app_host_control_session_close();
    app_host_control_generation_adopt(APP_CONTROL_GENERATION_LAST);
    CHECK(ctl_open(0xC0u, 256u, 1024u, &gen) == APP_CONTROL_OK);
    CHECK_MSG(gen == APP_CONTROL_GENERATION_LAST, "generation_last_usable_value");
    app_host_control_session_close();
    CHECK_MSG(ctl_open(0xC0u, 256u, 1024u, &gen) == (int32_t)APP_CONTROL_ERR_GENERATION_EXHAUSTED,
              "generation_wrap_refused");
    app_host_control_generation_adopt(1u);
    CHECK(ctl_open(0xC0u, 256u, 1024u, &gen) == APP_CONTROL_OK);
    CHECK(gen == 1u);
    app_host_control_session_close();
}

static void test_v4_live_session_open(void)
{
    uint32_t gen = 0u;
    app_control_session_cfg_t cfg;

    ctl_init_fake();
    memset(&cfg, 0, sizeof(cfg));
    cfg.protocol_major = 4u;
    cfg.caps = APP_ADMISSION_V4_CAP_LIVE_PUBLISH;
    strcpy(cfg.publisher_id, "test-publisher");
    strcpy(cfg.app_id, "line-crossing");
    CHECK_MSG(app_host_control_session_open(&cfg, &gen) == APP_CONTROL_OK,
              "v4_live_open_zero_quota");
    CHECK(gen == 1u);
    app_host_control_session_close();
    cfg.control_max = 32u;
    CHECK_MSG(app_host_control_session_open(&cfg, &gen) ==
                  APP_CONTROL_INVALID_ARGUMENT,
              "v4_live_nonzero_control_quota_refused");
    cfg.control_max = 0u;
    cfg.snapshot_max = 64u;
    CHECK_MSG(app_host_control_session_open(&cfg, &gen) ==
                  APP_CONTROL_INVALID_ARGUMENT,
              "v4_live_nonzero_snapshot_quota_refused");
    cfg.snapshot_max = 0u;
    cfg.caps = APP_ADMISSION_V4_CAP_ENABLE_CONTROL;
    CHECK_MSG(app_host_control_session_open(&cfg, &gen) ==
                  APP_CONTROL_UNAUTHORIZED,
              "v4_enable_control_still_unauthorized");
    cfg.caps = APP_ADMISSION_V4_CAP_ENABLE_CONTROL |
               APP_ADMISSION_V4_CAP_LIVE_PUBLISH;
    CHECK_MSG(app_host_control_session_open(&cfg, &gen) ==
                  APP_CONTROL_UNAUTHORIZED,
              "v4_enable_with_live_still_unauthorized");
}

static void test_v4_enable_linearization_wire(void)
{
    app_control_session_cfg_t cfg;
    app_control_request_t req;
    app_control_status_t status;
    uint8_t payload[APP_CONTROL_ENABLE_PAYLOAD_SIZE] = {
        0x45u, 0x4Eu, 0x42u, 0x31u, 2u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u
    };
    uint8_t command[APP_CONTROL_ENABLE_SET_SIZE];
    uint8_t receipt[APP_CONTROL_ENABLE_RECEIPT_SIZE];
    uint32_t generation = 0u;
    uint32_t actual_len = 0u;

    ctl_init_enable_fake();
    memset(&cfg, 0, sizeof(cfg));
    cfg.control_max = APP_CONTROL_HEADER_SIZE;
    cfg.caps = 0x170u;
    cfg.protocol_major = 4u;
    strcpy(cfg.publisher_id, "test-publisher");
    strcpy(cfg.app_id, "line-crossing");
    CHECK(app_host_control_session_open(&cfg, &generation) == APP_CONTROL_UNAUTHORIZED);
    cfg.control_max = APP_CONTROL_ENABLE_SET_SIZE;
    cfg.caps = 0x160u;
    CHECK(app_host_control_session_open(&cfg, &generation) == APP_CONTROL_UNAUTHORIZED);
    cfg.caps = 0x170u;
    CHECK(app_host_control_session_open(&cfg, &generation) == APP_CONTROL_OK);

    memset(&req, 0, sizeof(req));
    req.request_id = 1u;
    req.msg_type = APP_CONTROL_MSG_TYPE_ENABLE_SET;
    req.payload = payload;
    req.payload_len = sizeof(payload);
    req.expected_config_generation = 7u;
    CHECK(app_host_control_register(&req) == APP_CONTROL_REG_OK);
    CHECK(g_enable_precommit_calls == 1u);
    CHECK(app_host_control_deliver(command, sizeof(command), &actual_len) == APP_CONTROL_OK);
    CHECK(actual_len == APP_CONTROL_ENABLE_SET_SIZE);
    CHECK(rd32(command, 0u) == APP_CONTROL_PROTO_MAGIC_V4);
    CHECK(rd16(command, 6u) == APP_CONTROL_MSG_TYPE_ENABLE_SET);
    CHECK(rd32(command, 24u) == 0u);
    CHECK(rd32(command, 28u) == 0u);
    CHECK(memcmp(command + APP_CONTROL_HEADER_SIZE, payload, sizeof(payload)) == 0);

    memset(receipt, 0, sizeof(receipt));
    receipt[0] = 0x4Eu;
    receipt[1] = 0x46u;
    receipt[2] = 0x43u;
    receipt[3] = 0x34u;
    receipt[4] = 1u;
    receipt[6] = APP_CONTROL_MSG_TYPE_RECEIPT;
    receipt[8] = APP_CONTROL_ENABLE_RECEIPT_SIZE;
    receipt[12] = (uint8_t)generation;
    receipt[16] = 1u;
    receipt[20] = 7u;
    receipt[24] = 1u;
    receipt[32] = 0x45u;
    receipt[33] = 0x4Eu;
    receipt[34] = 0x52u;
    receipt[35] = 0x31u;
    receipt[36] = APP_CONTROL_ENABLE_STATE_PAUSED;
    receipt[40] = 1u;
    CHECK(app_host_control_receipt(receipt, APP_CONTROL_HEADER_SIZE) ==
          APP_CONTROL_INVALID_ARGUMENT);
    CHECK(app_host_control_receipt(receipt, sizeof(receipt)) == APP_CONTROL_OK);
    CHECK(g_enable_receipt_calls == 1u);
    CHECK(app_host_control_receipt(receipt, sizeof(receipt)) == APP_CONTROL_OK);
    receipt[36] = APP_CONTROL_ENABLE_STATE_ENABLED;
    CHECK(app_host_control_receipt(receipt, sizeof(receipt)) == APP_CONTROL_INVALID_ARGUMENT);
    app_host_control_status(&status);
    CHECK(status.command_state == APP_CONTROL_CMD_APPLIED);

    req.request_id = 2u;
    g_enable_precommit_result = APP_CONTROL_INVALID_ARGUMENT;
    CHECK(app_host_control_register(&req) == APP_CONTROL_ERR_STORAGE_GUARD);
    CHECK(g_enable_precommit_calls == 2u);
    app_host_control_status(&status);
    CHECK(status.unknown_count == 1u);
    CHECK(status.unknown_request_id == 2u);
    CHECK(app_host_control_deliver(command, sizeof(command), &actual_len) ==
          APP_CONTROL_NO_EVENT);
    CHECK(app_host_control_register(&req) == APP_CONTROL_ERR_DUPLICATE_ID_CONFLICT);

    req.request_id = 3u;
    g_enable_precommit_result = APP_CONTROL_OK;
    CHECK(app_host_control_register(&req) == APP_CONTROL_REG_OK);
    CHECK(app_host_control_deliver(command, sizeof(command), &actual_len) ==
          APP_CONTROL_OK);
    receipt[16] = 3u;
    receipt[24] = 2u;
    receipt[40] = 2u;
    g_enable_receipt_result = APP_CONTROL_ERR_STORAGE_GUARD;
    CHECK(app_host_control_receipt(receipt, sizeof(receipt)) ==
          APP_CONTROL_ERR_STORAGE_GUARD);
    app_host_control_status(&status);
    CHECK(status.command_state == APP_CONTROL_CMD_UNKNOWN);
    CHECK(status.unknown_count == 2u);
    CHECK(status.unknown_request_id == 3u);
    CHECK(app_host_control_receipt(receipt, sizeof(receipt)) ==
          APP_CONTROL_INVALID_ARGUMENT);
    req.request_id = 4u;
    g_enable_precommit_result = APP_CONTROL_UNAUTHORIZED;
    CHECK(app_host_control_register(&req) == APP_CONTROL_ERR_UNAUTHORIZED);
    app_host_control_status(&status);
    CHECK(status.unknown_count == 3u);
    CHECK(status.unknown_request_id == 4u);
    app_host_control_session_close();
}

static void test_register_decisions(void)
{
    uint32_t gen = 0u;
    uint8_t payload[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    uint8_t payload2[8] = { 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u };
    app_control_request_t req;
    app_control_status_t st;

    ctl_init_fake();
    g_now_ms = 0u;
    memset(&req, 0, sizeof(req));
    req.request_id = 1u;
    req.msg_type = APP_CONTROL_MSG_TYPE_CONFIG_APPLY;
    req.payload = payload;
    req.payload_len = sizeof(payload);
    req.expected_config_generation = 6u;
    CHECK_MSG(app_host_control_register(&req) == APP_CONTROL_ERR_UNAUTHORIZED,
              "register_without_session_unauthorized");
    CHECK_MSG(app_host_control_register(NULL) == APP_CONTROL_ERR_PARAM,
              "register_null_param");

    CHECK(ctl_open(0x00u, 256u, 0u, &gen) == APP_CONTROL_OK);
    CHECK_MSG(app_host_control_register(&req) == APP_CONTROL_ERR_UNAUTHORIZED,
              "register_without_bit6_unauthorized");
    app_host_control_session_close();

    CHECK(ctl_open(0x40u, 256u, 0u, &gen) == APP_CONTROL_OK);
    {
        app_control_request_t bad = req;

        bad.request_id = 0u;
        CHECK_MSG(app_host_control_register(&bad) == APP_CONTROL_ERR_INVALID,
                  "register_zero_id_invalid");
        bad = req;
        bad.msg_type = 7u;
        CHECK_MSG(app_host_control_register(&bad) == APP_CONTROL_ERR_INVALID,
                  "register_unknown_type_invalid");
        bad = req;
        bad.expected_config_generation = 0u;
        CHECK_MSG(app_host_control_register(&bad) == APP_CONTROL_ERR_INVALID,
                  "register_zero_expected_gen_invalid");
        bad = req;
        bad.flags = 0xFFFFFFFEu;
        CHECK_MSG(app_host_control_register(&bad) == APP_CONTROL_ERR_INVALID,
                  "register_unknown_flags_invalid");
        bad = req;
        bad.payload = NULL;
        bad.payload_len = 0u;
        CHECK_MSG(app_host_control_register(&bad) == APP_CONTROL_ERR_INVALID,
                  "register_empty_config_payload_invalid");
        bad = req;
        bad.msg_type = APP_CONTROL_MSG_TYPE_COUNTERS_RESET;
        bad.payload = payload;
        bad.payload_len = sizeof(payload);
        CHECK_MSG(app_host_control_register(&bad) == APP_CONTROL_ERR_INVALID,
                  "register_reset_with_payload_invalid");
    }
    {
        uint8_t big[APP_CONTROL_MSG_RAM_MAX];
        app_control_request_t big_req = req;
        uint8_t rx[APP_CONTROL_MSG_RAM_MAX];
        uint32_t rx_len = 0u;

        big_req.payload = big;
        memset(big, 0xABu, sizeof(big));
        big_req.payload_len = APP_CONTROL_MSG_RAM_MAX - APP_CONTROL_HEADER_SIZE + 1u;
        CHECK_MSG(app_host_control_register(&big_req) == APP_CONTROL_ERR_QUOTA_EXCEEDED,
                  "register_over_quota_rejected");
        big_req.payload_len = 224u;
        big_req.request_id = 10u;
        CHECK(app_host_control_register(&big_req) == APP_CONTROL_REG_OK);
        app_host_control_status(&st);
        CHECK(st.has_command == AICAM_TRUE);
        CHECK(st.command_state == APP_CONTROL_CMD_PENDING);
        CHECK(st.command_payload_len == 224u);
        CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_OK);
        CHECK(rx_len == APP_CONTROL_HEADER_SIZE + 224u);
        build_receipt(rx, gen, 10u, 6u, 3u, APP_CONTROL_RESULT_REJECTED_INVALID);
        CHECK(app_host_control_receipt(rx, APP_CONTROL_HEADER_SIZE) == APP_CONTROL_OK);
        app_host_control_status(&st);
        CHECK(st.command_state == APP_CONTROL_CMD_REJECTED_INVALID);
    }
    {
        app_control_request_t reset = req;
        uint8_t rx[APP_CONTROL_MSG_RAM_MAX];
        uint32_t rx_len = 0u;

        reset.msg_type = APP_CONTROL_MSG_TYPE_COUNTERS_RESET;
        reset.payload = NULL;
        reset.payload_len = 0u;
        reset.flags = 0u;
        reset.request_id = 20u;
        CHECK_MSG(app_host_control_register(&reset) == APP_CONTROL_ERR_UNCONFIRMED,
                  "reset_unconfirmed_rejected");
        reset.flags = APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED;
        CHECK_MSG(app_host_control_register(&reset) == APP_CONTROL_REG_OK,
                  "reset_confirmed_accepted");
        CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_OK);
        CHECK(rx_len == APP_CONTROL_HEADER_SIZE);
        CHECK(rd16(rx, 6) == APP_CONTROL_MSG_TYPE_COUNTERS_RESET);
        CHECK(rd32(rx, 24) == 0u);
        CHECK(rd32(rx, 28) == APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED);
        build_receipt(rx, gen, 20u, 6u, 4u, APP_CONTROL_RESULT_APPLIED);
        CHECK(app_host_control_receipt(rx, APP_CONTROL_HEADER_SIZE) == APP_CONTROL_OK);
    }
    {
        uint8_t rx[APP_CONTROL_MSG_RAM_MAX];
        uint32_t rx_len = 0u;
        app_control_request_t again;

        CHECK_MSG(app_host_control_register(&req) == APP_CONTROL_REG_OK,
                  "fresh_command_after_terminal_accepted");
        CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_OK);
        CHECK(rx_len == APP_CONTROL_HEADER_SIZE + 8u);
        build_receipt(rx, gen, req.request_id, 6u, 5u, APP_CONTROL_RESULT_APPLIED);
        CHECK(app_host_control_receipt(rx, APP_CONTROL_HEADER_SIZE) == APP_CONTROL_OK);
        again = req;
        CHECK_MSG(app_host_control_register(&again) == APP_CONTROL_REG_CACHED,
                  "same_semantics_cached");
        again.payload = payload2;
        CHECK_MSG(app_host_control_register(&again) == APP_CONTROL_ERR_DUPLICATE_ID_CONFLICT,
                  "same_id_diff_payload_conflict");
        again.payload = payload;
        again.expected_config_generation = 7u;
        CHECK_MSG(app_host_control_register(&again) == APP_CONTROL_ERR_DUPLICATE_ID_CONFLICT,
                  "same_id_diff_gen_conflict");
        again.expected_config_generation = 6u;
        again.flags = APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED;
        CHECK_MSG(app_host_control_register(&again) == APP_CONTROL_ERR_DUPLICATE_ID_CONFLICT,
                  "same_id_diff_flags_conflict");
    }
    {
        app_control_request_t other = req;
        app_control_request_t third;

        other.request_id = 30u;
        CHECK_MSG(app_host_control_register(&other) == APP_CONTROL_REG_OK,
                  "second_command_accepted");
        third = other;
        third.request_id = 31u;
        CHECK_MSG(app_host_control_register(&third) == APP_CONTROL_ERR_BUSY,
                  "third_command_busy");
        third.request_id = 30u;
        third.payload = payload2;
        CHECK_MSG(app_host_control_register(&third) == APP_CONTROL_ERR_DUPLICATE_ID_CONFLICT,
                  "in_flight_same_id_diff_conflict");
    }
    app_host_control_status(&st);
    CHECK(st.command_state == APP_CONTROL_CMD_PENDING);
    {
        uint8_t rx[APP_CONTROL_MSG_RAM_MAX];
        uint32_t rx_len = 0u;

        CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_OK);
        CHECK(rx_len == APP_CONTROL_HEADER_SIZE + 8u);
        app_host_control_status(&st);
        CHECK(st.command_state == APP_CONTROL_CMD_DELIVERED);
        {
            app_control_request_t another = req;

            another.request_id = 31u;
            CHECK_MSG(app_host_control_register(&another) == APP_CONTROL_ERR_BUSY,
                      "register_while_delivered_busy");
        }
        build_receipt(rx, gen, 30u, 6u, 7u, APP_CONTROL_RESULT_APPLIED);
        CHECK(app_host_control_receipt(rx, APP_CONTROL_HEADER_SIZE) == APP_CONTROL_OK);
        app_host_control_status(&st);
        CHECK(st.command_state == APP_CONTROL_CMD_APPLIED);
        CHECK(st.has_known_config_generation == AICAM_TRUE);
        CHECK(st.known_config_generation == 6u);
        CHECK(st.result_state_revision == 7u);
    }
    {
        app_control_request_t conflict = req;

        conflict.request_id = 40u;
        conflict.expected_config_generation = 5u;
        CHECK_MSG(app_host_control_register(&conflict) == APP_CONTROL_ERR_CONFLICT,
                  "stale_expected_gen_conflict");
        conflict.expected_config_generation = 6u;
        CHECK(app_host_control_register(&conflict) == APP_CONTROL_REG_OK);
    }
    app_host_control_session_close();
}

static void test_deliver_wire_and_receipt_guards(void)
{
    uint32_t gen = 0u;
    uint8_t payload[4] = { 0xDEu, 0xADu, 0xBEu, 0xEFu };
    app_control_request_t req;
    uint8_t rx[APP_CONTROL_MSG_RAM_MAX];
    uint32_t rx_len = 0u;
    app_control_status_t st;

    ctl_init_fake();
    g_now_ms = 0u;
    CHECK(ctl_open(0xC0u, 256u, 1024u, &gen) == APP_CONTROL_OK);
    CHECK(gen == 1u);
    CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_NO_EVENT);
    CHECK(app_host_control_deliver(NULL, 32u, &rx_len) == APP_CONTROL_INVALID_ARGUMENT);
    CHECK(app_host_control_deliver(rx, 0u, &rx_len) == APP_CONTROL_INVALID_ARGUMENT);

    memset(&req, 0, sizeof(req));
    req.request_id = 42u;
    req.msg_type = APP_CONTROL_MSG_TYPE_CONFIG_APPLY;
    req.payload = payload;
    req.payload_len = sizeof(payload);
    req.expected_config_generation = 6u;
    CHECK(app_host_control_register(&req) == APP_CONTROL_REG_OK);

    CHECK_MSG(app_host_control_deliver(rx, APP_CONTROL_HEADER_SIZE - 1u, &rx_len) ==
              APP_CONTROL_BUFFER_TOO_SMALL, "small_cap_buffer_too_small");
    CHECK_MSG(rx_len == APP_CONTROL_HEADER_SIZE + 4u,
              "required_len_reports_full_message");
    app_host_control_status(&st);
    CHECK_MSG(st.command_state == APP_CONTROL_CMD_PENDING,
              "buffer_too_small_keeps_command_pending");
    CHECK_MSG(app_host_control_deliver(rx, APP_CONTROL_HEADER_SIZE + 3u, &rx_len) ==
              APP_CONTROL_BUFFER_TOO_SMALL, "still_too_small");
    CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_OK);
    CHECK(rx_len == APP_CONTROL_HEADER_SIZE + 4u);
    CHECK(rd32(rx, 0) == APP_CONTROL_PROTO_MAGIC);
    CHECK(rd16(rx, 4) == APP_CONTROL_HEADER_VERSION);
    CHECK(rd16(rx, 6) == APP_CONTROL_MSG_TYPE_CONFIG_APPLY);
    CHECK(rd32(rx, 8) == APP_CONTROL_HEADER_SIZE + 4u);
    CHECK(rd32(rx, 12) == gen);
    CHECK(rd32(rx, 16) == 42u);
    CHECK(rd32(rx, 20) == 6u);
    CHECK(rd32(rx, 24) == 0u);
    CHECK(rd32(rx, 28) == 0u);
    CHECK(memcmp(rx + APP_CONTROL_HEADER_SIZE, payload, 4u) == 0);
    app_host_control_status(&st);
    CHECK(st.command_state == APP_CONTROL_CMD_DELIVERED);
    CHECK_MSG(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_NO_EVENT,
              "delivered_command_not_redelivered");

    CHECK(app_host_control_receipt(NULL, APP_CONTROL_HEADER_SIZE) ==
          APP_CONTROL_INVALID_ARGUMENT);
    CHECK(app_host_control_receipt(rx, APP_CONTROL_HEADER_SIZE - 1u) ==
          APP_CONTROL_INVALID_ARGUMENT);
    CHECK(app_host_control_receipt(rx, APP_CONTROL_HEADER_SIZE + 1u) ==
          APP_CONTROL_INVALID_ARGUMENT);
    {
        uint8_t bad[APP_CONTROL_HEADER_SIZE];

        build_receipt(bad, gen, 42u, 7u, 5u, APP_CONTROL_RESULT_APPLIED);
        bad[0] = 0x00u;
        CHECK(app_host_control_receipt(bad, APP_CONTROL_HEADER_SIZE) ==
              APP_CONTROL_INVALID_ARGUMENT);
        build_receipt(bad, gen, 42u, 7u, 5u, APP_CONTROL_RESULT_APPLIED);
        bad[6] = 0x01u;
        CHECK(app_host_control_receipt(bad, APP_CONTROL_HEADER_SIZE) ==
              APP_CONTROL_INVALID_ARGUMENT);
        build_receipt(bad, gen, 42u, 7u, 5u, APP_CONTROL_RESULT_APPLIED);
        bad[11] = 0x01u;
        CHECK(app_host_control_receipt(bad, APP_CONTROL_HEADER_SIZE) ==
              APP_CONTROL_INVALID_ARGUMENT);
        build_receipt(bad, gen + 1u, 42u, 7u, 5u, APP_CONTROL_RESULT_APPLIED);
        CHECK_MSG(app_host_control_receipt(bad, APP_CONTROL_HEADER_SIZE) ==
                  APP_CONTROL_INVALID_ARGUMENT, "foreign_generation_receipt_rejected");
        app_host_control_status(&st);
        CHECK_MSG(st.command_state == APP_CONTROL_CMD_DELIVERED,
                  "bad_receipts_keep_delivered");
        build_receipt(bad, gen, 43u, 7u, 5u, APP_CONTROL_RESULT_APPLIED);
        CHECK_MSG(app_host_control_receipt(bad, APP_CONTROL_HEADER_SIZE) ==
                  APP_CONTROL_INVALID_ARGUMENT, "wrong_request_id_rejected");
        build_receipt(bad, gen, 0u, 7u, 5u, APP_CONTROL_RESULT_APPLIED);
        CHECK_MSG(app_host_control_receipt(bad, APP_CONTROL_HEADER_SIZE) ==
                  APP_CONTROL_INVALID_ARGUMENT, "zero_request_id_rejected");
        build_receipt(bad, gen, 42u, 0u, 5u, APP_CONTROL_RESULT_APPLIED);
        CHECK_MSG(app_host_control_receipt(bad, APP_CONTROL_HEADER_SIZE) ==
                  APP_CONTROL_INVALID_ARGUMENT, "zero_result_generation_rejected");
        build_receipt(bad, gen, 42u, 7u, 5u, 8u);
        CHECK_MSG(app_host_control_receipt(bad, APP_CONTROL_HEADER_SIZE) ==
                  APP_CONTROL_INVALID_ARGUMENT, "unknown_result_status_rejected");
    }
    {
        uint8_t ok[APP_CONTROL_HEADER_SIZE];

        build_receipt(ok, gen, 42u, 7u, 5u, APP_CONTROL_RESULT_APPLIED);
        CHECK(app_host_control_receipt(ok, APP_CONTROL_HEADER_SIZE) == APP_CONTROL_OK);
        app_host_control_status(&st);
        CHECK(st.command_state == APP_CONTROL_CMD_APPLIED);
        CHECK(st.known_config_generation == 7u);
        CHECK(st.result_state_revision == 5u);
        build_receipt(ok, gen, 42u, 9u, 6u, APP_CONTROL_RESULT_FAILED_STORAGE_IO);
        CHECK_MSG(app_host_control_receipt(ok, APP_CONTROL_HEADER_SIZE) == APP_CONTROL_OK,
                  "duplicate_receipt_tolerated");
        app_host_control_status(&st);
        CHECK_MSG(st.command_state == APP_CONTROL_CMD_APPLIED,
                  "duplicate_receipt_keeps_cached_result");
        CHECK(st.result_state_revision == 5u);
    }
    {
        uint8_t ok[APP_CONTROL_HEADER_SIZE];
        app_control_request_t next = req;

        next.request_id = 43u;
        next.expected_config_generation = 7u;
        CHECK(app_host_control_register(&next) == APP_CONTROL_REG_OK);
        CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_OK);
        build_receipt(ok, gen, 43u, 7u, 6u, APP_CONTROL_RESULT_REJECTED_STALE_CONFIG);
        CHECK(app_host_control_receipt(ok, APP_CONTROL_HEADER_SIZE) == APP_CONTROL_OK);
        app_host_control_status(&st);
        CHECK(st.command_state == APP_CONTROL_CMD_REJECTED_STALE_CONFIG);
        CHECK_MSG(st.known_config_generation == 7u,
                  "receipt_evidence_updates_known_generation");
        CHECK(st.result_state_revision == 6u);
    }
    {
        uint8_t ok[APP_CONTROL_HEADER_SIZE];
        app_control_request_t reset = req;

        reset.request_id = 44u;
        reset.msg_type = APP_CONTROL_MSG_TYPE_COUNTERS_RESET;
        reset.payload = NULL;
        reset.payload_len = 0u;
        reset.flags = APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED;
        reset.expected_config_generation = 7u;
        CHECK(app_host_control_register(&reset) == APP_CONTROL_REG_OK);
        CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_OK);
        build_receipt(ok, gen, 44u, 7u, 7u, APP_CONTROL_RESULT_FAILED_STORAGE_UNKNOWN);
        CHECK(app_host_control_receipt(ok, APP_CONTROL_HEADER_SIZE) == APP_CONTROL_OK);
        app_host_control_status(&st);
        CHECK(st.command_state == APP_CONTROL_CMD_FAILED_STORAGE_UNKNOWN);
        {
            app_control_request_t after = reset;

            after.request_id = 45u;
            CHECK_MSG(app_host_control_register(&after) == APP_CONTROL_ERR_STORAGE_GUARD,
                      "reset_after_storage_unknown_guarded");
            after.msg_type = APP_CONTROL_MSG_TYPE_CONFIG_APPLY;
            after.payload = payload;
            after.payload_len = sizeof(payload);
            after.flags = APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED;
            CHECK_MSG(app_host_control_register(&after) == APP_CONTROL_ERR_STORAGE_GUARD,
                      "destructive_config_after_storage_unknown_guarded");
            after.flags = 0u;
            CHECK_MSG(app_host_control_register(&after) == APP_CONTROL_REG_OK,
                      "non_destructive_config_allowed");
            {
                uint8_t rx2[APP_CONTROL_MSG_RAM_MAX];
                uint32_t rx2_len = 0u;

                CHECK(app_host_control_deliver(rx2, sizeof(rx2), &rx2_len) == APP_CONTROL_OK);
                build_receipt(rx2, gen, 45u, 7u, 8u, APP_CONTROL_RESULT_APPLIED);
                CHECK(app_host_control_receipt(rx2, APP_CONTROL_HEADER_SIZE) == APP_CONTROL_OK);
            }
            after.request_id = 46u;
            after.flags = APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED;
            CHECK_MSG(app_host_control_register(&after) == APP_CONTROL_REG_OK,
                      "guard_cleared_by_newer_applied_receipt");
            {
                uint8_t rx2[APP_CONTROL_MSG_RAM_MAX];
                uint32_t rx2_len = 0u;

                CHECK(app_host_control_deliver(rx2, sizeof(rx2), &rx2_len) == APP_CONTROL_OK);
                build_receipt(rx2, gen, 46u, 7u, 9u, APP_CONTROL_RESULT_APPLIED);
                CHECK(app_host_control_receipt(rx2, APP_CONTROL_HEADER_SIZE) == APP_CONTROL_OK);
            }
        }
    }
    app_host_control_session_close();
}

static void test_cross_session_isolation(void)
{
    uint32_t gen = 0u;
    uint8_t payload[4] = { 1u, 2u, 3u, 4u };
    app_control_request_t req;
    uint8_t rx[APP_CONTROL_MSG_RAM_MAX];
    uint32_t rx_len = 0u;
    app_control_status_t st;
    app_control_snapshot_view_t view;

    ctl_init_fake();
    g_now_ms = 0u;
    CHECK(ctl_open(0xC0u, 256u, 1024u, &gen) == APP_CONTROL_OK);
    CHECK(gen == 1u);
    memset(&req, 0, sizeof(req));
    req.request_id = 100u;
    req.msg_type = APP_CONTROL_MSG_TYPE_CONFIG_APPLY;
    req.payload = payload;
    req.payload_len = sizeof(payload);
    req.expected_config_generation = 3u;
    CHECK(app_host_control_register(&req) == APP_CONTROL_REG_OK);
    CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_OK);
    {
        uint8_t snap[64];
        uint32_t total = build_snapshot(snap, sizeof(snap), "{\"k\":1}", 3u, 2u,
                                        0u, 0u, 0u, 0u);

        CHECK(app_host_control_snapshot_submit(snap, total) == APP_CONTROL_OK);
    }
    app_host_control_session_close();
    app_host_control_status(&st);
    CHECK_MSG(st.session_active == AICAM_FALSE, "session_closed");
    CHECK_MSG(st.unknown_count == 1u, "delivered_without_receipt_becomes_unknown");
    CHECK(st.unknown_session_generation == 1u);
    CHECK(st.unknown_request_id == 100u);
    CHECK(st.unknown_msg_type == APP_CONTROL_MSG_TYPE_CONFIG_APPLY);
    memset(&view, 0, sizeof(view));
    CHECK_MSG(app_host_control_snapshot_read(&view, NULL, 0u, NULL) == APP_CONTROL_NOT_FOUND,
              "snapshot_unavailable_after_close");
    CHECK(view.freshness == APP_CONTROL_SNAPSHOT_NONE);

    CHECK(ctl_open(0xC0u, 256u, 1024u, &gen) == APP_CONTROL_OK);
    CHECK_MSG(gen == 2u, "new_session_generation");
    app_host_control_status(&st);
    CHECK_MSG(st.has_command == AICAM_FALSE, "no_command_carried_over");
    CHECK_MSG(st.has_known_config_generation == AICAM_FALSE,
              "no_generation_baseline_carried_over");
    {
        uint8_t ok[APP_CONTROL_HEADER_SIZE];

        build_receipt(ok, 1u, 100u, 3u, 2u, APP_CONTROL_RESULT_APPLIED);
        CHECK_MSG(app_host_control_receipt(ok, APP_CONTROL_HEADER_SIZE) ==
                  APP_CONTROL_INVALID_ARGUMENT, "old_generation_receipt_rejected");
        build_receipt(ok, 2u, 101u, 3u, 2u, APP_CONTROL_RESULT_APPLIED);
        CHECK_MSG(app_host_control_receipt(ok, APP_CONTROL_HEADER_SIZE) ==
                  APP_CONTROL_INVALID_ARGUMENT, "no_delivered_command_receipt_rejected");
    }
    CHECK_MSG(app_host_control_register(&req) == APP_CONTROL_REG_OK,
              "same_id_fresh_session_allowed");
    {
        uint8_t ok[APP_CONTROL_HEADER_SIZE];

        CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_OK);
        CHECK(rd32(rx, 12) == 2u);
        build_receipt(ok, 2u, 100u, 3u, 4u, APP_CONTROL_RESULT_APPLIED);
        CHECK(app_host_control_receipt(ok, APP_CONTROL_HEADER_SIZE) == APP_CONTROL_OK);
    }
    app_host_control_session_close();
    app_host_control_status(&st);
    CHECK(st.unknown_count == 1u);
    CHECK_MSG(app_host_control_register(&req) == APP_CONTROL_ERR_UNAUTHORIZED,
              "register_after_close_unauthorized");
}

static void test_snapshot_lifecycle(void)
{
    uint32_t gen = 0u;
    app_control_snapshot_view_t view;
    uint8_t payload_out[APP_CONTROL_SNAPSHOT_RAM_MAX];
    uint32_t payload_len = 0u;
    app_control_status_t st;

    ctl_init_fake();
    g_now_ms = 0u;
    memset(&view, 0, sizeof(view));
    CHECK(app_host_control_snapshot_read(&view, NULL, 0u, NULL) == APP_CONTROL_NOT_FOUND);
    CHECK(view.freshness == APP_CONTROL_SNAPSHOT_NONE);
    {
        uint8_t tmp[64];
        uint32_t total = build_snapshot(tmp, sizeof(tmp), "{\"k\":1}", 1u, 1u,
                                        0u, 0u, 0u, 0u);

        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                  APP_CONTROL_UNAUTHORIZED, "snapshot_without_session_unauthorized");
    }
    CHECK(ctl_open(0x40u, 256u, 0u, &gen) == APP_CONTROL_OK);
    {
        uint8_t tmp[64];
        uint32_t total = build_snapshot(tmp, sizeof(tmp), "{\"k\":1}", 1u, 1u,
                                        0u, 0u, 0u, 0u);

        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                  APP_CONTROL_UNAUTHORIZED, "snapshot_without_bit7_unauthorized");
    }
    app_host_control_session_close();

    CHECK(ctl_open(0x80u, 256u, 64u, &gen) == APP_CONTROL_OK);
    {
        uint8_t tmp[64];
        uint32_t total = build_snapshot(tmp, sizeof(tmp), "{\"k\":1}", 1u, 1u,
                                        0u, 0u, 0u, 0u);

        CHECK(app_host_control_snapshot_submit(tmp, total) == APP_CONTROL_OK);
        tmp[6] = 0x03u;
        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                  APP_CONTROL_INVALID_ARGUMENT, "receipt_type_submit_rejected");
        CHECK_MSG(app_host_control_snapshot_submit(tmp, APP_CONTROL_HEADER_SIZE - 1u) ==
                  APP_CONTROL_INVALID_ARGUMENT, "snapshot_short_buffer_rejected");
    }
    app_host_control_session_close();
    CHECK(ctl_open(0xC0u, 256u, 64u, &gen) == APP_CONTROL_OK);
    CHECK(gen == 3u);
    {
        uint8_t tmp[64];
        uint32_t total = build_snapshot(tmp, sizeof(tmp), "{\"k\":1}", 5u, 9u,
                                        0u, 0u, 0u, 0u);

        CHECK(app_host_control_snapshot_submit(tmp, total) == APP_CONTROL_OK);
        CHECK(app_host_control_snapshot_read(&view, payload_out, sizeof(payload_out),
                                             &payload_len) == APP_CONTROL_OK);
        CHECK_MSG(view.freshness == APP_CONTROL_SNAPSHOT_FRESH, "fresh_snapshot");
        CHECK_MSG(view.session_generation == gen, "snapshot_bound_to_host_session");
        CHECK_MSG(view.visible_version == 1u, "visible_version_starts_at_one");
        CHECK(view.config_generation == 5u);
        CHECK(view.state_revision == 9u);
        CHECK(view.age_ms == 0u);
        CHECK(payload_len == 7u);
        CHECK(memcmp(payload_out, "{\"k\":1}", 7u) == 0);
        CHECK(strcmp(view.publisher_id, "test-publisher") == 0);
        CHECK(strcmp(view.app_id, "line-crossing") == 0);
        app_host_control_status(&st);
        CHECK(st.has_known_config_generation == AICAM_TRUE);
        CHECK(st.known_config_generation == 5u);
    }
    {
        uint8_t tmp[64];
        uint32_t total = build_snapshot(tmp, sizeof(tmp), "{\"k\":2}", 5u, 10u,
                                        0u, 0u, 0u, 0u);

        CHECK(app_host_control_snapshot_submit(tmp, total) == APP_CONTROL_OK);
        CHECK(app_host_control_snapshot_read(&view, payload_out, sizeof(payload_out),
                                             &payload_len) == APP_CONTROL_OK);
        CHECK_MSG(view.visible_version == 2u, "visible_version_monotonic");
        CHECK(payload_len == 7u);
        CHECK(memcmp(payload_out, "{\"k\":2}", 7u) == 0);
    }
    {
        uint8_t tmp[64];
        uint32_t total = build_snapshot(tmp, sizeof(tmp), "{\"k\":3}", 5u, 11u,
                                        0u, 0u, 0u, 0u);

        CHECK(app_host_control_snapshot_submit(tmp, total) == APP_CONTROL_OK);
        memcpy(tmp + APP_CONTROL_HEADER_SIZE, "{\"k\":9}", 7u);
        memset(payload_out, 0, sizeof(payload_out));
        CHECK(app_host_control_snapshot_read(&view, payload_out, sizeof(payload_out),
                                             &payload_len) == APP_CONTROL_OK);
        CHECK_MSG(memcmp(payload_out, "{\"k\":3}", 7u) == 0,
                  "snapshot_synchronously_copied_no_dangling_pointer");
    }
    {
        uint8_t tmp[256];
        uint32_t total;
        uint32_t i;

        total = build_snapshot(tmp, sizeof(tmp), "{\"dup\":1,\"dup\":2}", 5u, 11u,
                               0u, 0u, 0u, 0u);
        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                  APP_CONTROL_INVALID_ARGUMENT, "snapshot_duplicate_key_rejected");
        total = build_snapshot(tmp, sizeof(tmp), "[1,2]", 5u, 11u, 0u, 0u, 0u, 0u);
        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                  APP_CONTROL_INVALID_ARGUMENT, "snapshot_top_array_rejected");
        total = build_snapshot(tmp, sizeof(tmp), "{\"a\":NaN}", 5u, 11u, 0u, 0u, 0u, 0u);
        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                  APP_CONTROL_INVALID_ARGUMENT, "snapshot_nan_rejected");
        total = build_snapshot(tmp, sizeof(tmp), "{\"a\":1} junk", 5u, 11u, 0u, 0u, 0u, 0u);
        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                  APP_CONTROL_INVALID_ARGUMENT, "snapshot_trailing_rejected");
        total = build_snapshot(tmp, sizeof(tmp), "{\"a\":1}", 0u, 11u, 0u, 0u, 0u, 0u);
        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                  APP_CONTROL_INVALID_ARGUMENT, "snapshot_zero_generation_rejected");
        total = build_snapshot(tmp, sizeof(tmp), "{\"a\":1}", 5u, 11u, 7u, 0u, 0u, 0u);
        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                  APP_CONTROL_INVALID_ARGUMENT,
                  "snapshot_app_filled_session_generation_rejected");
        total = build_snapshot(tmp, sizeof(tmp), "{\"a\":1}", 5u, 11u, 0u, 9u, 0u, 0u);
        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                  APP_CONTROL_INVALID_ARGUMENT,
                  "snapshot_app_filled_request_id_rejected");
        total = build_snapshot(tmp, sizeof(tmp), "{\"a\":1}", 5u, 11u, 0u, 0u, 1u, 0u);
        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                  APP_CONTROL_INVALID_ARGUMENT, "snapshot_flags_nonzero_rejected");
        total = build_snapshot(tmp, sizeof(tmp), "{\"a\":1}", 5u, 11u, 0u, 0u, 0u,
                               APP_CONTROL_HEADER_SIZE + 5u);
        CHECK_MSG(app_host_control_snapshot_submit(tmp, APP_CONTROL_HEADER_SIZE + 5u) ==
                  APP_CONTROL_INVALID_ARGUMENT, "snapshot_total_len_mismatch_rejected");
        CHECK_MSG(app_host_control_snapshot_submit(tmp, APP_CONTROL_HEADER_SIZE + 6u) ==
                  APP_CONTROL_INVALID_ARGUMENT, "snapshot_submit_len_mismatch_rejected");
        (void)build_snapshot(tmp, sizeof(tmp), "{\"a\":1}", 5u, 11u, 0u, 0u, 0u,
                             APP_CONTROL_HEADER_SIZE + 1u + 64u);
        CHECK_MSG(app_host_control_snapshot_submit(tmp, APP_CONTROL_HEADER_SIZE + 1u + 64u) ==
                  APP_CONTROL_QUOTA_EXCEEDED, "snapshot_over_quota_rejected");
        for (i = 0u; i < 200u; i++) {
            tmp[i] = 0x41u;
        }
        CHECK_MSG(app_host_control_snapshot_submit(tmp, 200u) ==
                  APP_CONTROL_INVALID_ARGUMENT, "snapshot_not_json_rejected");
    }
    {
        uint8_t tmp[64];
        uint32_t total = build_snapshot(tmp, sizeof(tmp), "{\"k\":4}", 5u, 12u,
                                        0u, 0u, 0u, 0u);

        CHECK(app_host_control_snapshot_submit(tmp, total) == APP_CONTROL_OK);
        fake_advance(APP_CONTROL_STALE_MS - 1u);
        CHECK(app_host_control_snapshot_read(&view, NULL, 0u, NULL) == APP_CONTROL_OK);
        CHECK_MSG(view.freshness == APP_CONTROL_SNAPSHOT_FRESH, "just_under_10s_fresh");
        fake_advance(1u);
        CHECK(app_host_control_snapshot_read(&view, NULL, 0u, NULL) == APP_CONTROL_OK);
        CHECK_MSG(view.freshness == APP_CONTROL_SNAPSHOT_FRESH, "exactly_10s_fresh");
        fake_advance(1u);
        CHECK(app_host_control_snapshot_read(&view, NULL, 0u, NULL) == APP_CONTROL_OK);
        CHECK_MSG(view.freshness == APP_CONTROL_SNAPSHOT_STALE, "over_10s_stale");
        CHECK(view.age_ms == APP_CONTROL_STALE_MS + 1u);
        CHECK_MSG(app_host_control_snapshot_read(&view, payload_out, 4u,
                                                 &payload_len) == APP_CONTROL_BUFFER_TOO_SMALL,
                  "small_payload_cap_buffer_too_small");
        CHECK(payload_len == 0u);
        CHECK(view.payload_len == 7u);
    }
    app_host_control_session_close();
    CHECK(app_host_control_snapshot_read(&view, NULL, 0u, NULL) == APP_CONTROL_NOT_FOUND);
    CHECK(view.freshness == APP_CONTROL_SNAPSHOT_NONE);
}

typedef struct {
    pthread_mutex_t state_mtx;
    aicam_bool_t model_available;
    app_session_model_view_t model;
    volatile int release;
    volatile int thread_exited;
} sess_backend_t;

static sess_backend_t sb;
static app_install_io_t g_sess_io;
static uint8_t g_sess_stage[STAGE_CAP];

static void sb_lock(void)
{
    pthread_mutex_lock(&sb.state_mtx);
}

static void sb_unlock(void)
{
    pthread_mutex_unlock(&sb.state_mtx);
}

static uint8_t *sb_stage_base(void *ctx, size_t *cap)
{
    (void)ctx;
    *cap = STAGE_CAP;
    return g_sess_stage;
}

static int sb_stage_commit(void *ctx, size_t stage_cap, const app_host_image_info_t *info)
{
    (void)ctx;
    (void)stage_cap;
    (void)info;
    return 0;
}

static int sb_invoke_entry(void *ctx, const void *api, uint32_t abi_version,
                           const app_host_image_info_t *info, int *raw)
{
    app_session_fn_should_stop_t should_stop;

    (void)ctx;
    (void)info;
    if (api == NULL) {
        return -1;
    }
    if (abi_version == APP_SESSION_ABI_VERSION_V3) {
        should_stop = ((const app_host_session_api_table_v3_t *)api)->should_stop;
    } else {
        should_stop = ((const app_host_session_api_table_t *)api)->should_stop;
    }
    for (;;) {
        if (should_stop() == 1) {
            break;
        }
        sleep_ms_short(2);
    }
    *raw = 0;
    return 0;
}

static void sb_scrub(void *ctx)
{
    (void)ctx;
}

static void sb_log_sink(void *ctx, const char *text)
{
    (void)ctx;
    (void)text;
}

static uint32_t sb_tick_ms(void *ctx)
{
    (void)ctx;
    return g_now_ms;
}

static void sb_sleep_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    sleep_ms_short(ms);
}

static void *sb_thread_trampoline(void *arg)
{
    void (*fn)(void *) = (void (*)(void *))arg;

    fn(NULL);
    sb_lock();
    sb.thread_exited++;
    sb_unlock();
    return NULL;
}

static int sb_entry_spawn(void *ctx, void (*fn)(void *arg), void *arg)
{
    pthread_t t;

    (void)ctx;
    (void)arg;
    if (pthread_create(&t, NULL, sb_thread_trampoline, (void *)fn) != 0) {
        return -1;
    }
    pthread_detach(t);
    return 0;
}

static aicam_bool_t sb_entry_exited(void *ctx)
{
    (void)ctx;
    return AICAM_TRUE;
}

static void *sb_mutex_create(void *ctx)
{
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    (void)ctx;
    pthread_mutex_init(m, NULL);
    return m;
}

static void sb_mutex_destroy(void *ctx, void *mtx)
{
    fake_mutex_destroy(ctx, mtx);
}

static int sb_mutex_lock(void *ctx, void *mtx)
{
    return fake_mutex_lock(ctx, mtx);
}

static void sb_mutex_unlock(void *ctx, void *mtx)
{
    fake_mutex_unlock(ctx, mtx);
}

static void *sb_event_create(void *ctx)
{
    (void)ctx;
    return NULL;
}

static void sb_event_destroy(void *ctx, void *evt)
{
    (void)ctx;
    (void)evt;
}

static uint32_t sb_event_wait(void *ctx, void *evt, uint32_t timeout_ms)
{
    (void)ctx;
    (void)evt;
    (void)timeout_ms;
    return 0u;
}

static void sb_event_set(void *ctx, void *evt)
{
    (void)ctx;
    (void)evt;
}

static aicam_bool_t sb_ai_attach(void *ctx)
{
    (void)ctx;
    return AICAM_TRUE;
}

static void sb_ai_detach(void *ctx)
{
    (void)ctx;
}

static aicam_bool_t sb_ai_model_view(void *ctx, app_session_model_view_t *out)
{
    (void)ctx;
    sb_lock();
    if (sb.model_available != AICAM_TRUE) {
        sb_unlock();
        return AICAM_FALSE;
    }
    *out = sb.model;
    sb_unlock();
    return AICAM_TRUE;
}

static app_session_result_t sb_pair_authorize(void *ctx,
                                               const app_install_io_t *io,
                                               const app_install_record_t *native_record,
                                               char *reason,
                                               size_t reason_cap)
{
    (void)ctx;
    (void)io;
    (void)native_record;
    if (reason != NULL && reason_cap > 0u) {
        reason[0] = '\0';
    }
    return APP_SESSION_OK;
}

static const app_host_session_backend_t g_sess_backend = {
    NULL,
    sb_stage_base,
    sb_stage_commit,
    sb_invoke_entry,
    sb_scrub,
    sb_log_sink,
    sb_tick_ms,
    sb_sleep_ms,
    sb_entry_spawn,
    sb_entry_exited,
    sb_mutex_create,
    sb_mutex_destroy,
    sb_mutex_lock,
    sb_mutex_unlock,
    sb_event_create,
    sb_event_destroy,
    sb_event_wait,
    sb_event_set,
    sb_ai_attach,
    sb_ai_detach,
    sb_ai_model_view,
    sb_pair_authorize
};

aicam_bool_t app_host_exec_region(uint32_t *base, uint32_t *size)
{
    *base = TARGET_BASE;
    *size = 2u * 1024u * 1024u;
    return AICAM_TRUE;
}

static app_admission_result_t sess_install_verify(const uint8_t *data, size_t len,
                                                  app_admission_verdict_t *out,
                                                  void *ctx)
{
    app_admission_policy_t policy;

    (void)ctx;
    memset(&policy, 0, sizeof(policy));
    policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy.psram_mib = 64u;
    policy.abi_version = APP_ADMISSION_ABI_VERSION_V4;
    policy.supported_caps_mask = APP_ADMISSION_V4_KNOWN_CAPS_MASK;
    policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V5;
    policy.event_capacity = APP_SESSION_EVENT_CAPACITY_MAX;
    policy.report_capacity = APP_ADMISSION_V3_REPORT_MAX;
    policy.state_quota_capacity = APP_SESSION_V3_STATE_QUOTA_MAX;
    policy.control_capacity = APP_SESSION_V3_CONTROL_CAPACITY;
    policy.snapshot_capacity = APP_SESSION_V3_SNAPSHOT_CAPACITY;
    policy.abi_table_size = APP_ADMISSION_V4_ABI_TABLE_SIZE;
    policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;
    policy.live_publish_capacity = APP_ADMISSION_V4_LIVE_MIN_PUBLISH;
    policy.live_track_capacity = APP_ADMISSION_V4_LIVE_TRACK_MAX;
    if (app_host_exec_region(&policy.exec_region_base, &policy.exec_region_size) != AICAM_TRUE) {
        return APP_ADMISSION_ERR_PARAM;
    }
    return app_admission_verify_package(data, len, &policy, NULL, out);
}

static app_install_result_t sess_install(const uint8_t *pkg, size_t len,
                                         app_install_record_t *rec)
{
    app_install_record_t removed;

    app_install_uninstall(&g_sess_io, &removed);
    return app_install(&g_sess_io, sess_install_verify, NULL, pkg, len, rec);
}

static void sess_backend_init(void)
{
    pthread_mutex_init(&sb.state_mtx, NULL);
    sb.model_available = AICAM_TRUE;
    sb.release = 0;
    sb.thread_exited = 0;
    memset(&sb.model, 0, sizeof(sb.model));
    strcpy(sb.model.model_name, "test-model");
    strcpy(sb.model.model_version, "1.0.0");
    strcpy(sb.model.class_names[0], "person");
    strcpy(sb.model.class_names[1], "car");
    sb.model.class_count = 2u;
    sb.model.identity_addr = 0x1000u;
    sb.model.identity_size = 100u;
    sb.model.content_digest = 0xABCDEF01u;
    sb.model.result_type_od = AICAM_TRUE;
    mem_reset();
    mem_io_init(&g_sess_io);
    app_host_session_init(&g_sess_backend);
}

static void sess_backend_release(void)
{
    pthread_mutex_destroy(&sb.state_mtx);
}

static void test_legacy_signed_packages_rejected(void)
{
    static const struct {
        const uint8_t *data;
        size_t len;
    } packages[] = {
        {SESS_PKG_EM2048, SESS_PKG_EM2048_LEN},
        {CTL_V3_FULL_PACKAGE, CTL_V3_FULL_PACKAGE_LEN},
        {CTL_V3_MINIMAL_PACKAGE, CTL_V3_MINIMAL_PACKAGE_LEN},
        {CTL_V3_UNKNOWN_CAP_PACKAGE, CTL_V3_UNKNOWN_CAP_PACKAGE_LEN},
        {CTL_V3_OVER_QUOTA_PACKAGE, CTL_V3_OVER_QUOTA_PACKAGE_LEN},
        {CTL_V3_NO_STATE_PACKAGE, CTL_V3_NO_STATE_PACKAGE_LEN}
    };
    app_install_record_t rec;
    app_host_session_start_out_t out;
    aicam_bool_t present;
    size_t index;

    sess_backend_init();
    for (index = 0u; index < sizeof(packages) / sizeof(packages[0]); index++) {
        memset(&rec, 0, sizeof(rec));
        memset(&out, 0, sizeof(out));
        CHECK(sess_install(packages[index].data, packages[index].len, &rec) ==
              APP_INSTALL_ERR_BAD_PACKAGE);
        CHECK(app_install_query_committed(&g_sess_io, &rec, &present) == APP_INSTALL_OK);
        CHECK(present == AICAM_FALSE);
        CHECK(count_files() == 0);
        CHECK(app_host_session_start(&g_sess_io, &out) == APP_SESSION_ERR_NOT_INSTALLED);
    }
    sess_backend_release();
}

static void test_json_unicode_keys(void)
{
    {
        const char raw_vs_esc[] = "{\"\xC3\xA9\":1,\"\\u00e9\":2}";

        CHECK_MSG(app_host_control_json_object_valid(raw_vs_esc,
                                                     (uint32_t)sizeof(raw_vs_esc) - 1u) ==
                      AICAM_FALSE,
                  "json_raw_vs_escaped_eacute_duplicate_rejected");
    }
    {
        const char esc_pair[] = "{\"\\ud83d\\ude00\":1,\"\xF0\x9F\x98\x80\":2}";

        CHECK_MSG(app_host_control_json_object_valid(esc_pair,
                                                     (uint32_t)sizeof(esc_pair) - 1u) ==
                      AICAM_FALSE,
                  "json_escaped_surrogate_pair_vs_raw_emoji_duplicate_rejected");
    }
    {
        const char both_esc[] = "{\"\\u00e9\":1,\"\\u00E9\":2}";

        CHECK_MSG(app_host_control_json_object_valid(both_esc,
                                                     (uint32_t)sizeof(both_esc) - 1u) ==
                      AICAM_FALSE,
                  "json_two_escaped_eacute_duplicate_rejected");
    }
    {
        const char raw_cjk_dup[] = "{\"\xE5\xAE\xA2\":1,\"\xE5\xAE\xA2\":2}";

        CHECK_MSG(app_host_control_json_object_valid(raw_cjk_dup,
                                                     (uint32_t)sizeof(raw_cjk_dup) - 1u) ==
                      AICAM_FALSE,
                  "json_raw_multibyte_duplicate_rejected");
    }
    {
        const char esc_vs_raw_cjk[] = "{\"\\u5ba2\":1,\"\xE5\xAE\xA2\":2}";

        CHECK_MSG(app_host_control_json_object_valid(esc_vs_raw_cjk,
                                                     (uint32_t)sizeof(esc_vs_raw_cjk) - 1u) ==
                      AICAM_FALSE,
                  "json_escaped_vs_raw_cjk_duplicate_rejected");
    }
    {
        const char distinct_cjk[] = "{\"\xE5\xAE\xA2\":1,\"\xE6\x88\xB7\":2}";

        CHECK_MSG(app_host_control_json_object_valid(distinct_cjk,
                                                     (uint32_t)sizeof(distinct_cjk) - 1u) ==
                      AICAM_TRUE,
                  "json_distinct_multibyte_keys_valid");
    }
    {
        const char not_equivalent[] = "{\"\xC3\xA9\":1,\"\\u00c3\\u00a9\":2}";

        CHECK_MSG(app_host_control_json_object_valid(not_equivalent,
                                                     (uint32_t)sizeof(not_equivalent) - 1u) ==
                      AICAM_TRUE,
                  "json_raw_eacute_vs_two_escapes_distinct_valid");
    }
    {
        const char nested_dup[] = "{\"o\":{\"\xC3\xA9\":1,\"\\u00e9\":2}}";

        CHECK_MSG(app_host_control_json_object_valid(nested_dup,
                                                     (uint32_t)sizeof(nested_dup) - 1u) ==
                      AICAM_FALSE,
                  "json_nested_escaped_equivalent_duplicate_rejected");
    }
    {
        const char slash_dup[] = "{\"a/b\":1,\"a\\/b\":2}";

        CHECK_MSG(app_host_control_json_object_valid(slash_dup,
                                                     (uint32_t)sizeof(slash_dup) - 1u) ==
                      AICAM_FALSE,
                  "json_escaped_slash_duplicate_rejected");
    }
    {
        const char multilingual[] =
            "{\"\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF\":"
            "\"\xE4\xB8\x96\xE7\x95\x8C\",\"emoji\":\"\xF0\x9F\x98\x80\"}";

        CHECK_MSG(app_host_control_json_object_valid(multilingual,
                                                     (uint32_t)sizeof(multilingual) - 1u) ==
                      AICAM_TRUE,
                  "json_multilingual_valid");
    }
    {
        const uint8_t truncated_lead[] =
            { 0x7B, 0x22, 0xC3, 0x22, 0x3A, 0x31, 0x7D };

        CHECK_MSG(app_host_control_json_object_valid(truncated_lead,
                                                     (uint32_t)sizeof(truncated_lead)) ==
                      AICAM_FALSE,
                  "json_truncated_multibyte_key_rejected");
    }
    {
        uint32_t gen = 0u;
        uint8_t tmp[64];
        uint32_t total;

        ctl_init_fake();
        g_now_ms = 0u;
        CHECK(ctl_open(0x80u, 512u, 2048u, &gen) == APP_CONTROL_OK);
        total = build_snapshot(tmp, sizeof(tmp), "{\"\xC3\xA9\":1,\"\\u00e9\":2}",
                               1u, 1u, 0u, 0u, 0u, 0u);
        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                      APP_CONTROL_INVALID_ARGUMENT,
                  "snapshot_escaped_equivalent_duplicate_rejected");
        total = build_snapshot(tmp, sizeof(tmp), "{\"\\ud83d\\ude00\":1,\"\xF0\x9F\x98\x80\":2}",
                               1u, 1u, 0u, 0u, 0u, 0u);
        CHECK_MSG(app_host_control_snapshot_submit(tmp, total) ==
                      APP_CONTROL_INVALID_ARGUMENT,
                  "snapshot_surrogate_equivalent_duplicate_rejected");
        app_host_control_session_close();
    }
}

typedef struct {
    uint32_t iterations;
    uint32_t ok_count;
    uint32_t unauthorized_count;
    uint32_t quota_count;
    uint32_t invalid_count;
    uint32_t other_count;
    uint32_t violations;
    uint32_t stale_bindings;
} stress_result_t;

static void stress_fill_seq(char *json, size_t cap, uint32_t seq)
{
    snprintf(json, cap, "{\"seq\":%u,\"pad\":\"%04u%04u%04u\"}", seq, seq, seq, seq);
}

static void *stress_submitter(void *arg)
{
    stress_result_t *r = (stress_result_t *)arg;
    uint32_t i;

    for (i = 0u; i < r->iterations; i++) {
        uint8_t buf[APP_CONTROL_HEADER_SIZE + 128u];
        char json[96];
        uint32_t total;
        int32_t rc;

        stress_fill_seq(json, sizeof(json), i);
        total = build_snapshot(buf, sizeof(buf), json, 1u, 1u, 0u, 0u, 0u, 0u);
        rc = app_host_control_snapshot_submit(buf, total);
        if (rc == APP_CONTROL_OK) {
            r->ok_count++;
        } else if (rc == APP_CONTROL_UNAUTHORIZED) {
            r->unauthorized_count++;
        } else if (rc == APP_CONTROL_QUOTA_EXCEEDED) {
            r->quota_count++;
        } else if (rc == APP_CONTROL_INVALID_ARGUMENT) {
            r->invalid_count++;
        } else {
            r->other_count++;
        }
    }
    return NULL;
}

static void *stress_bad_submitter(void *arg)
{
    stress_result_t *r = (stress_result_t *)arg;
    uint32_t i;

    for (i = 0u; i < r->iterations; i++) {
        uint8_t buf[APP_CONTROL_HEADER_SIZE + 64u];
        uint32_t total;
        int32_t rc;

        if ((i & 1u) == 0u) {
            total = build_snapshot(buf, sizeof(buf), "{\"a\":1,\"a\":2}", 1u, 1u,
                                   0u, 0u, 0u, 0u);
        } else {
            total = build_snapshot(buf, sizeof(buf), "[1,2]", 1u, 1u, 0u, 0u, 0u, 0u);
        }
        rc = app_host_control_snapshot_submit(buf, total);
        if (rc == APP_CONTROL_OK) {
            r->violations++;
        } else if (rc == APP_CONTROL_INVALID_ARGUMENT) {
            r->invalid_count++;
        } else if (rc == APP_CONTROL_UNAUTHORIZED) {
            r->unauthorized_count++;
        } else if (rc == APP_CONTROL_QUOTA_EXCEEDED) {
            r->quota_count++;
        }
    }
    return NULL;
}

static void *stress_cycler(void *arg)
{
    stress_result_t *r = (stress_result_t *)arg;
    uint32_t i;

    for (i = 0u; i < r->iterations; i++) {
        uint32_t gen = 0u;

        app_host_control_session_close();
        if ((i % 3u) == 0u) {
            (void)ctl_open(0x80u, 512u, 2048u, &gen);
        } else if ((i % 3u) == 1u) {
            (void)ctl_open(0x40u, 512u, 2048u, &gen);
        } else {
            (void)ctl_open(0x80u, 512u, 64u, &gen);
        }
    }
    return NULL;
}

static void *stress_reader(void *arg)
{
    stress_result_t *r = (stress_result_t *)arg;
    uint32_t i;

    for (i = 0u; i < r->iterations; i++) {
        app_control_snapshot_view_t view;
        uint8_t payload[256];
        uint32_t payload_len = 0u;
        app_control_status_t st;
        int32_t rc;

        memset(&view, 0, sizeof(view));
        rc = app_host_control_snapshot_read(&view, payload, sizeof(payload),
                                            &payload_len);
        if (rc == APP_CONTROL_OK) {
            app_control_status_t st_after;
            uint32_t gen_after;

            if (payload_len < 7u || memcmp(payload, "{\"seq\":", 7u) != 0) {
                r->violations++;
            }
            app_host_control_status(&st_after);
            gen_after = st_after.session_generation;
            if (gen_after != 0u && view.session_generation > gen_after) {
                r->stale_bindings++;
            }
            if (view.payload_len != payload_len) {
                r->violations++;
            }
        } else if (rc != APP_CONTROL_NOT_FOUND) {
            r->violations++;
        }
        app_host_control_status(&st);
    }
    return NULL;
}

static void *stress_valid_p(void *arg)
{
    stress_result_t *r = (stress_result_t *)arg;
    uint32_t i;

    for (i = 0u; i < r->iterations; i++) {
        uint8_t buf[APP_CONTROL_HEADER_SIZE + 64u];
        char json[48];
        uint32_t total;
        int32_t rc;

        snprintf(json, sizeof(json), "{\"pa\":%u}", i);
        total = build_snapshot(buf, sizeof(buf), json, 1u, 1u, 0u, 0u, 0u, 0u);
        rc = app_host_control_snapshot_submit(buf, total);
        if (rc == APP_CONTROL_OK) {
            r->ok_count++;
        } else {
            r->other_count++;
        }
    }
    return NULL;
}

static void *stress_dup_q(void *arg)
{
    stress_result_t *r = (stress_result_t *)arg;
    uint32_t i;

    for (i = 0u; i < r->iterations; i++) {
        uint8_t buf[APP_CONTROL_HEADER_SIZE + 64u];
        uint32_t total;
        int32_t rc;

        total = build_snapshot(buf, sizeof(buf), "{\"pb\":1,\"pb\":2}", 1u, 1u,
                               0u, 0u, 0u, 0u);
        rc = app_host_control_snapshot_submit(buf, total);
        if (rc == APP_CONTROL_OK) {
            r->violations++;
        } else if (rc == APP_CONTROL_INVALID_ARGUMENT) {
            r->invalid_count++;
        }
    }
    return NULL;
}

static void test_snapshot_session_isolation_concurrent(void)
{
    stress_result_t sub;
    stress_result_t bad;
    stress_result_t cyc;
    stress_result_t rdr;
    uint32_t gen = 0u;

    ctl_init_fake();
    g_now_ms = 0u;
    CHECK(ctl_open(0x80u, 512u, 2048u, &gen) == APP_CONTROL_OK);
    memset(&sub, 0, sizeof(sub));
    memset(&bad, 0, sizeof(bad));
    memset(&cyc, 0, sizeof(cyc));
    memset(&rdr, 0, sizeof(rdr));
    sub.iterations = 4000u;
    bad.iterations = 4000u;
    cyc.iterations = 4000u;
    rdr.iterations = 8000u;
    {
        pthread_t t_sub;
        pthread_t t_bad;
        pthread_t t_cyc;
        pthread_t t_rdr;

        CHECK(pthread_create(&t_sub, NULL, stress_submitter, &sub) == 0);
        CHECK(pthread_create(&t_bad, NULL, stress_bad_submitter, &bad) == 0);
        CHECK(pthread_create(&t_cyc, NULL, stress_cycler, &cyc) == 0);
        CHECK(pthread_create(&t_rdr, NULL, stress_reader, &rdr) == 0);
        pthread_join(t_sub, NULL);
        pthread_join(t_bad, NULL);
        pthread_join(t_cyc, NULL);
        pthread_join(t_rdr, NULL);
    }
    CHECK_MSG(bad.violations == 0u, "stress_corrupt_payload_never_accepted");
    CHECK_MSG((bad.invalid_count + bad.unauthorized_count + bad.quota_count) ==
                  bad.iterations,
              "stress_corrupt_payload_known_reject_codes_only");
    CHECK_MSG(sub.invalid_count == 0u, "stress_valid_payload_never_invalid");
    CHECK_MSG(sub.other_count == 0u, "stress_valid_payload_known_codes_only");
    CHECK_MSG(rdr.violations == 0u, "stress_reader_payload_shape_stable");
    CHECK_MSG(rdr.stale_bindings == 0u, "stress_reader_no_stale_generation_binding");
    app_host_control_session_close();

    ctl_init_fake();
    g_now_ms = 0u;
    CHECK(ctl_open(0x80u, 512u, 2048u, &gen) == APP_CONTROL_OK);
    memset(&sub, 0, sizeof(sub));
    memset(&bad, 0, sizeof(bad));
    sub.iterations = 2000u;
    bad.iterations = 2000u;
    {
        pthread_t t_p;
        pthread_t t_q;

        CHECK(pthread_create(&t_p, NULL, stress_valid_p, &sub) == 0);
        CHECK(pthread_create(&t_q, NULL, stress_dup_q, &bad) == 0);
        pthread_join(t_p, NULL);
        pthread_join(t_q, NULL);
    }
    CHECK_MSG(sub.ok_count == sub.iterations, "stress_parallel_valid_all_accepted");
    CHECK_MSG(sub.other_count == 0u, "stress_parallel_valid_no_other_result");
    CHECK_MSG(bad.violations == 0u, "stress_parallel_duplicate_never_accepted");
    CHECK_MSG(bad.invalid_count == bad.iterations,
              "stress_parallel_duplicate_always_rejected");
    app_host_control_session_close();
}

static void test_snapshot_session_switch_gates(void)
{
    uint32_t gen = 0u;
    app_control_snapshot_view_t view;
    uint8_t tmp[APP_CONTROL_HEADER_SIZE + 128u];
    char json[96];
    uint32_t total;
    uint32_t i;

    ctl_init_fake();
    g_now_ms = 0u;
    memset(&view, 0, sizeof(view));
    snprintf(json, sizeof(json),
             "{\"seq\":7,\"pad\":\"%s\"}",
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    total = build_snapshot(tmp, sizeof(tmp), json, 1u, 1u, 0u, 0u, 0u, 0u);
    CHECK(total > 96u);
    CHECK(ctl_open(0x80u, 512u, 2048u, &gen) == APP_CONTROL_OK);
    CHECK(app_host_control_snapshot_submit(tmp, total) == APP_CONTROL_OK);
    app_host_control_session_close();
    CHECK(app_host_control_snapshot_read(&view, NULL, 0u, NULL) == APP_CONTROL_NOT_FOUND);

    CHECK(ctl_open(0x80u, 512u, 64u, &gen) == APP_CONTROL_OK);
    CHECK_MSG(app_host_control_snapshot_submit(tmp, total) == APP_CONTROL_QUOTA_EXCEEDED,
              "session_switch_quota_gate_uses_new_session");
    CHECK(app_host_control_snapshot_read(&view, NULL, 0u, NULL) == APP_CONTROL_NOT_FOUND);

    app_host_control_session_close();
    CHECK(ctl_open(0x40u, 512u, 2048u, &gen) == APP_CONTROL_OK);
    CHECK_MSG(app_host_control_snapshot_submit(tmp, total) == APP_CONTROL_UNAUTHORIZED,
              "session_switch_no_bit7_unauthorized");
    CHECK(app_host_control_snapshot_read(&view, NULL, 0u, NULL) == APP_CONTROL_NOT_FOUND);

    app_host_control_session_close();
    CHECK(ctl_open(0x80u, 512u, 2048u, &gen) == APP_CONTROL_OK);
    CHECK(app_host_control_snapshot_submit(tmp, total) == APP_CONTROL_OK);
    CHECK(app_host_control_snapshot_read(&view, NULL, 0u, NULL) == APP_CONTROL_OK);
    CHECK_MSG(view.session_generation == gen, "resubmit_binds_current_generation");
    CHECK_MSG(view.payload_len == (uint32_t)strlen(json), "resubmit_payload_len_matches");
    for (i = 0u; json[i] != '\0'; i++) {
        CHECK(json[i] == (char)tmp[APP_CONTROL_HEADER_SIZE + i]);
    }
    app_host_control_session_close();
}

int main(void)
{
    test_json_validator();
    test_json_unicode_keys();
    test_open_and_generation();
    test_v4_live_session_open();
    test_v4_enable_linearization_wire();
    test_register_decisions();
    test_deliver_wire_and_receipt_guards();
    test_cross_session_isolation();
    test_snapshot_lifecycle();
    test_snapshot_session_isolation_concurrent();
    test_snapshot_session_switch_gates();
    test_legacy_signed_packages_rejected();
    printf("app_host_control: total=%d fail=%d\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
