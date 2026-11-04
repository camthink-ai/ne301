#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "app_host_live.h"
#include "app27_live_fixtures.h"

static int g_total;
static int g_fail;
static uint32_t g_fake_now_ms;
static uint64_t g_fake_epoch_ms;

#define CHECK(v) do { \
    g_total++; \
    if (!(v)) { \
        g_fail++; \
        printf("FAIL app_host_live_joint:%d %s\n", g_total, #v); \
    } \
} while (0)

static const uint32_t sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t sha_rotr(uint32_t v, uint32_t n)
{
    return (v >> n) | (v << (32u - n));
}

static void sha256_block(uint32_t *h, const uint8_t *p)
{
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t hh;
    uint32_t t1;
    uint32_t t2;
    uint32_t i;

    for (i = 0u; i < 16u; i++) {
        w[i] = ((uint32_t)p[i * 4u] << 24) |
               ((uint32_t)p[i * 4u + 1u] << 16) |
               ((uint32_t)p[i * 4u + 2u] << 8) |
               ((uint32_t)p[i * 4u + 3u]);
    }
    for (i = 16u; i < 64u; i++) {
        uint32_t s0 = sha_rotr(w[i - 15u], 7u) ^ sha_rotr(w[i - 15u], 18u) ^
                      (w[i - 15u] >> 3u);
        uint32_t s1 = sha_rotr(w[i - 2u], 17u) ^ sha_rotr(w[i - 2u], 19u) ^
                      (w[i - 2u] >> 10u);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    a = h[0];
    b = h[1];
    c = h[2];
    d = h[3];
    e = h[4];
    f = h[5];
    g = h[6];
    hh = h[7];
    for (i = 0u; i < 64u; i++) {
        uint32_t s1 = sha_rotr(e, 6u) ^ sha_rotr(e, 11u) ^ sha_rotr(e, 25u);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t s0 = sha_rotr(a, 2u) ^ sha_rotr(a, 13u) ^ sha_rotr(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);

        t1 = hh + s1 + ch + sha256_k[i] + w[i];
        t2 = s0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

static void sha256(const uint8_t *msg, uint32_t len, uint8_t out[32])
{
    uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    uint8_t tail[64];
    uint64_t bit_len = (uint64_t)len * 8u;
    uint32_t offset = 0u;
    uint32_t rem;
    uint32_t i;

    while (offset + 64u <= len) {
        sha256_block(h, msg + offset);
        offset += 64u;
    }
    rem = len - offset;
    memset(tail, 0, sizeof(tail));
    memcpy(tail, msg + offset, rem);
    tail[rem] = 0x80u;
    if (rem >= 56u) {
        sha256_block(h, tail);
        memset(tail, 0, sizeof(tail));
    }
    for (i = 0u; i < 8u; i++) {
        tail[56u + i] = (uint8_t)(bit_len >> (56u - 8u * i));
    }
    sha256_block(h, tail);
    for (i = 0u; i < 8u; i++) {
        out[i * 4u] = (uint8_t)(h[i] >> 24);
        out[i * 4u + 1u] = (uint8_t)(h[i] >> 16);
        out[i * 4u + 2u] = (uint8_t)(h[i] >> 8);
        out[i * 4u + 3u] = (uint8_t)(h[i]);
    }
}

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

static void joint_identity(app_host_live_identity_t *ident, uint32_t generation,
                           uint32_t publish_max, uint32_t track_max)
{
    memset(ident, 0, sizeof(*ident));
    ident->session_generation = generation;
    snprintf(ident->publisher_id, sizeof(ident->publisher_id), "%s",
             "ne301-exp-pub");
    snprintf(ident->app_id, sizeof(ident->app_id), "%s", "hello-app");
    ident->spki_sha256[0] = 0xA1u;
    ident->tbs_sha256[0] = 0xB2u;
    ident->package_sha256[0] = 0xC3u;
    ident->live_publish_max = publish_max;
    ident->live_track_max = track_max;
}

static int32_t joint_bind(uint32_t generation, uint32_t publish_max,
                          uint32_t track_max)
{
    app_host_live_identity_t ident;

    joint_identity(&ident, generation, publish_max, track_max);
    return app_host_live_bind(&ident);
}

static uint32_t fixture_u16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t fixture_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void check_fixture_pin(const char *name, const uint8_t *data,
                              uint32_t len, const uint8_t expect[32])
{
    uint8_t got[32];

    sha256(data, len, got);
    g_total++;
    if (memcmp(got, expect, 32u) != 0) {
        g_fail++;
        printf("FAIL app_host_live_joint:%d pin:%s\n", g_total, name);
    }
}

static void test_fixture_byte_pins(void)
{
    check_fixture_pin("kind1-event-batch", APP27_KIND1_EVENT_BATCH,
                      APP27_LEN_KIND1_EVENT_BATCH,
                      APP27_SHA256_KIND1_EVENT_BATCH);
    check_fixture_pin("kind1-source-gap", APP27_KIND1_SOURCE_GAP,
                      APP27_LEN_KIND1_SOURCE_GAP,
                      APP27_SHA256_KIND1_SOURCE_GAP);
    check_fixture_pin("kind2-tracks-64x16", APP27_KIND2_TRACKS_64X16,
                      APP27_LEN_KIND2_TRACKS_64X16,
                      APP27_SHA256_KIND2_TRACKS_64X16);
    check_fixture_pin("kind2-zero-track", APP27_KIND2_ZERO_TRACK,
                      APP27_LEN_KIND2_ZERO_TRACK,
                      APP27_SHA256_KIND2_ZERO_TRACK);
    check_fixture_pin("kind1-resync", APP27_KIND1_RESYNC,
                      APP27_LEN_KIND1_RESYNC, APP27_SHA256_KIND1_RESYNC);
    check_fixture_pin("kind2-resync", APP27_KIND2_RESYNC,
                      APP27_LEN_KIND2_RESYNC, APP27_SHA256_KIND2_RESYNC);
    check_fixture_pin("slow-0", APP27_SLOW_READER_0, APP27_LEN_SLOW_READER_0,
                      APP27_SHA256_SLOW_READER_0);
    check_fixture_pin("slow-1", APP27_SLOW_READER_1, APP27_LEN_SLOW_READER_1,
                      APP27_SHA256_SLOW_READER_1);
    check_fixture_pin("slow-2", APP27_SLOW_READER_2, APP27_LEN_SLOW_READER_2,
                      APP27_SHA256_SLOW_READER_2);
    check_fixture_pin("slow-3", APP27_SLOW_READER_3, APP27_LEN_SLOW_READER_3,
                      APP27_SHA256_SLOW_READER_3);
    check_fixture_pin("slow-4", APP27_SLOW_READER_4, APP27_LEN_SLOW_READER_4,
                      APP27_SHA256_SLOW_READER_4);
    check_fixture_pin("slow-all", APP27_SLOW_READER,
                      APP27_SLOW_READER_COUNT * APP27_SLOW_READER_PACKET_LEN,
                      APP27_SHA256_SLOW_READER);

    CHECK(memcmp(APP27_KIND1_EVENT_BATCH, "LVN4", 4u) == 0);
    CHECK(fixture_u16(APP27_KIND1_EVENT_BATCH + 4u) == 1u);
    CHECK(fixture_u16(APP27_KIND1_EVENT_BATCH + 6u) ==
          APP_HOST_LIVE_KIND_EVENT_BATCH);
    CHECK(fixture_u32(APP27_KIND1_EVENT_BATCH + 8u) ==
          APP27_LEN_KIND1_EVENT_BATCH);
    CHECK(fixture_u32(APP27_KIND1_EVENT_BATCH + 36u) == 1u);
    CHECK(fixture_u32(APP27_KIND1_EVENT_BATCH + 40u) == 0u);
    CHECK(fixture_u32(APP27_KIND1_EVENT_BATCH + 44u) == 0u);
    CHECK(fixture_u16(APP27_KIND1_SOURCE_GAP + 6u) ==
          APP_HOST_LIVE_KIND_EVENT_BATCH);
    CHECK(fixture_u32(APP27_KIND1_SOURCE_GAP + 8u) ==
          APP27_LEN_KIND1_SOURCE_GAP);
    CHECK(fixture_u32(APP27_KIND1_SOURCE_GAP + 40u) ==
          APP_HOST_LIVE_FLAG_SOURCE_GAP_DELTA);
    CHECK(fixture_u16(APP27_KIND2_TRACKS_64X16 + 6u) ==
          APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT);
    CHECK(fixture_u32(APP27_KIND2_TRACKS_64X16 + 8u) ==
          APP27_LEN_KIND2_TRACKS_64X16);
    CHECK(fixture_u32(APP27_KIND2_TRACKS_64X16 + 36u) == 64u);
    CHECK(fixture_u16(APP27_KIND2_ZERO_TRACK + 6u) ==
          APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT);
    CHECK(fixture_u32(APP27_KIND2_ZERO_TRACK + 36u) == 0u);
    CHECK(fixture_u32(APP27_KIND1_RESYNC + 40u) ==
          APP_HOST_LIVE_FLAG_RESYNC_REQUIRED);
}

static void test_joint_event_batch_cursor(void)
{
    app_host_live_status_t st;
    app_host_live_read_out_t ro;
    app_host_live_event_t evts[8];

    g_fake_now_ms = 500u;
    g_fake_epoch_ms = 1760000000500ull;
    CHECK(joint_bind(9101u, 9008u, 64u) == APP_HOST_LIVE_OK);
    app_host_live_status(&st);
    CHECK(st.bound == AICAM_TRUE);
    CHECK(st.live_publish_max == 9008u);
    CHECK(st.live_track_max == 64u);
    CHECK(strcmp(st.publisher_id, "ne301-exp-pub") == 0);
    CHECK(strcmp(st.app_id, "hello-app") == 0);

    CHECK(app_host_live_ingest(APP27_KIND1_EVENT_BATCH,
                               APP27_LEN_KIND1_EVENT_BATCH, 9101u, 2u) ==
          APP_HOST_LIVE_OK);
    app_host_live_status(&st);
    CHECK(st.packet_count == 1u);
    CHECK(st.event_count == 1u);
    CHECK(st.rejected_total == 0u);
    CHECK(st.through_seq == 1u);
    CHECK(st.source_total == 1u);
    CHECK(st.source_gap == 0u);

    memset(&ro, 0, sizeof(ro));
    memset(evts, 0, sizeof(evts));
    CHECK(app_host_live_read(9101u, 0u, evts, 8u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 1u);
    CHECK(evts[0].cursor == 1u);
    CHECK(evts[0].event_seq == 1u);
    CHECK(evts[0].track_id == 1u);
    CHECK(evts[0].event_ts_ms == g_fake_epoch_ms);
    CHECK(evts[0].event_ts_ms != 2800u);
    CHECK(evts[0].direction == APP_HOST_LIVE_DIRECTION_IN);
    CHECK(evts[0].flags == 0u);
    CHECK(evts[0].x_q8 == 128u);
    CHECK(evts[0].y_q8 == 148u);
    CHECK(ro.through_seq == 1u);
    CHECK(ro.source_total == 1u);
    CHECK(ro.source_gap == 0u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_CONTINUOUS);
    CHECK(ro.freshness == APP_HOST_LIVE_FRESHNESS_FRESH);
    CHECK(ro.last_packet_seq == 35u);
    CHECK(ro.packet_count == 1u);
    CHECK(ro.session_generation == 9101u);
    CHECK(ro.next_cursor == 2u);
    CHECK(ro.oldest_cursor == 1u);
    CHECK(ro.newest_cursor == 1u);
    CHECK(ro.host_dropped_events == 0u);

    CHECK(app_host_live_ingest(APP27_KIND1_EVENT_BATCH,
                               APP27_LEN_KIND1_EVENT_BATCH, 9101u, 2u) ==
          APP_HOST_LIVE_OK);
    app_host_live_status(&st);
    CHECK(st.idempotent_replays == 1u);
    CHECK(st.event_count == 1u);
    CHECK(st.packet_count == 1u);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(9101u, 0u, evts, 8u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 1u);
    CHECK(ro.next_cursor == 2u);

    CHECK(app_host_live_ingest(APP27_KIND1_SOURCE_GAP,
                               APP27_LEN_KIND1_SOURCE_GAP, 9101u, 2u) ==
          APP_HOST_LIVE_ERR_SEQUENCE);
    app_host_live_status(&st);
    CHECK(st.rejected_sequence == 1u);
    CHECK(st.through_seq == 1u);
    CHECK(app_host_live_ingest(APP27_KIND2_TRACKS_64X16,
                               APP27_LEN_KIND2_TRACKS_64X16, 9101u, 2u) ==
          APP_HOST_LIVE_ERR_SEQUENCE);
    app_host_live_status(&st);
    CHECK(st.rejected_sequence == 2u);
    CHECK(st.event_count == 1u);
    app_host_live_end(9101u);
}

static void test_joint_source_gap_and_zero_track(void)
{
    app_host_live_status_t st;
    app_host_live_read_out_t ro;
    app_host_live_snapshot_out_t so;
    app_host_live_event_t evts[32];
    app_host_live_track_t tracks[4];
    uint32_t i;

    g_fake_now_ms = 600u;
    g_fake_epoch_ms = 1760000000600ull;
    CHECK(joint_bind(9102u, 9008u, 64u) == APP_HOST_LIVE_OK);
    memset(&so, 0, sizeof(so));
    CHECK(app_host_live_snapshot_read(9102u, tracks, 4u, &so) ==
          APP_HOST_LIVE_OK);
    CHECK(so.track_count == 0u);
    CHECK(so.packet_seq == 0u);
    CHECK(so.freshness == APP_HOST_LIVE_FRESHNESS_NONE);

    CHECK(app_host_live_ingest(APP27_KIND1_SOURCE_GAP,
                               APP27_LEN_KIND1_SOURCE_GAP, 9102u, 2u) ==
          APP_HOST_LIVE_OK);
    memset(&ro, 0, sizeof(ro));
    memset(evts, 0, sizeof(evts));
    CHECK(app_host_live_read(9102u, 0u, evts, 32u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 16u);
    CHECK(ro.through_seq == 64u);
    CHECK(ro.source_total == 64u);
    CHECK(ro.source_gap == 48u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_GAPPED);
    CHECK(ro.oldest_cursor == 1u);
    CHECK(ro.newest_cursor == 16u);
    CHECK(ro.host_dropped_events == 0u);
    for (i = 0u; i < 16u; i++) {
        CHECK(evts[i].event_seq == i + 1u);
        CHECK(evts[i].track_id == i + 1u);
        CHECK(evts[i].event_ts_ms == g_fake_epoch_ms);
        CHECK(evts[i].direction == APP_HOST_LIVE_DIRECTION_IN);
        CHECK(evts[i].y_q8 == 148u);
    }
    CHECK(evts[0].x_q8 == 5u);
    CHECK(evts[15].x_q8 == 62u);

    CHECK(app_host_live_ingest(APP27_KIND2_ZERO_TRACK,
                               APP27_LEN_KIND2_ZERO_TRACK, 9102u, 2u) ==
          APP_HOST_LIVE_OK);
    memset(&so, 0, sizeof(so));
    CHECK(app_host_live_snapshot_read(9102u, tracks, 4u, &so) ==
          APP_HOST_LIVE_OK);
    CHECK(so.track_count == 0u);
    CHECK(so.packet_seq == 48u);
    CHECK(so.through_seq == 64u);
    CHECK(so.source_total == 64u);
    CHECK(so.source_gap == 48u);
    CHECK(so.freshness == APP_HOST_LIVE_FRESHNESS_FRESH);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(9102u, 0u, evts, 32u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_GAPPED);
    CHECK(ro.through_seq == 64u);
    CHECK(ro.source_gap == 48u);
    app_host_live_status(&st);
    CHECK(st.event_count == 16u);
    CHECK(st.snapshot_count == 1u);
    CHECK(st.packet_count == 2u);
    app_host_live_end(9102u);
}

static void test_joint_tracks_64x16_snapshot(void)
{
    app_host_live_snapshot_out_t so;
    app_host_live_read_out_t ro;
    app_host_live_track_t tracks[64];
    app_host_live_event_t evts[1];

    g_fake_now_ms = 700u;
    CHECK(joint_bind(9103u, 9008u, 64u) == APP_HOST_LIVE_OK);
    CHECK(app_host_live_ingest(APP27_KIND2_TRACKS_64X16,
                               APP27_LEN_KIND2_TRACKS_64X16, 9103u, 2u) ==
          APP_HOST_LIVE_OK);
    memset(&so, 0, sizeof(so));
    memset(tracks, 0, sizeof(tracks));
    CHECK(app_host_live_snapshot_read(9103u, tracks, 64u, &so) ==
          APP_HOST_LIVE_OK);
    CHECK(so.track_count == 64u);
    CHECK(so.packet_seq == 32u);
    CHECK(so.through_seq == 0u);
    CHECK(so.source_total == 0u);
    CHECK(so.source_gap == 0u);
    CHECK(so.freshness == APP_HOST_LIVE_FRESHNESS_FRESH);
    CHECK(so.session_generation == 9103u);
    CHECK(tracks[0].track_id == 1u);
    CHECK(tracks[0].point_count == 16u);
    CHECK(tracks[0].class_index == 1u);
    CHECK(tracks[0].state == APP_HOST_LIVE_TRACK_STATE_ACTIVE);
    CHECK(tracks[0].flags == 0u);
    CHECK(tracks[0].confidence_q16 == 0u);
    CHECK(tracks[0].points[0].ts_ms == 1000u);
    CHECK(tracks[0].points[0].x_q16 == 1311u);
    CHECK(tracks[0].points[0].y_q16 == 27525u);
    CHECK(tracks[0].points[15].ts_ms == 2500u);
    CHECK(tracks[0].points[15].x_q16 == 1311u);
    CHECK(tracks[1].points[0].x_q16 == 2294u);
    CHECK(tracks[63].track_id == 64u);
    CHECK(tracks[63].point_count == 16u);
    CHECK(tracks[63].points[0].x_q16 == 63241u);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(9103u, 0u, evts, 1u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 0u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_CONTINUOUS);
    g_fake_now_ms = 700u + 10001u;
    memset(&so, 0, sizeof(so));
    CHECK(app_host_live_snapshot_read(9103u, tracks, 64u, &so) ==
          APP_HOST_LIVE_OK);
    CHECK(so.freshness == APP_HOST_LIVE_FRESHNESS_STALE);
    CHECK(so.age_ms == 10001u);
    app_host_live_end(9103u);
}

static void test_joint_resync_and_recovery(void)
{
    app_host_live_read_out_t ro;
    app_host_live_snapshot_out_t so;
    app_host_live_event_t evts[4];
    app_host_live_track_t tracks[4];

    g_fake_now_ms = 800u;
    CHECK(joint_bind(9104u, 9008u, 64u) == APP_HOST_LIVE_OK);
    CHECK(app_host_live_ingest(APP27_KIND1_RESYNC, APP27_LEN_KIND1_RESYNC,
                               9104u, 2u) == APP_HOST_LIVE_OK);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(9104u, 0u, evts, 4u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 0u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_UNKNOWN);
    CHECK(ro.resync_required == AICAM_TRUE);
    CHECK(ro.loss_unknown == AICAM_FALSE);
    CHECK(ro.through_seq == 0u);
    CHECK(ro.source_gap == 0u);
    CHECK(ro.packet_count == 1u);

    CHECK(app_host_live_ingest(APP27_KIND2_RESYNC, APP27_LEN_KIND2_RESYNC,
                               9104u, 2u) == APP_HOST_LIVE_OK);
    memset(&so, 0, sizeof(so));
    memset(tracks, 0, sizeof(tracks));
    CHECK(app_host_live_snapshot_read(9104u, tracks, 4u, &so) ==
          APP_HOST_LIVE_OK);
    CHECK(so.track_count == 1u);
    CHECK(so.packet_seq == 2u);
    CHECK(tracks[0].track_id == 1u);
    CHECK(tracks[0].point_count == 1u);
    CHECK(tracks[0].class_index == 1u);
    CHECK(tracks[0].points[0].ts_ms == 1000u);
    CHECK(tracks[0].points[0].x_q16 == 32768u);
    CHECK(tracks[0].points[0].y_q16 == 27525u);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(9104u, 0u, evts, 4u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_CONTINUOUS);
    CHECK(ro.resync_required == AICAM_FALSE);
    app_host_live_end(9104u);
}

static void test_joint_slow_reader_host_gap(void)
{
    app_host_live_status_t st;
    app_host_live_read_out_t ro;
    app_host_live_event_t evts[128];
    uint32_t i;

    g_fake_now_ms = 900u;
    CHECK(joint_bind(9105u, 9008u, 64u) == APP_HOST_LIVE_OK);
    for (i = 0u; i < APP27_SLOW_READER_COUNT; i++) {
        CHECK(app_host_live_ingest(APP27_SLOW_READER + i *
                                   APP27_SLOW_READER_PACKET_LEN,
                                   APP27_SLOW_READER_PACKET_LEN, 9105u, 2u) ==
              APP_HOST_LIVE_OK);
    }
    app_host_live_status(&st);
    CHECK(st.event_count == 80u);
    CHECK(st.host_dropped_events == 16u);
    CHECK(st.retained_events == 64u);
    CHECK(st.through_seq == 80u);
    CHECK(st.source_gap == 0u);
    memset(&ro, 0, sizeof(ro));
    memset(evts, 0, sizeof(evts));
    CHECK(app_host_live_read(9105u, 0u, evts, 128u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 64u);
    CHECK(ro.oldest_cursor == 17u);
    CHECK(ro.newest_cursor == 80u);
    CHECK(ro.host_dropped_events == 16u);
    CHECK(ro.through_seq == 80u);
    CHECK(ro.source_total == 80u);
    CHECK(ro.source_gap == 0u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_GAPPED);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(9105u, 16u, evts, 128u, &ro) ==
          APP_HOST_LIVE_OK);
    CHECK(ro.returned == 64u);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(9105u, 80u, evts, 128u, &ro) ==
          APP_HOST_LIVE_OK);
    CHECK(ro.returned == 0u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_CONTINUOUS);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(9105u, 0u, evts, 128u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_GAPPED);
    app_host_live_end(9105u);
}

static void test_joint_negatives(void)
{
    app_host_live_status_t st;
    uint8_t v3_wire[48];
    app_host_live_read_out_t ro;
    app_host_live_snapshot_out_t so;
    app_host_live_event_t evts[4];

    g_fake_now_ms = 1000u;
    CHECK(joint_bind(9106u, 9008u, 64u) == APP_HOST_LIVE_OK);
    memset(v3_wire, 0, sizeof(v3_wire));
    memcpy(v3_wire, "NFC3", 4u);
    v3_wire[8] = 48u;
    CHECK(app_host_live_ingest(v3_wire, sizeof(v3_wire), 9106u, 2u) ==
          APP_HOST_LIVE_ERR_HEADER);
    app_host_live_status(&st);
    CHECK(st.rejected_header == 1u);
    CHECK(st.packet_count == 0u);
    app_host_live_end(9106u);
    CHECK(app_host_live_ingest(APP27_KIND1_EVENT_BATCH,
                               APP27_LEN_KIND1_EVENT_BATCH, 9106u, 2u) ==
          APP_HOST_LIVE_ERR_UNBOUND);
    CHECK(app_host_live_read(9106u, 0u, evts, 4u, &ro) ==
          APP_HOST_LIVE_ERR_UNBOUND);
    memset(&so, 0, sizeof(so));
    CHECK(app_host_live_snapshot_read(9106u, NULL, 0u, &so) ==
          APP_HOST_LIVE_ERR_UNBOUND);

    CHECK(joint_bind(9107u, 9008u, 64u) == APP_HOST_LIVE_OK);
    CHECK(app_host_live_ingest(APP27_KIND1_EVENT_BATCH,
                               APP27_LEN_KIND1_EVENT_BATCH, 9101u, 2u) ==
          APP_HOST_LIVE_ERR_GENERATION);
    CHECK(app_host_live_read(9101u, 0u, evts, 4u, &ro) ==
          APP_HOST_LIVE_ERR_GENERATION);
    app_host_live_end(9107u);
}

static void test_joint_cap_deficiency(void)
{
    app_host_live_status_t st;
    app_host_live_snapshot_out_t so;
    app_host_live_track_t tracks[4];

    g_fake_now_ms = 1100u;
    CHECK(joint_bind(9108u, 312u, 1u) == APP_HOST_LIVE_OK);
    app_host_live_status(&st);
    CHECK(st.live_publish_max == 312u);
    CHECK(st.live_track_max == 1u);
    CHECK(app_host_live_ingest(APP27_KIND1_SOURCE_GAP,
                               APP27_LEN_KIND1_SOURCE_GAP, 9108u, 2u) ==
          APP_HOST_LIVE_OK);
    CHECK(app_host_live_ingest(APP27_KIND2_ZERO_TRACK,
                               APP27_LEN_KIND2_ZERO_TRACK, 9108u, 2u) ==
          APP_HOST_LIVE_OK);
    CHECK(app_host_live_ingest(APP27_KIND2_TRACKS_64X16,
                               APP27_LEN_KIND2_TRACKS_64X16, 9108u, 2u) ==
          APP_HOST_LIVE_ERR_LENGTH);
    app_host_live_status(&st);
    CHECK(st.rejected_length == 1u);
    CHECK(st.packet_count == 2u);
    memset(&so, 0, sizeof(so));
    CHECK(app_host_live_snapshot_read(9108u, tracks, 4u, &so) ==
          APP_HOST_LIVE_OK);
    CHECK(so.track_count == 0u);
    app_host_live_end(9108u);
}

static void test_joint_grant_revoke(void)
{
    app_host_live_status_t st;
    app_host_live_read_out_t ro;
    app_host_live_event_t evts[4];

    g_fake_now_ms = 1200u;
    CHECK(joint_bind(9109u, 9008u, 64u) == APP_HOST_LIVE_OK);
    CHECK(app_host_live_ingest(APP27_KIND1_EVENT_BATCH,
                               APP27_LEN_KIND1_EVENT_BATCH, 9109u, 2u) ==
          APP_HOST_LIVE_OK);
    app_host_live_end(9109u);
    app_host_live_status(&st);
    CHECK(st.bound == AICAM_FALSE);
    CHECK(st.event_count == 0u);
    CHECK(st.retained_events == 0u);
    CHECK(app_host_live_ingest(APP27_KIND1_EVENT_BATCH,
                               APP27_LEN_KIND1_EVENT_BATCH, 9109u, 2u) ==
          APP_HOST_LIVE_ERR_UNBOUND);
    CHECK(app_host_live_read(9109u, 0u, evts, 4u, &ro) ==
          APP_HOST_LIVE_ERR_UNBOUND);
}

int main(void)
{
    fake_backend_init();
    test_fixture_byte_pins();
    fake_backend_init();
    test_joint_event_batch_cursor();
    fake_backend_init();
    test_joint_source_gap_and_zero_track();
    fake_backend_init();
    test_joint_tracks_64x16_snapshot();
    fake_backend_init();
    test_joint_resync_and_recovery();
    fake_backend_init();
    test_joint_slow_reader_host_gap();
    fake_backend_init();
    test_joint_negatives();
    fake_backend_init();
    test_joint_cap_deficiency();
    fake_backend_init();
    test_joint_grant_revoke();
    printf("app_host_live_joint: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
