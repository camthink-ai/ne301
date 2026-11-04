
#include "host_stub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

lcstub_t *lcstub_new(void)
{
    lcstub_t *s = (lcstub_t *)calloc(1, sizeof(*s));
    if (!s) abort();
    s->authorized = 1;
    s->tick_step = 0;
    s->boot_id = 0x5A5A0001u;
    s->default_mqtt_state = LC_RSTATE_PENDING;
    s->default_webhook_state = LC_RSTATE_PENDING;
    s->verify_report_seq = 1;
    s->control_max = LC_CTRL_CONTROL_MAX_DECLARED;
    s->snapshot_ram = LC_CTRL_SNAPSHOT_MAX_DECLARED;
    return s;
}

void lcstub_destroy(lcstub_t *s) { free(s); }

void lcstub_set_session(lcstub_t *s, int authorized) { s->authorized = authorized; }
void lcstub_set_stop(lcstub_t *s, int stop) { s->stop_flag = stop; }
void lcstub_set_tick(lcstub_t *s, uint32_t t) { s->tick_now = t; }
void lcstub_set_tick_step(lcstub_t *s, uint32_t step) { s->tick_step = step; }

void lcstub_set_model(lcstub_t *s, int loaded, uint32_t model_gen, uint32_t class_gen,
                      uint32_t class_count, const char *model_name, const char *model_version)
{
    s->model_loaded = loaded;
    s->model_gen = model_gen;
    s->class_gen = class_gen;
    s->class_count = class_count;
    snprintf(s->model_name, sizeof(s->model_name), "%s", model_name ? model_name : "");
    snprintf(s->model_version, sizeof(s->model_version), "%s", model_version ? model_version : "");
    s->meta_raw_len = 0;
}

void lcstub_set_class(lcstub_t *s, uint32_t index, const char *name)
{
    if (index < LCSTUB_MAX_CLASSES) {
        snprintf(s->classes[index], sizeof(s->classes[index]), "%s", name);
    }
}

void lcstub_set_meta_raw(lcstub_t *s, const uint8_t *bytes, uint32_t len)
{
    s->meta_raw_len = 0;
    if (!bytes || len > sizeof(s->meta_raw)) return;
    memcpy(s->meta_raw, bytes, len);
    s->meta_raw_len = len;
}

void lcstub_set_state(lcstub_t *s, const void *blob, uint32_t len, uint32_t revision)
{
    if (len > LCSTUB_STATE_CAP) return;
    s->state_present = blob != NULL;
    if (blob && len) {
        memcpy(s->state_blob, blob, len);
        s->state_len = len;
        s->state_revision = revision;
    }
}

void lcstub_set_boot_id(lcstub_t *s, uint32_t boot_id) { s->boot_id = boot_id; }

void lcstub_set_report_channels(lcstub_t *s, uint32_t mqtt_state, uint32_t webhook_state)
{
    s->default_mqtt_state = mqtt_state;
    s->default_webhook_state = webhook_state;
}

void lcstub_set_report_state(lcstub_t *s, uint32_t seq, uint32_t mqtt_state, uint32_t web_state)
{
    for (int i = 0; i < LCSTUB_MAX_REPORTS; i++) {
        if (s->reports[i].present && s->reports[i].seq == seq) {
            s->reports[i].mqtt_state = mqtt_state;
            s->reports[i].web_state = web_state;
            return;
        }
    }
}

void lcstub_fault(lcstub_t *s, lcstub_fn_t fn, int32_t code, int times)
{
    s->fault_code[fn] = code;
    s->fault_times[fn] = times;
    s->fault_skip[fn] = 0;
}

void lcstub_fault_after(lcstub_t *s, lcstub_fn_t fn, int32_t code, int skip,
                        int times)
{
    s->fault_code[fn] = code;
    s->fault_times[fn] = times;
    s->fault_skip[fn] = skip;
}

void lcstub_state_read_fault(lcstub_t *s, int32_t code, int times)
{
    s->state_read_fault = code;
    s->state_read_fault_times = times;
}

void lcstub_state_commit_fault(lcstub_t *s, int32_t code, int times)
{
    s->state_commit_fault = code;
    s->state_commit_fault_times = times;
}

uint32_t lcstub_calls(const lcstub_t *s, lcstub_fn_t fn) { return s->calls[fn]; }

const char *lcstub_last_log(const lcstub_t *s)
{
    if (s->log_count == 0) return "";
    int idx = (s->log_head + LCSTUB_LOG_LINES - 1) % LCSTUB_LOG_LINES;
    return s->logs[idx];
}

const lcstub_report_t *lcstub_report_by_seq(const lcstub_t *s, uint32_t seq)
{
    for (int i = 0; i < LCSTUB_MAX_REPORTS; i++) {
        if (s->reports[i].present && s->reports[i].seq == seq) return &s->reports[i];
    }
    return NULL;
}

void lcstub_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t f32_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

static void push_bytes(lcstub_t *s, const uint8_t *bytes, uint32_t len)
{
    if (s->ev_count >= LCSTUB_MAX_EVENTS || len > LCSTUB_EVENT_MAX) return;
    int slot = (s->ev_head + s->ev_count) % LCSTUB_MAX_EVENTS;
    memcpy(s->events[slot], bytes, len);
    s->event_len[slot] = len;
    s->ev_count++;
}

void lcstub_push_raw(lcstub_t *s, const void *bytes, uint32_t len)
{
    push_bytes(s, (const uint8_t *)bytes, len);
}

static void encode_hdr(uint8_t *b, uint32_t total_len, uint32_t kind, uint32_t seq,
                       uint32_t mono, uint32_t mgen, uint32_t cgen,
                       uint32_t flags, uint32_t lost)
{
    memset(b, 0, LC_EVT_HDR_SIZE);
    lcstub_wr32(b + 4 * 0, total_len);
    lcstub_wr32(b + 4 * 1, kind);
    lcstub_wr32(b + 4 * 2, seq);
    lcstub_wr32(b + 4 * 3, mono);
    lcstub_wr32(b + 4 * 4, mgen);
    lcstub_wr32(b + 4 * 5, cgen);
    lcstub_wr32(b + 4 * 6, flags);
    lcstub_wr32(b + 4 * 7, 0);
    lcstub_wr32(b + 4 * 8, lost);
    lcstub_wr32(b + 4 * 9, 0);
}

void lcstub_push_kind(lcstub_t *s, uint32_t kind, uint32_t seq, uint32_t mono_ms,
                      uint32_t model_gen, uint32_t class_gen, uint32_t flags, uint32_t lost)
{
    uint8_t b[LC_EVT_HDR_SIZE];
    encode_hdr(b, LC_EVT_HDR_SIZE, kind, seq, mono_ms, model_gen, class_gen, flags, lost);
    push_bytes(s, b, sizeof(b));
}

void lcstub_push_frame(lcstub_t *s, uint32_t seq, uint32_t mono_ms,
                       uint32_t model_gen, uint32_t class_gen,
                       uint32_t flags, uint32_t lost,
                       const lcstub_det_t *dets, uint32_t n)
{
    uint8_t b[LC_EVT_HDR_SIZE + LC_EVT_MAX_DETECTIONS * LC_EVT_REC_SIZE];
    uint32_t total = LC_EVT_HDR_SIZE + n * LC_EVT_REC_SIZE;
    encode_hdr(b, total, LC_EVT_KIND_FRAME, seq, mono_ms, model_gen, class_gen, flags, lost);
    lcstub_wr32(b + 4 * 7, n);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *r = b + LC_EVT_HDR_SIZE + i * LC_EVT_REC_SIZE;
        lcstub_wr32(r + 4 * 0, f32_bits(dets[i].x));
        lcstub_wr32(r + 4 * 1, f32_bits(dets[i].y));
        lcstub_wr32(r + 4 * 2, f32_bits(dets[i].w));
        lcstub_wr32(r + 4 * 3, f32_bits(dets[i].h));
        lcstub_wr32(r + 4 * 4, f32_bits(dets[i].conf));
        lcstub_wr32(r + 4 * 5, dets[i].class_index);
    }
    push_bytes(s, b, total);
}

static int32_t take_fault(lcstub_t *s, lcstub_fn_t fn)
{
    if (s->fault_skip[fn] > 0) {
        s->fault_skip[fn]--;
        return LC_RET_OK;
    }
    if (s->fault_times[fn] > 0) {
        s->fault_times[fn]--;
        return s->fault_code[fn];
    }
    return LC_RET_OK;
}

static lcstub_t *g_cur;

static void record_log(lcstub_t *s, const char *text)
{
    s->logs[s->log_head][0] = '\0';
    snprintf(s->logs[s->log_head], LCSTUB_LOG_LINE_LEN, "%s", text ? text : "");
    s->log_head = (s->log_head + 1) % LCSTUB_LOG_LINES;
    if (s->log_count < LCSTUB_LOG_LINES) s->log_count++;
}

static int32_t fn_log(const char *text)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_LOG]++;
    record_log(s, text);
    return 0;
}

static int32_t fn_tick_ms(void)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_TICK]++;
    s->tick_now += s->tick_step;
    return (int32_t)s->tick_now;
}

static int32_t fn_event_next(void *out, uint32_t cap, uint32_t *actual_len,
                             uint32_t max_wait_ms)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_EVENT_NEXT]++;
    (void)max_wait_ms;
    int32_t f = take_fault(s, LCSTUB_FN_EVENT_NEXT);
    if (f != LC_RET_OK) return f;
    if (!s->authorized) return LC_RET_UNAUTHORIZED;
    if (s->ev_count == 0) {
        s->no_event_streak++;
        if (s->auto_stop_after_no_events != 0 &&
            s->no_event_streak >= s->auto_stop_after_no_events) {
            s->stop_flag = 1;
        }
        return LC_RET_NO_EVENT;
    }
    int slot = s->ev_head;
    if (cap < s->event_len[slot]) return LC_RET_BUFFER_TOO_SMALL;
    memcpy(out, s->events[slot], s->event_len[slot]);
    *actual_len = s->event_len[slot];
    s->ev_head = (s->ev_head + 1) % LCSTUB_MAX_EVENTS;
    s->ev_count--;
    return LC_RET_OK;
}

static int32_t fn_model_meta(void *out, uint32_t cap, uint32_t *actual_len)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_MODEL_META]++;
    int32_t f = take_fault(s, LCSTUB_FN_MODEL_META);
    if (f != LC_RET_OK) return f;
    if (!s->authorized) return LC_RET_UNAUTHORIZED;
    if (s->meta_raw_len != 0) {
        if (cap < s->meta_raw_len) return LC_RET_BUFFER_TOO_SMALL;
        memcpy(out, s->meta_raw, s->meta_raw_len);
        *actual_len = s->meta_raw_len;
        return LC_RET_OK;
    }
    if (cap < LC_MODEL_META_SIZE) return LC_RET_BUFFER_TOO_SMALL;
    uint8_t m[LC_MODEL_META_SIZE];
    memset(m, 0, sizeof(m));
    lcstub_wr32(m + 0, s->model_loaded ? 1u : 0u);
    lcstub_wr32(m + 4, s->meta_result_type_override ? 2u : 1u);
    lcstub_wr32(m + 8, s->model_gen);
    lcstub_wr32(m + 12, s->class_gen);
    lcstub_wr32(m + 16, s->class_count);

    snprintf((char *)m + 24, LC_MODEL_NAME_LEN, "%s", s->model_name);
    snprintf((char *)m + 88, LC_MODEL_VERSION_LEN, "%s", s->model_version);
    memcpy(out, m, LC_MODEL_META_SIZE);
    *actual_len = LC_MODEL_META_SIZE;
    return LC_RET_OK;
}

static int32_t fn_class_name(uint32_t model_gen, uint32_t class_gen, uint32_t class_index,
                             void *out_utf8, uint32_t cap, uint32_t *actual_len)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_CLASS_NAME]++;
    int32_t f = take_fault(s, LCSTUB_FN_CLASS_NAME);
    if (f != LC_RET_OK) return f;
    if (!s->authorized) return LC_RET_UNAUTHORIZED;
    if (model_gen != s->model_gen || class_gen != s->class_gen) {
        return LC_RET_INCOMPATIBLE;
    }
    if (class_index >= s->class_count) return LC_RET_INVALID_ARGUMENT;
    const char *name = s->classes[class_index];
    size_t len = strlen(name);
    if (cap < len + 1u) return LC_RET_BUFFER_TOO_SMALL;
    memcpy(out_utf8, name, len + 1u);
    *actual_len = (uint32_t)len;
    return LC_RET_OK;
}

static int json_find_u32(const char *json, const char *key, uint32_t *out)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);

    while (*p == ' ') p++;
    if (*p < '0' || *p > '9') return 0;
    unsigned long v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10u + (unsigned long)(*p - '0');
        p++;
    }
    *out = (uint32_t)v;
    return 1;
}

static int32_t fn_report_submit(const void *json, uint32_t len, uint32_t app_report_seq,
                                uint32_t *out_host_boot_id)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_REPORT_SUBMIT]++;
    int32_t f = take_fault(s, LCSTUB_FN_REPORT_SUBMIT);
    if (f != LC_RET_OK) return f;
    if (!s->authorized) return LC_RET_UNAUTHORIZED;
    if (!json || len == 0) return LC_RET_INVALID_ARGUMENT;
    if (len > 6144u) return LC_RET_QUOTA_EXCEEDED;

    char buf[6145];
    memcpy(buf, json, len);
    buf[len] = '\0';
    if (s->verify_report_seq) {
        uint32_t jseq = 0;
        if (!json_find_u32(buf, "report_seq", &jseq) || jseq != app_report_seq) {
            return LC_RET_INVALID_ARGUMENT;
        }
    }

    int slot = -1;
    for (int i = 0; i < LCSTUB_MAX_REPORTS; i++) {
        if (!s->reports[i].present) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < LCSTUB_MAX_REPORTS; i++) {
            if (s->reports[i].seq < s->reports[slot].seq) slot = i;
        }
    }
    s->reports[slot].present = 1;
    s->reports[slot].seq = app_report_seq;
    s->reports[slot].len = len;
    memcpy(s->reports[slot].bytes, json, len);
    s->reports[slot].mqtt_state = s->default_mqtt_state;
    s->reports[slot].web_state = s->default_webhook_state;
    if (s->report_count < LCSTUB_MAX_REPORTS) s->report_count++;
    *out_host_boot_id = s->boot_id;
    return LC_RET_OK;
}

static int32_t fn_report_status(uint32_t host_boot_id, uint32_t report_seq,
                                uint32_t *out_mqtt_state, uint32_t *out_webhook_state)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_REPORT_STATUS]++;
    int32_t f = take_fault(s, LCSTUB_FN_REPORT_STATUS);
    if (f != LC_RET_OK) return f;
    if (!s->authorized) return LC_RET_UNAUTHORIZED;
    if (host_boot_id != s->boot_id) return LC_RET_NOT_FOUND;
    for (int i = 0; i < LCSTUB_MAX_REPORTS; i++) {
        if (s->reports[i].present && s->reports[i].seq == report_seq) {
            *out_mqtt_state = s->reports[i].mqtt_state;
            *out_webhook_state = s->reports[i].web_state;
            return LC_RET_OK;
        }
    }
    return LC_RET_NOT_FOUND;
}

static int32_t fn_state_read(void *out, uint32_t cap, uint32_t *actual_len,
                             uint32_t *out_revision)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_STATE_READ]++;
    if (s->state_read_fault_times > 0) {
        s->state_read_fault_times--;
        return s->state_read_fault;
    }
    int32_t f = take_fault(s, LCSTUB_FN_STATE_READ);
    if (f != LC_RET_OK) return f;
    if (!s->authorized) return LC_RET_UNAUTHORIZED;
    if (!s->state_present) return LC_RET_NOT_FOUND;
    if (cap < s->state_len) return LC_RET_BUFFER_TOO_SMALL;
    memcpy(out, s->state_blob, s->state_len);
    *actual_len = s->state_len;
    *out_revision = s->state_revision;
    return LC_RET_OK;
}

static int32_t fn_state_commit(const void *blob, uint32_t len, uint32_t expected_revision,
                               uint32_t *new_revision)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_STATE_COMMIT]++;
    if (s->state_commit_fault_times > 0) {
        s->state_commit_fault_times--;
        return s->state_commit_fault;
    }
    int32_t f = take_fault(s, LCSTUB_FN_STATE_COMMIT);
    if (f != LC_RET_OK) return f;
    if (!s->authorized) return LC_RET_UNAUTHORIZED;
    if (!blob || len == 0 || len > LCSTUB_STATE_CAP) return LC_RET_QUOTA_EXCEEDED;
    if (expected_revision != s->state_revision) return LC_RET_REVISION_CONFLICT;
    if (s->state_torn_write) {
        memcpy(s->state_blob, blob, len / 2u);
        s->state_len = len;
        s->state_present = 1;
        s->state_revision++;
        *new_revision = s->state_revision_reply != 0u
                            ? s->state_revision_reply : s->state_revision;
        s->state_torn_write = 0;
        return LC_RET_OK;
    }
    memcpy(s->state_blob, blob, len);
    s->state_len = len;
    s->state_present = 1;
    s->state_revision++;
    *new_revision = s->state_revision_reply != 0u
                        ? s->state_revision_reply : s->state_revision;
    return LC_RET_OK;
}

static int32_t fn_should_stop(void)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_SHOULD_STOP]++;
    int32_t f = take_fault(s, LCSTUB_FN_SHOULD_STOP);
    if (f != LC_RET_OK) return f;
    return s->stop_flag ? 1 : 0;
}

void lcstub_make_table(lcstub_t *s, lc_app_api_v2_t *out)
{
    g_cur = s;
    memset(out, 0, sizeof(*out));
    out->table_size = LC_APP_API_TABLE_SIZE;
    out->abi_version = LC_APP_ABI_V2;
    out->log = fn_log;
    out->tick_ms = fn_tick_ms;
    out->event_next = fn_event_next;
    out->model_meta = fn_model_meta;
    out->class_name = fn_class_name;
    out->report_submit = fn_report_submit;
    out->report_status = fn_report_status;
    out->state_read = fn_state_read;
    out->state_commit = fn_state_commit;
    out->should_stop = fn_should_stop;
}

static int32_t fn_control_next(void *out, uint32_t cap, uint32_t *actual_len)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_CONTROL_NEXT]++;
    int32_t f = take_fault(s, LCSTUB_FN_CONTROL_NEXT);
    if (f != LC_RET_OK) return f;
    if (!s->authorized || s->ctrl_unauthorized) return LC_RET_UNAUTHORIZED;
    if (s->cmd_count == 0) return LC_RET_NO_EVENT;
    int slot = s->cmd_head;
    if (cap < s->command_len[slot]) {
        *actual_len = s->command_len[slot];
        return LC_RET_BUFFER_TOO_SMALL;
    }
    memcpy(out, s->commands[slot], s->command_len[slot]);
    *actual_len = s->command_len[slot];
    uint32_t type = (uint32_t)s->commands[slot][LC_CTRL_OFF_MSG_TYPE] |
                    ((uint32_t)s->commands[slot][LC_CTRL_OFF_MSG_TYPE + 1u] << 8);
    s->delivered_type = type;
    s->delivered_magic = lc_rd_u32(s->commands[slot] + LC_CTRL_OFF_PROTO_MAGIC);
    s->delivered_session = lc_rd_u32(s->commands[slot] + LC_CTRL_OFF_SESSION_GENERATION);
    s->delivered_request = lc_rd_u32(s->commands[slot] + LC_CTRL_OFF_REQUEST_ID);
    s->ctrl_delivered_valid = 1;
    s->cmd_head = (s->cmd_head + 1) % LCSTUB_MAX_COMMANDS;
    s->cmd_count--;
    return LC_RET_OK;
}

static int utf8_valid_bytes(const uint8_t *p, uint32_t n)
{
    uint32_t i = 0;
    while (i < n) {
        uint8_t c = p[i];
        uint32_t need = 0;
        uint32_t cp = 0;
        if (c < 0x80u) { i++; continue; }
        else if ((c & 0xE0u) == 0xC0u) { need = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0u) == 0xE0u) { need = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8u) == 0xF0u) { need = 3; cp = c & 0x07u; }
        else return 0;
        i++;
        if (i + need > n) return 0;
        while (need > 0) {
            if ((p[i] & 0xC0u) != 0x80u) return 0;
            cp = (cp << 6) | (p[i] & 0x3Fu);
            i++;
            need--;
        }
        if (cp > 0x10FFFFu) return 0;
        if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;
    }
    return 1;
}

typedef struct {
    char keys[64][48];
    int  counts[64];
    int  n;
} stub_keyset_t;

static int stub_key_seen(stub_keyset_t *ks, const char *key, uint32_t len)
{
    for (int i = 0; i < ks->n; i++) {
        if (strlen(ks->keys[i]) == len && memcmp(ks->keys[i], key, len) == 0) {
            ks->counts[i]++;
            return 1;
        }
    }
    if (ks->n < 64 && len < 48) {
        memcpy(ks->keys[ks->n], key, len);
        ks->keys[ks->n][len] = '\0';
        ks->counts[ks->n] = 1;
        ks->n++;
    }
    return 0;
}

static const uint8_t *stub_scan_value(const uint8_t *p, const uint8_t *end,
                                      stub_keyset_t *ks, int depth);

static const uint8_t *stub_scan_string(const uint8_t *p, const uint8_t *end)
{
    if (p >= end || *p != '"') return NULL;
    p++;
    while (p < end && *p != '"') {
        if (*p == '\\') {
            p++;
            if (p >= end) return NULL;
            if (*p == 'u') {
                for (int k = 0; k < 4; k++) {
                    p++;
                    if (p >= end || !((*p >= '0' && *p <= '9') ||
                                      (*p >= 'a' && *p <= 'f') ||
                                      (*p >= 'A' && *p <= 'F'))) return NULL;
                }
            } else if (*p != '"' && *p != '\\' && *p != '/' && *p != 'b' &&
                       *p != 'f' && *p != 'n' && *p != 'r' && *p != 't') {
                return NULL;
            }
            p++;
        } else if (*p < 0x20u) {
            return NULL;
        } else {
            p++;
        }
    }
    if (p >= end) return NULL;
    return p + 1;
}

static const uint8_t *stub_scan_object(const uint8_t *p, const uint8_t *end,
                                       stub_keyset_t *ks, int depth);

static const uint8_t *stub_skip_ws(const uint8_t *p, const uint8_t *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static const uint8_t *stub_scan_value(const uint8_t *p, const uint8_t *end,
                                      stub_keyset_t *ks, int depth)
{
    if (depth > 16) return NULL;
    p = stub_skip_ws(p, end);
    if (p >= end) return NULL;
    if (*p == '"') return stub_scan_string(p, end);
    if (*p == '{') return stub_scan_object(p, end, ks, depth + 1);
    if (*p == '[') {
        p++;
        p = stub_skip_ws(p, end);
        if (p < end && *p == ']') return p + 1;
        for (;;) {
            p = stub_scan_value(p, end, ks, depth + 1);
            if (!p) return NULL;
            p = stub_skip_ws(p, end);
            if (p < end && *p == ',') { p++; continue; }
            if (p < end && *p == ']') return p + 1;
            return NULL;
        }
    }
    const uint8_t *start = p;
    while (p < end && ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' ||
                       *p == '.' || *p == 'e' || *p == 'E')) p++;
    if (p > start) {
        for (const uint8_t *q = start; q < p; q++) {
            if ((q + 3 <= p && memcmp(q, "NaN", 3) == 0) ||
                (q + 8 <= p && memcmp(q, "Infinity", 8) == 0)) {
                return NULL;
            }
        }
        return p;
    }
    if (p + 4 <= end && memcmp(p, "true", 4) == 0) return p + 4;
    if (p + 5 <= end && memcmp(p, "false", 5) == 0) return p + 5;
    if (p + 4 <= end && memcmp(p, "null", 4) == 0) return p + 4;
    return NULL;
}

static const uint8_t *stub_scan_object(const uint8_t *p, const uint8_t *end,
                                       stub_keyset_t *ks, int depth)
{
    stub_keyset_t local;
    stub_keyset_t *set = ks ? ks : &local;
    set->n = 0;
    p = stub_skip_ws(p + 1, end);
    if (p < end && *p == '}') return p + 1;
    for (;;) {
        p = stub_skip_ws(p, end);
        if (p >= end || *p != '"') return NULL;
        const uint8_t *kstart = p + 1;
        p = stub_scan_string(p, end);
        if (!p) return NULL;
        uint32_t klen = (uint32_t)((p - 1) - kstart);
        if (!utf8_valid_bytes(kstart, klen)) return NULL;
        if (stub_key_seen(set, (const char *)kstart, klen)) return NULL;
        p = stub_skip_ws(p, end);
        if (p >= end || *p != ':') return NULL;
        p = stub_scan_value(p + 1, end, NULL, depth);
        if (!p) return NULL;
        p = stub_skip_ws(p, end);
        if (p < end && *p == ',') { p++; continue; }
        if (p < end && *p == '}') return p + 1;
        return NULL;
    }
}

int lcstub_snapshot_valid_json(const uint8_t *payload, uint32_t len)
{
    if (!payload || len == 0 || len > 4096) return 0;
    if (!utf8_valid_bytes(payload, len)) return 0;
    const uint8_t *end = payload + len;
    const uint8_t *p = stub_skip_ws(payload, end);
    if (p >= end || *p != '{') return 0;
    p = stub_scan_object(p, end, NULL, 0);
    if (!p) return 0;
    p = stub_skip_ws(p, end);
    return p == end;
}

static int32_t fn_control_reply(const void *buf, uint32_t len)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_CONTROL_REPLY]++;
    int32_t f = take_fault(s, LCSTUB_FN_CONTROL_REPLY);
    if (f != LC_RET_OK) return f;
    if (!s->authorized || s->ctrl_unauthorized) return LC_RET_UNAUTHORIZED;
    uint32_t expected_len = s->delivered_type == LC_CTRL_V4_TYPE_ENABLE_SET
                                ? LC_CTRL_V4_ENABLE_RECEIPT_SIZE
                                : LC_CTRL_HEADER_SIZE;
    if (!buf || len != expected_len) {
        s->reply_wire_rejects++;
        return LC_RET_INVALID_ARGUMENT;
    }
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t expected_magic = s->delivered_magic;
    if ((lc_rd_u32(p + LC_CTRL_OFF_PROTO_MAGIC) != LC_CTRL_PROTO_MAGIC &&
         lc_rd_u32(p + LC_CTRL_OFF_PROTO_MAGIC) != LC_CTRL_V4_PROTO_MAGIC) ||
        lc_rd_u32(p + LC_CTRL_OFF_PROTO_MAGIC) != expected_magic ||
        p[LC_CTRL_OFF_HEADER_VERSION] != LC_CTRL_HEADER_VERSION ||
        p[LC_CTRL_OFF_HEADER_VERSION + 1u] != 0u ||
        p[LC_CTRL_OFF_MSG_TYPE] != LC_CTRL_TYPE_REPLY ||
        p[LC_CTRL_OFF_MSG_TYPE + 1u] != 0u ||
        lc_rd_u32(p + LC_CTRL_OFF_TOTAL_LEN) != len) {
        s->reply_wire_rejects++;
        return LC_RET_INVALID_ARGUMENT;
    }
    if (expected_len == LC_CTRL_V4_ENABLE_RECEIPT_SIZE &&
        (lc_rd_u32(p + 32u) != LC_CTRL_V4_RECEIPT_MAGIC ||
         lc_rd_u32(p + 36u) < 1u || lc_rd_u32(p + 36u) > LC_CTRL_V4_ENABLE_UNKNOWN ||
         lc_rd_u32(p + 40u) != lc_rd_u32(p + LC_CTRL_OFF_STATE_REVISION) ||
         lc_rd_u32(p + 44u) != 0u ||
         lc_rd_u32(p + LC_CTRL_OFF_FLAGS) > LC_CTRL_RESULT_FAILED_STORAGE_UNKNOWN)) {
        s->reply_wire_rejects++;
        return LC_RET_INVALID_ARGUMENT;
    }
    if (lc_rd_u32(p + LC_CTRL_OFF_SESSION_GENERATION) == 0u ||
        lc_rd_u32(p + LC_CTRL_OFF_REQUEST_ID) == 0u ||
        lc_rd_u32(p + LC_CTRL_OFF_CONFIG_GENERATION) == 0u) {
        s->reply_wire_rejects++;
        return LC_RET_INVALID_ARGUMENT;
    }
    if (!s->ctrl_delivered_valid ||
        lc_rd_u32(p + LC_CTRL_OFF_SESSION_GENERATION) != s->delivered_session ||
        lc_rd_u32(p + LC_CTRL_OFF_REQUEST_ID) != s->delivered_request) {
        s->reply_wire_rejects++;
        return LC_RET_INVALID_ARGUMENT;
    }
    if (len > s->control_max) return LC_RET_QUOTA_EXCEEDED;
    if (s->reply_count < LCSTUB_MAX_REPLIES) {
        memcpy(s->replies[s->reply_count].bytes, buf, len);
        s->replies[s->reply_count].present = 1;
        s->replies[s->reply_count].len = len;
        s->reply_count++;
    }
    return LC_RET_OK;
}

static int32_t fn_snapshot_publish(const void *buf, uint32_t len)
{
    lcstub_t *s = g_cur;
    if (!s) return -1;
    s->calls[LCSTUB_FN_SNAPSHOT_PUBLISH]++;
    int32_t f = take_fault(s, LCSTUB_FN_SNAPSHOT_PUBLISH);
    if (f != LC_RET_OK) return f;
    if (!s->authorized || s->ctrl_unauthorized) return LC_RET_UNAUTHORIZED;
    if (!buf || len < LC_CTRL_HEADER_SIZE || len > sizeof(s->snapshots[0].bytes)) {
        s->snapshot_wire_rejects++;
        return LC_RET_INVALID_ARGUMENT;
    }
    const uint8_t *p = (const uint8_t *)buf;
    if ((lc_rd_u32(p + LC_CTRL_OFF_PROTO_MAGIC) != LC_CTRL_PROTO_MAGIC &&
         lc_rd_u32(p + LC_CTRL_OFF_PROTO_MAGIC) != LC_CTRL_V4_PROTO_MAGIC) ||
        p[LC_CTRL_OFF_HEADER_VERSION] != LC_CTRL_HEADER_VERSION ||
        p[LC_CTRL_OFF_HEADER_VERSION + 1u] != 0u ||
        p[LC_CTRL_OFF_MSG_TYPE] != LC_CTRL_TYPE_SNAPSHOT ||
        p[LC_CTRL_OFF_MSG_TYPE + 1u] != 0u ||
        lc_rd_u32(p + LC_CTRL_OFF_TOTAL_LEN) != len ||
        lc_rd_u32(p + LC_CTRL_OFF_SESSION_GENERATION) != 0u ||
        lc_rd_u32(p + LC_CTRL_OFF_REQUEST_ID) != 0u ||
        lc_rd_u32(p + LC_CTRL_OFF_CONFIG_GENERATION) == 0u ||
        lc_rd_u32(p + LC_CTRL_OFF_FLAGS) != 0u) {
        s->snapshot_wire_rejects++;
        return LC_RET_INVALID_ARGUMENT;
    }
    if (!lcstub_snapshot_valid_json(p + LC_CTRL_HEADER_SIZE,
                                    len - LC_CTRL_HEADER_SIZE)) {
        s->snapshot_wire_rejects++;
        return LC_RET_INVALID_ARGUMENT;
    }
    if (len > s->snapshot_ram) return LC_RET_QUOTA_EXCEEDED;
    int idx = s->snapshot_slot % LCSTUB_MAX_SNAPSHOTS;
    s->snapshots[idx].present = 1;
    s->snapshots[idx].len = len;
    memcpy(s->snapshots[idx].bytes, buf, len);
    s->snapshot_slot++;
    if (s->snapshot_count < LCSTUB_MAX_SNAPSHOTS) s->snapshot_count++;
    return LC_RET_OK;
}

void lcstub_make_table_v3(lcstub_t *s, lc_app_api_v3_t *out)
{
    g_cur = s;
    memset(out, 0, sizeof(*out));
    out->table_size = LC_APP_API_V3_TABLE_SIZE;
    out->abi_version = LC_APP_ABI_V3;
    out->log = fn_log;
    out->tick_ms = fn_tick_ms;
    out->event_next = fn_event_next;
    out->model_meta = fn_model_meta;
    out->class_name = fn_class_name;
    out->report_submit = fn_report_submit;
    out->report_status = fn_report_status;
    out->state_read = fn_state_read;
    out->state_commit = fn_state_commit;
    out->should_stop = fn_should_stop;
    out->control_next = fn_control_next;
    out->control_reply = fn_control_reply;
    out->snapshot_publish = fn_snapshot_publish;
}

static int32_t fn_live_publish(const void *buf, uint32_t len)
{
    lcstub_t *s = g_cur;
    if (!s) return LC_RET_UNAUTHORIZED;
    s->calls[LCSTUB_FN_LIVE_PUBLISH]++;
    s->live_publish_calls++;
    if (!s || !s->authorized) return LC_RET_UNAUTHORIZED;
    if (s->live_publish_hook) {
        return s->live_publish_hook(s->live_publish_user, buf, len);
    }
    return LC_RET_OK;
}

void lcstub_make_table_v4(lcstub_t *s, lc_app_api_v4_t *out)
{
    lc_app_api_v3_t base;
    lcstub_make_table_v3(s, &base);
    memset(out, 0, sizeof(*out));
    memcpy(out, &base, sizeof(base));
    out->table_size = LC_APP_API_V4_TABLE_SIZE;
    out->abi_version = LC_APP_ABI_V4;
    out->live_publish = fn_live_publish;
}

uint32_t lcstub_make_command(uint8_t *out, uint32_t cap, uint32_t msg_type,
                             uint32_t session_gen, uint32_t request_id,
                             uint32_t expected_gen, uint32_t flags,
                             const void *payload, uint32_t payload_len)
{
    uint32_t total = LC_CTRL_HEADER_SIZE + payload_len;
    if (!out || cap < total || total > LC_CTRL_MSG_BUF_CAP) return 0;
    memset(out, 0, LC_CTRL_HEADER_SIZE);
    lcstub_wr32(out + LC_CTRL_OFF_PROTO_MAGIC, LC_CTRL_PROTO_MAGIC);
    out[LC_CTRL_OFF_HEADER_VERSION] = (uint8_t)LC_CTRL_HEADER_VERSION;
    out[LC_CTRL_OFF_MSG_TYPE] = (uint8_t)(msg_type & 0xFFu);
    out[LC_CTRL_OFF_MSG_TYPE + 1u] = (uint8_t)((msg_type >> 8) & 0xFFu);
    lcstub_wr32(out + LC_CTRL_OFF_TOTAL_LEN, total);
    lcstub_wr32(out + LC_CTRL_OFF_SESSION_GENERATION, session_gen);
    lcstub_wr32(out + LC_CTRL_OFF_REQUEST_ID, request_id);
    lcstub_wr32(out + LC_CTRL_OFF_CONFIG_GENERATION, expected_gen);
    lcstub_wr32(out + LC_CTRL_OFF_STATE_REVISION, 0u);
    lcstub_wr32(out + LC_CTRL_OFF_FLAGS, flags);
    if (payload_len) memcpy(out + LC_CTRL_HEADER_SIZE, payload, payload_len);
    return total;
}

void lcstub_push_command(lcstub_t *s, const void *bytes, uint32_t len)
{
    if (!bytes || len == 0 || len > LC_CTRL_MSG_BUF_CAP) return;
    if (s->cmd_count >= LCSTUB_MAX_COMMANDS) return;
    int slot = (s->cmd_head + s->cmd_count) % LCSTUB_MAX_COMMANDS;
    memcpy(s->commands[slot], bytes, len);
    s->command_len[slot] = len;
    s->cmd_count++;
}

void lcstub_set_control_capacity(lcstub_t *s, uint32_t control_max, uint32_t snapshot_ram)
{
    s->control_max = control_max;
    s->snapshot_ram = snapshot_ram;
}

int lcstub_reply_count(const lcstub_t *s) { return s->reply_count; }

const uint8_t *lcstub_reply(const lcstub_t *s, int index)
{
    if (index < 0 || index >= s->reply_count) return NULL;
    return s->replies[index].bytes;
}

int lcstub_snapshot_count(const lcstub_t *s)
{
    int last = (s->snapshot_slot - 1 + LCSTUB_MAX_SNAPSHOTS * 8) % LCSTUB_MAX_SNAPSHOTS;
    (void)last;
    return s->snapshot_slot;
}

const uint8_t *lcstub_snapshot(const lcstub_t *s, int index, uint32_t *len)
{
    if (index < 0 || index >= s->snapshot_slot || index < s->snapshot_slot - LCSTUB_MAX_SNAPSHOTS) {
        return NULL;
    }
    int idx = index % LCSTUB_MAX_SNAPSHOTS;
    if (!s->snapshots[idx].present) return NULL;
    if (len) *len = s->snapshots[idx].len;
    return s->snapshots[idx].bytes;
}
