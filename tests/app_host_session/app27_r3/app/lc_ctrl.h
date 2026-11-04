
#ifndef LC_CTRL_H
#define LC_CTRL_H

#include <stdint.h>
#include "lc_app_abi_v3.h"
#include "lc_app_abi_v4.h"
#include "lc_bus.h"

#define LC_CTRL_APP_STATE_RUNNING    0u
#define LC_CTRL_APP_STATE_UNBOUND    1u
#define LC_CTRL_APP_STATE_RESTRICTED 2u

#define LC_CTRL_STORAGE_OK      0u
#define LC_CTRL_STORAGE_NONE    1u
#define LC_CTRL_STORAGE_DEGRADED 2u
#define LC_CTRL_STORAGE_CORRUPT 3u
#define LC_CTRL_STORAGE_CONFLICT 4u
#define LC_CTRL_STORAGE_UNKNOWN 5u

typedef struct lc_ctrl_dedup_slot {
    uint8_t  in_use;
    uint32_t request_id;
    uint32_t cmd_len;
    uint8_t  cmd[LC_CTRL_MSG_BUF_CAP];
    uint32_t result_status;
    uint32_t result_generation;
    uint32_t result_revision;
    uint32_t result_enable_state;
} lc_ctrl_dedup_slot_t;

typedef struct lc_ctrl_stats {
    uint32_t commands_received;
    uint32_t wire_rejected;
    uint32_t dedup_cached;
    uint32_t dedup_conflict;
    uint32_t rejected_stale_config;
    uint32_t rejected_invalid;
    uint32_t rejected_resource;
    uint32_t rejected_unconfirmed;
    uint32_t failed_state_conflict;
    uint32_t failed_storage_io;
    uint32_t failed_storage_unknown;
    uint32_t applied;
    uint32_t replies_sent;
    uint32_t snapshots_published;
    uint32_t snapshots_rejected;
} lc_ctrl_stats_t;

typedef struct lc_ctrl {
    lcbus_t *bus;
    lc_app_fn_control_next_t control_next;
    lc_app_fn_control_reply_t control_reply;
    lc_app_fn_snapshot_publish_t snapshot_publish;
    uint32_t session_generation;
    uint32_t last_snapshot_ms;
    uint8_t  last_snapshot_valid;
    uint8_t  snapshot_forced;
    uint8_t  protocol_major;
    uint8_t  session_generation_valid;
    uint32_t dedup_cursor;
    lc_ctrl_dedup_slot_t dedup[LC_CTRL_DEDUP_SLOTS];
    lc_ctrl_stats_t st;
} lc_ctrl_t;

void lc_ctrl_init(lc_ctrl_t *c, lcbus_t *bus, const lc_app_api_v3_t *api);

void lc_ctrl_init_v4(lc_ctrl_t *c, lcbus_t *bus, const lc_app_api_v4_t *api);

void lc_ctrl_poll(lc_ctrl_t *c);

void lc_ctrl_snapshot_force(lc_ctrl_t *c);

#endif
