/*
 * join_replay.c - #193 P0 join-sync/IDR root-cause replay harness (host).
 *
 * Closes the chain  join -> keyframe production -> FIFO enqueue/drop ->
 * join-frame build/send -> browser decode  as a deterministic discrete-time
 * simulation (1 ms tick) so every step is observable and reproducible.
 *
 * 1:1 with firmware (logic replicated, anchors):
 *   - ws_frame_fifo_push drop policy      websocket_stream_server.c:598-651
 *   - ws_frame_fifo_drain join gate       websocket_stream_server.c:722-822
 *   - client_count==0 push gate           websocket_stream_server.c:1247-1249
 *   - start_stream sps_synced reset       websocket_stream_server.c:360-364
 *   - send-buffer cap                     websocket_stream_server.c:775-780
 *   - ping/pong liveness                  websocket_stream_server.c:1151-1228
 *   - GOP cadence pic_cnt%fps==0          Custom/Hal/enc.c:283,382 (fps=30)
 *   - SPS/PPS once per encoder session    Custom/Hal/enc.c:351-357
 *   - hub SPS/PPS cache + invalidation    video_stream_hub.c:295-311,395-422
 *
 * Measured-device input (e2b_604 campaign):
 *   - push rate 20-28 msg/s raw drain client (e2b_604_drain_samples)
 *   - push rate 0.5-12.3 msg/s browser attached (e2b_604_{W1..CLOSE}_samples)
 *   - connection lifetimes 2-8 s, gaps 25-37 s (e2b_604_serial_boot)
 *
 * Frontend-source input (browser policy):
 *   - 8 s no-valid-frame / 5 s stall watchdogs, hardRestart reconnect
 *     Web/src/lib/MSE/h264Player.ts:497-501,508,527,563-618
 *   - ws worker retry/backoff Web/src/lib/MSE/websocket-init.ts:55-69,301
 *
 * Fix candidate under test (FIX column):
 *   ENC_CMD_FORCE_INTRA ioctl + drain-side request when an unsynced client
 *   waits (rate-limited 2 s), so the join waits <= one frame period instead
 *   of <= one GOP period (30 frames).
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---------------- device-sourced parameters ---------------- */
#define GOP_FRAMES        30      /* enc.c:382 gop_len=fps(30)-1, IDR at pic_cnt%30==0 */
#define FIFO_SLOTS        5       /* websocket_stream_server.c:579 */
#define SEND_CAP_BYTES    (512u*1024u)  /* :584 */
#define MAX_CLIENTS       2       /* :24 */
#define POLL_TICK_MS      20      /* :546 mg_mgr_poll timeout */
#define FRAME_BYTES       7500u   /* measured avg (#190) */
#define IDR_BYTES         45000u  /* ~6x P frame at 2 Mbps VBR */
#define PING_MS           30000   /* web_config default :188 */
#define PONG_TIMEOUT_MS   10000   /* :189 */
#define WATCHDOG_NOFRAME_MS  8000 /* h264Player.ts:498 */
#define WATCHDOG_STALL_MS    5000 /* h264Player.ts:508,527 */
#define HARD_RESTART_MS      400  /* h264Player.ts hardRestart teardown sleeps */
#define PAGE_RELOAD_GAP_MS   31000 /* serial-log inter-connection gaps 25-37 s */
#define FORCE_IDR_GATE_MS    2000 /* fix candidate request spacing */
#define SIM_WINDOW_MS     180000

typedef struct {
    bool is_idr;
    bool has_sps_pps;       /* in-band SPS/PPS (encoder session first frame) */
    uint32_t bytes;
    uint32_t seq;
} frame_t;

/* server-side client slot (websocket_client_t model) */
typedef struct {
    bool active;
    bool sps_synced;
    uint64_t send_len;      /* mongoose conn->send.len model */
    int64_t last_ping_ms;
    bool pong_pending;
    /* browser-side policy model */
    bool browser_alive;
    bool net_offline;
    bool gave_up;
    int64_t connect_ms;     /* -1 = closed */
    int64_t reconnect_at_ms;/* pending hardRestart reconnect */
    int64_t last_valid_ms;  /* last accepted WS message */
    int64_t last_decoded_ms;
    bool playing;
    /* episode + cumulative metrics (survive reconnects) */
    int64_t first_connect_ever_ms;
    int64_t first_join_ever_ms;
    uint32_t joins, restarts, attempts;
} client_t;

static frame_t s_fifo[FIFO_SLOTS];
static uint32_t s_head, s_tail, s_count;
static bool s_skip_until_key;
static uint32_t s_fifo_drops;

static client_t s_clients[MAX_CLIENTS];
static uint32_t s_client_count;

static bool s_hub_sps_valid;       /* video_stream_hub cache state */
static bool s_stream_active;
static uint64_t s_pic_cnt;         /* enc.c pic_cnt */
static bool s_enc_session_fresh;   /* is_sps_pps_done == 0 */
static bool s_force_intra;         /* enc_t.is_intra_force via ioctl (fix) */
static int64_t s_last_force_req_ms;
static uint32_t s_force_requests;
static bool s_idr_suppressed;      /* S5 scenario: no new IDR production */
static bool s_fix_enabled;

typedef struct { int64_t at_ms; double rate; } rate_point_t;
static rate_point_t s_rate_profile[4];
static int s_rate_points;
static int64_t s_next_frame_ms;
static uint32_t s_frame_seq;
static int64_t s_now;
static uint32_t s_join_sends;

static char s_trace[80][112];
static int s_trace_n;
static void trace_msg(const char *fmt, ...)
{
    if (s_trace_n >= (int)(sizeof(s_trace)/sizeof(s_trace[0]))) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_trace[s_trace_n], sizeof(s_trace[0]), fmt, ap);
    va_end(ap);
    s_trace_n++;
}

static double rate_now(void)
{
    double r = s_rate_profile[0].rate;
    for (int i = 0; i < s_rate_points; i++)
        if (s_rate_profile[i].at_ms <= s_now) r = s_rate_profile[i].rate;
    return r;
}

/* ------------- 1:1: ws_frame_fifo_push drop policy (:598-651) ---------- */
static bool fifo_push(const frame_t *f)
{
    if (s_skip_until_key) {
        if (!f->is_idr) { s_fifo_drops++; return false; }
        s_skip_until_key = false;
    }
    if (s_count == FIFO_SLOTS) {
        s_fifo_drops++;
        s_skip_until_key = true;
        return false;
    }
    s_fifo[s_head] = *f;
    s_head = (s_head + 1) % FIFO_SLOTS;
    s_count++;
    return true;
}

/* browser acceptance: join frame decodes (jmuxer init segment + IDR sample);
 * after a live-IDR join, consecutive frames decode; mid-GOP P never does */
static void browser_receive(client_t *c, bool is_join, int64_t t)
{
    c->last_valid_ms = t;
    if (is_join) {
        c->playing = true;
        c->joins++;
        if (c->first_join_ever_ms < 0) c->first_join_ever_ms = t;
        c->last_decoded_ms = t;
    } else if (c->playing) {
        c->last_decoded_ms = t;
    }
}

/* ------------- 1:1: ws_frame_fifo_drain (:722-822) + fix hook ---------- */
static void drain_once(void)
{
    for (;;) {
        if (s_count == 0) return;
        frame_t f = s_fifo[s_tail];
        s_tail = (s_tail + 1) % FIFO_SLOTS;
        s_count--;

        for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
            client_t *c = &s_clients[i];
            if (!c->active || c->connect_ms < 0) continue;

            if (c->send_len >= SEND_CAP_BYTES) continue;   /* :775 cap */

            if (!c->sps_synced) {
                if (!f.is_idr) continue;                    /* :792 gate */
                c->sps_synced = true;                       /* :805 */
                s_join_sends++;
                if (c->browser_alive && !c->net_offline) {
                    browser_receive(c, true, s_now);
                } else {
                    c->send_len += f.bytes;  /* append to dead peer */
                }
                continue;
            }
            if (c->browser_alive && !c->net_offline) {
                browser_receive(c, false, s_now);
            } else {
                c->send_len += f.bytes;      /* mg_ws_send to dead socket */
            }
        }
    }
}

/* fix candidate hook, called each poll tick BEFORE drain when the fix is
 * enabled: if any connected client is unsynced, request a forced IDR from
 * the encoder (device_ioctl(ENC_CMD_FORCE_INTRA)), rate-limited. */
static void fix_maybe_request_idr(void)
{
    if (!s_fix_enabled || !s_stream_active) return;
    for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
        client_t *c = &s_clients[i];
        if (c->active && c->connect_ms >= 0 && !c->sps_synced) {
            if (s_now - s_last_force_req_ms >= FORCE_IDR_GATE_MS) {
                s_last_force_req_ms = s_now;
                s_force_intra = true;
                s_force_requests++;
                trace_msg("[%6llu.%03llu] fix: FORCE-IDR requested (c%u waiting)",
                          (unsigned long long)s_now/1000,
                          (unsigned long long)s_now%1000, i);
            }
            return;
        }
    }
}

/* ------------- encoder + hub model ------------- */
static void produce_frame(void)
{
    frame_t f = {0};
    bool idr = (s_pic_cnt % GOP_FRAMES) == 0;      /* enc.c:283 */
    if (s_force_intra) {                            /* enc.c:284 override */
        idr = true;
        s_force_intra = false;
    } else if (s_idr_suppressed) {
        idr = false;
    }
    f.is_idr = idr;
    f.has_sps_pps = s_enc_session_fresh && (s_pic_cnt == 0); /* enc.c:351 */
    f.bytes = idr ? IDR_BYTES : FRAME_BYTES;
    f.seq = s_frame_seq++;
    s_pic_cnt++;
    if (f.has_sps_pps) s_enc_session_fresh = false;

    /* hub cache fill: first IDR after invalidation re-extracts (hub:295) */
    if (idr && !s_hub_sps_valid) s_hub_sps_valid = true;

    /* ws_stream_on_hub_frame push gate: no client -> no push (:1247) */
    if (s_client_count == 0 || !s_stream_active) return;
    fifo_push(&f);
}

/* ------------- client lifecycle ------------- */
static void client_connect(uint32_t idx, int64_t t)
{
    client_t *c = &s_clients[idx];
    if (c->active && c->connect_ms >= 0) s_client_count--;  /* same-IP replace */
    bool gave_up = c->gave_up;
    int64_t first_conn = c->first_connect_ever_ms;
    uint32_t joins = c->joins, restarts = c->restarts, attempts = c->attempts;
    int64_t first_join = c->first_join_ever_ms;
    memset(c, 0, sizeof(*c));
    c->active = true;
    c->connect_ms = t;
    c->first_connect_ever_ms = (first_conn >= 0) ? first_conn : t;
    c->first_join_ever_ms = first_join;
    c->joins = joins;
    c->restarts = restarts;
    c->attempts = attempts + 1;
    c->gave_up = gave_up;
    c->browser_alive = !c->gave_up;
    c->last_valid_ms = -1;
    c->last_decoded_ms = -1;
    c->reconnect_at_ms = -1;
    s_client_count++;
    trace_msg("[%6llu.%03llu] c%u WS open attempt #%u (sps_synced=false)",
              (unsigned long long)t/1000, (unsigned long long)t%1000,
              idx, c->attempts);
}

static void client_disconnect(uint32_t idx, int64_t t)
{
    client_t *c = &s_clients[idx];
    if (!c->active || c->connect_ms < 0) return;
    c->active = false;
    c->connect_ms = -1;
    s_client_count--;
    trace_msg("[%6llu.%03llu] c%u closed (%s)",
              (unsigned long long)t/1000, (unsigned long long)t%1000, idx,
              c->playing ? "was playing" : "never joined");
}

typedef struct {
    int64_t connect_ms[MAX_CLIENTS];  /* -1 = never */
    int64_t offline_at_ms, online_at_ms;   /* -1 = never */
    bool browser_gives_up;
    bool suppress_idr;
    int64_t suppress_idr_at_ms;      /* -1 = never */
    int64_t invalidate_cache_at_ms;  /* -1 = never */
    int64_t page_reload_gap_ms;      /* 0 = plain hardRestart latency */
} scenario_t;

static void scenario_init(scenario_t *sc)
{
    memset(sc, 0, sizeof(*sc));
    for (uint32_t i = 0; i < MAX_CLIENTS; i++) sc->connect_ms[i] = -1;
    sc->offline_at_ms = -1;
    sc->online_at_ms = -1;
    sc->suppress_idr_at_ms = -1;
    sc->invalidate_cache_at_ms = -1;
}

typedef struct {
    int64_t join_latency_ms[MAX_CLIENTS];  /* first-connect -> first join */
    uint32_t restarts[MAX_CLIENTS];
    uint32_t joins[MAX_CLIENTS];
    uint32_t attempts[MAX_CLIENTS];
    bool playing[MAX_CLIENTS];
    uint32_t fifo_drops, force_requests;
} result_t;

static result_t run_scenario(const scenario_t *sc)
{
    memset(s_clients, 0, sizeof(s_clients));
    s_head = s_tail = s_count = 0;
    s_skip_until_key = false;
    s_fifo_drops = 0;
    s_join_sends = 0;
    s_client_count = 0;
    s_hub_sps_valid = false;
    s_stream_active = true;
    s_pic_cnt = 0;
    s_enc_session_fresh = true;
    s_force_intra = false;
    s_idr_suppressed = false;
    s_last_force_req_ms = -FORCE_IDR_GATE_MS;
    s_force_requests = 0;
    s_next_frame_ms = 0;
    s_frame_seq = 0;
    s_trace_n = 0;
    for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
        s_clients[i].connect_ms = -1;
        s_clients[i].reconnect_at_ms = -1;
        s_clients[i].first_connect_ever_ms = -1;
        s_clients[i].first_join_ever_ms = -1;
    }

    for (s_now = 0; s_now <= SIM_WINDOW_MS; s_now++) {
        for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
            if (sc->connect_ms[i] == (int64_t)s_now) client_connect(i, s_now);
            if (s_clients[i].reconnect_at_ms == (int64_t)s_now &&
                !s_clients[i].active) {
                client_connect(i, s_now);
            }
        }
        if (sc->offline_at_ms == (int64_t)s_now && s_clients[0].active) {
            s_clients[0].net_offline = true;
            s_clients[0].last_valid_ms = -1;
            trace_msg("[%6llu] c0 network OFFLINE", (unsigned long long)s_now);
        }
        if (sc->online_at_ms == (int64_t)s_now) {
            s_clients[0].net_offline = false;
            if (sc->browser_gives_up) {
                /* device-observed behavior (e2b OFFLINE window): page made
                 * zero reconnect attempts in the 60 s resume window */
                s_clients[0].gave_up = true;
                trace_msg("[%6llu] c0 network back: page GAVE UP (0 attempts)",
                          (unsigned long long)s_now);
            } else if (!s_clients[0].active) {
                client_connect(0, s_now);   /* ws worker reconnect */
                trace_msg("[%6llu] c0 network back: reconnect attempt",
                          (unsigned long long)s_now);
            }
        }
        if (sc->suppress_idr && sc->suppress_idr_at_ms == (int64_t)s_now) {
            s_idr_suppressed = true;
            trace_msg("[%6llu] IDR production suppressed (no new keyframes)",
                      (unsigned long long)s_now);
        }
        if (sc->invalidate_cache_at_ms == (int64_t)s_now) {
            s_hub_sps_valid = false;
            trace_msg("[%6llu] hub SPS/PPS cache invalidated",
                      (unsigned long long)s_now);
        }

        if ((int64_t)s_now >= s_next_frame_ms && rate_now() > 0) {
            produce_frame();
            uint64_t period = (uint64_t)(1000.0 / rate_now());
            s_next_frame_ms = s_now + (period ? (int64_t)period : 1);
        }

        if (s_now % POLL_TICK_MS == 0) {
            fix_maybe_request_idr();
            drain_once();
            /* wire flush: live reading peers drain per tick; dead peers
             * accumulate (modeled in drain via send_len) */
            for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
                client_t *c = &s_clients[i];
                if (c->active && c->browser_alive && !c->net_offline)
                    c->send_len = 0;
            }
        }

        /* server ping / pong-timeout close (:1151-1228) */
        if (s_now % 1000 == 0) {
            for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
                client_t *c = &s_clients[i];
                if (!c->active) continue;
                if (!c->pong_pending && s_now - c->last_ping_ms >= PING_MS) {
                    c->last_ping_ms = s_now;
                    c->pong_pending = true;
                }
                if (c->pong_pending) {
                    if (c->browser_alive && !c->net_offline) {
                        c->pong_pending = false;
                    } else if (s_now - c->last_ping_ms > PONG_TIMEOUT_MS) {
                        trace_msg("[%6llu] c%u pong-timeout: server close",
                                  (unsigned long long)s_now, i);
                        client_disconnect(i, s_now);
                    }
                }
            }
        }

        /* browser watchdogs (h264Player.ts:483-537) */
        for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
            client_t *c = &s_clients[i];
            if (!c->active || c->net_offline || c->gave_up) continue;
            bool watchdog = false;
            const char *why = "";
            if (!c->playing &&
                s_now - c->connect_ms >= WATCHDOG_NOFRAME_MS) {
                watchdog = true;                       /* no-valid-frame */
                why = "no-valid-frame";
            } else if (c->playing &&
                       c->last_decoded_ms >= 0 &&
                       s_now - c->last_decoded_ms >= WATCHDOG_STALL_MS) {
                watchdog = true;                       /* decode-stall */
                why = "decode-stall";
            }
            if (watchdog) {
                c->restarts++;
                trace_msg("[%6llu] c%u watchdog(%s) -> hardRestart #%u",
                          (unsigned long long)s_now, i, why, c->restarts);
                client_disconnect(i, s_now);
                c->reconnect_at_ms = s_now + HARD_RESTART_MS +
                    (sc->page_reload_gap_ms > 0 ? sc->page_reload_gap_ms : 0);
            }
        }
    }

    result_t r;
    memset(&r, 0, sizeof(r));
    for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
        r.joins[i] = s_clients[i].joins;
        r.restarts[i] = s_clients[i].restarts;
        r.attempts[i] = s_clients[i].attempts;
        r.playing[i] = s_clients[i].playing && s_clients[i].active;
        if (s_clients[i].first_join_ever_ms >= 0 &&
            s_clients[i].first_connect_ever_ms >= 0) {
            r.join_latency_ms[i] = s_clients[i].first_join_ever_ms -
                                   s_clients[i].first_connect_ever_ms;
        } else {
            r.join_latency_ms[i] = -1;
        }
    }
    r.fifo_drops = s_fifo_drops;
    r.force_requests = s_force_requests;
    return r;
}

static int g_fail;
#define CHECK(cond, name, fmt, ...) do { \
    if (cond) { printf("PASS  %s\n", name); } \
    else { printf("FAIL  %s  " fmt "\n", name, ##__VA_ARGS__); g_fail++; } \
} while (0)

static void set_rate(double r)
{
    s_rate_points = 1;
    s_rate_profile[0].at_ms = 0;
    s_rate_profile[0].rate = r;
}

/* ------------- S1: late-join rate sweep (bimodality separator) -------- */
static void s1_rate_sweep(void)
{
    printf("\n== S1: single late joiner (connect t=5s), push-rate sweep ==\n");
    printf("        (join latency measured in-sim, not derived)\n");
    double rates[] = {25, 12, 8, 4, 3.75, 2, 1, 0.5};
    printf("rate/s | IDR-int |  BASELINE join (watchdog flaps)      | FIXED join\n");
    for (unsigned k = 0; k < sizeof(rates)/sizeof(rates[0]); k++) {
        double rate = rates[k];
        scenario_t sc; scenario_init(&sc);
        sc.connect_ms[0] = 5000;
        set_rate(rate);

        s_fix_enabled = false;
        result_t base = run_scenario(&sc);
        s_fix_enabled = true;
        result_t fix = run_scenario(&sc);

        char base_buf[64];
        if (base.join_latency_ms[0] < 0)
            snprintf(base_buf, sizeof(base_buf), "NEVER (flaps=%u, attempts=%u)",
                     base.restarts[0], base.attempts[0]);
        else
            snprintf(base_buf, sizeof(base_buf), "%lldms (flaps=%u)",
                     (long long)base.join_latency_ms[0], base.restarts[0]);
        printf("%6.2f | %5.0fms | %-38s | %s\n", rate, 1000.0*GOP_FRAMES/rate,
               base_buf,
               fix.join_latency_ms[0] < 0 ? "NEVER" :
               "joins (flaps=0)");
        if (fix.join_latency_ms[0] >= 0)
            printf("       |         |   -> fixed join latency %lldms, force reqs=%u\n",
                   (long long)fix.join_latency_ms[0], fix.force_requests);
    }
    printf("S1: baseline join waits a FULL IDR interval (GOP30/rate); it exceeds\n"
           "      the 8s no-valid-frame watchdog below ~3.75 msg/s, so every\n"
           "      attempt dies before the join frame -> NEVER mode.\n");
}

/* ------------- S2: W1-like degraded window + page-reload gaps ---------- */
static void s2_w1(double rate, bool fixed, bool verbose)
{
    s_fix_enabled = fixed;
    set_rate(rate);
    scenario_t sc; scenario_init(&sc);
    sc.connect_ms[0] = 6000;              /* first attempt */
    sc.page_reload_gap_ms = PAGE_RELOAD_GAP_MS;  /* iframe reload loop */
    result_t r = run_scenario(&sc);
    printf("S2 rate=%.1f/s %s: join=%s attempts=%u flaps=%u playing=%s drops=%u\n",
           rate, fixed ? "FIXED   " : "BASELINE",
           r.join_latency_ms[0] < 0 ? "NEVER" : "yes",
           r.attempts[0], r.restarts[0],
           r.playing[0] ? "yes" : "NO", r.fifo_drops);
    if (verbose) {
        for (int t = 0; t < s_trace_n; t++) printf("   %s\n", s_trace[t]);
    }
}

/* ------------- S3: OFFLINE window reconnect --------------------------- */
static void s3_reconnect(bool persistent, bool fixed, bool verbose)
{
    s_fix_enabled = fixed;
    s_rate_points = 1;
    set_rate(10.0);
    scenario_t sc; scenario_init(&sc);
    sc.connect_ms[0] = 12000;   /* OFFLINE window: joined ~18s rel open */
    sc.offline_at_ms = 60000;   /* 20s network cut during playback */
    sc.online_at_ms = 80000;
    sc.browser_gives_up = !persistent;
    result_t r = run_scenario(&sc);
    printf("S3 %s %s: pre-cut join=%lldms; end playing=%s attempts=%u joins=%u drops=%u\n",
           persistent ? "persistent" : "give-up  ",
           fixed ? "FIXED   " : "BASELINE",
           (long long)r.join_latency_ms[0],
           r.playing[0] ? "YES" : "NO",
           r.attempts[0], r.joins[0], r.fifo_drops);
    if (verbose) {
        for (int t = 0; t < s_trace_n; t++) printf("   %s\n", s_trace[t]);
    }
}

/* reconnect at degraded rate: baseline latency vs fixed */
static void s3_degraded_reconnect(void)
{
    for (int fixed = 0; fixed <= 1; fixed++) {
        s_fix_enabled = fixed;
        set_rate(1.0);
        scenario_t sc; scenario_init(&sc);
        sc.connect_ms[0] = 12000;
        sc.offline_at_ms = 40000;
        sc.online_at_ms = 60000;
        result_t r = run_scenario(&sc);
        printf("S3-deg 1/s %s: rejoin after reconnect: %s (attempts=%u joins=%u)\n",
               fixed ? "FIXED   " : "BASELINE",
               r.playing[0] ? "YES" : "NO", r.attempts[0], r.joins[0]);
    }
}

/* ------------- S4: dual client ---------------------------------------- */
static void s4_dual(bool fixed)
{
    s_fix_enabled = fixed;
    s_rate_points = 2;
    s_rate_profile[0].at_ms = 0;    s_rate_profile[0].rate = 10.0;
    s_rate_profile[1].at_ms = 25000; s_rate_profile[1].rate = 4.0;
    scenario_t sc; scenario_init(&sc);
    sc.connect_ms[0] = 5000;
    sc.connect_ms[1] = 20000;
    result_t r = run_scenario(&sc);
    printf("S4 %s: c1 join=%lldms playing=%s | c2 join=%lldms playing=%s drops=%u force=%u\n",
           fixed ? "FIXED   " : "BASELINE",
           (long long)r.join_latency_ms[0], r.playing[0] ? "yes" : "NO",
           (long long)r.join_latency_ms[1], r.playing[1] ? "yes" : "NO",
           r.fifo_drops, r.force_requests);
}

/* ------------- S5: no new IDR production ------------------------------ */
static void s5_no_idr(bool fixed)
{
    s_fix_enabled = fixed;
    set_rate(10.0);
    scenario_t sc; scenario_init(&sc);
    sc.connect_ms[0] = 30000;      /* joins AFTER suppression began */
    sc.suppress_idr = true;
    sc.suppress_idr_at_ms = 10000;
    result_t r = run_scenario(&sc);
    printf("S5 %s: no-IDR from t=10s, client t=30s: join=%s playing=%s force=%u\n",
           fixed ? "FIXED   " : "BASELINE",
           r.join_latency_ms[0] < 0 ? "NEVER" : "yes",
           r.playing[0] ? "yes" : "NO", r.force_requests);
    CHECK(fixed ? (r.join_latency_ms[0] >= 0) : (r.join_latency_ms[0] < 0),
          fixed ? "S5 FIXED: forced IDR opens join window"
                : "S5 BASELINE: gate starves without new IDR",
          "join=%lld", (long long)r.join_latency_ms[0]);
}

/* ------------- S6: hub cache invalidation ----------------------------- */
static void s6_cache(bool fixed)
{
    s_fix_enabled = fixed;
    set_rate(10.0);
    scenario_t sc; scenario_init(&sc);
    sc.connect_ms[0] = 5000;
    sc.connect_ms[1] = 22000;
    sc.invalidate_cache_at_ms = 20000;
    result_t r = run_scenario(&sc);
    printf("S6 %s: invalidate t=20s, c2 t=22s: c2 join=%lldms playing=%s (c1 %s)\n",
           fixed ? "FIXED   " : "BASELINE",
           (long long)r.join_latency_ms[1], r.playing[1] ? "yes" : "NO",
           r.playing[0] ? "unaffected" : "BROKEN");
    CHECK(r.join_latency_ms[1] >= 0 && r.playing[1],
          "S6 join after invalidation uses re-extracted cache", "");
    CHECK(r.playing[0], "S6 established client unaffected", "");
}

/* ------------- S7: dead/slow peer alongside healthy client ------------ */
static void s7_dead_peer(bool fixed)
{
    s_fix_enabled = fixed;
    set_rate(10.0);
    scenario_t sc; scenario_init(&sc);
    sc.connect_ms[0] = 5000;
    sc.connect_ms[1] = 20000;
    sc.offline_at_ms = 15000;      /* c0 dark mid-stream */
    sc.browser_gives_up = true;    /* its page stops attempting */
    result_t r = run_scenario(&sc);
    printf("S7 %s: dead c0 (cap path) + healthy c1: c1 join=%lldms playing=%s drops=%u\n",
           fixed ? "FIXED   " : "BASELINE",
           (long long)r.join_latency_ms[1], r.playing[1] ? "yes" : "NO",
           r.fifo_drops);
    CHECK(r.playing[1], "S7 healthy client unaffected by dead peer", "");
}

/* ===================================================================== */
/* fix-side server invariants: threaded negatives (real concurrency)      */
/* ===================================================================== */
static pthread_mutex_t s_enc_mtx = PTHREAD_MUTEX_INITIALIZER;
static bool s_enc_force_flag;
static int64_t s_enc_last_req_us;
static atomic_bool s_stop;
static uint64_t s_prod_max_block_us;

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000ull + ts.tv_nsec/1000ull;
}
static void sleep_us(uint64_t us)
{
    struct timespec ts = {(time_t)(us/1000000ull), (long)((us%1000000ull)*1000ul)};
    nanosleep(&ts, NULL);
}

/* ws_request_join_idr(): web task -> device ioctl (enc state_mtx) */
static void ws_request_join_idr(void)
{
    int64_t t = (int64_t)now_us();
    if (t - s_enc_last_req_us < FORCE_IDR_GATE_MS*1000ll) return;
    s_enc_last_req_us = t;
    pthread_mutex_lock(&s_enc_mtx);
    s_enc_force_flag = true;
    pthread_mutex_unlock(&s_enc_mtx);
}

typedef struct { unsigned seed; } neg_ctx_t;

static void *neg_producer(void *arg)
{
    neg_ctx_t *ctx = arg;
    unsigned rnd = ctx->seed;
    while (!atomic_load(&s_stop)) {
        uint64_t t0 = now_us();
        pthread_mutex_lock(&s_enc_mtx);   /* enc: consume force flag */
        bool forced = s_enc_force_flag;
        s_enc_force_flag = false;
        pthread_mutex_unlock(&s_enc_mtx);
        (void)forced;
        uint64_t blocked = now_us() - t0;
        if (blocked > s_prod_max_block_us) s_prod_max_block_us = blocked;
        rnd = rnd*1103515245u + 12345u;
        sleep_us(30000 + (rnd % 20000));
    }
    return NULL;
}

static void *neg_drain(void *arg)
{
    neg_ctx_t *ctx = arg;
    unsigned rnd = ctx->seed * 3u;
    while (!atomic_load(&s_stop)) {
        ws_request_join_idr();
        rnd = rnd*1103515245u + 12345u;
        sleep_us(20000);
    }
    return NULL;
}

static void neg_threaded(void)
{
    printf("\n== fix-side server invariants (threaded) ==\n");
    atomic_store(&s_stop, false);
    s_enc_force_flag = false;
    s_enc_last_req_us = -(FORCE_IDR_GATE_MS*1000ll);
    s_prod_max_block_us = 0;
    neg_ctx_t pc = {11}, dc = {7};
    pthread_t tp, td;
    pthread_create(&tp, NULL, neg_producer, &pc);
    pthread_create(&td, NULL, neg_drain, &dc);
    sleep_us(2000000);
    atomic_store(&s_stop, true);
    pthread_join(tp, NULL);
    pthread_join(td, NULL);
    CHECK(s_prod_max_block_us < 50000,
          "NJ1 encoder path never blocked behind force-request",
          "max block %llu us", (unsigned long long)s_prod_max_block_us);
}

/* join invariants: cap / re-entry / disconnect (single-threaded) */
static void nj_join_invariants(void)
{
    printf("\n== join invariants (cap, re-entry, disconnect) ==\n");
    memset(s_clients, 0, sizeof(s_clients));
    s_client_count = 0; s_head = s_tail = s_count = 0;
    s_skip_until_key = false; s_fifo_drops = 0; s_join_sends = 0;
    s_hub_sps_valid = true; s_stream_active = true; s_fix_enabled = true;
    s_last_force_req_ms = -FORCE_IDR_GATE_MS;

    s_clients[0].active = true; s_clients[0].connect_ms = 0;
    s_clients[0].browser_alive = true;
    s_clients[0].send_len = SEND_CAP_BYTES;          /* at cap */
    s_clients[1].active = true; s_clients[1].connect_ms = 0;
    s_clients[1].browser_alive = true;
    s_client_count = 2;

    frame_t idr = {.is_idr = true, .bytes = IDR_BYTES, .seq = 1};
    fifo_push(&idr);
    drain_once();
    CHECK(!s_clients[0].sps_synced,
          "NJ2 capped client NOT marked synced (join retried)", "");
    CHECK(s_clients[1].sps_synced && s_clients[1].joins == 1,
          "NJ2 healthy client joined from same keyframe", "");

    s_clients[0].send_len = 0;                       /* wire drains */
    fifo_push(&(frame_t){.is_idr = false, .bytes = FRAME_BYTES, .seq = 2});
    fifo_push(&(frame_t){.is_idr = true, .bytes = IDR_BYTES, .seq = 3});
    drain_once();
    CHECK(s_clients[0].sps_synced && s_clients[0].joins == 1,
          "NJ2 join exactly once after cap clears (no double join)",
          "joins=%u", s_clients[0].joins);

    s_clients[1].active = false; s_client_count--;   /* peer disconnects */
    fifo_push(&(frame_t){.is_idr = true, .bytes = IDR_BYTES, .seq = 4});
    drain_once();
    CHECK(s_clients[0].joins == 1 && s_clients[0].playing,
          "NJ3 drain continues after peer disconnect (no corruption)", "");

    for (int i = 0; i < 12; i++)
        fifo_push(&(frame_t){.is_idr = (i == 0), .bytes = FRAME_BYTES});
    CHECK(s_fifo_drops > 0 && s_skip_until_key,
          "NJ4 fifo-full skip-until-key intact with fix present",
          "drops=%u", s_fifo_drops);

    /* NJ5: force request rate-limited even with continuous unsynced waiter */
    for (int64_t t = 0; t < 7000; t++) {
        s_now = t;
        fix_maybe_request_idr();
    }
    CHECK(s_force_requests <= 4 && s_force_requests >= 1,
          "NJ5 force-IDR request rate-limited (2s gate)",
          "reqs=%u", s_force_requests);
}

/* ------------- NJ6: start_stream vs in-flight join write-back ---------
 * Real-code shape: ws_frame_fifo_drain snapshots the client under
 * g_websocket_server.mutex, releases it across mg_ws_send, then writes
 * sps_synced=true back by slot. websocket_stream_server_start_stream runs
 * on an API task and re-arms every client for a fresh SPS/PPS session;
 * landing inside that window, the old unconditional write-back erased the
 * re-arm, so the new session ran as "synced" P-frames the client cannot
 * decode (no SPS/PPS until the next re-arm). Modeled as snapshot -> send
 * -> [start_stream] -> write-back, guarded vs unguarded. */
static bool s_wb_guard;        /* true = session-guarded write-back (fix) */
static uint32_t s_stream_gen;  /* g_websocket_server.stream_generation */

static void nj_writeback_race(bool guard)
{
    memset(s_clients, 0, sizeof(s_clients));
    s_client_count = 0; s_head = s_tail = s_count = 0;
    s_skip_until_key = false; s_fifo_drops = 0; s_join_sends = 0;
    s_hub_sps_valid = true; s_stream_active = true; s_fix_enabled = true;
    s_wb_guard = guard; s_stream_gen = 1;

    client_t *c = &s_clients[0];
    c->active = true; c->connect_ms = 0; c->browser_alive = true;
    s_client_count = 1;

    frame_t old_idr = {.is_idr = true, .bytes = IDR_BYTES, .seq = 1};
    fifo_push(&old_idr);

    /* phase 1: snapshot + mg_ws_send (mutex released in the real code) */
    frame_t f = s_fifo[s_tail];
    s_tail = (s_tail + 1) % FIFO_SLOTS;
    s_count--;
    if (!f.is_idr) return;   /* join path fires on keyframes only */
    uint32_t snap_gen = s_stream_gen;
    s_join_sends++;
    browser_receive(c, true, s_now);   /* old-stream join frame delivered */
    CHECK(c->joins == 1 && !c->sps_synced,
          "NJ6 join sent but write-back still pending", "guard=%d", guard);

    /* phase 2: API task - start_stream re-arms every client (fresh SPS/PPS) */
    for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
        s_clients[i].sps_synced = false;
    }
    s_stream_gen++;

    /* phase 3: drain write-back, by slot, from the stale snapshot */
    if (!s_wb_guard) {
        c->sps_synced = true;
    } else if (s_stream_gen == snap_gen) {
        c->sps_synced = true;
    }

    if (guard) {
        CHECK(!c->sps_synced,
              "NJ6 FIXED: re-arm survives in-flight write-back", "");
        fifo_push(&(frame_t){.is_idr = false, .bytes = FRAME_BYTES, .seq = 2});
        drain_once();
        CHECK(!c->sps_synced && c->joins == 1,
              "NJ6 FIXED: new-session P-frame still gated", "");
        fifo_push(&(frame_t){.is_idr = true, .bytes = IDR_BYTES, .seq = 3});
        drain_once();
        CHECK(c->sps_synced && c->joins == 2,
              "NJ6 FIXED: new session joins on its own keyframe",
              "joins=%u", c->joins);
    } else {
        CHECK(c->sps_synced,
              "NJ6 BASELINE: re-arm erased by stale write-back (bug)", "");
        fifo_push(&(frame_t){.is_idr = true, .bytes = IDR_BYTES, .seq = 3});
        drain_once();
        CHECK(c->joins == 1,
              "NJ6 BASELINE: new session never re-joins (no gate)",
              "joins=%u", c->joins);
    }
}

int main(void)
{
    printf("== #193 P0 join-sync/IDR replay harness ==\n");
    printf("params: GOP=%d frames | poll=%dms | watchdog no-frame=%dms stall=%dms | "
           "cap=%uKB | fifo=%d slots | ping=%dms pongTO=%dms\n",
           GOP_FRAMES, POLL_TICK_MS, WATCHDOG_NOFRAME_MS, WATCHDOG_STALL_MS,
           SEND_CAP_BYTES/1024, FIFO_SLOTS, PING_MS, PONG_TIMEOUT_MS);

    s1_rate_sweep();

    printf("\n== S2: W1-like degraded window (page-reload gaps) ==\n");
    s2_w1(2.0, false, true);    /* baseline, verbose trace */
    s2_w1(2.0, true, false);
    s2_w1(1.0, false, false);
    s2_w1(1.0, true, false);
    s2_w1(10.0, false, false);  /* fast-join control */
    s2_w1(10.0, true, false);

    printf("\n== S3: offline reconnect (20s cut) ==\n");
    s3_reconnect(false, false, true);
    s3_reconnect(false, true, false);
    s3_reconnect(true, false, false);
    s3_reconnect(true, true, false);
    s3_degraded_reconnect();

    printf("\n== S4: dual client ==\n");
    s4_dual(false);
    s4_dual(true);

    printf("\n== S5: no new IDR ==\n");
    s5_no_idr(false);
    s5_no_idr(true);

    printf("\n== S6: cache invalidation ==\n");
    s6_cache(false);
    s6_cache(true);

    printf("\n== S7: dead peer ==\n");
    s7_dead_peer(false);
    s7_dead_peer(true);

    nj_join_invariants();
    printf("\n== NJ6: start_stream vs in-flight join write-back ==\n");
    nj_writeback_race(false);
    nj_writeback_race(true);
    neg_threaded();

    printf(g_fail ? "== RESULT: %d FAIL ==\n" : "== RESULT: ALL PASS ==\n", g_fail);
    return g_fail ? 1 : 0;
}
