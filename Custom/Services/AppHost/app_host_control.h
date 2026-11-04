#ifndef APP_HOST_CONTROL_H
#define APP_HOST_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CONTROL_PROTO_MAGIC 0x3343464Eu
#define APP_CONTROL_PROTO_MAGIC_V4 0x3443464Eu
#define APP_CONTROL_HEADER_VERSION 1u
#define APP_CONTROL_HEADER_SIZE 32u

#define APP_CONTROL_MSG_TYPE_CONFIG_APPLY 1u
#define APP_CONTROL_MSG_TYPE_COUNTERS_RESET 2u
#define APP_CONTROL_MSG_TYPE_RECEIPT 3u
#define APP_CONTROL_MSG_TYPE_SNAPSHOT 4u
#define APP_CONTROL_MSG_TYPE_ENABLE_SET 5u
#define APP_CONTROL_ENABLE_SET_SIZE 48u
#define APP_CONTROL_ENABLE_RECEIPT_SIZE 48u
#define APP_CONTROL_ENABLE_PAYLOAD_SIZE 16u
#define APP_CONTROL_ENABLE_MAGIC 0x31424E45u
#define APP_CONTROL_ENABLE_RECEIPT_MAGIC 0x31524E45u
#define APP_CONTROL_ENABLE_STATE_ENABLED 1u
#define APP_CONTROL_ENABLE_STATE_PAUSED 2u
#define APP_CONTROL_ENABLE_STATE_UNKNOWN 3u

#define APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED 0x00000001u
#define APP_CONTROL_FLAG_KNOWN_MASK APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED

#define APP_CONTROL_RESULT_APPLIED 0u
#define APP_CONTROL_RESULT_REJECTED_STALE_CONFIG 1u
#define APP_CONTROL_RESULT_REJECTED_INVALID 2u
#define APP_CONTROL_RESULT_REJECTED_RESOURCE 3u
#define APP_CONTROL_RESULT_REJECTED_UNCONFIRMED 4u
#define APP_CONTROL_RESULT_FAILED_STATE_CONFLICT 5u
#define APP_CONTROL_RESULT_FAILED_STORAGE_IO 6u
#define APP_CONTROL_RESULT_FAILED_STORAGE_UNKNOWN 7u
#define APP_CONTROL_RESULT_MAX 7u

#define APP_CONTROL_MSG_RAM_MAX 512u
#define APP_CONTROL_SNAPSHOT_RAM_MAX 2048u
#define APP_CONTROL_CACHE_SLOTS 4u
#define APP_CONTROL_STALE_MS 10000u
#define APP_CONTROL_JSON_MAX_DEPTH 16u
#define APP_CONTROL_JSON_KEYS_MAX 48u
#define APP_CONTROL_IDENTITY_SLOT 48u
#define APP_CONTROL_GENERATION_LAST 0xFFFFFFFFu

#define APP_CONTROL_OK 0
#define APP_CONTROL_NO_EVENT 1
#define APP_CONTROL_NOT_FOUND 2
#define APP_CONTROL_INVALID_ARGUMENT (-1)
#define APP_CONTROL_BUFFER_TOO_SMALL (-2)
#define APP_CONTROL_UNAUTHORIZED (-3)
#define APP_CONTROL_QUOTA_EXCEEDED (-5)

typedef enum {
    APP_CONTROL_REG_OK = 0,
    APP_CONTROL_REG_CACHED = 1,
    APP_CONTROL_REG_ALREADY = 2,
    APP_CONTROL_ERR_PARAM = -1,
    APP_CONTROL_ERR_UNAUTHORIZED = -2,
    APP_CONTROL_ERR_INVALID = -3,
    APP_CONTROL_ERR_BUSY = -4,
    APP_CONTROL_ERR_CONFLICT = -5,
    APP_CONTROL_ERR_DUPLICATE_ID_CONFLICT = -6,
    APP_CONTROL_ERR_UNCONFIRMED = -7,
    APP_CONTROL_ERR_QUOTA_EXCEEDED = -8,
    APP_CONTROL_ERR_STORAGE_GUARD = -9,
    APP_CONTROL_ERR_GENERATION_EXHAUSTED = -10
} app_control_register_result_t;

typedef enum {
    APP_CONTROL_CMD_NONE = 0,
    APP_CONTROL_CMD_PENDING = 1,
    APP_CONTROL_CMD_DELIVERED = 2,
    APP_CONTROL_CMD_APPLIED = 3,
    APP_CONTROL_CMD_REJECTED_STALE_CONFIG = 4,
    APP_CONTROL_CMD_REJECTED_INVALID = 5,
    APP_CONTROL_CMD_REJECTED_RESOURCE = 6,
    APP_CONTROL_CMD_REJECTED_UNCONFIRMED = 7,
    APP_CONTROL_CMD_FAILED_STATE_CONFLICT = 8,
    APP_CONTROL_CMD_FAILED_STORAGE_IO = 9,
    APP_CONTROL_CMD_FAILED_STORAGE_UNKNOWN = 10,
    APP_CONTROL_CMD_UNKNOWN = 11
} app_control_cmd_state_t;

typedef enum {
    APP_CONTROL_SNAPSHOT_NONE = 0,
    APP_CONTROL_SNAPSHOT_FRESH = 1,
    APP_CONTROL_SNAPSHOT_STALE = 2
} app_control_snapshot_freshness_t;

typedef struct {
    void *ctx;
    uint32_t (*now_ms)(void *ctx);
    void *(*mutex_create)(void *ctx);
    void (*mutex_destroy)(void *ctx, void *mtx);
    int (*mutex_lock)(void *ctx, void *mtx);
    void (*mutex_unlock)(void *ctx, void *mtx);
    int32_t (*enable_precommit)(void *ctx, uint32_t session_generation,
                                uint32_t request_id, uint32_t config_generation,
                                uint32_t expected_state_revision, uint32_t desired_state);
    int32_t (*enable_receipt_check)(void *ctx, uint32_t session_generation,
                                    uint32_t request_id, uint32_t config_generation,
                                    uint32_t state_revision, uint32_t desired_state);
} app_control_backend_t;

typedef struct {
    uint32_t control_max;
    uint32_t snapshot_max;
    uint32_t caps;
    uint32_t protocol_major;
    char publisher_id[APP_CONTROL_IDENTITY_SLOT];
    char app_id[APP_CONTROL_IDENTITY_SLOT];
} app_control_session_cfg_t;

typedef struct {
    uint32_t request_id;
    uint32_t msg_type;
    const void *payload;
    uint32_t payload_len;
    uint32_t expected_config_generation;
    uint32_t flags;
} app_control_request_t;

typedef struct {
    aicam_bool_t session_active;
    uint32_t session_generation;
    uint32_t caps;
    uint32_t control_max;
    uint32_t snapshot_max;
    aicam_bool_t has_known_config_generation;
    uint32_t known_config_generation;
    aicam_bool_t has_command;
    app_control_cmd_state_t command_state;
    uint32_t command_request_id;
    uint32_t command_msg_type;
    uint32_t command_expected_config_generation;
    uint32_t command_flags;
    uint32_t command_payload_len;
    uint32_t result_config_generation;
    uint32_t result_state_revision;
    uint32_t unknown_count;
    uint32_t unknown_session_generation;
    uint32_t unknown_request_id;
    uint32_t unknown_msg_type;
} app_control_status_t;

typedef struct {
    app_control_snapshot_freshness_t freshness;
    uint32_t session_generation;
    uint32_t visible_version;
    uint32_t config_generation;
    uint32_t state_revision;
    uint32_t age_ms;
    uint32_t payload_len;
    char publisher_id[APP_CONTROL_IDENTITY_SLOT];
    char app_id[APP_CONTROL_IDENTITY_SLOT];
} app_control_snapshot_view_t;

void app_host_control_init(const app_control_backend_t *backend);

void app_host_control_generation_adopt(uint32_t next_generation);

int32_t app_host_control_session_open(const app_control_session_cfg_t *cfg,
                                      uint32_t *out_generation);

void app_host_control_session_close(void);

app_control_register_result_t app_host_control_register(const app_control_request_t *req);

int32_t app_host_control_deliver(void *out, uint32_t cap, uint32_t *actual_len);

int32_t app_host_control_receipt(const void *buf, uint32_t len);

int32_t app_host_control_snapshot_submit(const void *buf, uint32_t len);

void app_host_control_status(app_control_status_t *out);

int32_t app_host_control_snapshot_read(app_control_snapshot_view_t *out,
                                       void *payload_buf, uint32_t cap,
                                       uint32_t *payload_len);

aicam_bool_t app_host_control_json_object_valid(const void *payload, uint32_t len);

const char *app_host_control_register_result_str(app_control_register_result_t result);

const char *app_host_control_cmd_state_str(app_control_cmd_state_t state);

const char *app_host_control_snapshot_freshness_str(app_control_snapshot_freshness_t f);

const char *app_host_control_msg_type_str(uint32_t msg_type);

#ifdef __cplusplus
}
#endif

#endif
