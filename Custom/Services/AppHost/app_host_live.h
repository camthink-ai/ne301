#ifndef APP_HOST_LIVE_H
#define APP_HOST_LIVE_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"
#include "app_admission.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_HOST_LIVE_MAGIC 0x344E564Cu
#define APP_HOST_LIVE_HEADER_VERSION 1u
#define APP_HOST_LIVE_HEADER_SIZE APP_ADMISSION_V4_LIVE_HEADER_SIZE
#define APP_HOST_LIVE_EVENT_RECORD_SIZE APP_ADMISSION_V4_LIVE_EVENT_RECORD_SIZE
#define APP_HOST_LIVE_TRACK_HEADER_SIZE APP_ADMISSION_V4_LIVE_TRACK_HEADER_SIZE
#define APP_HOST_LIVE_TRACK_POINT_SIZE APP_ADMISSION_V4_LIVE_TRACK_POINT_SIZE
#define APP_HOST_LIVE_TRACK_POINT_MAX APP_ADMISSION_V4_LIVE_TRACK_POINT_MAX
#define APP_HOST_LIVE_TRACK_MAX APP_ADMISSION_V4_LIVE_TRACK_MAX
#define APP_HOST_LIVE_KIND_EVENT_BATCH 1u
#define APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT 2u
#define APP_HOST_LIVE_EVENT_RECORD_MAX 16u
#define APP_HOST_LIVE_PUBLISH_CAPACITY \
    (APP_HOST_LIVE_HEADER_SIZE + \
     ((uint32_t)APP_HOST_LIVE_TRACK_MAX * \
      (APP_HOST_LIVE_TRACK_HEADER_SIZE + \
       ((uint32_t)APP_HOST_LIVE_TRACK_POINT_MAX * APP_HOST_LIVE_TRACK_POINT_SIZE))))
#define APP_HOST_LIVE_TRACK_CAPACITY APP_HOST_LIVE_TRACK_MAX
#define APP_HOST_LIVE_FLAG_SOURCE_GAP_DELTA 0x00000001u
#define APP_HOST_LIVE_FLAG_SOURCE_LOSS_UNKNOWN 0x00000002u
#define APP_HOST_LIVE_FLAG_RESYNC_REQUIRED 0x00000004u
#define APP_HOST_LIVE_FLAG_KNOWN_MASK \
    (APP_HOST_LIVE_FLAG_SOURCE_GAP_DELTA | \
     APP_HOST_LIVE_FLAG_SOURCE_LOSS_UNKNOWN | \
     APP_HOST_LIVE_FLAG_RESYNC_REQUIRED)
#define APP_HOST_LIVE_EVENT_FLAG_SOURCE_UNCERTAIN 0x01u
#define APP_HOST_LIVE_EVENT_FLAG_KNOWN_MASK APP_HOST_LIVE_EVENT_FLAG_SOURCE_UNCERTAIN
#define APP_HOST_LIVE_DIRECTION_IN 1u
#define APP_HOST_LIVE_DIRECTION_OUT 2u
#define APP_HOST_LIVE_TRACK_STATE_ACTIVE 1u
#define APP_HOST_LIVE_CLASS_UNKNOWN 0xFFFFu
#define APP_HOST_LIVE_EVENT_RING_SLOTS 64u
#define APP_HOST_LIVE_STALE_MS 10000u
#define APP_HOST_LIVE_IDENTITY_SLOT 48u

#define APP_HOST_LIVE_OK 0
#define APP_HOST_LIVE_ERR_PARAM (-1)
#define APP_HOST_LIVE_ERR_UNBOUND (-2)
#define APP_HOST_LIVE_ERR_GENERATION (-3)
#define APP_HOST_LIVE_ERR_LENGTH (-4)
#define APP_HOST_LIVE_ERR_HEADER (-5)
#define APP_HOST_LIVE_ERR_SEQUENCE (-6)
#define APP_HOST_LIVE_ERR_WATERMARK (-7)
#define APP_HOST_LIVE_ERR_CONFIG_GENERATION (-8)
#define APP_HOST_LIVE_ERR_RECORD (-9)
#define APP_HOST_LIVE_ERR_OVERFLOW (-10)
#define APP_HOST_LIVE_ERR_CLOSED (-11)

typedef enum {
    APP_HOST_LIVE_FRESHNESS_NONE = 0,
    APP_HOST_LIVE_FRESHNESS_FRESH = 1,
    APP_HOST_LIVE_FRESHNESS_STALE = 2
} app_host_live_freshness_t;

typedef enum {
    APP_HOST_LIVE_READ_STATE_UNKNOWN = 0,
    APP_HOST_LIVE_READ_STATE_CONTINUOUS = 1,
    APP_HOST_LIVE_READ_STATE_GAPPED = 2
} app_host_live_read_state_t;

typedef struct {
    void *ctx;
    uint32_t (*now_ms)(void *ctx);
    uint64_t (*epoch_ms)(void *ctx);
    void *(*mutex_create)(void *ctx);
    void (*mutex_destroy)(void *ctx, void *mtx);
    int (*mutex_lock)(void *ctx, void *mtx);
    void (*mutex_unlock)(void *ctx, void *mtx);
} app_host_live_backend_t;

typedef struct {
    uint32_t session_generation;
    char publisher_id[APP_HOST_LIVE_IDENTITY_SLOT];
    char app_id[APP_HOST_LIVE_IDENTITY_SLOT];
    uint8_t spki_sha256[APP_ADMISSION_SHA256_LEN];
    uint8_t tbs_sha256[APP_ADMISSION_SHA256_LEN];
    uint8_t package_sha256[APP_ADMISSION_SHA256_LEN];
    uint32_t live_publish_max;
    uint32_t live_track_max;
} app_host_live_identity_t;

typedef struct {
    uint64_t cursor;
    uint32_t event_seq;
    uint32_t track_id;
    uint64_t event_ts_ms;
    uint8_t direction;
    uint8_t flags;
    uint8_t x_q8;
    uint8_t y_q8;
} app_host_live_event_t;

typedef struct {
    uint32_t ts_ms;
    uint16_t x_q16;
    uint16_t y_q16;
} app_host_live_point_t;

typedef struct {
    uint32_t track_id;
    uint16_t point_count;
    uint16_t class_index;
    uint8_t state;
    uint8_t flags;
    uint16_t confidence_q16;
    app_host_live_point_t points[APP_HOST_LIVE_TRACK_POINT_MAX];
} app_host_live_track_t;

typedef struct {
    uint64_t next_cursor;
    uint32_t returned;
    uint64_t oldest_cursor;
    uint64_t newest_cursor;
    uint64_t host_dropped_events;
    uint32_t through_seq;
    uint32_t source_total;
    uint32_t source_gap;
    uint32_t packet_count;
    uint32_t last_packet_seq;
    app_host_live_read_state_t state;
    app_host_live_freshness_t freshness;
    uint32_t age_ms;
    aicam_bool_t loss_unknown;
    aicam_bool_t resync_required;
    uint32_t session_generation;
    uint32_t view_epoch;
} app_host_live_read_out_t;

typedef struct {
    uint32_t track_count;
    uint32_t packet_seq;
    uint32_t age_ms;
    app_host_live_freshness_t freshness;
    aicam_bool_t loss_unknown;
    aicam_bool_t resync_required;
    uint32_t through_seq;
    uint32_t source_total;
    uint32_t source_gap;
    uint64_t host_dropped_events;
    uint32_t session_generation;
    uint32_t view_epoch;
} app_host_live_snapshot_out_t;

typedef struct {
    aicam_bool_t bound;
    aicam_bool_t closed;
    uint32_t session_generation;
    uint32_t view_epoch;
    char publisher_id[APP_HOST_LIVE_IDENTITY_SLOT];
    char app_id[APP_HOST_LIVE_IDENTITY_SLOT];
    uint8_t spki_sha256[APP_ADMISSION_SHA256_LEN];
    uint8_t tbs_sha256[APP_ADMISSION_SHA256_LEN];
    uint8_t package_sha256[APP_ADMISSION_SHA256_LEN];
    uint32_t live_publish_max;
    uint32_t live_track_max;
    uint32_t packet_count;
    uint32_t event_count;
    uint32_t snapshot_count;
    uint32_t idempotent_replays;
    uint32_t rejected_total;
    uint32_t rejected_length;
    uint32_t rejected_header;
    uint32_t rejected_sequence;
    uint32_t rejected_watermark;
    uint32_t rejected_config_generation;
    uint32_t rejected_record;
    uint32_t rejected_overflow;
    uint64_t host_dropped_events;
    uint32_t last_packet_seq;
    uint32_t through_seq;
    uint32_t source_total;
    uint32_t source_gap;
    uint32_t retained_events;
    uint32_t snapshot_tracks;
    aicam_bool_t loss_unknown;
    aicam_bool_t resync_required;
    app_host_live_freshness_t freshness;
    uint32_t age_ms;
} app_host_live_status_t;

void app_host_live_init(const app_host_live_backend_t *backend);

int32_t app_host_live_bind(const app_host_live_identity_t *identity);

void app_host_live_end(uint32_t session_generation);

int32_t app_host_live_ingest(const void *buf, uint32_t len,
                             uint32_t session_generation,
                             uint32_t trusted_class_count);

int32_t app_host_live_read(uint32_t session_generation, uint64_t after_cursor,
                           app_host_live_event_t *out, uint32_t cap,
                           app_host_live_read_out_t *out_info);

int32_t app_host_live_snapshot_read(uint32_t session_generation,
                                    app_host_live_track_t *out, uint32_t cap,
                                    app_host_live_snapshot_out_t *out_info);

void app_host_live_status(app_host_live_status_t *out);

uint64_t app_host_live_epoch_ms(void);

const char *app_host_live_result_str(int32_t result);

const char *app_host_live_read_state_str(app_host_live_read_state_t state);

const char *app_host_live_freshness_str(app_host_live_freshness_t freshness);

#ifdef __cplusplus
}
#endif

#endif
