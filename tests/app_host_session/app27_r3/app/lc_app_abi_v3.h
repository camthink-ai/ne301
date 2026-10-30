
#ifndef LC_APP_ABI_V3_H
#define LC_APP_ABI_V3_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LC_APP_ABI_V3 0x00030000u

#define LC_APP_API_V3_TABLE_SIZE 60u

typedef int32_t (*lc_app_fn_log_t)(const char *text);
typedef int32_t (*lc_app_fn_tick_ms_t)(void);
typedef int32_t (*lc_app_fn_event_next_t)(void *out, uint32_t cap,
                                          uint32_t *actual_len,
                                          uint32_t max_wait_ms);
typedef int32_t (*lc_app_fn_model_meta_t)(void *out, uint32_t cap,
                                          uint32_t *actual_len);
typedef int32_t (*lc_app_fn_class_name_t)(uint32_t model_gen,
                                          uint32_t class_gen,
                                          uint32_t class_index,
                                          void *out_utf8, uint32_t cap,
                                          uint32_t *actual_len);
typedef int32_t (*lc_app_fn_report_submit_t)(const void *json, uint32_t len,
                                             uint32_t app_report_seq,
                                             uint32_t *out_host_boot_id);
typedef int32_t (*lc_app_fn_report_status_t)(uint32_t host_boot_id,
                                             uint32_t report_seq,
                                             uint32_t *out_mqtt_state,
                                             uint32_t *out_webhook_state);
typedef int32_t (*lc_app_fn_state_read_t)(void *out, uint32_t cap,
                                          uint32_t *actual_len,
                                          uint32_t *out_revision);
typedef int32_t (*lc_app_fn_state_commit_t)(const void *blob, uint32_t len,
                                            uint32_t expected_revision,
                                            uint32_t *new_revision);
typedef int32_t (*lc_app_fn_should_stop_t)(void);
typedef int32_t (*lc_app_fn_control_next_t)(void *out, uint32_t cap,
                                            uint32_t *actual_len);
typedef int32_t (*lc_app_fn_control_reply_t)(const void *buf, uint32_t len);
typedef int32_t (*lc_app_fn_snapshot_publish_t)(const void *buf, uint32_t len);

typedef struct lc_app_api_v3 {
    uint32_t                 table_size;
    uint32_t                 abi_version;
    lc_app_fn_log_t          log;
    lc_app_fn_tick_ms_t      tick_ms;
    lc_app_fn_event_next_t   event_next;
    lc_app_fn_model_meta_t   model_meta;
    lc_app_fn_class_name_t   class_name;
    lc_app_fn_report_submit_t report_submit;
    lc_app_fn_report_status_t report_status;
    lc_app_fn_state_read_t   state_read;
    lc_app_fn_state_commit_t state_commit;
    lc_app_fn_should_stop_t  should_stop;
    lc_app_fn_control_next_t  control_next;
    lc_app_fn_control_reply_t control_reply;
    lc_app_fn_snapshot_publish_t snapshot_publish;
} lc_app_api_v3_t;

#if UINTPTR_MAX == 0xFFFFFFFFu
#include "lc_app_abi_v3_layout_asserts.h"
#endif

#define LC_RET_OK               0
#define LC_RET_NO_EVENT         1
#define LC_RET_NOT_FOUND        2
#define LC_RET_INVALID_ARGUMENT (-1)
#define LC_RET_BUFFER_TOO_SMALL (-2)
#define LC_RET_UNAUTHORIZED     (-3)
#define LC_RET_INCOMPATIBLE     (-4)
#define LC_RET_QUOTA_EXCEEDED   (-5)
#define LC_RET_STORAGE_UNKNOWN  (-6)
#define LC_RET_BUSY             (-7)
#define LC_RET_REVISION_CONFLICT (-8)
#define LC_RET_IO_ERROR         (-9)
#define LC_RET_STOPPING         (-10)

#define LC_CTRL_PROTO_MAGIC     0x3343464Eu
#define LC_CTRL_HEADER_VERSION  1u
#define LC_CTRL_HEADER_SIZE     32u

#define LC_CTRL_TYPE_CONFIG_APPLY   1u
#define LC_CTRL_TYPE_COUNTERS_RESET 2u
#define LC_CTRL_TYPE_REPLY          3u
#define LC_CTRL_TYPE_SNAPSHOT       4u

#define LC_CTRL_OFF_PROTO_MAGIC        0u
#define LC_CTRL_OFF_HEADER_VERSION     4u
#define LC_CTRL_OFF_MSG_TYPE           6u
#define LC_CTRL_OFF_TOTAL_LEN          8u
#define LC_CTRL_OFF_SESSION_GENERATION 12u
#define LC_CTRL_OFF_REQUEST_ID         16u
#define LC_CTRL_OFF_CONFIG_GENERATION  20u
#define LC_CTRL_OFF_STATE_REVISION     24u
#define LC_CTRL_OFF_FLAGS              28u

#define LC_CTRL_FLAG_DESTRUCTIVE_CONFIRMED 0x1u
#define LC_CTRL_FLAGS_VALID_MASK           0x1u

#define LC_CTRL_RESULT_APPLIED                 0u
#define LC_CTRL_RESULT_REJECTED_STALE_CONFIG   1u
#define LC_CTRL_RESULT_REJECTED_INVALID        2u
#define LC_CTRL_RESULT_REJECTED_RESOURCE       3u
#define LC_CTRL_RESULT_REJECTED_UNCONFIRMED    4u
#define LC_CTRL_RESULT_FAILED_STATE_CONFLICT   5u
#define LC_CTRL_RESULT_FAILED_STORAGE_IO       6u
#define LC_CTRL_RESULT_FAILED_STORAGE_UNKNOWN  7u

#define LC_CTRL_CONTROL_MAX_DECLARED  256u
#define LC_CTRL_SNAPSHOT_MAX_DECLARED 1024u

#define LC_CTRL_MSG_BUF_CAP     LC_CTRL_CONTROL_MAX_DECLARED
#define LC_CTRL_CANDIDATE_MAX   (LC_CTRL_MSG_BUF_CAP - LC_CTRL_HEADER_SIZE)
#define LC_CTRL_DEDUP_SLOTS     4u

#define LC_SNAPSHOT_BUF_CAP     LC_CTRL_SNAPSHOT_MAX_DECLARED
#define LC_SNAPSHOT_INTERVAL_MS 5000u

#ifndef LC_APP_ABI_INLINE_HELPERS
#define LC_APP_ABI_INLINE_HELPERS
static inline uint32_t lc_tick_diff(uint32_t later, uint32_t earlier)
{
    return (uint32_t)(later - earlier);
}

static inline uint32_t lc_rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void lc_wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}
#endif

#ifdef __cplusplus
}
#endif

#endif
