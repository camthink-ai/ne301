
#ifndef LC_BUS_H
#define LC_BUS_H

#include <stdint.h>
#include "lc_app_abi_v2.h"
#include "lc_bus_config.h"
#include "lc_tracker.h"

#ifndef LC_APP_ID
#define LC_APP_ID "line-crossing"
#endif
#ifndef LC_APP_SW_VERSION
#define LC_APP_SW_VERSION "1.0.0"
#endif
#define LC_APP_DEVICE_ID "ne301-app:" LC_APP_ID

#define LCBUS_REPORT_MAX_TRACKS 12u
#define LCBUS_REPORT_MAX_POINTS 8u
#define LCBUS_HEAT_DIM          16u
#define LCBUS_HEAT_SIZE         (LCBUS_HEAT_DIM * LCBUS_HEAT_DIM)

#define LCBUS_FLUSH_INTERVAL_MS  5000u
#define LCBUS_REBIND_RETRY_MS    1000u

typedef struct lcbus_host_ops {
    void    *user;
    int32_t (*model_meta)(void *user, uint8_t *out128);
    int32_t (*class_name)(void *user, uint32_t model_gen, uint32_t class_gen,
                          uint32_t class_index, char *out_utf8, uint32_t cap,
                          uint32_t *actual_len);
    uint32_t (*tick_ms)(void *user);
    int32_t (*state_read)(void *user, uint8_t *out, uint32_t cap,
                          uint32_t *actual_len, uint32_t *out_revision);
    int32_t (*state_commit)(void *user, const uint8_t *blob, uint32_t len,
                            uint32_t expected_revision, uint32_t *new_revision);
    int32_t (*report_submit)(void *user, const uint8_t *json, uint32_t len,
                             uint32_t app_report_seq, uint32_t *out_host_boot_id);
    int32_t (*report_status)(void *user, uint32_t host_boot_id, uint32_t report_seq,
                             uint32_t *out_mqtt_state, uint32_t *out_webhook_state);
    void    (*log)(void *user, const char *text);
} lcbus_host_ops_t;

const lcbus_host_ops_t *lcbus_abi_ops(void);

typedef enum {
    LCBUS_MSTATE_RUNNING = 0,
    LCBUS_MSTATE_UNSUPPORTED_MODEL,
    LCBUS_MSTATE_TARGET_CLASS_INVALID
} lcbus_model_state_t;

typedef enum {
    LCBUS_PERSIST_OK = 0,
    LCBUS_PERSIST_NONE,
    LCBUS_PERSIST_DEGRADED,
    LCBUS_PERSIST_CORRUPT,
    LCBUS_PERSIST_CONFLICT
} lcbus_persist_state_t;

typedef enum {
    LCBUS_ENABLE_UNKNOWN = 0,
    LCBUS_ENABLE_ENABLED = 1,
    LCBUS_ENABLE_PAUSED = 2
} lcbus_enable_state_t;

typedef struct lcbus_binding {
    uint8_t  bound;
    uint32_t model_generation;
    uint32_t class_generation;
    uint32_t class_count;
    int32_t  target_index;
    char     target_name[LC_TARGET_CLASS_NAME_LEN];
    char     model_name[LC_MODEL_NAME_LEN];
    char     model_version[LC_MODEL_VERSION_LEN];
} lcbus_binding_t;

typedef struct lcbus_stats {

    uint32_t frames_total;
    uint32_t frames_empty;
    uint32_t frames_unusable;
    uint32_t no_event_polls;
    uint32_t host_faults;
    uint32_t malformed_events;
    uint32_t gaps_total;
    uint32_t lost_frames_total;
    uint32_t lost_unknown_events;
    uint32_t model_rebinds;
    uint32_t alloc_failures;

    uint32_t reports_submitted;
    uint32_t reports_dropped;
    uint32_t reports_failed_transport;
    uint32_t reports_delivered_mqtt;
    uint32_t reports_delivered_webhook;
    uint32_t reports_status_unresolved;

    uint32_t state_commits_ok;
    uint32_t state_commit_failures;
    uint32_t state_conflicts;
} lcbus_stats_t;

#define LCBUS_PENDING_MAX 8

typedef struct lcbus_pending {
    uint32_t seq;
    uint32_t polls;
    uint8_t  in_use;
    uint8_t  mqtt_done;
    uint8_t  web_done;
} lcbus_pending_t;

typedef enum {
    LCBUS_TXN_OK = 0,
    LCBUS_TXN_ERR_INVALID = -1,
    LCBUS_TXN_ERR_RESOURCE = -2,
    LCBUS_TXN_ERR_STATE_CONFLICT = -3,
    LCBUS_TXN_ERR_STORAGE_IO = -4,
    LCBUS_TXN_ERR_STORAGE_UNKNOWN = -5,
    LCBUS_TXN_ERR_STALE_CONFIG = -6
} lcbus_txn_result_t;

typedef struct lcbus_cfg_txn {
    lc_bus_config_t  candidate;
    lcbus_binding_t  new_binding;
    lc_line_cross_t *new_line;
    lc_tracker_t    *new_tracker;
    uint32_t         tracker_carry;
    int32_t          target_index;
    uint8_t          target_changed;
    uint8_t          line_changed;
    uint8_t          tracker_changed;
} lcbus_cfg_txn_t;

#define LCBUS_LIVE_MAX_EVENTS 16u

typedef struct lcbus_live_facts {
    uint32_t       mono_ms;
    uint8_t        kind_frame;
    uint8_t        frame_complete;
    uint8_t        frame_dropped;
    uint8_t        is_gap;
    uint8_t        lost_unknown;
    uint8_t        n_evts;
    uint32_t       frame_in;
    uint32_t       frame_out;
    lc_cross_evt_t evts[LCBUS_LIVE_MAX_EVENTS];
} lcbus_live_facts_t;

typedef void (*lcbus_live_observer_t)(void *user, const lcbus_live_facts_t *facts);

typedef struct lcbus {
    lcbus_host_ops_t      ops;
    lc_bus_config_t       cfg;
    lcbus_binding_t       binding;
    lcbus_model_state_t   model_state;
    lcbus_persist_state_t persist_state;
    lcbus_enable_state_t  enable_state;

    lc_tracker_t   *tracker;
    lc_line_cross_t *line;
    uint32_t        next_id_carry;
    uint32_t        consecutive_alloc_failures;
    uint32_t        last_frame_received_ms;
    uint8_t         last_frame_received_valid;

    uint32_t window_in, window_out, total_in, total_out;
    uint32_t window_start_ms;
    uint8_t  window_carried;
    uint32_t carried_in, carried_out;

    uint32_t gaps_window, lost_window;
    uint8_t  lost_unknown_window;
    uint32_t unusable_window;

    uint32_t report_seq;
    uint32_t host_boot_id;
    uint8_t  host_boot_known;

    lcbus_pending_t pending[LCBUS_PENDING_MAX];

    uint8_t  state_dirty;
    uint8_t  business_mutated;
    uint8_t  corrupt_reset_armed;
    uint32_t revision;
    uint32_t last_flush_ms;
    uint8_t  last_flush_valid;
    uint32_t last_rebind_ms;
    uint8_t  last_rebind_valid;

    uint8_t  state_schema;
    uint32_t config_generation;
    uint8_t  storage_unknown_latch;

    uint32_t heat[LCBUS_HEAT_SIZE];

    lcbus_stats_t st;
    uint8_t  session_fatal;
    uint8_t  resource_fatal;
    uint8_t  stop_requested;

    lcbus_live_observer_t live_observer;
    void                 *live_observer_user;
} lcbus_t;

void lcbus_set_live_observer(lcbus_t *b, lcbus_live_observer_t fn, void *user);

void lcbus_init(lcbus_t *b, const lcbus_host_ops_t *ops);

void lcbus_restore(lcbus_t *b);

void lcbus_rebind(lcbus_t *b, int force);

void lcbus_on_event(lcbus_t *b, const uint8_t *ev, uint32_t len);

void lcbus_on_idle(lcbus_t *b);

void lcbus_flush(lcbus_t *b, int force);

int  lcbus_apply_config(lcbus_t *b, const lc_bus_config_t *candidate);

void lcbus_reset(lcbus_t *b);

int  lcbus_txn_prepare_config(lcbus_t *b, const lc_bus_config_t *candidate,
                              lcbus_cfg_txn_t *txn);

int  lcbus_txn_commit_config(lcbus_t *b, lcbus_cfg_txn_t *txn);

void lcbus_txn_abort_config(lcbus_t *b, lcbus_cfg_txn_t *txn);

int  lcbus_txn_commit_reset(lcbus_t *b, int allow_corrupt_replace);

int  lcbus_enable_set(lcbus_t *b, uint32_t desired_state,
                      uint32_t expected_config_generation,
                      uint32_t expected_state_revision,
                      uint32_t *out_revision);

int  lcbus_storage_restricted(const lcbus_t *b);

#endif
