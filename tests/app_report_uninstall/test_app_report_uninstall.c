#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_installer.h"
#include "app_sidecar_lifecycle.h"
#include "app_report_policy.h"
#include "app_report_service.h"
#include "app_report_service_device.h"
#include "cmsis_os2.h"
#include "debug.h"
#include "factory_test.h"
#include "mem_sidecar_backend.h"
#include "mqtt_service.h"
#include "netif_manager.h"
#include "stm32n6xx_hal.h"
#include "storage.h"
#include "webhook_service.h"

static int g_total;
static int g_fail;
static uint8_t g_policy_blob[APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY];
static uint32_t g_policy_len;
static uint32_t g_tick;
static int g_mqtt_mode;
static int g_storage_fail_next_read;
static int g_storage_fail_next_write;
static int g_storage_partial_next_write;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL app_report_uninstall:%d %s\n", g_total, #cond); \
    } \
} while (0)

aicam_result_t app_report_service_device_uninstall_prepare(
    const app_install_io_t *io);
void app_report_service_device_uninstall_finish(const app_install_io_t *io);
const app_uninstall_cleanup_ops_t *app_report_service_device_cleanup_ops(void);

const app_install_io_t *app_installer_device_io(void)
{
    return NULL;
}

osMutexId_t osMutexNew(const osMutexAttr_t *attr)
{
    pthread_mutexattr_t mutex_attr;
    pthread_mutex_t *mutex = (pthread_mutex_t *)malloc(sizeof(*mutex));

    if (mutex == NULL) {
        return NULL;
    }
    (void)pthread_mutexattr_init(&mutex_attr);
    if (attr != NULL && (attr->attr_bits & osMutexRecursive) != 0u) {
        (void)pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    }
    if (pthread_mutex_init(mutex, &mutex_attr) != 0) {
        free(mutex);
        mutex = NULL;
    }
    (void)pthread_mutexattr_destroy(&mutex_attr);
    return mutex;
}

osStatus_t osMutexAcquire(osMutexId_t id, uint32_t timeout)
{
    (void)timeout;
    return (id != NULL && pthread_mutex_lock((pthread_mutex_t *)id) == 0) ?
           osOK : osError;
}

osStatus_t osMutexRelease(osMutexId_t id)
{
    return (id != NULL && pthread_mutex_unlock((pthread_mutex_t *)id) == 0) ?
           osOK : osError;
}

osThreadId_t osThreadNew(void (*func)(void *), void *arg,
                         const osThreadAttr_t *attr)
{
    (void)func;
    (void)arg;
    (void)attr;
    return NULL;
}

osStatus_t osThreadTerminate(osThreadId_t id)
{
    (void)id;
    return osError;
}

osThreadState_t osThreadGetState(osThreadId_t id)
{
    (void)id;
    return osThreadTerminated;
}

uint32_t osKernelGetTickCount(void)
{
    return g_tick++;
}

osStatus_t osDelay(uint32_t ticks)
{
    g_tick += ticks;
    return osOK;
}

int storage_nvs_read(NVS_Type_t type, const char *key, void *data, size_t len)
{
    if (type != NVS_USER || strcmp(key, "app_report_policy") != 0 ||
        data == NULL || len < g_policy_len) {
        return -EINVAL;
    }
    if (g_storage_fail_next_read > 0) {
        g_storage_fail_next_read--;
        return -EIO;
    }
    if (g_policy_len == 0u) {
        return -ENOENT;
    }
    memcpy(data, g_policy_blob, g_policy_len);
    return (int)g_policy_len;
}

int storage_nvs_write(NVS_Type_t type, const char *key, const void *data,
                      size_t len)
{
    size_t stored_len = len;

    if (type != NVS_USER || strcmp(key, "app_report_policy") != 0 ||
        data == NULL || len == 0u || len > sizeof(g_policy_blob)) {
        return -EINVAL;
    }
    if (g_storage_fail_next_write > 0) {
        g_storage_fail_next_write--;
        return -EIO;
    }
    if (g_storage_partial_next_write > 0) {
        g_storage_partial_next_write--;
        stored_len = len / 2u;
    }
    memcpy(g_policy_blob, data, stored_len);
    g_policy_len = (uint32_t)stored_len;
    return (int)stored_len;
}

int factory_mac_get_burned(uint8_t *mac)
{
    static const uint8_t value[6] = {0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u};

    if (mac == NULL) {
        return -1;
    }
    memcpy(mac, value, sizeof(value));
    return 0;
}

aicam_result_t nm_get_netif_info(const char *if_name, netif_info_t *netif_info)
{
    (void)if_name;
    (void)netif_info;
    return AICAM_ERROR;
}

uint32_t HAL_GetUIDw0(void)
{
    return 1u;
}

uint32_t HAL_GetUIDw1(void)
{
    return 2u;
}

uint32_t HAL_GetUIDw2(void)
{
    return 3u;
}

aicam_result_t mqtt_service_get_topic_config(mqtt_service_topic_config_t *config)
{
    if (config == NULL) {
        return AICAM_ERROR;
    }
    memset(config, 0, sizeof(*config));
    memcpy(config->data_report_topic, "reports", sizeof("reports"));
    config->data_report_qos = 1;
    return AICAM_OK;
}

aicam_bool_t mqtt_service_is_running(void)
{
    return AICAM_TRUE;
}

int mqtt_service_publish(const char *topic, const uint8_t *payload,
                         int payload_len, int qos, int retain)
{
    (void)topic;
    (void)payload;
    (void)payload_len;
    (void)qos;
    (void)retain;
    return (g_mqtt_mode == 0) ? 1 : -1;
}

aicam_result_t mqtt_service_get_acked_msg_id(int *out_msg_id, uint32_t timeout_ms)
{
    (void)out_msg_id;
    g_tick += timeout_ms;
    return AICAM_ERROR_TIMEOUT;
}

aicam_result_t json_config_get_webhook_config(webhook_config_t *config)
{
    if (config != NULL) {
        memset(config, 0, sizeof(*config));
    }
    return AICAM_ERROR;
}

aicam_result_t webhook_service_push_json(const uint8_t *json, uint32_t json_len)
{
    (void)json;
    (void)json_len;
    return AICAM_ERROR;
}

void debug_register_commands(const debug_cmd_reg_t *table, uint32_t count)
{
    (void)table;
    (void)count;
}

static void make_record(app_install_record_t *record, uint8_t seed)
{
    size_t i;

    memset(record, 0, sizeof(*record));
    memcpy(record->publisher_id, "publisher", sizeof("publisher"));
    memcpy(record->app_id, "app-a", sizeof("app-a"));
    record->version_major = 1u;
    record->version_minor = 2u;
    record->version_patch = 3u;
    record->package_len = 1u;
    for (i = 0u; i < APP_INSTALL_SHA256_LEN; i++) {
        record->package_sha256[i] = (uint8_t)(seed + i);
        record->tbs_sha256[i] = (uint8_t)(seed + 32u + i);
        record->image_sha256[i] = (uint8_t)(seed + 64u + i);
        record->publisher_key_sha256[i] = (uint8_t)(0x90u + i);
    }
    memcpy(record->package_path, "/apps/pkg/", sizeof("/apps/pkg/") - 1u);
    for (i = 0u; i < APP_INSTALL_SHA256_LEN; i++) {
        (void)snprintf(record->package_path + 10u + i * 2u,
                       sizeof(record->package_path) - 10u - i * 2u,
                       "%02x", record->package_sha256[i]);
    }
    memcpy(record->package_path + 74u, ".neapp", sizeof(".neapp"));
}

static void store_record(const app_install_io_t *io,
                        const app_install_record_t *record);

static void make_session(const app_install_record_t *record,
                         app_report_session_t *session)
{
    memset(session, 0, sizeof(*session));
    session->active = AICAM_TRUE;
    memcpy(session->publisher_id, record->publisher_id,
           sizeof(session->publisher_id));
    memcpy(session->app_id, record->app_id, sizeof(session->app_id));
    memcpy(session->publisher_key_sha256, record->publisher_key_sha256,
           sizeof(session->publisher_key_sha256));
    session->report_max = APP_REPORT_INPUT_MAX;
}

static void make_policy_identity(const app_install_record_t *record,
                                app_report_policy_identity_t *identity)
{
    memset(identity, 0, sizeof(*identity));
    memcpy(identity->publisher_id, record->publisher_id,
           sizeof(identity->publisher_id));
    memcpy(identity->app_id, record->app_id, sizeof(identity->app_id));
    memcpy(identity->publisher_key_sha256, record->publisher_key_sha256,
           sizeof(identity->publisher_key_sha256));
    memcpy(identity->native_tbs_sha256, record->tbs_sha256,
           sizeof(identity->native_tbs_sha256));
}

static size_t make_report(char *buf, size_t cap, const app_install_record_t *record)
{
    return (size_t)snprintf(
        buf, cap,
        "{\"schema_version\":1,\"type\":\"line_counting\","
        "\"app\":{\"app_id\":\"%s\",\"sw_version\":\"1.0.0\"},"
        "\"boot_id\":1,\"report_seq\":1,\"clock_valid\":false,"
        "\"reported_at\":null,\"window\":{\"start_ms\":0,"
        "\"end_ms\":1000,\"in\":1,\"out\":0},"
        "\"total\":{\"in\":5,\"out\":3},"
        "\"counter\":{\"counter_name\":\"d\"},"
        "\"target\":{\"class_name\":\"person\"},"
        "\"model\":{\"name\":\"yolo26n\",\"version\":\"1.0\"}}",
        record->app_id);
}

static void reset_fixture(app_install_io_t *io, app_install_record_t *record,
                          uint8_t seed)
{
    mem_reset();
    mem_io_init(io);
    memset(g_policy_blob, 0, sizeof(g_policy_blob));
    g_policy_len = 0u;
    g_storage_fail_next_read = 0;
    g_storage_fail_next_write = 0;
    g_storage_partial_next_write = 0;
    g_mqtt_mode = 0;
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    app_install_set_app_running(AICAM_FALSE);
    app_report_service_restart();
    (void)app_report_service_set_session(NULL);
    make_record(record, seed);
    store_record(io, record);
    CHECK(app_install_boot_resolve(io) == APP_INSTALL_OK);
    app_report_service_set_committed_io(io);
}

static app_uninstall_purge_result_t purge_unknown_once(
    void *ctx, const app_install_record_t *record)
{
    (void)ctx;
    (void)record;
    return APP_UNINSTALL_PURGE_UNKNOWN;
}

static app_install_record_t read_committed_fixture_record(void)
{
    app_install_record_t record;
    mem_kv_t *kv = mem_find_key(APP_INSTALL_KEY_RECORD);

    memset(&record, 0, sizeof(record));
    if (kv == NULL ||
        app_install_record_decode(kv->data, kv->len, &record) != AICAM_TRUE) {
        memset(&record, 0, sizeof(record));
    }
    return record;
}

static void store_record(const app_install_io_t *io,
                         const app_install_record_t *record)
{
    uint8_t encoded[APP_INSTALL_RECORD_BIN_SIZE];
    uint8_t package[] = {0x5au};

    CHECK(app_install_record_encode(record, encoded, sizeof(encoded)) == AICAM_TRUE);
    CHECK(io->kv_write(io->ctx, APP_INSTALL_KEY_RECORD, encoded,
                       sizeof(encoded)) == APP_INSTALL_IO_OK);
    CHECK(io->file_write(io->ctx, record->package_path, package,
                         sizeof(package)) == APP_INSTALL_IO_OK);
}

static void run_confirmed_uninstall(const app_install_io_t *io,
                                    app_install_record_t *record,
                                    app_uninstall_result_t *result,
                                    app_uninstall_state_t *state)
{
    app_install_record_t removed;

    CHECK(app_report_service_device_uninstall_prepare(io) == AICAM_OK);
    *result = app_install_uninstall_confirmed(
        io, AICAM_TRUE, app_report_service_device_cleanup_ops(), &removed, state);
    app_report_service_device_uninstall_finish(io);
    CHECK(memcmp(removed.tbs_sha256, record->tbs_sha256,
                 APP_INSTALL_SHA256_LEN) == 0);
}

static void test_real_report_purge_and_new_generation_on_reinstall(void)
{
    app_install_io_t io;
    app_install_record_t record;
    app_report_policy_identity_t identity;
    app_uninstall_result_t result;
    app_uninstall_state_t state;
    uint64_t first_intent = 0u;
    uint64_t second_intent = 0u;

    mem_reset();
    mem_io_init(&io);
    memset(g_policy_blob, 0, sizeof(g_policy_blob));
    g_policy_len = 0u;
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    app_install_set_app_running(AICAM_FALSE);
    make_record(&record, 0x11u);
    store_record(&io, &record);
    CHECK(count_files() == 1);
    run_confirmed_uninstall(&io, &record, &result, &state);
    CHECK(result == APP_UNINSTALL_OK);
    CHECK(state == APP_UNINSTALL_STATE_NONE);
    memcpy(identity.publisher_id, record.publisher_id,
           sizeof(identity.publisher_id));
    memcpy(identity.app_id, record.app_id, sizeof(identity.app_id));
    memcpy(identity.publisher_key_sha256, record.publisher_key_sha256,
           sizeof(identity.publisher_key_sha256));
    memcpy(identity.native_tbs_sha256, record.tbs_sha256,
           sizeof(identity.native_tbs_sha256));
    CHECK(app_report_policy_store_get_cleanup_intent(&identity, &first_intent) ==
          APP_REPORT_ABI_OK);
    CHECK((first_intent & APP_REPORT_POLICY_PURGE_INTENT_ADMIN) != 0u);
    CHECK(app_report_service_policy_purge_status(&identity, first_intent,
                                                 &(app_report_policy_status_t){0}) ==
          APP_REPORT_ABI_OK);

    store_record(&io, &record);
    app_install_boot_resolve_reset();
    run_confirmed_uninstall(&io, &record, &result, &state);
    CHECK(result == APP_UNINSTALL_OK);
    CHECK(state == APP_UNINSTALL_STATE_NONE);
    CHECK(app_report_policy_store_get_cleanup_intent(&identity, &second_intent) ==
          APP_REPORT_ABI_OK);
    CHECK((second_intent & APP_REPORT_POLICY_PURGE_INTENT_ADMIN) != 0u);
    CHECK(first_intent != second_intent);
}

static void test_enqueue_before_query_is_revoked_and_purged_for_current_tbs(void)
{
    app_install_io_t io;
    app_install_record_t record;
    app_report_session_t session;
    app_report_policy_identity_t identity;
    app_report_policy_status_t status;
    app_install_record_t removed;
    app_uninstall_state_t state;
    char report[512];
    uint32_t boot_id = 0u;
    uint32_t mqtt_state = 0u;
    uint32_t webhook_state = 0u;
    size_t report_len;

    reset_fixture(&io, &record, 0x31u);
    make_session(&record, &session);
    report_len = make_report(report, sizeof(report), &record);
    CHECK(app_report_service_set_session(&session) == APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&session, report, (uint32_t)report_len, 1u,
                            &boot_id) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_status(boot_id, 1u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_PENDING);
    CHECK(app_report_service_device_uninstall_prepare(&io) == AICAM_OK);
    CHECK(app_report_service_queue_count() == 1u);
    CHECK(app_report_status(boot_id, 1u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_service_entry(0u, &(app_report_entry_info_t){0}) ==
          AICAM_TRUE);
    CHECK(app_report_submit(&session, report, (uint32_t)report_len, 2u,
                            &boot_id) == APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_entry(0u, &(app_report_entry_info_t){0}) ==
          AICAM_TRUE);
    {
        app_report_entry_info_t entry;
        CHECK(app_report_service_entry(0u, &entry) == AICAM_TRUE);
        CHECK(entry.mqtt_state == APP_REPORT_MQTT_CANCELLED);
        CHECK(entry.webhook_state == APP_REPORT_WEBHOOK_CANCELLED);
    }
    CHECK(app_install_uninstall_confirmed(
              &io, AICAM_TRUE, app_report_service_device_cleanup_ops(),
              &removed, &state) == APP_UNINSTALL_OK);
    app_report_service_device_uninstall_finish(&io);
    make_policy_identity(&record, &identity);
    CHECK(app_report_policy_store_get_cleanup_intent(&identity,
                                                     &status.cleanup_intent) ==
          APP_REPORT_ABI_OK);
    CHECK((status.cleanup_intent & APP_REPORT_POLICY_PURGE_INTENT_ADMIN) != 0u);
    CHECK(app_report_service_policy_purge_status(&identity,
                                                status.cleanup_intent,
                                                &status) == APP_REPORT_ABI_OK);
    CHECK(status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_submit(&session, report, (uint32_t)report_len, 3u,
                            &boot_id) == APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(state == APP_UNINSTALL_STATE_NONE);
}

static void test_started_unknown_blocks_uninstall(void)
{
    app_install_io_t io;
    app_install_record_t record;
    app_report_session_t session;
    app_report_entry_info_t entry;
    app_install_record_t current;
    aicam_bool_t present = AICAM_FALSE;
    char report[512];
    uint32_t boot_id = 0u;
    size_t report_len;

    reset_fixture(&io, &record, 0x41u);
    make_session(&record, &session);
    report_len = make_report(report, sizeof(report), &record);
    CHECK(app_report_service_set_session(&session) == APP_REPORT_ABI_OK);
    CHECK(app_report_submit(&session, report, (uint32_t)report_len, 1u,
                            &boot_id) == APP_REPORT_ABI_OK);
    app_report_service_pump();
    CHECK(app_report_service_entry(0u, &entry) == AICAM_TRUE);
    CHECK(entry.mqtt_state == APP_REPORT_MQTT_UNKNOWN);
    CHECK(app_report_service_device_uninstall_prepare(&io) != AICAM_OK);
    CHECK(app_install_query_committed(&io, &current, &present) == APP_INSTALL_OK);
    CHECK(present == AICAM_TRUE);
    CHECK(memcmp(current.tbs_sha256, record.tbs_sha256,
                 APP_INSTALL_SHA256_LEN) == 0);
    app_report_service_device_uninstall_finish(&io);
}

static void test_policy_storage_unknown_and_partial_write_fail_closed(void)
{
    app_install_io_t io;
    app_install_record_t record;
    app_install_record_t current;
    aicam_bool_t present = AICAM_FALSE;

    reset_fixture(&io, &record, 0x51u);
    g_storage_fail_next_read = 1;
    CHECK(app_report_service_device_uninstall_prepare(&io) != AICAM_OK);
    app_report_service_device_uninstall_finish(&io);
    CHECK(app_install_query_committed(&io, &current, &present) == APP_INSTALL_OK);
    CHECK(present == AICAM_TRUE);

    reset_fixture(&io, &record, 0x61u);
    g_storage_partial_next_write = 1;
    CHECK(app_report_service_device_uninstall_prepare(&io) != AICAM_OK);
    app_report_service_device_uninstall_finish(&io);
    CHECK(app_install_query_committed(&io, &current, &present) == APP_INSTALL_OK);
    CHECK(present == AICAM_TRUE);
}

static void test_retry_and_corrupt_durable_intent(void)
{
    app_install_io_t io;
    app_install_record_t record;
    app_install_record_t removed;
    app_uninstall_cleanup_ops_t fail_ops = {NULL, purge_unknown_once};
    app_uninstall_result_t result;
    app_uninstall_state_t state;
    mem_kv_t *bridge;

    reset_fixture(&io, &record, 0x71u);
    CHECK(app_report_service_device_uninstall_prepare(&io) == AICAM_OK);
    result = app_install_uninstall_confirmed(&io, AICAM_TRUE, &fail_ops,
                                             &removed, &state);
    CHECK(result == APP_UNINSTALL_CLEANUP_PENDING);
    CHECK(state == APP_UNINSTALL_STATE_CLEANUP_PENDING);
    app_report_service_device_uninstall_finish(&io);
    app_report_service_restart();
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    CHECK(app_report_service_device_uninstall_prepare(&io) == AICAM_OK);
    CHECK(app_install_uninstall_confirmed(
              &io, AICAM_TRUE, app_report_service_device_cleanup_ops(),
              &removed, &state) == APP_UNINSTALL_OK);
    app_report_service_device_uninstall_finish(&io);
    CHECK(state == APP_UNINSTALL_STATE_NONE);

    reset_fixture(&io, &record, 0x81u);
    CHECK(app_report_service_device_uninstall_prepare(&io) == AICAM_OK);
    bridge = mem_find_key("app_report_cleanup_intent");
    CHECK(bridge != NULL);
    if (bridge != NULL && bridge->len > 0u) {
        bridge->data[bridge->len - 1u] ^= 0x01u;
    }
    app_report_service_device_uninstall_finish(&io);
    CHECK(app_report_service_device_uninstall_prepare(&io) != AICAM_OK);
    CHECK(app_install_query_committed(&io, &removed, &(aicam_bool_t){0}) ==
          APP_INSTALL_OK);
    app_report_service_device_uninstall_finish(&io);
}

static void test_rotated_tbs_and_self_declared_old_query_cannot_purge_new_record(void)
{
    app_install_io_t io;
    app_install_record_t record_a;
    app_install_record_t record_b;
    app_install_record_t removed;
    app_install_record_t pending_record;
    app_report_session_t session_b;
    app_report_policy_identity_t identity_a;
    app_report_policy_status_t status;
    app_uninstall_cleanup_ops_t fail_ops = {NULL, purge_unknown_once};
    app_uninstall_result_t result;
    app_uninstall_state_t state;
    uint64_t cleanup_intent = 0u;

    reset_fixture(&io, &record_a, 0x91u);
    CHECK(app_report_service_device_uninstall_prepare(&io) == AICAM_OK);
    result = app_install_uninstall_confirmed(&io, AICAM_TRUE, &fail_ops,
                                             &removed, &state);
    CHECK(result == APP_UNINSTALL_CLEANUP_PENDING);
    app_report_service_device_uninstall_finish(&io);

    make_record(&record_b, 0xa1u);
    store_record(&io, &record_b);
    make_session(&record_b, &session_b);
    make_policy_identity(&record_a, &identity_a);
    app_report_service_set_committed_io(&io);
    CHECK(app_report_service_set_session(&session_b) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_query(&session_b, &identity_a, &status) !=
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_trust_uninstall_target(&identity_a) !=
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_set_session(NULL) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_device_uninstall_prepare(&io) != AICAM_OK);

    pending_record = read_committed_fixture_record();
    CHECK(memcmp(pending_record.tbs_sha256, record_b.tbs_sha256,
                 APP_INSTALL_SHA256_LEN) == 0);
    CHECK(mem_find_file(record_b.package_path) != NULL);
    CHECK(app_install_uninstall_status(&io, &state, &pending_record) ==
          APP_INSTALL_OK);
    CHECK(state == APP_UNINSTALL_STATE_CLEANUP_PENDING);
    CHECK(memcmp(pending_record.tbs_sha256, record_a.tbs_sha256,
                 APP_INSTALL_SHA256_LEN) == 0);
    make_policy_identity(&record_a, &identity_a);
    CHECK(app_report_policy_store_get_cleanup_intent(&identity_a,
                                                     &cleanup_intent) ==
          APP_REPORT_ABI_OK);
    CHECK(cleanup_intent == 0u);
    app_report_service_device_uninstall_finish(&io);
}

static void test_cleanup_intent_write_failure_keeps_committed_record(void)
{
    app_install_io_t io;
    app_install_record_t record;
    app_install_record_t current;
    aicam_bool_t present = AICAM_FALSE;

    reset_fixture(&io, &record, 0xb1u);
    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key),
             "%s", "app_report_cleanup_gen");
    CHECK(app_report_service_device_uninstall_prepare(&io) != AICAM_OK);
    app_report_service_device_uninstall_finish(&io);
    CHECK(app_install_query_committed(&io, &current, &present) == APP_INSTALL_OK);
    CHECK(present == AICAM_TRUE);
}

static void test_self_declared_old_tbs_cannot_bind_or_trust(void)
{
    app_install_io_t io;
    app_install_record_t old_record;
    app_install_record_t current_record;
    app_report_session_t session;
    app_report_policy_identity_t old_identity;
    app_report_policy_identity_t current_identity;
    app_report_policy_status_t status;

    reset_fixture(&io, &current_record, 0xc1u);
    make_record(&old_record, 0xb1u);
    make_session(&current_record, &session);
    make_policy_identity(&old_record, &old_identity);
    make_policy_identity(&current_record, &current_identity);
    CHECK(memcmp(old_record.publisher_key_sha256,
                 current_record.publisher_key_sha256,
                 APP_INSTALL_SHA256_LEN) == 0);
    CHECK(memcmp(old_record.tbs_sha256, current_record.tbs_sha256,
                 APP_INSTALL_SHA256_LEN) != 0);
    CHECK(app_report_service_set_session(&session) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_policy_query(&session, &old_identity, &status) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(app_report_service_policy_query(&session, &current_identity, &status) ==
          APP_REPORT_ABI_OK);
    CHECK(app_report_service_trust_uninstall_target(&old_identity) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    app_report_service_device_uninstall_finish(&io);
}

int main(void)
{
    test_real_report_purge_and_new_generation_on_reinstall();
    test_enqueue_before_query_is_revoked_and_purged_for_current_tbs();
    test_started_unknown_blocks_uninstall();
    test_policy_storage_unknown_and_partial_write_fail_closed();
    test_retry_and_corrupt_durable_intent();
    test_rotated_tbs_and_self_declared_old_query_cannot_purge_new_record();
    test_cleanup_intent_write_failure_keeps_committed_record();
    test_self_declared_old_tbs_cannot_bind_or_trust();
    printf("app_report_uninstall: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
