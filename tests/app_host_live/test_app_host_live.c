#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "app_host_live.h"

static int g_total;
static int g_fail;
static uint32_t g_fake_now_ms;
static uint64_t g_fake_epoch_ms;

#define CHECK(v) do { \
    g_total++; \
    if (!(v)) { \
        g_fail++; \
        printf("FAIL app_host_live:%d %s\n", g_total, #v); \
    } \
} while (0)

static void *fake_mutex_create(void *ctx)
{
    pthread_mutex_t *mtx = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));

    (void)ctx;
    pthread_mutex_init(mtx, NULL);
    return mtx;
}

static void fake_mutex_destroy(void *ctx, void *mtx)
{
    (void)ctx;
    pthread_mutex_destroy((pthread_mutex_t *)mtx);
    free(mtx);
}

static int fake_mutex_lock(void *ctx, void *mtx)
{
    (void)ctx;
    return pthread_mutex_lock((pthread_mutex_t *)mtx);
}

static void fake_mutex_unlock(void *ctx, void *mtx)
{
    (void)ctx;
    pthread_mutex_unlock((pthread_mutex_t *)mtx);
}

static uint32_t fake_now_ms(void *ctx)
{
    (void)ctx;
    return g_fake_now_ms;
}

static uint64_t fake_epoch_ms(void *ctx)
{
    (void)ctx;
    return g_fake_epoch_ms;
}

static void fake_backend_init(void)
{
    app_host_live_backend_t backend;

    memset(&backend, 0, sizeof(backend));
    backend.ctx = NULL;
    backend.now_ms = fake_now_ms;
    backend.epoch_ms = fake_epoch_ms;
    backend.mutex_create = fake_mutex_create;
    backend.mutex_destroy = fake_mutex_destroy;
    backend.mutex_lock = fake_mutex_lock;
    backend.mutex_unlock = fake_mutex_unlock;
    app_host_live_init(&backend);
}

static void lv_put_u16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void lv_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void lv_identity_fill(app_host_live_identity_t *ident,
                             uint32_t generation,
                             uint32_t publish_max,
                             uint32_t track_max)
{
    memset(ident, 0, sizeof(*ident));
    ident->session_generation = generation;
    snprintf(ident->publisher_id, sizeof(ident->publisher_id), "%s",
             "live-publisher");
    snprintf(ident->app_id, sizeof(ident->app_id), "%s", "live-app");
    ident->spki_sha256[0] = 0x01u;
    ident->tbs_sha256[0] = 0x02u;
    ident->package_sha256[0] = 0x03u;
    ident->live_publish_max = publish_max;
    ident->live_track_max = track_max;
}

static uint32_t lv_header(uint8_t *buf, uint32_t kind, uint32_t total_len,
                          uint32_t packet_seq, uint32_t through_seq,
                          uint32_t source_total, uint32_t source_gap,
                          uint32_t config_generation, uint32_t record_count,
                          uint32_t flags)
{
    memset(buf, 0, APP_HOST_LIVE_HEADER_SIZE);
    memcpy(buf, "LVN4", 4u);
    lv_put_u16(buf + 4u, APP_HOST_LIVE_HEADER_VERSION);
    lv_put_u16(buf + 6u, kind);
    lv_put_u32(buf + 8u, total_len);
    lv_put_u32(buf + 12u, packet_seq);
    lv_put_u32(buf + 16u, through_seq);
    lv_put_u32(buf + 20u, source_total);
    lv_put_u32(buf + 24u, source_gap);
    lv_put_u32(buf + 32u, config_generation);
    lv_put_u32(buf + 36u, record_count);
    lv_put_u32(buf + 40u, flags);
    lv_put_u32(buf + 44u, 0u);
    return APP_HOST_LIVE_HEADER_SIZE;
}

static void lv_event(uint8_t *rec, uint32_t event_seq, uint32_t track_id,
                     uint32_t ts_ms, uint32_t direction, uint32_t flags,
                     uint8_t x_q8, uint8_t y_q8)
{
    lv_put_u32(rec + 0u, event_seq);
    lv_put_u32(rec + 4u, track_id);
    lv_put_u32(rec + 8u, ts_ms);
    rec[12u] = (uint8_t)direction;
    rec[13u] = (uint8_t)flags;
    rec[14u] = x_q8;
    rec[15u] = y_q8;
}

static uint32_t lv_track(uint8_t *buf, uint32_t track_id, uint32_t point_count,
                         uint32_t class_index, uint32_t state, uint32_t flags,
                         uint32_t confidence_q16)
{
    uint32_t i;

    lv_put_u32(buf + 0u, track_id);
    lv_put_u16(buf + 4u, point_count);
    lv_put_u16(buf + 6u, class_index);
    buf[8u] = (uint8_t)state;
    buf[9u] = (uint8_t)flags;
    lv_put_u16(buf + 10u, confidence_q16);
    for (i = 0u; i < point_count; i++) {
        uint8_t *pt = buf + APP_HOST_LIVE_TRACK_HEADER_SIZE +
                      i * APP_HOST_LIVE_TRACK_POINT_SIZE;

        lv_put_u32(pt + 0u, 1000u + i);
        lv_put_u16(pt + 4u, (uint16_t)(i * 4096u));
        lv_put_u16(pt + 6u, (uint16_t)(65535u - i * 4096u));
    }
    return APP_HOST_LIVE_TRACK_HEADER_SIZE +
           point_count * APP_HOST_LIVE_TRACK_POINT_SIZE;
}

static void test_bind_validation(void)
{
    app_host_live_identity_t ident;
    app_host_live_status_t st;

    fake_backend_init();
    app_host_live_status(&st);
    CHECK(st.bound == AICAM_FALSE);
    CHECK(st.view_epoch == 1u);
    CHECK(app_host_live_bind(NULL) == APP_HOST_LIVE_ERR_PARAM);
    lv_identity_fill(&ident, 0u, 9008u, 64u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_ERR_PARAM);
    lv_identity_fill(&ident, 7u, APP_ADMISSION_V4_LIVE_MIN_PUBLISH - 1u, 64u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_ERR_PARAM);
    lv_identity_fill(&ident, 7u, 312u, 0u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_ERR_PARAM);
    lv_identity_fill(&ident, 7u, 312u, APP_HOST_LIVE_TRACK_CAPACITY + 1u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_ERR_PARAM);
    lv_identity_fill(&ident, 7u, APP_HOST_LIVE_PUBLISH_CAPACITY + 1u, 64u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_ERR_PARAM);
    lv_identity_fill(&ident, 7u, 400u, 3u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_ERR_PARAM);
    lv_identity_fill(&ident, 7u, 312u, 1u);
    ident.publisher_id[0] = '\0';
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_ERR_PARAM);
    lv_identity_fill(&ident, 7u, 312u, 1u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_OK);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_ERR_UNBOUND);
    app_host_live_status(&st);
    CHECK(st.bound == AICAM_TRUE);
    CHECK(st.session_generation == 7u);
    CHECK(st.live_publish_max == 312u);
    CHECK(st.live_track_max == 1u);
    CHECK(strcmp(st.publisher_id, "live-publisher") == 0);
    CHECK(strcmp(st.app_id, "live-app") == 0);
    CHECK(st.freshness == APP_HOST_LIVE_FRESHNESS_NONE);
    app_host_live_end(8u);
    app_host_live_status(&st);
    CHECK(st.bound == AICAM_TRUE);
    app_host_live_end(7u);
    app_host_live_status(&st);
    CHECK(st.bound == AICAM_FALSE);
    CHECK(app_host_live_ingest(NULL, 0u, 7u, 4u) == APP_HOST_LIVE_ERR_PARAM);
}

static void test_header_validation(void)
{
    app_host_live_identity_t ident;
    app_host_live_status_t st;
    uint8_t buf[APP_HOST_LIVE_PUBLISH_CAPACITY];
    uint32_t len;

    fake_backend_init();
    lv_identity_fill(&ident, 11u, 9008u, 64u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_OK);
    CHECK(app_host_live_ingest(buf, 0u, 11u, 4u) == APP_HOST_LIVE_ERR_PARAM);
    len = lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 1u, 0u, 0u, 0u,
                    3u, 0u, 0u);
    CHECK(app_host_live_ingest(buf, 47u, 11u, 4u) == APP_HOST_LIVE_ERR_LENGTH);
    CHECK(app_host_live_ingest(buf, 9010u, 11u, 4u) == APP_HOST_LIVE_ERR_LENGTH);
    CHECK(app_host_live_ingest(buf, len, 12u, 4u) ==
          APP_HOST_LIVE_ERR_GENERATION);
    CHECK(app_host_live_ingest(buf, len, 11u, 4u) == APP_HOST_LIVE_OK);
    buf[0] = 'X';
    CHECK(app_host_live_ingest(buf, len, 11u, 4u) == APP_HOST_LIVE_ERR_HEADER);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 1u, 0u, 0u, 0u, 3u, 0u,
              0u);
    buf[4] = 2u;
    CHECK(app_host_live_ingest(buf, len, 11u, 4u) == APP_HOST_LIVE_ERR_HEADER);
    lv_header(buf, 3u, 48u, 2u, 0u, 0u, 0u, 3u, 0u, 0u);
    CHECK(app_host_live_ingest(buf, 48u, 11u, 4u) == APP_HOST_LIVE_ERR_HEADER);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 50u, 2u, 0u, 0u, 0u, 3u, 0u,
              0u);
    CHECK(app_host_live_ingest(buf, 48u, 11u, 4u) == APP_HOST_LIVE_ERR_HEADER);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 2u, 0u, 0u, 0u, 3u, 0u,
              0u);
    buf[44u] = 1u;
    CHECK(app_host_live_ingest(buf, 48u, 11u, 4u) == APP_HOST_LIVE_ERR_HEADER);
    buf[44u] = 0u;
    buf[40u] = 0x08u;
    CHECK(app_host_live_ingest(buf, 48u, 11u, 4u) == APP_HOST_LIVE_ERR_HEADER);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 0u, 0u, 0u, 0u, 3u, 0u,
              0u);
    CHECK(app_host_live_ingest(buf, 48u, 11u, 4u) ==
          APP_HOST_LIVE_ERR_SEQUENCE);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 9009u, 2u, 0u, 0u, 0u, 3u,
              0u, 0u);
    CHECK(app_host_live_ingest(buf, 9009u, 11u, 4u) == APP_HOST_LIVE_ERR_LENGTH);
    app_host_live_status(&st);
    CHECK(st.rejected_header == 6u);
    CHECK(st.rejected_length == 3u);
    CHECK(st.rejected_sequence == 1u);
    CHECK(st.packet_count == 1u);
}

static void test_sequence_rules(void)
{
    app_host_live_identity_t ident;
    app_host_live_status_t st;
    uint8_t buf[64];

    fake_backend_init();
    lv_identity_fill(&ident, 21u, 312u, 1u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_OK);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 5u, 0u, 0u, 0u, 3u, 0u,
              0u);
    CHECK(app_host_live_ingest(buf, 48u, 21u, 4u) == APP_HOST_LIVE_OK);
    CHECK(app_host_live_ingest(buf, 48u, 21u, 4u) == APP_HOST_LIVE_OK);
    app_host_live_status(&st);
    CHECK(st.idempotent_replays == 1u);
    CHECK(st.packet_count == 1u);
    buf[16u] = 1u;
    CHECK(app_host_live_ingest(buf, 48u, 21u, 4u) ==
          APP_HOST_LIVE_ERR_SEQUENCE);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 4u, 0u, 0u, 0u, 3u, 0u,
              0u);
    CHECK(app_host_live_ingest(buf, 48u, 21u, 4u) ==
          APP_HOST_LIVE_ERR_SEQUENCE);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 9u, 0u, 0u, 0u, 3u, 0u,
              0u);
    CHECK(app_host_live_ingest(buf, 48u, 21u, 4u) == APP_HOST_LIVE_OK);
    app_host_live_status(&st);
    CHECK(st.last_packet_seq == 9u);
    CHECK(st.rejected_sequence == 2u);
    fake_backend_init();
    lv_identity_fill(&ident, 22u, 312u, 1u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_OK);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 0xFFFFFFFFu, 0u, 0u, 0u,
              3u, 0u, 0u);
    CHECK(app_host_live_ingest(buf, 48u, 22u, 4u) == APP_HOST_LIVE_OK);
    CHECK(app_host_live_ingest(buf, 48u, 22u, 4u) == APP_HOST_LIVE_OK);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 1u, 0u, 0u, 0u, 3u, 0u,
              0u);
    CHECK(app_host_live_ingest(buf, 48u, 22u, 4u) == APP_HOST_LIVE_ERR_CLOSED);
    app_host_live_status(&st);
    CHECK(st.closed == AICAM_TRUE);
}

static void test_watermark_and_generation(void)
{
    app_host_live_identity_t ident;
    app_host_live_status_t st;
    uint32_t epoch;
    uint8_t buf[64];

    fake_backend_init();
    app_host_live_status(&st);
    epoch = st.view_epoch;
    lv_identity_fill(&ident, 31u, 312u, 1u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_OK);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 1u, 10u, 10u, 0u, 7u,
              0u, 0u);
    CHECK(app_host_live_ingest(buf, 48u, 31u, 4u) == APP_HOST_LIVE_OK);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 2u, 9u, 10u, 0u, 7u, 0u,
              0u);
    CHECK(app_host_live_ingest(buf, 48u, 31u, 4u) ==
          APP_HOST_LIVE_ERR_WATERMARK);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 2u, 10u, 10u, 0u, 0u,
              0u, 0u);
    CHECK(app_host_live_ingest(buf, 48u, 31u, 4u) ==
          APP_HOST_LIVE_ERR_CONFIG_GENERATION);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 2u, 10u, 10u, 0u, 9u,
              0u, 0u);
    CHECK(app_host_live_ingest(buf, 48u, 31u, 4u) ==
          APP_HOST_LIVE_ERR_CONFIG_GENERATION);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 2u, 12u, 12u, 2u, 7u,
              0u, 0u);
    CHECK(app_host_live_ingest(buf, 48u, 31u, 4u) ==
          APP_HOST_LIVE_ERR_WATERMARK);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 2u, 12u, 12u, 2u, 7u,
              0u, APP_HOST_LIVE_FLAG_SOURCE_GAP_DELTA);
    CHECK(app_host_live_ingest(buf, 48u, 31u, 4u) == APP_HOST_LIVE_OK);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 3u, 12u, 12u, 2u, 7u,
              0u, APP_HOST_LIVE_FLAG_SOURCE_GAP_DELTA);
    CHECK(app_host_live_ingest(buf, 48u, 31u, 4u) ==
          APP_HOST_LIVE_ERR_WATERMARK);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 3u, 12u, 12u, 2u, 7u,
              0u, APP_HOST_LIVE_FLAG_RESYNC_REQUIRED);
    CHECK(app_host_live_ingest(buf, 48u, 31u, 4u) == APP_HOST_LIVE_OK);
    {
        app_host_live_read_out_t ro;

        CHECK(app_host_live_read(31u, 0u, NULL, 0u, &ro) ==
              APP_HOST_LIVE_ERR_PARAM);
        CHECK(app_host_live_read(31u, 0u, NULL, 1u, &ro) ==
              APP_HOST_LIVE_ERR_PARAM);
        CHECK(app_host_live_read(31u, 0u, NULL, 1u, NULL) ==
              APP_HOST_LIVE_ERR_PARAM);
    }
    {
        app_host_live_event_t evts[4];
        app_host_live_read_out_t ro;

        memset(&ro, 0, sizeof(ro));
        CHECK(app_host_live_read(31u, 0u, evts, 4u, &ro) == APP_HOST_LIVE_OK);
        CHECK(ro.returned == 0u);
        CHECK(ro.state == APP_HOST_LIVE_READ_STATE_UNKNOWN);
        CHECK(ro.resync_required == AICAM_TRUE);
        CHECK(ro.through_seq == 12u);
        CHECK(ro.source_total == 12u);
        CHECK(ro.source_gap == 2u);
        CHECK(ro.packet_count == 3u);
        CHECK(ro.last_packet_seq == 3u);
        CHECK(ro.next_cursor == 1u);
        CHECK(ro.view_epoch == epoch);
        CHECK(ro.session_generation == 31u);
    }
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 4u, 12u, 12u, 2u, 7u,
              0u, 0u);
    CHECK(app_host_live_ingest(buf, 48u, 31u, 4u) == APP_HOST_LIVE_OK);
    {
        app_host_live_event_t evts[4];
        app_host_live_read_out_t ro;

        memset(&ro, 0, sizeof(ro));
        CHECK(app_host_live_read(31u, 0u, evts, 4u, &ro) == APP_HOST_LIVE_OK);
        CHECK(ro.state == APP_HOST_LIVE_READ_STATE_GAPPED);
        CHECK(ro.resync_required == AICAM_FALSE);
        CHECK(ro.source_gap > 0u);
    }
    app_host_live_status(&st);
    CHECK(st.rejected_watermark == 3u);
    CHECK(st.rejected_config_generation == 2u);
    fake_backend_init();
    lv_identity_fill(&ident, 32u, 312u, 1u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_OK);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 1u, 10u, 10u, 0u, 7u,
              0u, 0u);
    CHECK(app_host_live_ingest(buf, 48u, 32u, 4u) == APP_HOST_LIVE_OK);
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 2u, 11u, 9u, 0u, 7u, 0u,
              0u);
    CHECK(app_host_live_ingest(buf, 48u, 32u, 4u) ==
          APP_HOST_LIVE_ERR_WATERMARK);
    app_host_live_status(&st);
    CHECK(st.closed == AICAM_TRUE);
    CHECK(app_host_live_ingest(buf, 48u, 32u, 4u) == APP_HOST_LIVE_ERR_CLOSED);
    {
        app_host_live_event_t evts[4];
        app_host_live_read_out_t ro;

        CHECK(app_host_live_read(32u, 0u, evts, 4u, &ro) ==
              APP_HOST_LIVE_ERR_GENERATION);
        CHECK(app_host_live_read(31u, 0u, evts, 4u, &ro) ==
              APP_HOST_LIVE_ERR_GENERATION);
    }
}

static void test_event_records(void)
{
    app_host_live_identity_t ident;
    app_host_live_event_t evts[20];
    app_host_live_read_out_t ro;
    app_host_live_status_t st;
    uint8_t buf[APP_HOST_LIVE_PUBLISH_CAPACITY];
    uint32_t len;
    uint32_t i;

    fake_backend_init();
    lv_identity_fill(&ident, 41u, 9008u, 64u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_OK);
    len = APP_HOST_LIVE_HEADER_SIZE + 1u * APP_HOST_LIVE_EVENT_RECORD_SIZE;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, len, 1u, 1u, 1u, 0u, 5u, 1u,
              0u);
    lv_event(buf + APP_HOST_LIVE_HEADER_SIZE, 1u, 100u, 1234u,
             APP_HOST_LIVE_DIRECTION_IN,
             APP_HOST_LIVE_EVENT_FLAG_SOURCE_UNCERTAIN, 12u, 34u);
    g_fake_epoch_ms = 1760000000000ull;
    CHECK(app_host_live_ingest(buf, len, 41u, 4u) == APP_HOST_LIVE_OK);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(41u, 0u, evts, 20u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 1u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_CONTINUOUS);
    CHECK(evts[0].cursor == 1u);
    CHECK(evts[0].event_seq == 1u);
    CHECK(evts[0].track_id == 100u);
    CHECK(evts[0].event_ts_ms == g_fake_epoch_ms);
    CHECK(evts[0].event_ts_ms != 1234u);
    CHECK(evts[0].direction == APP_HOST_LIVE_DIRECTION_IN);
    CHECK(evts[0].flags == APP_HOST_LIVE_EVENT_FLAG_SOURCE_UNCERTAIN);
    CHECK(evts[0].x_q8 == 12u && evts[0].y_q8 == 34u);
    CHECK(ro.next_cursor == 2u);
    CHECK(app_host_live_read(41u, 1u, evts, 20u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 0u);
    CHECK(ro.next_cursor == 2u);

    len = APP_HOST_LIVE_HEADER_SIZE + 16u * APP_HOST_LIVE_EVENT_RECORD_SIZE;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, len, 2u, 17u, 17u, 0u, 5u,
              16u, 0u);
    for (i = 0u; i < 16u; i++) {
        lv_event(buf + APP_HOST_LIVE_HEADER_SIZE +
                     i * APP_HOST_LIVE_EVENT_RECORD_SIZE,
                 i + 2u, 200u + i, 2000u + i,
                 (i % 2u == 0u) ? APP_HOST_LIVE_DIRECTION_OUT
                                : APP_HOST_LIVE_DIRECTION_IN,
                 0u, (uint8_t)i, (uint8_t)(255u - i));
    }
    CHECK(app_host_live_ingest(buf, len, 41u, 4u) == APP_HOST_LIVE_OK);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(41u, 1u, evts, 10u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 10u);
    CHECK(evts[0].event_seq == 2u && evts[9].event_seq == 11u);
    CHECK(evts[0].cursor == 2u && evts[9].cursor == 11u);
    CHECK(app_host_live_read(41u, evts[9].cursor, evts + 10u, 10u, &ro) ==
          APP_HOST_LIVE_OK);
    CHECK(ro.returned == 6u);
    CHECK(evts[10].event_seq == 12u && evts[15].event_seq == 17u);
    CHECK(ro.next_cursor == 18u);
    CHECK(ro.host_dropped_events == 0u);

    len = APP_HOST_LIVE_HEADER_SIZE + 17u * APP_HOST_LIVE_EVENT_RECORD_SIZE;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, len, 3u, 34u, 34u, 0u, 5u,
              17u, 0u);
    CHECK(app_host_live_ingest(buf, len, 41u, 4u) == APP_HOST_LIVE_ERR_OVERFLOW);

    len = 60u;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 60u, 3u, 18u, 18u, 0u,
              5u, 1u, 0u);
    lv_event(buf + APP_HOST_LIVE_HEADER_SIZE, 18u, 9u, 1u,
             APP_HOST_LIVE_DIRECTION_IN, 0u, 0u, 0u);
    CHECK(app_host_live_ingest(buf, len, 41u, 4u) == APP_HOST_LIVE_ERR_LENGTH);

    len = 65u;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 65u, 3u, 18u, 18u, 0u,
              5u, 1u, 0u);
    lv_event(buf + APP_HOST_LIVE_HEADER_SIZE, 18u, 9u, 1u,
             APP_HOST_LIVE_DIRECTION_IN, 0u, 0u, 0u);
    buf[64u] = 0xAAu;
    CHECK(app_host_live_ingest(buf, len, 41u, 4u) == APP_HOST_LIVE_ERR_LENGTH);

    len = 64u;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 64u, 3u, 18u, 18u, 0u,
              5u, 1u, 0u);
    lv_event(buf + APP_HOST_LIVE_HEADER_SIZE, 0u, 9u, 1u,
             APP_HOST_LIVE_DIRECTION_IN, 0u, 0u, 0u);
    CHECK(app_host_live_ingest(buf, len, 41u, 4u) == APP_HOST_LIVE_ERR_RECORD);

    len = 64u;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 64u, 3u, 18u, 18u, 0u,
              5u, 1u, 0u);
    lv_event(buf + APP_HOST_LIVE_HEADER_SIZE, 10u, 9u, 1u,
             APP_HOST_LIVE_DIRECTION_IN, 0u, 0u, 0u);
    CHECK(app_host_live_ingest(buf, len, 41u, 4u) == APP_HOST_LIVE_ERR_RECORD);

    len = 64u;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 64u, 3u, 18u, 18u, 0u,
              5u, 1u, 0u);
    lv_event(buf + APP_HOST_LIVE_HEADER_SIZE, 30u, 9u, 1u,
             APP_HOST_LIVE_DIRECTION_IN, 0u, 0u, 0u);
    CHECK(app_host_live_ingest(buf, len, 41u, 4u) == APP_HOST_LIVE_ERR_RECORD);

    len = 64u;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 64u, 3u, 18u, 18u, 0u,
              5u, 1u, 0u);
    lv_event(buf + APP_HOST_LIVE_HEADER_SIZE, 18u, 9u, 1u, 3u, 0u, 0u, 0u);
    CHECK(app_host_live_ingest(buf, len, 41u, 4u) == APP_HOST_LIVE_ERR_RECORD);

    len = 64u;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 64u, 3u, 18u, 18u, 0u,
              5u, 1u, 0u);
    lv_event(buf + APP_HOST_LIVE_HEADER_SIZE, 18u, 9u, 1u,
             APP_HOST_LIVE_DIRECTION_OUT, 0x02u, 0u, 0u);
    CHECK(app_host_live_ingest(buf, len, 41u, 4u) == APP_HOST_LIVE_ERR_RECORD);

    len = 64u;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 64u, 3u, 18u, 18u, 0u,
              5u, 1u, 0u);
    lv_event(buf + APP_HOST_LIVE_HEADER_SIZE, 18u, 9u, 1u,
             APP_HOST_LIVE_DIRECTION_OUT, 0u, 0u, 0u);
    CHECK(app_host_live_ingest(buf, len, 41u, 4u) == APP_HOST_LIVE_OK);

    len = 64u;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 64u, 4u, 19u, 19u, 0u,
              5u, 1u, 0u);
    lv_event(buf + APP_HOST_LIVE_HEADER_SIZE, 18u, 9u, 1u,
             APP_HOST_LIVE_DIRECTION_OUT, 0u, 0u, 0u);
    CHECK(app_host_live_ingest(buf, len, 41u, 4u) == APP_HOST_LIVE_ERR_RECORD);

    app_host_live_status(&st);
    CHECK(st.event_count == 18u);
    CHECK(st.packet_count == 3u);
    CHECK(st.rejected_record == 6u);
    CHECK(st.rejected_overflow == 1u);
    CHECK(st.rejected_length == 2u);
    CHECK(st.retained_events == 18u);
    CHECK(st.last_packet_seq == 3u);
}

static void test_drop_oldest_and_reader_hole(void)
{
    app_host_live_identity_t ident;
    app_host_live_event_t evts[APP_HOST_LIVE_EVENT_RING_SLOTS];
    app_host_live_read_out_t ro;
    app_host_live_status_t st;
    uint8_t buf[APP_HOST_LIVE_PUBLISH_CAPACITY];
    uint32_t len;
    uint32_t i;
    uint32_t packet;

    fake_backend_init();
    lv_identity_fill(&ident, 51u, 9008u, 64u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_OK);
    for (packet = 0u; packet < 5u; packet++) {
        len = APP_HOST_LIVE_HEADER_SIZE +
              16u * APP_HOST_LIVE_EVENT_RECORD_SIZE;
        lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, len, packet + 1u,
                  (packet + 1u) * 16u, (packet + 1u) * 16u, 0u, 5u, 16u, 0u);
        for (i = 0u; i < 16u; i++) {
            lv_event(buf + APP_HOST_LIVE_HEADER_SIZE +
                         i * APP_HOST_LIVE_EVENT_RECORD_SIZE,
                     packet * 16u + i + 1u, 1u, packet * 16u + i,
                     APP_HOST_LIVE_DIRECTION_IN, 0u, 0u, 0u);
        }
        CHECK(app_host_live_ingest(buf, len, 51u, 4u) == APP_HOST_LIVE_OK);
    }
    app_host_live_status(&st);
    CHECK(st.event_count == 80u);
    CHECK(st.host_dropped_events == 16u);
    CHECK(st.retained_events == APP_HOST_LIVE_EVENT_RING_SLOTS);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(51u, 0u, evts, APP_HOST_LIVE_EVENT_RING_SLOTS,
                             &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == APP_HOST_LIVE_EVENT_RING_SLOTS);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_GAPPED);
    CHECK(ro.host_dropped_events == 16u);
    CHECK(ro.oldest_cursor == 17u);
    CHECK(ro.newest_cursor == 80u);
    CHECK(ro.next_cursor == 81u);
    CHECK(evts[0].event_seq == 17u);
    CHECK(evts[63].event_seq == 80u);
    CHECK(evts[0].cursor == 17u);
    CHECK(app_host_live_read(51u, 16u, evts, 4u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_CONTINUOUS);
    CHECK(app_host_live_read(51u, 80u, evts, 4u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 0u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_CONTINUOUS);
    CHECK(ro.next_cursor == 81u);
}

static void test_tracks_snapshot(void)
{
    app_host_live_identity_t ident;
    app_host_live_track_t tracks[APP_HOST_LIVE_TRACK_MAX];
    app_host_live_snapshot_out_t so;
    app_host_live_status_t st;
    uint8_t buf[APP_HOST_LIVE_PUBLISH_CAPACITY];
    uint32_t body;
    uint32_t len;
    uint32_t i;

    fake_backend_init();
    lv_identity_fill(&ident, 61u, 9008u, 64u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_OK);
    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u, 1u, 0u, 0u,
                     0u, 5u, 2u, 0u);
    body += lv_track(buf + body, 7u, 0u, 3u, 1u, 0u, 65535u);
    body += lv_track(buf + body, 9u, 16u, APP_HOST_LIVE_CLASS_UNKNOWN, 1u, 0u,
                     1234u);
    lv_put_u32(buf + 8u, body);
    CHECK(app_host_live_ingest(buf, body, 61u, 4u) == APP_HOST_LIVE_OK);
    memset(&so, 0, sizeof(so));
    CHECK(app_host_live_snapshot_read(61u, tracks, APP_HOST_LIVE_TRACK_MAX,
                                      &so) == APP_HOST_LIVE_OK);
    CHECK(so.track_count == 2u);
    CHECK(so.packet_seq == 1u);
    CHECK(so.session_generation == 61u);
    CHECK(tracks[0].track_id == 7u);
    CHECK(tracks[0].point_count == 0u);
    CHECK(tracks[0].class_index == 3u);
    CHECK(tracks[0].confidence_q16 == 65535u);
    CHECK(tracks[1].track_id == 9u);
    CHECK(tracks[1].point_count == 16u);
    CHECK(tracks[1].class_index == APP_HOST_LIVE_CLASS_UNKNOWN);
    CHECK(tracks[1].points[0].ts_ms == 1000u);
    CHECK(tracks[1].points[0].x_q16 == 0u);
    CHECK(tracks[1].points[0].y_q16 == 65535u);
    CHECK(tracks[1].points[15].ts_ms == 1015u);
    CHECK(tracks[1].points[15].x_q16 == 61440u);
    CHECK(tracks[1].points[15].y_q16 == 4095u);

    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 48u, 2u, 0u, 0u,
                     0u, 5u, 0u, 0u);
    CHECK(app_host_live_ingest(buf, 48u, 61u, 2u) == APP_HOST_LIVE_OK);
    memset(&so, 0, sizeof(so));
    CHECK(app_host_live_snapshot_read(61u, tracks, APP_HOST_LIVE_TRACK_MAX,
                                      &so) == APP_HOST_LIVE_OK);
    CHECK(so.track_count == 0u);

    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u, 3u, 0u, 0u,
                     0u, 5u, 3u, 0u);
    body += lv_track(buf + body, 1u, 1u, 0u, 1u, 0u, 100u);
    body += lv_track(buf + body, 2u, 1u, 0u, 1u, 0u, 100u);
    body += lv_track(buf + body, 3u, 1u, 0u, 1u, 0u, 100u);
    lv_put_u32(buf + 8u, body);
    CHECK(app_host_live_ingest(buf, body, 61u, 2u) == APP_HOST_LIVE_OK);
    CHECK(app_host_live_snapshot_read(61u, tracks, 2u, &so) ==
          APP_HOST_LIVE_ERR_OVERFLOW);

    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 68u, 4u, 0u, 0u,
                     0u, 5u, 1u, 0u);
    body += lv_track(buf + body, 4u, 0u, 0u, 1u, 0u, 100u);
    lv_put_u32(buf + 8u, 68u);
    CHECK(app_host_live_ingest(buf, 68u, 61u, 2u) ==
          APP_HOST_LIVE_ERR_LENGTH);
    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 68u, 4u, 0u, 0u,
                     0u, 5u, 1u, 0u);
    body += lv_track(buf + body, 4u, 2u, 0u, 1u, 0u, 100u);
    CHECK(app_host_live_ingest(buf, 68u, 61u, 2u) ==
          APP_HOST_LIVE_ERR_LENGTH);

    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u, 4u, 0u, 0u,
                     0u, 5u, 1u, 0u);
    body += lv_track(buf + body, 5u, APP_HOST_LIVE_TRACK_POINT_MAX + 1u, 0u, 1u,
                     0u, 100u);
    lv_put_u32(buf + 8u, body);
    CHECK(app_host_live_ingest(buf, body, 61u, 2u) == APP_HOST_LIVE_ERR_RECORD);

    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u, 4u, 0u, 0u,
                     0u, 5u, 1u, 0u);
    body += lv_track(buf + body, 0u, 1u, 0u, 1u, 0u, 100u);
    lv_put_u32(buf + 8u, body);
    CHECK(app_host_live_ingest(buf, body, 61u, 2u) == APP_HOST_LIVE_ERR_RECORD);

    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u, 4u, 0u, 0u,
                     0u, 5u, 2u, 0u);
    body += lv_track(buf + body, 6u, 1u, 0u, 1u, 0u, 100u);
    body += lv_track(buf + body, 6u, 1u, 0u, 1u, 0u, 100u);
    lv_put_u32(buf + 8u, body);
    CHECK(app_host_live_ingest(buf, body, 61u, 2u) == APP_HOST_LIVE_ERR_RECORD);

    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u, 4u, 0u, 0u,
                     0u, 5u, 1u, 0u);
    body += lv_track(buf + body, 6u, 1u, 0u, 2u, 0u, 100u);
    lv_put_u32(buf + 8u, body);
    CHECK(app_host_live_ingest(buf, body, 61u, 2u) == APP_HOST_LIVE_ERR_RECORD);

    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u, 4u, 0u, 0u,
                     0u, 5u, 1u, 0u);
    body += lv_track(buf + body, 6u, 1u, 0u, 1u, 1u, 100u);
    lv_put_u32(buf + 8u, body);
    CHECK(app_host_live_ingest(buf, body, 61u, 2u) == APP_HOST_LIVE_ERR_RECORD);

    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u, 4u, 0u, 0u,
                     0u, 5u, 1u, 0u);
    body += lv_track(buf + body, 6u, 1u, 2u, 1u, 0u, 100u);
    lv_put_u32(buf + 8u, body);
    CHECK(app_host_live_ingest(buf, body, 61u, 2u) == APP_HOST_LIVE_ERR_RECORD);

    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u, 5u, 0u, 0u,
                     0u, 5u, 1u, 0u);
    body += lv_track(buf + body, 6u, 1u, APP_HOST_LIVE_CLASS_UNKNOWN, 1u, 0u,
                     100u);
    lv_put_u32(buf + 8u, body);
    CHECK(app_host_live_ingest(buf, body, 61u, 0u) == APP_HOST_LIVE_OK);

    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u, 6u, 0u, 0u,
                     0u, 5u, 1u, 0u);
    body += lv_track(buf + body, 6u, 1u, 2u, 1u, 0u, 100u);
    lv_put_u32(buf + 8u, body);
    CHECK(app_host_live_ingest(buf, body, 61u, 2u) == APP_HOST_LIVE_ERR_RECORD);
    CHECK(app_host_live_ingest(buf, body, 61u, 3u) == APP_HOST_LIVE_OK);

    body = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 48u, 7u, 0u, 0u,
                     0u, 5u, 65u, 0u);
    CHECK(app_host_live_ingest(buf, 48u, 61u, 3u) ==
          APP_HOST_LIVE_ERR_OVERFLOW);

    app_host_live_status(&st);
    CHECK(st.snapshot_count == 5u);
    CHECK(st.snapshot_tracks == 1u);
    CHECK(st.rejected_record == 7u);
    CHECK(st.rejected_length == 2u);
    CHECK(st.rejected_overflow == 1u);
    CHECK(st.packet_count == 5u);
    CHECK(app_host_live_snapshot_read(60u, tracks, APP_HOST_LIVE_TRACK_MAX,
                                      &so) == APP_HOST_LIVE_ERR_GENERATION);

    for (i = 0u; i < APP_HOST_LIVE_TRACK_MAX; i++) {
        len = lv_header(buf, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u, 8u + i, 0u,
                        0u, 0u, 5u, 1u, 0u);
        len += lv_track(buf + len, i + 1u, 16u, 1u, 1u, 0u, 100u);
        lv_put_u32(buf + 8u, len);
        CHECK(app_host_live_ingest(buf, len, 61u, 3u) == APP_HOST_LIVE_OK);
    }
    CHECK(app_host_live_ingest(buf, len, 61u, 3u) == APP_HOST_LIVE_OK);
    app_host_live_status(&st);
    CHECK(st.snapshot_tracks == 1u);
    CHECK(st.snapshot_count == 69u);
    CHECK(st.packet_count == 69u);
    CHECK(st.idempotent_replays == 1u);
}

static void test_freshness_and_lifecycle(void)
{
    app_host_live_identity_t ident;
    app_host_live_status_t st;
    app_host_live_read_out_t ro;
    app_host_live_event_t evts[2];
    uint32_t epoch;
    uint8_t buf[64];

    fake_backend_init();
    app_host_live_status(&st);
    epoch = st.view_epoch;
    lv_identity_fill(&ident, 71u, 312u, 1u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_OK);
    g_fake_now_ms = 100000u;
    lv_header(buf, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 1u, 0u, 0u, 0u, 7u, 0u,
              0u);
    CHECK(app_host_live_ingest(buf, 48u, 71u, 4u) == APP_HOST_LIVE_OK);
    app_host_live_status(&st);
    CHECK(st.freshness == APP_HOST_LIVE_FRESHNESS_FRESH);
    CHECK(st.age_ms == 0u);
    g_fake_now_ms = 100000u + APP_HOST_LIVE_STALE_MS + 1u;
    app_host_live_status(&st);
    CHECK(st.freshness == APP_HOST_LIVE_FRESHNESS_STALE);
    CHECK(st.age_ms == APP_HOST_LIVE_STALE_MS + 1u);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(71u, 0u, evts, 2u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.freshness == APP_HOST_LIVE_FRESHNESS_STALE);
    g_fake_now_ms = 0u;
    CHECK(strcmp(app_host_live_result_str(APP_HOST_LIVE_OK), "OK") == 0);
    CHECK(strcmp(app_host_live_result_str(APP_HOST_LIVE_ERR_CLOSED),
                 "CLOSED") == 0);
    CHECK(strcmp(app_host_live_result_str(99), "UNKNOWN") == 0);
    CHECK(strcmp(app_host_live_read_state_str(
                     APP_HOST_LIVE_READ_STATE_CONTINUOUS),
                 "continuous") == 0);
    CHECK(strcmp(app_host_live_read_state_str(APP_HOST_LIVE_READ_STATE_GAPPED),
                 "gapped") == 0);
    CHECK(strcmp(app_host_live_read_state_str(APP_HOST_LIVE_READ_STATE_UNKNOWN),
                 "unknown") == 0);
    CHECK(strcmp(app_host_live_freshness_str(APP_HOST_LIVE_FRESHNESS_FRESH),
                 "fresh") == 0);
    CHECK(strcmp(app_host_live_freshness_str(APP_HOST_LIVE_FRESHNESS_STALE),
                 "stale") == 0);
    CHECK(strcmp(app_host_live_freshness_str(APP_HOST_LIVE_FRESHNESS_NONE),
                 "none") == 0);
    app_host_live_end(71u);
    app_host_live_status(&st);
    CHECK(st.bound == AICAM_FALSE);
    CHECK(st.packet_count == 0u);
    CHECK(app_host_live_ingest(buf, 48u, 71u, 4u) == APP_HOST_LIVE_ERR_UNBOUND);
    CHECK(app_host_live_read(71u, 0u, evts, 2u, &ro) ==
          APP_HOST_LIVE_ERR_UNBOUND);
    lv_identity_fill(&ident, 72u, 312u, 1u);
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_OK);
    app_host_live_status(&st);
    CHECK(st.view_epoch == epoch);
    CHECK(app_host_live_read(71u, 0u, evts, 2u, &ro) ==
          APP_HOST_LIVE_ERR_GENERATION);
    CHECK(app_host_live_read(72u, 0u, evts, 2u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 0u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_UNKNOWN);
}

int main(void)
{
    test_bind_validation();
    test_header_validation();
    test_sequence_rules();
    test_watermark_and_generation();
    test_event_records();
    test_drop_oldest_and_reader_hole();
    test_tracks_snapshot();
    test_freshness_and_lifecycle();
    printf("app_host_live: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
