#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "app_report_service.c"

static int g_total;
static int g_fail;

#define CHECK(cond) \
    do { \
        g_total++; \
        if (!(cond)) { \
            g_fail++; \
            printf("FAIL capacity_study:%d %s\n", g_total, #cond); \
        } \
    } while (0)

#define BOOT_A 0x5A5A0001u
#define BOOT_B 0x17170002u
#define DEV_ID "aa:bb:cc:dd:ee:66"
#define PLACEHOLDER "ne301-app:line-crossing"

static uint64_t now_us(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static void sleep_us(uint32_t us)
{
    struct timespec ts;

    if (us == 0u) {
        return;
    }
    ts.tv_sec = (time_t)(us / 1000000u);
    ts.tv_nsec = (long)(us % 1000000u) * 1000L;
    (void)nanosleep(&ts, NULL);
}

static void pump_many(int n)
{
    for (int i = 0; i < n; i++) {
        app_report_service_pump();
    }
}

typedef struct {
    uint32_t mqtt_ready;
    uint32_t webhook_ready;
    uint32_t mqtt_delay_us;
    uint32_t webhook_delay_us;
    int mqtt_script[64];
    int mqtt_script_len;
    int mqtt_script_pos;
    int webhook_script[64];
    int webhook_script_len;
    int webhook_script_pos;
    uint32_t mqtt_calls;
    uint32_t webhook_calls;
} fake_transport_t;

static fake_transport_t g_ft;

static int ft_mqtt_publish(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    (void)json;
    (void)len;
    if (g_ft.mqtt_delay_us != 0u) {
        sleep_us(g_ft.mqtt_delay_us);
    }
    g_ft.mqtt_calls++;
    if (g_ft.mqtt_script_pos < g_ft.mqtt_script_len) {
        return g_ft.mqtt_script[g_ft.mqtt_script_pos++];
    }
    return APP_REPORT_TRANSPORT_OK;
}

static int ft_webhook_post(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    (void)json;
    (void)len;
    if (g_ft.webhook_delay_us != 0u) {
        sleep_us(g_ft.webhook_delay_us);
    }
    g_ft.webhook_calls++;
    if (g_ft.webhook_script_pos < g_ft.webhook_script_len) {
        return g_ft.webhook_script[g_ft.webhook_script_pos++];
    }
    return APP_REPORT_TRANSPORT_OK;
}

static uint32_t ft_mqtt_ready(void *user)
{
    (void)user;
    return g_ft.mqtt_ready;
}

static uint32_t ft_webhook_ready(void *user)
{
    (void)user;
    return g_ft.webhook_ready;
}

static void ft_reset(void)
{
    memset(&g_ft, 0, sizeof(g_ft));
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_READY;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_READY;
}

static const app_report_transport_t g_ft_transport = {
    NULL,
    ft_mqtt_ready,
    ft_mqtt_publish,
    ft_webhook_ready,
    ft_webhook_post
};

static aicam_bool_t ident_device_id(void *user, char *out, uint32_t cap)
{
    (void)user;
    if (out == NULL || cap < (uint32_t)sizeof(DEV_ID)) {
        return AICAM_FALSE;
    }
    memcpy(out, DEV_ID, sizeof(DEV_ID));
    return AICAM_TRUE;
}

static uint32_t g_boot_source = BOOT_A;

static uint32_t ident_boot_id(void *user)
{
    (void)user;
    return g_boot_source;
}

static const app_report_identity_t g_test_identity = {
    NULL,
    ident_device_id,
    ident_boot_id
};

static app_report_session_t g_test_session;

static void session_fill(app_report_session_t *s, const char *app,
                         const uint8_t key, uint32_t report_max,
                         aicam_bool_t active)
{
    memset(s, 0, sizeof(*s));
    strncpy(s->publisher_id, "ne301-exp-pub", APP_REPORT_PUBLISHER_ID_SLOT);
    strncpy(s->app_id, app, APP_REPORT_APP_ID_SLOT);
    memset(s->publisher_key_sha256, key, APP_REPORT_SHA256_LEN);
    s->report_max = report_max;
    s->active = active;
}

static void reset_all(void)
{
    ft_reset();
    g_boot_source = BOOT_A;
    app_report_service_init(&g_test_identity, &g_ft_transport);
    session_fill(&g_test_session, "line-crossing", 0xA5, APP_REPORT_INPUT_MAX,
                 AICAM_TRUE);
    if (app_report_service_set_session(&g_test_session) != APP_REPORT_ABI_OK) {
        printf("FATAL: session bind failed\n");
        exit(1);
    }
}

static size_t build_report(char *buf, size_t cap, uint32_t seq)
{
    return (size_t)snprintf(
        buf, cap,
        "{\"schema_version\":1,\"type\":\"line_counting\","
        "\"device_id\":\"" DEV_ID "\","
        "\"app\":{\"app_id\":\"line-crossing\",\"sw_version\":\"1.0.0\"},"
        "\"boot_id\":0,\"report_seq\":%u,"
        "\"clock_valid\":false,\"reported_at\":null,"
        "\"window\":{\"start_ms\":0,\"end_ms\":1000,\"in\":1,\"out\":0},"
        "\"total\":{\"in\":5,\"out\":3},"
        "\"counter\":{\"counter_name\":\"d\"},"
        "\"target\":{\"class_name\":\"person\"},"
        "\"model\":{\"name\":\"yolo26n\",\"version\":\"1.0\"}}",
        (unsigned int)seq);
}

static int submit_seq(uint32_t seq, uint32_t *out_boot)
{
    char rbuf[1024];
    size_t rlen = build_report(rbuf, sizeof(rbuf), seq);

    return app_report_submit(&g_test_session, rbuf, (uint32_t)rlen, seq, out_boot);
}

static void print_layout(void)
{
    unsigned long long seen_total = (unsigned long long)sizeof(g_seen_seq) +
                                    sizeof(g_seen_key) + sizeof(g_seen_app) +
                                    sizeof(g_seen_publisher) +
                                    sizeof(g_seen_gen) + sizeof(g_seen_used);
    unsigned long long selected_arrays =
        (unsigned long long)sizeof(g_queue) + sizeof(g_tx) + seen_total +
        sizeof(g_session) + sizeof(g_stats) + APP_REPORT_SLOT_CAP;

    printf("CAPACITY_STUDY sizeof_report_entry=%zu\n",
           sizeof(report_entry_t));
    printf("CAPACITY_STUDY report_entry_payload=%u\n",
           (unsigned int)APP_REPORT_SLOT_CAP);
    printf("CAPACITY_STUDY report_entry_overhead=%zu\n",
           sizeof(report_entry_t) - (size_t)APP_REPORT_SLOT_CAP);
    printf("CAPACITY_STUDY sizeof_tx_item=%zu\n", sizeof(app_tx_item_t));
    printf("CAPACITY_STUDY sizeof_queue_8=%zu\n", sizeof(g_queue));
    printf("CAPACITY_STUDY sizeof_tx_8=%zu\n", sizeof(g_tx));
    printf("CAPACITY_STUDY sizeof_seen_ring=%llu\n", seen_total);
    printf("CAPACITY_STUDY sizeof_session=%zu\n", sizeof(g_session));
    printf("CAPACITY_STUDY sizeof_stats=%zu\n", sizeof(g_stats));
    printf("CAPACITY_STUDY sizeof_jr_member=%zu\n", sizeof(jr_member_t));
    printf("CAPACITY_STUDY sizeof_jr_members_256=%zu\n",
           sizeof(jr_member_t) * (size_t)JR_MAX_MEMBERS);
    printf("CAPACITY_STUDY theoretical_queue_256=%zu\n",
           sizeof(report_entry_t) * (size_t)256u);
    printf("CAPACITY_STUDY submit_scratch=%u\n",
           (unsigned int)APP_REPORT_SLOT_CAP);
    printf("CAPACITY_STUDY service_selected_arrays=%llu\n", selected_arrays);
}

static void test_layout_constants(void)
{
    print_layout();
    CHECK(APP_REPORT_QUEUE_SLOTS == 8u);
    CHECK(APP_REPORT_SLOT_CAP == 6240u);
    CHECK(APP_REPORT_INPUT_MAX == 6144u);
    CHECK(APP_REPORT_SEEN_RING == 16u);
    CHECK(sizeof(g_seen_publisher) ==
          (size_t)APP_REPORT_SEEN_RING *
              (APP_REPORT_PUBLISHER_ID_SLOT + 1u));
    CHECK(APP_REPORT_MAX_ATTEMPTS == 3u);
    CHECK(APP_REPORT_BACKOFF_POLLS == 4u);
    CHECK(sizeof(g_queue) ==
          (size_t)APP_REPORT_QUEUE_SLOTS * sizeof(report_entry_t));
    CHECK(sizeof(g_tx) ==
          (size_t)APP_REPORT_QUEUE_SLOTS * sizeof(app_tx_item_t));
    CHECK(8u * APP_REPORT_SLOT_CAP == 49920u);
    CHECK(256u * APP_REPORT_SLOT_CAP == 1597440u);
}

static void test_saturation_busy(void)
{
    uint32_t boot = 0;
    app_report_stats_t st;
    uint32_t ms;
    uint32_t ws;

    reset_all();
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_WAITING;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_WAITING;
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        CHECK(submit_seq(300u + i, &boot) == APP_REPORT_ABI_OK);
    }
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    CHECK(submit_seq(999u, &boot) == APP_REPORT_ABI_BUSY);
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    pump_many((int)APP_REPORT_BACKOFF_POLLS + 2);
    CHECK(g_ft.mqtt_calls == 0u);
    CHECK(g_ft.webhook_calls == 0u);
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        CHECK(app_report_status(BOOT_A, 300u + i, &ms, &ws) ==
              APP_REPORT_ABI_OK);
        CHECK(ms == APP_REPORT_MQTT_PENDING);
        CHECK(ws == APP_REPORT_WEBHOOK_PENDING);
    }
    app_report_service_get_stats(&st);
    CHECK(st.rejected_busy >= 1u);
}

static int g_reenter_arm;
static int32_t g_reenter_result;
static uint32_t g_reenter_boot;

static int ft_mqtt_publish_reenter(void *user, const uint8_t *json,
                                   uint32_t len)
{
    char rbuf[1024];
    size_t rlen;

    (void)user;
    (void)json;
    (void)len;
    g_ft.mqtt_calls++;
    if (g_reenter_arm == 1) {
        g_reenter_arm = 0;
        rlen = build_report(rbuf, sizeof(rbuf), 999u);
        g_reenter_result = app_report_submit(&g_test_session, rbuf,
                                             (uint32_t)rlen, 999u,
                                             &g_reenter_boot);
    }
    if (g_ft.mqtt_script_pos < g_ft.mqtt_script_len) {
        return g_ft.mqtt_script[g_ft.mqtt_script_pos++];
    }
    return APP_REPORT_TRANSPORT_OK;
}

static const app_report_transport_t g_transport_reenter = {
    NULL,
    ft_mqtt_ready,
    ft_mqtt_publish_reenter,
    ft_webhook_ready,
    ft_webhook_post
};

static void test_inflight_reentrancy(void)
{
    uint32_t boot = 0;
    uint32_t ms;
    uint32_t ws;

    ft_reset();
    g_boot_source = BOOT_A;
    app_report_service_init(&g_test_identity, &g_transport_reenter);
    session_fill(&g_test_session, "line-crossing", 0xA5, APP_REPORT_INPUT_MAX,
                 AICAM_TRUE);
    CHECK(app_report_service_set_session(&g_test_session) == APP_REPORT_ABI_OK);
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_WAITING;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_WAITING;
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        CHECK(submit_seq(400u + i, &boot) == APP_REPORT_ABI_OK);
    }
    for (int i = 0; i < 64; i++) {
        g_ft.mqtt_script[i] = APP_REPORT_TRANSPORT_FAIL;
        g_ft.webhook_script[i] = APP_REPORT_TRANSPORT_FAIL;
    }
    g_ft.mqtt_script_len = 64;
    g_ft.webhook_script_len = 64;
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_READY;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_READY;
    g_reenter_arm = 1;
    g_reenter_result = APP_REPORT_ABI_OK;
    g_reenter_boot = 0u;
    app_report_service_pump();
    CHECK(g_reenter_result == APP_REPORT_ABI_BUSY);
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    CHECK(submit_seq(999u, &boot) == APP_REPORT_ABI_BUSY);
    pump_many(2 * (int)APP_REPORT_BACKOFF_POLLS + 2);
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        CHECK(app_report_status(BOOT_A, 400u + i, &ms, &ws) ==
              APP_REPORT_ABI_OK);
        CHECK(ms == APP_REPORT_MQTT_TRANSPORT_FAILED);
        CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_FAILED);
    }
    CHECK(submit_seq(999u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    CHECK(app_report_status(BOOT_A, 400u, &ms, &ws) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_status(BOOT_A, 401u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(app_report_status(BOOT_A, 999u, &ms, &ws) == APP_REPORT_ABI_OK);
}

static void test_dedup_horizon(void)
{
    uint32_t boot = 0;
    uint32_t ms;
    uint32_t ws;
    app_report_stats_t st;

    reset_all();
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_UNCONFIGURED;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_UNCONFIGURED;
    CHECK(submit_seq(500u, &boot) == APP_REPORT_ABI_OK);
    app_report_service_pump();
    for (uint32_t seq = 501u; seq <= 516u; seq++) {
        CHECK(submit_seq(seq, &boot) == APP_REPORT_ABI_OK);
        app_report_service_pump();
    }
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    app_report_service_get_stats(&st);
    CHECK(st.accepted == 17u);
    CHECK(st.dedup_hits == 0u);
    CHECK(app_report_status(BOOT_A, 500u, &ms, &ws) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(submit_seq(516u, &boot) == APP_REPORT_ABI_OK);
    app_report_service_get_stats(&st);
    CHECK(st.accepted == 17u);
    CHECK(st.dedup_hits == 1u);
    CHECK(submit_seq(500u, &boot) == APP_REPORT_ABI_OK);
    app_report_service_get_stats(&st);
    CHECK(st.accepted == 18u);
    CHECK(st.dedup_hits == 1u);
    CHECK(app_report_service_queue_count() == APP_REPORT_QUEUE_SLOTS);
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_READY;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_READY;
    app_report_service_pump();
    CHECK(g_ft.mqtt_calls == 1u);
    CHECK(g_ft.webhook_calls == 1u);
    CHECK(app_report_status(BOOT_A, 500u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
    CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
}

static void test_restart_silent_loss(void)
{
    uint32_t boot = 0;
    uint32_t ms;
    uint32_t ws;
    app_report_stats_t st;

    reset_all();
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_WAITING;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_WAITING;
    CHECK(submit_seq(600u, &boot) == APP_REPORT_ABI_OK);
    CHECK(app_report_service_queue_count() == 1u);
    app_report_service_restart();
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(BOOT_A, 600u, &ms, &ws) == APP_REPORT_ABI_OK);
    CHECK(ms == APP_REPORT_MQTT_UNKNOWN);
    CHECK(ws == APP_REPORT_WEBHOOK_UNKNOWN);
    pump_many(3);
    CHECK(g_ft.mqtt_calls == 0u);
    CHECK(g_ft.webhook_calls == 0u);
    CHECK(submit_seq(600u, &boot) == APP_REPORT_ABI_OK);
    app_report_service_get_stats(&st);
    CHECK(st.dedup_hits == 1u);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(g_ft.mqtt_calls == 0u);
    CHECK(g_ft.webhook_calls == 0u);

    g_boot_source = BOOT_B;
    app_report_service_init(&g_test_identity, &g_ft_transport);
    CHECK(app_report_service_set_session(&g_test_session) == APP_REPORT_ABI_OK);
    CHECK(app_report_status(BOOT_A, 600u, &ms, &ws) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_status(BOOT_B, 600u, &ms, &ws) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(submit_seq(600u, &boot) == APP_REPORT_ABI_OK);
    CHECK(boot == BOOT_B);
    CHECK(app_report_service_queue_count() == 1u);
}

static void test_batch_drain_timing(void)
{
    uint32_t boot = 0;
    uint64_t t0;
    uint64_t elapsed;
    uint32_t ms;
    uint32_t ws;
    app_report_stats_t st;

    reset_all();
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_WAITING;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_WAITING;
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        CHECK(submit_seq(700u + i, &boot) == APP_REPORT_ABI_OK);
    }
    g_ft.mqtt_ready = APP_REPORT_CHANNEL_READY;
    g_ft.webhook_ready = APP_REPORT_CHANNEL_READY;
    g_ft.mqtt_delay_us = 2000u;
    g_ft.webhook_delay_us = 2000u;
    t0 = now_us();
    app_report_service_pump();
    elapsed = now_us() - t0;
    CHECK(g_ft.mqtt_calls == APP_REPORT_QUEUE_SLOTS);
    CHECK(g_ft.webhook_calls == APP_REPORT_QUEUE_SLOTS);
    CHECK(elapsed >= 30000ull);
    CHECK(elapsed < 1000000ull);
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        CHECK(app_report_status(BOOT_A, 700u + i, &ms, &ws) ==
              APP_REPORT_ABI_OK);
        CHECK(ms == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
        CHECK(ws == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);
    }
    app_report_service_get_stats(&st);
    CHECK(st.tx_batches == 1u);
    CHECK(st.tx_skipped == 0u);
    printf("CAPACITY_STUDY measured_batch_drain_us=%llu\n",
           (unsigned long long)elapsed);
}

int main(void)
{
    test_layout_constants();
    test_saturation_busy();
    test_inflight_reentrancy();
    test_dedup_horizon();
    test_restart_silent_loss();
    test_batch_drain_timing();
    printf("capacity_study: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
