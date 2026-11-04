#include <stdio.h>
#include <string.h>

#include "app_report_service.h"

typedef enum {
    JR_OBJECT,
    JR_ARRAY,
    JR_STRING,
    JR_NUMBER,
    JR_TRUE,
    JR_FALSE,
    JR_NULL
} jr_type_t;

typedef struct {
    size_t raw_off;
    const char *name;
    size_t name_len;
    size_t value_off;
    size_t value_end;
    jr_type_t type;
} jr_member_t;

#define JR_MAX_MEMBERS 256u
#define JR_MAX_DEPTH 64u

typedef struct {
    const char *buf;
    size_t len;
    size_t pos;
    uint32_t depth;
    jr_member_t *members;
    size_t member_cap;
    size_t member_count;
} jp_t;

typedef struct {
    uint8_t payload[APP_REPORT_SLOT_CAP];
    size_t payload_len;
    char app_id[APP_REPORT_APP_ID_SLOT + 1u];
    uint8_t key_sha[APP_REPORT_SHA256_LEN];
    app_report_policy_identity_t policy_identity;
    uint32_t report_seq;
    uint32_t policy_generation;
    uint32_t mqtt_state;
    uint32_t webhook_state;
    uint32_t mqtt_attempts;
    uint32_t webhook_attempts;
    uint32_t mqtt_wait;
    uint32_t webhook_wait;
    uint64_t accept_order;
    uint32_t session_generation;
    aicam_bool_t in_flight;
    aicam_bool_t mqtt_in_call;
    aicam_bool_t webhook_in_call;
    aicam_bool_t mqtt_begin;
    aicam_bool_t webhook_begin;
    aicam_bool_t in_use;
} report_entry_t;

typedef enum {
    APP_TX_UNCONFIGURED,
    APP_TX_WAITING,
    APP_TX_SEND_OK,
    APP_TX_SEND_FAIL,
    APP_TX_SEND_UNKNOWN,
    APP_TX_POLICY_CANCELLED,
    APP_TX_POLICY_UNKNOWN
} app_tx_outcome_t;

typedef struct {
    report_entry_t *entry;
    uint64_t accept_order;
    aicam_bool_t do_mqtt;
    aicam_bool_t do_webhook;
    app_tx_outcome_t mqtt_outcome;
    app_tx_outcome_t webhook_outcome;
} app_tx_item_t;

static const app_report_identity_t *g_identity;
static const app_report_transport_t *g_transport;
static app_report_session_t g_session;
static aicam_bool_t g_session_bound;
static app_report_policy_identity_t g_active_policy_identity;
static aicam_bool_t g_policy_identity_bound;
static aicam_bool_t g_policy_identity_pending;
static aicam_bool_t g_policy_storage_configured;
static int32_t g_policy_last_result;
static report_entry_t *g_transport_gate_entry;
static uint32_t g_transport_gate_channel;
static aicam_bool_t g_transport_gate_active;
static report_entry_t *g_transport_start_entry;
static uint32_t g_transport_start_channel;
static report_entry_t *g_transport_begin_entry;
static uint32_t g_transport_begin_channel;
static app_report_policy_identity_t g_purge_identity;
static uint64_t g_purge_intent;
static uint32_t g_purge_state;
static uint32_t g_purge_entries;
static aicam_bool_t g_purge_identity_bound;
static app_report_policy_identity_t g_trusted_uninstall_identity;
static aicam_bool_t g_trusted_uninstall_bound;
static uint32_t g_boot_id;
static aicam_bool_t g_boot_ready;
static report_entry_t g_queue[APP_REPORT_QUEUE_SLOTS];
static app_tx_item_t g_tx[APP_REPORT_QUEUE_SLOTS];
static aicam_bool_t g_tx_active;
static uint64_t g_accept_counter;
static uint32_t g_seen_seq[APP_REPORT_SEEN_RING];
static uint8_t g_seen_key[APP_REPORT_SEEN_RING][APP_REPORT_SHA256_LEN];
static char g_seen_publisher[APP_REPORT_SEEN_RING][APP_REPORT_PUBLISHER_ID_SLOT + 1u];
static char g_seen_app[APP_REPORT_SEEN_RING][APP_REPORT_APP_ID_SLOT + 1u];
static uint32_t g_seen_gen[APP_REPORT_SEEN_RING];
static uint32_t g_seen_head;
static uint32_t g_restart_gen;
static aicam_bool_t g_seen_used[APP_REPORT_SEEN_RING];
static app_report_stats_t g_stats;
static void (*g_lock_enter)(void);
static void (*g_lock_exit)(void);
static uint32_t g_session_generation;

static void lock_enter(void)
{
    if (g_lock_enter != NULL) {
        g_lock_enter();
    }
}

static void lock_exit(void)
{
    if (g_lock_exit != NULL) {
        g_lock_exit();
    }
}

static int policy_identity_base_matches_session(
    const app_report_policy_identity_t *identity,
    const app_report_session_t *session)
{
    return strcmp(identity->publisher_id, session->publisher_id) == 0 &&
           strcmp(identity->app_id, session->app_id) == 0 &&
           memcmp(identity->publisher_key_sha256,
                  session->publisher_key_sha256, APP_REPORT_SHA256_LEN) == 0;
}

static int policy_identity_valid(const app_report_policy_identity_t *identity)
{
    uint8_t tbs_or = 0u;

    if (identity == NULL || identity->publisher_id[0] == '\0' ||
        identity->app_id[0] == '\0' ||
        memchr(identity->publisher_id, '\0', sizeof(identity->publisher_id)) == NULL ||
        memchr(identity->app_id, '\0', sizeof(identity->app_id)) == NULL) {
        return 0;
    }
    for (uint32_t i = 0u; i < APP_REPORT_SHA256_LEN; i++) {
        tbs_or |= identity->native_tbs_sha256[i];
    }
    return (tbs_or != 0u) ? 1 : 0;
}

static int policy_identity_equal(const app_report_policy_identity_t *a,
                                 const app_report_policy_identity_t *b)
{
    return strcmp(a->publisher_id, b->publisher_id) == 0 &&
           strcmp(a->app_id, b->app_id) == 0 &&
           memcmp(a->publisher_key_sha256, b->publisher_key_sha256,
                  APP_REPORT_SHA256_LEN) == 0 &&
           memcmp(a->native_tbs_sha256, b->native_tbs_sha256,
                  APP_REPORT_SHA256_LEN) == 0;
}

static int policy_identity_base_equal(const app_report_policy_identity_t *a,
                                      const app_report_policy_identity_t *b)
{
    return strcmp(a->publisher_id, b->publisher_id) == 0 &&
           strcmp(a->app_id, b->app_id) == 0 &&
           memcmp(a->publisher_key_sha256, b->publisher_key_sha256,
                  APP_REPORT_SHA256_LEN) == 0;
}

static const app_install_io_t *g_committed_io;

static int committed_identity_matches_locked(
    const app_report_policy_identity_t *identity)
{
    app_install_record_t record;
    aicam_bool_t present = AICAM_FALSE;
    app_install_result_t rc;

    if (g_committed_io == NULL) {
        return -1;
    }
    rc = app_install_query_committed(g_committed_io, &record, &present);
    if (rc != APP_INSTALL_OK) {
        return -1;
    }
    if (present != AICAM_TRUE) {
        return 0;
    }
    if (strcmp(record.publisher_id, identity->publisher_id) != 0 ||
        strcmp(record.app_id, identity->app_id) != 0 ||
        memcmp(record.publisher_key_sha256, identity->publisher_key_sha256,
               APP_REPORT_SHA256_LEN) != 0 ||
        memcmp(record.tbs_sha256, identity->native_tbs_sha256,
               APP_REPORT_SHA256_LEN) != 0) {
        return 0;
    }
    return 1;
}

static int committed_base_matches_locked(const app_report_session_t *session)
{
    app_install_record_t record;
    aicam_bool_t present = AICAM_FALSE;
    app_install_result_t rc;

    if (g_committed_io == NULL) {
        return -1;
    }
    rc = app_install_query_committed(g_committed_io, &record, &present);
    if (rc != APP_INSTALL_OK) {
        return -1;
    }
    if (present != AICAM_TRUE) {
        return 0;
    }
    if (strcmp(record.publisher_id, session->publisher_id) != 0 ||
        strcmp(record.app_id, session->app_id) != 0 ||
        memcmp(record.publisher_key_sha256, session->publisher_key_sha256,
               APP_REPORT_SHA256_LEN) != 0) {
        return 0;
    }
    return 1;
}

static int committed_tbs_for_session_locked(const app_report_session_t *session,
                                            uint8_t out_tbs[APP_REPORT_SHA256_LEN])
{
    app_install_record_t record;
    aicam_bool_t present = AICAM_FALSE;
    app_install_result_t rc;

    if (g_committed_io == NULL) {
        return -1;
    }
    rc = app_install_query_committed(g_committed_io, &record, &present);
    if (rc != APP_INSTALL_OK || present != AICAM_TRUE) {
        return -1;
    }
    if (strcmp(record.publisher_id, session->publisher_id) != 0 ||
        strcmp(record.app_id, session->app_id) != 0 ||
        memcmp(record.publisher_key_sha256, session->publisher_key_sha256,
               APP_REPORT_SHA256_LEN) != 0) {
        return -1;
    }
    memcpy(out_tbs, record.tbs_sha256, APP_REPORT_SHA256_LEN);
    return 0;
}

static void policy_identity_from_session(const app_report_session_t *session,
                                         app_report_policy_identity_t *out)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->publisher_id, session->publisher_id,
           sizeof(out->publisher_id));
    memcpy(out->app_id, session->app_id, sizeof(out->app_id));
    memcpy(out->publisher_key_sha256, session->publisher_key_sha256,
           APP_REPORT_SHA256_LEN);
}

static int entry_matches_policy_identity(const report_entry_t *entry,
                                         const app_report_policy_identity_t *identity)
{
    return policy_identity_equal(&entry->policy_identity, identity);
}

static int entry_matches_purge(const report_entry_t *entry,
                               const app_report_policy_identity_t *identity)
{
    if (entry->in_use != AICAM_TRUE || identity == NULL) {
        return 0;
    }
    return entry_matches_policy_identity(entry, identity);
}

static int entry_is_unbound(const report_entry_t *entry)
{
    uint8_t tbs_or = 0u;

    for (uint32_t i = 0u; i < APP_REPORT_SHA256_LEN; i++) {
        tbs_or |= entry->policy_identity.native_tbs_sha256[i];
    }
    return (tbs_or == 0u) ? 1 : 0;
}

static int entry_purgeable(const report_entry_t *entry,
                           const app_report_policy_identity_t *identity,
                           aicam_bool_t admin_intent)
{
    if (entry->in_use != AICAM_TRUE || identity == NULL) {
        return 0;
    }
    if (entry_matches_policy_identity(entry, identity) == 1) {
        return 1;
    }
    if (policy_identity_base_equal(&entry->policy_identity, identity) == 0) {
        return 0;
    }
    if (entry_is_unbound(entry) != 1 ||
        entry->session_generation != g_session_generation) {
        return 0;
    }
    if (g_policy_identity_bound == AICAM_TRUE &&
        policy_identity_equal(&g_active_policy_identity, identity)) {
        return 1;
    }
    if (admin_intent == AICAM_TRUE &&
        g_trusted_uninstall_bound == AICAM_TRUE &&
        policy_identity_equal(&g_trusted_uninstall_identity, identity)) {
        return 1;
    }
    return 0;
}

static uint32_t purge_blocked_count(const app_report_policy_identity_t *identity,
                                    aicam_bool_t admin_intent)
{
    uint32_t blocked = 0u;

    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        const report_entry_t *entry = &g_queue[i];

        if (entry->in_use != AICAM_TRUE ||
            entry_purgeable(entry, identity, admin_intent) == 1 ||
            policy_identity_base_equal(&entry->policy_identity, identity) == 0) {
            continue;
        }
        if (entry_is_unbound(entry) == 1) {
            blocked++;
        }
    }
    return blocked;
}

static int entry_has_pending(const report_entry_t *entry)
{
    return entry->mqtt_state == APP_REPORT_MQTT_PENDING ||
           entry->webhook_state == APP_REPORT_WEBHOOK_PENDING;
}

static int32_t policy_values_for_identity(
    const app_report_policy_identity_t *identity,
    app_report_policy_values_t *values, uint32_t *revision);

static int32_t policy_values_for_session(
    const app_report_session_t *session,
    app_report_policy_identity_t *identity,
    app_report_policy_values_t *values, uint32_t *revision)
{
    aicam_bool_t base_found = AICAM_FALSE;
    app_report_policy_identity_t probe;
    int committed_match;
    int32_t rc;

    policy_identity_from_session(session, identity);
    values->mqtt_enabled = AICAM_TRUE;
    values->webhook_enabled = AICAM_TRUE;
    values->backlog_capacity = APP_REPORT_POLICY_DEFAULT_CAPACITY;
    *revision = 0u;
    if (g_policy_storage_configured != AICAM_TRUE) {
        return APP_REPORT_ABI_OK;
    }
    if (g_policy_identity_pending == AICAM_TRUE) {
        return APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    if (g_policy_identity_bound == AICAM_TRUE) {
        if (!policy_identity_base_matches_session(&g_active_policy_identity, session)) {
            return APP_REPORT_ABI_UNAUTHORIZED;
        }
        committed_match = committed_identity_matches_locked(&g_active_policy_identity);
        if (committed_match == 1) {
            *identity = g_active_policy_identity;
            return policy_values_for_identity(identity, values, revision);
        }
        return (committed_match == 0) ? APP_REPORT_ABI_UNAUTHORIZED :
               APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    rc = committed_base_matches_locked(session);
    if (rc < 0) {
        return APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    if (rc == 0) {
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    probe = *identity;
    memset(probe.native_tbs_sha256, 1, sizeof(probe.native_tbs_sha256));
    rc = app_report_policy_store_has_base(&probe, &base_found);
    if (rc != APP_REPORT_ABI_OK) {
        return rc;
    }
    if (base_found == AICAM_TRUE) {
        rc = policy_values_for_identity(identity, values, revision);
        if (rc != APP_REPORT_ABI_OK) {
            return rc;
        }
        g_active_policy_identity = *identity;
        g_policy_identity_bound = AICAM_TRUE;
        g_policy_identity_pending = AICAM_FALSE;
    }
    return APP_REPORT_ABI_OK;
}

static uint32_t policy_backlog_used(const app_report_policy_identity_t *identity)
{
    uint32_t used = 0u;

    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        const report_entry_t *entry = &g_queue[i];
        if (entry->in_use == AICAM_TRUE && entry_has_pending(entry) &&
            entry_matches_policy_identity(entry, identity)) {
            used++;
        }
    }
    return used;
}

static int32_t policy_values_for_identity(
    const app_report_policy_identity_t *identity,
    app_report_policy_values_t *values, uint32_t *revision)
{
    int32_t rc;

    values->mqtt_enabled = AICAM_TRUE;
    values->webhook_enabled = AICAM_TRUE;
    values->backlog_capacity = APP_REPORT_POLICY_DEFAULT_CAPACITY;
    *revision = 0u;
    if (g_policy_storage_configured != AICAM_TRUE) {
        return APP_REPORT_ABI_OK;
    }
    rc = app_report_policy_store_get(identity, values, revision);
    if (rc == APP_REPORT_ABI_NOT_FOUND) {
        return APP_REPORT_ABI_OK;
    }
    return rc;
}

static void policy_status_fill(const app_report_policy_identity_t *identity,
                               const app_report_policy_values_t *values,
                               uint32_t revision,
                               app_report_policy_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->mqtt_enabled = values->mqtt_enabled;
    out->webhook_enabled = values->webhook_enabled;
    out->backlog_capacity = values->backlog_capacity;
    out->capacity_min = APP_REPORT_POLICY_MIN_CAPACITY;
    out->capacity_max = APP_REPORT_POLICY_MAX_CAPACITY;
    out->backlog_used = policy_backlog_used(identity);
    out->revision = revision;
    out->policy_generation = revision;
    out->purge_state = APP_REPORT_POLICY_PURGE_NONE;
    out->last_result = g_policy_last_result;
    out->storage_verified = g_policy_storage_configured;
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        const report_entry_t *entry = &g_queue[i];
        if (entry->in_use != AICAM_TRUE ||
            !entry_matches_policy_identity(entry, identity)) {
            continue;
        }
        if (entry->mqtt_state == APP_REPORT_MQTT_CANCELLED) {
            out->mqtt_cancelled++;
        }
        if (entry->webhook_state == APP_REPORT_WEBHOOK_CANCELLED) {
            out->webhook_cancelled++;
        }
    }
}

static void jp_ws(jp_t *j)
{
    while (j->pos < j->len) {
        char c = j->buf[j->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            j->pos++;
        } else {
            break;
        }
    }
}

static int jp_hex4(jp_t *j)
{
    for (size_t k = 0u; k < 4u; k++) {
        char c;
        if (j->pos >= j->len) {
            return -1;
        }
        c = j->buf[j->pos];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return -1;
        }
        j->pos++;
    }
    return 0;
}

static int jp_string(jp_t *j)
{
    if (j->pos >= j->len || j->buf[j->pos] != '"') {
        return -1;
    }
    j->pos++;
    while (j->pos < j->len) {
        unsigned char c = (unsigned char)j->buf[j->pos];
        if (c == '"') {
            j->pos++;
            return 0;
        }
        if (c < 0x20u) {
            return -1;
        }
        if (c == '\\') {
            char e;
            j->pos++;
            if (j->pos >= j->len) {
                return -1;
            }
            e = j->buf[j->pos];
            if (e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f' ||
                e == 'n' || e == 'r' || e == 't') {
                j->pos++;
                continue;
            }
            if (e == 'u') {
                j->pos++;
                if (jp_hex4(j) != 0) {
                    return -1;
                }
                continue;
            }
            return -1;
        }
        j->pos++;
    }
    return -1;
}

static int jp_literal(jp_t *j, const char *word, size_t n)
{
    if (j->pos + n > j->len || memcmp(j->buf + j->pos, word, n) != 0) {
        return -1;
    }
    j->pos += n;
    return 0;
}

static int jp_number(jp_t *j)
{
    size_t start = j->pos;

    if (j->pos < j->len && j->buf[j->pos] == '-') {
        j->pos++;
    }
    if (j->pos >= j->len) {
        return -1;
    }
    if (j->buf[j->pos] == '0') {
        j->pos++;
    } else if (j->buf[j->pos] >= '1' && j->buf[j->pos] <= '9') {
        while (j->pos < j->len && j->buf[j->pos] >= '0' && j->buf[j->pos] <= '9') {
            j->pos++;
        }
    } else {
        return -1;
    }
    if (j->pos < j->len && j->buf[j->pos] == '.') {
        j->pos++;
        if (j->pos >= j->len || j->buf[j->pos] < '0' || j->buf[j->pos] > '9') {
            return -1;
        }
        while (j->pos < j->len && j->buf[j->pos] >= '0' && j->buf[j->pos] <= '9') {
            j->pos++;
        }
    }
    if (j->pos < j->len && (j->buf[j->pos] == 'e' || j->buf[j->pos] == 'E')) {
        j->pos++;
        if (j->pos < j->len && (j->buf[j->pos] == '+' || j->buf[j->pos] == '-')) {
            j->pos++;
        }
        if (j->pos >= j->len || j->buf[j->pos] < '0' || j->buf[j->pos] > '9') {
            return -1;
        }
        while (j->pos < j->len && j->buf[j->pos] >= '0' && j->buf[j->pos] <= '9') {
            j->pos++;
        }
    }
    return (j->pos > start) ? 0 : -1;
}

static int jp_value(jp_t *j);
static int jp_object(jp_t *j);
static int jp_array(jp_t *j);

static int jp_value(jp_t *j)
{
    char c;

    jp_ws(j);
    if (j->pos >= j->len) {
        return -1;
    }
    c = j->buf[j->pos];
    if (c == '{') {
        return jp_object(j);
    }
    if (c == '[') {
        return jp_array(j);
    }
    if (c == '"') {
        return jp_string(j);
    }
    if (c == 't') {
        return jp_literal(j, "true", 4u);
    }
    if (c == 'f') {
        return jp_literal(j, "false", 5u);
    }
    if (c == 'n') {
        return jp_literal(j, "null", 4u);
    }
    return jp_number(j);
}

static int jp_object(jp_t *j)
{
    jp_ws(j);
    if (j->pos >= j->len || j->buf[j->pos] != '{') {
        return -1;
    }
    j->pos++;
    j->depth++;
    if (j->depth > JR_MAX_DEPTH) {
        j->depth--;
        return -1;
    }
    jp_ws(j);
    if (j->pos < j->len && j->buf[j->pos] == '}') {
        j->pos++;
        j->depth--;
        return 0;
    }
    for (;;) {
        size_t name_off;
        size_t name_end;
        size_t value_off;
        size_t value_end;
        char c;
        jr_type_t type;

        jp_ws(j);
        if (j->pos >= j->len || j->buf[j->pos] != '"') {
            j->depth--;
            return -1;
        }
        name_off = j->pos;
        if (jp_string(j) != 0) {
            j->depth--;
            return -1;
        }
        name_end = j->pos - 1u;
        jp_ws(j);
        if (j->pos >= j->len || j->buf[j->pos] != ':') {
            j->depth--;
            return -1;
        }
        j->pos++;
        jp_ws(j);
        value_off = j->pos;
        c = (j->pos < j->len) ? j->buf[j->pos] : '\0';
        type = JR_NUMBER;
        if (c == '{') {
            type = JR_OBJECT;
        } else if (c == '[') {
            type = JR_ARRAY;
        } else if (c == '"') {
            type = JR_STRING;
        } else if (c == 't') {
            type = JR_TRUE;
        } else if (c == 'f') {
            type = JR_FALSE;
        } else if (c == 'n') {
            type = JR_NULL;
        }
        if (jp_value(j) != 0) {
            j->depth--;
            return -1;
        }
        value_end = j->pos;
        if (j->depth == 1u) {
            if (j->member_count < j->member_cap) {
                jr_member_t *m = &j->members[j->member_count];
                m->raw_off = name_off;
                m->name = j->buf + name_off + 1u;
                m->name_len = name_end - name_off - 1u;
                m->value_off = value_off;
                m->value_end = value_end;
                m->type = type;
                j->member_count++;
            } else {
                j->depth--;
                return -1;
            }
        }
        jp_ws(j);
        if (j->pos >= j->len) {
            j->depth--;
            return -1;
        }
        if (j->buf[j->pos] == ',') {
            j->pos++;
            continue;
        }
        if (j->buf[j->pos] == '}') {
            j->pos++;
            j->depth--;
            return 0;
        }
        j->depth--;
        return -1;
    }
}

static int jp_array(jp_t *j)
{
    jp_ws(j);
    if (j->pos >= j->len || j->buf[j->pos] != '[') {
        return -1;
    }
    j->pos++;
    j->depth++;
    if (j->depth > JR_MAX_DEPTH) {
        j->depth--;
        return -1;
    }
    jp_ws(j);
    if (j->pos < j->len && j->buf[j->pos] == ']') {
        j->pos++;
        j->depth--;
        return 0;
    }
    for (;;) {
        if (jp_value(j) != 0) {
            j->depth--;
            return -1;
        }
        jp_ws(j);
        if (j->pos >= j->len) {
            j->depth--;
            return -1;
        }
        if (j->buf[j->pos] == ',') {
            j->pos++;
            continue;
        }
        if (j->buf[j->pos] == ']') {
            j->pos++;
            j->depth--;
            return 0;
        }
        j->depth--;
        return -1;
    }
}

static int jr_scan_top(const char *buf, size_t len,
                       jr_member_t *members, size_t max, size_t *out_count)
{
    jp_t j;

    j.buf = buf;
    j.len = len;
    j.pos = 0u;
    j.depth = 0u;
    j.members = members;
    j.member_cap = max;
    j.member_count = 0u;
    if (jp_value(&j) != 0) {
        return -1;
    }
    jp_ws(&j);
    if (j.pos != len) {
        return -1;
    }
    if (j.member_count == 0u) {
        return -1;
    }
    *out_count = j.member_count;
    return 0;
}

static int jr_name_is(const jr_member_t *m, const char *name)
{
    size_t n = strlen(name);

    return (m->name_len == n && memcmp(m->name, name, n) == 0);
}

static int jr_find(const jr_member_t *members, size_t count, const char *name,
                   const jr_member_t **out)
{
    for (size_t i = 0u; i < count; i++) {
        if (jr_name_is(&members[i], name)) {
            *out = &members[i];
            return 0;
        }
    }
    return -1;
}

static int jr_duplicates(const jr_member_t *members, size_t count)
{
    for (size_t i = 0u; i < count; i++) {
        for (size_t k = (size_t)(i + 1u); k < count; k++) {
            if (members[i].name_len == members[k].name_len &&
                memcmp(members[i].name, members[k].name, members[i].name_len) == 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int jr_u32_value(const jr_member_t *m, const char *buf, uint32_t *out)
{
    size_t i = m->value_off;
    size_t end = m->value_end;
    uint64_t v = 0;

    if (m->type != JR_NUMBER) {
        return -1;
    }
    if (i < end && buf[i] == '-') {
        return -1;
    }
    for (; i < end; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            return -1;
        }
        v = v * 10u + (uint64_t)(buf[i] - '0');
        if (v > 0xFFFFFFFFull) {
            return -1;
        }
    }
    *out = (uint32_t)v;
    return 0;
}

static int jr_object_member(const jr_member_t *members, size_t count,
                            const char *name)
{
    const jr_member_t *m;

    if (jr_find(members, count, name, &m) != 0) {
        return -1;
    }
    return (m->type == JR_OBJECT) ? 0 : -1;
}

static int jr_string_equals(const jr_member_t *m, const char *buf, const char *s)
{
    size_t n = strlen(s);

    if (m->type != JR_STRING) {
        return 0;
    }
    if (m->value_end - m->value_off != n + 2u) {
        return 0;
    }
    return (memcmp(buf + m->value_off + 1u, s, n) == 0) ? 1 : 0;
}

static int identity_device_id(char *out, uint32_t cap)
{
    if (g_identity == NULL || g_identity->device_id == NULL) {
        return -1;
    }
    return (g_identity->device_id(g_identity->user, out, cap) == AICAM_TRUE) ? 0 : -1;
}

static int device_id_safe(const char *s, size_t n)
{
    if (n == 0u || n > 24u) {
        return -1;
    }
    for (size_t i = 0u; i < n; i++) {
        char c = s[i];
        int ok = ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F') || c == ':' || c == '-' ||
                  c == '.' || c == '_');
        if (!ok) {
            return -1;
        }
    }
    return 0;
}

static int session_matches(const app_report_session_t *s)
{
    if (s == NULL || g_session_bound != AICAM_TRUE || g_session.active != AICAM_TRUE) {
        return -1;
    }
    if (s->active != AICAM_TRUE || s->report_max == 0u) {
        return -1;
    }
    if (strncmp(s->publisher_id, g_session.publisher_id,
                APP_REPORT_PUBLISHER_ID_SLOT + 1u) != 0) {
        return -1;
    }
    if (strncmp(s->app_id, g_session.app_id, APP_REPORT_APP_ID_SLOT + 1u) != 0) {
        return -1;
    }
    if (memcmp(s->publisher_key_sha256, g_session.publisher_key_sha256,
               APP_REPORT_SHA256_LEN) != 0) {
        return -1;
    }
    if (s->report_max != g_session.report_max) {
        return -1;
    }
    return 0;
}

static int seen_find(const char *publisher_id, const uint8_t *key_sha,
                     const char *app_id, uint32_t seq)
{
    for (uint32_t i = 0u; i < APP_REPORT_SEEN_RING; i++) {
        if (g_seen_used[i] == AICAM_TRUE && g_seen_seq[i] == seq &&
            strcmp(g_seen_publisher[i], publisher_id) == 0 &&
            strcmp(g_seen_app[i], app_id) == 0 &&
            memcmp(g_seen_key[i], key_sha, APP_REPORT_SHA256_LEN) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int seen_contains(const char *publisher_id, const uint8_t *key_sha,
                         const char *app_id, uint32_t seq)
{
    return (seen_find(publisher_id, key_sha, app_id, seq) >= 0) ? 1 : 0;
}

static void seen_push(const char *publisher_id, const uint8_t *key_sha,
                      const char *app_id, uint32_t seq)
{
    uint32_t slot = g_seen_head;

    g_seen_head = (g_seen_head + 1u) % APP_REPORT_SEEN_RING;
    strncpy(g_seen_publisher[slot], publisher_id, APP_REPORT_PUBLISHER_ID_SLOT);
    g_seen_publisher[slot][APP_REPORT_PUBLISHER_ID_SLOT] = '\0';
    memcpy(g_seen_key[slot], key_sha, APP_REPORT_SHA256_LEN);
    strncpy(g_seen_app[slot], app_id, APP_REPORT_APP_ID_SLOT);
    g_seen_app[slot][APP_REPORT_APP_ID_SLOT] = '\0';
    g_seen_seq[slot] = seq;
    g_seen_gen[slot] = g_restart_gen;
    g_seen_used[slot] = AICAM_TRUE;
}

static int32_t entry_accept(const app_report_session_t *s,
                            const app_report_policy_identity_t *policy_identity,
                            const app_report_policy_values_t *policy_values,
                            uint32_t policy_generation,
                            const char *normalized, size_t normalized_len,
                            uint32_t seq)
{
    report_entry_t *e = NULL;
    uint32_t free_slot = APP_REPORT_QUEUE_SLOTS;
    uint32_t evict_slot = APP_REPORT_QUEUE_SLOTS;
    uint64_t oldest = 0;

    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        if (g_queue[i].in_use != AICAM_TRUE) {
            if (free_slot == APP_REPORT_QUEUE_SLOTS) {
                free_slot = i;
            }
            continue;
        }
        if (g_queue[i].in_flight == AICAM_TRUE) {
            continue;
        }
        if (!entry_has_pending(&g_queue[i]) &&
            entry_matches_policy_identity(&g_queue[i], policy_identity)) {
            if (evict_slot == APP_REPORT_QUEUE_SLOTS ||
                g_queue[i].accept_order < oldest) {
                evict_slot = i;
                oldest = g_queue[i].accept_order;
            }
        }
    }

    if (free_slot != APP_REPORT_QUEUE_SLOTS) {
        e = &g_queue[free_slot];
    } else if (evict_slot != APP_REPORT_QUEUE_SLOTS) {
        e = &g_queue[evict_slot];
    }
    if (e == NULL) {
        return APP_REPORT_ABI_BUSY;
    }
    if (policy_backlog_used(policy_identity) >= policy_values->backlog_capacity) {
        return APP_REPORT_ABI_QUOTA_EXCEEDED;
    }

    memset(e, 0, sizeof(*e));
    memcpy(e->payload, normalized, normalized_len);
    e->payload_len = normalized_len;
    strncpy(e->app_id, s->app_id, APP_REPORT_APP_ID_SLOT);
    e->app_id[APP_REPORT_APP_ID_SLOT] = '\0';
    memcpy(e->key_sha, s->publisher_key_sha256, APP_REPORT_SHA256_LEN);
    e->policy_identity = *policy_identity;
    e->report_seq = seq;
    e->policy_generation = policy_generation;
    e->mqtt_state = (policy_values->mqtt_enabled == AICAM_TRUE) ?
                    APP_REPORT_MQTT_PENDING : APP_REPORT_MQTT_DISABLED;
    e->webhook_state = (policy_values->webhook_enabled == AICAM_TRUE) ?
                       APP_REPORT_WEBHOOK_PENDING : APP_REPORT_WEBHOOK_DISABLED;
    e->accept_order = ++g_accept_counter;
    e->session_generation = g_session_generation;
    e->in_use = AICAM_TRUE;
    return APP_REPORT_ABI_OK;
}

static int32_t report_validate_and_normalize(const char *json, uint32_t len,
                                             uint32_t app_report_seq,
                                             char *normalized, size_t cap,
                                             size_t *normalized_len)
{
    jr_member_t members[JR_MAX_MEMBERS];
    size_t count = 0;
    const jr_member_t *m_schema = NULL;
    const jr_member_t *m_type = NULL;
    const jr_member_t *m_device = NULL;
    const jr_member_t *m_boot = NULL;
    const jr_member_t *m_seq = NULL;
    uint32_t value32 = 0;
    char device_id[APP_REPORT_DEVICE_ID_CAP];
    char boot_text[11];
    size_t device_id_len;
    size_t out_pos = 0;
    int boot_len;

    if (jr_scan_top(json, len, members, JR_MAX_MEMBERS, &count) != 0) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    if (jr_duplicates(members, count) != 0) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    if (jr_find(members, count, "schema_version", &m_schema) != 0 ||
        jr_find(members, count, "type", &m_type) != 0 ||
        jr_find(members, count, "report_seq", &m_seq) != 0) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    if (m_schema->type != JR_NUMBER || jr_u32_value(m_schema, json, &value32) != 0) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    if (value32 != 1u) {
        return APP_REPORT_ABI_INCOMPATIBLE;
    }
    if (m_type->type != JR_STRING) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    if (jr_string_equals(m_type, json, APP_REPORT_TYPE_LINE_COUNTING) == 0) {
        return APP_REPORT_ABI_INCOMPATIBLE;
    }
    if (jr_u32_value(m_seq, json, &value32) != 0 || value32 != app_report_seq) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    if (jr_find(members, count, "device_id", &m_device) == 0 &&
        m_device->type != JR_STRING) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    if (jr_find(members, count, "boot_id", &m_boot) == 0 &&
        jr_u32_value(m_boot, json, &value32) != 0) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    if (jr_object_member(members, count, "window") != 0 ||
        jr_object_member(members, count, "total") != 0 ||
        jr_object_member(members, count, "counter") != 0 ||
        jr_object_member(members, count, "target") != 0 ||
        jr_object_member(members, count, "model") != 0) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }

    if (identity_device_id(device_id, (uint32_t)sizeof(device_id)) != 0) {
        return APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    device_id_len = strlen(device_id);
    if (device_id_safe(device_id, device_id_len) != 0) {
        return APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    if (g_boot_ready != AICAM_TRUE || g_boot_id == 0u) {
        return APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    boot_len = snprintf(boot_text, sizeof(boot_text), "%u",
                        (unsigned int)g_boot_id);
    if (boot_len <= 0 || (size_t)boot_len >= sizeof(boot_text)) {
        return APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    if (len + 64u > cap) {
        return APP_REPORT_ABI_QUOTA_EXCEEDED;
    }
    {
        size_t type_idx = (size_t)(m_type - members);
        size_t boot_idx = (m_boot != NULL) ? (size_t)(m_boot - members) : type_idx;
        int insert_device = (m_device == NULL);
        int insert_boot = (m_boot == NULL);

        normalized[out_pos] = '{';
        out_pos += 1u;
        for (size_t i = 0u; i < count; i++) {
            const jr_member_t *m = &members[i];
            if (i > 0u) {
                normalized[out_pos] = ',';
                out_pos += 1u;
            }
            if (m == m_device) {
                memcpy(normalized + out_pos, json + m->raw_off,
                       m->value_off - m->raw_off);
                out_pos += m->value_off - m->raw_off;
                normalized[out_pos] = '"';
                memcpy(normalized + out_pos + 1u, device_id, device_id_len);
                normalized[out_pos + 1u + device_id_len] = '"';
                out_pos += device_id_len + 2u;
            } else if (m == m_boot) {
                memcpy(normalized + out_pos, json + m->raw_off,
                       m->value_off - m->raw_off);
                out_pos += m->value_off - m->raw_off;
                memcpy(normalized + out_pos, boot_text, (size_t)boot_len);
                out_pos += (size_t)boot_len;
            } else {
                memcpy(normalized + out_pos, json + m->raw_off,
                       m->value_end - m->raw_off);
                out_pos += m->value_end - m->raw_off;
            }
            if (insert_device == 1 && i == type_idx) {
                memcpy(normalized + out_pos, ",\"device_id\":\"", 14u);
                out_pos += 1u;
                memcpy(normalized + out_pos + 13u, device_id, device_id_len);
                normalized[out_pos + 13u + device_id_len] = '"';
                out_pos += 13u + device_id_len + 1u;
                insert_device = 0;
            }
            if (insert_boot == 1 && i == boot_idx) {
                memcpy(normalized + out_pos, ",\"boot_id\":", 11u);
                out_pos += 1u;
                memcpy(normalized + out_pos + 10u, boot_text, (size_t)boot_len);
                out_pos += 10u + (size_t)boot_len;
                insert_boot = 0;
            }
        }
        normalized[out_pos] = '}';
        out_pos += 1u;
    }
    *normalized_len = out_pos;
    return APP_REPORT_ABI_OK;
}

int32_t app_report_submit(const app_report_session_t *session,
                          const void *json, uint32_t len,
                          uint32_t app_report_seq, uint32_t *out_host_boot_id)
{
    static char normalized[APP_REPORT_SLOT_CAP];
    app_report_policy_identity_t policy_identity;
    app_report_policy_values_t policy_values;
    uint32_t policy_revision = 0u;
    size_t normalized_len = 0;
    int32_t rc;

    if (out_host_boot_id == NULL) {
        g_stats.rejected_param++;
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    *out_host_boot_id = 0u;

    lock_enter();
    if (session_matches(session) != 0) {
        g_stats.rejected_unauthorized++;
        lock_exit();
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    rc = policy_values_for_session(session, &policy_identity, &policy_values,
                                   &policy_revision);
    if (rc != APP_REPORT_ABI_OK) {
        g_stats.rejected_identity++;
        g_policy_last_result = rc;
        lock_exit();
        return rc;
    }
    if (g_purge_identity_bound == AICAM_TRUE &&
        (g_purge_state == APP_REPORT_POLICY_PURGE_PENDING ||
         g_purge_state == APP_REPORT_POLICY_PURGE_UNKNOWN) &&
        policy_identity_base_equal(&policy_identity, &g_purge_identity)) {
        g_stats.rejected_unauthorized++;
        lock_exit();
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    if (json == NULL || len == 0u || memchr(json, '\0', (size_t)len) != NULL) {
        g_stats.rejected_param++;
        lock_exit();
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    if ((uint64_t)len > (uint64_t)session->report_max ||
        (uint64_t)len > (uint64_t)APP_REPORT_INPUT_MAX) {
        g_stats.rejected_quota++;
        lock_exit();
        return APP_REPORT_ABI_QUOTA_EXCEEDED;
    }
    if (seen_contains(session->publisher_id, session->publisher_key_sha256,
                      session->app_id, app_report_seq)) {
        g_stats.dedup_hits++;
        *out_host_boot_id = g_boot_id;
        lock_exit();
        return APP_REPORT_ABI_OK;
    }

    rc = report_validate_and_normalize((const char *)json, len, app_report_seq,
                                       normalized, sizeof(normalized),
                                       &normalized_len);
    if (rc == APP_REPORT_ABI_QUOTA_EXCEEDED) {
        g_stats.rejected_quota++;
        lock_exit();
        return rc;
    }
    if (rc == APP_REPORT_ABI_STORAGE_UNKNOWN) {
        g_stats.rejected_identity++;
        lock_exit();
        return rc;
    }
    if (rc != APP_REPORT_ABI_OK) {
        g_stats.rejected_schema++;
        lock_exit();
        return rc;
    }

    rc = entry_accept(session, &policy_identity, &policy_values,
                      policy_revision, normalized,
                      normalized_len, app_report_seq);
    if (rc != APP_REPORT_ABI_OK) {
        if (rc == APP_REPORT_ABI_QUOTA_EXCEEDED) {
            g_stats.rejected_quota++;
        } else {
            g_stats.rejected_busy++;
        }
        lock_exit();
        return rc;
    }
    seen_push(session->publisher_id, session->publisher_key_sha256,
              session->app_id, app_report_seq);
    g_stats.accepted++;
    *out_host_boot_id = g_boot_id;
    lock_exit();
    return APP_REPORT_ABI_OK;
}

int32_t app_report_status(uint32_t host_boot_id, uint32_t report_seq,
                          uint32_t *out_mqtt_state, uint32_t *out_webhook_state)
{
    int32_t rc = APP_REPORT_ABI_NOT_FOUND;

    if (out_mqtt_state == NULL || out_webhook_state == NULL) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    *out_mqtt_state = APP_REPORT_MQTT_UNKNOWN;
    *out_webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;

    lock_enter();
    if (g_session_bound != AICAM_TRUE || host_boot_id != g_boot_id) {
        lock_exit();
        return APP_REPORT_ABI_NOT_FOUND;
    }
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        const report_entry_t *e = &g_queue[i];
        if (e->in_use != AICAM_TRUE || e->report_seq != report_seq) {
            continue;
        }
        if (strcmp(e->app_id, g_session.app_id) != 0 ||
            memcmp(e->key_sha, g_session.publisher_key_sha256,
                   APP_REPORT_SHA256_LEN) != 0) {
            continue;
        }
        *out_mqtt_state = e->mqtt_state;
        *out_webhook_state = e->webhook_state;
        rc = APP_REPORT_ABI_OK;
        break;
    }
    if (rc != APP_REPORT_ABI_OK) {
        int idx = seen_find(g_session.publisher_id,
                            g_session.publisher_key_sha256, g_session.app_id,
                            report_seq);

        if (idx >= 0 && g_seen_gen[(uint32_t)idx] != g_restart_gen) {
            *out_mqtt_state = APP_REPORT_MQTT_UNKNOWN;
            *out_webhook_state = APP_REPORT_WEBHOOK_UNKNOWN;
            rc = APP_REPORT_ABI_OK;
        }
    }
    lock_exit();
    return rc;
}

static int32_t policy_values_for_entry(
    const app_report_policy_identity_t *identity,
    app_report_policy_values_t *values, uint32_t *revision)
{
    aicam_bool_t base_found = AICAM_FALSE;
    app_report_policy_identity_t probe;
    uint8_t tbs_or = 0u;
    int32_t rc;

    values->mqtt_enabled = AICAM_TRUE;
    values->webhook_enabled = AICAM_TRUE;
    values->backlog_capacity = APP_REPORT_POLICY_DEFAULT_CAPACITY;
    *revision = 0u;
    if (g_policy_storage_configured != AICAM_TRUE) {
        return APP_REPORT_ABI_OK;
    }
    for (uint32_t i = 0u; i < APP_REPORT_SHA256_LEN; i++) {
        tbs_or |= identity->native_tbs_sha256[i];
    }
    if (tbs_or == 0u) {
        probe = *identity;
        memset(probe.native_tbs_sha256, 1, sizeof(probe.native_tbs_sha256));
        rc = app_report_policy_store_has_base(&probe, &base_found);
        if (rc != APP_REPORT_ABI_OK) {
            return rc;
        }
        return (base_found == AICAM_TRUE) ? APP_REPORT_ABI_UNAUTHORIZED :
               APP_REPORT_ABI_OK;
    }
    return policy_values_for_identity(identity, values, revision);
}

static app_tx_outcome_t tx_send(report_entry_t *e, aicam_bool_t is_mqtt)
{
    uint32_t ready;
    int r;
    uint32_t channel = (is_mqtt == AICAM_TRUE) ?
                       APP_REPORT_CHANNEL_MQTT : APP_REPORT_CHANNEL_WEBHOOK;
    app_report_policy_values_t policy_values;
    uint32_t policy_revision = 0u;
    int32_t policy_rc;

    if (g_transport == NULL) {
        return APP_TX_UNCONFIGURED;
    }
    if (is_mqtt == AICAM_TRUE) {
        if (g_transport->mqtt_ready == NULL) {
            return APP_TX_UNCONFIGURED;
        }
        ready = g_transport->mqtt_ready(g_transport->user);
        if (ready == APP_REPORT_CHANNEL_UNCONFIGURED) {
            return APP_TX_UNCONFIGURED;
        }
        if (ready != APP_REPORT_CHANNEL_READY) {
            return APP_TX_WAITING;
        }
        lock_enter();
        if (e->in_use != AICAM_TRUE || e->mqtt_state != APP_REPORT_MQTT_PENDING) {
            lock_exit();
            return APP_TX_POLICY_CANCELLED;
        }
        if (entry_matches_purge(e, &g_purge_identity) &&
            (g_purge_state == APP_REPORT_POLICY_PURGE_PENDING ||
             g_purge_state == APP_REPORT_POLICY_PURGE_UNKNOWN)) {
            e->mqtt_state = (e->mqtt_attempts == 0u) ?
                            APP_REPORT_MQTT_CANCELLED :
                            APP_REPORT_MQTT_RETRY_SUPPRESSED;
            lock_exit();
            return APP_TX_POLICY_CANCELLED;
        }
        policy_rc = policy_values_for_entry(&e->policy_identity, &policy_values,
                                            &policy_revision);
        if (policy_rc != APP_REPORT_ABI_OK) {
            e->mqtt_state = APP_REPORT_MQTT_POLICY_UNKNOWN;
            g_policy_last_result = policy_rc;
            lock_exit();
            return APP_TX_POLICY_UNKNOWN;
        }
        if (policy_revision < e->policy_generation) {
            e->mqtt_state = APP_REPORT_MQTT_POLICY_UNKNOWN;
            g_policy_last_result = APP_REPORT_ABI_STORAGE_UNKNOWN;
            lock_exit();
            return APP_TX_POLICY_UNKNOWN;
        }
        e->policy_generation = policy_revision;
        if (policy_values.mqtt_enabled != AICAM_TRUE) {
            e->mqtt_state = (e->mqtt_attempts == 0u) ?
                            APP_REPORT_MQTT_CANCELLED :
                            APP_REPORT_MQTT_RETRY_SUPPRESSED;
            lock_exit();
            return APP_TX_POLICY_CANCELLED;
        }
        g_transport_gate_entry = e;
        g_transport_gate_channel = channel;
        g_transport_gate_active = AICAM_TRUE;
        r = g_transport->mqtt_publish(g_transport->user, e->payload,
                                      (uint32_t)e->payload_len);
        if (g_transport_gate_active == AICAM_TRUE) {
            g_transport_gate_active = AICAM_FALSE;
            g_transport_gate_entry = NULL;
            lock_exit();
        }
    } else {
        if (g_transport->webhook_ready == NULL) {
            return APP_TX_UNCONFIGURED;
        }
        ready = g_transport->webhook_ready(g_transport->user);
        if (ready == APP_REPORT_CHANNEL_UNCONFIGURED) {
            return APP_TX_UNCONFIGURED;
        }
        if (ready != APP_REPORT_CHANNEL_READY) {
            return APP_TX_WAITING;
        }
        lock_enter();
        if (e->in_use != AICAM_TRUE || e->webhook_state != APP_REPORT_WEBHOOK_PENDING) {
            lock_exit();
            return APP_TX_POLICY_CANCELLED;
        }
        if (entry_matches_purge(e, &g_purge_identity) &&
            (g_purge_state == APP_REPORT_POLICY_PURGE_PENDING ||
             g_purge_state == APP_REPORT_POLICY_PURGE_UNKNOWN)) {
            e->webhook_state = (e->webhook_attempts == 0u) ?
                               APP_REPORT_WEBHOOK_CANCELLED :
                               APP_REPORT_WEBHOOK_RETRY_SUPPRESSED;
            lock_exit();
            return APP_TX_POLICY_CANCELLED;
        }
        policy_rc = policy_values_for_entry(&e->policy_identity, &policy_values,
                                            &policy_revision);
        if (policy_rc != APP_REPORT_ABI_OK) {
            e->webhook_state = APP_REPORT_WEBHOOK_POLICY_UNKNOWN;
            g_policy_last_result = policy_rc;
            lock_exit();
            return APP_TX_POLICY_UNKNOWN;
        }
        if (policy_revision < e->policy_generation) {
            e->webhook_state = APP_REPORT_WEBHOOK_POLICY_UNKNOWN;
            g_policy_last_result = APP_REPORT_ABI_STORAGE_UNKNOWN;
            lock_exit();
            return APP_TX_POLICY_UNKNOWN;
        }
        e->policy_generation = policy_revision;
        if (policy_values.webhook_enabled != AICAM_TRUE) {
            e->webhook_state = (e->webhook_attempts == 0u) ?
                               APP_REPORT_WEBHOOK_CANCELLED :
                               APP_REPORT_WEBHOOK_RETRY_SUPPRESSED;
            lock_exit();
            return APP_TX_POLICY_CANCELLED;
        }
        g_transport_gate_entry = e;
        g_transport_gate_channel = channel;
        g_transport_gate_active = AICAM_TRUE;
        r = g_transport->webhook_post(g_transport->user, e->payload,
                                      (uint32_t)e->payload_len);
        if (g_transport_gate_active == AICAM_TRUE) {
            g_transport_gate_active = AICAM_FALSE;
            g_transport_gate_entry = NULL;
            lock_exit();
        }
    }
    if (r == APP_REPORT_TRANSPORT_OK) {
        return APP_TX_SEND_OK;
    }
    if (r == APP_REPORT_TRANSPORT_UNKNOWN) {
        return APP_TX_SEND_UNKNOWN;
    }
    return APP_TX_SEND_FAIL;
}

static void tx_apply(report_entry_t *e, aicam_bool_t is_mqtt,
                     app_tx_outcome_t outcome)
{
    uint32_t *state = is_mqtt ? &e->mqtt_state : &e->webhook_state;
    uint32_t *attempts = is_mqtt ? &e->mqtt_attempts : &e->webhook_attempts;
    uint32_t *wait = is_mqtt ? &e->mqtt_wait : &e->webhook_wait;

    if (is_mqtt == AICAM_TRUE) {
        e->mqtt_in_call = AICAM_FALSE;
        e->mqtt_begin = AICAM_FALSE;
    } else {
        e->webhook_in_call = AICAM_FALSE;
        e->webhook_begin = AICAM_FALSE;
    }
    if (*state != APP_REPORT_MQTT_PENDING) {
        return;
    }
    switch (outcome) {
    case APP_TX_UNCONFIGURED:
        *state = APP_REPORT_MQTT_NOT_CONFIGURED;
        break;
    case APP_TX_WAITING:
        *wait = APP_REPORT_BACKOFF_POLLS;
        break;
    case APP_TX_SEND_OK:
        (*attempts)++;
        *state = APP_REPORT_MQTT_TRANSPORT_SUCCEEDED;
        break;
    case APP_TX_SEND_UNKNOWN:
        (*attempts)++;
        *state = APP_REPORT_MQTT_UNKNOWN;
        break;
    case APP_TX_POLICY_UNKNOWN:
        *state = is_mqtt == AICAM_TRUE ? APP_REPORT_MQTT_POLICY_UNKNOWN :
                 APP_REPORT_WEBHOOK_POLICY_UNKNOWN;
        break;
    case APP_TX_POLICY_CANCELLED:
        *state = is_mqtt == AICAM_TRUE ? APP_REPORT_MQTT_CANCELLED :
                 APP_REPORT_WEBHOOK_CANCELLED;
        break;
    case APP_TX_SEND_FAIL:
    default:
        (*attempts)++;
        if (*attempts >= APP_REPORT_MAX_ATTEMPTS) {
            *state = APP_REPORT_MQTT_TRANSPORT_FAILED;
        } else {
            *wait = APP_REPORT_BACKOFF_POLLS;
        }
        break;
    }
}

static void tx_apply_current(const app_tx_item_t *t, aicam_bool_t is_mqtt,
                             app_tx_outcome_t outcome)
{
    report_entry_t *e = t->entry;

    lock_enter();
    if (e->in_use == AICAM_TRUE && e->in_flight == AICAM_TRUE &&
        e->accept_order == t->accept_order) {
        tx_apply(e, is_mqtt, outcome);
    }
    lock_exit();
}

void app_report_service_pump(void)
{
    uint32_t n = 0u;

    lock_enter();
    if (g_tx_active == AICAM_TRUE) {
        g_stats.tx_skipped++;
        lock_exit();
        return;
    }
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        report_entry_t *e = &g_queue[i];
        app_tx_item_t *t = &g_tx[n];

        if (e->in_use != AICAM_TRUE || e->in_flight == AICAM_TRUE) {
            continue;
        }
        if (e->mqtt_state != APP_REPORT_MQTT_PENDING &&
            e->webhook_state != APP_REPORT_WEBHOOK_PENDING) {
            continue;
        }
        t->entry = e;
        t->accept_order = e->accept_order;
        t->do_mqtt = AICAM_FALSE;
        t->do_webhook = AICAM_FALSE;
        t->mqtt_outcome = APP_TX_WAITING;
        t->webhook_outcome = APP_TX_WAITING;
        if (e->mqtt_state == APP_REPORT_MQTT_PENDING) {
            if (e->mqtt_wait > 0u) {
                e->mqtt_wait--;
            } else {
                t->do_mqtt = AICAM_TRUE;
            }
        }
        if (e->webhook_state == APP_REPORT_WEBHOOK_PENDING) {
            if (e->webhook_wait > 0u) {
                e->webhook_wait--;
            } else {
                t->do_webhook = AICAM_TRUE;
            }
        }
        if (t->do_mqtt == AICAM_TRUE || t->do_webhook == AICAM_TRUE) {
            e->in_flight = AICAM_TRUE;
            n++;
        }
    }
    if (n == 0u) {
        lock_exit();
        return;
    }
    g_tx_active = AICAM_TRUE;
    g_stats.tx_batches++;
    lock_exit();

    for (uint32_t i = 0u; i < n; i++) {
        app_tx_item_t *t = &g_tx[i];

        if (t->do_mqtt == AICAM_TRUE) {
            t->mqtt_outcome = tx_send(t->entry, AICAM_TRUE);
            tx_apply_current(t, AICAM_TRUE, t->mqtt_outcome);
        }
        if (t->do_webhook == AICAM_TRUE) {
            t->webhook_outcome = tx_send(t->entry, AICAM_FALSE);
            tx_apply_current(t, AICAM_FALSE, t->webhook_outcome);
        }
    }

    lock_enter();
    for (uint32_t i = 0u; i < n; i++) {
        app_tx_item_t *t = &g_tx[i];
        report_entry_t *e = t->entry;

        if (e->in_use != AICAM_TRUE || e->in_flight != AICAM_TRUE ||
            e->accept_order != t->accept_order) {
            continue;
        }
        e->in_flight = AICAM_FALSE;
    }
    g_tx_active = AICAM_FALSE;
    lock_exit();
}

void app_report_service_init(const app_report_identity_t *identity,
                             const app_report_transport_t *transport)
{
    g_identity = identity;
    g_transport = transport;
    memset(&g_session, 0, sizeof(g_session));
    g_session_bound = AICAM_FALSE;
    memset(&g_active_policy_identity, 0, sizeof(g_active_policy_identity));
    g_policy_identity_bound = AICAM_FALSE;
    g_policy_identity_pending = AICAM_FALSE;
    g_policy_last_result = APP_REPORT_ABI_OK;
    g_transport_gate_entry = NULL;
    g_transport_gate_channel = 0u;
    g_transport_gate_active = AICAM_FALSE;
    g_transport_start_entry = NULL;
    g_transport_start_channel = 0u;
    g_transport_begin_entry = NULL;
    g_transport_begin_channel = 0u;
    memset(&g_purge_identity, 0, sizeof(g_purge_identity));
    g_purge_intent = 0u;
    g_purge_state = APP_REPORT_POLICY_PURGE_NONE;
    g_purge_entries = 0u;
    g_purge_identity_bound = AICAM_FALSE;
    memset(&g_trusted_uninstall_identity, 0, sizeof(g_trusted_uninstall_identity));
    g_trusted_uninstall_bound = AICAM_FALSE;
    memset(g_queue, 0, sizeof(g_queue));
    memset(g_tx, 0, sizeof(g_tx));
    g_tx_active = AICAM_FALSE;
    g_accept_counter = 0u;
    memset(g_seen_key, 0, sizeof(g_seen_key));
    memset(g_seen_publisher, 0, sizeof(g_seen_publisher));
    memset(g_seen_app, 0, sizeof(g_seen_app));
    memset(g_seen_seq, 0, sizeof(g_seen_seq));
    memset(g_seen_gen, 0, sizeof(g_seen_gen));
    memset(g_seen_used, 0, sizeof(g_seen_used));
    g_seen_head = 0u;
    g_restart_gen = 0u;
    memset(&g_stats, 0, sizeof(g_stats));
    g_boot_ready = AICAM_FALSE;
    g_boot_id = 0u;
    if (identity != NULL && identity->boot_id != NULL) {
        uint32_t b = identity->boot_id(identity->user);
        if (b != 0u) {
            g_boot_id = b;
            g_boot_ready = AICAM_TRUE;
        }
    }
}

void app_report_service_restart(void)
{
    lock_enter();
    memset(g_queue, 0, sizeof(g_queue));
    memset(g_tx, 0, sizeof(g_tx));
    g_tx_active = AICAM_FALSE;
    g_transport_gate_entry = NULL;
    g_transport_gate_channel = 0u;
    g_transport_gate_active = AICAM_FALSE;
    g_transport_start_entry = NULL;
    g_transport_start_channel = 0u;
    g_transport_begin_entry = NULL;
    g_transport_begin_channel = 0u;
    g_accept_counter = 0u;
    memset(&g_stats, 0, sizeof(g_stats));
    g_restart_gen++;
    lock_exit();
}

void app_report_service_set_lock(void (*enter)(void), void (*exit)(void))
{
    g_lock_enter = enter;
    g_lock_exit = exit;
}

void app_report_service_set_policy_storage(
    const app_report_policy_storage_t *storage)
{
    app_report_policy_store_set_storage(storage);
    g_policy_storage_configured = (storage != NULL) ? AICAM_TRUE : AICAM_FALSE;
}

void app_report_service_set_committed_io(const app_install_io_t *io)
{
    lock_enter();
    g_committed_io = io;
    lock_exit();
}

int32_t app_report_service_trust_uninstall_target(
    const app_report_policy_identity_t *identity)
{
    int committed_match;

    lock_enter();
    if (!policy_identity_valid(identity)) {
        lock_exit();
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    if (g_session_bound == AICAM_TRUE &&
        policy_identity_base_matches_session(identity, &g_session) == 0) {
        lock_exit();
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    committed_match = committed_identity_matches_locked(identity);
    if (committed_match < 0) {
        lock_exit();
        return APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    if (committed_match == 0) {
        lock_exit();
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    if (g_policy_identity_bound != AICAM_TRUE ||
        policy_identity_equal(&g_active_policy_identity, identity) == 0) {
        lock_exit();
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    g_trusted_uninstall_identity = *identity;
    g_trusted_uninstall_bound = AICAM_TRUE;
    lock_exit();
    return APP_REPORT_ABI_OK;
}

static int32_t policy_resolve_current_locked(
    const app_report_session_t *session,
    const app_report_policy_identity_t *identity,
    app_report_policy_values_t *values, uint32_t *revision)
{
    int committed_match;
    int32_t rc;

    if (g_policy_storage_configured != AICAM_TRUE) {
        return APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    if (session_matches(session) != 0 ||
        !policy_identity_base_matches_session(identity, session)) {
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    committed_match = committed_identity_matches_locked(identity);
    if (committed_match < 0) {
        return APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    if (committed_match == 0) {
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    if (g_policy_identity_bound == AICAM_TRUE &&
        policy_identity_equal(&g_active_policy_identity, identity) == 0) {
        g_policy_identity_bound = AICAM_FALSE;
        memset(&g_active_policy_identity, 0, sizeof(g_active_policy_identity));
    }
    rc = policy_values_for_identity(identity, values, revision);
    if (rc != APP_REPORT_ABI_OK) {
        return rc;
    }
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        report_entry_t *entry = &g_queue[i];
        uint8_t tbs_or = 0u;

        if (entry->in_use != AICAM_TRUE ||
            entry->session_generation != g_session_generation ||
            !policy_identity_base_equal(&entry->policy_identity, identity)) {
            continue;
        }
        for (uint32_t j = 0u; j < APP_REPORT_SHA256_LEN; j++) {
            tbs_or |= entry->policy_identity.native_tbs_sha256[j];
        }
        if (tbs_or == 0u) {
            entry->policy_identity = *identity;
            entry->policy_generation = *revision;
        }
    }
    g_active_policy_identity = *identity;
    g_policy_identity_bound = AICAM_TRUE;
    g_policy_identity_pending = AICAM_FALSE;
    return APP_REPORT_ABI_OK;
}

static void policy_status_error(app_report_policy_status_t *out, int32_t result)
{
    memset(out, 0, sizeof(*out));
    out->capacity_min = APP_REPORT_POLICY_MIN_CAPACITY;
    out->capacity_max = APP_REPORT_POLICY_MAX_CAPACITY;
    out->last_result = result;
    out->storage_verified = AICAM_FALSE;
}

int32_t app_report_service_policy_query(
    const app_report_session_t *session,
    const app_report_policy_identity_t *identity,
    app_report_policy_status_t *out)
{
    app_report_policy_values_t values;
    uint32_t revision = 0u;
    int32_t rc;

    if (out == NULL || !policy_identity_valid(identity)) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    lock_enter();
    rc = policy_resolve_current_locked(session, identity, &values, &revision);
    if (rc == APP_REPORT_ABI_OK) {
        g_policy_last_result = APP_REPORT_ABI_OK;
        policy_status_fill(identity, &values, revision, out);
    } else {
        g_policy_last_result = rc;
        policy_status_error(out, rc);
    }
    lock_exit();
    return rc;
}

int32_t app_report_service_policy_commit(
    const app_report_session_t *session,
    const app_report_policy_identity_t *identity,
    uint32_t expected_revision,
    const app_report_policy_values_t *values,
    app_report_policy_status_t *out)
{
    app_report_policy_values_t current_values;
    uint32_t current_revision = 0u;
    uint32_t committed_revision = 0u;
    int32_t rc;

    if (out == NULL || values == NULL || !policy_identity_valid(identity)) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    if ((values->mqtt_enabled != AICAM_TRUE && values->mqtt_enabled != AICAM_FALSE) ||
        (values->webhook_enabled != AICAM_TRUE && values->webhook_enabled != AICAM_FALSE) ||
        values->backlog_capacity < APP_REPORT_POLICY_MIN_CAPACITY ||
        values->backlog_capacity > APP_REPORT_POLICY_MAX_CAPACITY) {
        policy_status_error(out, APP_REPORT_ABI_QUOTA_EXCEEDED);
        return APP_REPORT_ABI_QUOTA_EXCEEDED;
    }
    lock_enter();
    rc = policy_resolve_current_locked(session, identity, &current_values,
                                       &current_revision);
    if (rc != APP_REPORT_ABI_OK) {
        g_policy_last_result = rc;
        policy_status_error(out, rc);
        lock_exit();
        return rc;
    }
    if (current_revision != expected_revision) {
        rc = APP_REPORT_ABI_CONFLICT;
        g_policy_last_result = rc;
        policy_status_fill(identity, &current_values, current_revision, out);
        out->last_result = rc;
        lock_exit();
        return rc;
    }
    if (values->backlog_capacity < current_values.backlog_capacity &&
        policy_backlog_used(identity) > values->backlog_capacity) {
        rc = APP_REPORT_ABI_CAPACITY_CONFLICT;
        g_policy_last_result = rc;
        policy_status_fill(identity, &current_values, current_revision, out);
        out->last_result = rc;
        lock_exit();
        return rc;
    }
    if (values->mqtt_enabled != AICAM_TRUE ||
        values->webhook_enabled != AICAM_TRUE) {
        for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
            report_entry_t *entry = &g_queue[i];
            int channel_busy = 0;

            if (entry->in_use != AICAM_TRUE ||
                !entry_matches_policy_identity(entry, identity)) {
                continue;
            }
            if (values->mqtt_enabled != AICAM_TRUE &&
                entry->mqtt_state == APP_REPORT_MQTT_PENDING &&
                entry->mqtt_in_call == AICAM_TRUE &&
                entry->mqtt_begin == AICAM_TRUE) {
                channel_busy = 1;
            }
            if (values->webhook_enabled != AICAM_TRUE &&
                entry->webhook_state == APP_REPORT_WEBHOOK_PENDING &&
                entry->webhook_in_call == AICAM_TRUE &&
                entry->webhook_begin == AICAM_TRUE) {
                channel_busy = 1;
            }
            if (channel_busy == 1) {
                rc = APP_REPORT_ABI_BUSY;
                g_policy_last_result = rc;
                policy_status_fill(identity, &current_values, current_revision,
                                   out);
                out->last_result = rc;
                lock_exit();
                return rc;
            }
        }
    }
    rc = app_report_policy_store_commit(identity, expected_revision, values,
                                        &committed_revision);
    if (rc == APP_REPORT_ABI_OK) {
        g_active_policy_identity = *identity;
        g_policy_identity_bound = AICAM_TRUE;
        g_policy_identity_pending = AICAM_FALSE;
        for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
            report_entry_t *entry = &g_queue[i];
            if (entry->in_use != AICAM_TRUE ||
                !entry_matches_policy_identity(entry, identity)) {
                continue;
            }
            entry->policy_generation = committed_revision;
            if (values->mqtt_enabled != AICAM_TRUE &&
                entry->mqtt_state == APP_REPORT_MQTT_PENDING &&
                entry->mqtt_begin != AICAM_TRUE) {
                entry->mqtt_state = (entry->mqtt_attempts == 0u) ?
                                    APP_REPORT_MQTT_CANCELLED :
                                    APP_REPORT_MQTT_RETRY_SUPPRESSED;
                entry->mqtt_in_call = AICAM_FALSE;
            }
            if (values->webhook_enabled != AICAM_TRUE &&
                entry->webhook_state == APP_REPORT_WEBHOOK_PENDING &&
                entry->webhook_begin != AICAM_TRUE) {
                entry->webhook_state = (entry->webhook_attempts == 0u) ?
                                       APP_REPORT_WEBHOOK_CANCELLED :
                                       APP_REPORT_WEBHOOK_RETRY_SUPPRESSED;
                entry->webhook_in_call = AICAM_FALSE;
            }
        }
        g_policy_last_result = APP_REPORT_ABI_OK;
        policy_status_fill(identity, values, committed_revision, out);
    } else {
        g_policy_last_result = rc;
        if (rc == APP_REPORT_ABI_CONFLICT) {
            int32_t query_rc = policy_values_for_identity(identity,
                                                          &current_values,
                                                          &current_revision);
            if (query_rc == APP_REPORT_ABI_OK) {
                policy_status_fill(identity, &current_values, current_revision, out);
                out->last_result = rc;
            } else {
                policy_status_error(out, query_rc);
            }
        } else if (rc == APP_REPORT_ABI_STORAGE_ERROR) {
            int32_t query_rc = policy_values_for_identity(identity,
                                                          &current_values,
                                                          &current_revision);
            if (query_rc == APP_REPORT_ABI_OK) {
                policy_status_fill(identity, &current_values, current_revision, out);
                out->last_result = rc;
            } else {
                policy_status_error(out, query_rc);
            }
        } else {
            policy_status_error(out, rc);
        }
    }
    lock_exit();
    return rc;
}

int32_t app_report_service_policy_purge(
    const app_report_policy_identity_t *identity, uint64_t cleanup_intent,
    app_report_policy_status_t *out)
{
    app_report_policy_values_t values;
    uint32_t revision = 0u;
    uint32_t removed = 0u;
    aicam_bool_t admin_intent;
    int32_t rc;

    if (!policy_identity_valid(identity) || cleanup_intent == 0u || out == NULL) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    admin_intent = ((cleanup_intent & APP_REPORT_POLICY_PURGE_INTENT_ADMIN) !=
                    0u) ? AICAM_TRUE : AICAM_FALSE;
    lock_enter();
    if (g_purge_identity_bound == AICAM_TRUE &&
        (g_purge_state == APP_REPORT_POLICY_PURGE_PENDING ||
         g_purge_state == APP_REPORT_POLICY_PURGE_UNKNOWN) &&
        (!policy_identity_equal(&g_purge_identity, identity) ||
         g_purge_intent != cleanup_intent)) {
        g_policy_last_result = APP_REPORT_ABI_BUSY;
        policy_status_error(out, APP_REPORT_ABI_BUSY);
        out->purge_state = g_purge_state;
        lock_exit();
        return APP_REPORT_ABI_BUSY;
    }
    if (purge_blocked_count(identity, admin_intent) != 0u) {
        g_policy_last_result = APP_REPORT_ABI_BUSY;
        policy_status_error(out, APP_REPORT_ABI_BUSY);
        out->purge_state = APP_REPORT_POLICY_PURGE_PENDING;
        out->purged_entries = 0u;
        lock_exit();
        return APP_REPORT_ABI_BUSY;
    }
    if (g_purge_state == APP_REPORT_POLICY_PURGE_COMPLETE &&
        policy_identity_equal(&g_purge_identity, identity) &&
        g_purge_intent == cleanup_intent) {
        values.mqtt_enabled = AICAM_TRUE;
        values.webhook_enabled = AICAM_TRUE;
        values.backlog_capacity = APP_REPORT_POLICY_DEFAULT_CAPACITY;
        rc = app_report_policy_store_get(identity, &values, &revision);
        if (rc == APP_REPORT_ABI_NOT_FOUND) {
            rc = APP_REPORT_ABI_OK;
        }
        if (rc == APP_REPORT_ABI_OK) {
            policy_status_fill(identity, &values, revision, out);
        } else {
            policy_status_error(out, rc);
        }
        out->purge_state = (rc == APP_REPORT_ABI_OK) ?
                           APP_REPORT_POLICY_PURGE_COMPLETE :
                           APP_REPORT_POLICY_PURGE_UNKNOWN;
        out->purged_entries = g_purge_entries;
        out->last_result = rc;
        g_policy_last_result = rc;
        lock_exit();
        return rc;
    }
    g_purge_identity = *identity;
    g_purge_identity_bound = AICAM_TRUE;
    g_purge_intent = cleanup_intent;
    g_purge_state = APP_REPORT_POLICY_PURGE_PENDING;
    g_purge_entries = 0u;
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        report_entry_t *entry = &g_queue[i];
        if (entry_purgeable(entry, identity, admin_intent) == 1 &&
            (entry->mqtt_in_call == AICAM_TRUE ||
                      entry->webhook_in_call == AICAM_TRUE)) {
            g_purge_state = APP_REPORT_POLICY_PURGE_PENDING;
            g_policy_last_result = APP_REPORT_ABI_BUSY;
            policy_status_error(out, APP_REPORT_ABI_BUSY);
            out->purge_state = g_purge_state;
            lock_exit();
            return APP_REPORT_ABI_BUSY;
        }
    }
    rc = app_report_policy_store_remove(identity, cleanup_intent);
    if (rc == APP_REPORT_ABI_NOT_FOUND) {
        if (admin_intent == AICAM_TRUE) {
            for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
                report_entry_t *entry = &g_queue[i];
                if (entry_purgeable(entry, identity, admin_intent) == 1) {
                    memset(entry, 0, sizeof(*entry));
                    removed++;
                }
            }
            for (uint32_t i = 0u; i < APP_REPORT_SEEN_RING; i++) {
                if (g_seen_used[i] == AICAM_TRUE &&
                    strcmp(g_seen_publisher[i], identity->publisher_id) == 0 &&
                    strcmp(g_seen_app[i], identity->app_id) == 0 &&
                    memcmp(g_seen_key[i], identity->publisher_key_sha256,
                           APP_REPORT_SHA256_LEN) == 0) {
                    g_seen_used[i] = AICAM_FALSE;
                }
            }
        }
        g_purge_state = APP_REPORT_POLICY_PURGE_COMPLETE;
        g_purge_entries = removed;
        g_policy_identity_pending = AICAM_FALSE;
        values.mqtt_enabled = AICAM_TRUE;
        values.webhook_enabled = AICAM_TRUE;
        values.backlog_capacity = APP_REPORT_POLICY_DEFAULT_CAPACITY;
        revision = 0u;
        if (app_report_policy_store_get(identity, &values, &revision) !=
            APP_REPORT_ABI_OK) {
            values.mqtt_enabled = AICAM_TRUE;
            values.webhook_enabled = AICAM_TRUE;
            values.backlog_capacity = APP_REPORT_POLICY_DEFAULT_CAPACITY;
            revision = 0u;
        }
        policy_status_fill(identity, &values, revision, out);
        out->purge_state = APP_REPORT_POLICY_PURGE_COMPLETE;
        out->purged_entries = removed;
        out->cleanup_intent = cleanup_intent;
        out->last_result = APP_REPORT_ABI_OK;
        g_policy_last_result = APP_REPORT_ABI_OK;
        lock_exit();
        return APP_REPORT_ABI_OK;
    }
    if (rc == APP_REPORT_ABI_OK) {
        for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
            report_entry_t *entry = &g_queue[i];
            if (entry_purgeable(entry, identity, admin_intent) == 1) {
                memset(entry, 0, sizeof(*entry));
                removed++;
            }
        }
        for (uint32_t i = 0u; i < APP_REPORT_SEEN_RING; i++) {
            if (g_seen_used[i] == AICAM_TRUE &&
                strcmp(g_seen_publisher[i], identity->publisher_id) == 0 &&
                strcmp(g_seen_app[i], identity->app_id) == 0 &&
                memcmp(g_seen_key[i], identity->publisher_key_sha256,
                       APP_REPORT_SHA256_LEN) == 0) {
                g_seen_used[i] = AICAM_FALSE;
            }
        }
        if (g_policy_identity_bound == AICAM_TRUE &&
            policy_identity_equal(&g_active_policy_identity, identity)) {
            g_policy_identity_bound = AICAM_FALSE;
        }
        g_policy_identity_pending = AICAM_FALSE;
        g_purge_state = APP_REPORT_POLICY_PURGE_COMPLETE;
        g_purge_entries = removed;
    } else {
        g_purge_state = (rc == APP_REPORT_ABI_STORAGE_UNKNOWN) ?
                        APP_REPORT_POLICY_PURGE_UNKNOWN :
                        APP_REPORT_POLICY_PURGE_PENDING;
    }
    g_policy_last_result = rc;
    values.mqtt_enabled = AICAM_TRUE;
    values.webhook_enabled = AICAM_TRUE;
    values.backlog_capacity = APP_REPORT_POLICY_DEFAULT_CAPACITY;
    if (rc == APP_REPORT_ABI_OK) {
        (void)app_report_policy_store_get(identity, &values, &revision);
        policy_status_fill(identity, &values, revision, out);
    } else {
        policy_status_error(out, rc);
    }
    out->purge_state = g_purge_state;
    out->purged_entries = g_purge_entries;
    out->cleanup_intent = cleanup_intent;
    out->last_result = rc;
    lock_exit();
    return rc;
}

int32_t app_report_service_policy_purge_status(
    const app_report_policy_identity_t *identity,
    uint64_t cleanup_intent,
    app_report_policy_status_t *out)
{
    app_report_policy_values_t values;
    uint32_t revision = 0u;
    uint32_t queued = 0u;
    uint64_t stored_cleanup_intent = 0u;
    int32_t rc;

    if (!policy_identity_valid(identity) || cleanup_intent == 0u || out == NULL) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    lock_enter();
    rc = app_report_policy_store_get_cleanup_intent(identity,
                                                     &stored_cleanup_intent);
    if (rc != APP_REPORT_ABI_OK && rc != APP_REPORT_ABI_NOT_FOUND) {
        policy_status_error(out, rc);
        out->purge_state = APP_REPORT_POLICY_PURGE_UNKNOWN;
        lock_exit();
        return rc;
    }
    rc = app_report_policy_store_get(identity, &values, &revision);
    if (rc == APP_REPORT_ABI_NOT_FOUND) {
        values.mqtt_enabled = AICAM_TRUE;
        values.webhook_enabled = AICAM_TRUE;
        values.backlog_capacity = APP_REPORT_POLICY_DEFAULT_CAPACITY;
        rc = APP_REPORT_ABI_OK;
    }
    if (rc != APP_REPORT_ABI_OK) {
        policy_status_error(out, rc);
        out->purge_state = APP_REPORT_POLICY_PURGE_UNKNOWN;
        lock_exit();
        return rc;
    }
    if (purge_blocked_count(identity,
                            ((cleanup_intent &
                              APP_REPORT_POLICY_PURGE_INTENT_ADMIN) != 0u) ?
                                AICAM_TRUE : AICAM_FALSE) != 0u) {
        policy_status_fill(identity, &values, revision, out);
        out->purge_state = APP_REPORT_POLICY_PURGE_PENDING;
        out->purged_entries = (g_purge_identity_bound == AICAM_TRUE &&
                               policy_identity_equal(&g_purge_identity,
                                                     identity)) ?
                              g_purge_entries : 0u;
        out->cleanup_intent = stored_cleanup_intent;
        out->last_result = APP_REPORT_ABI_BUSY;
        lock_exit();
        return APP_REPORT_ABI_BUSY;
    }
    if (stored_cleanup_intent != cleanup_intent) {
        for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
            if (entry_matches_purge(&g_queue[i], identity)) {
                queued++;
            }
        }
    }
    policy_status_fill(identity, &values, revision, out);
    out->purge_state = (stored_cleanup_intent == cleanup_intent) ?
                       APP_REPORT_POLICY_PURGE_COMPLETE :
                       APP_REPORT_POLICY_PURGE_PENDING;
    out->purged_entries = (g_purge_identity_bound == AICAM_TRUE &&
                           policy_identity_equal(&g_purge_identity, identity)) ?
                          g_purge_entries : 0u;
    out->cleanup_intent = stored_cleanup_intent;
    out->last_result = (stored_cleanup_intent == cleanup_intent || queued == 0u) ?
                       APP_REPORT_ABI_OK : APP_REPORT_ABI_BUSY;
    lock_exit();
    return (stored_cleanup_intent == cleanup_intent || queued == 0u) ?
           APP_REPORT_ABI_OK : APP_REPORT_ABI_BUSY;
}

int32_t app_report_service_transport_entered(uint32_t channel)
{
    report_entry_t *entry;

    if (g_transport_gate_active != AICAM_TRUE ||
        g_transport_gate_entry == NULL ||
        channel != g_transport_gate_channel) {
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    entry = g_transport_gate_entry;
    if (entry->in_use == AICAM_TRUE) {
        if (channel == APP_REPORT_CHANNEL_MQTT &&
            entry->mqtt_state == APP_REPORT_MQTT_PENDING) {
            entry->mqtt_in_call = AICAM_TRUE;
            entry->mqtt_begin = AICAM_FALSE;
        } else if (channel == APP_REPORT_CHANNEL_WEBHOOK &&
                   entry->webhook_state == APP_REPORT_WEBHOOK_PENDING) {
            entry->webhook_in_call = AICAM_TRUE;
            entry->webhook_begin = AICAM_FALSE;
        }
    }
    g_transport_start_entry = entry;
    g_transport_start_channel = channel;
    g_transport_begin_entry = NULL;
    g_transport_begin_channel = 0u;
    g_transport_gate_active = AICAM_FALSE;
    g_transport_gate_entry = NULL;
    lock_exit();
    return APP_REPORT_ABI_OK;
}

int32_t app_report_service_transport_start(uint32_t channel)
{
    report_entry_t *entry;
    app_report_policy_values_t values;
    uint32_t gate_channel;
    uint32_t revision = 0u;
    aicam_bool_t enabled;
    int32_t rc;

    lock_enter();
    entry = g_transport_start_entry;
    gate_channel = g_transport_start_channel;
    g_transport_start_entry = NULL;
    g_transport_start_channel = 0u;
    if (entry == NULL || entry->in_use != AICAM_TRUE ||
        channel != gate_channel ||
        (channel != APP_REPORT_CHANNEL_MQTT &&
         channel != APP_REPORT_CHANNEL_WEBHOOK)) {
        lock_exit();
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    if (channel == APP_REPORT_CHANNEL_MQTT) {
        if (entry->mqtt_state != APP_REPORT_MQTT_PENDING ||
            entry->mqtt_in_call != AICAM_TRUE) {
            lock_exit();
            return APP_REPORT_ABI_UNAUTHORIZED;
        }
    } else {
        if (entry->webhook_state != APP_REPORT_WEBHOOK_PENDING ||
            entry->webhook_in_call != AICAM_TRUE) {
            lock_exit();
            return APP_REPORT_ABI_UNAUTHORIZED;
        }
    }
    rc = policy_values_for_entry(&entry->policy_identity, &values, &revision);
    if (rc != APP_REPORT_ABI_OK) {
        g_policy_last_result = rc;
        if (channel == APP_REPORT_CHANNEL_MQTT) {
            entry->mqtt_state = APP_REPORT_MQTT_POLICY_UNKNOWN;
            entry->mqtt_in_call = AICAM_FALSE;
        } else {
            entry->webhook_state = APP_REPORT_WEBHOOK_POLICY_UNKNOWN;
            entry->webhook_in_call = AICAM_FALSE;
        }
        lock_exit();
        return rc;
    }
    enabled = (channel == APP_REPORT_CHANNEL_MQTT) ? values.mqtt_enabled :
              values.webhook_enabled;
    if (enabled != AICAM_TRUE) {
        if (channel == APP_REPORT_CHANNEL_MQTT) {
            entry->mqtt_state = (entry->mqtt_attempts == 0u) ?
                                APP_REPORT_MQTT_CANCELLED :
                                APP_REPORT_MQTT_RETRY_SUPPRESSED;
            entry->mqtt_in_call = AICAM_FALSE;
        } else {
            entry->webhook_state = (entry->webhook_attempts == 0u) ?
                                   APP_REPORT_WEBHOOK_CANCELLED :
                                   APP_REPORT_WEBHOOK_RETRY_SUPPRESSED;
            entry->webhook_in_call = AICAM_FALSE;
        }
        lock_exit();
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    g_transport_begin_entry = entry;
    g_transport_begin_channel = channel;
    lock_exit();
    return APP_REPORT_ABI_OK;
}

int32_t app_report_service_transport_begin(uint32_t channel)
{
    report_entry_t *entry;
    uint32_t begin_channel;

    lock_enter();
    entry = g_transport_begin_entry;
    begin_channel = g_transport_begin_channel;
    g_transport_begin_entry = NULL;
    g_transport_begin_channel = 0u;
    if (entry == NULL || entry->in_use != AICAM_TRUE ||
        channel != begin_channel ||
        (channel != APP_REPORT_CHANNEL_MQTT &&
         channel != APP_REPORT_CHANNEL_WEBHOOK)) {
        lock_exit();
        return APP_REPORT_ABI_UNAUTHORIZED;
    }
    if (channel == APP_REPORT_CHANNEL_MQTT) {
        if (entry->mqtt_state != APP_REPORT_MQTT_PENDING ||
            entry->mqtt_in_call != AICAM_TRUE) {
            lock_exit();
            return APP_REPORT_ABI_UNAUTHORIZED;
        }
        entry->mqtt_begin = AICAM_TRUE;
    } else {
        if (entry->webhook_state != APP_REPORT_WEBHOOK_PENDING ||
            entry->webhook_in_call != AICAM_TRUE) {
            lock_exit();
            return APP_REPORT_ABI_UNAUTHORIZED;
        }
        entry->webhook_begin = AICAM_TRUE;
    }
    lock_exit();
    return APP_REPORT_ABI_OK;
}

int32_t app_report_service_set_session(const app_report_session_t *session)
{
    app_report_policy_identity_t probe;
    aicam_bool_t base_found = AICAM_FALSE;
    int32_t policy_rc;

    lock_enter();
    if (session == NULL) {
        memset(&g_session, 0, sizeof(g_session));
        g_session_bound = AICAM_FALSE;
        memset(&g_active_policy_identity, 0, sizeof(g_active_policy_identity));
        g_policy_identity_bound = AICAM_FALSE;
        g_policy_identity_pending = AICAM_FALSE;
        g_trusted_uninstall_bound = AICAM_FALSE;
        g_session_generation++;
        lock_exit();
        return APP_REPORT_ABI_OK;
    }
    if (session->active != AICAM_TRUE || session->report_max == 0u ||
        session->report_max > APP_REPORT_INPUT_MAX ||
        session->publisher_id[0] == '\0' || session->app_id[0] == '\0' ||
        memchr(session->publisher_id, '\0', sizeof(session->publisher_id)) == NULL ||
        memchr(session->app_id, '\0', sizeof(session->app_id)) == NULL) {
        lock_exit();
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    g_session = *session;
    g_session_bound = AICAM_TRUE;
    memset(&g_active_policy_identity, 0, sizeof(g_active_policy_identity));
    g_policy_identity_bound = AICAM_FALSE;
    g_policy_identity_pending = AICAM_FALSE;
    g_trusted_uninstall_bound = AICAM_FALSE;
    g_session_generation++;
    if (g_policy_storage_configured == AICAM_TRUE) {
        policy_identity_from_session(session, &probe);
        memset(probe.native_tbs_sha256, 1, sizeof(probe.native_tbs_sha256));
        policy_rc = app_report_policy_store_has_base(&probe, &base_found);
        if (policy_rc != APP_REPORT_ABI_OK) {
            g_policy_identity_pending = AICAM_TRUE;
            g_policy_last_result = policy_rc;
            lock_exit();
            return APP_REPORT_ABI_STORAGE_UNKNOWN;
        }
        if (base_found == AICAM_TRUE) {
            uint8_t committed_tbs[APP_REPORT_SHA256_LEN];
            app_report_policy_identity_t stored_identity;
            app_report_policy_values_t stored_values;
            uint32_t stored_revision = 0u;
            if (committed_tbs_for_session_locked(session, committed_tbs) == 0 &&
                app_report_policy_store_get_base(&probe, &stored_identity,
                                                 &stored_values,
                                                 &stored_revision) ==
                    APP_REPORT_ABI_OK &&
                memcmp(stored_identity.native_tbs_sha256, committed_tbs,
                       APP_REPORT_SHA256_LEN) == 0) {
                g_active_policy_identity = stored_identity;
                g_policy_identity_bound = AICAM_TRUE;
                g_policy_identity_pending = AICAM_FALSE;
            } else {
                g_policy_identity_pending = AICAM_TRUE;
            }
        }
    }
    lock_exit();
    return APP_REPORT_ABI_OK;
}

uint32_t app_report_service_boot_id(void)
{
    return g_boot_id;
}

int32_t app_report_service_policy_rebase(const app_install_record_t *record)
{
    app_report_policy_identity_t probe;
    aicam_bool_t moved = AICAM_FALSE;
    int32_t rc;

    if (record == NULL) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    lock_enter();
    g_policy_identity_bound = AICAM_FALSE;
    g_policy_identity_pending = AICAM_FALSE;
    memset(&g_active_policy_identity, 0, sizeof(g_active_policy_identity));
    if (g_policy_storage_configured != AICAM_TRUE) {
        lock_exit();
        return APP_REPORT_ABI_OK;
    }
    memset(&probe, 0, sizeof(probe));
    memcpy(probe.publisher_id, record->publisher_id,
           sizeof(probe.publisher_id));
    memcpy(probe.app_id, record->app_id, sizeof(probe.app_id));
    memcpy(probe.publisher_key_sha256, record->publisher_key_sha256,
           APP_REPORT_SHA256_LEN);
    memset(probe.native_tbs_sha256, 1, sizeof(probe.native_tbs_sha256));
    rc = app_report_policy_store_rebase_base_tbs(&probe, record->tbs_sha256,
                                                 &moved);
    lock_exit();
    return rc;
}


uint32_t app_report_service_queue_count(void)
{
    uint32_t n = 0u;

    lock_enter();
    for (uint32_t i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
        if (g_queue[i].in_use == AICAM_TRUE) {
            n++;
        }
    }
    lock_exit();
    return n;
}

void app_report_service_get_stats(app_report_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    lock_enter();
    *out = g_stats;
    lock_exit();
}

void app_report_service_policy_debug(aicam_bool_t *out_pending,
                                     aicam_bool_t *out_bound,
                                     int32_t *out_last_result,
                                     uint8_t out_active_tbs[APP_REPORT_SHA256_LEN])
{
    lock_enter();
    if (out_pending != NULL) {
        *out_pending = g_policy_identity_pending;
    }
    if (out_bound != NULL) {
        *out_bound = g_policy_identity_bound;
    }
    if (out_last_result != NULL) {
        *out_last_result = g_policy_last_result;
    }
    if (out_active_tbs != NULL) {
        memcpy(out_active_tbs, g_active_policy_identity.native_tbs_sha256,
               APP_REPORT_SHA256_LEN);
    }
    lock_exit();
}

aicam_bool_t app_report_service_entry(uint32_t index, app_report_entry_info_t *out)
{
    aicam_bool_t found = AICAM_FALSE;

    if (out == NULL) {
        return AICAM_FALSE;
    }
    memset(out, 0, sizeof(*out));
    lock_enter();
    if (index < APP_REPORT_QUEUE_SLOTS && g_queue[index].in_use == AICAM_TRUE) {
        out->host_boot_id = g_boot_id;
        out->report_seq = g_queue[index].report_seq;
        out->mqtt_state = g_queue[index].mqtt_state;
        out->webhook_state = g_queue[index].webhook_state;
        out->mqtt_attempts = g_queue[index].mqtt_attempts;
        out->webhook_attempts = g_queue[index].webhook_attempts;
        out->payload_len = g_queue[index].payload_len;
        found = AICAM_TRUE;
    }
    lock_exit();
    return found;
}

const char *app_report_result_str(int32_t result)
{
    switch (result) {
    case APP_REPORT_ABI_OK:
        return "OK";
    case APP_REPORT_ABI_NOT_FOUND:
        return "NOT_FOUND";
    case APP_REPORT_ABI_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case APP_REPORT_ABI_UNAUTHORIZED:
        return "UNAUTHORIZED";
    case APP_REPORT_ABI_INCOMPATIBLE:
        return "INCOMPATIBLE";
    case APP_REPORT_ABI_QUOTA_EXCEEDED:
        return "QUOTA_EXCEEDED";
    case APP_REPORT_ABI_STORAGE_UNKNOWN:
        return "STORAGE_UNKNOWN";
    case APP_REPORT_ABI_BUSY:
        return "BUSY";
    case APP_REPORT_ABI_STORAGE_ERROR:
        return "STORAGE_ERROR";
    case APP_REPORT_ABI_CONFLICT:
        return "REVISION_CONFLICT";
    case APP_REPORT_ABI_CAPACITY_CONFLICT:
        return "CAPACITY_CONFLICT";
    default:
        return "UNKNOWN";
    }
}
