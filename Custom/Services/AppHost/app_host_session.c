#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "app_host_session.h"

#include "app_host.h"
#include "app_pair_installer.h"
#include "app_sidecar_lifecycle.h"
#include "app_state_store.h"
#include "app_report_service.h"
#include "app_report_service_device.h"
#include "app_native_vnext_floor.h"
#include "app_host_live.h"

#include "mbedtls/sha256.h"

#if UINTPTR_MAX == 0xFFFFFFFFu
_Static_assert(sizeof(app_host_session_api_table_t) == 48u, "session api table layout");
_Static_assert(sizeof(app_host_session_api_table_v3_t) == 60u, "session v3 api table layout");
_Static_assert(sizeof(app_host_session_api_table_v4_t) == 64u, "session v4 api table layout");
#else
_Static_assert(sizeof(app_host_session_api_table_t) ==
               (2u * sizeof(uint32_t) + 10u * sizeof(app_session_fn_log_t)),
               "session api table layout");
_Static_assert(sizeof(app_host_session_api_table_v3_t) ==
               (2u * sizeof(uint32_t) + 13u * sizeof(app_session_fn_log_t)),
               "session v3 api table layout");
_Static_assert(sizeof(app_host_session_api_table_v4_t) ==
               (2u * sizeof(uint32_t) + 14u * sizeof(app_session_fn_log_t)),
               "session v4 api table layout");
#endif
_Static_assert(sizeof(((app_host_session_api_table_t *)0)->table_size) == 4u,
               "session table header width");

typedef struct {
    uint8_t wire[APP_SESSION_EVENT_WIRE_MAX];
    uint32_t len;
} session_slot_t;

static const app_host_session_backend_t *g_backend;
static void *g_mtx;
static void *g_report_life_mtx;
static void *g_enable_mtx;
static void *g_evt;
static app_session_state_t g_state;
static aicam_bool_t g_stop_requested;
static aicam_bool_t g_attached;
static aicam_bool_t g_model_incompatible;
static aicam_bool_t g_entry_alive;
static aicam_bool_t g_exit_latch;
static uint32_t g_model_generation;
static uint32_t g_class_generation;
static uint32_t g_sequence;
static uint32_t g_pending_lost;
static aicam_bool_t g_pending_lost_unknown;
static uint32_t g_ring_head;
static uint32_t g_ring_count;
static session_slot_t g_ring[APP_SESSION_RING_SLOTS];
static app_session_model_view_t g_model;
static app_install_record_t g_record;
static uint32_t g_event_max;
static uint32_t g_events_delivered;
static uint32_t g_frames_dropped;
static uint32_t g_gap_events;
static uint32_t g_model_changes;
static app_session_result_t g_last_result;
static char g_last_reason[APP_SESSION_REASON_SLOT];
static char g_log[APP_SESSION_LOG_CAP];
static size_t g_log_len;
static aicam_bool_t g_log_truncated;
static app_host_image_info_t g_image_info;
static app_host_session_api_table_t g_table;
static app_host_session_api_table_v3_t g_table_v3;
static app_host_session_api_table_v4_t g_table_v4;
static aicam_bool_t g_is_v3;
static uint32_t g_v3_caps;
static uint32_t g_container_major;
static aicam_bool_t g_state_bit;
static aicam_bool_t g_state_bound;
static app_state_session_t g_state_session;
static aicam_bool_t g_report_bound;
static aicam_bool_t g_report_worker_started;
static app_report_session_t g_report_session;
static uint32_t g_session_generation;
static uint32_t g_enable_boot_id;
static const app_install_io_t *g_enable_io;

typedef struct {
    aicam_bool_t active;
    aicam_bool_t committed;
    aicam_bool_t readback;
    uint32_t session_generation;
    uint32_t request_id;
    uint32_t config_generation;
    uint32_t expected_revision;
    uint32_t desired_state;
    uint32_t committed_revision;
    uint32_t blob_len;
    uint8_t blob_sha256[APP_INSTALL_SHA256_LEN];
} sess_enable_witness_t;

static sess_enable_witness_t g_enable_witness;

static void sess_lock(void);
static void sess_unlock(void);

static void sess_enable_lock(void)
{
    if (g_backend != NULL && g_backend->mutex_lock != NULL &&
        g_enable_mtx != NULL) {
        g_backend->mutex_lock(g_backend->ctx, g_enable_mtx);
    }
}

static void sess_enable_unlock(void)
{
    if (g_backend != NULL && g_backend->mutex_unlock != NULL &&
        g_enable_mtx != NULL) {
        g_backend->mutex_unlock(g_backend->ctx, g_enable_mtx);
    }
}

static void sess_report_lifecycle_lock(void)
{
    if (g_backend != NULL && g_backend->mutex_lock != NULL &&
        g_report_life_mtx != NULL) {
        g_backend->mutex_lock(g_backend->ctx, g_report_life_mtx);
    }
}

static void sess_report_lifecycle_unlock(void)
{
    if (g_backend != NULL && g_backend->mutex_unlock != NULL &&
        g_report_life_mtx != NULL) {
        g_backend->mutex_unlock(g_backend->ctx, g_report_life_mtx);
    }
}

static void sess_report_unbind_locked(void)
{
    if (g_report_bound == AICAM_TRUE) {
        (void)app_report_service_set_session(NULL);
        g_report_bound = AICAM_FALSE;
    }
    memset(&g_report_session, 0, sizeof(g_report_session));
}

static void sess_report_shutdown(void)
{
    sess_report_lifecycle_lock();
    sess_report_unbind_locked();
    if (g_report_worker_started == AICAM_TRUE &&
        app_report_service_device_stop() == AICAM_OK) {
        g_report_worker_started = AICAM_FALSE;
    }
    sess_report_lifecycle_unlock();
}

static aicam_bool_t sess_report_allowed(app_report_session_t *out)
{
    aicam_bool_t allowed;

    if (out == NULL) {
        return AICAM_FALSE;
    }
    sess_lock();
    sess_report_lifecycle_lock();
    allowed = (aicam_bool_t)(g_state == APP_SESSION_STATE_RUNNING &&
                             g_stop_requested != AICAM_TRUE &&
                             g_is_v3 == AICAM_TRUE &&
                             (g_v3_caps & APP_SESSION_CAP_REPORT_SUBMIT) != 0u &&
                             g_report_bound == AICAM_TRUE &&
                             g_report_worker_started == AICAM_TRUE &&
                             g_report_session.active == AICAM_TRUE);
    if (allowed == AICAM_TRUE) {
        *out = g_report_session;
    }
    sess_report_lifecycle_unlock();
    sess_unlock();
    if (allowed != AICAM_TRUE ||
        app_report_service_device_running() != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static uint32_t sess_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t sess_float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static aicam_bool_t sess_bits_finite(uint32_t bits)
{
    return ((bits & 0x7F800000u) != 0x7F800000u) ? AICAM_TRUE : AICAM_FALSE;
}

static void sess_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void sess_lock(void)
{
    if (g_backend != NULL && g_backend->mutex_lock != NULL) {
        g_backend->mutex_lock(g_backend->ctx, g_mtx);
    }
}

static void sess_unlock(void)
{
    if (g_backend != NULL && g_backend->mutex_unlock != NULL) {
        g_backend->mutex_unlock(g_backend->ctx, g_mtx);
    }
}

static void sess_wake(void)
{
    if (g_backend != NULL && g_backend->event_set != NULL) {
        g_backend->event_set(g_backend->ctx, g_evt);
    }
}

static void sess_log_append(const char *text)
{
    size_t len;
    size_t room;

    if (text == NULL) {
        return;
    }
    len = strlen(text);
    if (len >= APP_SESSION_LOG_CAP - g_log_len) {
        room = APP_SESSION_LOG_CAP - g_log_len - 1u;
        if (g_log_len < APP_SESSION_LOG_CAP) {
            memcpy(g_log + g_log_len, text, room);
            g_log_len = APP_SESSION_LOG_CAP - 1u;
        }
        g_log_truncated = AICAM_TRUE;
        return;
    }
    memcpy(g_log + g_log_len, text, len);
    g_log_len += len;
}

size_t app_host_session_take_log(char *buf, size_t cap)
{
    size_t copied;

    if (buf == NULL || cap == 0u) {
        return 0u;
    }
    sess_lock();
    copied = g_log_len;
    if (copied > cap - 1u) {
        copied = cap - 1u;
    }
    memcpy(buf, g_log, copied);
    buf[copied] = '\0';
    g_log_len = 0;
    g_log_truncated = AICAM_FALSE;
    sess_unlock();
    return copied;
}

aicam_bool_t app_host_session_log_truncated(void)
{
    return g_log_truncated;
}

static void sess_set_last(app_session_result_t result, const char *reason)
{
    g_last_result = result;
    snprintf(g_last_reason, sizeof(g_last_reason), "%s",
             (reason != NULL) ? reason : "");
}

static void sess_try_reap_locked(void)
{
    if (g_exit_latch != AICAM_TRUE || g_backend == NULL ||
        g_backend->entry_exited == NULL) {
        return;
    }
    if (g_backend->entry_exited(g_backend->ctx) != AICAM_TRUE) {
        return;
    }
    g_exit_latch = AICAM_FALSE;
    app_install_set_app_running(AICAM_FALSE);
    g_state = APP_SESSION_STATE_IDLE;
    sess_wake();
}

static aicam_bool_t sess_utf8_valid(const uint8_t *data, size_t len)
{
    size_t i = 0;

    while (i < len) {
        uint8_t c = data[i];
        uint32_t cp;
        uint32_t extra;
        uint32_t min;

        if (c < 0x80u) {
            i++;
            continue;
        }
        if ((c & 0xE0u) == 0xC0u) {
            cp = (uint32_t)(c & 0x1Fu);
            extra = 1u;
            min = 0x80u;
        } else if ((c & 0xF0u) == 0xE0u) {
            cp = (uint32_t)(c & 0x0Fu);
            extra = 2u;
            min = 0x800u;
        } else if ((c & 0xF8u) == 0xF0u) {
            cp = (uint32_t)(c & 0x07u);
            extra = 3u;
            min = 0x10000u;
        } else {
            return AICAM_FALSE;
        }
        if (i + (size_t)extra >= len) {
            return AICAM_FALSE;
        }
        i++;
        while (extra > 0u) {
            uint8_t cc = data[i];
            if ((cc & 0xC0u) != 0x80u) {
                return AICAM_FALSE;
            }
            cp = (cp << 6) | (uint32_t)(cc & 0x3Fu);
            i++;
            extra--;
        }
        if (cp > 0x10FFFFu) {
            return AICAM_FALSE;
        }
        if (cp >= 0xD800u && cp <= 0xDFFFu) {
            return AICAM_FALSE;
        }
        if (cp < min) {
            return AICAM_FALSE;
        }
    }
    return AICAM_TRUE;
}

static aicam_bool_t sess_slot_text_valid(const char *text, size_t slot)
{
    const uint8_t *field = (const uint8_t *)text;
    size_t text_len = 0;
    size_t i;

    if (field == NULL) {
        return AICAM_FALSE;
    }
    while (text_len < slot && field[text_len] != 0x00u) {
        text_len++;
    }
    if (text_len == 0u || text_len >= slot) {
        return AICAM_FALSE;
    }
    for (i = text_len; i < slot; i++) {
        if (field[i] != 0x00u) {
            return AICAM_FALSE;
        }
    }
    return sess_utf8_valid(field, text_len);
}

static aicam_bool_t sess_model_view_canonical(const app_session_model_view_t *view)
{
    uint32_t i;
    uint32_t j;

    if (view == NULL) {
        return AICAM_FALSE;
    }
    if (!sess_slot_text_valid(view->model_name, APP_SESSION_MODEL_NAME_SLOT) ||
        !sess_slot_text_valid(view->model_version, APP_SESSION_MODEL_VERSION_SLOT)) {
        return AICAM_FALSE;
    }
    if (view->class_count == 0u || view->class_count > APP_SESSION_MAX_CLASSES) {
        return AICAM_FALSE;
    }
    for (i = 0u; i < view->class_count; i++) {
        if (!sess_slot_text_valid(view->class_names[i], APP_SESSION_CLASS_SLOT)) {
            return AICAM_FALSE;
        }
        for (j = 0u; j < i; j++) {
            if (strcmp(view->class_names[i], view->class_names[j]) == 0) {
                return AICAM_FALSE;
            }
        }
    }
    return AICAM_TRUE;
}

static void sess_add_pending_lost_locked(uint32_t count)
{
    uint64_t total = (uint64_t)g_pending_lost + (uint64_t)count;

    if (total > 0xFFFFFFFFu) {
        g_pending_lost_unknown = AICAM_TRUE;
        g_pending_lost = APP_SESSION_EVT_LOST_UNKNOWN_MARK;
    } else {
        g_pending_lost = (uint32_t)total;
    }
}

static void sess_account_lost_locked(uint32_t count)
{
    g_frames_dropped += count;
    sess_add_pending_lost_locked(count);
}

static void sess_ring_drop_head_locked(void)
{
    const uint8_t *wire = g_ring[g_ring_head].wire;
    uint32_t kind = sess_le32(wire + 4);
    uint32_t flags = sess_le32(wire + 24);
    uint32_t lost = sess_le32(wire + 32);
    uint32_t frames = (kind == APP_SESSION_EVT_KIND_FRAME) ? 1u : 0u;

    if ((flags & APP_SESSION_EVT_FLAG_LOST_UNKNOWN) != 0u) {
        g_pending_lost_unknown = AICAM_TRUE;
    }
    sess_add_pending_lost_locked(lost);
    sess_add_pending_lost_locked(frames);
    g_frames_dropped += frames;
    g_ring_head = (g_ring_head + 1u) % APP_SESSION_RING_SLOTS;
    g_ring_count--;
}

static void sess_take_gap_flags_locked(uint32_t *flags, uint32_t *lost)
{
    if (g_pending_lost_unknown) {
        *flags = APP_SESSION_EVT_FLAG_LOST_KNOWN | APP_SESSION_EVT_FLAG_LOST_UNKNOWN;
        *lost = APP_SESSION_EVT_LOST_UNKNOWN_MARK;
    } else if (g_pending_lost > 0u) {
        *flags = APP_SESSION_EVT_FLAG_LOST_KNOWN;
        *lost = g_pending_lost;
    } else {
        *flags = 0u;
        *lost = 0u;
    }
    g_pending_lost = 0u;
    g_pending_lost_unknown = AICAM_FALSE;
}

static void sess_ring_flush_locked(void)
{
    while (g_ring_count > 0u) {
        sess_ring_drop_head_locked();
    }
    g_ring_head = 0u;
    g_ring_count = 0u;
}

static void sess_ring_push_locked(uint8_t *wire, uint32_t len)
{
    uint32_t pos;
    uint32_t flags;
    uint32_t lost;

    if (g_ring_count >= APP_SESSION_RING_SLOTS) {
        sess_ring_drop_head_locked();
        sess_take_gap_flags_locked(&flags, &lost);
        sess_put_u32(wire + 24, flags);
        sess_put_u32(wire + 32, lost);
    }
    pos = (g_ring_head + g_ring_count) % APP_SESSION_RING_SLOTS;
    memcpy(g_ring[pos].wire, wire, len);
    g_ring[pos].len = len;
    g_ring_count++;
}

static aicam_bool_t sess_refresh_model_locked(void)
{
    app_session_model_view_t view;

    if (g_backend == NULL || g_backend->ai_model_view == NULL) {
        return AICAM_FALSE;
    }
    if (g_backend->ai_model_view(g_backend->ctx, &view) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    if (!sess_model_view_canonical(&view)) {
        return AICAM_FALSE;
    }
    g_model = view;
    g_model_generation++;
    g_class_generation++;
    g_model_changes++;
    return AICAM_TRUE;
}

static int32_t sess_api_log(const char *text)
{
    sess_lock();
    sess_log_append(text);
    sess_unlock();
    if (g_backend != NULL && g_backend->log_sink != NULL) {
        g_backend->log_sink(g_backend->ctx, text);
    }
    return 0;
}

static int32_t sess_api_tick_ms(void)
{
    if (g_backend != NULL && g_backend->tick_ms != NULL) {
        return (int32_t)g_backend->tick_ms(g_backend->ctx);
    }
    return 0;
}

static int32_t sess_api_event_next(void *out, uint32_t cap,
                                   uint32_t *actual_len, uint32_t max_wait_ms)
{
    uint32_t wait_left;
    uint32_t deadline;
    uint32_t now;
    uint32_t copied;
    const session_slot_t *head;

    if (out == NULL || actual_len == NULL || cap == 0u) {
        return -1;
    }
    if (max_wait_ms > APP_SESSION_MAX_WAIT_CLAMP_MS) {
        max_wait_ms = APP_SESSION_MAX_WAIT_CLAMP_MS;
    }

    sess_lock();
    if (g_state != APP_SESSION_STATE_RUNNING && g_state != APP_SESSION_STATE_STOPPING) {
        sess_unlock();
        return -3;
    }
    if (g_state == APP_SESSION_STATE_STOPPING || g_stop_requested == AICAM_TRUE) {
        sess_unlock();
        return -10;
    }
    if (g_model_incompatible) {
        sess_unlock();
        return -4;
    }

    if (g_backend != NULL && g_backend->tick_ms != NULL) {
        now = g_backend->tick_ms(g_backend->ctx);
    } else {
        now = 0u;
    }
    deadline = now + max_wait_ms;
    wait_left = max_wait_ms;

    for (;;) {
        if (g_ring_count > 0u) {
            head = &g_ring[g_ring_head];
            if (head->len > cap) {
                sess_unlock();
                return -2;
            }
            copied = head->len;
            memcpy(out, head->wire, copied);
            *actual_len = copied;
            g_ring_head = (g_ring_head + 1u) % APP_SESSION_RING_SLOTS;
            g_ring_count--;
            g_events_delivered++;
            sess_unlock();
            return 0;
        }

        if (wait_left == 0u) {
            sess_unlock();
            return 1;
        }

        sess_unlock();
        {
            uint32_t slice = wait_left;

            if (slice > 100u) {
                slice = 100u;
            }
            if (g_backend != NULL && g_backend->event_wait != NULL) {
                (void)g_backend->event_wait(g_backend->ctx, g_evt, slice);
            }
        }
        sess_lock();

        if (g_state != APP_SESSION_STATE_RUNNING && g_state != APP_SESSION_STATE_STOPPING) {
            sess_unlock();
            return -3;
        }
        if (g_state == APP_SESSION_STATE_STOPPING || g_stop_requested == AICAM_TRUE) {
            sess_unlock();
            return -10;
        }
        if (g_model_incompatible) {
            sess_unlock();
            return -4;
        }

        if (g_backend != NULL && g_backend->tick_ms != NULL) {
            now = g_backend->tick_ms(g_backend->ctx);
        } else {
            now = deadline;
        }
        wait_left = (uint32_t)(deadline - now);
        if (wait_left > max_wait_ms) {
            wait_left = 0u;
        }
    }
}

static int32_t sess_api_model_meta(void *out, uint32_t cap, uint32_t *actual_len)
{
    uint8_t meta[APP_SESSION_META_SIZE];
    uint32_t text_len;

    if (out == NULL || actual_len == NULL) {
        return -1;
    }

    sess_lock();
    if (g_state != APP_SESSION_STATE_RUNNING && g_state != APP_SESSION_STATE_STOPPING) {
        sess_unlock();
        return -3;
    }
    if (g_model_incompatible) {
        sess_unlock();
        return -4;
    }

    memset(meta, 0, sizeof(meta));
    sess_put_u32(meta + 0, 1u);
    sess_put_u32(meta + 4, 1u);
    sess_put_u32(meta + 8, g_model_generation);
    sess_put_u32(meta + 12, g_class_generation);
    sess_put_u32(meta + 16, g_model.class_count);
    sess_put_u32(meta + 20, 0u);

    text_len = (uint32_t)strlen(g_model.model_name);
    memcpy(meta + 24, g_model.model_name, text_len);
    text_len = (uint32_t)strlen(g_model.model_version);
    memcpy(meta + 24 + APP_SESSION_MODEL_NAME_SLOT, g_model.model_version, text_len);

    if (cap < (uint32_t)sizeof(meta)) {
        sess_unlock();
        return -2;
    }
    memcpy(out, meta, sizeof(meta));
    *actual_len = (uint32_t)sizeof(meta);
    sess_unlock();
    return 0;
}

static int32_t sess_api_class_name(uint32_t model_gen, uint32_t class_gen,
                                   uint32_t class_index, void *out_utf8,
                                   uint32_t cap, uint32_t *actual_len)
{
    uint32_t len;

    if (out_utf8 == NULL || actual_len == NULL) {
        return -1;
    }

    sess_lock();
    if (g_state != APP_SESSION_STATE_RUNNING && g_state != APP_SESSION_STATE_STOPPING) {
        sess_unlock();
        return -3;
    }
    if (g_model_incompatible) {
        sess_unlock();
        return -4;
    }
    if (model_gen != g_model_generation || class_gen != g_class_generation) {
        sess_unlock();
        return -4;
    }
    if (class_index >= g_model.class_count) {
        sess_unlock();
        return -1;
    }
    len = (uint32_t)strlen(g_model.class_names[class_index]);
    if (cap < len + 1u) {
        sess_unlock();
        return -2;
    }
    memcpy(out_utf8, g_model.class_names[class_index], len);
    *actual_len = len;
    sess_unlock();
    return 0;
}

static int32_t sess_api_report_submit(const void *json, uint32_t len,
                                      uint32_t app_report_seq,
                                      uint32_t *out_host_boot_id)
{
    app_report_session_t session;

    if (out_host_boot_id != NULL) {
        *out_host_boot_id = 0u;
    }
    if (sess_report_allowed(&session) != AICAM_TRUE) {
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    return app_report_submit(&session, json, len, app_report_seq,
                             out_host_boot_id);
}

static int32_t sess_api_report_status(uint32_t host_boot_id, uint32_t report_seq,
                                      uint32_t *out_mqtt_state,
                                      uint32_t *out_webhook_state)
{
    app_report_session_t session;

    if (out_mqtt_state != NULL) {
        *out_mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    }
    if (out_webhook_state != NULL) {
        *out_webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;
    }
    if (sess_report_allowed(&session) != AICAM_TRUE) {
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    return app_report_status(host_boot_id, report_seq, out_mqtt_state,
                             out_webhook_state);
}

static int32_t sess_api_state_read(void *out, uint32_t cap,
                                   uint32_t *actual_len, uint32_t *out_revision)
{
    aicam_bool_t allowed;
    int32_t rc;

    sess_lock();
    allowed = (aicam_bool_t)(g_is_v3 == AICAM_TRUE && g_state_bit == AICAM_TRUE &&
                             g_state_bound == AICAM_TRUE &&
                             (g_state == APP_SESSION_STATE_RUNNING ||
                              g_state == APP_SESSION_STATE_STOPPING));
    sess_unlock();
    if (allowed != AICAM_TRUE) {
        return -3;
    }
    rc = app_state_read(&g_state_session, out, cap, actual_len, out_revision);
    if (rc == APP_STATE_OK && out != NULL && actual_len != NULL &&
        out_revision != NULL) {
        uint8_t sha[APP_INSTALL_SHA256_LEN];
        if (mbedtls_sha256((const uint8_t *)out, *actual_len, sha, 0) == 0) {
            sess_enable_lock();
            if (g_enable_witness.active == AICAM_TRUE &&
                g_enable_witness.committed == AICAM_TRUE &&
                out_revision[0] == g_enable_witness.committed_revision &&
                actual_len[0] == g_enable_witness.blob_len &&
                memcmp(sha, g_enable_witness.blob_sha256, sizeof(sha)) == 0) {
                g_enable_witness.readback = AICAM_TRUE;
            }
            sess_enable_unlock();
        }
    }
    sess_lock();
    allowed = (aicam_bool_t)(g_is_v3 == AICAM_TRUE &&
                             (g_state == APP_SESSION_STATE_RUNNING ||
                              g_state == APP_SESSION_STATE_STOPPING));
    sess_unlock();
    if (allowed != AICAM_TRUE) {
        return -3;
    }
    return rc;
}

static int32_t sess_api_state_commit(const void *blob, uint32_t len,
                                     uint32_t expected_revision,
                                     uint32_t *new_revision)
{
    aicam_bool_t allowed;
    int32_t rc;

    sess_lock();
    allowed = (aicam_bool_t)(g_is_v3 == AICAM_TRUE && g_state_bit == AICAM_TRUE &&
                             g_state_bound == AICAM_TRUE &&
                             (g_state == APP_SESSION_STATE_RUNNING ||
                              g_state == APP_SESSION_STATE_STOPPING));
    sess_unlock();
    if (allowed != AICAM_TRUE) {
        return -3;
    }
    rc = app_state_commit(&g_state_session, blob, len, expected_revision,
                          new_revision);
    if (rc == APP_STATE_OK && blob != NULL && new_revision != NULL) {
        uint8_t sha[APP_INSTALL_SHA256_LEN];
        if (mbedtls_sha256((const uint8_t *)blob, len, sha, 0) == 0) {
            sess_enable_lock();
            if (g_enable_witness.active == AICAM_TRUE &&
                expected_revision == g_enable_witness.expected_revision &&
                new_revision[0] == expected_revision + 1u) {
                g_enable_witness.committed = AICAM_TRUE;
                g_enable_witness.readback = AICAM_FALSE;
                g_enable_witness.committed_revision = *new_revision;
                g_enable_witness.blob_len = len;
                memcpy(g_enable_witness.blob_sha256, sha, sizeof(sha));
            }
            sess_enable_unlock();
        }
    }
    sess_lock();
    allowed = (aicam_bool_t)(g_is_v3 == AICAM_TRUE &&
                             (g_state == APP_SESSION_STATE_RUNNING ||
                              g_state == APP_SESSION_STATE_STOPPING));
    sess_unlock();
    if (allowed != AICAM_TRUE) {
        return -3;
    }
    return rc;
}

static int32_t sess_ctl_enable_precommit(void *ctx, uint32_t session_generation,
                                         uint32_t request_id,
                                         uint32_t config_generation,
                                         uint32_t expected_state_revision,
                                         uint32_t desired_state)
{
    uint32_t actual_len = 0u;
    uint32_t revision = 0u;
    int32_t state_rc;

    (void)ctx;
    if (session_generation == 0u || request_id == 0u || config_generation == 0u ||
        g_container_major != 5u ||
        (g_v3_caps & (APP_ADMISSION_V4_CAP_ENABLE_CONTROL |
                      APP_ADMISSION_V2_CAP_APP_STATE |
                      APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
                      APP_ADMISSION_V3_CAP_CONTROL_RECEIVE)) !=
            (APP_ADMISSION_V4_CAP_ENABLE_CONTROL |
             APP_ADMISSION_V2_CAP_APP_STATE |
             APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
             APP_ADMISSION_V3_CAP_CONTROL_RECEIVE) ||
        g_state_bound != AICAM_TRUE || g_enable_io == NULL ||
        g_enable_boot_id == 0u || g_enable_mtx == NULL || g_backend == NULL ||
        g_backend->mutex_lock == NULL || g_backend->mutex_unlock == NULL ||
        (desired_state != APP_CONTROL_ENABLE_STATE_ENABLED &&
         desired_state != APP_CONTROL_ENABLE_STATE_PAUSED)) {
        return APP_CONTROL_UNAUTHORIZED;
    }
    state_rc = app_state_read(&g_state_session, NULL, 0u, &actual_len, &revision);
    if (expected_state_revision == 0u) {
        if (state_rc == APP_STATE_NOT_FOUND && revision == 0u) {
            state_rc = APP_STATE_OK;
        } else if (revision != 0u || state_rc == APP_STATE_OK ||
                   state_rc == APP_STATE_BUFFER_TOO_SMALL) {
            return APP_CONTROL_ERR_CONFLICT;
        } else {
            return APP_CONTROL_ERR_STORAGE_GUARD;
        }
    } else if (revision != expected_state_revision) {
        return APP_CONTROL_ERR_CONFLICT;
    } else if (state_rc != APP_STATE_BUFFER_TOO_SMALL && state_rc != APP_STATE_OK) {
        return APP_CONTROL_ERR_STORAGE_GUARD;
    }
    if (desired_state == APP_CONTROL_ENABLE_STATE_PAUSED) {
        if (app_native_vnext_floor_raise(g_enable_io, &g_record,
                                         session_generation, g_enable_boot_id,
                                         request_id, config_generation,
                                         expected_state_revision) != APP_INSTALL_OK ||
            app_native_vnext_floor_verify(g_enable_io, &g_record,
                                          session_generation, g_enable_boot_id,
                                          request_id, config_generation,
                                          expected_state_revision) != APP_INSTALL_OK) {
            return APP_CONTROL_ERR_STORAGE_GUARD;
        }
    }
    sess_enable_lock();
    memset(&g_enable_witness, 0, sizeof(g_enable_witness));
    g_enable_witness.active = AICAM_TRUE;
    g_enable_witness.session_generation = session_generation;
    g_enable_witness.request_id = request_id;
    g_enable_witness.config_generation = config_generation;
    g_enable_witness.expected_revision = expected_state_revision;
    g_enable_witness.desired_state = desired_state;
    sess_enable_unlock();
    return APP_CONTROL_OK;
}

static int32_t sess_ctl_enable_receipt_check(void *ctx,
                                            uint32_t session_generation,
                                            uint32_t request_id,
                                            uint32_t config_generation,
                                            uint32_t state_revision,
                                            uint32_t desired_state)
{
    sess_enable_witness_t witness;
    uint8_t *blob;
    uint32_t actual_len = 0u;
    uint32_t revision = 0u;
    uint8_t sha[APP_INSTALL_SHA256_LEN];
    int32_t rc;

    (void)ctx;
    sess_enable_lock();
    memcpy(&witness, &g_enable_witness, sizeof(witness));
    sess_enable_unlock();
    if (witness.active != AICAM_TRUE || witness.committed != AICAM_TRUE ||
        witness.readback != AICAM_TRUE ||
        witness.session_generation != session_generation ||
        witness.request_id != request_id ||
        witness.config_generation != config_generation ||
        witness.desired_state != desired_state ||
        witness.committed_revision != state_revision ||
        state_revision <= witness.expected_revision ||
        witness.blob_len == 0u || witness.blob_len > g_state_session.quota) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    blob = (uint8_t *)malloc(witness.blob_len);
    if (blob == NULL) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    rc = app_state_read(&g_state_session, blob, witness.blob_len,
                        &actual_len, &revision);
    if (rc == APP_STATE_OK && actual_len == witness.blob_len &&
        revision == state_revision) {
        if (mbedtls_sha256(blob, actual_len, sha, 0) != 0 ||
            memcmp(sha, witness.blob_sha256, sizeof(sha)) != 0) {
            rc = APP_STATE_STORAGE_UNKNOWN;
        }
    } else {
        rc = APP_STATE_STORAGE_UNKNOWN;
    }
    free(blob);
    if (rc != APP_STATE_OK) {
        return APP_CONTROL_ERR_STORAGE_GUARD;
    }
    if (desired_state == APP_CONTROL_ENABLE_STATE_PAUSED &&
        app_native_vnext_floor_verify(g_enable_io, &g_record,
                                      session_generation, g_enable_boot_id,
                                      request_id, config_generation,
                                      witness.expected_revision) != APP_INSTALL_OK) {
        return APP_CONTROL_ERR_STORAGE_GUARD;
    }
    return APP_CONTROL_OK;
}

static int32_t sess_api_should_stop(void)
{
    int32_t rc;

    sess_lock();
    if (g_state != APP_SESSION_STATE_RUNNING && g_state != APP_SESSION_STATE_STOPPING) {
        rc = -3;
    } else if (g_stop_requested == AICAM_TRUE ||
               g_state == APP_SESSION_STATE_STOPPING) {
        rc = 1;
    } else {
        rc = 0;
    }
    sess_unlock();
    return rc;
}

static uint32_t sess_ctl_now_ms(void *ctx)
{
    (void)ctx;
    if (g_backend != NULL && g_backend->tick_ms != NULL) {
        return g_backend->tick_ms(g_backend->ctx);
    }
    return 0u;
}

static void *sess_ctl_mutex_create(void *ctx)
{
    (void)ctx;
    if (g_backend != NULL && g_backend->mutex_create != NULL) {
        return g_backend->mutex_create(g_backend->ctx);
    }
    return NULL;
}

static void sess_ctl_mutex_destroy(void *ctx, void *mtx)
{
    (void)ctx;
    if (g_backend != NULL && g_backend->mutex_destroy != NULL) {
        g_backend->mutex_destroy(g_backend->ctx, mtx);
    }
}

static int sess_ctl_mutex_lock(void *ctx, void *mtx)
{
    (void)ctx;
    if (g_backend != NULL && g_backend->mutex_lock != NULL) {
        return g_backend->mutex_lock(g_backend->ctx, mtx);
    }
    return 0;
}

static void sess_ctl_mutex_unlock(void *ctx, void *mtx)
{
    (void)ctx;
    if (g_backend != NULL && g_backend->mutex_unlock != NULL) {
        g_backend->mutex_unlock(g_backend->ctx, mtx);
    }
}

static int32_t sess_api_control_next(void *out, uint32_t cap, uint32_t *actual_len)
{
    aicam_bool_t allowed;
    int32_t rc;

    if (out == NULL || actual_len == NULL || cap == 0u) {
        return -1;
    }
    sess_lock();
    allowed = (aicam_bool_t)(g_is_v3 == AICAM_TRUE &&
                             g_state == APP_SESSION_STATE_RUNNING &&
                             g_stop_requested != AICAM_TRUE);
    sess_unlock();
    if (allowed != AICAM_TRUE) {
        return -3;
    }
    rc = app_host_control_deliver(out, cap, actual_len);
    return rc;
}

static int32_t sess_api_control_reply(const void *buf, uint32_t len)
{
    aicam_bool_t allowed;

    sess_lock();
    allowed = (aicam_bool_t)(g_is_v3 == AICAM_TRUE &&
                             g_state == APP_SESSION_STATE_RUNNING &&
                             g_stop_requested != AICAM_TRUE);
    sess_unlock();
    if (allowed != AICAM_TRUE) {
        return -3;
    }
    return app_host_control_receipt(buf, len);
}

static int32_t sess_api_snapshot_publish(const void *buf, uint32_t len)
{
    aicam_bool_t allowed;

    sess_lock();
    allowed = (aicam_bool_t)(g_is_v3 == AICAM_TRUE &&
                             g_state == APP_SESSION_STATE_RUNNING &&
                             g_stop_requested != AICAM_TRUE);
    sess_unlock();
    if (allowed != AICAM_TRUE) {
        return -3;
    }
    return app_host_control_snapshot_submit(buf, len);
}

static uint32_t sess_live_now_ms(void *ctx)
{
    (void)ctx;
    if (g_backend != NULL && g_backend->tick_ms != NULL) {
        return g_backend->tick_ms(g_backend->ctx);
    }
    return 0u;
}

static void *sess_live_mutex_create(void *ctx)
{
    if (g_backend != NULL && g_backend->mutex_create != NULL) {
        return g_backend->mutex_create(ctx);
    }
    return NULL;
}

static void sess_live_mutex_destroy(void *ctx, void *mtx)
{
    if (g_backend != NULL && g_backend->mutex_destroy != NULL) {
        g_backend->mutex_destroy(ctx, mtx);
    }
}

static int sess_live_mutex_lock(void *ctx, void *mtx)
{
    if (g_backend != NULL && g_backend->mutex_lock != NULL) {
        return g_backend->mutex_lock(ctx, mtx);
    }
    return 0;
}

static void sess_live_mutex_unlock(void *ctx, void *mtx)
{
    if (g_backend != NULL && g_backend->mutex_unlock != NULL) {
        g_backend->mutex_unlock(ctx, mtx);
    }
}

static void sess_live_backend_init(void)
{
    app_host_live_backend_t live_backend;

    memset(&live_backend, 0, sizeof(live_backend));
    live_backend.ctx = (g_backend != NULL) ? g_backend->ctx : NULL;
    live_backend.now_ms = sess_live_now_ms;
    live_backend.mutex_create = sess_live_mutex_create;
    live_backend.mutex_destroy = sess_live_mutex_destroy;
    live_backend.mutex_lock = sess_live_mutex_lock;
    live_backend.mutex_unlock = sess_live_mutex_unlock;
    app_host_live_init(&live_backend);
}

static int32_t sess_api_live_publish(const void *buf, uint32_t len)
{
    aicam_bool_t allowed;
    uint32_t generation;
    uint32_t class_count;

    sess_lock();
    allowed = (aicam_bool_t)(g_container_major == 5u &&
                             (g_v3_caps & APP_ADMISSION_V4_CAP_LIVE_PUBLISH) != 0u &&
                             g_state == APP_SESSION_STATE_RUNNING &&
                             g_stop_requested != AICAM_TRUE);
    generation = g_session_generation;
    class_count = g_model.class_count;
    sess_unlock();
    if (allowed != AICAM_TRUE) {
        return APP_SESSION_ERR_UNAVAILABLE;
    }
    return (int32_t)app_host_live_ingest(buf, len, generation, class_count);
}

void app_host_session_init(const app_host_session_backend_t *backend)
{
    app_control_backend_t ctl_backend;

    sess_report_shutdown();
    if (g_backend != NULL && g_backend->mutex_destroy != NULL &&
        g_enable_mtx != NULL) {
        g_backend->mutex_destroy(g_backend->ctx, g_enable_mtx);
        g_enable_mtx = NULL;
    }
    g_backend = backend;
    g_mtx = (backend != NULL && backend->mutex_create != NULL)
                ? backend->mutex_create(backend->ctx) : NULL;
    g_report_life_mtx = (backend != NULL && backend->mutex_create != NULL)
                            ? backend->mutex_create(backend->ctx) : NULL;
    g_enable_mtx = (backend != NULL && backend->mutex_create != NULL)
                       ? backend->mutex_create(backend->ctx) : NULL;
    g_evt = (backend != NULL && backend->event_create != NULL)
                ? backend->event_create(backend->ctx) : NULL;
    g_state = APP_SESSION_STATE_IDLE;
    g_stop_requested = AICAM_FALSE;
    g_attached = AICAM_FALSE;
    g_model_incompatible = AICAM_FALSE;
    g_entry_alive = AICAM_FALSE;
    g_exit_latch = AICAM_FALSE;
    g_model_generation = 0u;
    g_class_generation = 0u;
    g_sequence = 0u;
    g_pending_lost = 0u;
    g_pending_lost_unknown = AICAM_FALSE;
    g_ring_head = 0u;
    g_ring_count = 0u;
    memset(&g_model, 0, sizeof(g_model));
    memset(&g_record, 0, sizeof(g_record));
    g_event_max = 0u;
    g_events_delivered = 0u;
    g_frames_dropped = 0u;
    g_gap_events = 0u;
    g_model_changes = 0u;
    g_last_result = APP_SESSION_OK;
    g_last_reason[0] = '\0';
    g_log_len = 0;
    g_log_truncated = AICAM_FALSE;
    memset(&g_image_info, 0, sizeof(g_image_info));
    g_is_v3 = AICAM_FALSE;
    g_v3_caps = 0u;
    g_container_major = 0u;
    g_session_generation = 0u;
    g_enable_boot_id = 0u;
    g_enable_io = NULL;
    memset(&g_enable_witness, 0, sizeof(g_enable_witness));
    g_state_bit = AICAM_FALSE;
    g_state_bound = AICAM_FALSE;
    memset(&g_state_session, 0, sizeof(g_state_session));
    sess_live_backend_init();
    sess_report_lifecycle_lock();
    g_report_bound = AICAM_FALSE;
    memset(&g_report_session, 0, sizeof(g_report_session));
    sess_report_lifecycle_unlock();

    g_table.table_size = (uint32_t)sizeof(g_table);
    g_table.abi_version = APP_SESSION_ABI_VERSION;
    g_table.log = sess_api_log;
    g_table.tick_ms = sess_api_tick_ms;
    g_table.event_next = sess_api_event_next;
    g_table.model_meta = sess_api_model_meta;
    g_table.class_name = sess_api_class_name;
    g_table.report_submit = sess_api_report_submit;
    g_table.report_status = sess_api_report_status;
    g_table.state_read = sess_api_state_read;
    g_table.state_commit = sess_api_state_commit;
    g_table.should_stop = sess_api_should_stop;

    g_table_v3.table_size = (uint32_t)sizeof(g_table_v3);
    g_table_v3.abi_version = APP_SESSION_ABI_VERSION_V3;
    g_table_v3.log = sess_api_log;
    g_table_v3.tick_ms = sess_api_tick_ms;
    g_table_v3.event_next = sess_api_event_next;
    g_table_v3.model_meta = sess_api_model_meta;
    g_table_v3.class_name = sess_api_class_name;
    g_table_v3.report_submit = sess_api_report_submit;
    g_table_v3.report_status = sess_api_report_status;
    g_table_v3.state_read = sess_api_state_read;
    g_table_v3.state_commit = sess_api_state_commit;
    g_table_v3.should_stop = sess_api_should_stop;
    g_table_v3.control_next = sess_api_control_next;
    g_table_v3.control_reply = sess_api_control_reply;
    g_table_v3.snapshot_publish = sess_api_snapshot_publish;

    g_table_v4.table_size = APP_SESSION_V4_ABI_TABLE_SIZE;
    g_table_v4.abi_version = APP_SESSION_ABI_VERSION_V4;
    g_table_v4.log = sess_api_log;
    g_table_v4.tick_ms = sess_api_tick_ms;
    g_table_v4.event_next = sess_api_event_next;
    g_table_v4.model_meta = sess_api_model_meta;
    g_table_v4.class_name = sess_api_class_name;
    g_table_v4.report_submit = sess_api_report_submit;
    g_table_v4.report_status = sess_api_report_status;
    g_table_v4.state_read = sess_api_state_read;
    g_table_v4.state_commit = sess_api_state_commit;
    g_table_v4.should_stop = sess_api_should_stop;
    g_table_v4.control_next = sess_api_control_next;
    g_table_v4.control_reply = sess_api_control_reply;
    g_table_v4.snapshot_publish = sess_api_snapshot_publish;
    g_table_v4.live_publish = sess_api_live_publish;

    memset(&ctl_backend, 0, sizeof(ctl_backend));
    ctl_backend.ctx = NULL;
    ctl_backend.now_ms = sess_ctl_now_ms;
    ctl_backend.mutex_create = sess_ctl_mutex_create;
    ctl_backend.mutex_destroy = sess_ctl_mutex_destroy;
    ctl_backend.mutex_lock = sess_ctl_mutex_lock;
    ctl_backend.mutex_unlock = sess_ctl_mutex_unlock;
    ctl_backend.enable_precommit = sess_ctl_enable_precommit;
    ctl_backend.enable_receipt_check = sess_ctl_enable_receipt_check;
    app_host_control_init(&ctl_backend);
}

const app_host_session_api_table_t *app_host_session_api_table(void)
{
    return &g_table;
}

const app_host_session_api_table_v3_t *app_host_session_api_table_v3(void)
{
    return &g_table_v3;
}

const app_host_session_api_table_v4_t *app_host_session_api_table_v4(void)
{
    return &g_table_v4;
}

aicam_bool_t app_host_session_stop_requested(void)
{
    return g_stop_requested;
}

void app_host_session_status(app_host_session_status_t *out)
{
    if (out == NULL) {
        return;
    }
    sess_lock();
    sess_try_reap_locked();
    out->state = g_state;
    out->app_running = app_install_app_running();
    memcpy(out->publisher_id, g_record.publisher_id, sizeof(out->publisher_id));
    memcpy(out->app_id, g_record.app_id, sizeof(out->app_id));
    out->version_major = g_record.version_major;
    out->version_minor = g_record.version_minor;
    out->version_patch = g_record.version_patch;
    out->abi_version = (g_container_major == 5u) ? APP_SESSION_ABI_VERSION_V4 :
                       (g_is_v3 == AICAM_TRUE) ? APP_SESSION_ABI_VERSION_V3 :
                                                 APP_SESSION_ABI_VERSION;
    out->granted_caps = (g_is_v3 == AICAM_TRUE) ? g_v3_caps : APP_SESSION_GRANTED_CAPS;
    out->event_max = g_event_max;
    out->model_generation = g_model_generation;
    out->class_generation = g_class_generation;
    out->class_count = g_model.class_count;
    memcpy(out->model_name, g_model.model_name, sizeof(out->model_name));
    memcpy(out->model_version, g_model.model_version, sizeof(out->model_version));
    out->events_delivered = g_events_delivered;
    out->frames_dropped = g_frames_dropped;
    out->gap_events = g_gap_events;
    out->model_changes = g_model_changes;
    out->last_result = g_last_result;
    memcpy(out->last_reason, g_last_reason, sizeof(out->last_reason));
    sess_unlock();
    {
        app_control_status_t ctl;

        memset(&ctl, 0, sizeof(ctl));
        app_host_control_status(&ctl);
        sess_lock();
        out->session_generation = ctl.session_active ? ctl.session_generation : 0u;
        out->control_max = ctl.session_active ? ctl.control_max : 0u;
        out->snapshot_max = ctl.session_active ? ctl.snapshot_max : 0u;
        sess_unlock();
    }
}

static void sess_session_teardown(app_session_result_t result, const char *reason,
                                  aicam_bool_t worker_exit)
{
    if (g_attached == AICAM_TRUE) {
        g_attached = AICAM_FALSE;
        if (g_backend != NULL && g_backend->ai_detach != NULL) {
            g_backend->ai_detach(g_backend->ctx);
        }
    }
    sess_lock();
    if (g_state != APP_SESSION_STATE_IDLE) {
        g_state = APP_SESSION_STATE_STOPPING;
    }
    g_stop_requested = AICAM_FALSE;
    g_model_incompatible = AICAM_FALSE;
    g_entry_alive = AICAM_FALSE;
    sess_ring_flush_locked();
    g_pending_lost = 0u;
    g_pending_lost_unknown = AICAM_FALSE;
    sess_set_last(result, reason);
    g_exit_latch = AICAM_FALSE;
    g_is_v3 = AICAM_FALSE;
    g_v3_caps = 0u;
    g_container_major = 0u;
    g_state_bit = AICAM_FALSE;
    sess_unlock();
    app_host_live_end(g_session_generation);
    g_session_generation = 0u;
    app_host_control_session_close();
    sess_enable_lock();
    memset(&g_enable_witness, 0, sizeof(g_enable_witness));
    sess_enable_unlock();
    g_enable_io = NULL;
    g_enable_boot_id = 0u;
    if (g_state_bound == AICAM_TRUE) {
        app_state_session_end(&g_state_session);
        g_state_bound = AICAM_FALSE;
    }
    sess_report_shutdown();
    if (g_backend != NULL && g_backend->scrub != NULL) {
        g_backend->scrub(g_backend->ctx);
    }
    sess_lock();
    if (worker_exit == AICAM_TRUE) {
        g_exit_latch = AICAM_TRUE;
    } else {
        app_install_set_app_running(AICAM_FALSE);
        g_stop_requested = AICAM_FALSE;
        g_state = APP_SESSION_STATE_IDLE;
        g_exit_latch = AICAM_FALSE;
    }
    sess_unlock();
    sess_wake();
}

static void sess_entry_thread(void *arg)
{
    int raw = 0;
    int rc;
    uint32_t abi_version;
    const void *api;
    app_session_result_t result;
    char reason[APP_SESSION_REASON_SLOT];

    (void)arg;
    rc = -1;
    if (g_backend != NULL && g_backend->invoke_entry != NULL) {
        sess_lock();
        abi_version = (g_container_major == 5u) ? APP_SESSION_ABI_VERSION_V4 :
                      (g_is_v3 == AICAM_TRUE) ? APP_SESSION_ABI_VERSION_V3 :
                                                APP_SESSION_ABI_VERSION;
        api = (g_container_major == 5u) ? (const void *)&g_table_v4 :
              (g_is_v3 == AICAM_TRUE) ? (const void *)&g_table_v3 :
                                        (const void *)&g_table;
        sess_unlock();
        rc = g_backend->invoke_entry(g_backend->ctx, api, abi_version,
                                     &g_image_info, &raw);
    }
    if (rc != 0) {
        result = APP_SESSION_ERR_RUNTIME_UNAVAILABLE;
        snprintf(reason, sizeof(reason), "%s", "RUNTIME_UNAVAILABLE");
    } else {
        result = APP_SESSION_OK;
        snprintf(reason, sizeof(reason), "%s", "entry_returned");
    }
    sess_session_teardown(result, reason, AICAM_TRUE);
}

static void sess_encode_control_event(uint8_t *wire, uint32_t kind,
                                      uint32_t monotonic_ms)
{
    uint32_t flags;
    uint32_t lost;

    sess_take_gap_flags_locked(&flags, &lost);
    memset(wire, 0, APP_SESSION_EVENT_HEADER_SIZE);
    sess_put_u32(wire + 0, APP_SESSION_EVENT_HEADER_SIZE);
    sess_put_u32(wire + 4, kind);
    sess_put_u32(wire + 8, g_sequence);
    g_sequence++;
    sess_put_u32(wire + 12, monotonic_ms);
    sess_put_u32(wire + 16, g_model_generation);
    sess_put_u32(wire + 20, g_class_generation);
    sess_put_u32(wire + 24, flags);
    sess_put_u32(wire + 28, 0u);
    sess_put_u32(wire + 32, lost);
    sess_put_u32(wire + 36, 0u);
}

static void sess_produce_gap_locked(uint32_t monotonic_ms)
{
    uint8_t wire[APP_SESSION_EVENT_HEADER_SIZE];

    if (g_ring_count > 0u) {
        return;
    }
    if (g_pending_lost == 0u && g_pending_lost_unknown == AICAM_FALSE) {
        return;
    }
    sess_encode_control_event(wire, APP_SESSION_EVT_KIND_GAP, monotonic_ms);
    sess_ring_push_locked(wire, APP_SESSION_EVENT_HEADER_SIZE);
    g_gap_events++;
}

static void sess_on_model_incompatible_locked(uint32_t monotonic_ms)
{
    uint8_t wire[APP_SESSION_EVENT_HEADER_SIZE];

    sess_ring_flush_locked();
    g_model_incompatible = AICAM_TRUE;
    g_model_changes++;
    if (g_pending_lost > 0u || g_pending_lost_unknown == AICAM_TRUE) {
        sess_encode_control_event(wire, APP_SESSION_EVT_KIND_GAP, monotonic_ms);
        sess_ring_push_locked(wire, APP_SESSION_EVENT_HEADER_SIZE);
        g_gap_events++;
    }
    sess_wake();
}

static void sess_on_model_changed_locked(uint32_t monotonic_ms)
{
    uint8_t wire[APP_SESSION_EVENT_HEADER_SIZE];

    sess_ring_flush_locked();
    g_model_incompatible = AICAM_FALSE;
    sess_encode_control_event(wire, APP_SESSION_EVT_KIND_MODEL_CHANGED, monotonic_ms);
    sess_ring_push_locked(wire, APP_SESSION_EVENT_HEADER_SIZE);
    g_gap_events++;
    sess_wake();
}

void app_host_session_on_ai_result(uint32_t detect_count,
                                   const od_detect_t *detects,
                                   uint32_t monotonic_ms)
{
    uint8_t wire[APP_SESSION_EVENT_WIRE_MAX];
    app_session_model_view_t view;
    aicam_bool_t view_ok;
    uint32_t count;
    uint32_t i;
    uint32_t c;
    uint32_t flags;
    uint32_t lost;
    uint32_t offset;
    const od_detect_t *det;
    int32_t class_index;
    aicam_bool_t encodable;

    if (g_backend == NULL || g_mtx == NULL) {
        return;
    }

    sess_lock();
    if (g_state != APP_SESSION_STATE_RUNNING ||
        g_attached != AICAM_TRUE ||
        g_stop_requested == AICAM_TRUE) {
        sess_unlock();
        return;
    }

    view_ok = AICAM_FALSE;
    if (g_backend->ai_model_view != NULL) {
        view_ok = g_backend->ai_model_view(g_backend->ctx, &view);
    }
    if (view_ok != AICAM_TRUE || view.result_type_od != AICAM_TRUE) {
        if (g_model_incompatible == AICAM_FALSE) {
            sess_on_model_incompatible_locked(monotonic_ms);
        }
        sess_unlock();
        return;
    }
    if (view.identity_addr != g_model.identity_addr ||
        view.identity_size != g_model.identity_size ||
        view.content_digest != g_model.content_digest ||
        g_model.class_count == 0u ||
        g_model_incompatible == AICAM_TRUE) {
        if (!sess_model_view_canonical(&view)) {
            sess_on_model_incompatible_locked(monotonic_ms);
            sess_unlock();
            return;
        }
        if (view.identity_addr != g_model.identity_addr ||
            view.identity_size != g_model.identity_size ||
            view.content_digest != g_model.content_digest ||
            g_model.class_count == 0u) {
            g_model = view;
            g_model_generation++;
            g_class_generation++;
            g_model_changes++;
        }
        sess_on_model_changed_locked(monotonic_ms);
    }

    if (detect_count > APP_SESSION_EVENT_MAX_DETECTIONS) {
        sess_account_lost_locked(1u);
        sess_produce_gap_locked(monotonic_ms);
        sess_unlock();
        return;
    }
    count = detect_count;
    encodable = AICAM_TRUE;
    for (i = 0u; i < count && encodable == AICAM_TRUE; i++) {
        det = &detects[i];
        if (sess_bits_finite(sess_float_bits(det->x)) != AICAM_TRUE ||
            sess_bits_finite(sess_float_bits(det->y)) != AICAM_TRUE ||
            sess_bits_finite(sess_float_bits(det->width)) != AICAM_TRUE ||
            sess_bits_finite(sess_float_bits(det->height)) != AICAM_TRUE ||
            sess_bits_finite(sess_float_bits(det->conf)) != AICAM_TRUE) {
            encodable = AICAM_FALSE;
            break;
        }
        if (det->class_name == NULL) {
            encodable = AICAM_FALSE;
            break;
        }
        class_index = -1;
        for (c = 0u; c < g_model.class_count; c++) {
            if (strcmp(det->class_name, g_model.class_names[c]) == 0) {
                class_index = (int32_t)c;
                break;
            }
        }
        if (class_index < 0) {
            encodable = AICAM_FALSE;
        }
    }
    if (encodable != AICAM_TRUE) {
        sess_account_lost_locked(1u);
        sess_produce_gap_locked(monotonic_ms);
        sess_unlock();
        return;
    }

    sess_take_gap_flags_locked(&flags, &lost);
    offset = APP_SESSION_EVENT_HEADER_SIZE + count * APP_SESSION_EVENT_RECORD_SIZE;
    memset(wire, 0, offset);
    sess_put_u32(wire + 0, offset);
    sess_put_u32(wire + 4, APP_SESSION_EVT_KIND_FRAME);
    sess_put_u32(wire + 8, g_sequence);
    g_sequence++;
    sess_put_u32(wire + 12, monotonic_ms);
    sess_put_u32(wire + 16, g_model_generation);
    sess_put_u32(wire + 20, g_class_generation);
    sess_put_u32(wire + 24, flags);
    sess_put_u32(wire + 28, count);
    sess_put_u32(wire + 32, lost);
    sess_put_u32(wire + 36, 0u);
    offset = APP_SESSION_EVENT_HEADER_SIZE;
    for (i = 0u; i < count; i++) {
        det = &detects[i];
        sess_put_u32(wire + offset + 0, sess_float_bits(det->x));
        sess_put_u32(wire + offset + 4, sess_float_bits(det->y));
        sess_put_u32(wire + offset + 8, sess_float_bits(det->width));
        sess_put_u32(wire + offset + 12, sess_float_bits(det->height));
        sess_put_u32(wire + offset + 16, sess_float_bits(det->conf));
        for (c = 0u; c < g_model.class_count; c++) {
            if (strcmp(det->class_name, g_model.class_names[c]) == 0) {
                sess_put_u32(wire + offset + 20, c);
                break;
            }
        }
        offset += APP_SESSION_EVENT_RECORD_SIZE;
    }

    sess_ring_push_locked(wire, offset);
    sess_wake();
    sess_unlock();
}

static aicam_bool_t sess_fill_v3_policy(app_admission_policy_t *policy)
{
    uint32_t base = 0;
    uint32_t size = 0;

    memset(policy, 0, sizeof(*policy));
    policy->target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy->psram_mib = 64u;
    policy->abi_version = APP_SESSION_ABI_VERSION_V3;
    policy->supported_caps_mask = APP_SESSION_V3_GRANTED_CAPS;
    policy->max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V3;
    policy->event_capacity = APP_SESSION_EVENT_CAPACITY_MAX;
    policy->report_capacity = (g_report_life_mtx != NULL &&
                               g_backend != NULL &&
                               g_backend->mutex_lock != NULL &&
                               g_backend->mutex_unlock != NULL)
                                  ? APP_REPORT_INPUT_MAX : 0u;
    policy->state_quota_capacity = APP_SESSION_V3_STATE_QUOTA_MAX;
    policy->control_capacity = APP_SESSION_V3_CONTROL_CAPACITY;
    policy->snapshot_capacity = APP_SESSION_V3_SNAPSHOT_CAPACITY;
    policy->abi_table_size = APP_SESSION_V3_ABI_TABLE_SIZE;
    policy->v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;
    if (app_host_exec_region(&base, &size) == AICAM_TRUE) {
        policy->exec_region_base = base;
        policy->exec_region_size = size;
    }
    return AICAM_TRUE;
}

static aicam_bool_t sess_fill_v4_policy(app_admission_policy_t *policy)
{
    if (sess_fill_v3_policy(policy) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    policy->abi_version = APP_SESSION_ABI_VERSION_V4;
    policy->abi_table_size = APP_SESSION_V4_ABI_TABLE_SIZE;
    policy->max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V4;
    policy->supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK |
                                  APP_ADMISSION_V4_CAP_ENABLE_CONTROL |
                                  APP_ADMISSION_V4_CAP_LIVE_PUBLISH;
    policy->live_publish_capacity = APP_HOST_LIVE_PUBLISH_CAPACITY;
    policy->live_track_capacity = APP_HOST_LIVE_TRACK_CAPACITY;
    return AICAM_TRUE;
}

static aicam_bool_t sess_fill_v5_policy(app_admission_policy_t *policy)
{
    if (sess_fill_v4_policy(policy) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    policy->max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V5;
    return AICAM_TRUE;
}

app_session_result_t app_host_session_pair_authorize(
    const app_install_io_t *io,
    const app_install_record_t *native_record,
    char *reason,
    size_t reason_cap)
{
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state = APP_SIDECAR_LIFECYCLE_NONE;
    app_sidecar_lifecycle_result_t result;

    if (reason != NULL && reason_cap > 0u) {
        reason[0] = '\0';
    }
    if (io == NULL || native_record == NULL) {
        if (reason != NULL && reason_cap > 0u) {
            (void)snprintf(reason, reason_cap, "%s", "PAIR_PARAM");
        }
        return APP_SESSION_ERR_PARAM;
    }
    if (strcmp(native_record->publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) != 0 ||
        memcmp(native_record->publisher_key_sha256,
               (const uint8_t[APP_INSTALL_SHA256_LEN]){0},
               APP_INSTALL_SHA256_LEN) != 0) {
        if (reason != NULL && reason_cap > 0u) {
            (void)snprintf(reason, reason_cap, "%s",
                           "LEGACY_SIGNED_STATE_PRESERVED");
        }
        return APP_SESSION_ERR_UNAVAILABLE;
    }
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
    trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
    trust.app_id = native_record->app_id;
    trust.app_id_len = strlen(native_record->app_id);
    trust.native_tbs_sha256 = native_record->tbs_sha256;
    result = app_sidecar_lifecycle_query(io, &trust, native_record,
                                         &descriptor, &state);
    if (result == APP_SIDECAR_LIFECYCLE_ERR_BUSY) {
        if (reason != NULL && reason_cap > 0u) {
            (void)snprintf(reason, reason_cap, "%s", "BUSY");
        }
        return APP_SESSION_ERR_BUSY;
    }
    if (result == APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN ||
        state == APP_SIDECAR_LIFECYCLE_UNKNOWN) {
        if (reason != NULL && reason_cap > 0u) {
            (void)snprintf(reason, reason_cap, "%s", "PAIR_STORAGE_UNKNOWN");
        }
        return APP_SESSION_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (result != APP_SIDECAR_LIFECYCLE_OK ||
        state != APP_SIDECAR_LIFECYCLE_ACTIVE) {
        if (reason != NULL && reason_cap > 0u) {
            (void)snprintf(reason, reason_cap, "%s", "PAIR_NOT_SERVABLE");
        }
        return APP_SESSION_ERR_UNAVAILABLE;
    }
    if (app_pair_is_authorized(io, native_record, &descriptor) != AICAM_TRUE) {
        if (reason != NULL && reason_cap > 0u) {
            (void)snprintf(reason, reason_cap, "%s", "PAIR_AUTHORIZATION_MISMATCH");
        }
        return APP_SESSION_ERR_UNAVAILABLE;
    }
    return APP_SESSION_OK;
}

app_session_result_t app_host_session_start(const app_install_io_t *io,
                                            app_host_session_start_out_t *out)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    app_admission_result_t arc;
    app_install_record_t rec;
    aicam_bool_t present = AICAM_FALSE;
    app_install_result_t qrc;
    app_install_io_result_t src;
    app_install_io_result_t rrc;
    size_t stat_len = 0;
    size_t staged_len = 0;
    uint8_t *stage;
    size_t stage_cap = 0;
    uint8_t sha[APP_INSTALL_SHA256_LEN];
    app_host_load_policy_t load_policy;
    app_host_check_t check;
    app_session_result_t result;
    uint32_t container;
    uint32_t enable_epoch = 0u;
    const uint8_t *native;
    const app_host_image_header_t *header;
    size_t header_size;

    if (out == NULL) {
        return APP_SESSION_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));
    out->result = APP_SESSION_ERR_PARAM;
    if (io == NULL || io->tx_begin == NULL || io->tx_end == NULL ||
        io->file_stat == NULL || io->file_read == NULL) {
        return APP_SESSION_ERR_PARAM;
    }
    if (g_backend == NULL || g_backend->stage_base == NULL ||
        g_backend->stage_commit == NULL || g_backend->invoke_entry == NULL ||
        g_backend->entry_spawn == NULL || g_mtx == NULL) {
        out->result = APP_SESSION_ERR_RUNTIME_UNAVAILABLE;
        snprintf(out->reason, sizeof(out->reason), "%s", "RUNTIME_UNAVAILABLE");
        return APP_SESSION_ERR_RUNTIME_UNAVAILABLE;
    }

    sess_lock();
    sess_try_reap_locked();
    if (g_state != APP_SESSION_STATE_IDLE) {
        sess_set_last(APP_SESSION_ERR_BUSY, "session_active");
        sess_unlock();
        out->result = APP_SESSION_ERR_BUSY;
        snprintf(out->reason, sizeof(out->reason), "%s", "session_active");
        return APP_SESSION_ERR_BUSY;
    }
    if (app_install_app_running() == AICAM_TRUE) {
        sess_set_last(APP_SESSION_ERR_BUSY, "BUSY");
        sess_unlock();
        out->result = APP_SESSION_ERR_BUSY;
        snprintf(out->reason, sizeof(out->reason), "%s", "BUSY");
        return APP_SESSION_ERR_BUSY;
    }

    if (io->tx_begin(io->ctx) != AICAM_TRUE) {
        sess_set_last(APP_SESSION_ERR_BUSY, "BUSY");
        sess_unlock();
        out->result = APP_SESSION_ERR_BUSY;
        snprintf(out->reason, sizeof(out->reason), "%s", "BUSY");
        return APP_SESSION_ERR_BUSY;
    }
    app_install_set_app_running(AICAM_TRUE);
    io->tx_end(io->ctx);

    result = APP_SESSION_ERR_UNAVAILABLE;

    qrc = app_install_query_committed(io, &rec, &present);
    if (qrc != APP_INSTALL_OK) {
        result = APP_SESSION_ERR_STORAGE_STATE_UNKNOWN;
        snprintf(out->reason, sizeof(out->reason), "%s", app_install_result_str(qrc));
        goto refuse;
    }
    if (present != AICAM_TRUE) {
        result = APP_SESSION_ERR_NOT_INSTALLED;
        snprintf(out->reason, sizeof(out->reason), "%s", "NOT_INSTALLED");
        goto refuse;
    }
    g_record = rec;
    g_enable_io = io;
    g_enable_boot_id = 0u;

    stage = g_backend->stage_base(g_backend->ctx, &stage_cap);
    if (stage == NULL || stage_cap == 0u ||
        (size_t)rec.package_len > stage_cap ||
        (uint64_t)rec.package_len > (uint64_t)APP_ADMISSION_MAX_PACKAGE_LEN_V5) {
        snprintf(out->reason, sizeof(out->reason), "%s", "RESOURCE_LIMIT");
        goto refuse;
    }

    src = io->file_stat(io->ctx, rec.package_path, &stat_len);
    if (src == APP_INSTALL_IO_ERROR) {
        result = APP_SESSION_ERR_STORAGE_STATE_UNKNOWN;
        snprintf(out->reason, sizeof(out->reason), "%s", "STORAGE_STATE_UNKNOWN");
        goto refuse;
    }
    if (src == APP_INSTALL_IO_NOT_FOUND) {
        snprintf(out->reason, sizeof(out->reason), "%s", "file_missing");
        goto refuse;
    }
    if (stat_len != (size_t)rec.package_len) {
        snprintf(out->reason, sizeof(out->reason), "%s", "content_mismatch");
        goto refuse;
    }

    rrc = io->file_read(io->ctx, rec.package_path, stage, (size_t)rec.package_len,
                        &staged_len);
    if (rrc == APP_INSTALL_IO_ERROR) {
        result = APP_SESSION_ERR_STORAGE_STATE_UNKNOWN;
        snprintf(out->reason, sizeof(out->reason), "%s", "STORAGE_STATE_UNKNOWN");
        goto refuse;
    }
    if (rrc != APP_INSTALL_IO_OK || staged_len != (size_t)rec.package_len) {
        snprintf(out->reason, sizeof(out->reason), "%s", "content_unreadable");
        goto refuse;
    }

    mbedtls_sha256(stage, staged_len, sha, 0);
    if (memcmp(sha, rec.package_sha256, APP_INSTALL_SHA256_LEN) != 0) {
        snprintf(out->reason, sizeof(out->reason), "%s", "content_mismatch");
        goto refuse;
    }

    container = app_admission_container_version(stage, staged_len);
    if (container != 5u) {
        result = APP_SESSION_ERR_UNAVAILABLE;
        snprintf(out->reason, sizeof(out->reason), "%s",
                 (container >= 1u && container <= 4u)
                     ? "LEGACY_SIGNED_STATE_PRESERVED" : "UNKNOWN_FORMAT");
        goto refuse;
    }

    if (sess_fill_v5_policy(&policy) != AICAM_TRUE) {
        snprintf(out->reason, sizeof(out->reason), "%s", "APP_POLICY_UNAVAILABLE");
        goto refuse;
    }
    memset(&trust, 0, sizeof(trust));

    arc = app_admission_verify_package(stage, staged_len, &policy, NULL, &verdict);
    if (arc != APP_ADMISSION_OK) {
        snprintf(out->reason, sizeof(out->reason), "%s",
                 app_admission_result_str(arc));
        goto refuse;
    }

    if (strcmp(rec.publisher_id, verdict.publisher_id) != 0 ||
        strcmp(rec.app_id, verdict.app_id) != 0 ||
        rec.version_major != verdict.version_major ||
        rec.version_minor != verdict.version_minor ||
        rec.version_patch != verdict.version_patch ||
        memcmp(rec.tbs_sha256, verdict.tbs_sha256, APP_INSTALL_SHA256_LEN) != 0 ||
        memcmp(rec.publisher_key_sha256, verdict.publisher_key_sha256,
               APP_INSTALL_SHA256_LEN) != 0) {
        snprintf(out->reason, sizeof(out->reason), "%s", "identity_mismatch");
        goto refuse;
    }

    if (container == 5u) {
        if (g_backend->pair_authorize == NULL) {
            snprintf(out->reason, sizeof(out->reason), "%s", "PAIR_GATE_UNAVAILABLE");
            goto refuse;
        }
        result = g_backend->pair_authorize(g_backend->ctx, io, &rec,
                                           out->reason, sizeof(out->reason));
        if (result != APP_SESSION_OK) {
            if (out->reason[0] == '\0') {
                snprintf(out->reason, sizeof(out->reason), "%s", "PAIR_NOT_SERVABLE");
            }
            goto refuse;
        }
    }

    {
        app_install_result_t floor_rc =
            app_native_vnext_floor_check(io, &rec, container);
        if (floor_rc != APP_INSTALL_OK) {
            result = (floor_rc == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN)
                         ? APP_SESSION_ERR_STORAGE_STATE_UNKNOWN
                         : APP_SESSION_ERR_UNAVAILABLE;
            snprintf(out->reason, sizeof(out->reason), "%s",
                     app_install_result_str(floor_rc));
            goto refuse;
        }
    }

    if (container == 5u &&
        (verdict.required_host_caps & APP_ADMISSION_V4_CAP_ENABLE_CONTROL) != 0u) {
        if (app_native_vnext_floor_epoch_advance(io, &enable_epoch) != APP_INSTALL_OK) {
            result = APP_SESSION_ERR_STORAGE_STATE_UNKNOWN;
            snprintf(out->reason, sizeof(out->reason), "%s", "STORAGE_STATE_UNKNOWN");
            goto refuse;
        }
        g_enable_boot_id = enable_epoch;
    }

    memset(&g_model, 0, sizeof(g_model));
    g_model_generation = 0u;
    g_class_generation = 0u;
    g_model_changes = 0u;
    if (sess_refresh_model_locked() != AICAM_TRUE ||
        g_model.result_type_od != AICAM_TRUE) {
        snprintf(out->reason, sizeof(out->reason), "%s", "model_incompatible");
        goto refuse;
    }

    load_policy.expected_target_addr = verdict.native_target_addr;
    load_policy.region_size = stage_cap;
    load_policy.abi_version = (container == 5u) ? APP_SESSION_ABI_VERSION_V4 :
                              (container == 3u) ? APP_SESSION_ABI_VERSION_V3 :
                                                  APP_SESSION_ABI_VERSION;
    native = stage + ((container == 5u) ? (size_t)APP_ADMISSION_IMAGE_OFFSET_V4 :
                      (container == 3u) ? (size_t)APP_ADMISSION_IMAGE_OFFSET_V3 :
                                          (size_t)APP_ADMISSION_IMAGE_OFFSET_V2);
    check = app_host_validate(native,
                              (size_t)APP_ADMISSION_NATIVE_HEADER_SIZE +
                                  (size_t)verdict.native_image_size,
                              &load_policy, &g_image_info);
    if (check != APP_HOST_CHECK_OK) {
        snprintf(out->reason, sizeof(out->reason), "%s",
                 app_host_check_str(check));
        goto refuse;
    }
    header = (const app_host_image_header_t *)(const void *)native;
    header_size = (size_t)header->header_size;
    memmove(stage, native + header_size, (size_t)g_image_info.image_size);
    if ((size_t)g_image_info.image_size < stage_cap) {
        memset(stage + (size_t)g_image_info.image_size, 0,
               stage_cap - (size_t)g_image_info.image_size);
    }

    if (g_backend->stage_commit(g_backend->ctx, stage_cap, &g_image_info) != 0) {
        snprintf(out->reason, sizeof(out->reason), "%s", "stage_commit_failed");
        goto refuse;
    }

    if (container >= 3u) {
        app_control_session_cfg_t cfg;
        uint32_t generation = 0u;
        int32_t open_rc;

        memset(&cfg, 0, sizeof(cfg));
        cfg.control_max = (verdict.control_max < APP_SESSION_V3_CONTROL_CAPACITY)
                              ? verdict.control_max
                              : APP_SESSION_V3_CONTROL_CAPACITY;
        cfg.snapshot_max = (verdict.snapshot_max < APP_SESSION_V3_SNAPSHOT_CAPACITY)
                               ? verdict.snapshot_max
                               : APP_SESSION_V3_SNAPSHOT_CAPACITY;
        cfg.caps = verdict.required_host_caps;
        cfg.protocol_major = (container == 5u) ? 4u : container;
        memcpy(cfg.publisher_id, rec.publisher_id, sizeof(cfg.publisher_id) - 1u);
        memcpy(cfg.app_id, rec.app_id, sizeof(cfg.app_id) - 1u);
        open_rc = app_host_control_session_open(&cfg, &generation);
        if (open_rc != APP_CONTROL_OK) {
            result = APP_SESSION_ERR_UNAVAILABLE;
            snprintf(out->reason, sizeof(out->reason), "%s",
                     "session_generation_exhausted");
            goto refuse;
        }
        g_is_v3 = AICAM_TRUE;
        g_v3_caps = verdict.required_host_caps;
        g_container_major = container;
        out->control_max = cfg.control_max;
        out->snapshot_max = cfg.snapshot_max;
        out->session_generation = generation;
        if ((verdict.required_host_caps & APP_SESSION_V3_CAP_APP_STATE) != 0u) {
            app_state_identity_t ident;

            memset(&ident, 0, sizeof(ident));
            memcpy(ident.spki_sha256, rec.publisher_key_sha256, APP_STATE_SHA256_LEN);
            memcpy(ident.publisher_id, rec.publisher_id,
                   sizeof(ident.publisher_id) - 1u);
            memcpy(ident.app_id, rec.app_id, sizeof(ident.app_id) - 1u);
            if (app_state_session_bind(&g_state_session, io, &ident,
                                       verdict.state_quota) == APP_STATE_OK) {
                g_state_bit = AICAM_TRUE;
                g_state_bound = AICAM_TRUE;
            } else {
                g_is_v3 = AICAM_FALSE;
                g_v3_caps = 0u;
                app_host_control_session_close();
                result = APP_SESSION_ERR_UNAVAILABLE;
                snprintf(out->reason, sizeof(out->reason), "%s", "state_bind_failed");
                goto refuse;
            }
        }
        if ((verdict.required_host_caps & APP_SESSION_CAP_REPORT_SUBMIT) != 0u) {
            uint32_t report_capacity = policy.report_capacity;
            aicam_result_t report_start_rc;

            if (report_capacity > APP_REPORT_INPUT_MAX) {
                report_capacity = APP_REPORT_INPUT_MAX;
            }
            if (verdict.report_max == 0u || report_capacity == 0u) {
                result = APP_SESSION_ERR_UNAVAILABLE;
                snprintf(out->reason, sizeof(out->reason), "%s",
                         "report_service_unavailable");
                goto refuse;
            }
            sess_report_lifecycle_lock();
            report_start_rc = app_report_service_device_start();
            if (report_start_rc != AICAM_OK) {
                sess_report_lifecycle_unlock();
                result = APP_SESSION_ERR_UNAVAILABLE;
                snprintf(out->reason, sizeof(out->reason), "%s",
                         "report_service_unavailable");
                goto refuse;
            }
            g_report_worker_started = AICAM_TRUE;
            if (app_report_service_device_running() != AICAM_TRUE ||
                app_report_service_boot_id() == 0u) {
                sess_report_lifecycle_unlock();
                result = APP_SESSION_ERR_UNAVAILABLE;
                snprintf(out->reason, sizeof(out->reason), "%s",
                         "report_identity_unavailable");
                goto refuse;
            }
            memset(&g_report_session, 0, sizeof(g_report_session));
            g_report_session.active = AICAM_TRUE;
            memcpy(g_report_session.publisher_id, verdict.publisher_id,
                   sizeof(g_report_session.publisher_id));
            memcpy(g_report_session.app_id, verdict.app_id,
                   sizeof(g_report_session.app_id));
            memcpy(g_report_session.publisher_key_sha256,
                   verdict.publisher_key_sha256,
                   sizeof(g_report_session.publisher_key_sha256));
            g_report_session.report_max =
                (verdict.report_max < report_capacity) ? verdict.report_max
                                                       : report_capacity;
            if (app_report_service_set_session(&g_report_session) !=
                APP_REPORT_ABI_OK) {
                sess_report_lifecycle_unlock();
                result = APP_SESSION_ERR_UNAVAILABLE;
                snprintf(out->reason, sizeof(out->reason), "%s",
                         "report_bind_failed");
                goto refuse;
            }
            g_report_bound = AICAM_TRUE;
            sess_report_lifecycle_unlock();
        }
        if ((verdict.required_host_caps & APP_ADMISSION_V4_CAP_LIVE_PUBLISH) != 0u) {
            app_host_live_identity_t live_ident;

            if (container != 5u) {
                result = APP_SESSION_ERR_UNAVAILABLE;
                snprintf(out->reason, sizeof(out->reason), "%s", "live_bind_failed");
                goto refuse;
            }
            memset(&live_ident, 0, sizeof(live_ident));
            live_ident.session_generation = generation;
            memcpy(live_ident.publisher_id, rec.publisher_id,
                   sizeof(rec.publisher_id));
            memcpy(live_ident.app_id, rec.app_id, sizeof(rec.app_id));
            memcpy(live_ident.spki_sha256, rec.publisher_key_sha256,
                   sizeof(live_ident.spki_sha256));
            memcpy(live_ident.tbs_sha256, rec.tbs_sha256,
                   sizeof(live_ident.tbs_sha256));
            memcpy(live_ident.package_sha256, rec.package_sha256,
                   sizeof(live_ident.package_sha256));
            live_ident.live_publish_max =
                (verdict.live_publish_max < APP_HOST_LIVE_PUBLISH_CAPACITY)
                    ? verdict.live_publish_max
                    : APP_HOST_LIVE_PUBLISH_CAPACITY;
            live_ident.live_track_max =
                (verdict.live_track_max < APP_HOST_LIVE_TRACK_CAPACITY)
                    ? verdict.live_track_max
                    : APP_HOST_LIVE_TRACK_CAPACITY;
            if (app_host_live_bind(&live_ident) != APP_HOST_LIVE_OK) {
                result = APP_SESSION_ERR_UNAVAILABLE;
                snprintf(out->reason, sizeof(out->reason), "%s", "live_bind_failed");
                goto refuse;
            }
        }
        g_session_generation = generation;
    }

    g_event_max = verdict.event_max;
    g_stop_requested = AICAM_FALSE;
    g_model_incompatible = AICAM_FALSE;
    g_ring_head = 0u;
    g_ring_count = 0u;
    g_pending_lost = 0u;
    g_pending_lost_unknown = AICAM_FALSE;
    g_events_delivered = 0u;
    g_frames_dropped = 0u;
    g_gap_events = 0u;
    g_sequence = 0u;
    sess_set_last(APP_SESSION_OK, "");
    out->result = APP_SESSION_OK;
    out->record = rec;
    out->abi_version = (container == 5u) ? APP_SESSION_ABI_VERSION_V4 :
                       (container == 3u) ? APP_SESSION_ABI_VERSION_V3 :
                                           APP_SESSION_ABI_VERSION;
    out->granted_caps = (container >= 3u) ? verdict.required_host_caps
                                          : APP_SESSION_GRANTED_CAPS;
    out->event_max = verdict.event_max;
    if (container < 3u) {
        out->control_max = 0u;
        out->snapshot_max = 0u;
        out->session_generation = 0u;
    }
    g_state = APP_SESSION_STATE_STOPPING;
    sess_unlock();

    if (g_backend->ai_attach == NULL) {
        g_attached = AICAM_FALSE;
        sess_session_teardown(APP_SESSION_ERR_RUNTIME_UNAVAILABLE, "ai_attach_failed",
                              AICAM_FALSE);
        out->result = APP_SESSION_ERR_RUNTIME_UNAVAILABLE;
        snprintf(out->reason, sizeof(out->reason), "%s", "ai_attach_failed");
        return APP_SESSION_ERR_RUNTIME_UNAVAILABLE;
    }
    g_attached = g_backend->ai_attach(g_backend->ctx);
    if (g_attached != AICAM_TRUE) {
        sess_session_teardown(APP_SESSION_ERR_RUNTIME_UNAVAILABLE, "ai_attach_failed",
                              AICAM_FALSE);
        out->result = APP_SESSION_ERR_RUNTIME_UNAVAILABLE;
        snprintf(out->reason, sizeof(out->reason), "%s", "ai_attach_failed");
        return APP_SESSION_ERR_RUNTIME_UNAVAILABLE;
    }

    sess_lock();
    g_state = APP_SESSION_STATE_RUNNING;
    g_entry_alive = AICAM_TRUE;
    sess_unlock();

    if (g_backend->entry_spawn(g_backend->ctx, sess_entry_thread, NULL) != 0) {
        sess_session_teardown(APP_SESSION_ERR_RUNTIME_UNAVAILABLE, "entry_spawn_failed",
                              AICAM_FALSE);
        out->result = APP_SESSION_ERR_RUNTIME_UNAVAILABLE;
        snprintf(out->reason, sizeof(out->reason), "%s", "entry_spawn_failed");
        return APP_SESSION_ERR_RUNTIME_UNAVAILABLE;
    }

    return APP_SESSION_OK;

refuse:
    if (result == APP_SESSION_OK) {
        result = APP_SESSION_ERR_UNAVAILABLE;
    }
    sess_report_shutdown();
    if (g_state_bound == AICAM_TRUE) {
        app_state_session_end(&g_state_session);
        g_state_bound = AICAM_FALSE;
    }
    app_host_live_end(g_session_generation);
    g_session_generation = 0u;
    if (g_is_v3 == AICAM_TRUE) {
        app_host_control_session_close();
    }
    sess_enable_lock();
    memset(&g_enable_witness, 0, sizeof(g_enable_witness));
    sess_enable_unlock();
    g_enable_io = NULL;
    g_enable_boot_id = 0u;
    g_is_v3 = AICAM_FALSE;
    g_v3_caps = 0u;
    g_container_major = 0u;
    g_state_bit = AICAM_FALSE;
    memset(&g_state_session, 0, sizeof(g_state_session));
    app_install_set_app_running(AICAM_FALSE);
    sess_set_last(result, out->reason);
    sess_unlock();
    out->result = result;
    return result;
}

app_session_result_t app_host_session_stop(uint32_t wait_ms,
                                           app_host_session_stop_out_t *out)
{
    uint32_t waited = 0u;

    if (out == NULL) {
        return APP_SESSION_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));
    if (wait_ms == 0u || wait_ms > 60000u) {
        wait_ms = APP_SESSION_DEFAULT_STOP_WAIT_MS;
    }

    sess_lock();
    if (g_state != APP_SESSION_STATE_RUNNING && g_state != APP_SESSION_STATE_STOPPING) {
        sess_unlock();
        out->result = APP_SESSION_ERR_NOT_RUNNING;
        snprintf(out->reason, sizeof(out->reason), "%s", "not_running");
        return APP_SESSION_ERR_NOT_RUNNING;
    }
    g_stop_requested = AICAM_TRUE;
    g_state = APP_SESSION_STATE_STOPPING;
    sess_unlock();
    sess_wake();
    sess_report_shutdown();

    for (;;) {
        sess_lock();
        sess_try_reap_locked();
        if (g_state == APP_SESSION_STATE_IDLE) {
            sess_unlock();
            out->result = APP_SESSION_OK;
            out->entry_returned = AICAM_TRUE;
            return APP_SESSION_OK;
        }
        sess_unlock();
        if (waited >= wait_ms) {
            break;
        }
        if (g_backend != NULL && g_backend->sleep_ms != NULL) {
            g_backend->sleep_ms(g_backend->ctx, 10u);
        }
        waited += 10u;
    }

    sess_lock();
    if (g_entry_alive != AICAM_TRUE) {
        sess_unlock();
        out->result = APP_SESSION_ERR_BUSY;
        out->entry_returned = AICAM_TRUE;
        snprintf(out->reason, sizeof(out->reason), "%s", "cleanup_pending");
        return APP_SESSION_ERR_BUSY;
    }
    sess_unlock();
    out->result = APP_SESSION_ERR_BUSY;
    out->entry_returned = AICAM_FALSE;
    snprintf(out->reason, sizeof(out->reason), "%s", "entry_not_returned");
    return APP_SESSION_ERR_BUSY;
}

const char *app_session_result_str(app_session_result_t result)
{
    switch (result) {
    case APP_SESSION_OK:
        return "OK";
    case APP_SESSION_ERR_PARAM:
        return "PARAM";
    case APP_SESSION_ERR_NOT_INSTALLED:
        return "NOT_INSTALLED";
    case APP_SESSION_ERR_UNAVAILABLE:
        return "UNAVAILABLE";
    case APP_SESSION_ERR_STORAGE_STATE_UNKNOWN:
        return "STORAGE_STATE_UNKNOWN";
    case APP_SESSION_ERR_BUSY:
        return "BUSY";
    case APP_SESSION_ERR_LOAD_FAILED:
        return "LOAD_FAILED";
    case APP_SESSION_ERR_RUNTIME_UNAVAILABLE:
        return "RUNTIME_UNAVAILABLE";
    case APP_SESSION_ERR_NOT_RUNNING:
        return "NOT_RUNNING";
    case APP_SESSION_ERR_PROFILE_UNSUPPORTED:
        return "PROFILE_UNSUPPORTED";
    default:
        return "UNKNOWN";
    }
}

const char *app_session_state_str(app_session_state_t state)
{
    switch (state) {
    case APP_SESSION_STATE_IDLE:
        return "idle";
    case APP_SESSION_STATE_RUNNING:
        return "running";
    case APP_SESSION_STATE_STOPPING:
        return "stopping";
    default:
        return "unknown";
    }
}
