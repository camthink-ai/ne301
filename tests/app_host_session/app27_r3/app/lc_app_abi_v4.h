#ifndef LC_APP_ABI_V4_H
#define LC_APP_ABI_V4_H

#include <stddef.h>
#include <stdint.h>
#include "lc_app_abi_v3.h"

#define LC_APP_ABI_V4 0x00040000u
#define LC_APP_API_V4_TABLE_SIZE 64u

#define LC_CTRL_V4_PROTO_MAGIC 0x3443464Eu
#define LC_CTRL_V4_TYPE_ENABLE_SET 5u
#define LC_CTRL_V4_ENABLE_SET_SIZE 48u
#define LC_CTRL_V4_ENABLE_RECEIPT_SIZE 48u
#define LC_CTRL_V4_ENABLE_MAGIC 0x31424E45u
#define LC_CTRL_V4_RECEIPT_MAGIC 0x31524E45u
#define LC_CTRL_V4_ENABLE_UNKNOWN 3u

typedef int32_t (*lc_app_fn_live_publish_t)(const void *buf, uint32_t len);

typedef struct lc_app_api_v4 {
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
    lc_app_fn_control_next_t control_next;
    lc_app_fn_control_reply_t control_reply;
    lc_app_fn_snapshot_publish_t snapshot_publish;
    lc_app_fn_live_publish_t live_publish;
} lc_app_api_v4_t;

#if UINTPTR_MAX == 0xFFFFFFFFu
_Static_assert(sizeof(lc_app_api_v4_t) == LC_APP_API_V4_TABLE_SIZE,
               "v4 table size");
_Static_assert(offsetof(lc_app_api_v4_t, live_publish) == 60u,
               "v4 live publish offset");
#endif

#endif
