
#ifndef LC_APP_ABI_V2_H
#define LC_APP_ABI_V2_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LC_APP_ABI_V2 0x00020000u

#define LC_APP_API_TABLE_SIZE 48u

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

typedef struct lc_app_api_v2 {
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
} lc_app_api_v2_t;

#if UINTPTR_MAX == 0xFFFFFFFFu
#include "lc_app_abi_v2_layout_asserts.h"
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

#define LC_EVT_KIND_FRAME        1u
#define LC_EVT_KIND_MODEL_CHANGED 2u
#define LC_EVT_KIND_GAP          3u
#define LC_EVT_KIND_STOPPING     4u

#define LC_EVT_FLAG_LOST_KNOWN   0x1u
#define LC_EVT_FLAG_LOST_UNKNOWN 0x2u
#define LC_EVT_FLAGS_VALID_MASK  0x3u

#define LC_EVT_LOST_UNKNOWN_MARK 0xFFFFFFFFu

#define LC_EVT_HDR_SIZE          40u
#define LC_EVT_REC_SIZE          24u
#define LC_EVT_MAX_DETECTIONS    64u
#define LC_EVT_MAX_SIZE          (LC_EVT_HDR_SIZE + LC_EVT_MAX_DETECTIONS * LC_EVT_REC_SIZE)

#define LC_EVTH_TOTAL_LEN        0u
#define LC_EVTH_KIND             1u
#define LC_EVTH_SEQUENCE         2u
#define LC_EVTH_MONOTONIC_MS     3u
#define LC_EVTH_MODEL_GEN        4u
#define LC_EVTH_CLASS_GEN        5u
#define LC_EVTH_FLAGS            6u
#define LC_EVTH_DETECTION_COUNT  7u
#define LC_EVTH_LOST_FRAME_COUNT 8u
#define LC_EVTH_RESERVED0        9u

#define LC_EVT_REC_X_BITS        0u
#define LC_EVT_REC_Y_BITS        1u
#define LC_EVT_REC_WIDTH_BITS    2u
#define LC_EVT_REC_HEIGHT_BITS   3u
#define LC_EVT_REC_CONF_BITS     4u
#define LC_EVT_REC_CLASS_INDEX   5u

#define LC_MODEL_META_SIZE       128u
#define LC_MODEL_META_RESULT_OD  1u
#define LC_MODEL_NAME_LEN        64u
#define LC_MODEL_VERSION_LEN     32u

#define LC_RSTATE_NOT_CONFIGURED     0u
#define LC_RSTATE_PENDING            1u
#define LC_RSTATE_TRANSPORT_SUCCEEDED 2u
#define LC_RSTATE_TRANSPORT_FAILED   3u
#define LC_RSTATE_UNKNOWN            4u

#define LC_APP_EVENT_BUF_CAP     2048u
#define LC_APP_STATE_QUOTA       4096u
#define LC_APP_REPORT_MAX        6144u

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

static inline int lc_finite_f32_bits(uint32_t bits)
{
    return ((bits & 0x7F800000u) != 0x7F800000u) ? 1 : 0;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
