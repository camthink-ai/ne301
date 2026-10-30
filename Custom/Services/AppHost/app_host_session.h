#ifndef APP_HOST_SESSION_H
#define APP_HOST_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"
#include "pp.h"
#include "app_host_abi.h"
#include "app_host_validate.h"
#include "app_admission.h"
#include "app_installer.h"
#include "app_host_control.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SESSION_REASON_SLOT 48u
#define APP_SESSION_LOG_CAP 512u

#define APP_SESSION_ABI_VERSION APP_ADMISSION_ABI_VERSION_V2
#define APP_SESSION_ABI_VERSION_V3 APP_ADMISSION_ABI_VERSION_V3
#define APP_SESSION_ABI_VERSION_V4 APP_ADMISSION_ABI_VERSION_V4

#define APP_SESSION_CAP_LOG APP_ADMISSION_CAP_LOG
#define APP_SESSION_CAP_TICK_MS APP_ADMISSION_CAP_TICK_MS
#define APP_SESSION_CAP_AI_EVENTS APP_ADMISSION_V2_CAP_AI_EVENTS
#define APP_SESSION_CAP_REPORT_SUBMIT APP_ADMISSION_V2_CAP_REPORT_SUBMIT
#define APP_SESSION_CAP_SESSION_LIFECYCLE APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE
#define APP_SESSION_GRANTED_CAPS (APP_SESSION_CAP_LOG | \
                                  APP_SESSION_CAP_TICK_MS | \
                                  APP_SESSION_CAP_AI_EVENTS | \
                                  APP_SESSION_CAP_SESSION_LIFECYCLE)

#define APP_SESSION_V3_CAP_APP_STATE APP_ADMISSION_V2_CAP_APP_STATE
#define APP_SESSION_V3_CAP_CONTROL_RECEIVE APP_ADMISSION_V3_CAP_CONTROL_RECEIVE
#define APP_SESSION_V3_CAP_BUSINESS_SNAPSHOT APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT
#define APP_SESSION_V3_GRANTED_CAPS (APP_SESSION_CAP_LOG | \
                                     APP_SESSION_CAP_TICK_MS | \
                                     APP_SESSION_CAP_AI_EVENTS | \
                                     APP_SESSION_CAP_REPORT_SUBMIT | \
                                     APP_SESSION_V3_CAP_APP_STATE | \
                                     APP_SESSION_CAP_SESSION_LIFECYCLE | \
                                     APP_SESSION_V3_CAP_CONTROL_RECEIVE | \
                                     APP_SESSION_V3_CAP_BUSINESS_SNAPSHOT)

#define APP_SESSION_V3_ABI_TABLE_SIZE APP_ADMISSION_V3_ABI_TABLE_SIZE
#define APP_SESSION_V3_STATE_QUOTA_MAX 8192u
#define APP_SESSION_V3_CONTROL_CAPACITY APP_CONTROL_MSG_RAM_MAX
#define APP_SESSION_V3_SNAPSHOT_CAPACITY APP_CONTROL_SNAPSHOT_RAM_MAX
#define APP_SESSION_V4_ABI_TABLE_SIZE APP_ADMISSION_V4_ABI_TABLE_SIZE

#define APP_SESSION_EVENT_HEADER_SIZE APP_ADMISSION_V2_EVENT_HEADER_SIZE
#define APP_SESSION_EVENT_RECORD_SIZE APP_ADMISSION_V2_EVENT_RECORD_SIZE
#define APP_SESSION_EVENT_MAX_DETECTIONS APP_ADMISSION_V2_EVENT_MAX_DETECTIONS
#define APP_SESSION_EVENT_WIRE_MAX APP_ADMISSION_V2_MIN_EVENT_MAX
#define APP_SESSION_EVENT_CAPACITY_MAX 16384u

#define APP_SESSION_RING_SLOTS 8u
#define APP_SESSION_MAX_CLASSES 32u
#define APP_SESSION_CLASS_SLOT 32u
#define APP_SESSION_MODEL_NAME_SLOT 64u
#define APP_SESSION_MODEL_VERSION_SLOT 32u
#define APP_SESSION_META_SIZE 128u
#define APP_SESSION_MAX_WAIT_CLAMP_MS 5000u
#define APP_SESSION_DEFAULT_STOP_WAIT_MS 10000u

#define APP_SESSION_EVT_KIND_FRAME 1u
#define APP_SESSION_EVT_KIND_MODEL_CHANGED 2u
#define APP_SESSION_EVT_KIND_GAP 3u

#define APP_SESSION_EVT_FLAG_LOST_KNOWN 0x1u
#define APP_SESSION_EVT_FLAG_LOST_UNKNOWN 0x2u
#define APP_SESSION_EVT_LOST_UNKNOWN_MARK 0xFFFFFFFFu

typedef int32_t (*app_session_fn_log_t)(const char *text);
typedef int32_t (*app_session_fn_tick_ms_t)(void);
typedef int32_t (*app_session_fn_event_next_t)(void *out, uint32_t cap,
                                               uint32_t *actual_len,
                                               uint32_t max_wait_ms);
typedef int32_t (*app_session_fn_model_meta_t)(void *out, uint32_t cap,
                                               uint32_t *actual_len);
typedef int32_t (*app_session_fn_class_name_t)(uint32_t model_gen,
                                               uint32_t class_gen,
                                               uint32_t class_index,
                                               void *out_utf8, uint32_t cap,
                                               uint32_t *actual_len);
typedef int32_t (*app_session_fn_report_submit_t)(const void *json, uint32_t len,
                                                  uint32_t app_report_seq,
                                                  uint32_t *out_host_boot_id);
typedef int32_t (*app_session_fn_report_status_t)(uint32_t host_boot_id,
                                                  uint32_t report_seq,
                                                  uint32_t *out_mqtt_state,
                                                  uint32_t *out_webhook_state);
typedef int32_t (*app_session_fn_state_read_t)(void *out, uint32_t cap,
                                               uint32_t *actual_len,
                                               uint32_t *out_revision);
typedef int32_t (*app_session_fn_state_commit_t)(const void *blob, uint32_t len,
                                                 uint32_t expected_revision,
                                                 uint32_t *new_revision);
typedef int32_t (*app_session_fn_should_stop_t)(void);

typedef int32_t (*app_session_fn_control_next_t)(void *out, uint32_t cap,
                                                 uint32_t *actual_len);
typedef int32_t (*app_session_fn_control_reply_t)(const void *buf, uint32_t len);
typedef int32_t (*app_session_fn_snapshot_publish_t)(const void *buf, uint32_t len);
typedef int32_t (*app_session_fn_live_publish_t)(const void *buf, uint32_t len);

typedef struct {
    uint32_t table_size;
    uint32_t abi_version;
    app_session_fn_log_t log;
    app_session_fn_tick_ms_t tick_ms;
    app_session_fn_event_next_t event_next;
    app_session_fn_model_meta_t model_meta;
    app_session_fn_class_name_t class_name;
    app_session_fn_report_submit_t report_submit;
    app_session_fn_report_status_t report_status;
    app_session_fn_state_read_t state_read;
    app_session_fn_state_commit_t state_commit;
    app_session_fn_should_stop_t should_stop;
} app_host_session_api_table_t;

typedef struct {
    uint32_t table_size;
    uint32_t abi_version;
    app_session_fn_log_t log;
    app_session_fn_tick_ms_t tick_ms;
    app_session_fn_event_next_t event_next;
    app_session_fn_model_meta_t model_meta;
    app_session_fn_class_name_t class_name;
    app_session_fn_report_submit_t report_submit;
    app_session_fn_report_status_t report_status;
    app_session_fn_state_read_t state_read;
    app_session_fn_state_commit_t state_commit;
    app_session_fn_should_stop_t should_stop;
    app_session_fn_control_next_t control_next;
    app_session_fn_control_reply_t control_reply;
    app_session_fn_snapshot_publish_t snapshot_publish;
} app_host_session_api_table_v3_t;

typedef struct {
    uint32_t table_size;
    uint32_t abi_version;
    app_session_fn_log_t log;
    app_session_fn_tick_ms_t tick_ms;
    app_session_fn_event_next_t event_next;
    app_session_fn_model_meta_t model_meta;
    app_session_fn_class_name_t class_name;
    app_session_fn_report_submit_t report_submit;
    app_session_fn_report_status_t report_status;
    app_session_fn_state_read_t state_read;
    app_session_fn_state_commit_t state_commit;
    app_session_fn_should_stop_t should_stop;
    app_session_fn_control_next_t control_next;
    app_session_fn_control_reply_t control_reply;
    app_session_fn_snapshot_publish_t snapshot_publish;
    app_session_fn_live_publish_t live_publish;
} app_host_session_api_table_v4_t;

typedef int (*app_session_entry_fn_t)(const app_host_session_api_table_t *api,
                                      uint32_t abi_version);

typedef int (*app_session_entry_v3_fn_t)(const app_host_session_api_table_v3_t *api,
                                         uint32_t abi_version);
typedef int (*app_session_entry_v4_fn_t)(const app_host_session_api_table_v4_t *api,
                                         uint32_t abi_version);

typedef enum {
    APP_SESSION_STATE_IDLE = 0,
    APP_SESSION_STATE_RUNNING,
    APP_SESSION_STATE_STOPPING
} app_session_state_t;

typedef enum {
    APP_SESSION_OK = 0,
    APP_SESSION_ERR_PARAM = -1,
    APP_SESSION_ERR_NOT_INSTALLED = -2,
    APP_SESSION_ERR_UNAVAILABLE = -3,
    APP_SESSION_ERR_STORAGE_STATE_UNKNOWN = -4,
    APP_SESSION_ERR_BUSY = -5,
    APP_SESSION_ERR_LOAD_FAILED = -6,
    APP_SESSION_ERR_RUNTIME_UNAVAILABLE = -7,
    APP_SESSION_ERR_NOT_RUNNING = -8,
    APP_SESSION_ERR_PROFILE_UNSUPPORTED = -9
} app_session_result_t;

typedef app_session_result_t (*app_session_pair_authorize_fn_t)(
    void *ctx,
    const app_install_io_t *io,
    const app_install_record_t *native_record,
    char *reason,
    size_t reason_cap);

typedef struct {
    char model_name[APP_SESSION_MODEL_NAME_SLOT];
    char model_version[APP_SESSION_MODEL_VERSION_SLOT];
    uint32_t class_count;
    char class_names[APP_SESSION_MAX_CLASSES][APP_SESSION_CLASS_SLOT];
    uintptr_t identity_addr;
    uint32_t identity_size;
    uint32_t content_digest;
    aicam_bool_t result_type_od;
} app_session_model_view_t;

typedef struct {
    void *ctx;
    uint8_t *(*stage_base)(void *ctx, size_t *cap);
    int (*stage_commit)(void *ctx, size_t stage_cap, const app_host_image_info_t *info);
    int (*invoke_entry)(void *ctx, const void *api, uint32_t abi_version,
                        const app_host_image_info_t *info, int *raw);
    void (*scrub)(void *ctx);
    void (*log_sink)(void *ctx, const char *text);
    uint32_t (*tick_ms)(void *ctx);
    void (*sleep_ms)(void *ctx, uint32_t ms);
    int (*entry_spawn)(void *ctx, void (*fn)(void *arg), void *arg);
    aicam_bool_t (*entry_exited)(void *ctx);
    void *(*mutex_create)(void *ctx);
    void (*mutex_destroy)(void *ctx, void *mtx);
    int (*mutex_lock)(void *ctx, void *mtx);
    void (*mutex_unlock)(void *ctx, void *mtx);
    void *(*event_create)(void *ctx);
    void (*event_destroy)(void *ctx, void *evt);
    uint32_t (*event_wait)(void *ctx, void *evt, uint32_t timeout_ms);
    void (*event_set)(void *ctx, void *evt);
    aicam_bool_t (*ai_attach)(void *ctx);
    void (*ai_detach)(void *ctx);
    aicam_bool_t (*ai_model_view)(void *ctx, app_session_model_view_t *out);
    app_session_pair_authorize_fn_t pair_authorize;
} app_host_session_backend_t;

typedef struct {
    app_session_result_t result;
    char reason[APP_SESSION_REASON_SLOT];
    app_install_record_t record;
    uint32_t abi_version;
    uint32_t granted_caps;
    uint32_t event_max;
    uint32_t control_max;
    uint32_t snapshot_max;
    uint32_t session_generation;
} app_host_session_start_out_t;

typedef struct {
    app_session_result_t result;
    aicam_bool_t entry_returned;
    char reason[APP_SESSION_REASON_SLOT];
} app_host_session_stop_out_t;

typedef struct {
    app_session_state_t state;
    aicam_bool_t app_running;
    char publisher_id[APP_INSTALL_PUBLISHER_SLOT + 1];
    char app_id[APP_INSTALL_APP_SLOT + 1];
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint32_t abi_version;
    uint32_t granted_caps;
    uint32_t event_max;
    uint32_t model_generation;
    uint32_t class_generation;
    uint32_t class_count;
    char model_name[APP_SESSION_MODEL_NAME_SLOT];
    char model_version[APP_SESSION_MODEL_VERSION_SLOT];
    uint32_t events_delivered;
    uint32_t frames_dropped;
    uint32_t gap_events;
    uint32_t model_changes;
    app_session_result_t last_result;
    char last_reason[APP_SESSION_REASON_SLOT];
    uint32_t session_generation;
    uint32_t control_max;
    uint32_t snapshot_max;
} app_host_session_status_t;

void app_host_session_init(const app_host_session_backend_t *backend);

app_session_result_t app_host_session_pair_authorize(
    const app_install_io_t *io,
    const app_install_record_t *native_record,
    char *reason,
    size_t reason_cap);

app_session_result_t app_host_session_start(const app_install_io_t *io,
                                            app_host_session_start_out_t *out);

app_session_result_t app_host_session_stop(uint32_t wait_ms,
                                           app_host_session_stop_out_t *out);

aicam_bool_t app_host_session_stop_requested(void);

void app_host_session_status(app_host_session_status_t *out);

const app_host_session_api_table_t *app_host_session_api_table(void);

const app_host_session_api_table_v3_t *app_host_session_api_table_v3(void);

const app_host_session_api_table_v4_t *app_host_session_api_table_v4(void);

void app_host_session_on_ai_result(uint32_t detect_count,
                                   const od_detect_t *detects,
                                   uint32_t monotonic_ms);

size_t app_host_session_take_log(char *buf, size_t cap);

aicam_bool_t app_host_session_log_truncated(void);

const char *app_session_result_str(app_session_result_t result);

const char *app_session_state_str(app_session_state_t state);

#ifdef __cplusplus
}
#endif

#endif
