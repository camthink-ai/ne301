#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_installer.h"
#include "app_report_policy.h"
#include "app_report_service.h"

static int g_total;
static int g_fail;
static uint8_t g_blob[APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY];
static uint32_t g_blob_len;
static int g_fail_write;
static int g_partial_write;
static int g_read_error;
static uint32_t g_mqtt_calls;
static uint32_t g_webhook_calls;
static int g_mqtt_result;
static int g_webhook_result;
static int g_commit_on_mqtt_ready;
static uint32_t g_policy_revision;
static app_report_policy_status_t g_injected_status;
static pthread_mutex_t g_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_gate_cond = PTHREAD_COND_INITIALIZER;
static int g_prestart_enabled;
static int g_prestart_started;
static int g_prestart_release;
static int g_poststart_enabled;
static int g_poststart_started;
static int g_poststart_release;
static int g_inprimitive_enabled;
static int g_inprimitive_started;
static int g_inprimitive_release;
static int g_webhook_inprimitive_enabled;
static int g_webhook_inprimitive_started;
static int g_webhook_inprimitive_release;

#define CHECK(cond) \
    do { \
        g_total++; \
        if (!(cond)) { \
            g_fail++; \
            printf("FAIL app_report_policy:%d:%d %s\n", g_total, __LINE__, #cond); \
        } \
    } while (0)

static int32_t fake_read(void *user, void *out, uint32_t cap, uint32_t *out_len)
{
    (void)user;
    if (g_read_error != 0) {
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    if (out == NULL || out_len == NULL || cap < g_blob_len) {
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    if (g_blob_len == 0u) {
        *out_len = 0u;
        return APP_REPORT_POLICY_STORAGE_NOT_FOUND;
    }
    memcpy(out, g_blob, g_blob_len);
    *out_len = g_blob_len;
    return APP_REPORT_POLICY_STORAGE_OK;
}

static int32_t fake_write(void *user, const void *data, uint32_t len)
{
    (void)user;
    if (data == NULL || len > sizeof(g_blob) || g_fail_write != 0) {
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    if (g_partial_write != 0) {
        uint32_t partial = len / 2u;
        if (partial == 0u) {
            partial = 1u;
        }
        memcpy(g_blob, data, partial);
        g_blob_len = partial;
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    memcpy(g_blob, data, len);
    g_blob_len = len;
    return APP_REPORT_POLICY_STORAGE_OK;
}

static app_report_policy_storage_t g_storage = {
    NULL,
    fake_read,
    fake_write
};

static uint8_t g_install_blob[APP_INSTALL_RECORD_BIN_SIZE];
static int g_install_present;
static int g_install_read_error;

static app_install_io_result_t install_kv_read(void *ctx, const char *key,
                                               uint8_t *buf, size_t cap,
                                               size_t *len)
{
    (void)ctx;
    if (strcmp(key, APP_INSTALL_KEY_RECORD) != 0) {
        return APP_INSTALL_IO_NOT_FOUND;
    }
    if (g_install_read_error != 0) {
        return APP_INSTALL_IO_ERROR;
    }
    if (g_install_present == 0) {
        return APP_INSTALL_IO_NOT_FOUND;
    }
    if (buf == NULL || len == NULL || cap < sizeof(g_install_blob)) {
        return APP_INSTALL_IO_ERROR;
    }
    memcpy(buf, g_install_blob, sizeof(g_install_blob));
    *len = sizeof(g_install_blob);
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t install_kv_write(void *ctx, const char *key,
                                                const uint8_t *data, size_t len)
{
    (void)ctx;
    (void)key;
    (void)data;
    (void)len;
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t install_kv_delete(void *ctx, const char *key)
{
    (void)ctx;
    (void)key;
    return APP_INSTALL_IO_OK;
}

static aicam_bool_t install_tx_begin(void *ctx)
{
    (void)ctx;
    return AICAM_TRUE;
}

static void install_tx_end(void *ctx)
{
    (void)ctx;
}

static const app_install_io_t g_install_io = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    install_kv_write,
    install_kv_read,
    install_kv_delete,
    NULL,
    install_tx_begin,
    install_tx_end,
    NULL,
    NULL,
    NULL
};

static void set_committed_identity(const app_report_policy_identity_t *identity)
{
    app_install_record_t record;
    char path[APP_INSTALL_PATH_MAX];
    size_t pos;
    uint32_t i;

    memset(&record, 0, sizeof(record));
    memcpy(record.publisher_id, identity->publisher_id,
           sizeof(record.publisher_id) - 1u);
    memcpy(record.app_id, identity->app_id, sizeof(record.app_id) - 1u);
    memcpy(record.publisher_key_sha256, identity->publisher_key_sha256,
           APP_INSTALL_SHA256_LEN);
    memcpy(record.tbs_sha256, identity->native_tbs_sha256,
           APP_INSTALL_SHA256_LEN);
    memcpy(record.package_sha256, identity->native_tbs_sha256,
           APP_INSTALL_SHA256_LEN);
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
    CHECK(app_install_record_encode(&record, g_install_blob,
                                    sizeof(g_install_blob)) == AICAM_TRUE);
    g_install_present = 1;
    app_install_boot_resolve_reset();
}

static app_report_identity_t g_runtime_identity;
static app_report_transport_t g_transport;
static app_report_session_t g_session;
static app_report_policy_identity_t g_policy_identity;

static aicam_bool_t report_device_id(void *user, char *out, uint32_t cap)
{
    static const char value[] = "aa:bb:cc:dd:ee:66";
    (void)user;
    if (out == NULL || cap < sizeof(value)) {
        return AICAM_FALSE;
    }
    memcpy(out, value, sizeof(value));
    return AICAM_TRUE;
}

static uint32_t report_boot_id(void *user)
{
    (void)user;
    return 0x5A5A0001u;
}

static uint32_t mqtt_ready(void *user)
{
    app_report_policy_values_t values;
    (void)user;
    if (g_commit_on_mqtt_ready != 0) {
        g_commit_on_mqtt_ready = 0;
        values.mqtt_enabled = AICAM_FALSE;
        values.webhook_enabled = AICAM_TRUE;
        values.backlog_capacity = APP_REPORT_POLICY_DEFAULT_CAPACITY;
        CHECK(app_report_service_policy_commit(&g_session, &g_policy_identity,
                                              g_policy_revision, &values,
                                              &g_injected_status) ==
              APP_REPORT_ABI_OK);
        g_policy_revision = g_injected_status.revision;
    }
    return APP_REPORT_CHANNEL_READY;
}

static uint32_t webhook_ready(void *user)
{
    (void)user;
    return APP_REPORT_CHANNEL_READY;
}

static int mqtt_publish(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    (void)json;
    (void)len;
    if (app_report_service_transport_entered(APP_REPORT_CHANNEL_MQTT) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    pthread_mutex_lock(&g_gate_mutex);
    if (g_prestart_enabled != 0) {
        g_prestart_started = 1;
        pthread_cond_broadcast(&g_gate_cond);
        while (g_prestart_release == 0) {
            pthread_cond_wait(&g_gate_cond, &g_gate_mutex);
        }
    }
    pthread_mutex_unlock(&g_gate_mutex);
    if (app_report_service_transport_start(APP_REPORT_CHANNEL_MQTT) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    pthread_mutex_lock(&g_gate_mutex);
    if (g_poststart_enabled != 0) {
        g_poststart_started = 1;
        pthread_cond_broadcast(&g_gate_cond);
        while (g_poststart_release == 0) {
            pthread_cond_wait(&g_gate_cond, &g_gate_mutex);
        }
    }
    pthread_mutex_unlock(&g_gate_mutex);
    if (app_report_service_transport_begin(APP_REPORT_CHANNEL_MQTT) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    pthread_mutex_lock(&g_gate_mutex);
    if (g_inprimitive_enabled != 0) {
        g_inprimitive_started = 1;
        pthread_cond_broadcast(&g_gate_cond);
        while (g_inprimitive_release == 0) {
            pthread_cond_wait(&g_gate_cond, &g_gate_mutex);
        }
    }
    pthread_mutex_unlock(&g_gate_mutex);
    g_mqtt_calls++;
    return g_mqtt_result;
}

static int webhook_post(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    (void)json;
    (void)len;
    if (app_report_service_transport_entered(APP_REPORT_CHANNEL_WEBHOOK) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (app_report_service_transport_start(APP_REPORT_CHANNEL_WEBHOOK) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (app_report_service_transport_begin(APP_REPORT_CHANNEL_WEBHOOK) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    pthread_mutex_lock(&g_gate_mutex);
    if (g_webhook_inprimitive_enabled != 0) {
        g_webhook_inprimitive_started = 1;
        pthread_cond_broadcast(&g_gate_cond);
        while (g_webhook_inprimitive_release == 0) {
            pthread_cond_wait(&g_gate_cond, &g_gate_mutex);
        }
    }
    pthread_mutex_unlock(&g_gate_mutex);
    g_webhook_calls++;
    return g_webhook_result;
}

static void reset_service(void)
{
    memset(g_blob, 0, sizeof(g_blob));
    g_blob_len = 0u;
    g_fail_write = 0;
    g_partial_write = 0;
    g_read_error = 0;
    g_mqtt_calls = 0u;
    g_webhook_calls = 0u;
    g_mqtt_result = APP_REPORT_TRANSPORT_OK;
    g_webhook_result = APP_REPORT_TRANSPORT_OK;
    g_commit_on_mqtt_ready = 0;
    g_policy_revision = 0u;
    memset(&g_session, 0, sizeof(g_session));
    g_session.active = AICAM_TRUE;
    memcpy(g_session.publisher_id, "publisher-a", sizeof("publisher-a"));
    memcpy(g_session.app_id, "app-a", sizeof("app-a"));
    memset(g_session.publisher_key_sha256, 0x51,
           sizeof(g_session.publisher_key_sha256));
    g_session.report_max = APP_REPORT_INPUT_MAX;
    memset(&g_policy_identity, 0, sizeof(g_policy_identity));
    memcpy(g_policy_identity.publisher_id, g_session.publisher_id,
           sizeof(g_policy_identity.publisher_id));
    memcpy(g_policy_identity.app_id, g_session.app_id,
           sizeof(g_policy_identity.app_id));
    memcpy(g_policy_identity.publisher_key_sha256,
           g_session.publisher_key_sha256,
           sizeof(g_policy_identity.publisher_key_sha256));
    memset(g_policy_identity.native_tbs_sha256, 0xA7,
           sizeof(g_policy_identity.native_tbs_sha256));
    memset(&g_runtime_identity, 0, sizeof(g_runtime_identity));
    g_runtime_identity.device_id = report_device_id;
    g_runtime_identity.boot_id = report_boot_id;
    memset(&g_transport, 0, sizeof(g_transport));
    g_transport.mqtt_ready = mqtt_ready;
    g_transport.mqtt_publish = mqtt_publish;
    g_transport.webhook_ready = webhook_ready;
    g_transport.webhook_post = webhook_post;
    pthread_mutex_lock(&g_gate_mutex);
    g_prestart_enabled = 0;
    g_prestart_started = 0;
    g_prestart_release = 0;
    g_poststart_enabled = 0;
    g_poststart_started = 0;
    g_poststart_release = 0;
    g_inprimitive_enabled = 0;
    g_inprimitive_started = 0;
    g_inprimitive_release = 0;
    g_webhook_inprimitive_enabled = 0;
    g_webhook_inprimitive_started = 0;
    g_webhook_inprimitive_release = 0;
    pthread_mutex_unlock(&g_gate_mutex);
    app_report_service_set_policy_storage(&g_storage);
    app_report_service_set_committed_io(&g_install_io);
    app_report_service_init(&g_runtime_identity, &g_transport);
    g_install_read_error = 0;
    set_committed_identity(&g_policy_identity);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
}

static size_t make_report(char *out, size_t cap, uint32_t seq)
{
    return (size_t)snprintf(
        out, cap,
        "{\"schema_version\":1,\"type\":\"line_counting\","
        "\"app\":{\"app_id\":\"app-a\",\"sw_version\":\"1\"},"
        "\"report_seq\":%u,\"window\":{},\"total\":{},"
        "\"counter\":{},\"target\":{},\"model\":{}}",
        (unsigned int)seq);
}

static int32_t submit_report(uint32_t seq, uint32_t *out_boot)
{
    char json[512];
    size_t len = make_report(json, sizeof(json), seq);
    return app_report_submit(&g_session, json, (uint32_t)len, seq, out_boot);
}

static int32_t submit_report_for(const app_report_session_t *session,
                                 uint32_t seq, uint32_t *out_boot)
{
    char json[512];
    size_t len = make_report(json, sizeof(json), seq);
    return app_report_submit(session, json, (uint32_t)len, seq, out_boot);
}

static int32_t commit_policy(uint32_t expected, aicam_bool_t mqtt,
                             aicam_bool_t webhook, uint32_t capacity,
                             app_report_policy_status_t *status)
{
    app_report_policy_values_t values;
    values.mqtt_enabled = mqtt;
    values.webhook_enabled = webhook;
    values.backlog_capacity = capacity;
    return app_report_service_policy_commit(&g_session, &g_policy_identity,
                                            expected, &values, status);
}

static void test_policy_revision_and_restart(void)
{
    app_report_policy_status_t status;
    app_report_policy_values_t values;
    app_report_policy_identity_t wrong_identity;
    app_report_session_t wrong_session;
    uint32_t boot = 0u;

    reset_service();
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 0u);
    CHECK(status.mqtt_enabled == AICAM_TRUE);
    CHECK(status.webhook_enabled == AICAM_TRUE);
    CHECK(status.backlog_capacity == APP_REPORT_POLICY_DEFAULT_CAPACITY);
    CHECK(status.capacity_min == APP_REPORT_POLICY_MIN_CAPACITY);
    CHECK(status.capacity_max == APP_REPORT_POLICY_MAX_CAPACITY);
    wrong_session = g_session;
    wrong_session.publisher_key_sha256[0] ^= 1u;
    CHECK(app_report_service_policy_query(&wrong_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_UNAUTHORIZED);
    wrong_identity = g_policy_identity;
    wrong_identity.native_tbs_sha256[0] ^= 1u;
    CHECK(app_report_service_policy_query(&g_session, &wrong_identity,
                                          &status) == APP_REPORT_ABI_UNAUTHORIZED);
    memset(wrong_identity.native_tbs_sha256, 0,
           sizeof(wrong_identity.native_tbs_sha256));
    CHECK(app_report_service_policy_purge(&wrong_identity, 1u, &status) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    wrong_identity = g_policy_identity;
    memset(wrong_identity.app_id, 'x', sizeof(wrong_identity.app_id));
    CHECK(app_report_service_policy_query(&g_session, &wrong_identity,
                                          &status) == APP_REPORT_ABI_INVALID_ARGUMENT);
    values.mqtt_enabled = AICAM_FALSE;
    values.webhook_enabled = AICAM_TRUE;
    values.backlog_capacity = 2u;
    CHECK(app_report_service_policy_commit(&g_session, &g_policy_identity, 0u,
                                           &values, &status) == APP_REPORT_ABI_OK);
    g_policy_revision = status.revision;
    CHECK(status.revision == 1u);
    CHECK(status.mqtt_enabled == AICAM_FALSE);
    CHECK(status.webhook_enabled == AICAM_TRUE);
    CHECK(status.backlog_capacity == 2u);
    CHECK(submit_report(2u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    values.webhook_enabled = AICAM_FALSE;
    CHECK(app_report_service_policy_commit(&g_session, &g_policy_identity, 0u,
                                           &values, &status) == APP_REPORT_ABI_CONFLICT);
    CHECK(status.revision == 1u);
    values.backlog_capacity = APP_REPORT_POLICY_MAX_CAPACITY + 1u;
    CHECK(app_report_service_policy_commit(&g_session, &g_policy_identity, 1u,
                                           &values, &status) ==
          APP_REPORT_ABI_QUOTA_EXCEEDED);
    app_report_service_init(&g_runtime_identity, &g_transport);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 1u);
    CHECK(status.mqtt_enabled == AICAM_FALSE);
    CHECK(status.webhook_enabled == AICAM_TRUE);
    CHECK(status.backlog_capacity == 2u);
}

static void test_submit_adopts_legacy_policy_base_after_upgrade(void)
{
    app_report_policy_status_t status;
    app_report_policy_identity_t upgraded;
    app_install_record_t upgraded_record;
    app_report_session_t stranger;
    uint32_t boot = 0;

    reset_service();
    CHECK(commit_policy(0u, AICAM_TRUE, AICAM_FALSE, 2u, &status) ==
          APP_REPORT_ABI_OK);
    upgraded = g_policy_identity;
    upgraded.native_tbs_sha256[0] ^= 0x3Cu;
    set_committed_identity(&upgraded);
    memset(&upgraded_record, 0, sizeof(upgraded_record));
    memcpy(upgraded_record.publisher_id, upgraded.publisher_id,
           sizeof(upgraded_record.publisher_id));
    memcpy(upgraded_record.app_id, upgraded.app_id,
           sizeof(upgraded_record.app_id));
    memcpy(upgraded_record.publisher_key_sha256, upgraded.publisher_key_sha256,
           APP_REPORT_SHA256_LEN);
    memcpy(upgraded_record.tbs_sha256, upgraded.native_tbs_sha256,
           APP_REPORT_SHA256_LEN);
    CHECK(app_report_service_policy_rebase(&upgraded_record) ==
          APP_REPORT_ABI_OK);
    app_report_service_restart();
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(submit_report(5u, &boot) == APP_REPORT_ABI_OK);
    memset(&stranger, 0, sizeof(stranger));
    stranger.active = AICAM_TRUE;
    memcpy(stranger.publisher_id, "publisher-a", sizeof("publisher-a"));
    memcpy(stranger.app_id, "app-a", sizeof("app-a"));
    memset(stranger.publisher_key_sha256, 0x77,
           sizeof(stranger.publisher_key_sha256));
    stranger.report_max = APP_REPORT_INPUT_MAX;
    CHECK(submit_report_for(&stranger, 6u, &boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);
}

static void test_current_tbs_rebind(void)
{
    app_report_policy_status_t status;
    app_report_policy_values_t values;
    app_report_policy_identity_t updated_identity;
    app_report_policy_values_t old_values;
    uint32_t old_revision = 0u;

    reset_service();
    CHECK(commit_policy(0u, AICAM_FALSE, AICAM_TRUE, 2u, &status) ==
          APP_REPORT_ABI_OK);
    updated_identity = g_policy_identity;
    updated_identity.native_tbs_sha256[0] ^= 0x3Cu;
    set_committed_identity(&updated_identity);
    app_report_service_init(&g_runtime_identity, &g_transport);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_query(&g_session, &updated_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 0u);
    CHECK(status.mqtt_enabled == AICAM_TRUE);
    CHECK(status.webhook_enabled == AICAM_TRUE);
    values.mqtt_enabled = AICAM_TRUE;
    values.webhook_enabled = AICAM_FALSE;
    values.backlog_capacity = 2u;
    CHECK(app_report_service_policy_commit(&g_session, &updated_identity, 0u,
                                           &values, &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 1u);
    CHECK(app_report_policy_store_get(&g_policy_identity, &old_values,
                                      &old_revision) == APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_policy_query(&g_session, &updated_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.mqtt_enabled == AICAM_TRUE);
    CHECK(status.webhook_enabled == AICAM_FALSE);
}

static void test_old_tbs_purge_preserves_current_install(void)
{
    app_report_policy_status_t status;
    app_report_policy_values_t values;
    app_report_policy_identity_t old_identity;
    app_report_policy_identity_t current_identity;
    uint32_t boot = 0u;

    reset_service();
    CHECK(commit_policy(0u, AICAM_TRUE, AICAM_TRUE, 2u, &status) ==
          APP_REPORT_ABI_OK);
    old_identity = g_policy_identity;
    current_identity = old_identity;
    current_identity.native_tbs_sha256[0] ^= 0x6Du;
    set_committed_identity(&current_identity);
    app_report_service_init(&g_runtime_identity, &g_transport);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_query(&g_session, &current_identity,
                                         &status) == APP_REPORT_ABI_OK);
    values.mqtt_enabled = AICAM_FALSE;
    values.webhook_enabled = AICAM_TRUE;
    values.backlog_capacity = 2u;
    CHECK(app_report_service_policy_commit(&g_session, &current_identity, 0u,
                                           &values, &status) == APP_REPORT_ABI_OK);
    CHECK(submit_report(61u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_purge(&old_identity, 0x99887766u,
                                         &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_query(&g_session, &current_identity,
                                         &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 1u);
    CHECK(status.mqtt_enabled == AICAM_FALSE);
    CHECK(status.webhook_enabled == AICAM_TRUE);
    CHECK(status.backlog_used == 1u);
}

static void test_old_tbs_purge_preserves_unbound_entry(void)
{
    app_report_policy_status_t status;
    app_report_policy_values_t values;
    app_report_policy_identity_t old_identity;
    uint32_t revision = 0u;
    uint32_t boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    reset_service();
    CHECK(submit_report(62u, &boot) == APP_REPORT_ABI_OK);
    old_identity = g_policy_identity;
    old_identity.native_tbs_sha256[0] ^= 0x6Du;
    CHECK(app_report_service_policy_purge(&old_identity, 0x99887767u,
                                         &status) == APP_REPORT_ABI_BUSY);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_PENDING);
    CHECK(status.purged_entries == 0u);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                         &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 0u);
    CHECK(status.backlog_used == 1u);
    CHECK(status.mqtt_enabled == AICAM_TRUE);
    CHECK(status.webhook_enabled == AICAM_TRUE);
    CHECK(app_report_policy_store_get(&old_identity, &values, &revision) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_service_policy_purge_status(&old_identity, 0x99887767u,
                                                &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_PENDING);
    CHECK(status.purged_entries == 0u);
    CHECK(app_report_status(boot, 62u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_PENDING);
    CHECK(app_report_service_policy_purge(&old_identity, 0x99887768u,
                                         &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 0u);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0x99887769u,
                                         &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 1u);
    CHECK(app_report_service_queue_count() == 0u);
}

static void test_new_tbs_purge_preserves_old_unbound_generation(void)
{
    app_report_policy_status_t status;
    app_report_policy_identity_t current_identity;
    uint32_t boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    reset_service();
    CHECK(submit_report(63u, &boot) == APP_REPORT_ABI_OK);
    current_identity = g_policy_identity;
    current_identity.native_tbs_sha256[0] ^= 0x6Du;
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    set_committed_identity(&current_identity);
    CHECK(app_report_service_policy_query(&g_session, &current_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_purge(&current_identity, 0x99887770u,
                                          &status) == APP_REPORT_ABI_BUSY);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_PENDING);
    CHECK(status.purged_entries == 0u);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_status(boot, 63u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_PENDING);
}

static void test_admin_purge_clears_current_unbound_generation(void)
{
    app_report_policy_status_t status;
    app_report_policy_identity_t current_identity;
    app_report_policy_identity_t wrong_base_identity;
    app_report_policy_values_t values;
    uint64_t cleanup_intent = APP_REPORT_POLICY_PURGE_INTENT_ADMIN |
                              0x11223344u;
    uint32_t boot = 0u;
    uint32_t revision = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    reset_service();
    current_identity = g_policy_identity;
    current_identity.native_tbs_sha256[0] ^= 0x6Du;
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(submit_report_for(&g_session, 64u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_policy_store_get(&current_identity, &values, &revision) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_service_policy_purge(&current_identity, cleanup_intent,
                                          &status) == APP_REPORT_ABI_BUSY);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_PENDING);
    CHECK(status.purged_entries == 0u);
    CHECK(app_report_service_queue_count() == 1u);
    set_committed_identity(&current_identity);
    CHECK(app_report_service_policy_query(&g_session, &current_identity,
                                          &status) == APP_REPORT_ABI_OK);
    wrong_base_identity = current_identity;
    memset(wrong_base_identity.app_id, 'z', sizeof(wrong_base_identity.app_id));
    CHECK(app_report_service_trust_uninstall_target(&wrong_base_identity) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
    wrong_base_identity = current_identity;
    wrong_base_identity.app_id[0] = 'z';
    CHECK(app_report_service_trust_uninstall_target(&wrong_base_identity) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_trust_uninstall_target(&current_identity) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_purge(&current_identity, cleanup_intent,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 1u);
    CHECK(status.cleanup_intent == cleanup_intent);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(boot, 64u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_NOT_FOUND);
}

static void test_admin_purge_preserves_other_tbs_late_cleanup(void)
{
    app_report_policy_status_t status;
    app_report_policy_identity_t current_identity;
    app_report_policy_identity_t old_identity;
    app_report_policy_values_t values;
    uint64_t current_intent = APP_REPORT_POLICY_PURGE_INTENT_ADMIN |
                              0xAABB0002u;
    uint64_t old_intent = APP_REPORT_POLICY_PURGE_INTENT_ADMIN |
                          0xAABB0001u;
    uint32_t boot = 0u;
    uint32_t revision = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    reset_service();
    current_identity = g_policy_identity;
    current_identity.native_tbs_sha256[0] ^= 0x6Du;
    old_identity = g_policy_identity;
    old_identity.native_tbs_sha256[0] ^= 0x9Cu;
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(submit_report_for(&g_session, 66u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_policy_store_get(&current_identity, &values, &revision) ==
          APP_REPORT_ABI_NOT_FOUND);
    set_committed_identity(&current_identity);
    CHECK(app_report_service_policy_query(&g_session, &current_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_trust_uninstall_target(&current_identity) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_purge(&old_identity, old_intent, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 0u);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_status(boot, 66u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_PENDING);
    CHECK(app_report_service_policy_purge(&current_identity, current_intent,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 1u);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(boot, 66u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_NOT_FOUND);
}

static void test_admin_purge_preserves_other_tbs_after_query(void)
{
    app_report_policy_status_t status;
    app_report_policy_identity_t current_identity;
    app_report_policy_identity_t old_identity;
    uint64_t old_intent = APP_REPORT_POLICY_PURGE_INTENT_ADMIN |
                          0xAABB0011u;
    uint32_t boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    reset_service();
    current_identity = g_policy_identity;
    current_identity.native_tbs_sha256[0] ^= 0x6Du;
    old_identity = g_policy_identity;
    old_identity.native_tbs_sha256[0] ^= 0x9Cu;
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(submit_report_for(&g_session, 67u, &boot) == APP_REPORT_ABI_OK);
    set_committed_identity(&current_identity);
    CHECK(app_report_service_policy_query(&g_session, &current_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.backlog_used == 1u);
    CHECK(app_report_service_trust_uninstall_target(&current_identity) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_purge(&old_identity, old_intent, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 0u);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_status(boot, 67u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_PENDING);
    CHECK(app_report_service_policy_purge(&current_identity,
                                          APP_REPORT_POLICY_PURGE_INTENT_ADMIN |
                                          0xAABB0012u,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 1u);
    CHECK(app_report_service_queue_count() == 0u);
}

static void test_admin_purge_rejects_unverified_stale_target(void)
{
    app_report_policy_status_t status;
    app_report_policy_identity_t current_identity;
    app_report_policy_identity_t old_identity;
    app_report_policy_identity_t arbitrary_identity;
    uint64_t current_intent = APP_REPORT_POLICY_PURGE_INTENT_ADMIN |
                              0xAABB0021u;
    uint64_t old_intent = APP_REPORT_POLICY_PURGE_INTENT_ADMIN |
                          0xAABB0020u;
    uint32_t boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    reset_service();
    current_identity = g_policy_identity;
    current_identity.native_tbs_sha256[0] ^= 0x6Du;
    old_identity = g_policy_identity;
    old_identity.native_tbs_sha256[0] ^= 0x9Cu;
    arbitrary_identity = g_policy_identity;
    arbitrary_identity.native_tbs_sha256[0] ^= 0x3Cu;
    CHECK(submit_report_for(&g_session, 68u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_trust_uninstall_target(&old_identity) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_trust_uninstall_target(&arbitrary_identity) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_policy_purge(&old_identity, old_intent, &status) ==
          APP_REPORT_ABI_BUSY);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_PENDING);
    CHECK(status.purged_entries == 0u);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_status(boot, 68u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_PENDING);
    set_committed_identity(&current_identity);
    CHECK(app_report_service_policy_query(&g_session, &current_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_trust_uninstall_target(&old_identity) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_policy_purge(&old_identity, old_intent, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 0u);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_status(boot, 68u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_PENDING);
    CHECK(app_report_service_trust_uninstall_target(&current_identity) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_purge(&current_identity, current_intent,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 1u);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(boot, 68u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_NOT_FOUND);
}

static void test_unbound_current_tbs_purge_blocked_until_bind(void)
{
    app_report_policy_status_t status;
    app_report_policy_values_t values;
    uint64_t stored_intent = 0u;
    uint32_t revision = 0u;
    uint32_t boot = 0u;
    uint32_t ms;
    uint32_t ws;

    reset_service();
    CHECK(submit_report(71u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_policy_store_get(&g_policy_identity, &values, &revision) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_policy_store_get_cleanup_intent(&g_policy_identity,
                                                     &stored_intent) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0x11223344u,
                                          &status) == APP_REPORT_ABI_BUSY);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_PENDING);
    CHECK(status.purged_entries == 0u);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_purge_status(&g_policy_identity,
                                                 0x11223344u,
                                                 &status) ==
          APP_REPORT_ABI_BUSY);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_PENDING);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0x11223345u,
                                          &status) == APP_REPORT_ABI_BUSY);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_PENDING);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_policy_store_get(&g_policy_identity, &values, &revision) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_policy_store_get_cleanup_intent(&g_policy_identity,
                                                     &stored_intent) ==
          APP_REPORT_ABI_NOT_FOUND);
    app_report_service_pump();
    CHECK(g_mqtt_calls == 1u);
    CHECK(g_webhook_calls == 1u);
    CHECK(app_report_status(boot, 71u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0x11223344u,
                                          &status) == APP_REPORT_ABI_BUSY);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_PENDING);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.backlog_used == 0u);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0x11223346u,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 1u);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(boot, 71u, &ms, &ws) == APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0x11223346u,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 1u);
    CHECK(app_report_service_queue_count() == 0u);
}

static void test_capacity_and_shared_queue(void)
{
    app_report_policy_status_t status;
    uint32_t boot = 0u;
    app_report_session_t app_b;
    app_report_session_t app_a;
    app_report_policy_identity_t app_b_identity;

    reset_service();
    app_a = g_session;
    CHECK(commit_policy(0u, AICAM_TRUE, AICAM_TRUE, 2u, &status) ==
          APP_REPORT_ABI_OK);
    g_policy_revision = status.revision;
    CHECK(submit_report(1u, &boot) == APP_REPORT_ABI_OK);
    CHECK(submit_report(2u, &boot) == APP_REPORT_ABI_OK);
    CHECK(submit_report(3u, &boot) == APP_REPORT_ABI_QUOTA_EXCEEDED);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.backlog_used == 2u);
    CHECK(commit_policy(1u, AICAM_TRUE, AICAM_TRUE, 1u, &status) ==
          APP_REPORT_ABI_CAPACITY_CONFLICT);
    CHECK(status.revision == 1u);
    CHECK(commit_policy(1u, AICAM_TRUE, AICAM_TRUE, 3u, &status) ==
          APP_REPORT_ABI_OK);
    g_policy_revision = status.revision;
    CHECK(submit_report(3u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.backlog_used == 3u);
    app_b = g_session;
    memset(app_b.publisher_id, 0, sizeof(app_b.publisher_id));
    memcpy(app_b.publisher_id, "publisher-b", sizeof("publisher-b"));
    app_b_identity = g_policy_identity;
    memset(app_b_identity.publisher_id, 0, sizeof(app_b_identity.publisher_id));
    memcpy(app_b_identity.publisher_id, app_b.publisher_id,
           sizeof(app_b.publisher_id));
    set_committed_identity(&app_b_identity);
    CHECK(app_report_service_set_session(&app_b) == APP_REPORT_ABI_OK);
    g_session = app_b;
    CHECK(submit_report(1u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 4u);
    for (uint32_t seq = 10u; seq < 14u; seq++) {
        CHECK(submit_report(seq, &boot) == APP_REPORT_ABI_OK);
    }
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    CHECK(submit_report(14u, &boot) == APP_REPORT_ABI_BUSY);
    set_committed_identity(&g_policy_identity);
    CHECK(app_report_service_set_session(&app_a) == APP_REPORT_ABI_OK);
    g_session = app_a;
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.backlog_used == 3u);
}

static void test_preselected_close_and_independent_channel(void)
{
    app_report_policy_status_t status;
    uint32_t boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    reset_service();
    CHECK(submit_report(21u, &boot) == APP_REPORT_ABI_OK);
    g_commit_on_mqtt_ready = 1;
    app_report_service_pump();
    CHECK(g_mqtt_calls == 0u);
    CHECK(g_webhook_calls == 1u);
    CHECK(app_report_status(boot, 21u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_CANCELLED);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.mqtt_cancelled == 1u);
    CHECK(status.webhook_cancelled == 0u);
    CHECK(commit_policy(status.revision, AICAM_TRUE, AICAM_TRUE,
                        status.backlog_capacity, &status) == APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(g_mqtt_calls == 0u);
    CHECK(g_webhook_calls == 1u);
    CHECK(app_report_status(boot, 21u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_CANCELLED);
}

static void *pump_thread(void *arg)
{
    (void)arg;
    app_report_service_pump();
    return NULL;
}

static void test_send_start_close_cancels_unstarted_channel(void)
{
    app_report_policy_status_t status;
    uint32_t boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_PENDING;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_PENDING;
    pthread_t thread;

    reset_service();
    CHECK(commit_policy(0u, AICAM_TRUE, AICAM_TRUE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    g_policy_revision = status.revision;
    CHECK(status.revision == 1u);
    CHECK(submit_report(31u, &boot) == APP_REPORT_ABI_OK);
    pthread_mutex_lock(&g_gate_mutex);
    g_prestart_enabled = 1;
    g_prestart_started = 0;
    g_prestart_release = 0;
    pthread_mutex_unlock(&g_gate_mutex);
    CHECK(pthread_create(&thread, NULL, pump_thread, NULL) == 0);
    pthread_mutex_lock(&g_gate_mutex);
    while (g_prestart_started == 0) {
        pthread_cond_wait(&g_gate_cond, &g_gate_mutex);
    }
    pthread_mutex_unlock(&g_gate_mutex);
    CHECK(commit_policy(1u, AICAM_FALSE, AICAM_TRUE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(status.revision == 2u);
    pthread_mutex_lock(&g_gate_mutex);
    g_prestart_release = 1;
    pthread_cond_broadcast(&g_gate_cond);
    pthread_mutex_unlock(&g_gate_mutex);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(app_report_status(boot, 31u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_CANCELLED);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    CHECK(g_mqtt_calls == 0u);
    CHECK(g_webhook_calls == 1u);
    CHECK(commit_policy(2u, AICAM_TRUE, AICAM_TRUE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(g_mqtt_calls == 0u);
    CHECK(g_webhook_calls == 1u);
    CHECK(app_report_status(boot, 31u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
}

static void test_inflight_close_unknown_and_reopen(void)
{
    app_report_policy_status_t status;
    uint32_t boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;
    pthread_t thread;

    reset_service();
    CHECK(commit_policy(0u, AICAM_TRUE, AICAM_TRUE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    g_policy_revision = status.revision;
    CHECK(submit_report(31u, &boot) == APP_REPORT_ABI_OK);
    pthread_mutex_lock(&g_gate_mutex);
    g_inprimitive_enabled = 1;
    g_inprimitive_started = 0;
    g_inprimitive_release = 0;
    pthread_mutex_unlock(&g_gate_mutex);
    g_mqtt_result = APP_REPORT_TRANSPORT_UNKNOWN;
    CHECK(pthread_create(&thread, NULL, pump_thread, NULL) == 0);
    pthread_mutex_lock(&g_gate_mutex);
    while (g_inprimitive_started == 0) {
        pthread_cond_wait(&g_gate_cond, &g_gate_mutex);
    }
    pthread_mutex_unlock(&g_gate_mutex);
    CHECK(g_mqtt_calls == 0u);
    CHECK(commit_policy(1u, AICAM_TRUE, AICAM_FALSE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(status.revision == 2u);
    CHECK(commit_policy(2u, AICAM_FALSE, AICAM_FALSE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_BUSY);
    CHECK(status.revision == 2u);
    CHECK(g_mqtt_calls == 0u);
    pthread_mutex_lock(&g_gate_mutex);
    g_inprimitive_release = 1;
    pthread_cond_broadcast(&g_gate_cond);
    pthread_mutex_unlock(&g_gate_mutex);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(app_report_status(boot, 31u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_UNKNOWN);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_CANCELLED);
    CHECK(g_mqtt_calls == 1u);
    CHECK(g_webhook_calls == 0u);
    CHECK(commit_policy(2u, AICAM_FALSE, AICAM_FALSE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(status.revision == 3u);
    CHECK(commit_policy(3u, AICAM_TRUE, AICAM_TRUE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(g_mqtt_calls == 1u);
    CHECK(g_webhook_calls == 0u);
    CHECK(app_report_status(boot, 31u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_CANCELLED);
}

static void test_inflight_webhook_close_busy_then_apply(void)
{
    app_report_policy_status_t status;
    uint32_t boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;
    pthread_t thread;

    reset_service();
    CHECK(commit_policy(0u, AICAM_TRUE, AICAM_TRUE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    g_policy_revision = status.revision;
    CHECK(submit_report(32u, &boot) == APP_REPORT_ABI_OK);
    pthread_mutex_lock(&g_gate_mutex);
    g_webhook_inprimitive_enabled = 1;
    g_webhook_inprimitive_started = 0;
    g_webhook_inprimitive_release = 0;
    pthread_mutex_unlock(&g_gate_mutex);
    g_webhook_result = APP_REPORT_TRANSPORT_UNKNOWN;
    CHECK(pthread_create(&thread, NULL, pump_thread, NULL) == 0);
    pthread_mutex_lock(&g_gate_mutex);
    while (g_webhook_inprimitive_started == 0) {
        pthread_cond_wait(&g_gate_cond, &g_gate_mutex);
    }
    pthread_mutex_unlock(&g_gate_mutex);
    CHECK(g_mqtt_calls == 1u);
    CHECK(g_webhook_calls == 0u);
    CHECK(commit_policy(1u, AICAM_FALSE, AICAM_TRUE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(status.revision == 2u);
    CHECK(commit_policy(2u, AICAM_FALSE, AICAM_FALSE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_BUSY);
    CHECK(status.revision == 2u);
    pthread_mutex_lock(&g_gate_mutex);
    g_webhook_inprimitive_release = 1;
    pthread_cond_broadcast(&g_gate_cond);
    pthread_mutex_unlock(&g_gate_mutex);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(app_report_status(boot, 32u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_UNKNOWN);
    CHECK(g_mqtt_calls == 1u);
    CHECK(g_webhook_calls == 1u);
    CHECK(commit_policy(2u, AICAM_FALSE, AICAM_FALSE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(status.revision == 3u);
    app_report_service_pump();
    CHECK(g_mqtt_calls == 1u);
    CHECK(g_webhook_calls == 1u);
}

static void test_disable_applied_blocks_unbegun_publish(void)
{
    app_report_policy_status_t status;
    uint32_t boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;
    pthread_t thread;

    reset_service();
    CHECK(commit_policy(0u, AICAM_TRUE, AICAM_TRUE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    g_policy_revision = status.revision;
    CHECK(status.revision == 1u);
    CHECK(submit_report(33u, &boot) == APP_REPORT_ABI_OK);
    pthread_mutex_lock(&g_gate_mutex);
    g_poststart_enabled = 1;
    g_poststart_started = 0;
    g_poststart_release = 0;
    pthread_mutex_unlock(&g_gate_mutex);
    CHECK(pthread_create(&thread, NULL, pump_thread, NULL) == 0);
    pthread_mutex_lock(&g_gate_mutex);
    while (g_poststart_started == 0) {
        pthread_cond_wait(&g_gate_cond, &g_gate_mutex);
    }
    pthread_mutex_unlock(&g_gate_mutex);
    CHECK(g_mqtt_calls == 0u);
    CHECK(commit_policy(1u, AICAM_FALSE, AICAM_TRUE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(status.revision == 2u);
    CHECK(g_mqtt_calls == 0u);
    pthread_mutex_lock(&g_gate_mutex);
    g_poststart_release = 1;
    pthread_cond_broadcast(&g_gate_cond);
    pthread_mutex_unlock(&g_gate_mutex);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(g_mqtt_calls == 0u);
    CHECK(g_webhook_calls == 1u);
    CHECK(app_report_status(boot, 33u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_CANCELLED);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    CHECK(commit_policy(2u, AICAM_TRUE, AICAM_TRUE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(g_mqtt_calls == 0u);
    CHECK(g_webhook_calls == 1u);
    CHECK(app_report_status(boot, 33u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_CANCELLED);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
}

static void test_retry_suppression(void)
{
    app_report_policy_status_t status;
    uint32_t boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    reset_service();
    g_mqtt_result = APP_REPORT_TRANSPORT_FAIL;
    CHECK(submit_report(41u, &boot) == APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(g_mqtt_calls == 1u);
    CHECK(g_webhook_calls == 1u);
    CHECK(app_report_status(boot, 41u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    CHECK(commit_policy(0u, AICAM_FALSE, AICAM_TRUE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_status(boot, 41u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_RETRY_SUPPRESSED);
    CHECK(status.mqtt_cancelled == 0u);
    CHECK(commit_policy(1u, AICAM_TRUE, AICAM_TRUE,
                        APP_REPORT_POLICY_DEFAULT_CAPACITY, &status) ==
          APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(g_mqtt_calls == 1u);
}

static void test_storage_failures(void)
{
    app_report_policy_status_t status;
    uint8_t saved[APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY];
    uint32_t saved_len;

    reset_service();
    CHECK(commit_policy(0u, AICAM_TRUE, AICAM_TRUE, 3u, &status) ==
          APP_REPORT_ABI_OK);
    saved_len = g_blob_len;
    memcpy(saved, g_blob, saved_len);
    g_fail_write = 1;
    CHECK(commit_policy(1u, AICAM_FALSE, AICAM_TRUE, 3u, &status) ==
          APP_REPORT_ABI_STORAGE_ERROR);
    CHECK(status.storage_verified == AICAM_TRUE);
    CHECK(status.revision == 1u);
    CHECK(status.mqtt_enabled == AICAM_TRUE);
    g_fail_write = 0;
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 1u);
    g_partial_write = 1;
    CHECK(commit_policy(1u, AICAM_FALSE, AICAM_TRUE, 3u, &status) ==
          APP_REPORT_ABI_STORAGE_UNKNOWN);
    g_partial_write = 0;
    memcpy(g_blob, saved, saved_len);
    g_blob_len = saved_len;
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 1u);
    g_blob[10] ^= 0x80u;
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_STORAGE_UNKNOWN);
    CHECK(submit_report(90u, &(uint32_t){0u}) == APP_REPORT_ABI_STORAGE_UNKNOWN);
    g_read_error = 1;
    CHECK(commit_policy(1u, AICAM_FALSE, AICAM_TRUE, 3u, &status) ==
          APP_REPORT_ABI_STORAGE_UNKNOWN);
}

static void test_purge_retry_and_reinstall(void)
{
    app_report_policy_status_t status;
    app_report_policy_status_t app_b_status;
    app_report_session_t app_b;
    app_report_policy_identity_t app_b_identity;
    uint32_t boot = 0u;

    reset_service();
    CHECK(commit_policy(0u, AICAM_FALSE, AICAM_TRUE, 2u, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(submit_report(51u, &boot) == APP_REPORT_ABI_OK);
    app_b = g_session;
    memcpy(app_b.publisher_id, "publisher-b", sizeof("publisher-b"));
    app_b_identity = g_policy_identity;
    memset(app_b_identity.publisher_id, 0,
           sizeof(app_b_identity.publisher_id));
    memcpy(app_b_identity.publisher_id, app_b.publisher_id,
           sizeof(app_b.publisher_id));
    set_committed_identity(&app_b_identity);
    CHECK(app_report_service_set_session(&app_b) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_query(&app_b, &app_b_identity,
                                          &app_b_status) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_commit(&app_b, &app_b_identity, 0u,
                                          &(app_report_policy_values_t){
                                              AICAM_TRUE, AICAM_FALSE, 2u
                                          }, &app_b_status) == APP_REPORT_ABI_OK);
    CHECK(submit_report_for(&app_b, 52u, &boot) == APP_REPORT_ABI_OK);
    g_fail_write = 1;
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0xAABBCCDDu,
                                         &status) == APP_REPORT_ABI_STORAGE_ERROR);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_PENDING);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0xAABBCCDEu,
                                         &status) == APP_REPORT_ABI_BUSY);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0xAABBCCDEu,
                                         &status) == APP_REPORT_ABI_BUSY);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(submit_report(52u, &boot) == APP_REPORT_ABI_STORAGE_UNKNOWN);
    g_fail_write = 0;
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0xAABBCCDDu,
                                         &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 1u);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0xAABBCCDDu,
                                         &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(app_report_service_policy_purge_status(&g_policy_identity,
                                                 0xAABBCCDDu,
                                                 &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.cleanup_intent == 0xAABBCCDDu);
    CHECK(status.revision == 0u);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    set_committed_identity(&g_policy_identity);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 0u);
    CHECK(status.mqtt_enabled == AICAM_TRUE);
    CHECK(status.webhook_enabled == AICAM_TRUE);
    CHECK(status.backlog_capacity == APP_REPORT_POLICY_DEFAULT_CAPACITY);
    set_committed_identity(&app_b_identity);
    CHECK(app_report_service_set_session(&app_b) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_query(&app_b, &app_b_identity,
                                          &app_b_status) == APP_REPORT_ABI_OK);
    CHECK(app_b_status.revision == 1u);
    CHECK(app_b_status.mqtt_enabled == AICAM_TRUE);
    CHECK(app_b_status.webhook_enabled == AICAM_FALSE);
    {
        app_report_stats_t stats;
        CHECK(submit_report_for(&app_b, 52u, &boot) == APP_REPORT_ABI_OK);
        app_report_service_get_stats(&stats);
        CHECK(stats.dedup_hits == 1u);
        CHECK(app_report_service_queue_count() == 1u);
    }
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    set_committed_identity(&g_policy_identity);
    CHECK(commit_policy(0u, AICAM_FALSE, AICAM_TRUE, 2u, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(status.revision == 1u);
    CHECK(submit_report(53u, &boot) == APP_REPORT_ABI_OK);
    app_report_service_init(&g_runtime_identity, &g_transport);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0xAABBCCDDu,
                                         &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.revision == 1u);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 1u);
    CHECK(submit_report(54u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0xAABBCCDDu,
                                         &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0xAABBCCDFu,
                                         &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(app_report_service_queue_count() == 0u);
}

static void test_stale_query_bind_purge_blocked(void)
{
    app_report_policy_status_t status;
    app_report_policy_identity_t current_identity;
    app_report_policy_identity_t stale_identity;
    uint64_t stale_intent = APP_REPORT_POLICY_PURGE_INTENT_ADMIN |
                            0xAABB0031u;
    uint64_t current_intent = APP_REPORT_POLICY_PURGE_INTENT_ADMIN |
                              0xAABB0032u;
    uint32_t boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    reset_service();
    current_identity = g_policy_identity;
    current_identity.native_tbs_sha256[0] ^= 0x6Du;
    stale_identity = g_policy_identity;
    set_committed_identity(&current_identity);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(submit_report_for(&g_session, 81u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_query(&g_session, &stale_identity,
                                          &status) == APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_trust_uninstall_target(&stale_identity) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_policy_purge(&stale_identity, stale_intent,
                                          &status) == APP_REPORT_ABI_BUSY);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_PENDING);
    CHECK(status.purged_entries == 0u);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_status(boot, 81u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_PENDING);
    CHECK(app_report_service_policy_purge(&stale_identity, stale_intent + 1u,
                                          &status) == APP_REPORT_ABI_BUSY);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_query(&g_session, &current_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.backlog_used == 1u);
    CHECK(app_report_service_trust_uninstall_target(&current_identity) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_purge(&current_identity, current_intent,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 1u);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(boot, 81u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_NOT_FOUND);
}

static void test_reinstall_reanchors_identity_binding(void)
{
    app_report_policy_status_t status;
    app_report_policy_values_t values;
    app_report_policy_identity_t updated_identity;
    uint64_t current_intent = APP_REPORT_POLICY_PURGE_INTENT_ADMIN |
                              0xAABB0041u;
    uint32_t boot = 0u;

    reset_service();
    CHECK(submit_report(82u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.backlog_used == 1u);
    values.mqtt_enabled = AICAM_FALSE;
    values.webhook_enabled = AICAM_TRUE;
    values.backlog_capacity = 2u;
    CHECK(app_report_service_policy_commit(&g_session, &g_policy_identity, 0u,
                                           &values, &status) == APP_REPORT_ABI_OK);
    updated_identity = g_policy_identity;
    updated_identity.native_tbs_sha256[0] ^= 0x6Du;
    set_committed_identity(&updated_identity);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_policy_query(&g_session, &updated_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 0u);
    CHECK(status.mqtt_enabled == AICAM_TRUE);
    CHECK(submit_report(83u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_query(&g_session, &updated_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.backlog_used == 1u);
    CHECK(app_report_service_policy_purge(&updated_identity, current_intent,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(status.purged_entries == 1u);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_UNAUTHORIZED);
}

static void test_uncommitted_identity_cannot_bind(void)
{
    app_report_policy_status_t status;
    app_report_policy_values_t values;
    uint32_t revision = 0u;

    reset_service();
    g_install_present = 0;
    app_install_boot_resolve_reset();
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_trust_uninstall_target(&g_policy_identity) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    app_report_service_set_committed_io(NULL);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_STORAGE_UNKNOWN);
    CHECK(app_report_service_trust_uninstall_target(&g_policy_identity) ==
          APP_REPORT_ABI_STORAGE_UNKNOWN);
    g_install_read_error = 1;
    app_report_service_set_committed_io(&g_install_io);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_STORAGE_UNKNOWN);
    g_install_read_error = 0;
    set_committed_identity(&g_policy_identity);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 0u);
    CHECK(app_report_policy_store_get(&g_policy_identity, &values,
                                      &revision) == APP_REPORT_ABI_NOT_FOUND);
}

static void test_distrusted_committed_rejects_report_ingress(void)
{
    app_report_policy_status_t status;
    app_report_session_t app_b;
    app_report_policy_identity_t app_b_identity;
    app_report_stats_t stats;
    uint32_t boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    reset_service();
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(submit_report(91u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_status(boot, 91u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_PENDING);
    app_b = g_session;
    memcpy(app_b.publisher_id, "publisher-b", sizeof("publisher-b"));
    app_b_identity = g_policy_identity;
    memset(app_b_identity.publisher_id, 0, sizeof(app_b_identity.publisher_id));
    memcpy(app_b_identity.publisher_id, app_b.publisher_id,
           sizeof(app_b.publisher_id));
    set_committed_identity(&app_b_identity);
    CHECK(submit_report(92u, &boot) == APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_queue_count() == 1u);
    g_install_present = 0;
    CHECK(submit_report(93u, &boot) == APP_REPORT_ABI_UNAUTHORIZED);
    g_install_read_error = 1;
    CHECK(submit_report(94u, &boot) == APP_REPORT_ABI_STORAGE_UNKNOWN);
    g_install_read_error = 0;
    app_report_service_set_committed_io(NULL);
    CHECK(submit_report(95u, &boot) == APP_REPORT_ABI_STORAGE_UNKNOWN);
    app_report_service_set_committed_io(&g_install_io);
    set_committed_identity(&app_b_identity);
    CHECK(app_report_service_set_session(&app_b) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_query(&app_b, &app_b_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(submit_report_for(&app_b, 96u, &boot) == APP_REPORT_ABI_OK);
    set_committed_identity(&g_policy_identity);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(submit_report(97u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 3u);
    app_report_service_get_stats(&stats);
    CHECK(stats.rejected_identity == 4u);
}

static void test_same_base_tbs_swap_rejects_bound_ingress(void)
{
    app_report_policy_status_t status;
    app_report_policy_identity_t updated_identity;
    app_report_entry_info_t info;
    uint32_t boot = 0u;
    uint32_t accepted_boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    reset_service();
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(submit_report(91u, &boot) == APP_REPORT_ABI_OK);
    accepted_boot = boot;
    CHECK(app_report_service_queue_count() == 1u);
    updated_identity = g_policy_identity;
    updated_identity.native_tbs_sha256[0] ^= 0x5Bu;
    set_committed_identity(&updated_identity);
    CHECK(submit_report(92u, &boot) == APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(submit_report(93u, &boot) == APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_status(accepted_boot, 91u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_PENDING);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_query(&g_session, &updated_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_entry(0u, &info) == AICAM_TRUE);
    CHECK(info.report_seq == 91u);
    CHECK(app_report_service_entry(1u, &info) == AICAM_FALSE);
    CHECK(app_report_status(accepted_boot, 92u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(submit_report(94u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 2u);
}

static void test_same_base_tbs_swap_rejects_with_persisted_policy(void)
{
    app_report_policy_status_t status;
    app_report_policy_identity_t updated_identity;
    uint32_t boot = 0u;
    uint32_t accepted_boot = 0u;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    reset_service();
    CHECK(commit_policy(0u, AICAM_TRUE, AICAM_TRUE, 2u, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(status.revision == 1u);
    CHECK(submit_report(71u, &boot) == APP_REPORT_ABI_OK);
    accepted_boot = boot;
    CHECK(app_report_service_queue_count() == 1u);
    updated_identity = g_policy_identity;
    updated_identity.native_tbs_sha256[0] ^= 0x5Bu;
    set_committed_identity(&updated_identity);
    CHECK(submit_report(72u, &boot) == APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_status(accepted_boot, 71u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_PENDING);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(submit_report(73u, &boot) == APP_REPORT_ABI_STORAGE_UNKNOWN);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_query(&g_session, &updated_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.revision == 0u);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(submit_report(74u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 2u);
}

static void test_completed_purge_allows_same_base_new_tbs_ingress(void)
{
    app_report_policy_status_t status;
    app_report_policy_identity_t updated_identity;
    app_report_policy_identity_t probe;
    app_report_policy_values_t values;
    uint32_t revision = 0u;
    uint32_t boot = 0u;
    aicam_bool_t found = AICAM_TRUE;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;
    app_report_entry_info_t info;

    reset_service();
    CHECK(commit_policy(0u, AICAM_TRUE, AICAM_TRUE, 2u, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(submit_report(101u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_purge(&g_policy_identity, 0xAABBCC01u,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_policy_store_get(&g_policy_identity, &values,
                                      &revision) == APP_REPORT_ABI_NOT_FOUND);
    probe = g_policy_identity;
    memset(probe.native_tbs_sha256, 1, sizeof(probe.native_tbs_sha256));
    CHECK(app_report_policy_store_has_base(&probe, &found) == APP_REPORT_ABI_OK);
    CHECK(found == AICAM_FALSE);
    updated_identity = g_policy_identity;
    updated_identity.native_tbs_sha256[0] ^= 0x5Bu;
    set_committed_identity(&updated_identity);
    CHECK(app_report_service_set_session(&g_session) == APP_REPORT_ABI_OK);
    CHECK(submit_report(102u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(submit_report(103u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 2u);
    CHECK(app_report_service_entry(0u, &info) == AICAM_TRUE);
    CHECK(info.report_seq == 102u);
    CHECK(app_report_status(boot, 102u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_PENDING);
}

static void test_unbound_healthy_first_report_then_bind(void)
{
    app_report_policy_status_t status;
    app_report_policy_identity_t updated_identity;
    uint32_t boot = 0u;

    reset_service();
    CHECK(submit_report(81u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_service_policy_query(&g_session, &g_policy_identity,
                                          &status) == APP_REPORT_ABI_OK);
    CHECK(status.backlog_used == 1u);
    CHECK(submit_report(82u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 2u);
    updated_identity = g_policy_identity;
    updated_identity.native_tbs_sha256[0] ^= 0x5Bu;
    set_committed_identity(&updated_identity);
    CHECK(submit_report(83u, &boot) == APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_queue_count() == 2u);
}

int main(void)
{
    test_policy_revision_and_restart();
    test_submit_adopts_legacy_policy_base_after_upgrade();
    test_current_tbs_rebind();
    test_old_tbs_purge_preserves_current_install();
    test_old_tbs_purge_preserves_unbound_entry();
    test_new_tbs_purge_preserves_old_unbound_generation();
    test_admin_purge_clears_current_unbound_generation();
    test_admin_purge_preserves_other_tbs_late_cleanup();
    test_admin_purge_preserves_other_tbs_after_query();
    test_admin_purge_rejects_unverified_stale_target();
    test_stale_query_bind_purge_blocked();
    test_reinstall_reanchors_identity_binding();
    test_uncommitted_identity_cannot_bind();
    test_distrusted_committed_rejects_report_ingress();
    test_same_base_tbs_swap_rejects_bound_ingress();
    test_same_base_tbs_swap_rejects_with_persisted_policy();
    test_completed_purge_allows_same_base_new_tbs_ingress();
    test_unbound_healthy_first_report_then_bind();
    test_capacity_and_shared_queue();
    test_preselected_close_and_independent_channel();
    test_send_start_close_cancels_unstarted_channel();
    test_disable_applied_blocks_unbegun_publish();
    test_inflight_close_unknown_and_reopen();
    test_inflight_webhook_close_busy_then_apply();
    test_retry_suppression();
    test_storage_failures();
    test_purge_retry_and_reinstall();
    test_unbound_current_tbs_purge_blocked_until_bind();
    printf("app_report_policy: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
