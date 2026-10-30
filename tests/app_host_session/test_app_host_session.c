#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#include "app_host_session.h"
#include "app_host_session_device.h"
#include "app_admission.h"
#include "app_host.h"
#include "app_trust_anchor.h"
#include "app_report_service.h"
#include "app_report_service_device.h"
#include "app_native_vnext_floor.h"
#include "app_state_store.h"
#include "app_host_live.h"
#include "app27_live_fixtures.h"
#include "host_stub.h"
#include "lc_app_entry_v4.h"
#include "lc_bus_config.h"
#include "lc_stateblob.h"
#include "installer_vectors.h"
#include "anchor_vectors.h"
#include "session_vectors.h"
#include "v3_vectors.h"
#include "v4_vectors.h"
#include "mem_io_backend.h"
#include "mem_map.h"
#include "devshim/dev_seam.h"

#define STAGE_BUF_CAP (256u * 1024u)
#define TARGET_BASE 0x93E00000u

static int g_total;
static int g_fail;

static void check_impl(int cond, int line)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL session:%d line=%d\n", g_total, line);
    }
}

static void check_msg_impl(int cond, int line, const char *what)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL session:%d line=%d: %s\n", g_total, line, what);
    }
}

#define CHECK(cond) check_impl((cond), __LINE__)
#define CHECK_MSG(cond, what) check_msg_impl((cond), __LINE__, (what))

typedef struct {
    pthread_mutex_t state_mtx;
    pthread_cond_t cond;
    int signalled;
    aicam_bool_t attached;
    int attach_count;
    int detach_count;
    aicam_bool_t fail_attach;
    int fail_spawn;
    int scrub_count;
    int scrub_hold;
    aicam_bool_t model_available;
    app_session_model_view_t model;
    void (*body)(const app_host_session_api_table_t *api);
    void (*v3_body)(const app_host_session_api_table_v3_t *api);
    void (*v4_body)(const app_host_session_api_table_v4_t *api);
    volatile int park_release;
    volatile int terminal_hold;
    volatile int thread_exited;
    int thread_started;
    int mutex_create_count;
    void *report_life_mutex;
    pthread_t thread;
} test_backend_t;

static test_backend_t tb;
static pthread_mutex_t tb_scrub_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tb_scrub_cond = PTHREAD_COND_INITIALIZER;
static app_install_io_t g_session_io;
static uint8_t g_stage[STAGE_BUF_CAP];
static aicam_bool_t g_trust_mode_session;
static aicam_bool_t g_trust_mode_v3;
static aicam_bool_t g_trust_mode_app37;
static uint8_t g_app37_spki_der[128];
static size_t g_app37_spki_der_len;
static const uint8_t g_app37_spki_sha256[APP_ADMISSION_SHA256_LEN] = {
    0xdc, 0x0c, 0xe6, 0x33, 0xdb, 0xcc, 0x91, 0x3d,
    0xaf, 0xaf, 0xa4, 0xb8, 0x9a, 0xc4, 0x4d, 0x8c,
    0xe6, 0x83, 0xfd, 0xf3, 0xc6, 0x0c, 0x8b, 0xdf,
    0x21, 0x32, 0x13, 0xb9, 0xf2, 0xb5, 0x34, 0xba
};
static uint8_t g_ebuf[APP_SESSION_EVENT_WIRE_MAX];
static int g_entry_kinds[16];
static int g_entry_kind_count;
static pthread_mutex_t g_report_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_report_race_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_report_race_cond = PTHREAD_COND_INITIALIZER;
static int g_report_race_gate_enabled;
static int g_report_race_gate_released;
static int g_report_race_lock_entries;
static int g_report_race_life_attempts;
static aicam_bool_t g_report_device_active;
static aicam_bool_t g_report_device_quiesce_pending;
static aicam_bool_t g_report_device_quiesced;
static int g_report_device_start_count;
static int g_report_device_stop_count;
static aicam_result_t g_report_device_start_result = AICAM_OK;
static aicam_result_t g_report_device_stop_result = AICAM_OK;
static int g_report_mqtt_calls;
static int g_report_webhook_calls;
static uint32_t g_report_mqtt_ready = APP_REPORT_CHANNEL_READY;
static uint32_t g_report_webhook_ready = APP_REPORT_CHANNEL_READY;
static int g_report_mqtt_result = APP_REPORT_TRANSPORT_OK;
static int g_report_webhook_result = APP_REPORT_TRANSPORT_OK;
static char g_report_last_mqtt[APP_REPORT_SLOT_CAP];
static char g_report_last_webhook[APP_REPORT_SLOT_CAP];
static const app_host_session_api_table_v3_t *g_captured_report_api;
static uint32_t g_report_test_seq;
static int g_report_body_done;
static uint32_t g_report_api_size;
static uint32_t g_report_api_version;
static int32_t g_report_submit_rc;
static int32_t g_report_duplicate_rc;
static int32_t g_report_mismatch_rc;
static int32_t g_report_status_rc;
static uint32_t g_report_submit_boot;
static uint32_t g_report_duplicate_boot;
static uint32_t g_report_mqtt_state;
static uint32_t g_report_webhook_state;
static volatile int g_v4_body_called;
static const app_host_session_api_table_v4_t *g_v4_api;
static uint32_t g_v4_table_size;
static uint32_t g_v4_table_abi;
static int32_t g_v4_live_publish_rc;
static int32_t g_v4_control_next_rc;

static void report_lock_enter(void)
{
    pthread_mutex_lock(&g_report_race_mutex);
    if (g_report_race_gate_enabled != 0) {
        g_report_race_lock_entries++;
        pthread_cond_broadcast(&g_report_race_cond);
        while (g_report_race_gate_released == 0) {
            pthread_cond_wait(&g_report_race_cond, &g_report_race_mutex);
        }
    }
    pthread_mutex_unlock(&g_report_race_mutex);
    pthread_mutex_lock(&g_report_mutex);
}

static void report_lock_exit(void)
{
    pthread_mutex_unlock(&g_report_mutex);
}

static aicam_bool_t report_identity_device_id(void *user, char *out, uint32_t cap)
{
    static const char device_id[] = "02:11:22:33:44:55";

    (void)user;
    if (out == NULL || cap < sizeof(device_id)) {
        return AICAM_FALSE;
    }
    memcpy(out, device_id, sizeof(device_id));
    return AICAM_TRUE;
}

static uint32_t report_identity_boot_id(void *user)
{
    (void)user;
    return 0x12345678u;
}

static uint32_t report_mqtt_ready_fn(void *user)
{
    (void)user;
    return g_report_mqtt_ready;
}

static int report_mqtt_publish_fn(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    if (len >= sizeof(g_report_last_mqtt)) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (app_report_service_transport_entered(APP_REPORT_CHANNEL_MQTT) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    pthread_mutex_lock(&g_report_mutex);
    memcpy(g_report_last_mqtt, json, len);
    g_report_last_mqtt[len] = '\0';
    g_report_mqtt_calls++;
    pthread_mutex_unlock(&g_report_mutex);
    return g_report_mqtt_result;
}

static uint32_t report_webhook_ready_fn(void *user)
{
    (void)user;
    return g_report_webhook_ready;
}

static int report_webhook_post_fn(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    if (len >= sizeof(g_report_last_webhook)) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (app_report_service_transport_entered(APP_REPORT_CHANNEL_WEBHOOK) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    pthread_mutex_lock(&g_report_mutex);
    memcpy(g_report_last_webhook, json, len);
    g_report_last_webhook[len] = '\0';
    g_report_webhook_calls++;
    pthread_mutex_unlock(&g_report_mutex);
    return g_report_webhook_result;
}

static const app_report_identity_t g_report_identity = {
    NULL,
    report_identity_device_id,
    report_identity_boot_id
};

static const app_report_transport_t g_report_transport = {
    NULL,
    report_mqtt_ready_fn,
    report_mqtt_publish_fn,
    report_webhook_ready_fn,
    report_webhook_post_fn
};

aicam_result_t app_report_service_device_start(void)
{
    g_report_device_start_count++;
    if (g_report_device_start_result != AICAM_OK) {
        return g_report_device_start_result;
    }
    if (g_report_device_quiesce_pending == AICAM_TRUE) {
        if (g_report_device_quiesced != AICAM_TRUE) {
            return AICAM_ERROR_BUSY;
        }
        app_report_service_restart();
        g_report_device_quiesce_pending = AICAM_FALSE;
        g_report_device_quiesced = AICAM_FALSE;
    }
    if (g_report_device_active != AICAM_TRUE) {
        app_report_service_init(&g_report_identity, &g_report_transport);
        app_report_service_set_lock(report_lock_enter, report_lock_exit);
        g_report_device_active = AICAM_TRUE;
    }
    return AICAM_OK;
}

aicam_result_t app_report_service_device_stop(void)
{
    if (g_report_device_active == AICAM_TRUE) {
        (void)app_report_service_set_session(NULL);
        if (g_report_device_stop_result != AICAM_OK) {
            g_report_device_active = AICAM_FALSE;
            g_report_device_quiesce_pending = AICAM_TRUE;
            return g_report_device_stop_result;
        }
        app_report_service_restart();
        g_report_device_active = AICAM_FALSE;
    } else if (g_report_device_quiesce_pending == AICAM_TRUE) {
        if (g_report_device_quiesced != AICAM_TRUE) {
            return AICAM_ERROR_BUSY;
        }
        app_report_service_restart();
        g_report_device_quiesce_pending = AICAM_FALSE;
        g_report_device_quiesced = AICAM_FALSE;
    }
    g_report_device_stop_count++;
    return AICAM_OK;
}

aicam_bool_t app_report_service_device_running(void)
{
    return (g_report_device_active == AICAM_TRUE ||
            g_report_device_quiesce_pending == AICAM_TRUE)
               ? AICAM_TRUE : AICAM_FALSE;
}

static uint32_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

static const uint8_t *trust_spki(void)
{
    if (g_trust_mode_app37 != AICAM_FALSE) {
        return g_app37_spki_der;
    }
    if (g_trust_mode_session != AICAM_FALSE) {
        return SESS_D1_SPKI_DER;
    }
    return (g_trust_mode_v3 != AICAM_FALSE) ? V3_TEST_SPKI_DER
                                            : ANCHOR_VECTOR_SPKI_DER;
}

static size_t trust_spki_len(void)
{
    if (g_trust_mode_app37 != AICAM_FALSE) {
        return g_app37_spki_der_len;
    }
    if (g_trust_mode_session != AICAM_FALSE) {
        return (size_t)SESS_D1_SPKI_DER_LEN;
    }
    return (g_trust_mode_v3 != AICAM_FALSE) ? (size_t)V3_TEST_SPKI_DER_LEN
                                            : (size_t)ANCHOR_VECTOR_SPKI_DER_LEN;
}

static const char *trust_publisher(void)
{
    if (g_trust_mode_app37 != AICAM_FALSE) {
        return "dev-publisher";
    }
    return (g_trust_mode_session != AICAM_FALSE) ? "test-publisher" : "ne301-exp-pub";
}

static const uint8_t *trust_sha(void)
{
    if (g_trust_mode_app37 != AICAM_FALSE) {
        return g_app37_spki_sha256;
    }
    if (g_trust_mode_session != AICAM_FALSE) {
        return SESS_D1_SPKI_SHA256;
    }
    return (g_trust_mode_v3 != AICAM_FALSE) ? V3_TEST_SPKI_SHA256
                                            : ANCHOR_VECTOR_SPKI_SHA256;
}

const app_trust_anchor_t *app_trust_anchor_get(void)
{
    static app_trust_anchor_t anchor;

    memset(&anchor, 0, sizeof(anchor));
    anchor.format_version = APP_TRUST_ANCHOR_FORMAT_VERSION;
    anchor.publisher_id = trust_publisher();
    anchor.publisher_id_len = strlen(trust_publisher());
    anchor.spki_der = trust_spki();
    anchor.spki_der_len = trust_spki_len();
    anchor.spki_sha256 = trust_sha();
    return &anchor;
}

app_trust_anchor_status_t app_trust_anchor_status(void)
{
    return APP_TRUST_ANCHOR_PROVISIONED;
}

const char *app_trust_anchor_status_str(app_trust_anchor_status_t status)
{
    (void)status;
    return "provisioned";
}

aicam_bool_t app_host_exec_region(uint32_t *base, uint32_t *size)
{
    *base = TARGET_BASE;
    *size = 2u * 1024u * 1024u;
    return AICAM_TRUE;
}

static void tb_state_lock(void)
{
    pthread_mutex_lock(&tb.state_mtx);
}

static void tb_state_unlock(void)
{
    pthread_mutex_unlock(&tb.state_mtx);
}

static void *tb_wait(uint32_t timeout_ms)
{
    struct timespec ts;
    void *signalled = NULL;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(timeout_ms / 1000u);
    ts.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&tb.state_mtx);
    if (tb.signalled) {
        tb.signalled = 0;
        signalled = (void *)1;
    } else {
        int rc = pthread_cond_timedwait(&tb.cond, &tb.state_mtx, &ts);
        if (rc == 0 && tb.signalled) {
            tb.signalled = 0;
            signalled = (void *)1;
        }
    }
    pthread_mutex_unlock(&tb.state_mtx);
    return signalled;
}

static void *tb_body_park(const app_host_session_api_table_t *api)
{
    (void)api;
    for (;;) {
        if (tb.park_release) {
            tb.park_release = 0;
            return NULL;
        }
        sleep_ms(2);
    }
}

static uint32_t rd32_stub(const uint8_t *p, uint32_t off)
{
    return (uint32_t)p[off] | ((uint32_t)p[off + 1u] << 8) |
           ((uint32_t)p[off + 2u] << 16) | ((uint32_t)p[off + 3u] << 24);
}

static void tb_body_consumer(const app_host_session_api_table_t *api)
{
    uint32_t len = 0;

    for (;;) {
        int32_t rc = api->event_next(g_ebuf, sizeof(g_ebuf), &len, 40u);
        if (rc == 0) {
            if (g_entry_kind_count < 16) {
                g_entry_kinds[g_entry_kind_count] = (int)((rd32_stub(g_ebuf, 4u)));
                g_entry_kind_count++;
            }
        } else if (rc == -10) {
            break;
        } else if (rc == 1) {
            if (api->should_stop() == 1) {
                break;
            }
            continue;
        } else {
            break;
        }
    }
}

static void *tb_entry_trampoline(void *arg)
{
    void (*fn)(void *) = (void (*)(void *))arg;

    fn(NULL);
    tb_state_lock();
    tb.thread_exited++;
    tb_state_unlock();
    return NULL;
}

static uint8_t *tb_stage_base(void *ctx, size_t *cap)
{
    (void)ctx;
    *cap = STAGE_BUF_CAP;
    return g_stage;
}

static int tb_stage_commit(void *ctx, size_t stage_cap, const app_host_image_info_t *info)
{
    (void)ctx;
    (void)stage_cap;
    (void)info;
    return 0;
}

static int tb_invoke_entry(void *ctx, const void *api, uint32_t abi_version,
                           const app_host_image_info_t *info, int *raw)
{
    (void)ctx;
    (void)info;
    if (api != NULL && abi_version == APP_SESSION_ABI_VERSION_V3 &&
        tb.v3_body != NULL) {
        tb.v3_body((const app_host_session_api_table_v3_t *)api);
    } else if (api != NULL && abi_version == APP_SESSION_ABI_VERSION_V4 &&
               tb.v4_body != NULL) {
        tb.v4_body((const app_host_session_api_table_v4_t *)api);
    } else if (tb.body != NULL) {
        tb.body((const app_host_session_api_table_t *)api);
    } else {
        tb_body_park((const app_host_session_api_table_t *)api);
    }
    *raw = 0;
    return 0;
}

static void tb_scrub(void *ctx)
{
    (void)ctx;
    pthread_mutex_lock(&tb_scrub_mtx);
    tb.scrub_count++;
    while (tb.scrub_hold != 0) {
        pthread_cond_wait(&tb_scrub_cond, &tb_scrub_mtx);
    }
    pthread_mutex_unlock(&tb_scrub_mtx);
    memset(g_stage, 0, sizeof(g_stage));
}

static void tb_scrub_hold_set(int hold)
{
    pthread_mutex_lock(&tb_scrub_mtx);
    tb.scrub_hold = hold;
    if (hold == 0) {
        pthread_cond_broadcast(&tb_scrub_cond);
    }
    pthread_mutex_unlock(&tb_scrub_mtx);
}

static int tb_scrub_count_get(void)
{
    int n;

    pthread_mutex_lock(&tb_scrub_mtx);
    n = tb.scrub_count;
    pthread_mutex_unlock(&tb_scrub_mtx);
    return n;
}

static void tb_log_sink(void *ctx, const char *text)
{
    (void)ctx;
    (void)text;
}

static uint32_t tb_tick_ms(void *ctx)
{
    (void)ctx;
    return mono_ms();
}

static void tb_sleep_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    sleep_ms(ms);
}

static int tb_entry_spawn(void *ctx, void (*fn)(void *arg), void *arg)
{
    (void)ctx;
    (void)arg;
    tb.thread_started++;
    if (tb.fail_spawn != 0) {
        return -1;
    }
    if (pthread_create(&tb.thread, NULL, tb_entry_trampoline, (void *)fn) != 0) {
        return -1;
    }
    pthread_detach(tb.thread);
    return 0;
}

static aicam_bool_t tb_entry_exited(void *ctx)
{
    aicam_bool_t exited;

    (void)ctx;
    tb_state_lock();
    exited = (tb.thread_exited != 0 && tb.terminal_hold == 0) ? AICAM_TRUE
                                                              : AICAM_FALSE;
    tb_state_unlock();
    return exited;
}

static void *tb_mutex_create(void *ctx)
{
    pthread_mutex_t *mtx;

    (void)ctx;
    mtx = (pthread_mutex_t *)malloc(sizeof(*mtx));
    if (mtx == NULL || pthread_mutex_init(mtx, NULL) != 0) {
        free(mtx);
        return NULL;
    }
    tb.mutex_create_count++;
    if (tb.mutex_create_count == 2) {
        tb.report_life_mutex = mtx;
    }
    return (void *)mtx;
}

static void tb_mutex_destroy(void *ctx, void *m)
{
    pthread_mutex_t *mtx = (pthread_mutex_t *)m;

    (void)ctx;
    if (mtx != NULL) {
        pthread_mutex_destroy(mtx);
        free(mtx);
    }
}

static int tb_mutex_lock(void *ctx, void *m)
{
    (void)ctx;
    if (m == tb.report_life_mutex) {
        pthread_mutex_lock(&g_report_race_mutex);
        if (g_report_race_gate_enabled != 0) {
            g_report_race_life_attempts++;
            pthread_cond_broadcast(&g_report_race_cond);
        }
        pthread_mutex_unlock(&g_report_race_mutex);
    }
    if (pthread_mutex_lock((pthread_mutex_t *)m) != 0) {
        return -1;
    }
    return 0;
}

static void tb_mutex_unlock2(void *ctx, void *m)
{
    (void)ctx;
    pthread_mutex_unlock((pthread_mutex_t *)m);
}

static void *tb_event_create(void *ctx)
{
    (void)ctx;
    return (void *)&tb.signalled;
}

static void tb_event_destroy(void *ctx, void *e)
{
    (void)ctx;
    (void)e;
}

static uint32_t tb_event_wait(void *ctx, void *e, uint32_t timeout_ms)
{
    (void)ctx;
    (void)e;
    return (tb_wait(timeout_ms) != NULL) ? 1u : 0u;
}

static void tb_event_set(void *ctx, void *e)
{
    (void)ctx;
    (void)e;
    pthread_mutex_lock(&tb.state_mtx);
    tb.signalled = 1;
    pthread_cond_signal(&tb.cond);
    pthread_mutex_unlock(&tb.state_mtx);
}

static aicam_bool_t tb_ai_attach(void *ctx)
{
    (void)ctx;
    tb_state_lock();
    tb.attach_count++;
    if (tb.fail_attach == AICAM_TRUE) {
        tb_state_unlock();
        return AICAM_FALSE;
    }
    tb.attached = AICAM_TRUE;
    tb_state_unlock();
    return AICAM_TRUE;
}

static void tb_ai_detach(void *ctx)
{
    (void)ctx;
    tb_state_lock();
    tb.attached = AICAM_FALSE;
    tb.detach_count++;
    tb_state_unlock();
}

static aicam_bool_t tb_ai_model_view(void *ctx, app_session_model_view_t *out)
{
    (void)ctx;
    tb_state_lock();
    if (tb.model_available != AICAM_TRUE) {
        tb_state_unlock();
        return AICAM_FALSE;
    }
    *out = tb.model;
    tb_state_unlock();
    return AICAM_TRUE;
}

static app_session_result_t tb_pair_authorize(void *ctx,
                                              const app_install_io_t *io,
                                              const app_install_record_t *native_record,
                                              char *reason,
                                              size_t reason_cap)
{
    (void)ctx;
    (void)io;
    (void)native_record;
    if (reason != NULL && reason_cap > 0u) {
        reason[0] = '\0';
    }
    return APP_SESSION_OK;
}

static const app_host_session_backend_t g_test_backend = {
    NULL,
    tb_stage_base,
    tb_stage_commit,
    tb_invoke_entry,
    tb_scrub,
    tb_log_sink,
    tb_tick_ms,
    tb_sleep_ms,
    tb_entry_spawn,
    tb_entry_exited,
    tb_mutex_create,
    tb_mutex_destroy,
    tb_mutex_lock,
    tb_mutex_unlock2,
    tb_event_create,
    tb_event_destroy,
    tb_event_wait,
    tb_event_set,
    tb_ai_attach,
    tb_ai_detach,
    tb_ai_model_view,
    tb_pair_authorize
};

static uint32_t tb_model_digest(uintptr_t identity, const char *const *names,
                                uint32_t count, const char *name,
                                const char *version)
{
    const char *s;
    uint32_t h = 2166136261u;
    uint32_t i;

    h ^= (uint32_t)(identity & 0xFFFFFFFFu);
    h *= 16777619u;
    h ^= 4096u;
    h *= 16777619u;
    for (s = name; *s != '\0'; s++) {
        h ^= (uint32_t)(uint8_t)*s;
        h *= 16777619u;
    }
    for (s = version; *s != '\0'; s++) {
        h ^= (uint32_t)(uint8_t)*s;
        h *= 16777619u;
    }
    for (i = 0u; i < count && i < APP_SESSION_MAX_CLASSES; i++) {
        for (s = names[i]; *s != '\0'; s++) {
            h ^= (uint32_t)(uint8_t)*s;
            h *= 16777619u;
        }
    }
    return h;
}

static void set_model(uintptr_t identity, const char *const *names, uint32_t count,
                      const char *name, const char *version)
{
    uint32_t i;
    uint32_t fill;

    tb_state_lock();
    memset(&tb.model, 0, sizeof(tb.model));
    snprintf(tb.model.model_name, APP_SESSION_MODEL_NAME_SLOT, "%s", name);
    snprintf(tb.model.model_version, APP_SESSION_MODEL_VERSION_SLOT, "%s", version);
    tb.model.identity_addr = identity;
    tb.model.identity_size = 4096u;
    tb.model.content_digest = tb_model_digest(identity, names, count, name, version);
    tb.model.result_type_od = AICAM_TRUE;
    tb.model.class_count = count;
    fill = (count > APP_SESSION_MAX_CLASSES) ? APP_SESSION_MAX_CLASSES : count;
    for (i = 0u; i < fill; i++) {
        snprintf(tb.model.class_names[i], APP_SESSION_CLASS_SLOT, "%s", names[i]);
    }
    tb_state_unlock();
}

static void set_class_slot_unterminated(uint32_t index)
{
    tb_state_lock();
    tb.model.class_names[index][APP_SESSION_CLASS_SLOT - 1u] = 'x';
    tb_state_unlock();
}

static void set_model_available(aicam_bool_t available)
{
    tb_state_lock();
    tb.model_available = available;
    tb_state_unlock();
}

static const char *const g_two_classes[] = { "person", "car" };
static const char *const g_three_classes[] = { "person", "car", "dog" };

static od_detect_t g_det[96];

static void mk_det(od_detect_t *d, float x, float y, float w, float h,
                   float conf, const char *name)
{
    d->x = x;
    d->y = y;
    d->width = w;
    d->height = h;
    d->conf = conf;
    d->class_name = (char *)name;
}

static void produce(uint32_t count, uint32_t tick)
{
    app_host_session_on_ai_result(count, g_det, tick);
}

static uint32_t rd32(const uint8_t *p, uint32_t off)
{
    return rd32_stub(p, off);
}

static uint32_t fbits(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static app_admission_policy_t s_v1_policy;
static app_admission_policy_t s_v2_policy;
static app_admission_policy_t s_v3_policy;
static app_admission_policy_t s_v4_policy;
static app_admission_trust_view_t s_trust;

static void fill_policies(void)
{
    memset(&s_v1_policy, 0, sizeof(s_v1_policy));
    s_v1_policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    s_v1_policy.psram_mib = 64u;
    s_v1_policy.abi_version = APP_HOST_ABI_VERSION;
    s_v1_policy.supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    s_v1_policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V2;
    s_v1_policy.exec_region_base = TARGET_BASE;
    s_v1_policy.exec_region_size = 2u * 1024u * 1024u;

    memset(&s_v2_policy, 0, sizeof(s_v2_policy));
    s_v2_policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    s_v2_policy.psram_mib = 64u;
    s_v2_policy.abi_version = APP_SESSION_ABI_VERSION;
    s_v2_policy.supported_caps_mask = APP_SESSION_GRANTED_CAPS;
    s_v2_policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V2;
    s_v2_policy.event_capacity = APP_SESSION_EVENT_CAPACITY_MAX;
    s_v2_policy.exec_region_base = TARGET_BASE;
    s_v2_policy.exec_region_size = 2u * 1024u * 1024u;

    memset(&s_v3_policy, 0, sizeof(s_v3_policy));
    s_v3_policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    s_v3_policy.psram_mib = 64u;
    s_v3_policy.abi_version = APP_SESSION_ABI_VERSION_V3;
    s_v3_policy.abi_table_size = APP_SESSION_V3_ABI_TABLE_SIZE;
    s_v3_policy.supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK;
    s_v3_policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V3;
    s_v3_policy.event_capacity = APP_SESSION_EVENT_CAPACITY_MAX;
    s_v3_policy.report_capacity = APP_REPORT_INPUT_MAX;
    s_v3_policy.state_quota_capacity = APP_SESSION_V3_STATE_QUOTA_MAX;
    s_v3_policy.control_capacity = APP_SESSION_V3_CONTROL_CAPACITY;
    s_v3_policy.snapshot_capacity = APP_SESSION_V3_SNAPSHOT_CAPACITY;
    s_v3_policy.exec_region_base = TARGET_BASE;
    s_v3_policy.exec_region_size = 2u * 1024u * 1024u;
    s_v3_policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;

    s_v4_policy = s_v3_policy;
    s_v4_policy.abi_version = APP_SESSION_ABI_VERSION_V4;
    s_v4_policy.abi_table_size = APP_SESSION_V4_ABI_TABLE_SIZE;
    s_v4_policy.supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK;
    s_v4_policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V4;
    s_v4_policy.live_publish_capacity = 0u;
    s_v4_policy.live_track_capacity = 0u;
}

static app_admission_result_t install_verify(const uint8_t *data, size_t len,
                                             app_admission_verdict_t *out, void *ctx)
{
    uint32_t container = app_admission_container_version(data, len);
    const app_trust_anchor_t *anchor;

    (void)ctx;
    g_trust_mode_v3 = (container >= 3u) ? AICAM_TRUE : AICAM_FALSE;
    anchor = app_trust_anchor_get();
    s_trust.publisher_id = anchor->publisher_id;
    s_trust.publisher_id_len = anchor->publisher_id_len;
    s_trust.spki_der = anchor->spki_der;
    s_trust.spki_der_len = anchor->spki_der_len;
    if (app_admission_container_version(data, len) == 3u) {
        return app_admission_verify_package(data, len, &s_v3_policy, &s_trust, out);
    }
    if (app_admission_container_version(data, len) == 4u) {
        return app_admission_verify_package(data, len, &s_v4_policy, &s_trust, out);
    }
    if (app_admission_container_version(data, len) == 2u) {
        return app_admission_verify_package(data, len, &s_v2_policy, &s_trust, out);
    }
    return app_admission_verify_package(data, len, &s_v1_policy, &s_trust, out);
}

static app_install_result_t install_fresh(const uint8_t *pkg, size_t len,
                                          app_install_record_t *rec)
{
    app_install_record_t removed;

    app_install_uninstall(&g_session_io, &removed);
    return app_install(&g_session_io, install_verify, NULL, pkg, len, rec);
}

static void reset_all(void)
{
    if (g_report_device_active == AICAM_TRUE) {
        (void)app_report_service_device_stop();
    }
    pthread_mutex_lock(&g_report_race_mutex);
    g_report_race_gate_enabled = 0;
    g_report_race_gate_released = 1;
    g_report_race_lock_entries = 0;
    g_report_race_life_attempts = 0;
    pthread_mutex_unlock(&g_report_race_mutex);
    memset(&tb, 0, sizeof(tb));
    pthread_mutex_init(&tb.state_mtx, NULL);
    pthread_cond_init(&tb.cond, NULL);
    tb.model_available = AICAM_TRUE;
    set_model(0x1000u, g_two_classes, 2u, "sess-model", "1.0.0");
    app_host_session_init(&g_test_backend);
    mem_reset();
    mem_io_init(&g_session_io);
    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
    fill_policies();
    g_trust_mode_session = AICAM_TRUE;
    g_trust_mode_v3 = AICAM_FALSE;
    g_report_device_active = AICAM_FALSE;
    g_report_device_quiesce_pending = AICAM_FALSE;
    g_report_device_quiesced = AICAM_FALSE;
    g_report_device_start_count = 0;
    g_report_device_stop_count = 0;
    g_report_device_start_result = AICAM_OK;
    g_report_device_stop_result = AICAM_OK;
    g_report_mqtt_calls = 0;
    g_report_webhook_calls = 0;
    g_report_mqtt_ready = APP_REPORT_CHANNEL_READY;
    g_report_webhook_ready = APP_REPORT_CHANNEL_READY;
    g_report_mqtt_result = APP_REPORT_TRANSPORT_OK;
    g_report_webhook_result = APP_REPORT_TRANSPORT_OK;
    memset(g_report_last_mqtt, 0, sizeof(g_report_last_mqtt));
    memset(g_report_last_webhook, 0, sizeof(g_report_last_webhook));
    g_captured_report_api = NULL;
    g_report_test_seq = 41u;
    g_report_body_done = 0;
    g_report_api_size = 0u;
    g_report_api_version = 0u;
    g_report_submit_rc = APP_REPORT_ABI_INVALID_ARGUMENT;
    g_report_duplicate_rc = APP_REPORT_ABI_INVALID_ARGUMENT;
    g_report_mismatch_rc = APP_REPORT_ABI_INVALID_ARGUMENT;
    g_report_status_rc = APP_REPORT_ABI_INVALID_ARGUMENT;
    g_report_submit_boot = 0u;
    g_report_duplicate_boot = 0u;
    g_report_mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    g_report_webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;
    g_v4_body_called = 0;
    g_v4_api = NULL;
    g_v4_table_size = 0u;
    g_v4_table_abi = 0u;
    g_v4_live_publish_rc = 0;
    g_v4_control_next_rc = 0;
}

static app_session_result_t start_ok(app_host_session_start_out_t *out)
{
    return app_host_session_start(&g_session_io, out);
}

static const app_host_session_api_table_t *api_tbl(void)
{
    return app_host_session_api_table();
}

static int32_t pop(uint8_t *buf, uint32_t cap, uint32_t *len, uint32_t wait)
{
    return api_tbl()->event_next(buf, cap, len, wait);
}

static app_install_result_t install_expected(const uint8_t *pkg, size_t len,
                                             app_install_record_t *rec)
{
    app_install_result_t rc = install_fresh(pkg, len, rec);
    return (rc == APP_INSTALL_OK || rc == APP_INSTALL_OK_ALREADY) ? APP_INSTALL_OK : rc;
}

typedef struct {
    app_session_result_t rc;
    char reason[APP_SESSION_REASON_SLOT];
} start_call_t;

static void *start_thread_fn(void *arg)
{
    start_call_t *call = (start_call_t *)arg;
    app_host_session_start_out_t out;

    memset(&out, 0, sizeof(out));
    call->rc = app_host_session_start(&g_session_io, &out);
    snprintf(call->reason, sizeof(call->reason), "%s", out.reason);
    return NULL;
}

static void wait_idle(void)
{
    app_host_session_status_t st;
    int spins = 0;

    for (;;) {
        app_host_session_status(&st);
        if (st.state == APP_SESSION_STATE_IDLE) {
            break;
        }
        sleep_ms(2);
        spins++;
        if (spins > 5000) {
            break;
        }
    }
}

static void test_container_version(void)
{
    uint8_t buf[16];

    CHECK(app_admission_container_version(NULL, 0u) == 0u);
    CHECK(app_admission_container_version(buf, 4u) == 0u);
    memcpy(buf, SESS_PKG, 8);
    CHECK(app_admission_container_version(buf, sizeof(buf)) == 2u);
    memcpy(buf, HELLO_V1_PACKAGE, 8);
    CHECK(app_admission_container_version(buf, sizeof(buf)) == 1u);
    memset(buf, 0xAA, sizeof(buf));
    CHECK(app_admission_container_version(buf, sizeof(buf)) == 0u);
}

static void test_unauthorized_before_start(void)
{
    uint8_t buf[512];
    uint32_t len = 0;

    CHECK(api_tbl()->event_next(buf, sizeof(buf), &len, 1u) == -3);
    CHECK(api_tbl()->model_meta(buf, sizeof(buf), &len) == -3);
    CHECK(api_tbl()->class_name(1u, 1u, 0u, buf, sizeof(buf), &len) == -3);
    CHECK(api_tbl()->should_stop() == -3);
    CHECK(api_tbl()->report_submit(buf, 1u, 1u, NULL) == -3);
    CHECK(api_tbl()->report_status(1u, 1u, NULL, NULL) == -3);
    CHECK(api_tbl()->state_read(buf, sizeof(buf), &len, NULL) == -3);
    CHECK(api_tbl()->state_commit(buf, 1u, 0u, NULL) == -3);
}

static void test_start_refusals(void)
{
    app_host_session_start_out_t out;
    app_install_record_t rec;
    uint32_t i;

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_NOT_INSTALLED);
    CHECK(strcmp(out.reason, "NOT_INSTALLED") == 0);
    CHECK(app_install_app_running() == AICAM_FALSE);

    g_trust_mode_session = AICAM_FALSE;
    CHECK(install_expected(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_PROFILE_UNSUPPORTED);
    CHECK_MSG(strcmp(out.reason, "profile_unsupported") == 0, "v1_refused");
    CHECK(app_install_app_running() == AICAM_FALSE);
    g_trust_mode_session = AICAM_TRUE;

    CHECK(install_fresh(SESS_PKG_NOSESSION, SESS_PKG_NOSESSION_LEN, &rec) ==
          APP_INSTALL_ERR_BAD_PACKAGE);
    CHECK(install_fresh(SESS_PKG_FULLCAPS, SESS_PKG_FULLCAPS_LEN, &rec) ==
          APP_INSTALL_ERR_ABI_INCOMPATIBLE);
    CHECK(install_fresh(SESS_PKG_SMALL_EM, SESS_PKG_SMALL_EM_LEN, &rec) ==
          APP_INSTALL_ERR_RESOURCE_LIMIT);
    CHECK(install_fresh(SESS_PKG_D2, SESS_PKG_D2_LEN, &rec) ==
          APP_INSTALL_ERR_PUBLISHER_UNTRUSTED);

    set_model_available(AICAM_FALSE);
    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
    CHECK_MSG(strcmp(out.reason, "model_incompatible") == 0, "no_model_refused");
    set_model_available(AICAM_TRUE);

    {
        static const char *dup_names[2];
        dup_names[0] = "person";
        dup_names[1] = "person";
        set_model(0x2000u, dup_names, 2u, "dup", "1.0.0");
        memset(&out, 0, sizeof(out));
        CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
        CHECK_MSG(strcmp(out.reason, "model_incompatible") == 0, "dup_labels_refused");
    }
    {
        static const char *long_names[2];
        char long_label[64];
        memset(long_label, 'a', sizeof(long_label) - 1u);
        long_label[sizeof(long_label) - 1u] = '\0';
        long_names[0] = long_label;
        long_names[1] = "car";
        set_model(0x2100u, long_names, 2u, "long", "1.0.0");
        set_class_slot_unterminated(0u);
        memset(&out, 0, sizeof(out));
        CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
        CHECK_MSG(strcmp(out.reason, "model_incompatible") == 0, "long_label_refused");
    }
    {
        static char labels[33][8];
        const char *many_names[33];
        for (i = 0u; i < 33u; i++) {
            snprintf(labels[i], sizeof(labels[i]), "c%u", (unsigned)i);
            many_names[i] = labels[i];
        }
        set_model(0x2200u, many_names, 33u, "many", "1.0.0");
        memset(&out, 0, sizeof(out));
        CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
        CHECK_MSG(strcmp(out.reason, "model_incompatible") == 0, "too_many_classes");
    }
    {
        static const char *pose_names[2];
        pose_names[0] = "kp0";
        pose_names[1] = "kp1";
        set_model(0x2300u, pose_names, 2u, "pose", "1.0.0");
        tb_state_lock();
        tb.model.result_type_od = AICAM_FALSE;
        tb_state_unlock();
        memset(&out, 0, sizeof(out));
        CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
        CHECK_MSG(strcmp(out.reason, "model_incompatible") == 0, "non_od_refused");
    }
    set_model(0x1000u, g_two_classes, 2u, "sess-model", "1.0.0");
}

static void test_start_success_and_double(void)
{
    app_host_session_start_out_t out;
    app_install_record_t rec;
    app_host_session_status_t st;
    uint32_t host_boot_id = 123u;
    uint32_t mqtt_state = APP_REPORT_MQTT_PENDING;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_PENDING;

    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    CHECK(count_files() == 1);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK_MSG(strcmp(out.record.app_id, "hello-app") == 0, "record_app");
    CHECK(out.abi_version == APP_SESSION_ABI_VERSION);
    CHECK(out.granted_caps == APP_SESSION_GRANTED_CAPS);
    CHECK(out.event_max == 1576u);

    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_RUNNING);
    CHECK(st.app_running == AICAM_TRUE);
    CHECK(st.event_max == 1576u);
    CHECK(st.model_generation == 1u);
    CHECK(st.class_generation == 1u);
    CHECK(st.class_count == 2u);
    CHECK(strcmp(st.model_name, "sess-model") == 0);
    CHECK(strcmp(st.publisher_id, "test-publisher") == 0);
    CHECK(strcmp(st.app_id, "hello-app") == 0);
    CHECK(tb.attach_count == 1);
    CHECK(st.abi_version == 0x00020000u);
    CHECK(api_tbl()->report_submit("{}", 2u, 1u, &host_boot_id) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(host_boot_id == 0u);
    CHECK(api_tbl()->report_status(1u, 1u, &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(mqtt_state == APP_REPORT_MQTT_UNKNOWN);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_UNKNOWN);
    CHECK(g_report_device_start_count == 0);

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_BUSY);
    CHECK(strcmp(out.reason, "session_active") == 0);
}

static void tb_body_v3_report(const app_host_session_api_table_v3_t *api)
{
    char json[512];
    char mismatch_json[512];
    uint32_t status_mqtt = APP_REPORT_MQTT_UNKNOWN;
    uint32_t status_webhook = APP_REPORT_WEBHOOK_UNKNOWN;
    int n;
    int mismatch_n;

    n = snprintf(json, sizeof(json),
                 "{\"schema_version\":1,\"type\":\"line_counting\","
                 "\"report_seq\":%u,\"device_id\":\"app-device\","
                 "\"boot_id\":99,\"window\":{},\"total\":{},"
                 "\"counter\":{},\"target\":{},\"model\":{}}",
                 (unsigned int)g_report_test_seq);
    g_report_api_size = api->table_size;
    g_report_api_version = api->abi_version;
    g_captured_report_api = api;
    mismatch_n = snprintf(mismatch_json, sizeof(mismatch_json),
                          "{\"schema_version\":1,\"type\":\"line_counting\","
                          "\"report_seq\":%u,\"window\":{},\"total\":{},"
                          "\"counter\":{},\"target\":{},\"model\":{}}",
                          (unsigned int)(g_report_test_seq + 1u));
    if (mismatch_n > 0 && (size_t)mismatch_n < sizeof(mismatch_json)) {
        g_report_mismatch_rc = api->report_submit(mismatch_json,
                                                  (uint32_t)mismatch_n,
                                                  g_report_test_seq,
                                                  &g_report_duplicate_boot);
    }
    if (n <= 0 || (size_t)n >= sizeof(json)) {
        g_report_submit_rc = APP_REPORT_ABI_INVALID_ARGUMENT;
        g_report_duplicate_rc = APP_REPORT_ABI_INVALID_ARGUMENT;
    } else {
        g_report_submit_rc = api->report_submit(json, (uint32_t)n,
                                                g_report_test_seq,
                                                &g_report_submit_boot);
        g_report_status_rc = api->report_status(g_report_submit_boot,
                                                g_report_test_seq,
                                                &status_mqtt,
                                                &status_webhook);
        g_report_mqtt_state = status_mqtt;
        g_report_webhook_state = status_webhook;
        g_report_duplicate_rc = api->report_submit(json, (uint32_t)n,
                                                   g_report_test_seq,
                                                   &g_report_duplicate_boot);
    }
    tb_state_lock();
    g_report_body_done = 1;
    pthread_cond_broadcast(&tb.cond);
    tb_state_unlock();
    while (tb.park_release == 0) {
        sleep_ms(2u);
    }
}

static int wait_report_body(uint32_t timeout_ms)
{
    uint32_t waited = 0u;

    while (waited < timeout_ms) {
        int done;

        tb_state_lock();
        done = g_report_body_done;
        tb_state_unlock();
        if (done != 0) {
            return 1;
        }
        sleep_ms(1u);
        waited++;
    }
    return 0;
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready;
    int go;
} report_race_start_gate_t;

typedef struct {
    report_race_start_gate_t *gate;
    app_session_result_t result;
    app_host_session_stop_out_t out;
} report_race_stop_call_t;

static void report_race_start_gate_wait(report_race_start_gate_t *gate)
{
    pthread_mutex_lock(&gate->mutex);
    gate->ready++;
    pthread_cond_broadcast(&gate->cond);
    while (gate->go == 0) {
        pthread_cond_wait(&gate->cond, &gate->mutex);
    }
    pthread_mutex_unlock(&gate->mutex);
}

static int report_race_start_gate_open(report_race_start_gate_t *gate,
                                       int target, uint32_t timeout_ms)
{
    struct timespec ts;
    int ready;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(timeout_ms / 1000u);
    ts.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&gate->mutex);
    while (gate->ready < target) {
        if (pthread_cond_timedwait(&gate->cond, &gate->mutex, &ts) != 0) {
            break;
        }
    }
    ready = (gate->ready == target) ? 1 : 0;
    gate->go = 1;
    pthread_cond_broadcast(&gate->cond);
    pthread_mutex_unlock(&gate->mutex);
    return ready;
}

static int wait_report_unbind_interleave(uint32_t timeout_ms)
{
    struct timespec ts;
    int overlapped;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(timeout_ms / 1000u);
    ts.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&g_report_race_mutex);
    while (!(g_report_race_lock_entries >= 1 &&
             (g_report_race_life_attempts >= 2 ||
              g_report_race_lock_entries >= 2))) {
        if (pthread_cond_timedwait(&g_report_race_cond,
                                   &g_report_race_mutex, &ts) != 0) {
            break;
        }
    }
    overlapped = (g_report_race_lock_entries >= 1 &&
                  (g_report_race_life_attempts >= 2 ||
                   g_report_race_lock_entries >= 2)) ? 1 : 0;
    g_report_race_gate_released = 1;
    pthread_cond_broadcast(&g_report_race_cond);
    pthread_mutex_unlock(&g_report_race_mutex);
    return overlapped;
}

static int report_race_gate_disable(void)
{
    int entries;

    pthread_mutex_lock(&g_report_race_mutex);
    entries = g_report_race_lock_entries;
    g_report_race_gate_enabled = 0;
    g_report_race_gate_released = 1;
    pthread_cond_broadcast(&g_report_race_cond);
    pthread_mutex_unlock(&g_report_race_mutex);
    return entries;
}

static void *report_race_return_thread(void *arg)
{
    report_race_start_gate_t *gate = (report_race_start_gate_t *)arg;

    report_race_start_gate_wait(gate);
    tb.park_release = 1;
    return NULL;
}

static void *report_race_stop_thread(void *arg)
{
    report_race_stop_call_t *call = (report_race_stop_call_t *)arg;

    report_race_start_gate_wait(call->gate);
    call->result = app_host_session_stop(3000u, &call->out);
    return NULL;
}

static void test_v3_report_capability_path(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_install_record_t rec;
    uint32_t mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;
    uint32_t old_boot = 0u;
    uint32_t submit_boot = 0u;
    char json[128];
    char over_limit[APP_REPORT_INPUT_MAX + 1u];
    int n;
    app_session_result_t stop_rc;

    g_trust_mode_session = AICAM_FALSE;
    CHECK(install_expected(V3_LINE_CROSSING_PACKAGE,
                           V3_LINE_CROSSING_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    tb.v3_body = tb_body_v3_report;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    if (out.result != APP_SESSION_OK) {
        return;
    }
    CHECK(out.abi_version == APP_SESSION_ABI_VERSION_V3);
    CHECK(out.granted_caps == 0x000000FFu);
    CHECK(g_report_device_start_count == 1);
    CHECK(wait_report_body(1000u) == 1);
    CHECK(g_report_api_size == sizeof(app_host_session_api_table_v3_t));
    CHECK(g_report_api_version == APP_SESSION_ABI_VERSION_V3);
    CHECK(g_report_mismatch_rc == APP_REPORT_ABI_INVALID_ARGUMENT);
    CHECK(g_report_submit_rc == APP_REPORT_ABI_OK);
    CHECK(g_report_submit_boot == 0x12345678u);
    CHECK(g_report_duplicate_rc == APP_REPORT_ABI_OK);
    CHECK(g_report_duplicate_boot == g_report_submit_boot);
    CHECK(g_report_status_rc == APP_REPORT_ABI_OK);
    CHECK(g_report_mqtt_state == APP_REPORT_MQTT_PENDING);
    CHECK(g_report_webhook_state == APP_REPORT_WEBHOOK_PENDING);
    CHECK(app_report_service_queue_count() == 1u);
    memset(over_limit, 'x', sizeof(over_limit));
    submit_boot = 0xFFFFFFFFu;
    CHECK(g_captured_report_api->report_submit(over_limit,
                                               (uint32_t)sizeof(over_limit),
                                               g_report_test_seq + 2u,
                                               &submit_boot) ==
          APP_REPORT_ABI_QUOTA_EXCEEDED);
    CHECK(submit_boot == 0u);

    old_boot = g_report_submit_boot;
    app_report_service_pump();
    CHECK(g_report_mqtt_calls == 1);
    CHECK(g_report_webhook_calls == 1);
    CHECK(strstr(g_report_last_mqtt, "\"device_id\":\"02:11:22:33:44:55\"") != NULL);
    CHECK(strstr(g_report_last_mqtt, "\"boot_id\":305419896") != NULL);
    CHECK(strstr(g_report_last_webhook, "\"device_id\":\"02:11:22:33:44:55\"") != NULL);
    CHECK(g_captured_report_api->report_status(old_boot, g_report_test_seq,
                                               &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_TRANSPORT_SUCCEEDED);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_TRANSPORT_SUCCEEDED);

    memset(&stop_out, 0, sizeof(stop_out));
    stop_rc = app_host_session_stop(20u, &stop_out);
    CHECK(stop_rc == APP_SESSION_ERR_BUSY);
    n = snprintf(json, sizeof(json),
                 "{\"schema_version\":1,\"type\":\"line_counting\","
                 "\"report_seq\":%u,\"window\":{},\"total\":{},"
                 "\"counter\":{},\"target\":{},\"model\":{}}", 42u);
    CHECK(n > 0);
    CHECK(g_captured_report_api->report_submit(json, (uint32_t)n, 42u,
                                               &submit_boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(submit_boot == 0u);
    CHECK(g_captured_report_api->report_status(old_boot, g_report_test_seq,
                                               &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(mqtt_state == APP_REPORT_MQTT_UNKNOWN);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_UNKNOWN);

    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(3000u, &stop_out) == APP_SESSION_OK);
    CHECK(g_report_device_stop_count == 1);
    CHECK(app_report_service_queue_count() == 0u);
    CHECK(app_report_status(old_boot, g_report_test_seq, &mqtt_state,
                            &webhook_state) == APP_REPORT_ABI_NOT_FOUND);

    g_report_test_seq = 42u;
    g_report_webhook_ready = APP_REPORT_CHANNEL_UNCONFIGURED;
    g_report_mqtt_result = APP_REPORT_TRANSPORT_FAIL;
    g_report_body_done = 0;
    tb.park_release = 0;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    if (out.result != APP_SESSION_OK) {
        return;
    }
    CHECK(wait_report_body(1000u) == 1);
    CHECK(g_report_submit_rc == APP_REPORT_ABI_OK);
    CHECK(g_captured_report_api->report_status(old_boot, 41u,
                                               &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(mqtt_state == APP_REPORT_MQTT_UNKNOWN);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_UNKNOWN);
    for (n = 0; n < (int)(2u * APP_REPORT_BACKOFF_POLLS + 3u); n++) {
        app_report_service_pump();
    }
    CHECK(g_report_mqtt_calls == 4);
    CHECK(g_report_webhook_calls == 1);
    CHECK(g_captured_report_api->report_status(g_report_submit_boot, 42u,
                                               &mqtt_state, &webhook_state) ==
          APP_REPORT_ABI_OK);
    CHECK(mqtt_state == APP_REPORT_MQTT_TRANSPORT_FAILED);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_NOT_CONFIGURED);

    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(3000u, &stop_out) == APP_SESSION_OK);
    CHECK(g_report_device_stop_count == 2);

    {
        int thread_started = tb.thread_started;

        g_report_device_start_result = AICAM_ERROR;
        memset(&out, 0, sizeof(out));
        CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
        CHECK(g_report_device_start_count == 3);
        CHECK(g_report_device_stop_count == 2);
        CHECK(g_report_device_active == AICAM_FALSE);
        CHECK(tb.thread_started == thread_started);
    }
}

static void test_v3_without_report_capability_stays_closed(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_install_record_t rec;
    uint32_t boot_id = 123u;
    uint32_t mqtt_state = APP_REPORT_MQTT_PENDING;
    uint32_t webhook_state = APP_REPORT_WEBHOOK_PENDING;

    g_trust_mode_session = AICAM_FALSE;
    CHECK(install_expected(V3_GENERIC_MINIMAL_PACKAGE,
                           V3_GENERIC_MINIMAL_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    if (out.result != APP_SESSION_OK) {
        return;
    }
    CHECK(out.abi_version == APP_SESSION_ABI_VERSION_V3);
    CHECK((out.granted_caps & APP_ADMISSION_V2_CAP_REPORT_SUBMIT) == 0u);
    CHECK(app_host_session_api_table_v3()->report_submit("{}", 2u, 1u,
                                                         &boot_id) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(boot_id == 0u);
    CHECK(app_host_session_api_table_v3()->report_status(1u, 1u, &mqtt_state,
                                                         &webhook_state) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(mqtt_state == APP_REPORT_MQTT_UNKNOWN);
    CHECK(webhook_state == APP_REPORT_WEBHOOK_UNKNOWN);
    CHECK(g_report_device_start_count == 0);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(3000u, &stop_out) == APP_SESSION_OK);
    CHECK(g_report_device_stop_count == 0);
}

static void test_frame_layout(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    mk_det(&g_det[1], 5.0f, 6.0f, 7.0f, 8.0f, 0.25f, "car");
    produce(2u, 1234u);

    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(len == APP_SESSION_EVENT_HEADER_SIZE + 2u * APP_SESSION_EVENT_RECORD_SIZE);
    CHECK(rd32(buf, 0) == len);
    CHECK(rd32(buf, 4) == 1u);
    CHECK(rd32(buf, 8) == 0u);
    CHECK(rd32(buf, 12) != 0u);
    CHECK(rd32(buf, 16) == 1u);
    CHECK(rd32(buf, 20) == 1u);
    CHECK(rd32(buf, 24) == 0u);
    CHECK(rd32(buf, 28) == 2u);
    CHECK(rd32(buf, 32) == 0u);
    CHECK(rd32(buf, 36) == 0u);
    CHECK(rd32(buf, 40) == fbits(1.0f));
    CHECK(rd32(buf, 44) == fbits(2.0f));
    CHECK(rd32(buf, 48) == fbits(3.0f));
    CHECK(rd32(buf, 52) == fbits(4.0f));
    CHECK(rd32(buf, 56) == fbits(0.5f));
    CHECK(rd32(buf, 60) == 0u);
    CHECK(rd32(buf, 64) == fbits(5.0f));
    CHECK(rd32(buf, 76) == fbits(8.0f));
    CHECK(rd32(buf, 80) == fbits(0.25f));
    CHECK(rd32(buf, 84) == 1u);
}

static void test_empty_frame_and_no_event(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;
    uint32_t t0;

    produce(0u, 77u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(len == APP_SESSION_EVENT_HEADER_SIZE);
    CHECK(rd32(buf, 4) == 1u);
    CHECK(rd32(buf, 28) == 0u);

    t0 = mono_ms();
    CHECK(pop(buf, sizeof(buf), &len, 40u) == 1);
    CHECK((uint32_t)(mono_ms() - t0) < 2000u);
}

static void test_buffer_too_small(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 55u);
    CHECK(pop(buf, APP_SESSION_EVENT_HEADER_SIZE - 1u, &len, 0u) == -2);
    CHECK(pop(buf, APP_SESSION_EVENT_HEADER_SIZE + APP_SESSION_EVENT_RECORD_SIZE, &len, 0u) == 0);
    CHECK(len == APP_SESSION_EVENT_HEADER_SIZE + APP_SESSION_EVENT_RECORD_SIZE);

    CHECK(api_tbl()->event_next(NULL, 512u, &len, 0u) == -1);
    CHECK(api_tbl()->event_next(buf, 0u, &len, 0u) == -1);
    CHECK(api_tbl()->event_next(buf, 512u, NULL, 0u) == -1);
}

static void test_sixty_four_and_over(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;
    uint32_t i;

    for (i = 0u; i < 64u; i++) {
        mk_det(&g_det[i], (float)i, 1.0f, 2.0f, 3.0f, 0.9f, "person");
    }
    produce(64u, 99u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(len == APP_SESSION_EVENT_WIRE_MAX);
    CHECK(rd32(buf, 28) == 64u);

    for (i = 0u; i < 65u; i++) {
        mk_det(&g_det[i], 1.0f, 1.0f, 1.0f, 1.0f, 0.9f, "person");
    }
    produce(65u, 100u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(len == APP_SESSION_EVENT_HEADER_SIZE);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_GAP);
    CHECK(rd32(buf, 24) == APP_SESSION_EVT_FLAG_LOST_KNOWN);
    CHECK(rd32(buf, 28) == 0u);
    CHECK(rd32(buf, 32) == 1u);
    CHECK(len == APP_SESSION_EVENT_HEADER_SIZE);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_GAP);
    CHECK(rd32(buf, 24) == APP_SESSION_EVT_FLAG_LOST_KNOWN);
    CHECK(rd32(buf, 28) == 0u);
    CHECK(rd32(buf, 32) == 1u);
}

static void test_nonfinite_and_unknown_class(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    g_det[0].conf = 1.0f / 0.0f;
    produce(1u, 101u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_GAP);
    CHECK(rd32(buf, 32) == 1u);

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "bird");
    produce(1u, 102u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_GAP);
    CHECK(rd32(buf, 32) == 1u);

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, NULL);
    produce(1u, 103u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_GAP);
}

static void test_slow_consumer_loss(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;
    uint32_t i;
    uint32_t first_seq;
    uint32_t base_dropped;
    app_host_session_status_t st;

    for (i = 0u; i < 8u; i++) {
        mk_det(&g_det[0], (float)i, 1.0f, 1.0f, 1.0f, 0.5f, "person");
        produce(1u, 200u + i);
    }
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_RUNNING);
    base_dropped = st.frames_dropped;

    mk_det(&g_det[0], 9.0f, 1.0f, 1.0f, 1.0f, 0.5f, "person");
    produce(1u, 208u);
    app_host_session_status(&st);
    CHECK(st.frames_dropped == base_dropped + 1u);

    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    first_seq = rd32(buf, 8);
    CHECK(rd32(buf, 24) == 0u);
    for (i = 1u; i < 8u; i++) {
        CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
        CHECK(rd32(buf, 8) == first_seq + i);
    }
    CHECK(rd32(buf, 24) == APP_SESSION_EVT_FLAG_LOST_KNOWN);
    CHECK(rd32(buf, 32) == 1u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 1);

    for (i = 0u; i < 8u; i++) {
        mk_det(&g_det[0], (float)i, 1.0f, 1.0f, 1.0f, 0.5f, "person");
        produce(1u, 300u + i);
    }
    produce(1u, 308u);
    produce(1u, 309u);
    app_host_session_status(&st);
    CHECK(st.frames_dropped == base_dropped + 3u);
    for (i = 0u; i < 8u; i++) {
        CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    }
    CHECK(rd32(buf, 24) == APP_SESSION_EVT_FLAG_LOST_KNOWN);
    CHECK(rd32(buf, 32) == 1u);
}

static void test_model_changed(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 400u);

    set_model(0x5000u, g_three_classes, 3u, "sess-model-v2", "2.0.0");
    produce(1u, 401u);

    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_MODEL_CHANGED);
    CHECK(rd32(buf, 16) == 2u);
    CHECK(rd32(buf, 20) == 2u);
    CHECK(rd32(buf, 24) == APP_SESSION_EVT_FLAG_LOST_KNOWN);
    CHECK(rd32(buf, 32) == 1u);
    CHECK(rd32(buf, 28) == 0u);

    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == 1u);
    CHECK(rd32(buf, 16) == 2u);
    CHECK(rd32(buf, 20) == 2u);
    CHECK(rd32(buf, 28) == 1u);
    CHECK(rd32(buf, 60) == 0u);

    CHECK(api_tbl()->class_name(1u, 1u, 0u, buf, sizeof(buf), &len) == -4);
    len = 0;
    CHECK(api_tbl()->class_name(2u, 2u, 0u, buf, sizeof(buf), &len) == 0);
    CHECK(len == 6u);
    CHECK(memcmp(buf, "person", 6u) == 0);
    len = 0;
    CHECK(api_tbl()->class_name(2u, 2u, 2u, buf, sizeof(buf), &len) == 0);
    CHECK(memcmp(buf, "dog", 3u) == 0);
    CHECK(api_tbl()->class_name(2u, 2u, 3u, buf, sizeof(buf), &len) == -1);

    len = 0;
    CHECK(api_tbl()->model_meta(buf, sizeof(buf), &len) == 0);
    CHECK(len == 128u);
    CHECK(rd32(buf, 8) == 2u);
    CHECK(rd32(buf, 12) == 2u);
    CHECK(rd32(buf, 16) == 3u);
    CHECK(rd32(buf, 20) == 0u);
    CHECK(memcmp(buf + 24, "sess-model-v2", 13u) == 0);
    CHECK(buf[24 + 13u] == 0);
    CHECK(memcmp(buf + 24 + 64u, "2.0.0", 5u) == 0);
    len = 0;
    CHECK(api_tbl()->model_meta(buf, 127u, &len) == -2);
}

static void test_model_incompatible_runtime(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    set_model_available(AICAM_FALSE);
    produce(1u, 500u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == -4);
    CHECK(api_tbl()->model_meta(buf, sizeof(buf), &len) == -4);
    CHECK(api_tbl()->class_name(1u, 1u, 0u, buf, sizeof(buf), &len) == -4);

    set_model_available(AICAM_TRUE);
    set_model(0x6000u, g_two_classes, 2u, "back", "3.0.0");
    produce(1u, 501u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_MODEL_CHANGED);
    CHECK(rd32(buf, 16) == 3u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == 1u);
}

static void test_model_changed_same_addr(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;
    uint32_t gen0;
    uint32_t mc0;
    app_host_session_status_t st;

    set_model(0x1000u, g_two_classes, 2u, "stable", "4.0.0");
    produce(1u, 799u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == APP_SESSION_EVT_KIND_MODEL_CHANGED);
    produce(1u, 800u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == 1u);
    app_host_session_status(&st);
    gen0 = st.model_generation;
    mc0 = st.model_changes;

    set_model(0x1000u, g_three_classes, 3u, "stable", "4.1.0");
    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 801u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK_MSG(rd32(buf, 4) == APP_SESSION_EVT_KIND_MODEL_CHANGED,
              "same_addr_version_class_change");
    CHECK(rd32(buf, 16) == gen0 + 1u);
    CHECK(rd32(buf, 20) == gen0 + 1u);
    app_host_session_status(&st);
    CHECK(st.model_generation == gen0 + 1u);
    CHECK(st.class_generation == gen0 + 1u);
    CHECK(st.class_count == 3u);
    CHECK(strcmp(st.model_version, "4.1.0") == 0);
    CHECK(st.model_changes == mc0 + 1u);

    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == 1u);
    CHECK(rd32(buf, 12) == 801u);
    CHECK(rd32(buf, 16) == gen0 + 1u);
    CHECK(rd32(buf, 20) == gen0 + 1u);
    CHECK(rd32(buf, 60) == 0u);

    len = 0;
    CHECK(api_tbl()->class_name(gen0, gen0, 0u, buf, sizeof(buf), &len) == -4);
    len = 0;
    CHECK(api_tbl()->class_name(gen0 + 1u, gen0 + 1u, 2u, buf, sizeof(buf), &len) == 0);
    CHECK(len == 3u);
    CHECK(memcmp(buf, "dog", 3u) == 0);

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "dog");
    produce(1u, 802u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == 1u);
    CHECK(rd32(buf, 12) == 802u);
    CHECK(rd32(buf, 16) == gen0 + 1u);
    CHECK(rd32(buf, 20) == gen0 + 1u);
    CHECK(rd32(buf, 60) == 2u);

    set_model(0x1000u, g_three_classes, 3u, "stable", "4.1.0");
    produce(1u, 803u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK_MSG(rd32(buf, 4) == 1u, "identical_model_no_event");
    CHECK(rd32(buf, 12) == 803u);
    app_host_session_status(&st);
    CHECK(st.model_generation == gen0 + 1u);
    CHECK(st.class_generation == gen0 + 1u);
}

static void test_meta_and_class_validation(void)
{
    uint8_t buf[256];
    uint32_t len = 0;
    uint32_t gen;
    app_host_session_status_t st;

    app_host_session_status(&st);
    gen = st.model_generation;
    CHECK(gen == st.class_generation);

    CHECK(api_tbl()->model_meta(NULL, 128u, &len) == -1);
    CHECK(api_tbl()->model_meta(buf, 128u, NULL) == -1);
    len = 99u;
    CHECK(api_tbl()->class_name(gen, gen, 0u, NULL, 32u, &len) == -1);
    CHECK(api_tbl()->class_name(gen, gen, 0u, buf, 32u, NULL) == -1);
    CHECK(api_tbl()->class_name(gen, gen, 99u, buf, 32u, &len) == -1);
    len = 0;
    CHECK(api_tbl()->class_name(gen, gen, 1u, buf, 3u, &len) == -2);
    len = 0;
    CHECK(api_tbl()->class_name(gen, gen, 1u, buf, 4u, &len) == 0);
    CHECK(len == 3u);
    CHECK(memcmp(buf, "car", 3u) == 0);
    len = 0;
    CHECK(api_tbl()->class_name(gen - 1u, gen - 1u, 0u, buf, 32u, &len) == -4);
}

static void test_log(void)
{
    char log_buf[APP_SESSION_LOG_CAP + 1u];
    size_t n;
    char big[1024];

    memset(big, 'x', sizeof(big));
    big[sizeof(big) - 1u] = '\0';

    CHECK(api_tbl()->log("hello-log") == 0);
    n = app_host_session_take_log(log_buf, sizeof(log_buf));
    CHECK(n == strlen("hello-log"));
    CHECK(strcmp(log_buf, "hello-log") == 0);
    n = app_host_session_take_log(log_buf, sizeof(log_buf));
    CHECK(n == 0u);
    CHECK(api_tbl()->log(big) == 0);
    CHECK(app_host_session_log_truncated() == AICAM_TRUE);
    (void)app_host_session_take_log(log_buf, sizeof(log_buf));
    CHECK(app_host_session_log_truncated() == AICAM_FALSE);
}

static void test_tick_wrap_semantics(void)
{
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 0xFFFFFF00u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 12) == 0xFFFFFF00u);
    CHECK(pop(buf, sizeof(buf), &len, 5u) == 1);
}

static void test_stop_busy_then_settles(void)
{
    app_host_session_stop_out_t stop_out;
    app_host_session_status_t st;
    app_install_record_t removed;
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    CHECK(api_tbl()->should_stop() == 0);

    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(120u, &stop_out) == APP_SESSION_ERR_BUSY);
    CHECK(stop_out.entry_returned == AICAM_FALSE);
    CHECK(strcmp(stop_out.reason, "entry_not_returned") == 0);
    CHECK(api_tbl()->should_stop() == 1);
    CHECK(api_tbl()->event_next(buf, sizeof(buf), &len, 0u) == -10);

    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_STOPPING);
    CHECK(st.app_running == AICAM_TRUE);
    CHECK(app_install_uninstall(&g_session_io, &removed) == APP_INSTALL_ERR_BUSY);

    tb.park_release = 1;
    wait_idle();
    CHECK(app_install_app_running() == AICAM_FALSE);
    CHECK(tb.detach_count == 1);
    CHECK(api_tbl()->event_next(buf, sizeof(buf), &len, 0u) == -3);
    CHECK(app_host_session_stop_requested() == AICAM_FALSE);

    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(10u, &stop_out) == APP_SESSION_ERR_NOT_RUNNING);
}

static void test_cooperative_stop_and_restart(void)
{
    app_host_session_stop_out_t stop_out;
    app_host_session_start_out_t out;
    app_install_record_t rec;
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;

    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 600u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 8) == 0u);
    CHECK(rd32(buf, 16) == 1u);

    tb.park_release = 1;

    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    CHECK(stop_out.entry_returned == AICAM_TRUE);
    CHECK(app_install_app_running() == AICAM_FALSE);

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 601u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 8) == 0u);
    CHECK(rd32(buf, 16) == 1u);
    CHECK(rd32(buf, 20) == 1u);

    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
}

static void test_entry_consumes_until_stopping(void)
{
    app_host_session_stop_out_t stop_out;
    app_host_session_start_out_t out;
    app_install_record_t rec;

    g_entry_kind_count = 0;
    memset(g_entry_kinds, 0, sizeof(g_entry_kinds));
    tb.body = tb_body_consumer;

    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 700u);
    produce(0u, 701u);
    sleep_ms(150);
    CHECK(g_entry_kind_count == 2);
    CHECK(g_entry_kinds[0] == 1);
    CHECK(g_entry_kinds[1] == 1);

    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    CHECK(stop_out.entry_returned == AICAM_TRUE);
    CHECK(g_entry_kind_count == 2);
    tb.body = NULL;
}

static void test_attach_failure_refused(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_host_session_status_t st;
    app_install_record_t rec;
    int d0;

    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    tb.fail_attach = AICAM_TRUE;
    d0 = tb.detach_count;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_RUNTIME_UNAVAILABLE);
    CHECK_MSG(strcmp(out.reason, "ai_attach_failed") == 0, "attach_fail_refused");
    CHECK(tb.detach_count == d0);
    CHECK(app_install_app_running() == AICAM_FALSE);
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_IDLE);
    CHECK(strcmp(st.last_reason, "ai_attach_failed") == 0);

    tb.fail_attach = AICAM_FALSE;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
}

static void test_spawn_failure_refused(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_host_session_status_t st;
    app_install_record_t rec;
    int scrub0;

    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    tb.fail_spawn = 1;
    scrub0 = tb_scrub_count_get();
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_RUNTIME_UNAVAILABLE);
    CHECK_MSG(strcmp(out.reason, "entry_spawn_failed") == 0, "spawn_fail_refused");
    CHECK(app_install_app_running() == AICAM_FALSE);
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_IDLE);
    CHECK(st.app_running == AICAM_FALSE);
    CHECK(strcmp(st.last_reason, "entry_spawn_failed") == 0);
    CHECK(tb_scrub_count_get() == scrub0 + 1);
    tb.fail_spawn = 0;

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
}

static void test_start_blocked_until_teardown_done(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_host_session_status_t st;
    app_install_record_t rec;
    app_install_record_t removed;
    start_call_t calls[4];
    pthread_t tids[4];
    int i;
    int spins;
    int scrub_before;

    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);

    scrub_before = tb_scrub_count_get();
    tb_scrub_hold_set(1);
    tb.park_release = 1;
    spins = 0;
    while (tb_scrub_count_get() < scrub_before + 1) {
        sleep_ms(2);
        spins++;
        if (spins > 5000) {
            break;
        }
    }
    CHECK(tb_scrub_count_get() >= scrub_before + 1);

    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_STOPPING);
    CHECK(st.app_running == AICAM_TRUE);
    CHECK(app_install_uninstall(&g_session_io, &removed) == APP_INSTALL_ERR_BUSY);

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_BUSY);
    CHECK_MSG(strcmp(out.reason, "session_active") == 0, "start_busy_during_cleanup");

    for (i = 0; i < 4; i++) {
        calls[i].rc = APP_SESSION_OK;
        calls[i].reason[0] = '\0';
        CHECK(pthread_create(&tids[i], NULL, start_thread_fn, &calls[i]) == 0);
    }
    for (i = 0; i < 4; i++) {
        pthread_join(tids[i], NULL);
        CHECK(calls[i].rc == APP_SESSION_ERR_BUSY);
    }
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_STOPPING);
    CHECK(app_install_app_running() == AICAM_TRUE);

    tb_scrub_hold_set(0);
    wait_idle();
    CHECK(app_install_app_running() == AICAM_FALSE);
    CHECK(tb_scrub_count_get() == scrub_before + 1);

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    CHECK(stop_out.entry_returned == AICAM_TRUE);
}

static int wait_thread_exit_count(int target)
{
    int spins = 0;

    while (tb.thread_exited < target) {
        sleep_ms(2);
        spins++;
        if (spins > 5000) {
            break;
        }
    }
    return tb.thread_exited;
}

static void test_start_blocked_until_worker_terminal(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_host_session_status_t st;
    app_install_record_t rec;
    app_install_record_t removed;
    start_call_t calls[4];
    pthread_t tids[4];
    int spawns0;
    int exited0;
    int i;

    tb.park_release = 0;
    tb.terminal_hold = 0;
    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    spawns0 = tb.thread_started;
    exited0 = tb.thread_exited;

    tb.terminal_hold = 1;
    tb.park_release = 1;
    CHECK(wait_thread_exit_count(exited0 + 1) == exited0 + 1);

    app_host_session_status(&st);
    CHECK_MSG(st.state == APP_SESSION_STATE_STOPPING, "latch_keeps_stopping");
    CHECK_MSG(st.app_running == AICAM_TRUE, "latch_keeps_app_running");
    CHECK(app_install_app_running() == AICAM_TRUE);
    CHECK_MSG(app_install_uninstall(&g_session_io, &removed) == APP_INSTALL_ERR_BUSY,
              "uninstall_blocked_until_terminal");
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK_MSG(app_host_session_stop(20u, &stop_out) == APP_SESSION_ERR_BUSY,
              "stop_busy_until_terminal");

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_BUSY);
    CHECK_MSG(strcmp(out.reason, "session_active") == 0, "start_busy_until_terminal");
    CHECK_MSG(tb.thread_started == spawns0, "no_respawn_until_terminal");

    for (i = 0; i < 4; i++) {
        calls[i].rc = APP_SESSION_OK;
        calls[i].reason[0] = '\0';
        CHECK(pthread_create(&tids[i], NULL, start_thread_fn, &calls[i]) == 0);
    }
    for (i = 0; i < 4; i++) {
        pthread_join(tids[i], NULL);
        CHECK_MSG(calls[i].rc == APP_SESSION_ERR_BUSY,
                  "concurrent_start_busy_until_terminal");
    }
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_STOPPING);
    CHECK(app_install_app_running() == AICAM_TRUE);
    CHECK_MSG(tb.thread_started == spawns0, "stack_not_reused_until_terminal");

    tb.terminal_hold = 0;
    wait_idle();
    app_host_session_status(&st);
    CHECK_MSG(st.state == APP_SESSION_STATE_IDLE, "status_poll_reaps_terminal");
    CHECK(app_install_app_running() == AICAM_FALSE);

    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK_MSG(tb.thread_started == spawns0 + 1, "respawn_after_terminal");
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_RUNNING);

    tb.terminal_hold = 1;
    tb.park_release = 1;
    CHECK(wait_thread_exit_count(exited0 + 2) == exited0 + 2);
    app_host_session_status(&st);
    CHECK_MSG(st.state == APP_SESSION_STATE_STOPPING, "second_cycle_latched");

    tb.terminal_hold = 0;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    CHECK(stop_out.entry_returned == AICAM_TRUE);
    app_host_session_status(&st);
    CHECK(st.state == APP_SESSION_STATE_IDLE);
}

static void test_v3_report_stop_return_race(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_install_record_t rec;
    report_race_start_gate_t gate;
    report_race_stop_call_t stop_call;
    pthread_t return_tid;
    pthread_t stop_tid;
    uint32_t old_boot;
    uint32_t submit_boot;
    uint32_t mqtt_state;
    uint32_t webhook_state;
    int lock_entries;
    int thread_started;
    char json[] = "{\"schema_version\":1}";

    g_trust_mode_session = AICAM_FALSE;
    tb.v3_body = tb_body_v3_report;
    tb.park_release = 0;
    CHECK(install_expected(V3_LINE_CROSSING_PACKAGE,
                           V3_LINE_CROSSING_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK(wait_report_body(1000u) == 1);
    old_boot = g_report_submit_boot;
    CHECK(app_report_service_queue_count() == 1u);
    thread_started = tb.thread_started;

    memset(&gate, 0, sizeof(gate));
    pthread_mutex_init(&gate.mutex, NULL);
    pthread_cond_init(&gate.cond, NULL);
    memset(&stop_call, 0, sizeof(stop_call));
    stop_call.gate = &gate;
    pthread_mutex_lock(&g_report_race_mutex);
    g_report_race_gate_enabled = 1;
    g_report_race_gate_released = 0;
    g_report_race_lock_entries = 0;
    g_report_race_life_attempts = 0;
    pthread_mutex_unlock(&g_report_race_mutex);
    CHECK(pthread_create(&return_tid, NULL, report_race_return_thread,
                         &gate) == 0);
    CHECK(pthread_create(&stop_tid, NULL, report_race_stop_thread,
                         &stop_call) == 0);
    CHECK(report_race_start_gate_open(&gate, 2, 1000u) == 1);
    CHECK(wait_report_unbind_interleave(2000u) == 1);
    pthread_join(return_tid, NULL);
    pthread_join(stop_tid, NULL);
    lock_entries = report_race_gate_disable();
    pthread_cond_destroy(&gate.cond);
    pthread_mutex_destroy(&gate.mutex);

    CHECK(stop_call.result == APP_SESSION_OK);
    CHECK(wait_thread_exit_count(1) == 1);
    CHECK(g_report_device_stop_count == 1);
    CHECK(lock_entries == 3);
    CHECK(tb.thread_started == thread_started);
    CHECK(app_report_service_queue_count() == 0u);
    submit_boot = 1u;
    CHECK(g_captured_report_api->report_submit(json, (uint32_t)strlen(json),
                                                42u, &submit_boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(submit_boot == 0u);
    CHECK(app_host_session_stop(20u, &stop_out) == APP_SESSION_ERR_NOT_RUNNING);
    CHECK(g_report_device_stop_count == 1);

    tb.park_release = 0;
    g_report_body_done = 0;
    g_report_test_seq = 42u;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK(wait_report_body(1000u) == 1);
    CHECK(g_captured_report_api->report_status(old_boot, 41u, &mqtt_state,
                                                &webhook_state) ==
          APP_REPORT_ABI_NOT_FOUND);
    CHECK(app_report_service_queue_count() == 1u);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    CHECK(g_report_device_stop_count == 2);

    tb.park_release = 0;
    tb.fail_spawn = 1;
    g_report_body_done = 0;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_RUNTIME_UNAVAILABLE);
    CHECK(strcmp(out.reason, "entry_spawn_failed") == 0);
    CHECK(app_install_app_running() == AICAM_FALSE);
    CHECK(tb.thread_started == thread_started + 2);
    CHECK(g_report_device_stop_count == 3);
    submit_boot = 1u;
    CHECK(app_host_session_api_table_v3()->report_submit(json,
              (uint32_t)strlen(json), 43u, &submit_boot) ==
          APP_REPORT_ABI_UNAUTHORIZED);
    CHECK(submit_boot == 0u);
    tb.fail_spawn = 0;
}

static void test_v3_report_quiesce_pending_blocks_reuse(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_install_record_t rec;
    uint32_t old_boot;
    uint32_t mqtt_state;
    uint32_t webhook_state;
    int thread_started;

    g_trust_mode_session = AICAM_FALSE;
    tb.v3_body = tb_body_v3_report;
    tb.park_release = 0;
    CHECK(install_expected(V3_LINE_CROSSING_PACKAGE,
                           V3_LINE_CROSSING_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK(wait_report_body(1000u) == 1);
    old_boot = g_report_submit_boot;
    thread_started = tb.thread_started;

    g_report_device_stop_result = AICAM_ERROR;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(20u, &stop_out) == APP_SESSION_ERR_BUSY);
    CHECK(g_report_device_quiesce_pending == AICAM_TRUE);
    CHECK(app_report_service_device_running() == AICAM_TRUE);
    CHECK(tb.thread_started == thread_started);
    {
        char json[] = "{\"schema_version\":1}";
        uint32_t submit_boot = 1u;

        CHECK(g_captured_report_api->report_submit(json,
                  (uint32_t)strlen(json), 42u, &submit_boot) ==
              APP_REPORT_ABI_UNAUTHORIZED);
        CHECK(submit_boot == 0u);
    }
    tb.park_release = 1;
    CHECK(wait_thread_exit_count(1) == 1);
    wait_idle();

    tb.park_release = 0;
    g_report_body_done = 0;
    g_report_test_seq = 42u;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
    CHECK(strcmp(out.reason, "report_service_unavailable") == 0);
    CHECK(tb.thread_started == thread_started);
    CHECK(app_install_app_running() == AICAM_FALSE);
    CHECK(g_report_device_quiesce_pending == AICAM_TRUE);

    g_report_device_quiesced = AICAM_TRUE;
    g_report_device_stop_result = AICAM_OK;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK(wait_report_body(1000u) == 1);
    CHECK(g_report_device_quiesce_pending == AICAM_FALSE);
    CHECK(tb.thread_started == thread_started + 1);
    CHECK(g_captured_report_api->report_status(old_boot, 41u, &mqtt_state,
                                                &webhook_state) ==
          APP_REPORT_ABI_NOT_FOUND);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    CHECK(g_report_device_quiesce_pending == AICAM_FALSE);
}

static void tb_body_v4_probe(const app_host_session_api_table_v4_t *api)
{
    uint8_t wire[APP_CONTROL_MSG_RAM_MAX];
    uint32_t actual_len = 0u;
    g_v4_api = api;
    g_v4_table_size = api->table_size;
    g_v4_table_abi = api->abi_version;
    g_v4_live_publish_rc = api->live_publish(NULL, 0u);
    g_v4_control_next_rc = api->control_next(wire, sizeof(wire), &actual_len);
    g_v4_body_called = 1;
    while (tb.park_release == 0) {
        sleep_ms(1u);
    }
}

static void v4_put_u16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void v4_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t v4_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t v4_floor_generation(void)
{
    mem_kv_t *kv = mem_find_key(APP_NATIVE_VNEXT_FLOOR_KEY);
    uint64_t generation = 0u;
    uint32_t i;
    if (kv == NULL || kv->len < 24u) {
        return 0u;
    }
    for (i = 0u; i < 8u; i++) {
        generation |= (uint64_t)kv->data[16u + i] << (i * 8u);
    }
    return generation;
}

static void test_v4_session_and_paused_floor_rollback(void)
{
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_install_record_t old_v3;
    app_install_record_t v4_record;
    app_install_record_t current;
    app_control_request_t req;
    mem_io_t *store;
    uint8_t record_blob[APP_INSTALL_RECORD_BIN_SIZE];
    uint8_t control_wire[APP_CONTROL_MSG_RAM_MAX];
    uint8_t receipt[APP_CONTROL_HEADER_SIZE];
    uint32_t control_len = 0u;
    int attach_count;
    int spins;

    g_trust_mode_session = AICAM_FALSE;
    CHECK(install_expected(V3_GENERIC_MINIMAL_PACKAGE,
                           V3_GENERIC_MINIMAL_PACKAGE_LEN, &old_v3) == APP_INSTALL_OK);
    CHECK(install_expected(V4_GENERIC_PACKAGE,
                           V4_GENERIC_PACKAGE_LEN, &v4_record) == APP_INSTALL_OK);
    tb.v4_body = tb_body_v4_probe;
    tb.park_release = 0;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK(out.abi_version == APP_SESSION_ABI_VERSION_V4);
    CHECK(out.granted_caps == 0x63u);
    for (spins = 0; spins < 1000 && g_v4_body_called == 0; spins++) {
        sleep_ms(1u);
    }
    CHECK(g_v4_body_called != 0);
    CHECK(g_v4_table_size == APP_SESSION_V4_ABI_TABLE_SIZE);
    CHECK(g_v4_table_abi == APP_SESSION_ABI_VERSION_V4);
    CHECK(g_v4_live_publish_rc == APP_SESSION_ERR_UNAVAILABLE);
    CHECK(g_v4_control_next_rc == APP_CONTROL_NO_EVENT);
    memset(&req, 0, sizeof(req));
    req.request_id = 6u;
    req.msg_type = APP_CONTROL_MSG_TYPE_COUNTERS_RESET;
    req.flags = APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED;
    req.expected_config_generation = 1u;
    CHECK(app_host_control_register(&req) == APP_CONTROL_REG_OK);
    CHECK(g_v4_api->control_next(control_wire, sizeof(control_wire), &control_len) ==
          APP_CONTROL_OK);
    CHECK(control_len == APP_CONTROL_HEADER_SIZE);
    CHECK(memcmp(control_wire, "NFC4", 4u) == 0);
    CHECK(control_wire[4] == 1u && control_wire[5] == 0u);
    CHECK(control_wire[6] == APP_CONTROL_MSG_TYPE_COUNTERS_RESET &&
          control_wire[7] == 0u);
    CHECK(v4_get_u32(control_wire + 28u) == APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED);
    CHECK(v4_get_u32(control_wire + 12u) == out.session_generation);
    memset(receipt, 0, sizeof(receipt));
    memcpy(receipt, "NFC3", 4u);
    v4_put_u16(receipt + 4u, APP_CONTROL_HEADER_VERSION);
    v4_put_u16(receipt + 6u, APP_CONTROL_MSG_TYPE_RECEIPT);
    v4_put_u32(receipt + 8u, sizeof(receipt));
    v4_put_u32(receipt + 12u, out.session_generation);
    v4_put_u32(receipt + 16u, 6u);
    v4_put_u32(receipt + 20u, 1u);
    CHECK(g_v4_api->control_reply(receipt, sizeof(receipt)) ==
          APP_CONTROL_INVALID_ARGUMENT);
    memcpy(receipt, "NFC4", 4u);
    v4_put_u32(receipt + 12u, out.session_generation + 1u);
    CHECK(g_v4_api->control_reply(receipt, sizeof(receipt)) ==
          APP_CONTROL_INVALID_ARGUMENT);
    v4_put_u32(receipt + 12u, out.session_generation);
    CHECK(g_v4_api->control_reply(receipt, sizeof(receipt)) == APP_CONTROL_OK);
    store = (mem_io_t *)g_session_io.ctx;
    snprintf(store->fail_kv_write_key, sizeof(store->fail_kv_write_key), "%s",
             APP_NATIVE_VNEXT_FLOOR_KEY);
    CHECK(app_native_vnext_floor_raise(&g_session_io, &v4_record,
                                       out.session_generation, 17u, 9u, 1u, 0u) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_native_vnext_floor_validate(&g_session_io) == APP_INSTALL_OK);
    snprintf(store->fail_kv_read_key, sizeof(store->fail_kv_read_key), "%s",
             APP_NATIVE_VNEXT_FLOOR_KEY);
    store->fail_kv_read_key_skip = 1;
    CHECK(app_native_vnext_floor_raise(&g_session_io, &v4_record,
                                       out.session_generation, 17u, 9u, 1u, 0u) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(v4_floor_generation() == 1u);
    CHECK(app_native_vnext_floor_raise(&g_session_io, &v4_record,
                                       out.session_generation, 17u, 9u, 1u, 0u) ==
          APP_INSTALL_OK);
    CHECK(v4_floor_generation() == 1u);
    CHECK(app_native_vnext_floor_raise(&g_session_io, &v4_record,
                                       out.session_generation, 17u, 9u, 2u, 1u) ==
          APP_INSTALL_ERR_IDENTITY_CONFLICT);
    store->tx_held = 1;
    CHECK(app_native_vnext_floor_raise(&g_session_io, &v4_record,
                                       out.session_generation, 17u, 10u, 2u, 1u) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    store->tx_held = 0;
    CHECK(v4_floor_generation() == 1u);
    CHECK(app_native_vnext_floor_raise(&g_session_io, &v4_record,
                                       out.session_generation, 17u, 10u, 2u, 1u) ==
          APP_INSTALL_OK);
    CHECK(v4_floor_generation() == 2u);
    memset(&req, 0, sizeof(req));
    req.request_id = 7u;
    req.msg_type = 5u;
    req.expected_config_generation = 1u;
    CHECK(app_host_control_register(&req) == APP_CONTROL_ERR_UNAUTHORIZED);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(3000u, &stop_out) == APP_SESSION_OK);
    wait_idle();
    CHECK(app_native_vnext_floor_validate(&g_session_io) == APP_INSTALL_OK);
    app_install_boot_resolve_reset();
    CHECK(app_install_boot_resolve(&g_session_io) == APP_INSTALL_OK);

    attach_count = tb.attach_count;
    CHECK(g_session_io.file_write(g_session_io.ctx, old_v3.package_path,
                                  V3_GENERIC_MINIMAL_PACKAGE,
                                  V3_GENERIC_MINIMAL_PACKAGE_LEN) == APP_INSTALL_IO_OK);
    CHECK(app_install_record_encode(&old_v3, record_blob,
                                    sizeof(record_blob)) == AICAM_TRUE);
    CHECK(g_session_io.kv_write(g_session_io.ctx, APP_INSTALL_KEY_RECORD,
                                record_blob, sizeof(record_blob)) == APP_INSTALL_IO_OK);
    app_install_set_app_running(AICAM_FALSE);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_UNAVAILABLE);
    CHECK(strcmp(out.reason, "DOWNGRADE_REFUSED") == 0);
    CHECK(tb.attach_count == attach_count);
    CHECK(app_install_query_committed(&g_session_io, &current,
                                      &(aicam_bool_t){AICAM_FALSE}) == APP_INSTALL_OK);
}

static size_t read_app37_fixture(const char *path, uint8_t *buf, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    long file_len;
    size_t read_len;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return 0u;
    }
    file_len = ftell(file);
    if (file_len <= 0L || (size_t)file_len > capacity || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return 0u;
    }
    read_len = fread(buf, 1u, (size_t)file_len, file);
    fclose(file);
    return read_len == (size_t)file_len ? read_len : 0u;
}

static void live_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t live_header(uint8_t *buf, uint32_t kind, uint32_t total_len,
                            uint32_t packet_seq, uint32_t through_seq,
                            uint32_t source_total, uint32_t source_gap,
                            uint32_t config_generation, uint32_t record_count,
                            uint32_t flags)
{
    memset(buf, 0, APP_HOST_LIVE_HEADER_SIZE);
    memcpy(buf, "LVN4", 4u);
    buf[4] = 1u;
    buf[5] = 0u;
    buf[6] = (uint8_t)(kind & 0xFFu);
    buf[7] = (uint8_t)((kind >> 8) & 0xFFu);
    live_put_u32(buf + 8u, total_len);
    live_put_u32(buf + 12u, packet_seq);
    live_put_u32(buf + 16u, through_seq);
    live_put_u32(buf + 20u, source_total);
    live_put_u32(buf + 24u, source_gap);
    live_put_u32(buf + 32u, config_generation);
    live_put_u32(buf + 36u, record_count);
    live_put_u32(buf + 40u, flags);
    live_put_u32(buf + 44u, 0u);
    return APP_HOST_LIVE_HEADER_SIZE;
}

static void live_event(uint8_t *rec, uint32_t event_seq, uint32_t track_id,
                       uint32_t direction)
{
    live_put_u32(rec + 0u, event_seq);
    live_put_u32(rec + 4u, track_id);
    live_put_u32(rec + 8u, 1000u + event_seq);
    rec[12u] = (uint8_t)direction;
    rec[13u] = 0u;
    rec[14u] = 10u;
    rec[15u] = 20u;
}

static void test_app37_package_session_authorization(void)
{
    uint8_t live[512];
    uint8_t enable[512];
    app_install_record_t record;
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_host_live_status_t live_st;
    app_host_live_read_out_t ro;
    app_host_live_event_t evts[4];
    uint8_t packet[64];
    uint8_t enable_state_blob[16] = { 1u, 2u, 3u };
    uint8_t enable_payload[APP_CONTROL_ENABLE_PAYLOAD_SIZE] = {
        0x45u, 0x4Eu, 0x42u, 0x31u, 2u, 0u, 0u, 0u,
        1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u
    };
    uint8_t enable_wire[APP_CONTROL_ENABLE_SET_SIZE];
    uint8_t enable_receipt[APP_CONTROL_ENABLE_RECEIPT_SIZE];
    app_control_status_t control_status;
    uint32_t state_len = 0u;
    uint32_t state_revision = 0u;
    uint32_t new_revision = 0u;
    uint32_t wire_len = 0u;
    size_t live_len;
    size_t enable_len;
    uint32_t spins;

    live_len = read_app37_fixture("../app_native_vnext/fixtures/app-pr37/live-only.neapp.v4",
                                  live, sizeof(live));
    enable_len = read_app37_fixture("../app_native_vnext/fixtures/app-pr37/enable.neapp.v4",
                                    enable, sizeof(enable));
    g_app37_spki_der_len = read_app37_fixture(
        "../app_native_vnext/fixtures/app-pr37/dev-publisher.spki.der",
        g_app37_spki_der, sizeof(g_app37_spki_der));
    CHECK(live_len != 0u && enable_len != 0u && g_app37_spki_der_len != 0u);
    g_trust_mode_app37 = AICAM_TRUE;
    g_trust_mode_session = AICAM_FALSE;
    g_trust_mode_v3 = AICAM_FALSE;
    fill_policies();
    s_v4_policy.supported_caps_mask = APP_ADMISSION_V4_KNOWN_CAPS_MASK;
    s_v4_policy.live_publish_capacity = 9008u;
    s_v4_policy.live_track_capacity = 64u;

    mem_reset();
    mem_io_init(&g_session_io);
    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
    CHECK(install_expected(enable, enable_len, &record) == APP_INSTALL_OK);
    tb.v4_body = tb_body_v4_probe;
    tb.park_release = 0;
    g_v4_body_called = 0;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK(out.abi_version == APP_SESSION_ABI_VERSION_V4);
    CHECK(out.granted_caps == 0x173u);
    for (spins = 0; spins < 1000 && g_v4_body_called == 0; spins++) {
        sleep_ms(1u);
    }
    CHECK(g_v4_body_called != 0);
    CHECK(g_v4_api->state_read(enable_state_blob, sizeof(enable_state_blob),
                               &state_len, &state_revision) == APP_STATE_NOT_FOUND);
    CHECK(state_revision == 0u);
    CHECK(g_v4_api->state_commit(enable_state_blob, sizeof(enable_state_blob),
                                 0u, &new_revision) == APP_STATE_OK);
    CHECK(new_revision == 1u);
    {
        app_control_request_t request;

        memset(&request, 0, sizeof(request));
        request.request_id = 401u;
        request.msg_type = APP_CONTROL_MSG_TYPE_ENABLE_SET;
        request.payload = enable_payload;
        request.payload_len = sizeof(enable_payload);
        request.expected_config_generation = 1u;
        CHECK(app_host_control_register(&request) == APP_CONTROL_REG_OK);
    }
    CHECK(mem_find_key(APP_NATIVE_VNEXT_FLOOR_KEY) != NULL);
    CHECK(g_v4_api->control_next(enable_wire, sizeof(enable_wire), &wire_len) ==
          APP_CONTROL_OK);
    CHECK(wire_len == APP_CONTROL_ENABLE_SET_SIZE);
    CHECK(memcmp(enable_wire, "NFC4", 4u) == 0);
    CHECK(enable_wire[6] == APP_CONTROL_MSG_TYPE_ENABLE_SET);
    CHECK(v4_get_u32(enable_wire + 24u) == 0u);
    CHECK(g_v4_api->state_commit(enable_state_blob, sizeof(enable_state_blob),
                                 1u, &new_revision) == APP_STATE_OK);
    CHECK(new_revision == 2u);
    state_len = 0u;
    state_revision = 0u;
    CHECK(g_v4_api->state_read(enable_state_blob, sizeof(enable_state_blob),
                               &state_len, &state_revision) == APP_STATE_OK);
    CHECK(state_len == sizeof(enable_state_blob));
    CHECK(state_revision == 2u);
    memset(enable_receipt, 0, sizeof(enable_receipt));
    memcpy(enable_receipt, "NFC4", 4u);
    v4_put_u16(enable_receipt + 4u, APP_CONTROL_HEADER_VERSION);
    v4_put_u16(enable_receipt + 6u, APP_CONTROL_MSG_TYPE_RECEIPT);
    v4_put_u32(enable_receipt + 8u, sizeof(enable_receipt));
    v4_put_u32(enable_receipt + 12u, out.session_generation);
    v4_put_u32(enable_receipt + 16u, 401u);
    v4_put_u32(enable_receipt + 20u, 1u);
    v4_put_u32(enable_receipt + 24u, 2u);
    v4_put_u32(enable_receipt + 28u, APP_CONTROL_RESULT_APPLIED);
    memcpy(enable_receipt + APP_CONTROL_HEADER_SIZE, "ENR1", 4u);
    v4_put_u32(enable_receipt + APP_CONTROL_HEADER_SIZE + 4u,
               APP_CONTROL_ENABLE_STATE_PAUSED);
    v4_put_u32(enable_receipt + APP_CONTROL_HEADER_SIZE + 8u, 2u);
    CHECK(g_v4_api->control_reply(enable_receipt, sizeof(enable_receipt)) ==
          APP_CONTROL_OK);
    app_host_control_status(&control_status);
    CHECK(control_status.command_state == APP_CONTROL_CMD_APPLIED);
    CHECK(control_status.command_request_id == 401u);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    printf("APP37_SESSION case=enable install=0 session=0 caps=0x173 floor=verified\n");

    mem_reset();
    mem_io_init(&g_session_io);
    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
    CHECK(install_expected(live, live_len, &record) == APP_INSTALL_OK);
    tb.v4_body = tb_body_v4_probe;
    tb.park_release = 0;
    g_v4_body_called = 0;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK(out.abi_version == APP_SESSION_ABI_VERSION_V4);
    CHECK(out.granted_caps == 0x223u);
    for (spins = 0; spins < 1000 && g_v4_body_called == 0; spins++) {
        sleep_ms(1u);
    }
    CHECK(g_v4_body_called != 0);
    CHECK(g_v4_live_publish_rc == APP_HOST_LIVE_ERR_PARAM);
    app_host_live_status(&live_st);
    CHECK(live_st.bound == AICAM_TRUE);
    CHECK(live_st.session_generation == out.session_generation);
    CHECK(live_st.live_publish_max == 9008u);
    CHECK(live_st.live_track_max == 64u);
    CHECK(strcmp(live_st.publisher_id, "dev-publisher") == 0);
    (void)live_header(packet, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 1u, 4u, 4u,
                      0u, 9u, 0u, 0u);
    CHECK(g_v4_api->live_publish(packet, 48u) == APP_HOST_LIVE_OK);
    (void)live_header(packet, APP_HOST_LIVE_KIND_EVENT_BATCH, 64u, 2u, 4u, 4u,
                      0u, 9u, 1u, 0u);
    live_event(packet + APP_HOST_LIVE_HEADER_SIZE, 1u, 7u,
               APP_HOST_LIVE_DIRECTION_IN);
    CHECK(g_v4_api->live_publish(packet, 64u) == APP_HOST_LIVE_OK);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(out.session_generation, 0u, evts, 4u, &ro) ==
          APP_HOST_LIVE_OK);
    CHECK(ro.returned == 1u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_CONTINUOUS);
    CHECK(evts[0].event_seq == 1u);
    CHECK(evts[0].track_id == 7u);
    CHECK(evts[0].direction == APP_HOST_LIVE_DIRECTION_IN);
    CHECK(ro.through_seq == 4u);
    CHECK(ro.source_total == 4u);
    CHECK(ro.source_gap == 0u);
    CHECK(ro.session_generation == out.session_generation);
    (void)live_header(packet, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 3u, 4u, 4u,
                      0u, 9u, 0u, 0u);
    packet[0] = 'X';
    CHECK(g_v4_api->live_publish(packet, 48u) == APP_HOST_LIVE_ERR_HEADER);
    app_host_live_status(&live_st);
    CHECK(live_st.rejected_header == 1u);
    CHECK(live_st.packet_count == 2u);
    CHECK(live_st.event_count == 1u);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    app_host_live_status(&live_st);
    CHECK(live_st.bound == AICAM_FALSE);
    CHECK(g_v4_api->live_publish(packet, 48u) == APP_SESSION_ERR_UNAVAILABLE);
    printf("APP37_SESSION case=live-only install=0 session=0 reason=OK\n");

    g_trust_mode_app37 = AICAM_FALSE;
    g_trust_mode_session = AICAM_TRUE;
    fill_policies();
}

static void test_v4_live_session_ingest_read(void)
{
    app_install_record_t record;
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_host_live_status_t live_st;
    app_host_live_read_out_t ro;
    app_host_live_event_t evts[16];
    uint8_t packet[APP_HOST_LIVE_PUBLISH_CAPACITY];
    uint32_t len;
    uint32_t i;
    uint32_t spins;

    g_trust_mode_session = AICAM_FALSE;
    g_trust_mode_v3 = AICAM_FALSE;
    g_trust_mode_app37 = AICAM_FALSE;
    fill_policies();
    s_v4_policy.supported_caps_mask = APP_ADMISSION_V4_KNOWN_CAPS_MASK;
    s_v4_policy.live_publish_capacity = 9008u;
    s_v4_policy.live_track_capacity = 64u;
    CHECK(install_expected(V4_LIVE_64_PACKAGE,
                           V4_LIVE_64_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    tb.v4_body = tb_body_v4_probe;
    tb.park_release = 0;
    g_v4_body_called = 0;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK(out.abi_version == APP_SESSION_ABI_VERSION_V4);
    CHECK(out.granted_caps == 0x263u);
    CHECK(out.control_max == 32u);
    for (spins = 0; spins < 1000 && g_v4_body_called == 0; spins++) {
        sleep_ms(1u);
    }
    CHECK(g_v4_body_called != 0);
    app_host_live_status(&live_st);
    CHECK(live_st.bound == AICAM_TRUE);
    CHECK(live_st.live_publish_max == 9008u);
    CHECK(live_st.live_track_max == 64u);
    (void)live_header(packet, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 1u, 0u, 0u,
                      0u, 1u, 0u, 0u);
    CHECK(g_v4_api->live_publish(packet, 48u) == APP_HOST_LIVE_OK);
    len = APP_HOST_LIVE_HEADER_SIZE + 2u * APP_HOST_LIVE_EVENT_RECORD_SIZE;
    (void)live_header(packet, APP_HOST_LIVE_KIND_EVENT_BATCH, len, 2u, 2u, 2u,
                      0u, 1u, 2u, 0u);
    live_event(packet + APP_HOST_LIVE_HEADER_SIZE, 1u, 11u,
               APP_HOST_LIVE_DIRECTION_IN);
    live_event(packet + APP_HOST_LIVE_HEADER_SIZE +
                   APP_HOST_LIVE_EVENT_RECORD_SIZE,
               2u, 12u, APP_HOST_LIVE_DIRECTION_OUT);
    CHECK(g_v4_api->live_publish(packet, len) == APP_HOST_LIVE_OK);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(out.session_generation, 0u, evts, 16u, &ro) ==
          APP_HOST_LIVE_OK);
    CHECK(ro.returned == 2u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_CONTINUOUS);
    CHECK(evts[0].event_seq == 1u && evts[1].event_seq == 2u);
    CHECK(evts[0].direction == APP_HOST_LIVE_DIRECTION_IN);
    CHECK(evts[1].direction == APP_HOST_LIVE_DIRECTION_OUT);
    memset(packet, 0, sizeof(packet));
    len = APP_HOST_LIVE_HEADER_SIZE + 16u * APP_HOST_LIVE_EVENT_RECORD_SIZE;
    (void)live_header(packet, APP_HOST_LIVE_KIND_EVENT_BATCH, len, 3u, 36u,
                      36u, 0u, 1u, 16u, 0u);
    for (i = 0u; i < 16u; i++) {
        live_event(packet + APP_HOST_LIVE_HEADER_SIZE +
                       i * APP_HOST_LIVE_EVENT_RECORD_SIZE,
                   i + 3u, 20u + i,
                   (i % 2u == 0u) ? APP_HOST_LIVE_DIRECTION_IN
                                  : APP_HOST_LIVE_DIRECTION_OUT);
    }
    CHECK(g_v4_api->live_publish(packet, len) == APP_HOST_LIVE_OK);
    len = 48u;
    (void)live_header(packet, APP_HOST_LIVE_KIND_EVENT_BATCH, len, 4u, 56u,
                      56u, 20u, 1u, 0u, APP_HOST_LIVE_FLAG_SOURCE_GAP_DELTA);
    CHECK(g_v4_api->live_publish(packet, len) == APP_HOST_LIVE_OK);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(out.session_generation, 0u, evts, 16u, &ro) ==
          APP_HOST_LIVE_OK);
    CHECK(ro.returned == 16u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_GAPPED);
    CHECK(ro.through_seq == 56u);
    CHECK(ro.source_total == 56u);
    CHECK(ro.source_gap == 20u);
    CHECK(ro.host_dropped_events == 0u);
    CHECK(ro.oldest_cursor == 1u);
    CHECK(ro.newest_cursor == 18u);
    len = APP_HOST_LIVE_HEADER_SIZE + 17u * APP_HOST_LIVE_EVENT_RECORD_SIZE;
    (void)live_header(packet, APP_HOST_LIVE_KIND_EVENT_BATCH, len, 5u, 73u,
                      73u, 20u, 1u, 17u, 0u);
    CHECK(g_v4_api->live_publish(packet, len) == APP_HOST_LIVE_ERR_OVERFLOW);
    (void)live_header(packet, APP_HOST_LIVE_KIND_EVENT_BATCH, 48u, 6u, 56u,
                      56u, 20u, 2u, 0u, 0u);
    CHECK(g_v4_api->live_publish(packet, 48u) ==
          APP_HOST_LIVE_ERR_CONFIG_GENERATION);
    (void)live_header(packet, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 48u, 6u, 56u,
                      56u, 20u, 1u, 1u, 0u);
    CHECK(g_v4_api->live_publish(packet, 48u) ==
          APP_HOST_LIVE_ERR_LENGTH);
    {
        app_host_live_track_t tracks[2];
        app_host_live_snapshot_out_t so;

        memset(packet, 0, 128u);
        (void)live_header(packet, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 96u, 6u,
                          56u, 56u, 20u, 1u, 2u, 0u);
        live_put_u32(packet + 48u, 31u);
        packet[52u] = 2u;
        packet[56u] = 1u;
        packet[58u] = 0x64u;
        live_put_u32(packet + 60u, 5000u);
        packet[64u] = 0x10u;
        packet[65u] = 0x27u;
        live_put_u32(packet + 68u, 6000u);
        packet[72u] = 0x20u;
        packet[73u] = 0x4Eu;
        live_put_u32(packet + 76u, 32u);
        packet[80u] = 1u;
        packet[82u] = 0xFFu;
        packet[83u] = 0xFFu;
        packet[84u] = 1u;
        packet[86u] = 0x10u;
        packet[87u] = 0x27u;
        live_put_u32(packet + 88u, 7000u);
        packet[92u] = 0x30u;
        packet[93u] = 0x75u;
        CHECK(g_v4_api->live_publish(packet, 96u) == APP_HOST_LIVE_OK);
        memset(&so, 0, sizeof(so));
        CHECK(app_host_live_snapshot_read(out.session_generation, tracks, 2u,
                                          &so) == APP_HOST_LIVE_OK);
        CHECK(so.track_count == 2u);
        CHECK(so.packet_seq == 6u);
        CHECK(so.source_gap == 20u);
        CHECK(tracks[0].track_id == 31u);
        CHECK(tracks[0].point_count == 2u);
        CHECK(tracks[0].class_index == 0u);
        CHECK(tracks[0].confidence_q16 == 100u);
        CHECK(tracks[0].points[0].ts_ms == 5000u);
        CHECK(tracks[0].points[0].x_q16 == 10000u);
        CHECK(tracks[0].points[1].ts_ms == 6000u);
        CHECK(tracks[0].points[1].x_q16 == 20000u);
        CHECK(tracks[1].track_id == 32u);
        CHECK(tracks[1].point_count == 1u);
        CHECK(tracks[1].class_index == APP_HOST_LIVE_CLASS_UNKNOWN);
        CHECK(tracks[1].confidence_q16 == 10000u);
        CHECK(tracks[1].points[0].ts_ms == 7000u);
        CHECK(tracks[1].points[0].x_q16 == 30000u);
    }
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    app_host_live_status(&live_st);
    CHECK(live_st.bound == AICAM_FALSE);
    CHECK(app_host_live_read(out.session_generation, 0u, evts, 16u, &ro) ==
          APP_HOST_LIVE_ERR_UNBOUND);
}

static int g_v3_park_called;

static void tb_body_v3_park(const app_host_session_api_table_v3_t *api)
{
    (void)api;
    g_v3_park_called = 1;
    while (tb.park_release == 0) {
        sleep_ms(1u);
    }
}

static int app27_joint_start(app_host_session_start_out_t *out)
{
    uint32_t spins;

    memset(out, 0, sizeof(*out));
    if (start_ok(out) != APP_SESSION_OK) {
        return -1;
    }
    if (out->abi_version != APP_SESSION_ABI_VERSION_V4) {
        return -1;
    }
    for (spins = 0; spins < 1000 && g_v4_body_called == 0; spins++) {
        sleep_ms(1u);
    }
    return (g_v4_body_called != 0) ? 0 : -1;
}

static int app27_joint_stop(void)
{
    app_host_session_stop_out_t stop_out;

    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    return (app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK) ? 0 : -1;
}

static void test_v4_live_app27_same_bytes_joint(void)
{
    app_install_record_t record;
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_host_live_status_t live_st;
    app_host_live_read_out_t ro;
    app_host_live_snapshot_out_t so;
    app_host_live_event_t evts[32];
    app_host_live_track_t tracks[64];
    uint32_t gen;
    uint32_t gen1;
    uint32_t spins;

    g_trust_mode_session = AICAM_FALSE;
    g_trust_mode_v3 = AICAM_FALSE;
    g_trust_mode_app37 = AICAM_FALSE;
    fill_policies();
    s_v4_policy.supported_caps_mask = APP_ADMISSION_V4_KNOWN_CAPS_MASK;
    s_v4_policy.live_publish_capacity = 9008u;
    s_v4_policy.live_track_capacity = 64u;
    CHECK(install_expected(V4_LIVE_64_PACKAGE, V4_LIVE_64_PACKAGE_LEN,
                           &record) == APP_INSTALL_OK);
    tb.v4_body = tb_body_v4_probe;
    tb.park_release = 0;
    g_v4_body_called = 0;

    CHECK(app27_joint_start(&out) == 0);
    gen1 = out.session_generation;
    CHECK(out.granted_caps == 0x263u);
    app_host_live_status(&live_st);
    CHECK(live_st.bound == AICAM_TRUE);
    CHECK(live_st.live_publish_max == 9008u);
    CHECK(live_st.live_track_max == 64u);
    CHECK(strcmp(live_st.publisher_id, "ne301-exp-pub") == 0);
    CHECK(strcmp(live_st.app_id, "hello-app") == 0);
    CHECK(g_v4_api->live_publish(APP27_KIND1_EVENT_BATCH,
                                 APP27_LEN_KIND1_EVENT_BATCH) ==
          APP_HOST_LIVE_OK);
    CHECK(g_v4_api->live_publish(APP27_KIND1_EVENT_BATCH,
                                 APP27_LEN_KIND1_EVENT_BATCH) ==
          APP_HOST_LIVE_OK);
    app_host_live_status(&live_st);
    CHECK(live_st.idempotent_replays == 1u);
    CHECK(g_v4_api->live_publish(APP27_KIND1_SOURCE_GAP,
                                 APP27_LEN_KIND1_SOURCE_GAP) ==
          APP_HOST_LIVE_ERR_SEQUENCE);
    CHECK(g_v4_api->live_publish(APP27_KIND2_TRACKS_64X16,
                                 APP27_LEN_KIND2_TRACKS_64X16) ==
          APP_HOST_LIVE_ERR_SEQUENCE);
    memset(&ro, 0, sizeof(ro));
    memset(evts, 0, sizeof(evts));
    CHECK(app_host_live_read(gen1, 0u, evts, 32u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 1u);
    CHECK(evts[0].event_seq == 1u);
    CHECK(evts[0].track_id == 1u);
    CHECK(evts[0].event_ts_ms == 2800u);
    CHECK(evts[0].direction == APP_HOST_LIVE_DIRECTION_IN);
    CHECK(evts[0].x_q8 == 128u);
    CHECK(evts[0].y_q8 == 148u);
    CHECK(ro.through_seq == 1u);
    CHECK(ro.source_total == 1u);
    CHECK(ro.source_gap == 0u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_CONTINUOUS);
    CHECK(ro.session_generation == gen1);
    CHECK(app27_joint_stop() == 0);
    CHECK(g_v4_api->live_publish(APP27_KIND2_ZERO_TRACK,
                                 APP27_LEN_KIND2_ZERO_TRACK) ==
          APP_SESSION_ERR_UNAVAILABLE);
    CHECK(app_host_live_read(gen1, 0u, evts, 32u, &ro) ==
          APP_HOST_LIVE_ERR_UNBOUND);

    tb.park_release = 0;
    g_v4_body_called = 0;
    CHECK(app27_joint_start(&out) == 0);
    gen = out.session_generation;
    CHECK(gen != gen1);
    CHECK(g_v4_api->live_publish(APP27_KIND1_SOURCE_GAP,
                                 APP27_LEN_KIND1_SOURCE_GAP) ==
          APP_HOST_LIVE_OK);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(gen, 0u, evts, 32u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 16u);
    CHECK(ro.through_seq == 64u);
    CHECK(ro.source_total == 64u);
    CHECK(ro.source_gap == 48u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_GAPPED);
    CHECK(g_v4_api->live_publish(APP27_KIND2_ZERO_TRACK,
                                 APP27_LEN_KIND2_ZERO_TRACK) ==
          APP_HOST_LIVE_OK);
    memset(&so, 0, sizeof(so));
    memset(tracks, 0, sizeof(tracks));
    CHECK(app_host_live_snapshot_read(gen, tracks, 64u, &so) ==
          APP_HOST_LIVE_OK);
    CHECK(so.track_count == 0u);
    CHECK(so.packet_seq == 48u);
    CHECK(so.source_gap == 48u);
    CHECK(app27_joint_stop() == 0);

    tb.park_release = 0;
    g_v4_body_called = 0;
    CHECK(app27_joint_start(&out) == 0);
    gen = out.session_generation;
    CHECK(g_v4_api->live_publish(APP27_KIND2_TRACKS_64X16,
                                 APP27_LEN_KIND2_TRACKS_64X16) ==
          APP_HOST_LIVE_OK);
    memset(&so, 0, sizeof(so));
    memset(tracks, 0, sizeof(tracks));
    CHECK(app_host_live_snapshot_read(gen, tracks, 64u, &so) ==
          APP_HOST_LIVE_OK);
    CHECK(so.track_count == 64u);
    CHECK(so.packet_seq == 32u);
    CHECK(tracks[0].track_id == 1u);
    CHECK(tracks[0].point_count == 16u);
    CHECK(tracks[0].class_index == 1u);
    CHECK(tracks[0].points[0].ts_ms == 1000u);
    CHECK(tracks[0].points[0].x_q16 == 1311u);
    CHECK(tracks[0].points[0].y_q16 == 27525u);
    CHECK(tracks[63].track_id == 64u);
    CHECK(tracks[63].point_count == 16u);
    CHECK(app27_joint_stop() == 0);

    tb.park_release = 0;
    g_v4_body_called = 0;
    CHECK(app27_joint_start(&out) == 0);
    gen = out.session_generation;
    CHECK(g_v4_api->live_publish(APP27_KIND1_RESYNC,
                                 APP27_LEN_KIND1_RESYNC) == APP_HOST_LIVE_OK);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(gen, 0u, evts, 32u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 0u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_UNKNOWN);
    CHECK(ro.resync_required == AICAM_TRUE);
    CHECK(g_v4_api->live_publish(APP27_KIND2_RESYNC,
                                 APP27_LEN_KIND2_RESYNC) == APP_HOST_LIVE_OK);
    memset(&so, 0, sizeof(so));
    memset(tracks, 0, sizeof(tracks));
    CHECK(app_host_live_snapshot_read(gen, tracks, 64u, &so) ==
          APP_HOST_LIVE_OK);
    CHECK(so.track_count == 1u);
    CHECK(tracks[0].track_id == 1u);
    CHECK(tracks[0].point_count == 1u);
    CHECK(tracks[0].points[0].x_q16 == 32768u);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(gen, 0u, evts, 32u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_CONTINUOUS);
    CHECK(ro.resync_required == AICAM_FALSE);
    CHECK(app27_joint_stop() == 0);

    CHECK(install_expected(V4_LIVE_PACKAGE, V4_LIVE_PACKAGE_LEN,
                           &record) == APP_INSTALL_OK);
    tb.park_release = 0;
    g_v4_body_called = 0;
    CHECK(app27_joint_start(&out) == 0);
    gen = out.session_generation;
    app_host_live_status(&live_st);
    CHECK(live_st.live_publish_max == 312u);
    CHECK(live_st.live_track_max == 1u);
    CHECK(g_v4_api->live_publish(APP27_KIND1_SOURCE_GAP,
                                 APP27_LEN_KIND1_SOURCE_GAP) ==
          APP_HOST_LIVE_OK);
    CHECK(g_v4_api->live_publish(APP27_KIND2_ZERO_TRACK,
                                 APP27_LEN_KIND2_ZERO_TRACK) ==
          APP_HOST_LIVE_OK);
    CHECK(g_v4_api->live_publish(APP27_KIND2_TRACKS_64X16,
                                 APP27_LEN_KIND2_TRACKS_64X16) ==
          APP_HOST_LIVE_ERR_LENGTH);
    app_host_live_status(&live_st);
    CHECK(live_st.rejected_length == 1u);
    CHECK(live_st.snapshot_tracks == 0u);
    CHECK(app27_joint_stop() == 0);

    CHECK(install_expected(V4_TAMPERED_TBS_PACKAGE,
                           V4_TAMPERED_TBS_PACKAGE_LEN,
                           &record) != APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_ERR_NOT_INSTALLED);
    app_host_live_status(&live_st);
    CHECK(live_st.bound == AICAM_FALSE);

    g_trust_mode_session = AICAM_FALSE;
    fill_policies();
    CHECK(install_expected(V3_LINE_CROSSING_PACKAGE,
                           V3_LINE_CROSSING_PACKAGE_LEN,
                           &record) == APP_INSTALL_OK);
    tb.v3_body = tb_body_v3_park;
    tb.v4_body = tb_body_v4_probe;
    tb.park_release = 0;
    g_v4_body_called = 0;
    g_v3_park_called = 0;
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);
    CHECK(out.abi_version == APP_SESSION_ABI_VERSION_V3);
    CHECK((out.granted_caps & 0x200u) == 0u);
    for (spins = 0; spins < 1000 && g_v3_park_called == 0; spins++) {
        sleep_ms(1u);
    }
    CHECK(g_v3_park_called == 1);
    CHECK(g_v4_body_called == 0);
    app_host_live_status(&live_st);
    CHECK(live_st.bound == AICAM_FALSE);
    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    printf("APP27_SESSION case=same-bytes-joint event+gap+zerotrack+tracks+"
           "resync+caps+tbs+v3 done\n");
}

typedef struct {
    uint32_t attempts;
    uint32_t accepted;
    int32_t last;
} app_entry_joint_pub_t;

static app_entry_joint_pub_t g_app_entry_pub;

static int32_t app_entry_joint_live_publish(void *user, const void *buf,
                                            uint32_t len)
{
    app_entry_joint_pub_t *pub = (app_entry_joint_pub_t *)user;
    int32_t rc;

    pub->attempts++;
    rc = g_v4_api->live_publish(buf, len);
    pub->last = rc;
    if (rc == APP_HOST_LIVE_OK) {
        pub->accepted++;
    }
    return rc;
}

static void app_entry_joint_set_enable(lcstub_t *stub, uint32_t enable_state)
{
    lc_bus_config_t cfg;
    uint8_t state[LC_ST_BLOB_MAX];
    uint32_t len;

    lc_bus_config_defaults(&cfg);
    len = lc_st_encode_enable(state, sizeof(state), &cfg, 7u, 4u, 2u, 1u, 9u,
                              1u, enable_state);
    lcstub_set_state(stub, state, len, 6u);
}

static void app_entry_joint_push_path(lcstub_t *stub)
{
    static const float y[] = {0.30f, 0.44f, 0.58f, 0.72f};
    uint32_t i;

    for (i = 0u; i < sizeof(y) / sizeof(y[0]); i++) {
        lcstub_det_t det = {0.45f, y[i] - 0.02f, 0.1f, 0.04f, 0.9f, 0u};
        lcstub_push_frame(stub, i + 1u, 100u + i * 100u, 7u, 4u, 0u, 0u, &det,
                          1u);
    }
}

static int app_entry_joint_run(lcstub_t *stub)
{
    lc_app_api_v4_t api;

    memset(&g_app_entry_pub, 0, sizeof(g_app_entry_pub));
    stub->auto_stop_after_no_events = 1u;
    lcstub_set_model(stub, 1, 7u, 4u, 2u, "od-demo", "1.2");
    lcstub_set_class(stub, 0u, "person");
    stub->live_publish_hook = app_entry_joint_live_publish;
    stub->live_publish_user = &g_app_entry_pub;
    lcstub_make_table_v4(stub, &api);
    return app_entry(&api, LC_APP_ABI_V4);
}

static void test_v4_live_app_entry_r3_signed_session_joint(void)
{
    app_install_record_t record;
    app_host_session_start_out_t out;
    app_host_live_status_t live_st;
    app_host_live_read_out_t ro;
    app_host_live_snapshot_out_t so;
    app_host_live_event_t evts[8];
    app_host_live_track_t tracks[4];
    lcstub_t *stub;
    uint32_t gen;

    CHECK_MSG(strcmp(APP27_FIXTURES_APP_SOURCE_SHA,
                     "c06fdedc5c7422b88d24a1e07d018dd07ff41567") == 0,
              "app27_r3_pin");
    g_trust_mode_session = AICAM_FALSE;
    g_trust_mode_v3 = AICAM_FALSE;
    g_trust_mode_app37 = AICAM_FALSE;
    fill_policies();
    s_v4_policy.supported_caps_mask = APP_ADMISSION_V4_KNOWN_CAPS_MASK;
    s_v4_policy.live_publish_capacity = 9008u;
    s_v4_policy.live_track_capacity = 64u;
    CHECK(install_expected(V4_LIVE_64_PACKAGE, V4_LIVE_64_PACKAGE_LEN,
                           &record) == APP_INSTALL_OK);
    tb.v4_body = tb_body_v4_probe;
    tb.park_release = 0;
    g_v4_body_called = 0;
    CHECK(app27_joint_start(&out) == 0);
    gen = out.session_generation;
    CHECK(out.granted_caps == 0x263u);
    app_host_live_status(&live_st);
    CHECK(live_st.bound == AICAM_TRUE);
    CHECK(live_st.live_publish_max == 9008u);
    CHECK(live_st.live_track_max == 64u);
    CHECK(strcmp(live_st.publisher_id, "ne301-exp-pub") == 0);
    CHECK(strcmp(live_st.app_id, "hello-app") == 0);

    stub = lcstub_new();
    app_entry_joint_set_enable(stub, LC_ST_ENABLE_ENABLED);
    app_entry_joint_push_path(stub);
    CHECK(app_entry_joint_run(stub) == LC_APP_EXIT_OK);
    CHECK(stub->live_publish_calls > 0u);
    CHECK(g_app_entry_pub.attempts == stub->live_publish_calls);
    CHECK(g_app_entry_pub.accepted == stub->live_publish_calls);
    CHECK(g_app_entry_pub.last == APP_HOST_LIVE_OK);
    memset(&ro, 0, sizeof(ro));
    memset(evts, 0, sizeof(evts));
    CHECK(app_host_live_read(gen, 0u, evts, 8u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned >= 1u);
    CHECK(ro.through_seq == ro.returned);
    CHECK(ro.source_total == ro.returned);
    CHECK(ro.source_gap == 0u);
    CHECK(ro.state == APP_HOST_LIVE_READ_STATE_CONTINUOUS);
    CHECK(ro.session_generation == gen);
    CHECK(ro.freshness == APP_HOST_LIVE_FRESHNESS_FRESH);
    CHECK(evts[0].direction == APP_HOST_LIVE_DIRECTION_IN);
    CHECK(evts[0].event_seq == 1u);
    memset(&so, 0, sizeof(so));
    memset(tracks, 0, sizeof(tracks));
    CHECK(app_host_live_snapshot_read(gen, tracks, 4u, &so) ==
          APP_HOST_LIVE_OK);
    CHECK(so.track_count == 1u);
    CHECK(so.freshness == APP_HOST_LIVE_FRESHNESS_FRESH);
    lcstub_destroy(stub);
    CHECK(app27_joint_stop() == 0);
    app_host_live_status(&live_st);
    CHECK(live_st.bound == AICAM_FALSE);
    CHECK(g_v4_api->live_publish(APP27_KIND1_EVENT_BATCH,
                                 APP27_LEN_KIND1_EVENT_BATCH) ==
          APP_SESSION_ERR_UNAVAILABLE);

    tb.park_release = 0;
    g_v4_body_called = 0;
    CHECK(app27_joint_start(&out) == 0);
    gen = out.session_generation;
    app_host_live_status(&live_st);
    CHECK(live_st.packet_count == 0u);
    stub = lcstub_new();
    app_entry_joint_set_enable(stub, LC_ST_ENABLE_PAUSED);
    app_entry_joint_push_path(stub);
    CHECK(app_entry_joint_run(stub) == LC_APP_EXIT_OK);
    CHECK(stub->live_publish_calls == 0u);
    CHECK(g_app_entry_pub.attempts == 0u);
    app_host_live_status(&live_st);
    CHECK(live_st.packet_count == 0u);
    CHECK(live_st.event_count == 0u);
    memset(&ro, 0, sizeof(ro));
    CHECK(app_host_live_read(gen, 0u, evts, 8u, &ro) == APP_HOST_LIVE_OK);
    CHECK(ro.returned == 0u);
    CHECK(ro.freshness == APP_HOST_LIVE_FRESHNESS_NONE);
    lcstub_destroy(stub);
    CHECK(app27_joint_stop() == 0);
    printf("APP27_SESSION case=app-entry-r3-signed-joint arm+publish+paused "
           "done\n");
}

static app_host_session_backend_t g_dev_session_backend;

static void dev_backend_prepare(void)
{
    const app_host_session_backend_t *dev = app_host_session_device_backend();

    CHECK(dev != NULL);
    g_dev_session_backend = *dev;
    g_dev_session_backend.stage_base = tb_stage_base;
    g_dev_session_backend.stage_commit = tb_stage_commit;
    g_dev_session_backend.invoke_entry = tb_invoke_entry;
    g_dev_session_backend.scrub = tb_scrub;
    g_dev_session_backend.entry_spawn = tb_entry_spawn;
    g_dev_session_backend.entry_exited = tb_entry_exited;
    g_dev_session_backend.pair_authorize = tb_pair_authorize;
    CHECK(g_dev_session_backend.ai_model_view == dev->ai_model_view);
    CHECK(g_dev_session_backend.ai_attach == dev->ai_attach);
    CHECK(g_dev_session_backend.ai_detach == dev->ai_detach);
}

static void dev_seam_fill_model(uintptr_t config_addr,
                                uintptr_t model_addr, uint32_t model_size,
                                const char *name, const char *version)
{
    nn_model_info_t *info = dev_seam_model_info();

    memset((void *)info, 0, sizeof(*info));
    snprintf(info->name, sizeof(info->name), "%s", name);
    snprintf(info->version, sizeof(info->version), "%s", version);
    snprintf(info->postprocess_type, sizeof(info->postprocess_type), "od_yolov8");
    info->config_ptr = config_addr;
    info->model_ptr = model_addr;
    info->model_size = model_size;
}

static void test_dev_model_digest_bounds(void)
{
    const app_host_session_backend_t *dev = app_host_session_device_backend();
    app_session_model_view_t view;
    app_session_model_view_t cached;
    nn_model_info_t *info;
    long page;
    uint8_t *region;
    const size_t region_bytes = 2u * 1024u * 1024u;
    const size_t strip_bytes = 128u * 1024u;
    uint32_t t0;
    uint32_t t1;
    uint32_t gen0;
    int i;

    CHECK(dev != NULL);
    dev_seam_reset();
    info = dev_seam_model_info();
    dev_seam_fill_model(0u, 0x91000000u, 4096u, "bound-model", "1.0.0");
    dev_seam_set_state(NN_STATE_READY);

    page = sysconf(_SC_PAGESIZE);
    region = mmap(NULL, region_bytes + (size_t)page, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(region != MAP_FAILED);
    CHECK(mprotect(region + region_bytes, (size_t)page, PROT_NONE) == 0);

    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_FALSE, "null_config_refused");

    memset(region + region_bytes - strip_bytes, 'x', strip_bytes);
    dev_seam_fill_model((uintptr_t)(region + region_bytes - strip_bytes),
                        0x91000000u, 4096u, "bound-model", "1.0.0");
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_FALSE, "unterminated_config_refused");

    t0 = mono_ms();
    for (i = 0; i < 1000; i++) {
        if (dev->ai_model_view(NULL, &view) != AICAM_FALSE) {
            break;
        }
    }
    t1 = mono_ms();
    CHECK_MSG(i == 1000, "unterminated_config_stable_fail");
    CHECK_MSG((t1 - t0) < 5000u, "unterminated_config_bounded_time");

    dev_seam_set_info_rc(-1);
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_FALSE, "info_failure_refused");
    dev_seam_set_info_rc(0);

    dev_seam_set_state(NN_STATE_INIT);
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_FALSE, "not_ready_refused");
    dev_seam_set_state(NN_STATE_READY);

    memset((void *)info->name, 'n', sizeof(info->name));
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_FALSE, "unterminated_name_refused");
    snprintf((char *)info->name, sizeof(info->name), "bound-model");

    region[0] = '{';
    memcpy(region + 1, "\"postprocess_params\":{\"class_names\":[\"person\",\"car\"]}}", 54);
    memset(region + 55, ' ', 59943);
    region[59998] = '\0';
    dev_seam_fill_model((uintptr_t)region, 0x91000000u, 4096u, "bound-model", "1.0.0");
    t0 = mono_ms();
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_TRUE, "large_config_accepted");
    t1 = mono_ms();
    CHECK_MSG((t1 - t0) < 2000u, "large_config_bounded_time");
    CHECK(view.content_digest != 0u);
    CHECK(view.class_count == 2u);
    CHECK(strcmp(view.class_names[0], "person") == 0);
    CHECK(strcmp(view.class_names[1], "car") == 0);
    CHECK(strcmp(view.model_name, "bound-model") == 0);
    cached = view;

    CHECK(mprotect(region, region_bytes + (size_t)page, PROT_NONE) == 0);
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_TRUE, "hot_path_uses_cache_key");
    CHECK(view.content_digest == cached.content_digest);
    CHECK(mprotect(region, region_bytes + (size_t)page, PROT_READ | PROT_WRITE) == 0);

    region[8] = 'x';
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_TRUE, "content_mutation_key_hit");
    CHECK(view.content_digest == cached.content_digest);

    gen0 = video_ai_node_model_load_generation();
    CHECK(video_ai_node_load_model(dev_seam_ai_node(), 0u) == AICAM_OK);
    CHECK(video_ai_node_model_load_generation() == gen0 + 1u);
    CHECK_MSG(dev->ai_model_view(NULL, &view) == AICAM_TRUE, "reload_recomputes");
    CHECK_MSG(view.content_digest != cached.content_digest, "generation_changes_digest");

    munmap(region, region_bytes + (size_t)page);
}

static void test_dev_load_generation_calibration(void)
{
    video_node_t *node;
    uint32_t gen;

    dev_seam_reset();
    node = dev_seam_ai_node();
    CHECK(node != NULL);
    gen = video_ai_node_model_load_generation();

    CHECK(video_ai_node_load_model(node, 0u) == AICAM_OK);
    CHECK_MSG(video_ai_node_model_load_generation() == gen + 1u, "active_load_counts_once");
    CHECK(dev_seam_load_calls() == 1u);
    CHECK(dev_seam_last_load_ptr() == (uintptr_t)AI_1_BASE + 1024u);

    CHECK(video_ai_node_load_model(node, 0u) == AICAM_OK);
    CHECK_MSG(video_ai_node_model_load_generation() == gen + 2u, "second_active_load_counts_once");
    CHECK(dev_seam_load_calls() == 2u);

    dev_seam_set_load_rc(-1);
    CHECK(video_ai_node_load_model(node, 0u) == AICAM_ERROR);
    CHECK_MSG(video_ai_node_model_load_generation() == gen + 2u, "failed_load_no_count");
    CHECK(dev_seam_load_calls() == 3u);

    dev_seam_set_load_rc(0);
    CHECK(video_ai_node_load_model(node, (uintptr_t)0x92000000u) == AICAM_OK);
    CHECK_MSG(video_ai_node_model_load_generation() == gen + 3u, "explicit_load_counts_once");
    CHECK(dev_seam_last_load_ptr() == (uintptr_t)0x92000000u);
    CHECK(dev_seam_active_switch_calls() >= 1u);
}

static void test_dev_model_changed_via_load(void)
{
    static char dev_cfg[256];
    app_host_session_start_out_t out;
    app_host_session_stop_out_t stop_out;
    app_install_record_t rec;
    uint8_t buf[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len = 0;
    app_host_session_status_t st;
    uint32_t gen_before;

    dev_seam_reset();
    dev_seam_fill_model(0u, 0x91000000u, 0x200000u, "load-model", "6.0.0");
    snprintf(dev_cfg, sizeof(dev_cfg),
             "{\"postprocess_params\":{\"class_names\":[\"person\",\"car\"]}}");
    dev_seam_model_info()->config_ptr = (uintptr_t)dev_cfg;
    dev_seam_set_state(NN_STATE_READY);
    CHECK(video_ai_node_load_model(dev_seam_ai_node(), 0u) == AICAM_OK);

    dev_backend_prepare();
    app_host_session_init(&g_dev_session_backend);
    mem_reset();
    mem_io_init(&g_session_io);
    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
    fill_policies();
    g_trust_mode_session = AICAM_TRUE;
    CHECK(install_expected(SESS_PKG, SESS_PKG_LEN, &rec) == APP_INSTALL_OK);
    memset(&out, 0, sizeof(out));
    CHECK(start_ok(&out) == APP_SESSION_OK);

    mk_det(&g_det[0], 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, "person");
    produce(1u, 900u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK_MSG(rd32(buf, 4) == 1u, "dev_view_frame_after_start");
    app_host_session_status(&st);
    CHECK(st.model_changes == 1u);
    CHECK(st.class_count == 2u);

    CHECK(video_ai_node_load_model(dev_seam_ai_node(), 0u) == AICAM_OK);
    produce(1u, 901u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK_MSG(rd32(buf, 4) == APP_SESSION_EVT_KIND_MODEL_CHANGED,
              "same_metadata_reload_model_changed");
    app_host_session_status(&st);
    CHECK(st.model_changes == 2u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK(rd32(buf, 4) == 1u);

    produce(1u, 902u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK_MSG(rd32(buf, 4) == 1u, "no_reload_no_model_changed");

    dev_seam_set_load_rc(-1);
    CHECK(video_ai_node_load_model(dev_seam_ai_node(), 0u) == AICAM_ERROR);
    dev_seam_set_load_rc(0);
    app_host_session_status(&st);
    gen_before = st.model_generation;
    produce(1u, 903u);
    CHECK(pop(buf, sizeof(buf), &len, 0u) == 0);
    CHECK_MSG(rd32(buf, 4) == 1u, "failed_load_no_model_changed");
    app_host_session_status(&st);
    CHECK(st.model_generation == gen_before);

    tb.park_release = 1;
    memset(&stop_out, 0, sizeof(stop_out));
    CHECK(app_host_session_stop(2000u, &stop_out) == APP_SESSION_OK);
    CHECK(stop_out.entry_returned == AICAM_TRUE);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    reset_all();
    test_container_version();
    test_unauthorized_before_start();
    test_start_refusals();
    reset_all();
    test_start_success_and_double();
    test_frame_layout();
    test_empty_frame_and_no_event();
    test_buffer_too_small();
    test_sixty_four_and_over();
    test_nonfinite_and_unknown_class();
    test_slow_consumer_loss();
    test_model_changed();
    test_model_incompatible_runtime();
    test_model_changed_same_addr();
    test_meta_and_class_validation();
    test_log();
    test_tick_wrap_semantics();
    test_stop_busy_then_settles();
    test_cooperative_stop_and_restart();
    test_entry_consumes_until_stopping();
    test_attach_failure_refused();
    test_spawn_failure_refused();
    test_start_blocked_until_teardown_done();
    test_start_blocked_until_worker_terminal();

    reset_all();
    test_v3_report_capability_path();
    reset_all();
    test_v3_without_report_capability_stays_closed();
    reset_all();
    test_v3_report_stop_return_race();
    reset_all();
    test_v3_report_quiesce_pending_blocks_reuse();
    reset_all();
    test_v4_session_and_paused_floor_rollback();
    reset_all();
    test_app37_package_session_authorization();
    reset_all();
    test_v4_live_session_ingest_read();
    reset_all();
    test_v4_live_app27_same_bytes_joint();
    reset_all();
    test_v4_live_app_entry_r3_signed_session_joint();

    reset_all();
    test_dev_model_digest_bounds();
    test_dev_load_generation_calibration();
    reset_all();
    test_dev_model_changed_via_load();

    printf("app_host_session: total=%d fail=%d\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
