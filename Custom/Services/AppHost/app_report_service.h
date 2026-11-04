#ifndef APP_REPORT_SERVICE_H
#define APP_REPORT_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"
#include "app_installer.h"
#include "app_report_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_REPORT_ABI_OK 0
#define APP_REPORT_ABI_NOT_FOUND 2
#define APP_REPORT_ABI_INVALID_ARGUMENT (-1)
#define APP_REPORT_ABI_UNAUTHORIZED (-3)
#define APP_REPORT_ABI_INCOMPATIBLE (-4)
#define APP_REPORT_ABI_QUOTA_EXCEEDED (-5)
#define APP_REPORT_ABI_STORAGE_UNKNOWN (-6)
#define APP_REPORT_ABI_BUSY (-7)
#define APP_REPORT_ABI_STORAGE_ERROR (-8)
#define APP_REPORT_ABI_CONFLICT (-9)
#define APP_REPORT_ABI_CAPACITY_CONFLICT (-10)

#define APP_REPORT_MQTT_NOT_CONFIGURED 0u
#define APP_REPORT_MQTT_PENDING 1u
#define APP_REPORT_MQTT_TRANSPORT_SUCCEEDED 2u
#define APP_REPORT_MQTT_TRANSPORT_FAILED 3u
#define APP_REPORT_MQTT_UNKNOWN 4u
#define APP_REPORT_MQTT_CANCELLED 5u
#define APP_REPORT_MQTT_RETRY_SUPPRESSED 6u
#define APP_REPORT_MQTT_DISABLED 7u
#define APP_REPORT_MQTT_POLICY_UNKNOWN 8u

#define APP_REPORT_WEBHOOK_NOT_CONFIGURED 0u
#define APP_REPORT_WEBHOOK_PENDING 1u
#define APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED 2u
#define APP_REPORT_WEBHOOK_TRANSPORT_FAILED 3u
#define APP_REPORT_WEBHOOK_UNKNOWN 4u
#define APP_REPORT_WEBHOOK_CANCELLED 5u
#define APP_REPORT_WEBHOOK_RETRY_SUPPRESSED 6u
#define APP_REPORT_WEBHOOK_DISABLED 7u
#define APP_REPORT_WEBHOOK_POLICY_UNKNOWN 8u

#define APP_REPORT_INPUT_MAX 6144u
#define APP_REPORT_SLOT_CAP 6240u
#define APP_REPORT_QUEUE_SLOTS 8u
#ifndef APP_REPORT_POLICY_MAX_CAPACITY
#define APP_REPORT_POLICY_MAX_CAPACITY APP_REPORT_QUEUE_SLOTS
#endif
#define APP_REPORT_POLICY_DEFAULT_CAPACITY APP_REPORT_POLICY_MAX_CAPACITY
#define APP_REPORT_SEEN_RING 16u
#define APP_REPORT_DEVICE_ID_CAP 32u
#define APP_REPORT_MAX_ATTEMPTS 3u
#define APP_REPORT_BACKOFF_POLLS 4u
#define APP_REPORT_SHA256_LEN 32u
#define APP_REPORT_PUBLISHER_ID_SLOT 16u
#define APP_REPORT_APP_ID_SLOT 32u

#define APP_REPORT_CHANNEL_UNCONFIGURED 0u
#define APP_REPORT_CHANNEL_READY 1u
#define APP_REPORT_CHANNEL_WAITING 2u
#define APP_REPORT_CHANNEL_MQTT 1u
#define APP_REPORT_CHANNEL_WEBHOOK 2u

#define APP_REPORT_TYPE_LINE_COUNTING "line_counting"

#if APP_REPORT_POLICY_MAX_CAPACITY > APP_REPORT_QUEUE_SLOTS
#error APP_REPORT_POLICY_MAX_CAPACITY exceeds APP_REPORT_QUEUE_SLOTS
#endif
#if APP_REPORT_POLICY_MAX_CAPACITY < APP_REPORT_POLICY_MIN_CAPACITY
#error APP_REPORT_POLICY_MAX_CAPACITY is below APP_REPORT_POLICY_MIN_CAPACITY
#endif

typedef struct {
    aicam_bool_t active;
    char publisher_id[APP_REPORT_PUBLISHER_ID_SLOT + 1u];
    char app_id[APP_REPORT_APP_ID_SLOT + 1u];
    uint8_t publisher_key_sha256[APP_REPORT_SHA256_LEN];
    uint32_t report_max;
} app_report_session_t;

typedef struct {
    void *user;
    aicam_bool_t (*device_id)(void *user, char *out, uint32_t cap);
    uint32_t (*boot_id)(void *user);
} app_report_identity_t;

#define APP_REPORT_TRANSPORT_OK 0
#define APP_REPORT_TRANSPORT_UNKNOWN 1
#define APP_REPORT_TRANSPORT_FAIL (-1)

typedef struct {
    void *user;
    uint32_t (*mqtt_ready)(void *user);
    int (*mqtt_publish)(void *user, const uint8_t *json, uint32_t len);
    uint32_t (*webhook_ready)(void *user);
    int (*webhook_post)(void *user, const uint8_t *json, uint32_t len);
} app_report_transport_t;

typedef struct {
    uint32_t accepted;
    uint32_t dedup_hits;
    uint32_t rejected_param;
    uint32_t rejected_schema;
    uint32_t rejected_quota;
    uint32_t rejected_busy;
    uint32_t rejected_unauthorized;
    uint32_t rejected_identity;
    uint32_t tx_batches;
    uint32_t tx_skipped;
} app_report_stats_t;

typedef struct {
    uint32_t host_boot_id;
    uint32_t report_seq;
    uint32_t mqtt_state;
    uint32_t webhook_state;
    uint32_t mqtt_attempts;
    uint32_t webhook_attempts;
    size_t payload_len;
} app_report_entry_info_t;

void app_report_service_init(const app_report_identity_t *identity,
                             const app_report_transport_t *transport);

void app_report_service_restart(void);

void app_report_service_set_lock(void (*enter)(void), void (*exit)(void));

void app_report_service_set_policy_storage(const app_report_policy_storage_t *storage);

void app_report_service_set_committed_io(const app_install_io_t *io);

int32_t app_report_service_trust_uninstall_target(
    const app_report_policy_identity_t *identity);

int32_t app_report_service_policy_query(const app_report_session_t *session,
                                        const app_report_policy_identity_t *identity,
                                        app_report_policy_status_t *out);

int32_t app_report_service_policy_commit(const app_report_session_t *session,
                                         const app_report_policy_identity_t *identity,
                                         uint32_t expected_revision,
                                         const app_report_policy_values_t *values,
                                         app_report_policy_status_t *out);

int32_t app_report_service_policy_purge(const app_report_policy_identity_t *identity,
                                        uint64_t cleanup_intent,
                                        app_report_policy_status_t *out);

int32_t app_report_service_policy_purge_status(
    const app_report_policy_identity_t *identity,
    uint64_t cleanup_intent,
    app_report_policy_status_t *out);

int32_t app_report_service_transport_entered(uint32_t channel);

int32_t app_report_service_transport_start(uint32_t channel);

int32_t app_report_service_transport_begin(uint32_t channel);

int32_t app_report_service_set_session(const app_report_session_t *session);

int32_t app_report_submit(const app_report_session_t *session,
                          const void *json, uint32_t len,
                          uint32_t app_report_seq, uint32_t *out_host_boot_id);

int32_t app_report_status(uint32_t host_boot_id, uint32_t report_seq,
                          uint32_t *out_mqtt_state, uint32_t *out_webhook_state);

void app_report_service_pump(void);

uint32_t app_report_service_boot_id(void);

int32_t app_report_service_policy_rebase(const app_install_record_t *record);

void app_report_service_policy_debug(aicam_bool_t *out_pending,
                                     aicam_bool_t *out_bound,
                                     int32_t *out_last_result,
                                     uint8_t out_active_tbs[APP_REPORT_SHA256_LEN]);

uint32_t app_report_service_queue_count(void);

void app_report_service_get_stats(app_report_stats_t *out);

aicam_bool_t app_report_service_entry(uint32_t index, app_report_entry_info_t *out);

const char *app_report_result_str(int32_t result);

#ifdef __cplusplus
}
#endif

#endif
