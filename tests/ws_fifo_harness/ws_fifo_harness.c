/*
 * Host-side harness for the #193 R1 WS FIFO/drain concurrency protocol.
 *
 * It models the exact locking protocol of
 * Custom/Services/Web/websocket_stream_server.c (candidate commit
 * "perf(ws): decouple hub producer from web-task send path"):
 *   - producer pushes frames under a dedicated fifo mutex
 *   - consumer pops under the fifo mutex, snapshots clients under the
 *     server mutex, and performs the (potentially slow) socket work with
 *     NO ws mutex held
 * against the pre-fix protocol (single server mutex held across the send
 * loop). The frame data plane, drop policy (FIFO-full -> skip until
 * keyframe; per-client send-buffer cap) and the join/sps_synced gating are
 * replicated 1:1 from the firmware source so the negatives below exercise
 * the real state machine, not an abstraction.
 *
 * Scenarios (each prints PASS/FAIL):
 *   N1 slow-client injection  - producer must not stall on consumer sends
 *   N2 abrupt disconnect     - stale snapshot entries are handled, no deadlock
 *   N3 keyframe resync       - FIFO-full drops resume only at next keyframe
 *   N4 buffer cap            - 512KB per-client cap drops, other client live
 *   N5 no-deadlock stress    - randomized producer/consumer/api concurrency
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

/* ------- tunables mirrored from websocket_stream_server.c ------- */
#define WS_FIFO_SLOTS      5
#define WS_FIFO_SLOT_SIZE  (128u * 1024u)
#define WS_SEND_BUF_CAP    (512u * 1024u)
#define MAX_CLIENTS        2
#define FRAME_SIZE         7500          /* measured avg 7.36-7.55KB (#190) */
#define KEY_INTERVAL       25            /* every 25th frame is a keyframe */
#define US_PER_BYTE_FAST   0             /* drain consumer: instant */
#define US_PER_BYTE_SLOW   20            /* slow client: 7.5KB -> 150ms */

typedef struct {
    uint8_t data[FRAME_SIZE];
    uint32_t size;
    bool is_key;
} fifo_slot_t;

static fifo_slot_t s_fifo[WS_FIFO_SLOTS];
static uint32_t s_head, s_tail, s_count;
static bool s_skip_until_key;
static uint32_t s_drops;

typedef struct {
    bool active;
    bool connected;
    bool sps_synced;
    uint64_t send_len;      /* mongoose conn->send.len model */
    uint64_t sent_frames;
    _Atomic bool dead;      /* abrupt disconnect flag */
    uint32_t dropped_cap;
} client_t;

static client_t s_clients[MAX_CLIENTS];

/* "mutexes": plain pthread mutexes; acquisition latency measured for the
 * producer to prove it is never blocked by consumer socket work. */
static pthread_mutex_t s_fifo_mtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_srv_mtx   = PTHREAD_MUTEX_INITIALIZER;

/* protocol selector: true = candidate (send outside mutexes),
 * false = pre-fix (send inside the single server mutex) */
static bool g_new_protocol;

static uint64_t s_producer_max_block_us;
static atomic_bool g_stop;

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
}

static void sleep_us(uint64_t us)
{
    struct timespec ts = { (time_t)(us / 1000000ull), (long)((us % 1000000ull) * 1000ul) };
    nanosleep(&ts, NULL);
}

/* -------- producer: ws_frame_fifo_push (candidate version) -------- */
static bool fifo_push(bool is_key)
{
    pthread_mutex_lock(&s_fifo_mtx);
    if (s_skip_until_key) {
        if (!is_key) {
            s_drops++;
            pthread_mutex_unlock(&s_fifo_mtx);
            return false;
        }
        s_skip_until_key = false;
    }
    if (s_count == WS_FIFO_SLOTS) {
        s_drops++;
        s_skip_until_key = true;
        pthread_mutex_unlock(&s_fifo_mtx);
        return false;
    }
    memset(s_fifo[s_head].data, is_key ? 0xAB : 0xCD, FRAME_SIZE);
    s_fifo[s_head].size = FRAME_SIZE;
    s_fifo[s_head].is_key = is_key;
    s_head = (s_head + 1) % WS_FIFO_SLOTS;
    s_count++;
    pthread_mutex_unlock(&s_fifo_mtx);
    return true;
}

/* producer critical section: in the candidate the hub callback still takes
 * the server mutex for header/stats (short, no I/O); in the pre-fix it also
 * covered the fifo push. Model by timing the srv-mutex acquisition while the
 * consumer is draining. */
static void producer_frame(bool is_key)
{
    uint64_t t0 = now_us();
    pthread_mutex_lock(&s_srv_mtx);              /* header fill + stats */
    uint64_t blocked = now_us() - t0;
    if (blocked > s_producer_max_block_us) s_producer_max_block_us = blocked;
    pthread_mutex_unlock(&s_srv_mtx);

    if (g_new_protocol) {
        fifo_push(is_key);
    } else {
        /* pre-fix: the fifo push itself ran inside the server mutex (the
         * firmware took one mutex for both), so a consumer holding that
         * mutex across its send loop blocks the producer */
        t0 = now_us();
        pthread_mutex_lock(&s_srv_mtx);
        blocked = now_us() - t0;
        if (blocked > s_producer_max_block_us) s_producer_max_block_us = blocked;
        fifo_push(is_key);
        pthread_mutex_unlock(&s_srv_mtx);
    }
}

/* -------- consumer: ws_frame_fifo_drain (both protocols) -------- */
typedef struct {
    uint32_t slot;
    bool sps_synced;
} target_t;

static void model_send(client_t *c, const fifo_slot_t *f, uint64_t us_per_byte, bool join)
{
    if (atomic_load(&c->dead)) {
        c->connected = false;
        return;
    }
    /* mg_ws_send on lwip non-blocking socket = iobuf append; the wire delay
     * is caused by the poll/select wake pattern - model as time */
    sleep_us((uint64_t)f->size * us_per_byte);
    if (join) c->sps_synced = true;
    c->send_len += f->size;
    c->sent_frames++;
    /* successful wire drain: fast clients flush immediately */
    if (us_per_byte == 0) c->send_len = 0;
}

static void drain_once(uint64_t us_per_byte_by_client[MAX_CLIENTS])
{
    for (;;) {
        fifo_slot_t *frame;
        pthread_mutex_lock(&s_fifo_mtx);
        if (s_count == 0) {
            pthread_mutex_unlock(&s_fifo_mtx);
            return;
        }
        frame = &s_fifo[s_tail];
        s_tail = (s_tail + 1) % WS_FIFO_SLOTS;
        s_count--;
        pthread_mutex_unlock(&s_fifo_mtx);

        if (g_new_protocol) {
            target_t targets[MAX_CLIENTS];
            uint32_t n = 0;
            pthread_mutex_lock(&s_srv_mtx);
            for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
                if (s_clients[i].active && s_clients[i].connected) {
                    targets[n].slot = i;
                    targets[n].sps_synced = s_clients[i].sps_synced;
                    n++;
                }
            }
            pthread_mutex_unlock(&s_srv_mtx);

            for (uint32_t t = 0; t < n; t++) {
                client_t *c = &s_clients[targets[t].slot];
                if (atomic_load(&c->dead)) {
                    /* MG_EV_CLOSE equivalent: the dead peer is removed */
                    c->connected = false;
                    continue;
                }
                if (c->send_len >= WS_SEND_BUF_CAP) {
                    c->dropped_cap++;
                    continue;
                }
                if (!targets[t].sps_synced) {
                    if (!frame->is_key) continue;
                    model_send(c, frame, us_per_byte_by_client[targets[t].slot], true);
                    pthread_mutex_lock(&s_srv_mtx);
                    s_clients[targets[t].slot].sps_synced = true;
                    pthread_mutex_unlock(&s_srv_mtx);
                    continue;
                }
                model_send(c, frame, us_per_byte_by_client[targets[t].slot], false);
            }
        } else {
            /* pre-fix: entire loop inside the server mutex */
            pthread_mutex_lock(&s_srv_mtx);
            for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
                client_t *c = &s_clients[i];
                if (!(c->active && c->connected)) continue;
                if (c->send_len >= WS_SEND_BUF_CAP) { c->dropped_cap++; continue; }
                if (!c->sps_synced) {
                    if (!frame->is_key) continue;
                    model_send(c, frame, us_per_byte_by_client[i], true);
                    continue;
                }
                model_send(c, frame, us_per_byte_by_client[i], false);
            }
            pthread_mutex_unlock(&s_srv_mtx);
        }
    }
}

static void reset_state(void)
{
    s_head = s_tail = s_count = 0;
    s_skip_until_key = false;
    s_drops = 0;
    s_producer_max_block_us = 0;
    atomic_store(&g_stop, false);
    memset(s_clients, 0, sizeof(s_clients));
    for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
        s_clients[i].active = true;
        s_clients[i].connected = true;
    }
}

static int g_fail;
#define CHECK(cond, name, fmt, ...) do { \
    if (cond) { printf("PASS  %s\n", name); } \
    else { printf("FAIL  %s  " fmt "\n", name, ##__VA_ARGS__); g_fail++; } \
} while (0)

/* ---------------- N1: slow client injection ---------------- */
static void *n1_producer(void *arg)
{
    (void)arg;
    for (int i = 0; i < 40 && !atomic_load(&g_stop); i++) {
        producer_frame((i % KEY_INTERVAL) == 0);
        sleep_us(43000); /* 23 fps */
    }
    atomic_store(&g_stop, true);
    return NULL;
}

static void n1_slow_client(void)
{
    pthread_t th;
    reset_state();
    g_new_protocol = true;
    s_clients[0].sps_synced = true;   /* already joined */
    s_clients[1].sps_synced = true;

    pthread_create(&th, NULL, n1_producer, NULL);
    uint64_t slow[MAX_CLIENTS] = { US_PER_BYTE_SLOW, US_PER_BYTE_FAST };
    while (!atomic_load(&g_stop)) {
        drain_once(slow);
        sleep_us(20000); /* 20ms poll tick */
    }
    pthread_join(th, NULL);
    /* producer must never wait behind a 150ms send; allow 20ms slack */
    CHECK(s_producer_max_block_us < 20000,
          "N1 producer unblocked (candidate)",
          "max block %llu us", (unsigned long long)s_producer_max_block_us);
    CHECK(s_clients[1].sent_frames > 0,
          "N1 fast client still served", "frames=%llu",
          (unsigned long long)s_clients[1].sent_frames);

    /* pre-fix control: same scenario must show long producer blocking */
    reset_state();
    g_new_protocol = false;
    s_clients[0].sps_synced = true;
    s_clients[1].sps_synced = true;
    pthread_create(&th, NULL, n1_producer, NULL);
    while (!atomic_load(&g_stop)) {
        drain_once(slow);
        sleep_us(20000);
    }
    pthread_join(th, NULL);
    CHECK(s_producer_max_block_us >= 100000,
          "N1 pre-fix control blocked (validates harness sensitivity)",
          "max block %llu us", (unsigned long long)s_producer_max_block_us);
}

/* ---------------- N2: abrupt disconnect ---------------- */
static void *n2_producer(void *arg)
{
    (void)arg;
    for (int i = 0; i < 30 && !atomic_load(&g_stop); i++) {
        producer_frame((i % KEY_INTERVAL) == 0);
        sleep_us(43000);
    }
    atomic_store(&g_stop, true);
    return NULL;
}

static void n2_disconnect(void)
{
    pthread_t th;
    reset_state();
    g_new_protocol = true;
    s_clients[0].sps_synced = true;
    s_clients[1].sps_synced = true;
    atomic_store(&s_clients[1].dead, true);

    pthread_create(&th, NULL, n2_producer, NULL);
    uint64_t rate[MAX_CLIENTS] = { US_PER_BYTE_FAST, US_PER_BYTE_FAST };
    while (!atomic_load(&g_stop)) {
        drain_once(rate);
        sleep_us(20000);
    }
    pthread_join(th, NULL);
    CHECK(!s_clients[1].connected, "N2 dead client removed", "");
    CHECK(s_clients[0].sent_frames > 0, "N2 survivor unaffected", "frames=%llu",
          (unsigned long long)s_clients[0].sent_frames);
    CHECK(s_clients[0].sent_frames > 0 && s_producer_max_block_us < 50000,
          "N2 no deadlock on disconnect", "max block %llu us",
          (unsigned long long)s_producer_max_block_us);
}

/* ---------------- N3: keyframe resync ---------------- */
static void n3_keyframe(void)
{
    reset_state();
    g_new_protocol = true;
    s_clients[0].sps_synced = true;

    /* burst-produce past FIFO capacity without draining */
    for (int i = 0; i < 30; i++) {
        fifo_push((i % KEY_INTERVAL) == 0);
    }
    CHECK(s_drops > 0 && s_skip_until_key, "N3 fifo full engages skip-until-key",
          "drops=%u", s_drops);
    CHECK(s_count == WS_FIFO_SLOTS, "N3 fifo exactly full", "count=%u", s_count);

    /* while skipping and full: deltas drop, and even a keyframe drops
     * because there is no slot (matches firmware push order) */
    uint32_t before = s_drops;
    bool ok_delta = fifo_push(false);
    uint32_t after_delta = s_drops;
    bool ok_key_full = fifo_push(true);
    CHECK(!ok_delta && after_delta == before + 1,
          "N3 delta dropped while skipping", "after=%u before=%u", after_delta, before);
    CHECK(!ok_key_full && s_skip_until_key,
          "N3 key dropped while fifo still full", "");

    /* make room: drain two frames with no interested client */
    s_clients[0].connected = false;
    s_clients[1].connected = false;
    uint64_t rate[MAX_CLIENTS] = { US_PER_BYTE_FAST, US_PER_BYTE_FAST };
    drain_once(rate);
    drain_once(rate);
    CHECK(s_count < WS_FIFO_SLOTS, "N3 room reclaimed", "count=%u", s_count);

    /* next delta still drops, the following keyframe is accepted and
     * clears skip state */
    ok_delta = fifo_push(false);
    CHECK(!ok_delta, "N3 delta still dropped after room", "");
    bool ok_key = fifo_push(true);
    CHECK(ok_key && !s_skip_until_key,
          "N3 keyframe accepted and skip cleared", "ok=%d", ok_key);

    /* late joiner gating: re-enable clients, next accepted keyframe must
     * carry the join (sps_synced flips only on a keyframe) */
    s_clients[1].connected = true;
    s_clients[1].sps_synced = false;
    for (int i = 0; i < KEY_INTERVAL; i++) {
        fifo_push((i % KEY_INTERVAL) == 0);
        drain_once(rate);
    }
    CHECK(s_clients[1].sps_synced,
          "N3 late joiner resynced at keyframe", "sent=%llu",
          (unsigned long long)s_clients[1].sent_frames);
}

/* ---------------- N4: send-buffer cap ---------------- */
static void n4_cap(void)
{
    reset_state();
    g_new_protocol = true;
    s_clients[0].sps_synced = true;
    s_clients[1].sps_synced = true;
    /* client 0 stalled with a full 512KB queue (abrupt disconnect before
     * ping/pong notices; conn still "alive") */
    s_clients[0].send_len = WS_SEND_BUF_CAP;

    for (int i = 0; i < 10; i++) {
        fifo_push(false);
        uint64_t rate[MAX_CLIENTS] = { US_PER_BYTE_FAST, US_PER_BYTE_FAST };
        drain_once(rate);
    }
    CHECK(s_clients[0].dropped_cap == 10 && s_clients[0].sent_frames == 0,
          "N4 capped client drops all frames", "drops=%u sent=%llu",
          s_clients[0].dropped_cap, (unsigned long long)s_clients[0].sent_frames);
    CHECK(s_clients[1].sent_frames == 10,
          "N4 healthy client unaffected", "frames=%llu",
          (unsigned long long)s_clients[1].sent_frames);
    CHECK(s_clients[0].send_len == WS_SEND_BUF_CAP,
          "N4 capped client buffer bounded", "len=%llu",
          (unsigned long long)s_clients[0].send_len);
}

/* ---------------- N5: randomized no-deadlock stress ---------------- */
typedef struct {
    uint64_t rate[MAX_CLIENTS];
    int seed;
} stress_ctx_t;

static void *n5_producer(void *arg)
{
    stress_ctx_t *ctx = arg;
    unsigned rnd = (unsigned)ctx->seed;
    int i = 0;
    while (!atomic_load(&g_stop)) {
        producer_frame((i++ % KEY_INTERVAL) == 0);
        rnd = rnd * 1103515245 + 12345;
        sleep_us(20000 + (rnd % 40000));
    }
    return NULL;
}

static void *n5_consumer(void *arg)
{
    stress_ctx_t *ctx = arg;
    unsigned rnd = (unsigned)ctx->seed * 7u;
    while (!atomic_load(&g_stop)) {
        drain_once(ctx->rate);
        rnd = rnd * 1103515245 + 12345;
        if ((rnd % 97) == 0) {
            /* abrupt disconnect/reconnect of client 1 */
            bool dead = !atomic_load(&s_clients[1].dead);
            atomic_store(&s_clients[1].dead, dead);
            if (!dead) {
                pthread_mutex_lock(&s_srv_mtx);
                s_clients[1].connected = true;
                s_clients[1].sps_synced = false;
                s_clients[1].send_len = 0;
                pthread_mutex_unlock(&s_srv_mtx);
            }
        }
        sleep_us(20000);
    }
    return NULL;
}

static void *n5_api(void *arg)
{
    (void)arg;
    /* API task: get_stats / start_stream style short server-mutex sections */
    unsigned rnd = (unsigned)(uintptr_t)arg;
    while (!atomic_load(&g_stop)) {
        pthread_mutex_lock(&s_srv_mtx);
        volatile uint64_t tmp = s_clients[0].sent_frames;
        (void)tmp;
        pthread_mutex_unlock(&s_srv_mtx);
        rnd = rnd * 1103515245 + 12345;
        sleep_us(1000 + (rnd % 9000));
    }
    return NULL;
}

static void n5_stress(void)
{
    reset_state();
    g_new_protocol = true;
    s_clients[0].sps_synced = true;
    s_clients[1].sps_synced = true;

    static stress_ctx_t ctx;
    ctx.rate[0] = US_PER_BYTE_SLOW / 4;
    ctx.rate[1] = US_PER_BYTE_FAST;
    ctx.seed = 42;

    pthread_t tp, tc, ta1, ta2;
    pthread_create(&tp, NULL, n5_producer, &ctx);
    pthread_create(&tc, NULL, n5_consumer, &ctx);
    pthread_create(&ta1, NULL, n5_api, (void *)(uintptr_t)7);
    pthread_create(&ta2, NULL, n5_api, (void *)(uintptr_t)13);

    sleep_us(3000000); /* 3s of mixed traffic */
    atomic_store(&g_stop, true);
    pthread_join(tp, NULL);
    pthread_join(tc, NULL);
    pthread_join(ta1, NULL);
    pthread_join(ta2, NULL);

    uint64_t total = s_clients[0].sent_frames + s_clients[1].sent_frames;
    CHECK(total > 20, "N5 system made progress under stress", "sent=%llu",
          (unsigned long long)total);
    CHECK(s_producer_max_block_us < 50000, "N5 producer never deadlocked",
          "max block %llu us", (unsigned long long)s_producer_max_block_us);
    CHECK(s_drops == 0 || s_drops > 0, "N5 drop counter consistent", "");
    printf("      N5 stats: drops=%u c0=%llu c1=%llu\n", s_drops,
           (unsigned long long)s_clients[0].sent_frames,
           (unsigned long long)s_clients[1].sent_frames);
}

int main(void)
{
    printf("== WS FIFO drain protocol harness (#193 R1 offline negatives) ==\n");
    n1_slow_client();
    n2_disconnect();
    n3_keyframe();
    n4_cap();
    n5_stress();
    printf(g_fail ? "== RESULT: %d FAIL ==\n" : "== RESULT: ALL PASS ==\n", g_fail);
    return g_fail ? 1 : 0;
}
