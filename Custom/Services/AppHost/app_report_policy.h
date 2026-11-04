#ifndef APP_REPORT_POLICY_H
#define APP_REPORT_POLICY_H

#include <stdint.h>

#include "aicam_types.h"

#define APP_REPORT_POLICY_PUBLISHER_ID_SLOT 16u
#define APP_REPORT_POLICY_APP_ID_SLOT 32u
#define APP_REPORT_POLICY_MIN_CAPACITY 1u
#define APP_REPORT_POLICY_MAX_RECORDS 16u
#define APP_REPORT_POLICY_WIRE_RECORD_SIZE 132u
#define APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY \
    (12u + APP_REPORT_POLICY_MAX_RECORDS * APP_REPORT_POLICY_WIRE_RECORD_SIZE)

#define APP_REPORT_POLICY_STORAGE_OK 0
#define APP_REPORT_POLICY_STORAGE_NOT_FOUND 1
#define APP_REPORT_POLICY_STORAGE_ERROR (-1)

#define APP_REPORT_POLICY_PURGE_NONE 0u
#define APP_REPORT_POLICY_PURGE_PENDING 1u
#define APP_REPORT_POLICY_PURGE_COMPLETE 2u
#define APP_REPORT_POLICY_PURGE_UNKNOWN 3u
#define APP_REPORT_POLICY_PURGE_INTENT_ADMIN \
    (0x8000000000000000ull)

typedef struct app_report_policy_identity {
    char publisher_id[APP_REPORT_POLICY_PUBLISHER_ID_SLOT + 1u];
    char app_id[APP_REPORT_POLICY_APP_ID_SLOT + 1u];
    uint8_t publisher_key_sha256[32u];
    uint8_t native_tbs_sha256[32u];
} app_report_policy_identity_t;

typedef struct app_report_policy_values {
    aicam_bool_t mqtt_enabled;
    aicam_bool_t webhook_enabled;
    uint32_t backlog_capacity;
} app_report_policy_values_t;

typedef struct app_report_policy_storage {
    void *user;
    int32_t (*read)(void *user, void *out, uint32_t cap, uint32_t *out_len);
    int32_t (*write)(void *user, const void *data, uint32_t len);
} app_report_policy_storage_t;

typedef struct app_report_policy_status {
    aicam_bool_t mqtt_enabled;
    aicam_bool_t webhook_enabled;
    uint32_t backlog_capacity;
    uint32_t capacity_min;
    uint32_t capacity_max;
    uint32_t backlog_used;
    uint32_t revision;
    uint32_t policy_generation;
    uint32_t mqtt_cancelled;
    uint32_t webhook_cancelled;
    uint32_t purge_state;
    uint32_t purged_entries;
    uint64_t cleanup_intent;
    int32_t last_result;
    aicam_bool_t storage_verified;
} app_report_policy_status_t;

void app_report_policy_store_set_storage(const app_report_policy_storage_t *storage);
int32_t app_report_policy_store_get_base(const app_report_policy_identity_t *identity,
                                         app_report_policy_identity_t *out_identity,
                                         app_report_policy_values_t *out_values,
                                         uint32_t *out_revision);
int32_t app_report_policy_store_rebase_base_tbs(
    const app_report_policy_identity_t *identity,
    const uint8_t new_tbs[32u],
    aicam_bool_t *out_moved);
int32_t app_report_policy_store_get(const app_report_policy_identity_t *identity,
                                    app_report_policy_values_t *out_values,
                                    uint32_t *out_revision);
int32_t app_report_policy_store_has_base(const app_report_policy_identity_t *identity,
                                         aicam_bool_t *out_found);
int32_t app_report_policy_store_commit(const app_report_policy_identity_t *identity,
                                       uint32_t expected_revision,
                                       const app_report_policy_values_t *values,
                                       uint32_t *out_revision);
int32_t app_report_policy_store_remove(const app_report_policy_identity_t *identity,
                                       uint64_t cleanup_intent);
int32_t app_report_policy_store_get_cleanup_intent(
    const app_report_policy_identity_t *identity, uint64_t *out_cleanup_intent);

#endif
