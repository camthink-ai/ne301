#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "app_report_service.h"
#include "app_report_service_device.h"

#include "app_installer.h"
#include "cmsis_os2.h"
#include "common_utils.h"
#include "stm32n6xx_hal.h"
#include "factory_test.h"
#include "netif_manager.h"
#include "mqtt_service.h"
#include "webhook_service.h"
#include "debug.h"
#include "storage.h"
#include "generic_math.h"

#ifndef APP_REPORT_DEV_PUMP_PERIOD_MS
#define APP_REPORT_DEV_PUMP_PERIOD_MS 500u
#endif
#define APP_REPORT_DEV_TASK_STACK (1024u * 16u)
#define APP_REPORT_DEV_TASK_PRIORITY 24u
#ifndef APP_REPORT_DEV_ACK_WAIT_MS
#define APP_REPORT_DEV_ACK_WAIT_MS 2000u
#endif
#ifndef APP_REPORT_DEV_ACK_POLL_MS
#define APP_REPORT_DEV_ACK_POLL_MS 250u
#endif
#ifndef APP_REPORT_DEV_STOP_JOIN_MS
#define APP_REPORT_DEV_STOP_JOIN_MS 5000u
#endif
#ifndef APP_REPORT_DEV_STOP_POLL_MS
#define APP_REPORT_DEV_STOP_POLL_MS 10u
#endif
#ifndef APP_REPORT_DEV_TRANSPORT_MAX_MS
#define APP_REPORT_DEV_TRANSPORT_MAX_MS 15000u
#endif
#define APP_REPORT_DEV_JOIN_FLOOR_MS \
    (APP_REPORT_DEV_ACK_WAIT_MS + APP_REPORT_DEV_TRANSPORT_MAX_MS + \
     APP_REPORT_DEV_PUMP_PERIOD_MS + (2u * APP_REPORT_DEV_STOP_POLL_MS) + \
     200u)
#define APP_REPORT_DEV_MAC_STR_LEN 17u

#define DEV_LIFE_STOPPED 0u
#define DEV_LIFE_RUNNING 1u
#define DEV_LIFE_STOPPING 2u
#define DEV_LIFE_QUIESCE_PENDING 3u

static osMutexId_t g_report_lock;
static osMutexId_t g_life_lock;
static volatile uint32_t g_life_state = DEV_LIFE_STOPPED;
static osThreadId_t g_pump_thread;
static volatile aicam_bool_t g_running;
static volatile aicam_bool_t g_pump_exit_done;
static aicam_bool_t g_dev_initialized;
static app_install_io_t g_cleanup_view_io;
static app_install_record_t g_cleanup_view_record;
static const app_install_io_t *g_cleanup_view_base;
static uint64_t g_cleanup_generation;
static aicam_bool_t g_cleanup_context_valid;

#define APP_REPORT_DEVICE_CLEANUP_KEY "app_report_cleanup_intent"
#define APP_REPORT_DEVICE_CLEANUP_COUNTER_KEY "app_report_cleanup_gen"
#define APP_REPORT_DEVICE_CLEANUP_MAGIC 0x31435241u
#define APP_REPORT_DEVICE_CLEANUP_VERSION 1u
#define APP_REPORT_DEVICE_CLEANUP_PREPARED 1u
#define APP_REPORT_DEVICE_CLEANUP_COMPLETE 2u
#define APP_REPORT_DEVICE_CLEANUP_STATE_OFF 8u
#define APP_REPORT_DEVICE_CLEANUP_GENERATION_OFF 12u
#define APP_REPORT_DEVICE_CLEANUP_RECORD_OFF 20u
#define APP_REPORT_DEVICE_CLEANUP_CRC_OFF 316u
#define APP_REPORT_DEVICE_CLEANUP_BLOB_SIZE 320u
#define APP_REPORT_DEVICE_COUNTER_MAGIC 0x31475241u
#define APP_REPORT_DEVICE_COUNTER_VERSION 1u
#define APP_REPORT_DEVICE_COUNTER_CRC_OFF 16u
#define APP_REPORT_DEVICE_COUNTER_SIZE 20u

typedef struct {
    uint8_t state;
    uint64_t generation;
    app_install_record_t record;
} app_report_device_cleanup_t;
static aicam_bool_t g_dev_boot_valid;
static uint32_t g_dev_boot_id;
static const osMutexAttr_t g_report_lock_attr = {
    "app_report_lock", osMutexRecursive, NULL, 0u
};
static uint8_t g_pump_thread_stack[APP_REPORT_DEV_TASK_STACK] ALIGN_32 IN_PSRAM;
static const osThreadAttr_t g_pump_thread_attr = {
    .name = "app_report_pump",
    .stack_size = APP_REPORT_DEV_TASK_STACK,
    .stack_mem = g_pump_thread_stack,
    .priority = APP_REPORT_DEV_TASK_PRIORITY,
};

static int32_t dev_policy_storage_read(void *user, void *out,
                                       uint32_t cap, uint32_t *out_len)
{
    int rc;

    (void)user;
    if (out == NULL || out_len == NULL) {
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    *out_len = 0u;
    rc = storage_nvs_read(NVS_USER, "app_report_policy", out, cap);
    if (rc == -ENOENT) {
        return APP_REPORT_POLICY_STORAGE_NOT_FOUND;
    }
    if (rc < 0 || (uint32_t)rc > cap) {
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    *out_len = (uint32_t)rc;
    return APP_REPORT_POLICY_STORAGE_OK;
}

static int32_t dev_policy_storage_write(void *user, const void *data,
                                        uint32_t len)
{
    int rc;

    (void)user;
    if (data == NULL || len == 0u) {
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    rc = storage_nvs_write(NVS_USER, "app_report_policy", data, len);
    return (rc >= 0 && (uint32_t)rc == len) ?
           APP_REPORT_POLICY_STORAGE_OK : APP_REPORT_POLICY_STORAGE_ERROR;
}

static const app_report_policy_storage_t g_dev_policy_storage = {
    NULL,
    dev_policy_storage_read,
    dev_policy_storage_write
};

static void report_lock_enter(void)
{
    if (g_report_lock != NULL) {
        (void)osMutexAcquire(g_report_lock, osWaitForever);
    }
}

static void report_lock_exit(void)
{
    if (g_report_lock != NULL) {
        (void)osMutexRelease(g_report_lock);
    }
}

static aicam_bool_t dev_locks_ensure(void)
{
    if (g_life_lock == NULL) {
        g_life_lock = osMutexNew(&g_report_lock_attr);
    }
    if (g_life_lock == NULL) {
        return AICAM_FALSE;
    }
    if (g_report_lock == NULL) {
        g_report_lock = osMutexNew(&g_report_lock_attr);
        if (g_report_lock == NULL) {
            return AICAM_FALSE;
        }
    }
    return AICAM_TRUE;
}

static aicam_bool_t dev_locks_ready(void)
{
    return (g_life_lock != NULL && g_report_lock != NULL) ? AICAM_TRUE
                                                        : AICAM_FALSE;
}

static aicam_bool_t dev_identity_device_id(void *user, char *out, uint32_t cap)
{
    uint8_t mac[6];
    netif_info_t netif;

    (void)user;
    if (out == NULL || cap < (APP_REPORT_DEV_MAC_STR_LEN + 1u)) {
        return AICAM_FALSE;
    }
    if (factory_mac_get_burned(mac) == 0) {
        (void)snprintf(out, cap, NETIF_MAC_STR_FMT,
                       NETIF_MAC_PARAMETER(mac));
        return AICAM_TRUE;
    }
    if (nm_get_netif_info(NETIF_NAME_WIFI_AP, &netif) == AICAM_OK) {
        (void)snprintf(out, cap, NETIF_MAC_STR_FMT,
                       NETIF_MAC_PARAMETER(netif.if_mac));
        return AICAM_TRUE;
    }
    return AICAM_FALSE;
}

static uint32_t dev_identity_boot_id(void *user)
{
    uint32_t v;

    (void)user;
    if (g_dev_boot_valid != AICAM_TRUE) {
        v = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
        v ^= (uint32_t)osKernelGetTickCount();
        if (v == 0u) {
            v = 1u;
        }
        g_dev_boot_id = v;
        g_dev_boot_valid = AICAM_TRUE;
    }
    return g_dev_boot_id;
}

static const app_report_identity_t g_dev_identity = {
    NULL,
    dev_identity_device_id,
    dev_identity_boot_id
};

static uint32_t dev_mqtt_ready(void *user)
{
    mqtt_service_topic_config_t tc;

    (void)user;
    if (mqtt_service_get_topic_config(&tc) != AICAM_OK) {
        return APP_REPORT_CHANNEL_WAITING;
    }
    if (tc.data_report_topic[0] == '\0') {
        return APP_REPORT_CHANNEL_UNCONFIGURED;
    }
    if (mqtt_service_is_running() != AICAM_TRUE) {
        return APP_REPORT_CHANNEL_WAITING;
    }
    return APP_REPORT_CHANNEL_READY;
}

static int dev_mqtt_publish(void *user, const uint8_t *json, uint32_t len)
{
    mqtt_service_topic_config_t tc;
    int msg_id;
    int qos;
    uint32_t start;
    aicam_bool_t acked = AICAM_FALSE;

    (void)user;
    if (app_report_service_transport_entered(APP_REPORT_CHANNEL_MQTT) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (mqtt_service_get_topic_config(&tc) != AICAM_OK ||
        tc.data_report_topic[0] == '\0') {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    qos = tc.data_report_qos;
    if (qos < 0) {
        qos = 0;
    }
    if (qos > 2) {
        qos = 2;
    }
    if (app_report_service_transport_start(APP_REPORT_CHANNEL_MQTT) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (app_report_service_transport_begin(APP_REPORT_CHANNEL_MQTT) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    msg_id = mqtt_service_publish(tc.data_report_topic, json, (int)len, qos, 0);
    if (msg_id < 0) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (qos == 0) {
        return APP_REPORT_TRANSPORT_OK;
    }
    start = (uint32_t)osKernelGetTickCount();
    while (((uint32_t)osKernelGetTickCount() - start) <
           APP_REPORT_DEV_ACK_WAIT_MS) {
        int acked_id = 0;
        aicam_result_t rc =
            mqtt_service_get_acked_msg_id(&acked_id, APP_REPORT_DEV_ACK_POLL_MS);
        if (rc == AICAM_OK && acked_id == msg_id) {
            acked = AICAM_TRUE;
            break;
        }
    }
    return (acked == AICAM_TRUE) ? APP_REPORT_TRANSPORT_OK
                                 : APP_REPORT_TRANSPORT_UNKNOWN;
}

static uint32_t dev_webhook_ready(void *user)
{
    webhook_config_t cfg;

    (void)user;
    if (json_config_get_webhook_config(&cfg) != AICAM_OK) {
        return APP_REPORT_CHANNEL_WAITING;
    }
    if (cfg.enable != AICAM_TRUE || cfg.url[0] == '\0') {
        return APP_REPORT_CHANNEL_UNCONFIGURED;
    }
    return APP_REPORT_CHANNEL_READY;
}

static int dev_webhook_post(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
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
    if (webhook_service_push_json(json, len) != AICAM_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    return APP_REPORT_TRANSPORT_OK;
}

static const app_report_transport_t g_dev_transport = {
    NULL,
    dev_mqtt_ready,
    dev_mqtt_publish,
    dev_webhook_ready,
    dev_webhook_post
};

static void app_report_pump_task(void *arg)
{
    (void)arg;
    while (g_running == AICAM_TRUE) {
        app_report_service_pump();
        if (g_running != AICAM_TRUE) {
            break;
        }
        (void)osDelay(APP_REPORT_DEV_PUMP_PERIOD_MS);
    }
    g_pump_exit_done = AICAM_TRUE;
    for (;;) {
        (void)osDelay(1000u);
    }
}

static uint32_t dev_join_window_ms(void)
{
    if (APP_REPORT_DEV_STOP_JOIN_MS < APP_REPORT_DEV_JOIN_FLOOR_MS) {
        return APP_REPORT_DEV_JOIN_FLOOR_MS;
    }
    return APP_REPORT_DEV_STOP_JOIN_MS;
}

static aicam_bool_t dev_quiesce_wait(void)
{
    uint32_t start = (uint32_t)osKernelGetTickCount();
    uint32_t window = dev_join_window_ms();

    while (((uint32_t)osKernelGetTickCount() - start) < window) {
        if (g_pump_exit_done == AICAM_TRUE) {
            return AICAM_TRUE;
        }
        (void)osDelay(APP_REPORT_DEV_STOP_POLL_MS);
    }
    return (g_pump_exit_done == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE;
}

static void dev_finish_stop(void)
{
    if (g_pump_thread != NULL) {
        (void)osThreadTerminate(g_pump_thread);
    }
    g_pump_thread = NULL;
    app_report_service_restart();
    g_life_state = DEV_LIFE_STOPPED;
}

static aicam_result_t dev_pump_launch(void)
{
    app_report_service_set_policy_storage(&g_dev_policy_storage);
    app_report_service_set_committed_io(app_installer_device_io());
    if (g_dev_initialized != AICAM_TRUE) {
        app_report_service_init(&g_dev_identity, &g_dev_transport);
        g_dev_initialized = AICAM_TRUE;
    }
    app_report_service_set_lock(report_lock_enter, report_lock_exit);
    g_pump_exit_done = AICAM_FALSE;
    g_running = AICAM_TRUE;
    g_pump_thread = osThreadNew(app_report_pump_task, NULL, &g_pump_thread_attr);
    if (g_pump_thread == NULL) {
        g_running = AICAM_FALSE;
        g_pump_exit_done = AICAM_TRUE;
        LOG_SVC_ERROR("appreport: pump task create failed");
        return AICAM_ERROR;
    }
    LOG_SVC_INFO("appreport: started (boot=%u)", (unsigned int)app_report_service_boot_id());
    return AICAM_OK;
}

static void dev_put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
    p[3] = (uint8_t)((value >> 24) & 0xffu);
}

static uint32_t dev_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void dev_put_u64(uint8_t *p, uint64_t value)
{
    dev_put_u32(p, (uint32_t)value);
    dev_put_u32(p + 4u, (uint32_t)(value >> 32));
}

static uint64_t dev_get_u64(const uint8_t *p)
{
    return (uint64_t)dev_get_u32(p) | ((uint64_t)dev_get_u32(p + 4u) << 32);
}

static aicam_bool_t dev_record_equal(const app_install_record_t *left,
                                     const app_install_record_t *right)
{
    uint8_t left_blob[APP_INSTALL_RECORD_BIN_SIZE];
    uint8_t right_blob[APP_INSTALL_RECORD_BIN_SIZE];

    if (left == NULL || right == NULL ||
        app_install_record_encode(left, left_blob, sizeof(left_blob)) != AICAM_TRUE ||
        app_install_record_encode(right, right_blob, sizeof(right_blob)) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    return (memcmp(left_blob, right_blob, sizeof(left_blob)) == 0) ?
           AICAM_TRUE : AICAM_FALSE;
}

static void dev_policy_identity_from_record(
    const app_install_record_t *record,
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

static void dev_report_session_from_record(
    const app_install_record_t *record,
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

static aicam_bool_t dev_cleanup_encode(
    const app_report_device_cleanup_t *context,
    uint8_t out[APP_REPORT_DEVICE_CLEANUP_BLOB_SIZE])
{
    if (context == NULL || out == NULL || context->generation == 0u ||
        context->generation >= APP_REPORT_POLICY_PURGE_INTENT_ADMIN ||
        (context->state != APP_REPORT_DEVICE_CLEANUP_PREPARED &&
         context->state != APP_REPORT_DEVICE_CLEANUP_COMPLETE)) {
        return AICAM_FALSE;
    }
    memset(out, 0, APP_REPORT_DEVICE_CLEANUP_BLOB_SIZE);
    dev_put_u32(out, APP_REPORT_DEVICE_CLEANUP_MAGIC);
    dev_put_u32(out + 4u, APP_REPORT_DEVICE_CLEANUP_VERSION);
    out[APP_REPORT_DEVICE_CLEANUP_STATE_OFF] = context->state;
    dev_put_u64(out + APP_REPORT_DEVICE_CLEANUP_GENERATION_OFF,
                context->generation);
    if (app_install_record_encode(&context->record,
                                  out + APP_REPORT_DEVICE_CLEANUP_RECORD_OFF,
                                  APP_INSTALL_RECORD_BIN_SIZE) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    dev_put_u32(out + APP_REPORT_DEVICE_CLEANUP_CRC_OFF,
                 generic_crc32(out, APP_REPORT_DEVICE_CLEANUP_CRC_OFF));
    return AICAM_TRUE;
}

static aicam_bool_t dev_cleanup_decode(
    const uint8_t data[APP_REPORT_DEVICE_CLEANUP_BLOB_SIZE],
    app_report_device_cleanup_t *out)
{
    size_t i;

    if (data == NULL || out == NULL ||
        dev_get_u32(data) != APP_REPORT_DEVICE_CLEANUP_MAGIC ||
        dev_get_u32(data + 4u) != APP_REPORT_DEVICE_CLEANUP_VERSION ||
        (data[APP_REPORT_DEVICE_CLEANUP_STATE_OFF] !=
             APP_REPORT_DEVICE_CLEANUP_PREPARED &&
         data[APP_REPORT_DEVICE_CLEANUP_STATE_OFF] !=
             APP_REPORT_DEVICE_CLEANUP_COMPLETE) ||
        dev_get_u32(data + APP_REPORT_DEVICE_CLEANUP_CRC_OFF) !=
            generic_crc32(data, APP_REPORT_DEVICE_CLEANUP_CRC_OFF)) {
        return AICAM_FALSE;
    }
    for (i = 9u; i < APP_REPORT_DEVICE_CLEANUP_GENERATION_OFF; i++) {
        if (data[i] != 0u) {
            return AICAM_FALSE;
        }
    }
    memset(out, 0, sizeof(*out));
    out->state = data[APP_REPORT_DEVICE_CLEANUP_STATE_OFF];
    out->generation = dev_get_u64(data + APP_REPORT_DEVICE_CLEANUP_GENERATION_OFF);
    if (out->generation == 0u ||
        out->generation >= APP_REPORT_POLICY_PURGE_INTENT_ADMIN ||
        app_install_record_decode(data + APP_REPORT_DEVICE_CLEANUP_RECORD_OFF,
                                  APP_INSTALL_RECORD_BIN_SIZE,
                                  &out->record) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static int dev_cleanup_load(const app_install_io_t *io,
                            app_report_device_cleanup_t *out)
{
    uint8_t data[APP_REPORT_DEVICE_CLEANUP_BLOB_SIZE];
    size_t len = 0u;
    app_install_io_result_t rc;

    rc = io->kv_read(io->ctx, APP_REPORT_DEVICE_CLEANUP_KEY,
                     data, sizeof(data), &len);
    if (rc == APP_INSTALL_IO_NOT_FOUND) {
        return 0;
    }
    if (rc != APP_INSTALL_IO_OK || len != sizeof(data) ||
        dev_cleanup_decode(data, out) != AICAM_TRUE) {
        return -1;
    }
    return 1;
}

static aicam_bool_t dev_cleanup_store(const app_install_io_t *io,
                                      const app_report_device_cleanup_t *context)
{
    uint8_t data[APP_REPORT_DEVICE_CLEANUP_BLOB_SIZE];
    uint8_t verify[APP_REPORT_DEVICE_CLEANUP_BLOB_SIZE];
    size_t len = 0u;

    if (dev_cleanup_encode(context, data) != AICAM_TRUE ||
        io->kv_write(io->ctx, APP_REPORT_DEVICE_CLEANUP_KEY,
                     data, sizeof(data)) != APP_INSTALL_IO_OK ||
        io->kv_read(io->ctx, APP_REPORT_DEVICE_CLEANUP_KEY,
                    verify, sizeof(verify), &len) != APP_INSTALL_IO_OK ||
        len != sizeof(verify) || memcmp(data, verify, sizeof(data)) != 0) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static aicam_bool_t dev_counter_decode(const uint8_t data[APP_REPORT_DEVICE_COUNTER_SIZE],
                                       uint64_t *generation)
{
    if (data == NULL || generation == NULL ||
        dev_get_u32(data) != APP_REPORT_DEVICE_COUNTER_MAGIC ||
        dev_get_u32(data + 4u) != APP_REPORT_DEVICE_COUNTER_VERSION ||
        dev_get_u32(data + APP_REPORT_DEVICE_COUNTER_CRC_OFF) !=
            generic_crc32(data, APP_REPORT_DEVICE_COUNTER_CRC_OFF)) {
        return AICAM_FALSE;
    }
    *generation = dev_get_u64(data + 8u);
    return (*generation < APP_REPORT_POLICY_PURGE_INTENT_ADMIN) ?
           AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t dev_counter_store(const app_install_io_t *io,
                                      uint64_t generation)
{
    uint8_t data[APP_REPORT_DEVICE_COUNTER_SIZE];
    uint8_t verify[APP_REPORT_DEVICE_COUNTER_SIZE];
    size_t len = 0u;

    if (generation >= APP_REPORT_POLICY_PURGE_INTENT_ADMIN) {
        return AICAM_FALSE;
    }
    memset(data, 0, sizeof(data));
    dev_put_u32(data, APP_REPORT_DEVICE_COUNTER_MAGIC);
    dev_put_u32(data + 4u, APP_REPORT_DEVICE_COUNTER_VERSION);
    dev_put_u64(data + 8u, generation);
    dev_put_u32(data + APP_REPORT_DEVICE_COUNTER_CRC_OFF,
                generic_crc32(data, APP_REPORT_DEVICE_COUNTER_CRC_OFF));
    if (io->kv_write(io->ctx, APP_REPORT_DEVICE_CLEANUP_COUNTER_KEY,
                     data, sizeof(data)) != APP_INSTALL_IO_OK ||
        io->kv_read(io->ctx, APP_REPORT_DEVICE_CLEANUP_COUNTER_KEY,
                    verify, sizeof(verify), &len) != APP_INSTALL_IO_OK ||
        len != sizeof(verify) || memcmp(data, verify, sizeof(data)) != 0) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static aicam_bool_t dev_cleanup_create(const app_install_io_t *io,
                                      const app_install_record_t *record,
                                      app_report_device_cleanup_t *out)
{
    uint8_t data[APP_REPORT_DEVICE_COUNTER_SIZE];
    uint64_t generation = 0u;
    size_t len = 0u;
    app_install_io_result_t rc;

    if (io->tx_begin(io->ctx) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    rc = io->kv_read(io->ctx, APP_REPORT_DEVICE_CLEANUP_COUNTER_KEY,
                     data, sizeof(data), &len);
    if (rc == APP_INSTALL_IO_OK) {
        if (len != sizeof(data) || dev_counter_decode(data, &generation) != AICAM_TRUE) {
            io->tx_end(io->ctx);
            return AICAM_FALSE;
        }
    } else if (rc != APP_INSTALL_IO_NOT_FOUND) {
        io->tx_end(io->ctx);
        return AICAM_FALSE;
    }
    if (generation >= APP_REPORT_POLICY_PURGE_INTENT_ADMIN - 1u) {
        io->tx_end(io->ctx);
        return AICAM_FALSE;
    }
    generation++;
    if (dev_counter_store(io, generation) != AICAM_TRUE) {
        io->tx_end(io->ctx);
        return AICAM_FALSE;
    }
    memset(out, 0, sizeof(*out));
    out->state = APP_REPORT_DEVICE_CLEANUP_PREPARED;
    out->generation = generation;
    memcpy(&out->record, record, sizeof(*record));
    if (dev_cleanup_store(io, out) != AICAM_TRUE) {
        io->tx_end(io->ctx);
        return AICAM_FALSE;
    }
    io->tx_end(io->ctx);
    return AICAM_TRUE;
}

static app_install_io_result_t dev_cleanup_view_kv_read(
    void *ctx, const char *key, uint8_t *buf, size_t cap, size_t *len)
{
    app_install_io_t *base = (app_install_io_t *)ctx;
    app_install_io_result_t rc;

    if (base == NULL || key == NULL || buf == NULL || len == NULL) {
        return APP_INSTALL_IO_ERROR;
    }
    if (strcmp(key, APP_INSTALL_KEY_UNINSTALL) == 0 ||
        strcmp(key, APP_INSTALL_KEY_PENDING) == 0) {
        *len = 0u;
        return APP_INSTALL_IO_NOT_FOUND;
    }
    if (strcmp(key, APP_INSTALL_KEY_RECORD) == 0) {
        rc = base->kv_read(base->ctx, key, buf, cap, len);
        if (rc != APP_INSTALL_IO_NOT_FOUND) {
            return rc;
        }
        if (cap < APP_INSTALL_RECORD_BIN_SIZE ||
            app_install_record_encode(&g_cleanup_view_record, buf, cap) != AICAM_TRUE) {
            return APP_INSTALL_IO_ERROR;
        }
        *len = APP_INSTALL_RECORD_BIN_SIZE;
        return APP_INSTALL_IO_OK;
    }
    return base->kv_read(base->ctx, key, buf, cap, len);
}

static app_install_io_result_t dev_cleanup_view_kv_write(
    void *ctx, const char *key, const uint8_t *data, size_t len)
{
    app_install_io_t *base = (app_install_io_t *)ctx;

    return (base != NULL && base->kv_write != NULL) ?
           base->kv_write(base->ctx, key, data, len) : APP_INSTALL_IO_ERROR;
}

static app_install_io_result_t dev_cleanup_view_kv_delete(void *ctx,
                                                           const char *key)
{
    app_install_io_t *base = (app_install_io_t *)ctx;

    return (base != NULL && base->kv_delete != NULL) ?
           base->kv_delete(base->ctx, key) : APP_INSTALL_IO_ERROR;
}

static aicam_bool_t dev_cleanup_view_tx_begin(void *ctx)
{
    app_install_io_t *base = (app_install_io_t *)ctx;

    return (base != NULL && base->tx_begin != NULL) ?
           base->tx_begin(base->ctx) : AICAM_FALSE;
}

static void dev_cleanup_view_tx_end(void *ctx)
{
    app_install_io_t *base = (app_install_io_t *)ctx;

    if (base != NULL && base->tx_end != NULL) {
        base->tx_end(base->ctx);
    }
}

static void dev_cleanup_view_set(const app_install_io_t *io,
                                 const app_install_record_t *record)
{
    memset(&g_cleanup_view_io, 0, sizeof(g_cleanup_view_io));
    g_cleanup_view_base = io;
    g_cleanup_view_record = *record;
    g_cleanup_view_io.ctx = (void *)io;
    g_cleanup_view_io.kv_write = dev_cleanup_view_kv_write;
    g_cleanup_view_io.kv_read = dev_cleanup_view_kv_read;
    g_cleanup_view_io.kv_delete = dev_cleanup_view_kv_delete;
    g_cleanup_view_io.tx_begin = dev_cleanup_view_tx_begin;
    g_cleanup_view_io.tx_end = dev_cleanup_view_tx_end;
}

static aicam_bool_t dev_cleanup_view_matches(
    const app_install_io_t *io,
    const app_install_record_t *record)
{
    app_install_record_t current;
    aicam_bool_t present = AICAM_FALSE;

    dev_cleanup_view_set(io, record);
    if (app_install_query_committed(&g_cleanup_view_io, &current, &present) !=
            APP_INSTALL_OK || present != AICAM_TRUE ||
        dev_record_equal(&current, record) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static int dev_uninstall_source(const app_install_io_t *io,
                                app_install_record_t *record,
                                app_uninstall_state_t *state)
{
    aicam_bool_t present = AICAM_FALSE;

    if (app_install_uninstall_status(io, state, record) != APP_INSTALL_OK ||
        *state == APP_UNINSTALL_STATE_UNKNOWN) {
        return -1;
    }
    if (*state == APP_UNINSTALL_STATE_CLEANUP_PENDING) {
        return 1;
    }
    if (app_install_query_committed(io, record, &present) != APP_INSTALL_OK ||
        present != AICAM_TRUE) {
        return -1;
    }
    return 0;
}

static aicam_bool_t dev_report_has_unknown(void)
{
    app_report_entry_info_t entry;
    uint32_t mqtt_state;
    uint32_t webhook_state;
    uint32_t i;

    for (i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        if (app_report_service_entry(i, &entry) != AICAM_TRUE) {
            continue;
        }
        {
            int32_t rc = app_report_status(entry.host_boot_id, entry.report_seq,
                                           &mqtt_state, &webhook_state);
            if (rc == APP_REPORT_ABI_NOT_FOUND) {
                continue;
            }
            if (rc != APP_REPORT_ABI_OK ||
                mqtt_state != entry.mqtt_state ||
                webhook_state != entry.webhook_state) {
                return AICAM_TRUE;
            }
        }
        if (entry.mqtt_state == APP_REPORT_MQTT_UNKNOWN ||
            entry.mqtt_state == APP_REPORT_MQTT_POLICY_UNKNOWN ||
            entry.webhook_state == APP_REPORT_WEBHOOK_UNKNOWN ||
            entry.webhook_state == APP_REPORT_WEBHOOK_POLICY_UNKNOWN) {
            return AICAM_TRUE;
        }
    }
    return AICAM_FALSE;
}

static aicam_bool_t dev_cleanup_service_ready(const app_install_io_t *io)
{
    if (io == NULL || io->kv_read == NULL || io->kv_write == NULL ||
        io->kv_delete == NULL || io->tx_begin == NULL || io->tx_end == NULL ||
        dev_locks_ensure() != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    if (g_dev_initialized != AICAM_TRUE) {
        app_report_service_init(&g_dev_identity, &g_dev_transport);
        g_dev_initialized = AICAM_TRUE;
    }
    app_report_service_set_lock(report_lock_enter, report_lock_exit);
    app_report_service_set_policy_storage(&g_dev_policy_storage);
    app_report_service_set_committed_io(io);
    return AICAM_TRUE;
}

static aicam_bool_t dev_cleanup_status_complete(
    const app_report_device_cleanup_t *context)
{
    app_report_policy_identity_t identity;
    app_report_policy_status_t status;
    int32_t rc;

    dev_policy_identity_from_record(&context->record, &identity);
    rc = app_report_service_policy_purge_status(
        &identity,
        APP_REPORT_POLICY_PURGE_INTENT_ADMIN | context->generation,
        &status);
    return (rc == APP_REPORT_ABI_OK &&
            status.purge_state == APP_REPORT_POLICY_PURGE_COMPLETE &&
            status.cleanup_intent ==
                (APP_REPORT_POLICY_PURGE_INTENT_ADMIN | context->generation) &&
            status.last_result == APP_REPORT_ABI_OK &&
            status.storage_verified == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t dev_report_query(
    const app_report_device_cleanup_t *context,
    app_report_session_t *session,
    app_report_policy_identity_t *identity,
    app_report_policy_status_t *status)
{
    int32_t rc;

    dev_report_session_from_record(&context->record, session);
    dev_policy_identity_from_record(&context->record, identity);
    app_report_service_set_committed_io(&g_cleanup_view_io);
    rc = app_report_service_policy_query(session, identity, status);
    if (rc == APP_REPORT_ABI_UNAUTHORIZED) {
        if (app_report_service_set_session(session) != APP_REPORT_ABI_OK) {
            return AICAM_FALSE;
        }
        rc = app_report_service_policy_query(session, identity, status);
    }
    return (rc == APP_REPORT_ABI_OK && status->storage_verified == AICAM_TRUE) ?
           AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t dev_report_status_consistent(void)
{
    app_report_entry_info_t entry;
    uint32_t mqtt_state;
    uint32_t webhook_state;

    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        if (app_report_service_entry(i, &entry) != AICAM_TRUE) {
            continue;
        }
        {
            int32_t rc = app_report_status(entry.host_boot_id, entry.report_seq,
                                           &mqtt_state, &webhook_state);
            if (rc == APP_REPORT_ABI_NOT_FOUND) {
                continue;
            }
            if (rc != APP_REPORT_ABI_OK || mqtt_state != entry.mqtt_state ||
                webhook_state != entry.webhook_state ||
            mqtt_state == APP_REPORT_MQTT_UNKNOWN ||
            mqtt_state == APP_REPORT_MQTT_POLICY_UNKNOWN ||
            webhook_state == APP_REPORT_WEBHOOK_UNKNOWN ||
            webhook_state == APP_REPORT_WEBHOOK_POLICY_UNKNOWN) {
                return AICAM_FALSE;
            }
        }
    }
    return AICAM_TRUE;
}

static aicam_bool_t dev_report_revoke(
    const app_report_device_cleanup_t *context)
{
    app_report_session_t session;
    app_report_policy_identity_t identity;
    app_report_policy_values_t values;
    app_report_policy_status_t status;

    if (dev_report_query(context, &session, &identity, &status) != AICAM_TRUE ||
        dev_report_has_unknown() == AICAM_TRUE ||
        app_report_service_trust_uninstall_target(&identity) != APP_REPORT_ABI_OK) {
        return AICAM_FALSE;
    }
    values.mqtt_enabled = AICAM_FALSE;
    values.webhook_enabled = AICAM_FALSE;
    values.backlog_capacity = status.backlog_capacity;
    if (app_report_service_policy_commit(&session, &identity, status.revision,
                                         &values, &status) != APP_REPORT_ABI_OK ||
        status.storage_verified != AICAM_TRUE ||
        status.mqtt_enabled != AICAM_FALSE ||
        status.webhook_enabled != AICAM_FALSE) {
        return AICAM_FALSE;
    }
    return (dev_report_query(context, &session, &identity, &status) == AICAM_TRUE &&
            status.mqtt_enabled == AICAM_FALSE &&
            status.webhook_enabled == AICAM_FALSE &&
            dev_report_status_consistent() == AICAM_TRUE) ?
           AICAM_TRUE : AICAM_FALSE;
}

static app_uninstall_purge_result_t dev_report_purge_transaction(
    void *ctx, const app_install_record_t *record)
{
    app_report_device_cleanup_t context;
    app_install_record_t intent_record;
    app_uninstall_state_t state;
    app_report_session_t session;
    app_report_policy_identity_t identity;
    app_report_policy_status_t status;
    int context_state;

    (void)ctx;
    if (record == NULL || g_cleanup_view_base == NULL ||
        app_install_uninstall_status(g_cleanup_view_base, &state,
                                     &intent_record) != APP_INSTALL_OK ||
        state != APP_UNINSTALL_STATE_CLEANUP_PENDING ||
        dev_record_equal(record, &intent_record) != AICAM_TRUE) {
        return APP_UNINSTALL_PURGE_UNKNOWN;
    }
    context_state = dev_cleanup_load(g_cleanup_view_base, &context);
    if (context_state != 1 || g_cleanup_context_valid != AICAM_TRUE) {
        return APP_UNINSTALL_PURGE_UNKNOWN;
    }
    if (dev_record_equal(&context.record, record) != AICAM_TRUE ||
        g_cleanup_context_valid != AICAM_TRUE ||
        g_cleanup_generation != context.generation ||
        dev_cleanup_view_matches(g_cleanup_view_base, &context.record) != AICAM_TRUE) {
        return APP_UNINSTALL_PURGE_UNKNOWN;
    }
    if (dev_cleanup_service_ready(g_cleanup_view_base) != AICAM_TRUE) {
        return APP_UNINSTALL_PURGE_UNKNOWN;
    }
    dev_cleanup_view_set(g_cleanup_view_base, &context.record);
    app_report_service_set_committed_io(&g_cleanup_view_io);
    if (dev_report_query(&context, &session, &identity, &status) != AICAM_TRUE ||
        dev_report_has_unknown() == AICAM_TRUE ||
        app_report_service_trust_uninstall_target(&identity) != APP_REPORT_ABI_OK) {
        return APP_UNINSTALL_PURGE_UNKNOWN;
    }
    if (app_report_service_policy_purge(
            &identity,
            APP_REPORT_POLICY_PURGE_INTENT_ADMIN | context.generation,
            &status) != APP_REPORT_ABI_OK ||
        status.purge_state != APP_REPORT_POLICY_PURGE_COMPLETE ||
        status.cleanup_intent !=
            (APP_REPORT_POLICY_PURGE_INTENT_ADMIN | context.generation) ||
        status.last_result != APP_REPORT_ABI_OK ||
        status.storage_verified != AICAM_TRUE ||
        app_report_service_policy_purge_status(
            &identity,
            APP_REPORT_POLICY_PURGE_INTENT_ADMIN | context.generation,
            &status) != APP_REPORT_ABI_OK ||
        status.purge_state != APP_REPORT_POLICY_PURGE_COMPLETE ||
        status.cleanup_intent !=
            (APP_REPORT_POLICY_PURGE_INTENT_ADMIN | context.generation) ||
        status.last_result != APP_REPORT_ABI_OK ||
        status.storage_verified != AICAM_TRUE) {
        return APP_UNINSTALL_PURGE_UNKNOWN;
    }
    context.state = APP_REPORT_DEVICE_CLEANUP_COMPLETE;
    if (dev_cleanup_store(g_cleanup_view_base, &context) != AICAM_TRUE) {
        return APP_UNINSTALL_PURGE_UNKNOWN;
    }
    g_cleanup_generation = context.generation;
    g_cleanup_context_valid = AICAM_TRUE;
    return APP_UNINSTALL_PURGE_CONFIRMED;
}

static app_uninstall_purge_result_t dev_report_purge(
    void *ctx, const app_install_record_t *record)
{
    app_uninstall_purge_result_t result;

    if (dev_locks_ensure() != AICAM_TRUE ||
        osMutexAcquire(g_report_lock, osWaitForever) != osOK) {
        return APP_UNINSTALL_PURGE_UNKNOWN;
    }
    result = dev_report_purge_transaction(ctx, record);
    if (g_cleanup_context_valid == AICAM_TRUE) {
        (void)app_report_service_set_session(NULL);
        if (g_cleanup_view_base != NULL) {
            app_report_service_set_committed_io(g_cleanup_view_base);
        }
    }
    (void)osMutexRelease(g_report_lock);
    return result;
}

const app_uninstall_cleanup_ops_t *app_report_service_device_cleanup_ops(void)
{
    static const app_uninstall_cleanup_ops_t ops = {
        NULL,
        dev_report_purge
    };
    return &ops;
}

aicam_result_t app_report_service_device_uninstall_prepare(
    const app_install_io_t *io)
{
    app_install_record_t record;
    app_uninstall_state_t state;
    app_report_device_cleanup_t context;
    int source_state;
    int context_state;
    aicam_bool_t valid;

    if (io == NULL || io->tx_begin == NULL || io->tx_end == NULL ||
        io->kv_read == NULL || io->kv_write == NULL || io->kv_delete == NULL ||
        app_install_app_running() == AICAM_TRUE ||
        dev_locks_ensure() != AICAM_TRUE) {
        return AICAM_ERROR;
    }
    if (osMutexAcquire(g_report_lock, osWaitForever) != osOK) {
        return AICAM_ERROR_BUSY;
    }
    if (dev_cleanup_service_ready(io) != AICAM_TRUE) {
        (void)osMutexRelease(g_report_lock);
        return AICAM_ERROR;
    }
    g_cleanup_context_valid = AICAM_FALSE;
    source_state = dev_uninstall_source(io, &record, &state);
    if (source_state < 0) {
        (void)osMutexRelease(g_report_lock);
        return AICAM_ERROR;
    }
    if (dev_cleanup_view_matches(io, &record) != AICAM_TRUE) {
        (void)osMutexRelease(g_report_lock);
        return AICAM_ERROR;
    }
    context_state = dev_cleanup_load(io, &context);
    if (context_state < 0 ||
        (source_state == 1 && context_state != 1) ||
        (source_state == 1 &&
         dev_record_equal(&context.record, &record) != AICAM_TRUE)) {
        (void)osMutexRelease(g_report_lock);
        return AICAM_ERROR;
    }
    if (source_state == 0) {
        if (context_state == 1 &&
            context.state == APP_REPORT_DEVICE_CLEANUP_PREPARED &&
            dev_record_equal(&context.record, &record) == AICAM_TRUE) {
            valid = AICAM_TRUE;
        } else {
            if (context_state == 1 &&
                dev_cleanup_status_complete(&context) != AICAM_TRUE) {
                (void)osMutexRelease(g_report_lock);
                return AICAM_ERROR;
            }
            valid = dev_cleanup_create(io, &record, &context);
        }
    } else {
        valid = AICAM_TRUE;
    }
    if (valid != AICAM_TRUE ||
        dev_record_equal(&context.record, &record) != AICAM_TRUE ||
        dev_cleanup_view_matches(io, &context.record) != AICAM_TRUE) {
        (void)osMutexRelease(g_report_lock);
        return AICAM_ERROR;
    }
    dev_cleanup_view_set(io, &context.record);
    app_report_service_set_committed_io(&g_cleanup_view_io);
    if (dev_report_revoke(&context) != AICAM_TRUE) {
        (void)app_report_service_set_session(NULL);
        app_report_service_set_committed_io(io);
        (void)osMutexRelease(g_report_lock);
        return AICAM_ERROR;
    }
    if (app_report_service_set_session(NULL) != APP_REPORT_ABI_OK) {
        (void)app_report_service_set_session(NULL);
        app_report_service_set_committed_io(io);
        (void)osMutexRelease(g_report_lock);
        return AICAM_ERROR;
    }
    g_cleanup_generation = context.generation;
    g_cleanup_context_valid = AICAM_TRUE;
    (void)osMutexRelease(g_report_lock);
    return AICAM_OK;
}

void app_report_service_device_uninstall_finish(const app_install_io_t *io)
{
    if (dev_locks_ensure() != AICAM_TRUE ||
        osMutexAcquire(g_report_lock, osWaitForever) != osOK) {
        return;
    }
    (void)app_report_service_set_session(NULL);
    app_report_service_set_committed_io(io);
    g_cleanup_context_valid = AICAM_FALSE;
    g_cleanup_view_base = NULL;
    (void)osMutexRelease(g_report_lock);
}

aicam_result_t app_report_service_device_start(void)
{
    aicam_result_t rc;

    if (dev_locks_ready() != AICAM_TRUE) {
        LOG_SVC_ERROR("appreport: lock create failed");
        return AICAM_ERROR;
    }
    (void)osMutexAcquire(g_life_lock, osWaitForever);
    if (g_life_state == DEV_LIFE_RUNNING) {
        (void)osMutexRelease(g_life_lock);
        return AICAM_OK;
    }
    if (g_life_state == DEV_LIFE_STOPPING) {
        (void)osMutexRelease(g_life_lock);
        LOG_SVC_ERROR("appreport: start while stopping");
        return AICAM_ERROR_BUSY;
    }
    if (g_life_state == DEV_LIFE_QUIESCE_PENDING) {
        if (dev_quiesce_wait() != AICAM_TRUE) {
            (void)osMutexRelease(g_life_lock);
            LOG_SVC_ERROR("appreport: start blocked, worker not quiesced");
            return AICAM_ERROR_BUSY;
        }
        dev_finish_stop();
    }
    rc = dev_pump_launch();
    if (rc == AICAM_OK) {
        g_life_state = DEV_LIFE_RUNNING;
    }
    (void)osMutexRelease(g_life_lock);
    return rc;
}

aicam_result_t app_report_service_device_stop(void)
{
    if (dev_locks_ready() != AICAM_TRUE) {
        LOG_SVC_ERROR("appreport: lock create failed");
        return AICAM_ERROR;
    }
    (void)osMutexAcquire(g_life_lock, osWaitForever);
    if (g_life_state == DEV_LIFE_STOPPED) {
        (void)osMutexRelease(g_life_lock);
        return AICAM_OK;
    }
    if (g_life_state == DEV_LIFE_STOPPING) {
        (void)osMutexRelease(g_life_lock);
        LOG_SVC_ERROR("appreport: stop already in progress");
        return AICAM_ERROR_BUSY;
    }
    g_life_state = DEV_LIFE_STOPPING;
    g_running = AICAM_FALSE;
    if (dev_quiesce_wait() != AICAM_TRUE) {
        g_life_state = DEV_LIFE_QUIESCE_PENDING;
        LOG_SVC_ERROR("appreport: stop failed, worker not quiesced, "
                      "state kept");
        (void)osMutexRelease(g_life_lock);
        return AICAM_ERROR;
    }
    dev_finish_stop();
    (void)osMutexRelease(g_life_lock);
    LOG_SVC_INFO("appreport: stopped");
    return AICAM_OK;
}

aicam_bool_t app_report_service_device_running(void)
{
    return (g_life_state == DEV_LIFE_STOPPED) ? AICAM_FALSE : AICAM_TRUE;
}

static const char *report_state_str(uint32_t state)
{
    switch (state) {
    case APP_REPORT_MQTT_NOT_CONFIGURED:
        return "NOT_CONFIGURED";
    case APP_REPORT_MQTT_PENDING:
        return "PENDING";
    case APP_REPORT_MQTT_TRANSPORT_SUCCEEDED:
        return "TRANSPORT_SUCCEEDED";
    case APP_REPORT_MQTT_TRANSPORT_FAILED:
        return "TRANSPORT_FAILED";
    case APP_REPORT_MQTT_UNKNOWN:
        return "UNKNOWN";
    case APP_REPORT_MQTT_CANCELLED:
        return "CANCELLED";
    case APP_REPORT_MQTT_RETRY_SUPPRESSED:
        return "RETRY_SUPPRESSED";
    case APP_REPORT_MQTT_DISABLED:
        return "DISABLED";
    case APP_REPORT_MQTT_POLICY_UNKNOWN:
        return "POLICY_UNKNOWN";
    default:
        return "?";
    }
}

static int appreport_cmd(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "start") == 0) {
        return (app_report_service_device_start() == AICAM_OK) ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "stop") == 0) {
        return (app_report_service_device_stop() == AICAM_OK) ? 0 : 1;
    }
    if (argc < 2 || strcmp(argv[1], "info") == 0) {
        app_report_stats_t st;
        uint32_t i;

        LOG_SIMPLE("appreport: running=%d boot=%u queue=%u\r\n",
                   (int)app_report_service_device_running(),
                   (unsigned int)app_report_service_boot_id(),
                   (unsigned int)app_report_service_queue_count());
        app_report_service_get_stats(&st);
        LOG_SIMPLE("appreport: accepted=%u dedup=%u rej(param=%u schema=%u "
                   "quota=%u busy=%u unauth=%u ident=%u)\r\n",
                   (unsigned int)st.accepted, (unsigned int)st.dedup_hits,
                   (unsigned int)st.rejected_param,
                   (unsigned int)st.rejected_schema,
                   (unsigned int)st.rejected_quota,
                   (unsigned int)st.rejected_busy,
                   (unsigned int)st.rejected_unauthorized,
                   (unsigned int)st.rejected_identity);
        for (i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
            app_report_entry_info_t info;
            if (app_report_service_entry(i, &info) != AICAM_TRUE) {
                continue;
            }
            LOG_SIMPLE("appreport: slot=%u seq=%u len=%u mqtt=%s(%u) "
                       "webhook=%s(%u)\r\n",
                       (unsigned int)i, (unsigned int)info.report_seq,
                       (unsigned int)info.payload_len,
                       report_state_str(info.mqtt_state),
                       (unsigned int)info.mqtt_attempts,
                       report_state_str(info.webhook_state),
                       (unsigned int)info.webhook_attempts);
        }
        return 0;
    }
    LOG_SIMPLE("usage: appreport info | appreport start | appreport stop\r\n");
    return 1;
}

static const debug_cmd_reg_t g_appreport_cmd_table[] = {
    { "appreport", "appreport info | appreport start | appreport stop",
      appreport_cmd }
};

void app_report_service_device_cmd_register(void)
{
    if (dev_locks_ensure() != AICAM_TRUE) {
        LOG_SVC_ERROR("appreport: lock create failed");
        return;
    }
    debug_register_commands(g_appreport_cmd_table, 1u);
}
