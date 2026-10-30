#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include "app_report_service.h"
#include "app_report_service_device.h"
#include "cmsis_os2.h"
#include "netif_manager.h"
#include "mqtt_service.h"
#include "webhook_service.h"
#include "debug.h"
#include "storage.h"

static int g_total;
static int g_fail;

#define CHECK(cond) \
    do { \
        g_total++; \
        if (!(cond)) { \
            g_fail++; \
            printf("FAIL app_report:%d %s\n", g_total, #cond); \
        } \
    } while (0)

#define BOOT_A 0x5A5A0001u
#define BOOT_B 0x17170002u
#define DEV_ID "aa:bb:cc:dd:ee:66"
#define PLACEHOLDER "ne301-app:line-crossing"

static uint64_t now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static void sleep_us(uint32_t us)
{
    struct timespec ts;

    if (us == 0u) {
        return;
    }
    ts.tv_sec = (time_t)(us / 1000000u);
    ts.tv_nsec = (long)(us % 1000000u) * 1000L;
    (void)nanosleep(&ts, NULL);
}

static void pump_many(int n)
{
    for (int i = 0; i < n; i++) {
        app_report_service_pump();
    }
}

static const uint8_t *span_find(const uint8_t *hay, uint32_t hay_len,
                                const char *needle)
{
    size_t n = strlen(needle);

    if (n == 0u || hay_len < (uint32_t)n) {
        return NULL;
    }
    for (uint32_t i = 0u; i <= hay_len - (uint32_t)n; i++) {
        if (memcmp(hay + i, needle, n) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

typedef struct {
    uint32_t mqtt_ready;
    uint32_t webhook_ready;
    uint32_t mqtt_delay_us;
    uint32_t webhook_delay_us;
    int mqtt_script[24];
    int mqtt_script_len;
    int mqtt_script_pos;
    int webhook_script[24];
    int webhook_script_len;
    int webhook_script_pos;
    uint8_t mqtt_last[APP_REPORT_SLOT_CAP];
    uint32_t mqtt_last_len;
    uint32_t mqtt_calls;
    uint8_t webhook_last[APP_REPORT_SLOT_CAP];
    uint32_t webhook_last_len;
    uint32_t webhook_calls;
} fake_transport_t;

static fake_transport_t g_ft;
static uint8_t g_device_policy_blob[APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY];
static uint32_t g_device_policy_len;

int storage_nvs_read(NVS_Type_t type, const char *key, void *data, size_t len)
{
    if (type != NVS_USER || strcmp(key, "app_report_policy") != 0 ||
        data == NULL || len < g_device_policy_len) {
        return -EINVAL;
    }
    if (g_device_policy_len == 0u) {
        return -ENOENT;
    }
    memcpy(data, g_device_policy_blob, g_device_policy_len);
    return (int)g_device_policy_len;
}

int storage_nvs_write(NVS_Type_t type, const char *key, const void *data,
                      size_t len)
{
    if (type != NVS_USER || strcmp(key, "app_report_policy") != 0 ||
        data == NULL || len > sizeof(g_device_policy_blob)) {
        return -EINVAL;
    }
    memcpy(g_device_policy_blob, data, len);
    g_device_policy_len = (uint32_t)len;
    return (int)len;
}

static uint8_t g_device_install_blob[APP_INSTALL_RECORD_BIN_SIZE];

static app_install_io_result_t dev_install_kv_read(void *ctx, const char *key,
                                                   uint8_t *buf, size_t cap,
                                                   size_t *len)
{
    (void)ctx;
    if (strcmp(key, APP_INSTALL_KEY_RECORD) != 0) {
        return APP_INSTALL_IO_NOT_FOUND;
    }
    if (buf == NULL || len == NULL || cap < sizeof(g_device_install_blob)) {
        return APP_INSTALL_IO_ERROR;
    }
    memcpy(buf, g_device_install_blob, sizeof(g_device_install_blob));
    *len = sizeof(g_device_install_blob);
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t dev_install_kv_write(void *ctx, const char *key,
                                                    const uint8_t *data,
                                                    size_t len)
{
    (void)ctx;
    (void)key;
    (void)data;
    (void)len;
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t dev_install_kv_delete(void *ctx, const char *key)
{
    (void)ctx;
    (void)key;
    return APP_INSTALL_IO_OK;
}

static aicam_bool_t dev_install_tx_begin(void *ctx)
{
    (void)ctx;
    return AICAM_TRUE;
}

static void dev_install_tx_end(void *ctx)
{
    (void)ctx;
}

static const app_install_io_t g_device_install_io = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    dev_install_kv_write,
    dev_install_kv_read,
    dev_install_kv_delete,
    NULL,
    dev_install_tx_begin,
    dev_install_tx_end,
    NULL,
    NULL,
    NULL
};

static void dev_install_fill_identity(void)
{
    app_install_record_t record;
    char path[APP_INSTALL_PATH_MAX];
    size_t pos;
    uint32_t i;

    memset(&record, 0, sizeof(record));
    memcpy(record.publisher_id, "ne301-exp-pub", sizeof("ne301-exp-pub"));
    memcpy(record.app_id, "line-crossing", sizeof("line-crossing"));
    memset(record.publisher_key_sha256, 0xA5, APP_INSTALL_SHA256_LEN);
    memset(record.tbs_sha256, 0x7E, APP_INSTALL_SHA256_LEN);
    memcpy(record.package_sha256, record.tbs_sha256, APP_INSTALL_SHA256_LEN);
    record.version_major = 1u;
    record.package_len = 1u;
    pos = (size_t)snprintf(path, sizeof(path), "%s/", APP_INSTALL_PKG_DIR);
    for (i = 0u; i < APP_INSTALL_SHA256_LEN; i++) {
        snprintf(path + pos, sizeof(path) - pos, "%02x",
                 record.package_sha256[i]);
        pos += 2u;
    }
    snprintf(path + pos, sizeof(path) - pos, "%s", ".neapp");
    memcpy(record.package_path, path, strlen(path) + 1u);
    (void)app_install_record_encode(&record, g_device_install_blob,
                                    sizeof(g_device_install_blob));
}

const app_install_io_t *app_installer_device_io(void)
{
    dev_install_fill_identity();
    return &g_device_install_io;
}

static void ft_delay(uint32_t us)
{
    if (us != 0u) {
        sleep_us(us);
    }
}

static int ft_mqtt_publish(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    if (app_report_service_transport_entered(APP_REPORT_CHANNEL_MQTT) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    ft_delay(g_ft.mqtt_delay_us);
    g_ft.mqtt_calls++;
    if ((size_t)len <= sizeof(g_ft.mqtt_last)) {
        memcpy(g_ft.mqtt_last, json, len);
        g_ft.mqtt_last_len = len;
    }
    if (g_ft.mqtt_script_pos < g_ft.mqtt_script_len) {
        return g_ft.mqtt_script[g_ft.mqtt_script_pos++];
    }
    return APP_REPORT_TRANSPORT_OK;
}

static int ft_webhook_post(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    if (app_report_service_transport_entered(APP_REPORT_CHANNEL_WEBHOOK) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    ft_delay(g_ft.webhook_delay_us);
    g_ft.webhook_calls++;
    if ((size_t)len <= sizeof(g_ft.webhook_last)) {
        memcpy(g_ft.webhook_last, json, len);
        g_ft.webhook_last_len = len;
    }
    if (g_ft.webhook_script_pos < g_ft.webhook_script_len) {
        return g_ft.webhook_script[g_ft.webhook_script_pos++];
    }
    return APP_REPORT_TRANSPORT_OK;
}

static uint32_t ft_mqtt_ready(void *user)
{
    (void)user;
    return g_ft.mqtt_ready;
}

static uint32_t ft_webhook_ready(void *user)
{
    (void)user;
    return g_ft.webhook_ready;
}

static void ft_reset(void)
{
    memset(&g_ft, 0, sizeof(g_ft));
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_READY;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_READY;
}

static const app_report_transport_t g_transport = {
    NULL,
    ft_mqtt_ready,
    ft_mqtt_publish,
    ft_webhook_ready,
    ft_webhook_post
};

static aicam_bool_t ident_device_id(void *user, char *out, uint32_t cap)
{
    (void)user;
    if (out == NULL || cap < (uint32_t)sizeof(DEV_ID)) {
        return AICAM_FALSE;
    }
    memcpy(out, DEV_ID, sizeof(DEV_ID));
    return AICAM_TRUE;
}

static aicam_bool_t ident_device_id_fail(void *user, char *out, uint32_t cap)
{
    (void)user;
    (void)out;
    (void)cap;
    return AICAM_FALSE;
}

static uint32_t g_boot_source = BOOT_A;

static uint32_t ident_boot_id(void *user)
{
    (void)user;
    return g_boot_source;
}

static const app_report_identity_t g_identity = {
    NULL,
    ident_device_id,
    ident_boot_id
};

static app_report_session_t g_session;

static void session_fill(app_report_session_t *s, const char *app,
                         const uint8_t key, uint32_t report_max,
                         aicam_bool_t active)
{
    memset(s, 0, sizeof(*s));
    strncpy(s->publisher_id, "ne301-exp-pub", APP_REPORT_PUBLISHER_ID_SLOT);
    strncpy(s->app_id, app, APP_REPORT_APP_ID_SLOT);
    memset(s->publisher_key_sha256, key, APP_REPORT_SHA256_LEN);
    s->report_max = report_max;
    s->active = active;
}

static void reset_all(void)
{
    ft_reset();
    g_boot_source = BOOT_A;
    memset(g_device_policy_blob, 0, sizeof(g_device_policy_blob));
    g_device_policy_len = 0u;
    app_report_service_set_policy_storage(NULL);
    app_report_service_init(&g_identity, &g_transport);
    session_fill(&g_session, "line-crossing", 0xA5, APP_REPORT_INPUT_MAX,
                 AICAM_TRUE);
    if (app_report_service_set_session(&g_session) != APP_REPORT_ABI_OK) {
        printf("FATAL: session bind failed\n");
        exit(1);
    }
}

static size_t build_report(char *buf, size_t cap, uint32_t seq,
                           const char *device_id, const char *boot_id_text,
                           const char *pad, int with_device)
{
    size_t n = 0;

    n += (size_t)snprintf(buf + n, cap - n,
                          "{\"schema_version\":1,\"type\":\"line_counting\",");
    if (with_device != 0) {
        n += (size_t)snprintf(buf + n, cap - n, "\"device_id\":\"%s\",",
                              device_id);
    }
    n += (size_t)snprintf(
        buf + n, cap - n,
        "\"app\":{\"app_id\":\"line-crossing\",\"sw_version\":\"1.0.0\"},"
        "\"boot_id\":%s,\"report_seq\":%u,"
        "\"clock_valid\":false,\"reported_at\":null,"
        "\"window\":{\"start_ms\":0,\"end_ms\":1000,\"in\":1,\"out\":0},"
        "\"total\":{\"in\":5,\"out\":3},"
        "\"counter\":{\"counter_name\":\"%s\"},"
        "\"target\":{\"class_name\":\"person\"},"
        "\"model\":{\"name\":\"yolo26n\",\"version\":\"1.0\"}}",
        boot_id_text, (unsigned int)seq, pad);
    return n;
}

static const char *g_report_tail =
    ",\"app\":{\"app_id\":\"line-crossing\",\"sw_version\":\"1.0.0\"},"
    "\"boot_id\":%s,\"report_seq\":%u,"
    "\"clock_valid\":false,\"reported_at\":null,"
    "\"window\":{\"start_ms\":0,\"end_ms\":1000,\"in\":1,\"out\":0},"
    "\"total\":{\"in\":5,\"out\":3},"
    "\"counter\":{\"counter_name\":\"d\"},"
    "\"target\":{\"class_name\":\"person\"},"
    "\"model\":{\"name\":\"yolo26n\",\"version\":\"1.0\"}}";

static void test_session_gates(void)
{
    app_report_session_t inactive;
    app_report_session_t other;
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;

    reset_all();
    CHECK(app_report_service_boot_id() == BOOT_A);
    CHECK(app_report_service_queue_count() == 0u);

    rlen = build_report(rbuf, sizeof(rbuf), 1u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(NULL, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(boot == 0u);

    CHECK(app_report_service_set_session(NULL) == APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);

    session_fill(&inactive, "line-crossing", 0xA5, APP_REPORT_INPUT_MAX,
                 AICAM_FALSE);
    CHECK(app_report_service_set_session(&inactive) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    CHECK(app_report_submit(&inactive, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);

    session_fill(&other, "other-app", 0xA5, APP_REPORT_INPUT_MAX, AICAM_TRUE);
    CHECK(app_report_service_set_session(&other) == APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&other, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_queue_count() == 0u);

    {
        app_report_session_t zero_quota;
        session_fill(&zero_quota, "line-crossing", 0xA5, 0u, AICAM_TRUE);
        CHECK(app_report_service_set_session(&zero_quota) ==
              APP_REPORT_ABI_INVALID_ARGUMENT);
    }
}

static void test_param_rejections(void)
{
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t mstate;
    uint32_t wstate;
    char nul_payload[256];

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 1u, PLACEHOLDER, "0", "d", 1);

    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 1u, NULL) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    CHECK(app_report_submit(&g_session, NULL, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    CHECK(app_report_submit(&g_session, rbuf, 0u, 1u, &boot) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);

    memset(nul_payload, 'x', sizeof(nul_payload));
    nul_payload[64] = '\0';
    CHECK(app_report_submit(&g_session, nul_payload, sizeof(nul_payload), 1u,
                            &boot) == APP_REPORT_ABI_INVALID_ARGUMENT);

    CHECK(app_report_status(BOOT_A, 1u, NULL, &wstate) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    CHECK(app_report_status(BOOT_A, 1u, &mstate, NULL) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    CHECK(app_report_service_queue_count() == 0u);
}

static void test_quota(void)
{
    app_report_session_t small;
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 1u, PLACEHOLDER, "0", "d", 1);

    session_fill(&small, "line-crossing", 0xA5, 100u, AICAM_TRUE);
    CHECK(app_report_service_set_session(&small) == APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&small, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_QUOTA_EXCEEDED);
    CHECK(boot == 0u);
    CHECK(app_report_service_queue_count() == 0u);

    reset_all();
    {
        static char over[APP_REPORT_INPUT_MAX + 8u];
        size_t head;
        size_t target = (size_t)APP_REPORT_INPUT_MAX + 1u;

        head = (size_t)snprintf(
            over, sizeof(over),
            "{\"schema_version\":1,\"type\":\"line_counting\","
            "\"device_id\":\"%s\",\"app\":{\"app_id\":\"line-crossing\"},"
            "\"boot_id\":0,\"report_seq\":9,\"window\":{},\"total\":{},"
            "\"counter\":{\"counter_name\":\"", PLACEHOLDER);
        for (size_t i = head; i < target - 2u; i++) {
            over[i] = 'p';
        }
        over[target - 2u] = '"';
        over[target - 1u] = '}';
        over[target] = '\0';
        CHECK(strlen(over) == target);
        CHECK(app_report_submit(&g_session, over, (uint32_t)strlen(over), 9u,
                                &boot) == APP_REPORT_ABI_QUOTA_EXCEEDED);
        CHECK(app_report_service_queue_count() == 0u);
    }
}

static void test_json_validity(void)
{
    uint32_t boot = 0;

    reset_all();
    {
        const char *bad[] = {
            "not json",
            "{",
            "[]",
            "{\"a\":1}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1} x",
            "{\"schema_version\":1,\"type\":\"line_count\ting\",\"report_seq\":1}",
            "{\"schema_version\":1,\"type\":\"line_counting\\x\",\"report_seq\":1}",
            "{\"schema_version\":01,\"type\":\"line_counting\",\"report_seq\":1}",
            "{\"schema_version\":1,,\"type\":\"line_counting\",\"report_seq\":1}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,}",
            "{\"schema_version\":1,'type':'line_counting',\"report_seq\":1}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":+1}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1 \"x\":2}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1e2}",
            ("{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
             "\"window\":{}}extra")};
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            CHECK(app_report_submit(&g_session, bad[i], (uint32_t)strlen(bad[i]),
                                    1u, &boot) == APP_REPORT_ABI_INVALID_ARGUMENT);
        }        CHECK(app_report_service_queue_count() == 0u);
    }
}

static void test_schema_rejections(void)
{
    uint32_t boot = 0;

    reset_all();
    {
        const char *schema2 =
            "{\"schema_version\":2,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}";
        CHECK(app_report_submit(&g_session, schema2, (uint32_t)strlen(schema2),
                                1u, &boot) == APP_REPORT_ABI_INCOMPATIBLE);
    }
    {
        const char *wrong_type =
            "{\"schema_version\":1,\"type\":\"face_count\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}";
        CHECK(app_report_submit(&g_session, wrong_type, (uint32_t)strlen(wrong_type),
                                1u, &boot) == APP_REPORT_ABI_INCOMPATIBLE);
    }
    {
        const char *missing[] = {
            "{\"type\":\"line_counting\",\"device_id\":\"x\",\"boot_id\":0,"
            "\"report_seq\":1,\"window\":{},\"total\":{},\"counter\":{},"
            "\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"device_id\":\"x\",\"boot_id\":0,"
            "\"report_seq\":1,\"window\":{},\"total\":{},\"counter\":{},"
            "\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"window\":{},\"total\":{},\"counter\":{},"
            "\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1,\"total\":{},\"counter\":{},"
            "\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1,\"window\":[],\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":true,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}"
        };
        for (size_t i = 0; i < sizeof(missing) / sizeof(missing[0]); i++) {
            CHECK(app_report_submit(&g_session, missing[i],
                                    (uint32_t)strlen(missing[i]), 1u, &boot) ==
                  APP_REPORT_ABI_INVALID_ARGUMENT);
        }
    }
    {
        const char *bad_fields[] = {
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":5,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":7,"
            "\"boot_id\":0,\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":\"zero\",\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":-4,\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1.5,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":5,\"device_id\":\"x\","
            "\"boot_id\":0,\"report_seq\":1,\"window\":{},\"total\":{},"
            "\"counter\":{},\"target\":{},\"model\":{}}"
        };
        for (size_t i = 0; i < sizeof(bad_fields) / sizeof(bad_fields[0]); i++) {
            CHECK(app_report_submit(&g_session, bad_fields[i],
                                    (uint32_t)strlen(bad_fields[i]), 1u, &boot) ==
                  APP_REPORT_ABI_INVALID_ARGUMENT);
        }
    }
    {
        const char *dup[] = {
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"a\","
            "\"device_id\":\"b\",\"boot_id\":0,\"report_seq\":1,\"window\":{},"
            "\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"a\","
            "\"boot_id\":0,\"boot_id\":0,\"report_seq\":1,\"window\":{},"
            "\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"device_id\":\"a\","
            "\"boot_id\":0,\"report_seq\":1,\"report_seq\":1,\"window\":{},"
            "\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}",
            "{\"schema_version\":1,\"type\":\"line_counting\",\"zz\":1,\"zz\":2,"
            "\"device_id\":\"a\",\"boot_id\":0,\"report_seq\":1,\"window\":{},"
            "\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}"
        };
        for (size_t i = 0; i < sizeof(dup) / sizeof(dup[0]); i++) {
            CHECK(app_report_submit(&g_session, dup[i], (uint32_t)strlen(dup[i]),
                                    1u, &boot) ==
                  APP_REPORT_ABI_INVALID_ARGUMENT);
        }
    }
    CHECK(app_report_service_queue_count() == 0u);
}

static void test_normalization_gaps_dedup(void)
{
    uint32_t boot = 0;
    char rbuf[1024];
    char tail[512];
    size_t rlen;
    app_report_entry_info_t info;
    const uint8_t *found;
    const uint8_t *found_tail;

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 1u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_A);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_entry(0u, &info) == AICAM_TRUE);
    CHECK(info.report_seq == 1u);
    CHECK(info.host_boot_id == BOOT_A);
    CHECK(info.mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(info.webhook_state == APP_REPORT_WEBHOOK_PENDING);
    CHECK(app_report_service_entry(APP_REPORT_QUEUE_SLOTS, &info) == AICAM_FALSE);

    app_report_service_pump();
    CHECK(g_ft.mqtt_calls == 1u);
    CHECK(g_ft.webhook_calls == 1u);
    CHECK(g_ft.mqtt_last_len > 0u);
    CHECK(g_ft.mqtt_last_len == g_ft.webhook_last_len);
    CHECK(memcmp(g_ft.mqtt_last, g_ft.webhook_last, g_ft.mqtt_last_len) == 0);
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, DEV_ID) != NULL);
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, PLACEHOLDER) == NULL);
    {
        char boot_text[24];
        (void)snprintf(boot_text, sizeof(boot_text), "\"boot_id\":%u",
                       (unsigned int)BOOT_A);
        CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, boot_text) != NULL);
    }
    {
        char seq_text[24];
        (void)snprintf(seq_text, sizeof(seq_text), "\"report_seq\":1,");
        CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, seq_text) != NULL);
    }
    {
        snprintf(tail, sizeof(tail), g_report_tail, "0", 1u);
        found_tail = (const uint8_t *)strstr(tail, ",\"report_seq\"");
        CHECK(found_tail != NULL);
        found = (const uint8_t *)span_find(g_ft.mqtt_last, g_ft.mqtt_last_len,
                                           (const char *)found_tail);
        CHECK(found != NULL);
        CHECK((uint32_t)((uintptr_t)found - (uintptr_t)g_ft.mqtt_last) +
                  (uint32_t)strlen((const char *)found_tail) ==
              g_ft.mqtt_last_len);
    }

    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_A);
    CHECK(app_report_service_queue_count() == 1u);

    rlen = build_report(rbuf, sizeof(rbuf), 3u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 3u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 2u);
    app_report_service_pump();
    CHECK(g_ft.mqtt_calls == 2u);
    CHECK(g_ft.webhook_calls == 2u);

    {
        uint32_t ms;
        uint32_t ws;
        CHECK(app_report_status(BOOT_A, 1u, &ms, &ws) == APP_REPORT_ABI_OK);
        CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
        CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
        CHECK(app_report_status(BOOT_A, 3u, &ms, &ws) == APP_REPORT_ABI_OK);
        CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
        CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
        CHECK(app_report_status(BOOT_A, 2u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
        CHECK(app_report_status(BOOT_A, 999u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
    }
    app_report_service_get_stats(NULL);
}

static void test_insertion_when_identity_absent(void)
{
    uint32_t boot = 0;
    char rbuf[1024];
    size_t rlen;
    const char *needle;

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 4u, "x", "0", "d", 0);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 4u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(g_ft.mqtt_calls == 1u);
    needle = "\"type\":\"line_counting\",\"device_id\":\"" DEV_ID "\",";
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, needle) != NULL);
    needle = ",\"boot_id\":";
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, needle) != NULL);
}

static void test_forged_device_id(void)
{
    uint32_t boot = 0;
    char rbuf[1024];
    size_t rlen;

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 5u, "de:ad:be:ef:00:11", "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 5u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, DEV_ID) != NULL);
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, "de:ad:be:ef:00:11") ==
          NULL);
}

static void test_identity_fail_closed(void)
{
    app_report_session_t s;
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    const app_report_identity_t broken = { NULL, ident_device_id_fail,
                                           ident_boot_id };

    session_fill(&s, "line-crossing", 0xA5, APP_REPORT_INPUT_MAX, AICAM_TRUE);
    ft_reset();
    app_report_service_init(&broken, &g_transport);
    CHECK(app_report_service_boot_id() == BOOT_A);
    CHECK(app_report_service_set_session(&s) == APP_REPORT_ABI_OK);
    rlen = build_report(rbuf, sizeof(rbuf), 1u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&s, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_STORAGE_UNKNOWN);
    CHECK(app_report_service_queue_count() == 0u);

    g_boot_source = 0u;
    app_report_service_init(&g_identity, &g_transport);
    CHECK(app_report_service_boot_id() == 0u);
    CHECK(app_report_service_set_session(&s) == APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&s, rbuf, (uint32_t)rlen, 1u, &boot) ==
          APP_REPORT_ABI_STORAGE_UNKNOWN);
    CHECK(app_report_service_queue_count() == 0u);
}

static void test_channel_independence(void)
{
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t ms;
    uint32_t ws;

    reset_all();
    for (int i = 0; i < 24; i++) {
        g_ft.mqtt_script[i] = APP_REPORT_TRANSPORT_FAIL;
    }
    g_ft.mqtt_script_len = 24;
    rlen = build_report(rbuf, sizeof(rbuf), 10u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 10u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 10u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_PENDING);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    CHECK(g_ft.mqtt_calls == 1u);
    CHECK(g_ft.webhook_calls == 1u);
    pump_many(2 * (int)APP_REPORT_BACKOFF_POLLS + 3);
    CHECK(app_report_status(BOOT_A, 10u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_TRANSPORT_FAILED);
    CHECK(g_ft.mqtt_calls == 3u);
    CHECK(g_ft.webhook_calls == 1u);
    CHECK(app_report_service_queue_count() == 1u);

    reset_all();
    g_ft.mqtt_script[0] = APP_REPORT_TRANSPORT_FAIL;
    g_ft.mqtt_script[1] = APP_REPORT_TRANSPORT_FAIL;
    g_ft.mqtt_script[2] = APP_REPORT_TRANSPORT_OK;
    g_ft.mqtt_script_len = 3;
    rlen = build_report(rbuf, sizeof(rbuf), 11u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 11u, &boot) ==
          APP_REPORT_ABI_OK);
    pump_many(2 * (int)APP_REPORT_BACKOFF_POLLS + 3);
    CHECK(app_report_status(BOOT_A, 11u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
    CHECK(g_ft.mqtt_calls == 3u);

    reset_all();
    g_ft.mqtt_script[0] = APP_REPORT_TRANSPORT_UNKNOWN;
    g_ft.mqtt_script_len = 1;
    rlen = build_report(rbuf, sizeof(rbuf), 12u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 12u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 12u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    pump_many(3);
    CHECK(g_ft.mqtt_calls == 1u);
    CHECK(app_report_status(BOOT_A, 12u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);

    reset_all();
    for (int i = 0; i < 24; i++) {
        g_ft.webhook_script[i] = APP_REPORT_TRANSPORT_FAIL;
    }
    g_ft.webhook_script_len = 24;
    rlen = build_report(rbuf, sizeof(rbuf), 13u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 13u, &boot) ==
          APP_REPORT_ABI_OK);
    pump_many(2 * (int)APP_REPORT_BACKOFF_POLLS + 3);
    CHECK(app_report_status(BOOT_A, 13u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_FAILED);
}

static void test_channel_disabled(void)
{
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t ms;
    uint32_t ws;

    reset_all();
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_UNCONFIGURED;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_UNCONFIGURED;
    rlen = build_report(rbuf, sizeof(rbuf), 20u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 20u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_status(BOOT_A, 20u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_PENDING);
    CHECK(ws == APP_REPORT_WEBHOOK_PENDING);
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 20u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_NOT_CONFIGURED);
    CHECK(ws == APP_REPORT_WEBHOOK_NOT_CONFIGURED);
    CHECK(g_ft.mqtt_calls == 0u);
    CHECK(g_ft.webhook_calls == 0u);
    CHECK(app_report_service_queue_count() == 1u);
    pump_many(3);
    CHECK(app_report_status(BOOT_A, 20u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_NOT_CONFIGURED);
    CHECK(ws == APP_REPORT_WEBHOOK_NOT_CONFIGURED);
    CHECK(g_ft.mqtt_calls == 0u);
    CHECK(g_ft.webhook_calls == 0u);

    reset_all();
    g_ft.webhook_ready = APP_REPORT_CHANNEL_UNCONFIGURED;
    rlen = build_report(rbuf, sizeof(rbuf), 21u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 21u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 21u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
    CHECK(ws == APP_REPORT_WEBHOOK_NOT_CONFIGURED);

    reset_all();
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_UNCONFIGURED;
    rlen = build_report(rbuf, sizeof(rbuf), 22u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 22u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 22u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_NOT_CONFIGURED);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
}

static void test_queue_bounds(void)
{
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;

    reset_all();
    for (int i = 0; i < 24; i++) {
        g_ft.mqtt_script[i] = APP_REPORT_TRANSPORT_FAIL;
        g_ft.webhook_script[i] = APP_REPORT_TRANSPORT_FAIL;
    }
    g_ft.mqtt_script_len = 24;
    g_ft.webhook_script_len = 24;
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        rlen = build_report(rbuf, sizeof(rbuf), 30u + i, PLACEHOLDER, "0", "d", 1);
        CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 30u + i,
                                &boot) == APP_REPORT_ABI_OK);
    }
    rlen = build_report(rbuf, sizeof(rbuf), 99u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 99u, &boot) ==
          APP_REPORT_ABI_BUSY);
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    pump_many(2 * (int)APP_REPORT_BACKOFF_POLLS + 3);
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        uint32_t ms;
        uint32_t ws;
        CHECK(app_report_status(BOOT_A, 30u + i, &ms, &ws) == APP_REPORT_ABI_OK);
        CHECK(ms == APP_REPORT_MQTT_TRANSPORT_FAILED);
        CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_FAILED);
    }
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 99u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    {
        uint32_t ms;
        uint32_t ws;
        CHECK(app_report_status(BOOT_A, 30u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
        CHECK(app_report_status(BOOT_A, 31u, &ms, &ws) == APP_REPORT_ABI_OK);
    }
}

static void test_restart_and_key_change(void)
{
    app_report_session_t rotated;
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t ms;
    uint32_t ws;

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 40u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 40u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_A);

    g_boot_source = BOOT_B;
    app_report_service_init(&g_identity, &g_transport);
    session_fill(&g_session, "line-crossing", 0xA5, APP_REPORT_INPUT_MAX,
                 AICAM_TRUE);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_boot_id() == BOOT_B);
    CHECK(app_report_status(BOOT_A, 40u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_status(BOOT_B, 40u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 40u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_B);
    CHECK(app_report_service_queue_count() == 1u);

    session_fill(&rotated, "line-crossing", 0x5B, APP_REPORT_INPUT_MAX,
                 AICAM_TRUE);
    CHECK(app_report_service_set_session(&rotated) == APP_REPORT_ABI_OK);
    CHECK(app_report_status(BOOT_B, 40u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 41u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    rlen = build_report(rbuf, sizeof(rbuf), 41u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&rotated, rbuf, (uint32_t)rlen, 41u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_B);
    CHECK(app_report_status(BOOT_B, 41u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(app_report_status(BOOT_B, 40u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
}

static void test_input_budget_boundary(void)
{
    uint32_t boot = 0;
    static char exact[APP_REPORT_INPUT_MAX + 1u];
    size_t head;
    size_t target = (size_t)APP_REPORT_INPUT_MAX;

    reset_all();
    const char *tail = "\"},\"target\":{},\"model\":{}}";
    size_t tail_len = strlen(tail);
    head = (size_t)snprintf(
        exact, sizeof(exact),
        "{\"schema_version\":1,\"type\":\"line_counting\","
        "\"device_id\":\"%s\",\"app\":{\"app_id\":\"line-crossing\"},"
        "\"boot_id\":0,\"report_seq\":50,\"window\":{},\"total\":{},"
        "\"counter\":{\"counter_name\":\"", PLACEHOLDER);
    CHECK(head < target - 40u);
    for (size_t i = head; i < target - tail_len; i++) {
        exact[i] = 'n';
    }
    snprintf(exact + (target - tail_len), sizeof(exact) - (target - tail_len),
             "%s", tail);
    CHECK(strlen(exact) == target);
    CHECK(app_report_submit(&g_session, exact, (uint32_t)strlen(exact), 50u,
                            &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    app_report_service_pump();
    CHECK(g_ft.mqtt_last_len > (uint32_t)strlen(exact));
    CHECK(g_ft.mqtt_last_len <= APP_REPORT_SLOT_CAP);
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, "\"report_seq\":50") !=
          NULL);
    CHECK(span_find(g_ft.mqtt_last, g_ft.mqtt_last_len, "counter_name\":\"nnnn") !=
          NULL);

    reset_all();
    {
        static char over[APP_REPORT_INPUT_MAX + 8u];
        size_t ohead;
        size_t otarget = (size_t)APP_REPORT_INPUT_MAX + 1u;

        ohead = (size_t)snprintf(
            over, sizeof(over),
            "{\"schema_version\":1,\"type\":\"line_counting\","
            "\"device_id\":\"%s\",\"app\":{\"app_id\":\"line-crossing\"},"
            "\"boot_id\":0,\"report_seq\":51,\"window\":{},\"total\":{},"
            "\"counter\":{\"counter_name\":\"", PLACEHOLDER);
        for (size_t i = ohead; i < otarget - 2u; i++) {
            over[i] = 'p';
        }
        over[otarget - 2u] = '"';
        over[otarget - 1u] = '}';
        over[otarget] = '\0';
        CHECK(strlen(over) == otarget);
        CHECK(app_report_submit(&g_session, over, (uint32_t)strlen(over), 51u,
                                &boot) == APP_REPORT_ABI_QUOTA_EXCEEDED);
        CHECK(app_report_service_queue_count() == 0u);
    }
}

static void test_stats_and_misc(void)
{
    app_report_stats_t st;
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    app_report_entry_info_t info;

    reset_all();
    app_report_service_pump();
    CHECK(app_report_service_queue_count() == 0u);

    rlen = build_report(rbuf, sizeof(rbuf), 60u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 60u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 60u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&g_session, "{bad", 4u, 61u, &boot) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    app_report_service_get_stats(&st);
    CHECK(st.accepted == 1u);
    CHECK(st.dedup_hits == 1u);
    CHECK(st.rejected_schema == 1u);
    CHECK(st.rejected_unauthorized == 0u);
    CHECK(app_report_service_entry(APP_REPORT_QUEUE_SLOTS, &info) == AICAM_FALSE);
    CHECK(strcmp(app_report_result_str(APP_REPORT_ABI_OK), "OK") == 0);
    CHECK(strcmp(app_report_result_str(APP_REPORT_ABI_BUSY), "BUSY") == 0);
    CHECK(strcmp(app_report_result_str(APP_REPORT_ABI_NOT_FOUND), "NOT_FOUND") ==
          0);
    CHECK(strcmp(app_report_result_str(APP_REPORT_ABI_QUOTA_EXCEEDED),
                 "QUOTA_EXCEEDED") == 0);
    CHECK(strcmp(app_report_result_str(1234), "UNKNOWN") == 0);
}

static void test_mqtt_outage_recovery(void)
{
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t ms;
    uint32_t ws;
    app_report_entry_info_t info;

    reset_all();
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_WAITING;
    rlen = build_report(rbuf, sizeof(rbuf), 100u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 100u, &boot) ==
          APP_REPORT_ABI_OK);
    pump_many((int)APP_REPORT_BACKOFF_POLLS + 3);
    CHECK(app_report_status(BOOT_A, 100u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_PENDING);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    CHECK(g_ft.mqtt_calls == 0u);
    CHECK(g_ft.webhook_calls == 1u);
    CHECK(app_report_service_entry(0u, &info) == AICAM_TRUE);
    CHECK(info.mqtt_attempts == 0u);
    CHECK(app_report_service_queue_count() == 1u);

    g_ft.mqtt_ready = APP_REPORT_CHANNEL_READY;
    pump_many((int)APP_REPORT_BACKOFF_POLLS + 3);
    CHECK(app_report_status(BOOT_A, 100u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
    CHECK(g_ft.mqtt_calls == 1u);
    CHECK(app_report_status(BOOT_A, 100u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
}

static void test_webhook_temporary_then_recover(void)
{
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t ms;
    uint32_t ws;
    app_report_entry_info_t info;

    reset_all();
    g_ft.webhook_ready = APP_REPORT_CHANNEL_WAITING;
    rlen = build_report(rbuf, sizeof(rbuf), 101u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 101u, &boot) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(app_report_status(BOOT_A, 101u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
    CHECK(ws == APP_REPORT_WEBHOOK_PENDING);
    CHECK(g_ft.webhook_calls == 0u);
    CHECK(g_ft.mqtt_calls == 1u);

    g_ft.webhook_ready = APP_REPORT_CHANNEL_READY;
    g_ft.webhook_script[0] = APP_REPORT_TRANSPORT_FAIL;
    g_ft.webhook_script[1] = APP_REPORT_TRANSPORT_OK;
    g_ft.webhook_script_len = 2;
    pump_many(2 * (int)APP_REPORT_BACKOFF_POLLS + 3);
    CHECK(app_report_status(BOOT_A, 101u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    CHECK(g_ft.webhook_calls == 2u);
    CHECK(app_report_service_entry(0u, &info) == AICAM_TRUE);
    CHECK(info.webhook_attempts == 2u);
    CHECK(info.mqtt_attempts == 1u);
}

static void test_queue_full_outage_recovery(void)
{
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t ms;
    uint32_t ws;

    reset_all();
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_WAITING;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_WAITING;
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        rlen = build_report(rbuf, sizeof(rbuf), 110u + i, PLACEHOLDER, "0", "d",
                            1);
        CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 110u + i,
                                &boot) == APP_REPORT_ABI_OK);
    }
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    rlen = build_report(rbuf, sizeof(rbuf), 118u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 118u, &boot) ==
          APP_REPORT_ABI_BUSY);
    pump_many((int)APP_REPORT_BACKOFF_POLLS + 1);
    CHECK(g_ft.mqtt_calls == 0u);
    CHECK(g_ft.webhook_calls == 0u);
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        CHECK(app_report_status(BOOT_A, 110u + i, &ms, &ws) ==
              APP_REPORT_ABI_OK);
        CHECK(ms == APP_REPORT_MQTT_PENDING);
        CHECK(ws == APP_REPORT_WEBHOOK_PENDING);
    }

    g_ft.mqtt_ready = APP_REPORT_CHANNEL_READY;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_READY;
    app_report_service_pump();
    CHECK(g_ft.mqtt_calls == APP_REPORT_QUEUE_SLOTS);
    CHECK(g_ft.webhook_calls == APP_REPORT_QUEUE_SLOTS);
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        CHECK(app_report_status(BOOT_A, 110u + i, &ms, &ws) ==
              APP_REPORT_ABI_OK);
        CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
        CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    }
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 118u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
}

static aicam_bool_t g_timing_stop;
static aicam_bool_t g_timing_done;

static void *timing_pump_thread(void *arg)
{
    (void)arg;
    while (g_timing_stop != AICAM_TRUE) {
        app_report_service_pump();
    }
    g_timing_done = AICAM_TRUE;
    return NULL;
}

static void test_timing_isolation(void)
{
    pthread_t th;
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t ms;
    uint32_t ws;
    uint64_t t0;
    uint64_t deadline;
    app_report_stats_t st;

    reset_all();
    for (uint32_t i = 0u; i < 3u; i++) {
        rlen = build_report(rbuf, sizeof(rbuf), 120u + i, PLACEHOLDER, "0", "d",
                            1);
        CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 120u + i,
                                &boot) == APP_REPORT_ABI_OK);
    }
    g_ft.mqtt_delay_us = 120000u;
    g_ft.webhook_delay_us = 150000u;
    g_timing_stop = AICAM_FALSE;
    g_timing_done = AICAM_FALSE;
    CHECK(pthread_create(&th, NULL, timing_pump_thread, NULL) == 0);
    sleep_us(400000u);

    t0 = now_ms();
    rlen = build_report(rbuf, sizeof(rbuf), 123u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 123u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(now_ms() - t0 < 50ull);

    t0 = now_ms();
    CHECK(app_report_status(BOOT_A, 120u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(now_ms() - t0 < 50ull);
    CHECK(ws == APP_REPORT_WEBHOOK_PENDING ||
          ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);

    rlen = build_report(rbuf, sizeof(rbuf), 124u, PLACEHOLDER, "0", "d", 1);
    t0 = now_ms();
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 124u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(now_ms() - t0 < 50ull);

    CHECK(g_ft.mqtt_calls + g_ft.webhook_calls >= 2u);

    g_timing_stop = AICAM_TRUE;
    deadline = now_ms() + 8000ull;
    while (g_timing_done != AICAM_TRUE && now_ms() < deadline) {
        sleep_us(20000u);
    }
    CHECK(g_timing_done == AICAM_TRUE);
    (void)pthread_join(th, NULL);

    pump_many(2);
    for (uint32_t i = 0u; i < 5u; i++) {
        CHECK(app_report_status(BOOT_A, 120u + i, &ms, &ws) ==
              APP_REPORT_ABI_OK);
        CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
        CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    }
    app_report_service_get_stats(&st);
    CHECK(st.tx_batches >= 1u);
    CHECK(st.tx_skipped == 0u);
}

static void test_stop_restart_unknown_dedup(void)
{
    uint32_t boot = 0;
    char rbuf[512];
    size_t rlen;
    uint32_t ms;
    uint32_t ws;

    reset_all();
    rlen = build_report(rbuf, sizeof(rbuf), 130u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 130u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);

    app_report_service_restart();
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(BOOT_A, 130u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(ws == APP_REPORT_WEBHOOK_UNKNOWN);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 130u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_A);
    CHECK(app_report_service_queue_count() == 0u);
    app_report_service_pump();
    CHECK(g_ft.mqtt_calls == 0u);
    CHECK(g_ft.webhook_calls == 0u);

    g_boot_source = BOOT_B;
    app_report_service_init(&g_identity, &g_transport);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(app_report_status(BOOT_A, 130u, &ms, &ws) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_status(BOOT_B, 130u, &ms, &ws) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 130u, &boot) ==
          APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_B);
    CHECK(app_report_service_queue_count() == 1u);
}

static volatile int g_svc_mqtt_configured;
static volatile int g_svc_mqtt_running;
static volatile int g_svc_mqtt_qos;
static volatile int g_svc_mqtt_publish_result;
static volatile int g_svc_mqtt_hold;
static volatile int g_svc_mqtt_calls;
static volatile int g_svc_webhook_enabled;
static volatile int g_svc_webhook_result;
static volatile int g_svc_webhook_calls;
static volatile int g_svc_webhook_hold;
static volatile int g_svc_webhook_cleanups;
static volatile int g_os_terminate_calls;
static osThreadId_t g_os_terminate_last_handle;

typedef struct {
    pthread_t th;
    void (*fn)(void *);
    void *arg;
    volatile int done;
    volatile int terminate_requested;
    int in_use;
} dev_stub_thread_t;

#define DEV_STUB_THREAD_MAX 16

static dev_stub_thread_t g_dev_threads[DEV_STUB_THREAD_MAX];
static int g_dev_thread_count;
static int g_dev_mutex_new_count;
static int g_debug_register_calls;
static int g_debug_mutex_new_count_at_register;
static pthread_mutex_t g_first_entry_test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_first_entry_test_cond = PTHREAD_COND_INITIALIZER;
static int g_first_entry_test_active;
static int g_first_start_thread_create_entered;
static int g_first_start_thread_create_active;
static int g_first_stop_acquire_attempted;
static int g_first_stop_done;
static int g_first_stop_done_during_start_create;
static osMutexId_t g_first_start_mutex;
static osMutexId_t g_first_stop_mutex;
static aicam_result_t g_first_start_rc;
static aicam_result_t g_first_stop_rc;
static _Thread_local int g_first_entry_role;

#define DEV_FIRST_ENTRY_START 1
#define DEV_FIRST_ENTRY_STOP 2

static void svc_reset(void)
{
    g_svc_mqtt_configured = 1;
    g_svc_mqtt_running = 1;
    g_svc_mqtt_qos = 0;
    g_svc_mqtt_publish_result = 7;
    g_svc_mqtt_hold = 0;
    g_svc_mqtt_calls = 0;
    g_svc_webhook_enabled = 1;
    g_svc_webhook_result = AICAM_OK;
    g_svc_webhook_calls = 0;
    g_svc_webhook_hold = 0;
    g_svc_webhook_cleanups = 0;
    g_os_terminate_calls = 0;
    g_os_terminate_last_handle = NULL;
}

static _Thread_local dev_stub_thread_t *t_dev_current;

static void *dev_stub_tramp(void *p)
{
    dev_stub_thread_t *t = (dev_stub_thread_t *)p;

    t_dev_current = t;
    t->fn(t->arg);
    t->done = 1;
    return NULL;
}

static void dev_stub_reclaim(void)
{
    for (int i = 0; i < g_dev_thread_count; i++) {
        if (g_dev_threads[i].in_use != 0 &&
            g_dev_threads[i].terminate_requested != 0) {
            (void)pthread_join(g_dev_threads[i].th, NULL);
            g_dev_threads[i].in_use = 0;
        }
    }
}

osMutexId_t osMutexNew(const osMutexAttr_t *attr)
{
    pthread_mutexattr_t a;
    pthread_mutex_t *m;

    (void)attr;
    g_dev_mutex_new_count++;
    m = (pthread_mutex_t *)malloc(sizeof(*m));
    if (m == NULL) {
        return NULL;
    }
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    return (osMutexId_t)m;
}

osStatus_t osMutexAcquire(osMutexId_t id, uint32_t timeout)
{
    (void)timeout;
    if (g_first_entry_test_active != 0 &&
        (g_first_entry_role == DEV_FIRST_ENTRY_START ||
         g_first_entry_role == DEV_FIRST_ENTRY_STOP)) {
        pthread_mutex_lock(&g_first_entry_test_mutex);
        if (g_first_entry_role == DEV_FIRST_ENTRY_START &&
            g_first_start_mutex == NULL) {
            g_first_start_mutex = id;
        }
        if (g_first_entry_role == DEV_FIRST_ENTRY_STOP &&
            g_first_stop_mutex == NULL) {
            g_first_stop_mutex = id;
            g_first_stop_acquire_attempted = 1;
            pthread_cond_broadcast(&g_first_entry_test_cond);
        }
        pthread_mutex_unlock(&g_first_entry_test_mutex);
    }
    return (pthread_mutex_lock((pthread_mutex_t *)id) == 0) ? osOK : osError;
}

osStatus_t osMutexRelease(osMutexId_t id)
{
    return (pthread_mutex_unlock((pthread_mutex_t *)id) == 0) ? osOK : osError;
}

osThreadId_t osThreadNew(void (*fn)(void *), void *arg,
                         const osThreadAttr_t *attr)
{
    dev_stub_thread_t *t;
    int appended;
    int first_entry_start =
        (g_first_entry_test_active != 0 &&
         g_first_entry_role == DEV_FIRST_ENTRY_START);

    (void)attr;
    if (first_entry_start != 0) {
        pthread_mutex_lock(&g_first_entry_test_mutex);
        g_first_start_thread_create_active = 1;
        g_first_start_thread_create_entered = 1;
        pthread_cond_broadcast(&g_first_entry_test_cond);
        while (g_first_stop_acquire_attempted == 0) {
            pthread_cond_wait(&g_first_entry_test_cond,
                              &g_first_entry_test_mutex);
        }
        if (g_first_start_mutex != g_first_stop_mutex) {
            while (g_first_stop_done == 0) {
                pthread_cond_wait(&g_first_entry_test_cond,
                                  &g_first_entry_test_mutex);
            }
        }
        pthread_mutex_unlock(&g_first_entry_test_mutex);
    }
    dev_stub_reclaim();
    t = NULL;
    appended = 0;
    for (int i = 0; i < g_dev_thread_count; i++) {
        if (g_dev_threads[i].in_use == 0) {
            t = &g_dev_threads[i];
            break;
        }
    }
    if (t == NULL && g_dev_thread_count < DEV_STUB_THREAD_MAX) {
        t = &g_dev_threads[g_dev_thread_count];
        g_dev_thread_count++;
        appended = 1;
    }
    if (t != NULL) {
        t->fn = fn;
        t->arg = arg;
        t->done = 0;
        t->terminate_requested = 0;
        if (pthread_create(&t->th, NULL, dev_stub_tramp, t) != 0) {
            if (appended != 0) {
                g_dev_thread_count--;
            }
            t = NULL;
        } else {
            t->in_use = 1;
        }
    }
    if (t == NULL) {
        if (first_entry_start != 0) {
            pthread_mutex_lock(&g_first_entry_test_mutex);
            g_first_start_thread_create_active = 0;
            pthread_cond_broadcast(&g_first_entry_test_cond);
            pthread_mutex_unlock(&g_first_entry_test_mutex);
        }
        return NULL;
    }
    if (first_entry_start != 0) {
        pthread_mutex_lock(&g_first_entry_test_mutex);
        g_first_start_thread_create_active = 0;
        pthread_cond_broadcast(&g_first_entry_test_cond);
        pthread_mutex_unlock(&g_first_entry_test_mutex);
    }
    return (osThreadId_t)t;
}

osThreadState_t osThreadGetState(osThreadId_t id)
{
    dev_stub_thread_t *t = (dev_stub_thread_t *)id;

    if (t == NULL) {
        return osThreadError;
    }
    return (t->done != 0) ? osThreadTerminated : osThreadRunning;
}

static void dev_stub_release_gates(void)
{
    g_svc_mqtt_hold = 0;
    g_svc_webhook_hold = 0;
}

osStatus_t osThreadTerminate(osThreadId_t id)
{
    dev_stub_thread_t *t = (dev_stub_thread_t *)id;

    if (t == NULL) {
        return osErrorParameter;
    }
    if (t->in_use == 0 || t->terminate_requested != 0 || t->done != 0) {
        return osErrorResource;
    }
    g_os_terminate_calls++;
    g_os_terminate_last_handle = id;
    dev_stub_release_gates();
    t->terminate_requested = 1;
    return osOK;
}

uint32_t osKernelGetTickCount(void)
{
    return (uint32_t)now_ms();
}

osStatus_t osDelay(uint32_t ticks)
{
    uint64_t end = now_ms() + ticks;

    while (now_ms() < end) {
        if (t_dev_current != NULL &&
            t_dev_current->terminate_requested != 0) {
            t_dev_current->done = 1;
            pthread_exit(NULL);
        }
        sleep_us(2000u);
    }
    return osOK;
}

uint32_t HAL_GetUIDw0(void)
{
    return 0x11111111u;
}

uint32_t HAL_GetUIDw1(void)
{
    return 0x22222222u;
}

uint32_t HAL_GetUIDw2(void)
{
    return 0x33333333u;
}

int factory_mac_get_burned(uint8_t *mac)
{
    mac[0] = 0xaa;
    mac[1] = 0xbb;
    mac[2] = 0xcc;
    mac[3] = 0xdd;
    mac[4] = 0xee;
    mac[5] = 0x66;
    return 0;
}

aicam_result_t nm_get_netif_info(const char *if_name, netif_info_t *netif_info)
{
    (void)if_name;
    (void)netif_info;
    return AICAM_ERROR;
}

aicam_result_t mqtt_service_get_topic_config(mqtt_service_topic_config_t *config)
{
    memset(config->data_report_topic, 0, sizeof(config->data_report_topic));
    if (g_svc_mqtt_configured != 0) {
        snprintf(config->data_report_topic,
                 sizeof(config->data_report_topic), "ne301/reports");
        config->data_report_qos = g_svc_mqtt_qos;
    } else {
        config->data_report_qos = 0;
    }
    return AICAM_OK;
}

aicam_bool_t mqtt_service_is_running(void)
{
    return (g_svc_mqtt_running != 0) ? AICAM_TRUE : AICAM_FALSE;
}

int mqtt_service_publish(const char *topic, const uint8_t *payload,
                         int payload_len, int qos, int retain)
{
    (void)topic;
    (void)payload;
    (void)payload_len;
    (void)qos;
    (void)retain;
    g_svc_mqtt_calls++;
    while (g_svc_mqtt_hold != 0) {
        sleep_us(2000u);
    }
    return g_svc_mqtt_publish_result;
}

aicam_result_t mqtt_service_get_acked_msg_id(int *out_msg_id, uint32_t timeout_ms)
{
    (void)out_msg_id;
    (void)timeout_ms;
    sleep_us(2000u);
    return AICAM_ERROR;
}

aicam_result_t json_config_get_webhook_config(webhook_config_t *config)
{
    memset(config, 0, sizeof(*config));
    if (g_svc_webhook_enabled != 0) {
        config->enable = AICAM_TRUE;
        snprintf(config->url, sizeof(config->url),
                 "https://hook.example/report");
    }
    return AICAM_OK;
}

aicam_result_t webhook_service_push_json(const uint8_t *json, uint32_t json_len)
{
    (void)json;
    (void)json_len;
    g_svc_webhook_calls++;
    while (g_svc_webhook_hold != 0) {
        sleep_us(2000u);
    }
    g_svc_webhook_cleanups++;
    return (aicam_result_t)g_svc_webhook_result;
}

void debug_register_commands(const debug_cmd_reg_t *table, uint32_t count)
{
    (void)table;
    (void)count;
    g_debug_register_calls++;
    g_debug_mutex_new_count_at_register = g_dev_mutex_new_count;
}

static void wait_svc_calls(volatile int *counter, int target, uint32_t timeout_ms)
{
    uint64_t deadline = now_ms() + timeout_ms;

    while (*counter < target && now_ms() < deadline) {
        sleep_us(5000u);
    }
}

static int dev_find_entry(uint32_t seq, app_report_entry_info_t *out)
{
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        if (app_report_service_entry(i, out) == AICAM_TRUE &&
            out->report_seq == seq) {
            return 1;
        }
    }
    return 0;
}

static int dev_wait_entry_state(uint32_t seq, uint32_t mqtt_state,
                                uint32_t webhook_state, uint32_t timeout_ms)
{
    uint64_t deadline = now_ms() + timeout_ms;
    app_report_entry_info_t info;

    while (now_ms() < deadline) {
        if (dev_find_entry(seq, &info) && info.mqtt_state == mqtt_state &&
            info.webhook_state == webhook_state) {
            return 1;
        }
        sleep_us(5000u);
    }
    return (dev_find_entry(seq, &info) && info.mqtt_state == mqtt_state &&
            info.webhook_state == webhook_state) ? 1 : 0;
}

static void *dev_first_entry_start_fn(void *arg)
{
    (void)arg;
    g_first_entry_role = DEV_FIRST_ENTRY_START;
    g_first_start_rc = app_report_service_device_start();
    return NULL;
}

static void *dev_first_entry_stop_fn(void *arg)
{
    (void)arg;
    g_first_entry_role = DEV_FIRST_ENTRY_STOP;
    g_first_stop_rc = app_report_service_device_stop();
    pthread_mutex_lock(&g_first_entry_test_mutex);
    if (g_first_start_thread_create_active != 0) {
        g_first_stop_done_during_start_create = 1;
    }
    g_first_stop_done = 1;
    pthread_cond_broadcast(&g_first_entry_test_cond);
    pthread_mutex_unlock(&g_first_entry_test_mutex);
    return NULL;
}

static void test_device_first_entry_lock_contract(void)
{
    pthread_t th_start;
    pthread_t th_stop;
    int threads_before = g_dev_thread_count;
    int locks_before = g_dev_mutex_new_count;
    int registrations_before = g_debug_register_calls;
    int threads_before_interleave;
    int locks_before_register;
    int start_created;
    int stop_created;
    int start_rc;
    int stop_rc;

    svc_reset();
    CHECK(app_report_service_device_running() == AICAM_FALSE);
    start_rc = app_report_service_device_start();
    stop_rc = app_report_service_device_stop();
    CHECK(start_rc == AICAM_ERROR);
    CHECK(stop_rc == AICAM_ERROR);
    CHECK(g_dev_mutex_new_count == locks_before);
    CHECK(g_dev_thread_count == threads_before);
    if (app_report_service_device_running() == AICAM_TRUE) {
        (void)app_report_service_device_stop();
    }
    for (int i = threads_before; i < g_dev_thread_count; i++) {
        (void)pthread_join(g_dev_threads[i].th, NULL);
    }

    locks_before_register = g_dev_mutex_new_count;
    threads_before_interleave = g_dev_thread_count;
    app_report_service_device_cmd_register();
    CHECK(g_dev_mutex_new_count == locks_before_register + 2);
    CHECK(g_debug_register_calls == registrations_before + 1);
    CHECK(g_debug_mutex_new_count_at_register == locks_before_register + 2);

    pthread_mutex_lock(&g_first_entry_test_mutex);
    g_first_entry_test_active = 1;
    g_first_start_thread_create_entered = 0;
    g_first_start_thread_create_active = 0;
    g_first_stop_acquire_attempted = 0;
    g_first_stop_done = 0;
    g_first_stop_done_during_start_create = 0;
    g_first_start_mutex = NULL;
    g_first_stop_mutex = NULL;
    g_first_start_rc = AICAM_ERROR;
    g_first_stop_rc = AICAM_ERROR;
    pthread_mutex_unlock(&g_first_entry_test_mutex);

    start_created = pthread_create(&th_start, NULL,
                                   dev_first_entry_start_fn, NULL) == 0;
    CHECK(start_created != 0);
    if (start_created != 0) {
        pthread_mutex_lock(&g_first_entry_test_mutex);
        while (g_first_start_thread_create_entered == 0) {
            pthread_cond_wait(&g_first_entry_test_cond,
                              &g_first_entry_test_mutex);
        }
        pthread_mutex_unlock(&g_first_entry_test_mutex);
        stop_created = pthread_create(&th_stop, NULL,
                                     dev_first_entry_stop_fn, NULL) == 0;
        CHECK(stop_created != 0);
        if (stop_created == 0) {
            pthread_mutex_lock(&g_first_entry_test_mutex);
            g_first_stop_mutex = g_first_start_mutex;
            g_first_stop_acquire_attempted = 1;
            pthread_cond_broadcast(&g_first_entry_test_cond);
            pthread_mutex_unlock(&g_first_entry_test_mutex);
        }
        (void)pthread_join(th_start, NULL);
        if (stop_created != 0) {
            (void)pthread_join(th_stop, NULL);
        }
    }
    pthread_mutex_lock(&g_first_entry_test_mutex);
    g_first_entry_test_active = 0;
    pthread_mutex_unlock(&g_first_entry_test_mutex);

    CHECK(g_first_start_rc == AICAM_OK);
    CHECK(g_first_stop_rc == AICAM_OK);
    CHECK(g_first_start_mutex != NULL);
    CHECK(g_first_start_mutex == g_first_stop_mutex);
    CHECK(g_first_stop_done_during_start_create == 0);
    CHECK(g_dev_thread_count == threads_before_interleave + 1);
    CHECK(app_report_service_device_running() == AICAM_FALSE);
    CHECK(g_os_terminate_calls == 1);
    for (int i = threads_before_interleave; i < g_dev_thread_count; i++) {
        (void)pthread_join(g_dev_threads[i].th, NULL);
    }
}

static void test_device_lifecycle_resume(void)
{
    app_report_entry_info_t info;
    app_report_stats_t st;
    uint32_t boot0 = 0;
    uint32_t boot_out = 0;
    uint32_t ms;
    uint32_t ws;
    uint32_t dedup_before;
    char rbuf[512];
    size_t rlen;

    svc_reset();
    g_svc_mqtt_configured = 0;
    CHECK(app_report_service_device_start() == AICAM_OK);
    CHECK(app_report_service_device_running() == AICAM_TRUE);
    boot0 = app_report_service_boot_id();
    CHECK(boot0 != 0u);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);

    rlen = build_report(rbuf, sizeof(rbuf), 200u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 200u,
                            &boot_out) == APP_REPORT_ABI_OK);
    CHECK(boot_out == boot0);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(dev_wait_entry_state(200u, APP_REPORT_MQTT_NOT_CONFIGURED,
                               APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED,
                               2000u) == 1);
    CHECK(dev_find_entry(200u, &info));
    CHECK(info.mqtt_state == APP_REPORT_MQTT_NOT_CONFIGURED);
    CHECK(info.webhook_state == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);

    CHECK(app_report_service_device_stop() == AICAM_OK);
    CHECK(app_report_service_device_running() == AICAM_FALSE);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(boot0, 200u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(ws == APP_REPORT_WEBHOOK_UNKNOWN);

    CHECK(app_report_service_device_start() == AICAM_OK);
    CHECK(app_report_service_boot_id() == boot0);
    CHECK(app_report_status(boot0, 200u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(ws == APP_REPORT_WEBHOOK_UNKNOWN);

    app_report_service_get_stats(&st);
    dedup_before = st.dedup_hits;
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 200u,
                            &boot_out) == APP_REPORT_ABI_OK);
    CHECK(boot_out == boot0);
    CHECK(app_report_service_queue_count() == 0u);
    app_report_service_get_stats(&st);
    CHECK(st.dedup_hits == dedup_before + 1u);
    CHECK(g_svc_webhook_calls == 1);
    CHECK(g_svc_mqtt_calls == 0);

    rlen = build_report(rbuf, sizeof(rbuf), 201u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 201u,
                            &boot_out) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(dev_wait_entry_state(201u, APP_REPORT_MQTT_NOT_CONFIGURED,
                               APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED,
                               2000u) == 1);
    CHECK(app_report_status(boot0, 201u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);

    CHECK(app_report_service_device_stop() == AICAM_OK);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(boot0, 201u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(ws == APP_REPORT_WEBHOOK_UNKNOWN);
    CHECK(app_report_service_device_stop() == AICAM_OK);
    CHECK(g_svc_webhook_calls == 2);
    CHECK(g_os_terminate_calls == 2);
}

static void *dev_stop_thread_fn(void *arg)
{
    *(aicam_result_t *)arg = app_report_service_device_stop();
    return NULL;
}

static volatile int g_stop_flag_done;
static aicam_result_t g_stop_flag_rc;
static uint64_t g_stop_flag_at;
static volatile int g_start_flag_done;
static aicam_result_t g_start_flag_rc;
static uint64_t g_start_flag_at;

static void *dev_stop_flagged_fn(void *arg)
{
    (void)arg;
    g_stop_flag_rc = app_report_service_device_stop();
    g_stop_flag_at = now_ms();
    g_stop_flag_done = 1;
    return NULL;
}

static void *dev_start_flagged_fn(void *arg)
{
    (void)arg;
    g_start_flag_rc = app_report_service_device_start();
    g_start_flag_at = now_ms();
    g_start_flag_done = 1;
    return NULL;
}

static void *dev_release_mqtt_hold_after_fn(void *arg)
{
    sleep_us((uint32_t)(uintptr_t)arg);
    g_svc_mqtt_hold = 0;
    return NULL;
}static void test_device_stop_inflight(void)
{
    pthread_t th;
    aicam_result_t stop_rc = AICAM_ERROR;
    uint32_t boot0 = 0;
    uint32_t boot_out = 0;
    uint32_t ms;
    uint32_t ws;
    uint64_t t0;
    char rbuf[512];
    size_t rlen;

    svc_reset();
    g_svc_mqtt_qos = 1;
    g_svc_webhook_enabled = 0;
    g_svc_mqtt_hold = 1;
    CHECK(app_report_service_device_start() == AICAM_OK);
    boot0 = app_report_service_boot_id();
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);

    rlen = build_report(rbuf, sizeof(rbuf), 210u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 210u,
                            &boot_out) == APP_REPORT_ABI_OK);
    wait_svc_calls(&g_svc_mqtt_calls, 1, 2000u);
    CHECK(g_svc_mqtt_hold != 0);

    t0 = now_ms();
    CHECK(pthread_create(&th, NULL, dev_stop_thread_fn, &stop_rc) == 0);
    sleep_us(120000u);
    g_svc_mqtt_hold = 0;
    (void)pthread_join(th, NULL);
    CHECK(stop_rc == AICAM_OK);
    CHECK(now_ms() - t0 >= 100ull);
    CHECK(app_report_service_device_running() == AICAM_FALSE);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(boot0, 210u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(ws == APP_REPORT_WEBHOOK_UNKNOWN);

    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 210u,
                            &boot_out) == APP_REPORT_ABI_OK);
    CHECK(boot_out == boot0);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(g_svc_mqtt_calls == 1);
    CHECK(g_os_terminate_calls == 1);
}

static void test_device_stop_fail_closed(void)
{
    app_report_entry_info_t info;
    uint32_t boot0 = 0;
    uint32_t boot_out = 0;
    uint32_t ms;
    uint32_t ws;
    char rbuf[512];
    size_t rlen;

    svc_reset();
    g_svc_mqtt_qos = 1;
    g_svc_webhook_enabled = 0;
    g_svc_mqtt_hold = 1;
    CHECK(app_report_service_device_start() == AICAM_OK);
    boot0 = app_report_service_boot_id();
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);

    rlen = build_report(rbuf, sizeof(rbuf), 220u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 220u,
                            &boot_out) == APP_REPORT_ABI_OK);
    wait_svc_calls(&g_svc_mqtt_calls, 1, 2000u);

    CHECK(app_report_service_device_stop() == AICAM_ERROR);
    CHECK(app_report_service_device_running() == AICAM_TRUE);
    CHECK(g_os_terminate_calls == 0);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(dev_find_entry(220u, &info));
    CHECK(info.mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(app_report_status(boot0, 220u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_PENDING);

    CHECK(app_report_service_device_stop() == AICAM_ERROR);
    CHECK(app_report_service_device_running() == AICAM_TRUE);
    CHECK(g_os_terminate_calls == 0);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(dev_find_entry(220u, &info));
    CHECK(info.mqtt_state == APP_REPORT_MQTT_PENDING);

    CHECK(app_report_service_device_start() == AICAM_ERROR_BUSY);
    CHECK(g_os_terminate_calls == 0);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(dev_find_entry(220u, &info));
    CHECK(info.mqtt_state == APP_REPORT_MQTT_PENDING);

    g_svc_mqtt_hold = 0;
    CHECK(dev_wait_entry_state(220u, APP_REPORT_MQTT_UNKNOWN,
                               APP_REPORT_WEBHOOK_NOT_CONFIGURED,
                               3000u) == 1);
    CHECK(app_report_service_device_stop() == AICAM_OK);
    CHECK(app_report_service_device_running() == AICAM_FALSE);
    CHECK(g_os_terminate_calls == 1);
    CHECK(app_report_service_queue_count() == 0u);

    CHECK(app_report_service_device_start() == AICAM_OK);
    CHECK(app_report_service_boot_id() == boot0);
    CHECK(app_report_status(boot0, 220u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 220u,
                            &boot_out) == APP_REPORT_ABI_OK);
    CHECK(boot_out == boot0);
    CHECK(app_report_service_queue_count() == 0u);

    CHECK(app_report_service_device_stop() == AICAM_OK);
    CHECK(app_report_service_device_running() == AICAM_FALSE);
    CHECK(app_report_status(boot0, 220u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(ws == APP_REPORT_WEBHOOK_UNKNOWN);
    CHECK(g_os_terminate_calls == 2);
}

static void test_device_stop_static_stack_reuse_race(void)
{
    app_report_entry_info_t info;
    uint32_t boot0 = 0;
    uint32_t boot_out = 0;
    uint32_t ms;
    uint32_t ws;
    int threads_before;
    char rbuf[512];
    size_t rlen;

    svc_reset();
    g_svc_mqtt_configured = 0;
    g_svc_webhook_hold = 1;
    CHECK(app_report_service_device_start() == AICAM_OK);
    boot0 = app_report_service_boot_id();
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    rlen = build_report(rbuf, sizeof(rbuf), 240u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 240u,
                            &boot_out) == APP_REPORT_ABI_OK);
    wait_svc_calls(&g_svc_webhook_calls, 1, 2000u);
    threads_before = g_dev_thread_count;

    CHECK(app_report_service_device_stop() == AICAM_ERROR);
    CHECK(app_report_service_device_running() == AICAM_TRUE);
    CHECK(g_os_terminate_calls == 0);
    CHECK(g_dev_thread_count == threads_before);
    CHECK(app_report_service_device_start() == AICAM_ERROR_BUSY);
    CHECK(g_os_terminate_calls == 0);
    CHECK(g_dev_thread_count == threads_before);
    CHECK(dev_find_entry(240u, &info));
    CHECK(info.mqtt_state == APP_REPORT_MQTT_NOT_CONFIGURED);

    g_svc_webhook_hold = 0;
    CHECK(dev_wait_entry_state(240u, APP_REPORT_MQTT_NOT_CONFIGURED,
                               APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED,
                               3000u) == 1);
    CHECK(app_report_service_device_start() == AICAM_OK);
    CHECK(app_report_service_device_running() == AICAM_TRUE);
    CHECK(g_os_terminate_calls == 1);
    CHECK(g_dev_thread_count == threads_before);
    CHECK(app_report_service_boot_id() == boot0);
    CHECK(app_report_service_device_stop() == AICAM_OK);
    CHECK(app_report_service_device_running() == AICAM_FALSE);
    CHECK(g_os_terminate_calls == 2);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(boot0, 240u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(ws == APP_REPORT_WEBHOOK_UNKNOWN);
    {
        uint64_t deadline = now_ms() + 1000ull;

        while (osThreadGetState(g_os_terminate_last_handle) !=
                   osThreadTerminated &&
               now_ms() < deadline) {
            sleep_us(2000u);
        }
        CHECK(osThreadGetState(g_os_terminate_last_handle) ==
              osThreadTerminated);
    }
}

static void test_device_stop_webhook_inflight(void)
{
    pthread_t th;
    uint32_t boot0 = 0;
    uint32_t boot_out = 0;
    uint32_t ms;
    uint32_t ws;
    uint64_t t0;
    uint64_t deadline;
    char rbuf[512];
    size_t rlen;

    svc_reset();
    g_svc_mqtt_configured = 0;
    g_svc_webhook_hold = 1;
    CHECK(app_report_service_device_start() == AICAM_OK);
    boot0 = app_report_service_boot_id();
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);

    rlen = build_report(rbuf, sizeof(rbuf), 230u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 230u,
                            &boot_out) == APP_REPORT_ABI_OK);
    wait_svc_calls(&g_svc_webhook_calls, 1, 2000u);
    CHECK(g_svc_webhook_hold != 0);
    CHECK(g_svc_webhook_cleanups == 0);

    t0 = now_ms();
    g_stop_flag_done = 0;
    g_stop_flag_rc = AICAM_ERROR;
    g_stop_flag_at = 0;
    CHECK(pthread_create(&th, NULL, dev_stop_flagged_fn, NULL) == 0);
    sleep_us(150000u);
    CHECK(g_stop_flag_done == 0);
    CHECK(g_svc_webhook_cleanups == 0);
    CHECK(g_os_terminate_calls == 0);

    g_svc_webhook_hold = 0;
    deadline = now_ms() + 3000ull;
    while (g_stop_flag_done == 0 && now_ms() < deadline) {
        sleep_us(5000u);
    }
    CHECK(g_stop_flag_done == 1);
    CHECK(g_stop_flag_rc == AICAM_OK);
    CHECK(now_ms() - t0 >= 100ull);
    CHECK(g_svc_webhook_cleanups == 1);
    CHECK(g_os_terminate_calls == 1);
    CHECK(app_report_service_device_running() == AICAM_FALSE);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(boot0, 230u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(ws == APP_REPORT_WEBHOOK_UNKNOWN);
    (void)pthread_join(th, NULL);
}

static void test_device_stop_start_interleave(void)
{
    pthread_t th_stop;
    pthread_t th_start;
    pthread_t th_rel;
    app_report_entry_info_t info;
    int threads_before;
    uint32_t boot0 = 0;
    uint32_t boot_out = 0;
    uint32_t ms;
    uint32_t ws;
    char rbuf[512];
    size_t rlen;

    svc_reset();
    g_svc_mqtt_qos = 0;
    g_svc_webhook_enabled = 0;
    g_svc_mqtt_hold = 1;
    CHECK(app_report_service_device_start() == AICAM_OK);
    boot0 = app_report_service_boot_id();
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    rlen = build_report(rbuf, sizeof(rbuf), 250u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 250u,
                            &boot_out) == APP_REPORT_ABI_OK);
    wait_svc_calls(&g_svc_mqtt_calls, 1, 2000u);

    g_stop_flag_done = 0;
    g_stop_flag_rc = AICAM_ERROR;
    g_stop_flag_at = 0;
    CHECK(pthread_create(&th_stop, NULL, dev_stop_flagged_fn, NULL) == 0);
    sleep_us(80000u);
    g_start_flag_done = 0;
    g_start_flag_rc = AICAM_ERROR;
    g_start_flag_at = 0;
    CHECK(pthread_create(&th_start, NULL, dev_start_flagged_fn, NULL) == 0);
    sleep_us(40000u);
    CHECK(g_start_flag_done == 0);
    CHECK(g_stop_flag_done == 0);
    CHECK(pthread_create(&th_rel, NULL, dev_release_mqtt_hold_after_fn,
                         (void *)(uintptr_t)60000u) == 0);
    (void)pthread_join(th_stop, NULL);
    (void)pthread_join(th_start, NULL);
    (void)pthread_join(th_rel, NULL);
    CHECK(g_stop_flag_rc == AICAM_OK);
    CHECK(g_start_flag_rc == AICAM_OK);
    CHECK(g_start_flag_at >= g_stop_flag_at);
    CHECK(g_os_terminate_calls == 1);
    CHECK(app_report_service_device_running() == AICAM_TRUE);
    CHECK(app_report_status(boot0, 250u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 250u,
                            &boot_out) == APP_REPORT_ABI_OK);
    CHECK(boot_out == boot0);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(g_svc_mqtt_calls == 1);
    CHECK(app_report_service_device_stop() == AICAM_OK);

    svc_reset();
    g_svc_mqtt_qos = 1;
    g_svc_webhook_enabled = 0;
    g_svc_mqtt_hold = 1;
    CHECK(app_report_service_device_start() == AICAM_OK);
    boot0 = app_report_service_boot_id();
    rlen = build_report(rbuf, sizeof(rbuf), 260u, PLACEHOLDER, "0", "d", 1);
    CHECK(app_report_submit(&g_session, rbuf, (uint32_t)rlen, 260u,
                            &boot_out) == APP_REPORT_ABI_OK);
    wait_svc_calls(&g_svc_mqtt_calls, 1, 2000u);
    threads_before = g_dev_thread_count;

    g_stop_flag_done = 0;
    g_stop_flag_rc = AICAM_ERROR;
    g_stop_flag_at = 0;
    CHECK(pthread_create(&th_stop, NULL, dev_stop_flagged_fn, NULL) == 0);
    sleep_us(80000u);
    g_start_flag_done = 0;
    g_start_flag_rc = AICAM_OK;
    g_start_flag_at = 0;
    CHECK(pthread_create(&th_start, NULL, dev_start_flagged_fn, NULL) == 0);
    sleep_us(40000u);
    CHECK(g_start_flag_done == 0);
    CHECK(pthread_create(&th_rel, NULL, dev_release_mqtt_hold_after_fn,
                         (void *)(uintptr_t)500000u) == 0);
    (void)pthread_join(th_stop, NULL);
    CHECK(g_stop_flag_rc == AICAM_ERROR);
    (void)pthread_join(th_start, NULL);
    CHECK(g_start_flag_rc == AICAM_ERROR_BUSY);
    CHECK(g_dev_thread_count == threads_before);
    CHECK(g_os_terminate_calls == 0);
    CHECK(app_report_service_device_running() == AICAM_TRUE);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(dev_find_entry(260u, &info));
    CHECK(info.mqtt_state == APP_REPORT_MQTT_PENDING);

    (void)pthread_join(th_rel, NULL);
    CHECK(app_report_service_device_stop() == AICAM_OK);
    CHECK(app_report_service_device_running() == AICAM_FALSE);
    CHECK(g_os_terminate_calls == 1);
}

static void test_device_stop_start_cycle_no_leak(void)
{
    int threads_before;
    int cycles = 24;

    svc_reset();
    g_svc_mqtt_configured = 0;
    g_svc_webhook_enabled = 0;
    threads_before = g_dev_thread_count;
    for (int i = 0; i < cycles; i++) {
        CHECK(app_report_service_device_start() == AICAM_OK);
        CHECK(app_report_service_device_running() == AICAM_TRUE);
        CHECK(app_report_service_device_stop() == AICAM_OK);
        CHECK(app_report_service_device_running() == AICAM_FALSE);
    }
    CHECK(g_dev_thread_count == threads_before);
    CHECK(g_os_terminate_calls == cycles);
    CHECK(app_report_service_device_start() == AICAM_OK);
    CHECK(app_report_service_device_stop() == AICAM_OK);
    CHECK(g_os_terminate_calls == cycles + 1);
}

int main(void)
{
    test_session_gates();
    test_param_rejections();
    test_quota();
    test_json_validity();
    test_schema_rejections();
    test_normalization_gaps_dedup();
    test_insertion_when_identity_absent();
    test_forged_device_id();
    test_identity_fail_closed();
    test_channel_independence();
    test_channel_disabled();
    test_queue_bounds();
    test_restart_and_key_change();
    test_input_budget_boundary();
    test_stats_and_misc();
    test_mqtt_outage_recovery();
    test_webhook_temporary_then_recover();
    test_queue_full_outage_recovery();
    test_timing_isolation();
    test_stop_restart_unknown_dedup();
    test_device_first_entry_lock_contract();
    test_device_lifecycle_resume();
    test_device_stop_inflight();
    test_device_stop_fail_closed();
    test_device_stop_static_stack_reuse_race();
    test_device_stop_webhook_inflight();
    test_device_stop_start_interleave();
    test_device_stop_start_cycle_no_leak();
    printf("app_report: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
