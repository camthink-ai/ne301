#include <string.h>
#include <stdint.h>
#include <time.h>

#include "app_host_live.h"

_Static_assert(APP_HOST_LIVE_HEADER_SIZE == 48u, "live wire header size");
_Static_assert(APP_HOST_LIVE_EVENT_RECORD_SIZE == 16u, "live event record size");
_Static_assert(APP_HOST_LIVE_TRACK_HEADER_SIZE == 12u, "live track header size");
_Static_assert(APP_HOST_LIVE_TRACK_POINT_SIZE == 8u, "live track point size");
_Static_assert(APP_HOST_LIVE_PUBLISH_CAPACITY == 9008u, "live publish capacity closure");

typedef struct {
    app_host_live_event_t event;
    uint64_t cursor;
} live_ring_slot_t;

typedef struct {
    aicam_bool_t bound;
    aicam_bool_t closed;
    uint32_t session_generation;
    char publisher_id[APP_HOST_LIVE_IDENTITY_SLOT];
    char app_id[APP_HOST_LIVE_IDENTITY_SLOT];
    uint8_t spki_sha256[APP_ADMISSION_SHA256_LEN];
    uint8_t tbs_sha256[APP_ADMISSION_SHA256_LEN];
    uint8_t package_sha256[APP_ADMISSION_SHA256_LEN];
    uint32_t live_publish_max;
    uint32_t live_track_max;
    aicam_bool_t seen_packet;
    uint32_t last_packet_seq;
    uint32_t last_packet_len;
    uint8_t last_packet[APP_HOST_LIVE_PUBLISH_CAPACITY];
    uint32_t last_through_seq;
    uint32_t last_source_total;
    uint32_t last_source_gap;
    aicam_bool_t event_watermark_valid;
    uint32_t last_event_seq;
    uint32_t config_generation;
    aicam_bool_t config_generation_valid;
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
    uint32_t ring_head;
    uint32_t ring_count;
    live_ring_slot_t ring[APP_HOST_LIVE_EVENT_RING_SLOTS];
    uint64_t next_cursor;
    app_host_live_track_t snapshot[APP_HOST_LIVE_TRACK_MAX];
    uint32_t snapshot_track_count;
    uint32_t snapshot_packet_seq;
    aicam_bool_t snapshot_valid;
    aicam_bool_t loss_unknown;
    aicam_bool_t resync_required;
    uint32_t last_ingest_ms;
    aicam_bool_t have_ingest_ms;
} live_state_t;

static app_host_live_backend_t g_live_backend;
static void *g_live_mtx;
static live_state_t g_live;
static uint32_t g_live_view_epoch;

static void lv_lock(void)
{
    if (g_live_backend.mutex_lock != NULL && g_live_mtx != NULL) {
        g_live_backend.mutex_lock(g_live_backend.ctx, g_live_mtx);
    }
}

static void lv_unlock(void)
{
    if (g_live_backend.mutex_unlock != NULL && g_live_mtx != NULL) {
        g_live_backend.mutex_unlock(g_live_backend.ctx, g_live_mtx);
    }
}

static uint32_t lv_now_ms(void)
{
    if (g_live_backend.now_ms != NULL) {
        return g_live_backend.now_ms(g_live_backend.ctx);
    }
    return 0u;
}

static uint64_t lv_epoch_ms(void)
{
    if (g_live_backend.epoch_ms != NULL) {
        return g_live_backend.epoch_ms(g_live_backend.ctx);
    }
    return (uint64_t)time(NULL) * 1000u;
}

static uint32_t lv_get_u16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t lv_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static app_host_live_freshness_t lv_freshness_locked(uint32_t now_ms,
                                                     uint32_t *age_ms)
{
    if (g_live.have_ingest_ms != AICAM_TRUE) {
        (*age_ms) = 0u;
        return APP_HOST_LIVE_FRESHNESS_NONE;
    }
    (*age_ms) = now_ms - g_live.last_ingest_ms;
    return ((*age_ms) > APP_HOST_LIVE_STALE_MS) ? APP_HOST_LIVE_FRESHNESS_STALE
                                                : APP_HOST_LIVE_FRESHNESS_FRESH;
}

static void lv_reset_locked(void)
{
    live_state_t *s = &g_live;

    s->bound = AICAM_FALSE;
    s->closed = AICAM_FALSE;
    s->session_generation = 0u;
    s->publisher_id[0] = '\0';
    s->app_id[0] = '\0';
    memset(s->spki_sha256, 0, sizeof(s->spki_sha256));
    memset(s->tbs_sha256, 0, sizeof(s->tbs_sha256));
    memset(s->package_sha256, 0, sizeof(s->package_sha256));
    s->live_publish_max = 0u;
    s->live_track_max = 0u;
    s->seen_packet = AICAM_FALSE;
    s->last_packet_seq = 0u;
    s->last_packet_len = 0u;
    memset(s->last_packet, 0, sizeof(s->last_packet));
    s->last_through_seq = 0u;
    s->last_source_total = 0u;
    s->last_source_gap = 0u;
    s->event_watermark_valid = AICAM_FALSE;
    s->last_event_seq = 0u;
    s->config_generation = 0u;
    s->config_generation_valid = AICAM_FALSE;
    s->packet_count = 0u;
    s->event_count = 0u;
    s->snapshot_count = 0u;
    s->idempotent_replays = 0u;
    s->rejected_total = 0u;
    s->rejected_length = 0u;
    s->rejected_header = 0u;
    s->rejected_sequence = 0u;
    s->rejected_watermark = 0u;
    s->rejected_config_generation = 0u;
    s->rejected_record = 0u;
    s->rejected_overflow = 0u;
    s->host_dropped_events = 0u;
    s->ring_head = 0u;
    s->ring_count = 0u;
    memset(s->ring, 0, sizeof(s->ring));
    s->next_cursor = 1u;
    memset(s->snapshot, 0, sizeof(s->snapshot));
    s->snapshot_track_count = 0u;
    s->snapshot_packet_seq = 0u;
    s->snapshot_valid = AICAM_FALSE;
    s->loss_unknown = AICAM_FALSE;
    s->resync_required = AICAM_FALSE;
    s->last_ingest_ms = 0u;
    s->have_ingest_ms = AICAM_FALSE;
}

void app_host_live_init(const app_host_live_backend_t *backend)
{
    if (backend != NULL) {
        g_live_backend = *backend;
    } else {
        memset(&g_live_backend, 0, sizeof(g_live_backend));
    }
    if (g_live_backend.mutex_create != NULL) {
        g_live_mtx = g_live_backend.mutex_create(g_live_backend.ctx);
    } else {
        g_live_mtx = NULL;
    }
    lv_lock();
    lv_reset_locked();
    g_live_view_epoch++;
    lv_unlock();
}

static void lv_copy_id(char *dst, size_t cap, const char *src)
{
    size_t i = 0u;

    while (i + 1u < cap && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int32_t app_host_live_bind(const app_host_live_identity_t *identity)
{
    uint64_t closed_max;
    int32_t rc = APP_HOST_LIVE_OK;

    if (identity == NULL || identity->session_generation == 0u ||
        identity->publisher_id[0] == '\0' || identity->app_id[0] == '\0') {
        return APP_HOST_LIVE_ERR_PARAM;
    }
    if (identity->live_publish_max < APP_ADMISSION_V4_LIVE_MIN_PUBLISH ||
        identity->live_track_max == 0u ||
        identity->live_track_max > APP_HOST_LIVE_TRACK_CAPACITY ||
        identity->live_publish_max > APP_HOST_LIVE_PUBLISH_CAPACITY) {
        return APP_HOST_LIVE_ERR_PARAM;
    }
    closed_max = (uint64_t)APP_HOST_LIVE_HEADER_SIZE +
                 (uint64_t)identity->live_track_max *
                     ((uint64_t)APP_HOST_LIVE_TRACK_HEADER_SIZE +
                      (uint64_t)APP_HOST_LIVE_TRACK_POINT_MAX *
                          APP_HOST_LIVE_TRACK_POINT_SIZE);
    if ((uint64_t)identity->live_publish_max < closed_max) {
        return APP_HOST_LIVE_ERR_PARAM;
    }
    lv_lock();
    if (g_live.bound == AICAM_TRUE) {
        rc = APP_HOST_LIVE_ERR_UNBOUND;
    } else {
        lv_reset_locked();
        g_live.bound = AICAM_TRUE;
        g_live.session_generation = identity->session_generation;
        lv_copy_id(g_live.publisher_id, sizeof(g_live.publisher_id),
                   identity->publisher_id);
        lv_copy_id(g_live.app_id, sizeof(g_live.app_id), identity->app_id);
        memcpy(g_live.spki_sha256, identity->spki_sha256,
               sizeof(g_live.spki_sha256));
        memcpy(g_live.tbs_sha256, identity->tbs_sha256,
               sizeof(g_live.tbs_sha256));
        memcpy(g_live.package_sha256, identity->package_sha256,
               sizeof(g_live.package_sha256));
        g_live.live_publish_max = identity->live_publish_max;
        g_live.live_track_max = identity->live_track_max;
    }
    lv_unlock();
    return rc;
}

void app_host_live_end(uint32_t session_generation)
{
    lv_lock();
    if (g_live.bound == AICAM_TRUE &&
        g_live.session_generation == session_generation) {
        lv_reset_locked();
    }
    lv_unlock();
}

static int32_t lv_reject_locked(uint32_t *counter, int32_t code)
{
    if (counter != NULL) {
        (*counter)++;
    }
    g_live.rejected_total++;
    return code;
}

static int32_t lv_validate_header_locked(const uint8_t *p, uint32_t len,
                                         uint32_t *kind, uint32_t *record_count,
                                         uint32_t *packet_seq,
                                         uint32_t *through_seq,
                                         uint32_t *source_total,
                                         uint32_t *source_gap,
                                         uint32_t *config_generation,
                                         uint32_t *flags)
{
    if (len < APP_HOST_LIVE_HEADER_SIZE) {
        return lv_reject_locked(&g_live.rejected_length, APP_HOST_LIVE_ERR_LENGTH);
    }
    if (len > g_live.live_publish_max) {
        return lv_reject_locked(&g_live.rejected_length, APP_HOST_LIVE_ERR_LENGTH);
    }
    if (lv_get_u32(p + 0u) != APP_HOST_LIVE_MAGIC ||
        lv_get_u16(p + 4u) != APP_HOST_LIVE_HEADER_VERSION ||
        (lv_get_u16(p + 6u) != APP_HOST_LIVE_KIND_EVENT_BATCH &&
         lv_get_u16(p + 6u) != APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT)) {
        return lv_reject_locked(&g_live.rejected_header, APP_HOST_LIVE_ERR_HEADER);
    }
    if (lv_get_u32(p + 8u) != len ||
        lv_get_u32(p + 44u) != 0u ||
        (lv_get_u32(p + 40u) & ~APP_HOST_LIVE_FLAG_KNOWN_MASK) != 0u) {
        return lv_reject_locked(&g_live.rejected_header, APP_HOST_LIVE_ERR_HEADER);
    }
    (*kind) = lv_get_u16(p + 6u);
    (*record_count) = lv_get_u32(p + 36u);
    (*packet_seq) = lv_get_u32(p + 12u);
    (*through_seq) = lv_get_u32(p + 16u);
    (*source_total) = lv_get_u32(p + 20u);
    (*source_gap) = lv_get_u32(p + 24u);
    (*config_generation) = lv_get_u32(p + 32u);
    (*flags) = lv_get_u32(p + 40u);
    return APP_HOST_LIVE_OK;
}

static int32_t lv_validate_events_locked(const uint8_t *p, uint32_t len,
                                         uint32_t record_count,
                                         uint32_t through_seq)
{
    uint32_t i;
    uint32_t prev_seq = 0u;
    aicam_bool_t first = AICAM_TRUE;

    if (record_count > APP_HOST_LIVE_EVENT_RECORD_MAX) {
        return lv_reject_locked(&g_live.rejected_overflow,
                                APP_HOST_LIVE_ERR_OVERFLOW);
    }
    if ((uint64_t)APP_HOST_LIVE_HEADER_SIZE +
            (uint64_t)record_count * APP_HOST_LIVE_EVENT_RECORD_SIZE !=
        (uint64_t)len) {
        return lv_reject_locked(&g_live.rejected_length, APP_HOST_LIVE_ERR_LENGTH);
    }
    for (i = 0u; i < record_count; i++) {
        const uint8_t *r = p + APP_HOST_LIVE_HEADER_SIZE +
                           (size_t)i * APP_HOST_LIVE_EVENT_RECORD_SIZE;
        uint32_t event_seq = lv_get_u32(r + 0u);
        uint32_t direction = r[12u];
        uint32_t event_flags = r[13u];

        if (event_seq == 0u ||
            (first != AICAM_TRUE && event_seq <= prev_seq) ||
            (g_live.event_watermark_valid == AICAM_TRUE &&
             event_seq <= g_live.last_event_seq)) {
            return lv_reject_locked(&g_live.rejected_record,
                                    APP_HOST_LIVE_ERR_RECORD);
        }
        if (event_seq > through_seq) {
            return lv_reject_locked(&g_live.rejected_record,
                                    APP_HOST_LIVE_ERR_RECORD);
        }
        if ((direction != APP_HOST_LIVE_DIRECTION_IN &&
             direction != APP_HOST_LIVE_DIRECTION_OUT) ||
            (event_flags & ~APP_HOST_LIVE_EVENT_FLAG_KNOWN_MASK) != 0u) {
            return lv_reject_locked(&g_live.rejected_record,
                                    APP_HOST_LIVE_ERR_RECORD);
        }
        prev_seq = event_seq;
        first = AICAM_FALSE;
    }
    return APP_HOST_LIVE_OK;
}

static int32_t lv_validate_tracks_locked(const uint8_t *p, uint32_t len,
                                         uint32_t record_count,
                                         uint32_t trusted_class_count)
{
    uint64_t offsets[APP_HOST_LIVE_TRACK_MAX];
    uint64_t offset = APP_HOST_LIVE_HEADER_SIZE;
    uint32_t i;
    uint32_t j;

    if (record_count > g_live.live_track_max) {
        return lv_reject_locked(&g_live.rejected_overflow,
                                APP_HOST_LIVE_ERR_OVERFLOW);
    }
    for (i = 0u; i < record_count; i++) {
        const uint8_t *r;
        uint32_t point_count;
        uint32_t class_index;
        uint32_t track_id;

        if (offset + APP_HOST_LIVE_TRACK_HEADER_SIZE > (uint64_t)len) {
            return lv_reject_locked(&g_live.rejected_length,
                                    APP_HOST_LIVE_ERR_LENGTH);
        }
        offsets[i] = offset;
        r = p + (size_t)offset;
        track_id = lv_get_u32(r + 0u);
        point_count = lv_get_u16(r + 4u);
        class_index = lv_get_u16(r + 6u);
        if (track_id == 0u ||
            point_count > APP_HOST_LIVE_TRACK_POINT_MAX ||
            r[8u] != APP_HOST_LIVE_TRACK_STATE_ACTIVE ||
            r[9u] != 0u) {
            return lv_reject_locked(&g_live.rejected_record,
                                    APP_HOST_LIVE_ERR_RECORD);
        }
        if (class_index != APP_HOST_LIVE_CLASS_UNKNOWN &&
            class_index >= trusted_class_count) {
            return lv_reject_locked(&g_live.rejected_record,
                                    APP_HOST_LIVE_ERR_RECORD);
        }
        for (j = 0u; j < i; j++) {
            if (lv_get_u32(p + (size_t)offsets[j]) == track_id) {
                return lv_reject_locked(&g_live.rejected_record,
                                        APP_HOST_LIVE_ERR_RECORD);
            }
        }
        offset += (uint64_t)APP_HOST_LIVE_TRACK_HEADER_SIZE +
                  ((uint64_t)point_count * APP_HOST_LIVE_TRACK_POINT_SIZE);
        if (offset > (uint64_t)len) {
            return lv_reject_locked(&g_live.rejected_length,
                                    APP_HOST_LIVE_ERR_LENGTH);
        }
    }
    if (offset != (uint64_t)len) {
        return lv_reject_locked(&g_live.rejected_length, APP_HOST_LIVE_ERR_LENGTH);
    }
    return APP_HOST_LIVE_OK;
}

static void lv_commit_events_locked(const uint8_t *p, uint32_t record_count)
{
    uint32_t i;
    uint64_t epoch_ms = lv_epoch_ms();

    for (i = 0u; i < record_count; i++) {
        const uint8_t *r = p + APP_HOST_LIVE_HEADER_SIZE +
                           (size_t)i * APP_HOST_LIVE_EVENT_RECORD_SIZE;
        live_ring_slot_t *slot;

        if (g_live.ring_count == APP_HOST_LIVE_EVENT_RING_SLOTS) {
            g_live.ring_head = (g_live.ring_head + 1u) %
                               APP_HOST_LIVE_EVENT_RING_SLOTS;
            g_live.ring_count--;
            g_live.host_dropped_events++;
        }
        slot = &g_live.ring[(g_live.ring_head + g_live.ring_count) %
                            APP_HOST_LIVE_EVENT_RING_SLOTS];
        slot->event.event_seq = lv_get_u32(r + 0u);
        slot->event.track_id = lv_get_u32(r + 4u);
        slot->event.event_ts_ms = epoch_ms;
        slot->event.direction = r[12u];
        slot->event.flags = r[13u];
        slot->event.x_q8 = r[14u];
        slot->event.y_q8 = r[15u];
        slot->cursor = g_live.next_cursor;
        g_live.next_cursor++;
        g_live.ring_count++;
        g_live.event_count++;
        g_live.last_event_seq = slot->event.event_seq;
        g_live.event_watermark_valid = AICAM_TRUE;
    }
}

static void lv_commit_tracks_locked(const uint8_t *p, uint32_t record_count)
{
    uint64_t offset = APP_HOST_LIVE_HEADER_SIZE;
    uint32_t i;

    for (i = 0u; i < record_count; i++) {
        const uint8_t *r = p + (size_t)offset;
        app_host_live_track_t *track = &g_live.snapshot[i];
        uint32_t point_count = lv_get_u16(r + 4u);
        uint32_t k;

        track->track_id = lv_get_u32(r + 0u);
        track->point_count = (uint16_t)point_count;
        track->class_index = (uint16_t)lv_get_u16(r + 6u);
        track->state = r[8u];
        track->flags = r[9u];
        track->confidence_q16 = (uint16_t)lv_get_u16(r + 10u);
        for (k = 0u; k < point_count; k++) {
            const uint8_t *pt = r + APP_HOST_LIVE_TRACK_HEADER_SIZE +
                                (size_t)k * APP_HOST_LIVE_TRACK_POINT_SIZE;

            track->points[k].ts_ms = lv_get_u32(pt + 0u);
            track->points[k].x_q16 = (uint16_t)lv_get_u16(pt + 4u);
            track->points[k].y_q16 = (uint16_t)lv_get_u16(pt + 6u);
        }
        offset += (uint64_t)APP_HOST_LIVE_TRACK_HEADER_SIZE +
                  (uint64_t)point_count * APP_HOST_LIVE_TRACK_POINT_SIZE;
    }
    g_live.snapshot_track_count = record_count;
    g_live.snapshot_packet_seq = g_live.last_packet_seq;
    g_live.snapshot_valid = AICAM_TRUE;
    g_live.snapshot_count++;
}

int32_t app_host_live_ingest(const void *buf, uint32_t len,
                             uint32_t session_generation,
                             uint32_t trusted_class_count)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t kind = 0u;
    uint32_t record_count = 0u;
    uint32_t packet_seq = 0u;
    uint32_t through_seq = 0u;
    uint32_t source_total = 0u;
    uint32_t source_gap = 0u;
    uint32_t config_generation = 0u;
    uint32_t flags = 0u;
    int32_t rc;

    if (buf == NULL || len == 0u) {
        return APP_HOST_LIVE_ERR_PARAM;
    }
    lv_lock();
    if (g_live.bound != AICAM_TRUE) {
        lv_unlock();
        return APP_HOST_LIVE_ERR_UNBOUND;
    }
    if (g_live.closed == AICAM_TRUE) {
        lv_unlock();
        return APP_HOST_LIVE_ERR_CLOSED;
    }
    if (g_live.session_generation != session_generation) {
        lv_unlock();
        return APP_HOST_LIVE_ERR_GENERATION;
    }
    rc = lv_validate_header_locked(p, len, &kind, &record_count, &packet_seq,
                                   &through_seq, &source_total, &source_gap,
                                   &config_generation, &flags);
    if (rc != APP_HOST_LIVE_OK) {
        lv_unlock();
        return rc;
    }
    if (g_live.seen_packet == AICAM_TRUE && packet_seq == g_live.last_packet_seq) {
        if (len == g_live.last_packet_len &&
            memcmp(p, g_live.last_packet, len) == 0) {
            g_live.idempotent_replays++;
            lv_unlock();
            return APP_HOST_LIVE_OK;
        }
        rc = lv_reject_locked(&g_live.rejected_sequence,
                              APP_HOST_LIVE_ERR_SEQUENCE);
        lv_unlock();
        return rc;
    }
    if (packet_seq == 0u) {
        rc = lv_reject_locked(&g_live.rejected_sequence,
                              APP_HOST_LIVE_ERR_SEQUENCE);
        lv_unlock();
        return rc;
    }
    if (g_live.seen_packet == AICAM_TRUE &&
        g_live.last_packet_seq == 0xFFFFFFFFu) {
        g_live.closed = AICAM_TRUE;
        lv_unlock();
        return APP_HOST_LIVE_ERR_CLOSED;
    }
    if (g_live.seen_packet == AICAM_TRUE &&
        packet_seq <= g_live.last_packet_seq) {
        rc = lv_reject_locked(&g_live.rejected_sequence,
                              APP_HOST_LIVE_ERR_SEQUENCE);
        lv_unlock();
        return rc;
    }
    if (g_live.seen_packet == AICAM_TRUE) {
        if (through_seq < g_live.last_through_seq ||
            source_total < g_live.last_source_total ||
            source_gap < g_live.last_source_gap) {
            if (source_total < g_live.last_source_total ||
                source_gap < g_live.last_source_gap) {
                g_live.closed = AICAM_TRUE;
            }
            rc = lv_reject_locked(&g_live.rejected_watermark,
                                  APP_HOST_LIVE_ERR_WATERMARK);
            lv_unlock();
            return rc;
        }
    }
    if (config_generation == 0u ||
        (g_live.config_generation_valid == AICAM_TRUE &&
         config_generation != g_live.config_generation)) {
        rc = lv_reject_locked(&g_live.rejected_config_generation,
                              APP_HOST_LIVE_ERR_CONFIG_GENERATION);
        lv_unlock();
        return rc;
    }
    {
        uint32_t prev_gap = (g_live.seen_packet == AICAM_TRUE)
                                ? g_live.last_source_gap
                                : 0u;
        aicam_bool_t gap_delta = (aicam_bool_t)(source_gap > prev_gap);

        if (((flags & APP_HOST_LIVE_FLAG_SOURCE_GAP_DELTA) != 0u) !=
            (gap_delta != AICAM_FALSE)) {
            rc = lv_reject_locked(&g_live.rejected_watermark,
                                  APP_HOST_LIVE_ERR_WATERMARK);
            lv_unlock();
            return rc;
        }
    }
    if (kind == APP_HOST_LIVE_KIND_EVENT_BATCH) {
        rc = lv_validate_events_locked(p, len, record_count, through_seq);
    } else {
        rc = lv_validate_tracks_locked(p, len, record_count, trusted_class_count);
    }
    if (rc != APP_HOST_LIVE_OK) {
        lv_unlock();
        return rc;
    }
    g_live.seen_packet = AICAM_TRUE;
    g_live.last_packet_seq = packet_seq;
    g_live.last_packet_len = len;
    memcpy(g_live.last_packet, p, len);
    g_live.last_through_seq = through_seq;
    g_live.last_source_total = source_total;
    g_live.last_source_gap = source_gap;
    g_live.config_generation = config_generation;
    g_live.config_generation_valid = AICAM_TRUE;
    g_live.packet_count++;
    g_live.loss_unknown = (aicam_bool_t)((flags &
        APP_HOST_LIVE_FLAG_SOURCE_LOSS_UNKNOWN) != 0u);
    g_live.resync_required = (aicam_bool_t)((flags &
        APP_HOST_LIVE_FLAG_RESYNC_REQUIRED) != 0u);
    if (kind == APP_HOST_LIVE_KIND_EVENT_BATCH) {
        lv_commit_events_locked(p, record_count);
    } else {
        lv_commit_tracks_locked(p, record_count);
    }
    g_live.last_ingest_ms = lv_now_ms();
    g_live.have_ingest_ms = AICAM_TRUE;
    lv_unlock();
    return APP_HOST_LIVE_OK;
}

int32_t app_host_live_read(uint32_t session_generation, uint64_t after_cursor,
                           app_host_live_event_t *out, uint32_t cap,
                           app_host_live_read_out_t *out_info)
{
    uint32_t now_ms;
    uint32_t i;
    uint32_t returned = 0u;
    uint64_t oldest_cursor = 0u;
    uint64_t newest_cursor = 0u;
    app_host_live_read_state_t state;
    app_host_live_freshness_t freshness;
    uint32_t age_ms = 0u;

    if (out == NULL || cap == 0u || out_info == NULL) {
        return APP_HOST_LIVE_ERR_PARAM;
    }
    memset(out_info, 0, sizeof(*out_info));
    lv_lock();
    if (g_live.bound != AICAM_TRUE) {
        lv_unlock();
        return APP_HOST_LIVE_ERR_UNBOUND;
    }
    if (g_live.closed == AICAM_TRUE ||
        g_live.session_generation != session_generation) {
        lv_unlock();
        return APP_HOST_LIVE_ERR_GENERATION;
    }
    for (i = 0u; i < g_live.ring_count; i++) {
        const live_ring_slot_t *slot =
            &g_live.ring[(g_live.ring_head + i) % APP_HOST_LIVE_EVENT_RING_SLOTS];

        if (i == 0u) {
            oldest_cursor = slot->cursor;
        }
        newest_cursor = slot->cursor;
        if (slot->cursor > after_cursor && returned < cap) {
            out[returned] = slot->event;
            out[returned].cursor = slot->cursor;
            returned++;
        }
    }
    now_ms = lv_now_ms();
    freshness = lv_freshness_locked(now_ms, &age_ms);
    if (g_live.seen_packet != AICAM_TRUE || g_live.loss_unknown == AICAM_TRUE ||
        g_live.resync_required == AICAM_TRUE) {
        state = APP_HOST_LIVE_READ_STATE_UNKNOWN;
    } else if (g_live.last_source_gap > 0u ||
               (oldest_cursor != 0u && after_cursor + 1u < oldest_cursor)) {
        state = APP_HOST_LIVE_READ_STATE_GAPPED;
    } else {
        state = APP_HOST_LIVE_READ_STATE_CONTINUOUS;
    }
    out_info->next_cursor = (newest_cursor == 0u) ? 1u : (newest_cursor + 1u);
    out_info->returned = returned;
    out_info->oldest_cursor = oldest_cursor;
    out_info->newest_cursor = newest_cursor;
    out_info->host_dropped_events = g_live.host_dropped_events;
    out_info->through_seq = g_live.last_through_seq;
    out_info->source_total = g_live.last_source_total;
    out_info->source_gap = g_live.last_source_gap;
    out_info->packet_count = g_live.packet_count;
    out_info->last_packet_seq = g_live.last_packet_seq;
    out_info->state = state;
    out_info->freshness = freshness;
    out_info->age_ms = age_ms;
    out_info->loss_unknown = g_live.loss_unknown;
    out_info->resync_required = g_live.resync_required;
    out_info->session_generation = g_live.session_generation;
    out_info->view_epoch = g_live_view_epoch;
    lv_unlock();
    return APP_HOST_LIVE_OK;
}

int32_t app_host_live_snapshot_read(uint32_t session_generation,
                                    app_host_live_track_t *out, uint32_t cap,
                                    app_host_live_snapshot_out_t *out_info)
{
    uint32_t now_ms;
    uint32_t age_ms = 0u;
    uint32_t i;

    if (out_info == NULL || (out == NULL && cap > 0u)) {
        return APP_HOST_LIVE_ERR_PARAM;
    }
    memset(out_info, 0, sizeof(*out_info));
    lv_lock();
    if (g_live.bound != AICAM_TRUE) {
        lv_unlock();
        return APP_HOST_LIVE_ERR_UNBOUND;
    }
    if (g_live.closed == AICAM_TRUE ||
        g_live.session_generation != session_generation) {
        lv_unlock();
        return APP_HOST_LIVE_ERR_GENERATION;
    }
    if (g_live.snapshot_valid == AICAM_TRUE && cap < g_live.snapshot_track_count) {
        lv_unlock();
        return APP_HOST_LIVE_ERR_OVERFLOW;
    }
    if (g_live.snapshot_valid == AICAM_TRUE && out != NULL) {
        for (i = 0u; i < g_live.snapshot_track_count; i++) {
            out[i] = g_live.snapshot[i];
        }
    }
    now_ms = lv_now_ms();
    out_info->track_count = g_live.snapshot_valid == AICAM_TRUE
                                ? g_live.snapshot_track_count
                                : 0u;
    out_info->packet_seq = g_live.snapshot_valid == AICAM_TRUE
                               ? g_live.snapshot_packet_seq
                               : 0u;
    out_info->freshness = lv_freshness_locked(now_ms, &age_ms);
    out_info->age_ms = age_ms;
    out_info->loss_unknown = g_live.loss_unknown;
    out_info->resync_required = g_live.resync_required;
    out_info->through_seq = g_live.last_through_seq;
    out_info->source_total = g_live.last_source_total;
    out_info->source_gap = g_live.last_source_gap;
    out_info->host_dropped_events = g_live.host_dropped_events;
    out_info->session_generation = g_live.session_generation;
    out_info->view_epoch = g_live_view_epoch;
    lv_unlock();
    return APP_HOST_LIVE_OK;
}

void app_host_live_status(app_host_live_status_t *out)
{
    uint32_t now_ms;
    uint32_t age_ms = 0u;

    if (out == NULL) {
        return;
    }
    lv_lock();
    out->bound = g_live.bound;
    out->closed = g_live.closed;
    out->session_generation = g_live.session_generation;
    out->view_epoch = g_live_view_epoch;
    memcpy(out->publisher_id, g_live.publisher_id, sizeof(out->publisher_id));
    memcpy(out->app_id, g_live.app_id, sizeof(out->app_id));
    memcpy(out->spki_sha256, g_live.spki_sha256, sizeof(out->spki_sha256));
    memcpy(out->tbs_sha256, g_live.tbs_sha256, sizeof(out->tbs_sha256));
    memcpy(out->package_sha256, g_live.package_sha256,
           sizeof(out->package_sha256));
    out->live_publish_max = g_live.live_publish_max;
    out->live_track_max = g_live.live_track_max;
    out->packet_count = g_live.packet_count;
    out->event_count = g_live.event_count;
    out->snapshot_count = g_live.snapshot_count;
    out->idempotent_replays = g_live.idempotent_replays;
    out->rejected_total = g_live.rejected_total;
    out->rejected_length = g_live.rejected_length;
    out->rejected_header = g_live.rejected_header;
    out->rejected_sequence = g_live.rejected_sequence;
    out->rejected_watermark = g_live.rejected_watermark;
    out->rejected_config_generation = g_live.rejected_config_generation;
    out->rejected_record = g_live.rejected_record;
    out->rejected_overflow = g_live.rejected_overflow;
    out->host_dropped_events = g_live.host_dropped_events;
    out->last_packet_seq = g_live.last_packet_seq;
    out->through_seq = g_live.last_through_seq;
    out->source_total = g_live.last_source_total;
    out->source_gap = g_live.last_source_gap;
    out->retained_events = g_live.ring_count;
    out->snapshot_tracks = g_live.snapshot_track_count;
    out->loss_unknown = g_live.loss_unknown;
    out->resync_required = g_live.resync_required;
    now_ms = lv_now_ms();
    out->freshness = lv_freshness_locked(now_ms, &age_ms);
    out->age_ms = age_ms;
    lv_unlock();
}

uint64_t app_host_live_epoch_ms(void)
{
    return lv_epoch_ms();
}

const char *app_host_live_result_str(int32_t result)
{
    switch (result) {
    case APP_HOST_LIVE_OK:
        return "OK";
    case APP_HOST_LIVE_ERR_PARAM:
        return "PARAM";
    case APP_HOST_LIVE_ERR_UNBOUND:
        return "UNBOUND";
    case APP_HOST_LIVE_ERR_GENERATION:
        return "GENERATION";
    case APP_HOST_LIVE_ERR_LENGTH:
        return "LENGTH";
    case APP_HOST_LIVE_ERR_HEADER:
        return "HEADER";
    case APP_HOST_LIVE_ERR_SEQUENCE:
        return "SEQUENCE";
    case APP_HOST_LIVE_ERR_WATERMARK:
        return "WATERMARK";
    case APP_HOST_LIVE_ERR_CONFIG_GENERATION:
        return "CONFIG_GENERATION";
    case APP_HOST_LIVE_ERR_RECORD:
        return "RECORD";
    case APP_HOST_LIVE_ERR_OVERFLOW:
        return "OVERFLOW";
    case APP_HOST_LIVE_ERR_CLOSED:
        return "CLOSED";
    default:
        return "UNKNOWN";
    }
}

const char *app_host_live_read_state_str(app_host_live_read_state_t state)
{
    switch (state) {
    case APP_HOST_LIVE_READ_STATE_CONTINUOUS:
        return "continuous";
    case APP_HOST_LIVE_READ_STATE_GAPPED:
        return "gapped";
    case APP_HOST_LIVE_READ_STATE_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *app_host_live_freshness_str(app_host_live_freshness_t freshness)
{
    switch (freshness) {
    case APP_HOST_LIVE_FRESHNESS_FRESH:
        return "fresh";
    case APP_HOST_LIVE_FRESHNESS_STALE:
        return "stale";
    case APP_HOST_LIVE_FRESHNESS_NONE:
    default:
        return "none";
    }
}
