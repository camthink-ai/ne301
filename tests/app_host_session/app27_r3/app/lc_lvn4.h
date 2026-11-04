
#ifndef LC_LVN4_H
#define LC_LVN4_H

#include <stdint.h>
#include "lc_bus.h"

#define LC_LVN4_MAGIC_0 0x4Cu
#define LC_LVN4_MAGIC_1 0x56u
#define LC_LVN4_MAGIC_2 0x4Eu
#define LC_LVN4_MAGIC_3 0x34u

#define LC_LVN4_HEADER_SIZE        48u
#define LC_LVN4_HEADER_VERSION     1u
#define LC_LVN4_KIND_EVENT_BATCH   1u
#define LC_LVN4_KIND_TRACKS        2u

#define LC_LVN4_OFF_MAGIC               0u
#define LC_LVN4_OFF_HEADER_VERSION      4u
#define LC_LVN4_OFF_KIND                6u
#define LC_LVN4_OFF_TOTAL_LEN           8u
#define LC_LVN4_OFF_PACKET_SEQ          12u
#define LC_LVN4_OFF_EVENT_THROUGH_SEQ   16u
#define LC_LVN4_OFF_CUMULATIVE_TOTAL    20u
#define LC_LVN4_OFF_CUMULATIVE_GAP      24u
#define LC_LVN4_OFF_SOURCE_TS_MS        28u
#define LC_LVN4_OFF_CONFIG_GENERATION   32u
#define LC_LVN4_OFF_RECORD_COUNT        36u
#define LC_LVN4_OFF_FLAGS               40u
#define LC_LVN4_OFF_RESERVED            44u

#define LC_LVN4_FLAG_SOURCE_GAP_DELTA   0x1u
#define LC_LVN4_FLAG_SOURCE_LOSS_UNKNOWN 0x2u
#define LC_LVN4_FLAG_RESYNC_REQUIRED    0x4u
#define LC_LVN4_FLAGS_VALID_MASK        0x7u

#define LC_LVN4_EVENT_RECORD_SIZE  16u
#define LC_LVN4_EVENT_MAX          16u
#define LC_LVN4_DIR_IN             1u
#define LC_LVN4_DIR_OUT            2u
#define LC_LVN4_EVT_FLAG_SOURCE_UNCERTAIN 0x1u
#define LC_LVN4_EVT_FLAGS_VALID_MASK      0x1u

#define LC_LVN4_TRACK_HEADER_SIZE  12u
#define LC_LVN4_TRACK_POINT_SIZE   8u
#define LC_LVN4_TRACK_POINTS_MAX   16u
#define LC_LVN4_TRACKS_MAX         64u
#define LC_LVN4_TRACK_STATE_ACTIVE 1u
#define LC_LVN4_CLASS_UNKNOWN      0xFFFFu

#define LC_LVN4_EVENT_BATCH_MAX_LEN \
    (LC_LVN4_HEADER_SIZE + (uint32_t)LC_LVN4_EVENT_MAX * LC_LVN4_EVENT_RECORD_SIZE)

#define LC_LVN4_MIN_PUBLISH_MAX    312u
#define LC_LVN4_PROFILE_PUBLISH_MAX 9008u
#define LC_LVN4_PROFILE_TRACK_MAX  64u

#define LC_LVN4_PKT_MAX \
    (LC_LVN4_HEADER_SIZE + \
     (uint32_t)LC_LVN4_TRACKS_MAX * (LC_LVN4_TRACK_HEADER_SIZE + \
                                     (uint32_t)LC_LVN4_TRACK_POINTS_MAX * LC_LVN4_TRACK_POINT_SIZE))

typedef enum {
    LC_LVN4_STATE_UNARMED = 0,
    LC_LVN4_STATE_ARMED,
    LC_LVN4_STATE_UNAVAILABLE_NO_CAPABILITY,
    LC_LVN4_STATE_UNAVAILABLE_BAD_PROFILE,
    LC_LVN4_STATE_UNAVAILABLE_ENTRY_REUSED,
    LC_LVN4_STATE_UNAVAILABLE_TRACK_CAPACITY,
    LC_LVN4_STATE_UNAVAILABLE_CONFIG_GEN,
    LC_LVN4_STATE_UNAVAILABLE_PUBLISH,
    LC_LVN4_STATE_UNAVAILABLE_EXHAUSTED,
    LC_LVN4_STATE_DISARMED
} lc_lvn4_state_t;

typedef enum {
    LC_LVN4_GATE_OPEN = 0,
    LC_LVN4_GATE_PAUSED,
    LC_LVN4_GATE_UNKNOWN
} lc_lvn4_gate_t;

typedef int32_t (*lc_lvn4_publish_fn)(void *user, const void *buf, uint32_t len);

typedef struct lc_lvn4_profile {
    uint32_t entry_epoch;
    uint32_t publish_max;
    uint32_t track_max;
} lc_lvn4_profile_t;

typedef struct lc_lvn4_stats {
    uint32_t packets_event;
    uint32_t packets_tracks;
    uint32_t publish_ok;
    uint32_t publish_failures;
    uint32_t encoded_events;
    uint32_t truncated_events;
    uint32_t gate_held_events;
    uint32_t gap_events;
    uint32_t track_snapshots;
    uint32_t track_cleared;
} lc_lvn4_stats_t;

typedef struct lc_lvn4 {
    lcbus_t            *bus;
    lc_lvn4_publish_fn  publish;
    void               *publish_user;
    uint32_t            entry_epoch;
    uint32_t            live_publish_max;
    uint32_t            live_track_max;
    lc_lvn4_state_t     state;
    lc_lvn4_gate_t      gate;
    uint32_t            packet_seq;
    uint32_t            through_seq;
    uint32_t            cumulative_total;
    uint32_t            cumulative_gap;
    uint32_t            last_pub_gap;
    uint32_t            bound_model_gen;
    uint32_t            bound_class_gen;
    int32_t             bound_target_index;
    uint8_t             armed_ever;
    uint8_t             loss_unknown_latch;
    uint8_t             resync_required;
    uint8_t             pkt[LC_LVN4_PKT_MAX];
    lc_lvn4_stats_t     st;
} lc_lvn4_t;

void lc_lvn4_init(lc_lvn4_t *p, lcbus_t *bus,
                  lc_lvn4_publish_fn publish, void *publish_user);

int lc_lvn4_arm(lc_lvn4_t *p, const lc_lvn4_profile_t *profile);

void lc_lvn4_disarm(lc_lvn4_t *p);

void lc_lvn4_set_gate(lc_lvn4_t *p, lc_lvn4_gate_t gate);

void lc_lvn4_observe(lc_lvn4_t *p, const lcbus_live_facts_t *facts);

lc_lvn4_state_t lc_lvn4_state(const lc_lvn4_t *p);

const lc_lvn4_stats_t *lc_lvn4_stats(const lc_lvn4_t *p);

#endif
