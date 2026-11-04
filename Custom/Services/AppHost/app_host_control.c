#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "app_host_control.h"
#include "app_admission.h"

#define CTL_FRAME_KIND_OBJECT 1u
#define CTL_FRAME_KIND_ARRAY 2u

typedef struct {
    const uint8_t *p;
    uint32_t len;
    uint32_t pos;
} json_scanner_t;

typedef struct {
    uint32_t kind;
    uint32_t key_count;
    uint32_t key_off[APP_CONTROL_JSON_KEYS_MAX];
    uint32_t key_len[APP_CONTROL_JSON_KEYS_MAX];
} json_frame_t;

typedef struct {
    aicam_bool_t used;
    uint32_t request_id;
    uint32_t msg_type;
    uint32_t flags;
    uint32_t expected_config_generation;
    uint32_t payload_len;
    uint8_t payload[APP_CONTROL_MSG_RAM_MAX - APP_CONTROL_HEADER_SIZE];
    app_control_cmd_state_t state;
    uint32_t result_config_generation;
    uint32_t result_state_revision;
    aicam_bool_t has_enable_receipt;
    uint8_t enable_receipt[APP_CONTROL_ENABLE_PAYLOAD_SIZE];
} ctl_cache_entry_t;

typedef struct {
    aicam_bool_t active;
    uint32_t generation;
    uint32_t caps;
    uint32_t protocol_major;
    uint32_t control_max;
    uint32_t snapshot_max;
    uint32_t last_enable_request_id;
    char publisher_id[APP_CONTROL_IDENTITY_SLOT];
    char app_id[APP_CONTROL_IDENTITY_SLOT];
} ctl_session_t;

typedef struct {
    aicam_bool_t present;
    app_control_cmd_state_t state;
    uint32_t request_id;
    uint32_t msg_type;
    uint32_t flags;
    uint32_t expected_config_generation;
    uint32_t result_config_generation;
    uint32_t result_state_revision;
    uint32_t wire_len;
    uint8_t wire[APP_CONTROL_MSG_RAM_MAX];
} ctl_command_t;

typedef struct {
    aicam_bool_t present;
    uint32_t session_generation;
    uint32_t visible_version;
    uint32_t config_generation;
    uint32_t state_revision;
    uint32_t arrival_ms;
    uint32_t payload_len;
    char publisher_id[APP_CONTROL_IDENTITY_SLOT];
    char app_id[APP_CONTROL_IDENTITY_SLOT];
    uint8_t payload[APP_CONTROL_SNAPSHOT_RAM_MAX];
} ctl_snapshot_t;

_Static_assert(APP_CONTROL_HEADER_SIZE == 32u, "control wire header size");

static app_control_backend_t g_ctl_backend;
static void *g_ctl_mtx;
static ctl_session_t g_ctl_session;
static ctl_command_t g_ctl_command;
static ctl_cache_entry_t g_ctl_cache[APP_CONTROL_CACHE_SLOTS];
static uint32_t g_ctl_cache_next;
static ctl_snapshot_t g_ctl_snapshot;
static aicam_bool_t g_ctl_known_valid;
static uint32_t g_ctl_known_generation;
static aicam_bool_t g_ctl_storage_unknown;
static uint32_t g_ctl_next_generation;
static uint32_t g_ctl_unknown_count;
static uint32_t g_ctl_unknown_session;
static uint32_t g_ctl_unknown_request;
static uint32_t g_ctl_unknown_type;
static json_frame_t g_json_frames[APP_CONTROL_JSON_MAX_DEPTH];

static uint32_t ctl_session_magic(const ctl_session_t *session)
{
    return (session->protocol_major == 4u) ? APP_CONTROL_PROTO_MAGIC_V4 :
                                             APP_CONTROL_PROTO_MAGIC;
}

static void ctl_put_u16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void ctl_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t ctl_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t ctl_get_u16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static void ctl_lock(void)
{
    if (g_ctl_backend.mutex_lock != NULL) {
        g_ctl_backend.mutex_lock(g_ctl_backend.ctx, g_ctl_mtx);
    }
}

static void ctl_unlock(void)
{
    if (g_ctl_backend.mutex_unlock != NULL) {
        g_ctl_backend.mutex_unlock(g_ctl_backend.ctx, g_ctl_mtx);
    }
}

static void ctl_record_unknown(uint32_t session_generation, uint32_t request_id,
                               uint32_t msg_type)
{
    g_ctl_unknown_count++;
    g_ctl_unknown_session = session_generation;
    g_ctl_unknown_request = request_id;
    g_ctl_unknown_type = msg_type;
}

static uint32_t ctl_now_ms(void)
{
    if (g_ctl_backend.now_ms != NULL) {
        return g_ctl_backend.now_ms(g_ctl_backend.ctx);
    }
    return 0u;
}

void app_host_control_init(const app_control_backend_t *backend)
{
    g_ctl_backend.ctx = (backend != NULL) ? backend->ctx : NULL;
    g_ctl_backend.now_ms = (backend != NULL) ? backend->now_ms : NULL;
    g_ctl_backend.mutex_create = (backend != NULL) ? backend->mutex_create : NULL;
    g_ctl_backend.mutex_destroy = (backend != NULL) ? backend->mutex_destroy : NULL;
    g_ctl_backend.mutex_lock = (backend != NULL) ? backend->mutex_lock : NULL;
    g_ctl_backend.mutex_unlock = (backend != NULL) ? backend->mutex_unlock : NULL;
    g_ctl_backend.enable_precommit = (backend != NULL) ? backend->enable_precommit : NULL;
    g_ctl_backend.enable_receipt_check = (backend != NULL) ? backend->enable_receipt_check : NULL;
    if (g_ctl_backend.mutex_create != NULL) {
        g_ctl_mtx = g_ctl_backend.mutex_create(g_ctl_backend.ctx);
    } else {
        g_ctl_mtx = NULL;
    }
    memset(&g_ctl_session, 0, sizeof(g_ctl_session));
    memset(&g_ctl_command, 0, sizeof(g_ctl_command));
    memset(g_ctl_cache, 0, sizeof(g_ctl_cache));
    memset(&g_ctl_snapshot, 0, sizeof(g_ctl_snapshot));
    g_ctl_cache_next = 0u;
    g_ctl_known_valid = AICAM_FALSE;
    g_ctl_known_generation = 0u;
    g_ctl_storage_unknown = AICAM_FALSE;
    g_ctl_next_generation = 1u;
    g_ctl_unknown_count = 0u;
    g_ctl_unknown_session = 0u;
    g_ctl_unknown_request = 0u;
    g_ctl_unknown_type = 0u;
    memset(g_json_frames, 0, sizeof(g_json_frames));
}

void app_host_control_generation_adopt(uint32_t next_generation)
{
    ctl_lock();
    g_ctl_next_generation = next_generation;
    ctl_unlock();
}

static aicam_bool_t ctl_bit6(const ctl_session_t *s)
{
    return (aicam_bool_t)((s->caps & 0x00000040u) != 0u);
}

static aicam_bool_t ctl_bit7(const ctl_session_t *s)
{
    return (aicam_bool_t)((s->caps & 0x00000080u) != 0u);
}

static aicam_bool_t ctl_bit8(const ctl_session_t *s)
{
    return (aicam_bool_t)((s->caps & APP_ADMISSION_V4_CAP_ENABLE_CONTROL) != 0u);
}

static void ctl_cache_store(uint32_t request_id, uint32_t msg_type, uint32_t flags,
                            uint32_t expected_gen, const uint8_t *payload,
                            uint32_t payload_len, app_control_cmd_state_t state,
                            uint32_t result_gen, uint32_t result_rev)
{
    ctl_cache_entry_t *e;

    e = &g_ctl_cache[g_ctl_cache_next];
    g_ctl_cache_next = (g_ctl_cache_next + 1u) % APP_CONTROL_CACHE_SLOTS;
    memset(e, 0, sizeof(*e));
    e->used = AICAM_TRUE;
    e->request_id = request_id;
    e->msg_type = msg_type;
    e->flags = flags;
    e->expected_config_generation = expected_gen;
    e->payload_len = payload_len;
    if (payload != NULL && payload_len > 0u) {
        memcpy(e->payload, payload, payload_len);
    }
    e->state = state;
    e->result_config_generation = result_gen;
    e->result_state_revision = result_rev;
}

static aicam_bool_t ctl_semantics_equal(const ctl_cache_entry_t *e, uint32_t msg_type,
                                        uint32_t flags, uint32_t expected_gen,
                                        const uint8_t *payload, uint32_t payload_len)
{
    if (e->msg_type != msg_type || e->flags != flags ||
        e->expected_config_generation != expected_gen ||
        e->payload_len != payload_len) {
        return AICAM_FALSE;
    }
    if (payload_len > 0u && memcmp(e->payload, payload, payload_len) != 0) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static aicam_bool_t ctl_cmd_semantics_equal(const ctl_command_t *c, uint32_t msg_type,
                                            uint32_t flags, uint32_t expected_gen,
                                            const uint8_t *payload, uint32_t payload_len)
{
    uint32_t cmd_payload_len;

    if (c->wire_len < APP_CONTROL_HEADER_SIZE) {
        return AICAM_FALSE;
    }
    cmd_payload_len = c->wire_len - APP_CONTROL_HEADER_SIZE;
    if (c->msg_type != msg_type || c->flags != flags ||
        c->expected_config_generation != expected_gen ||
        cmd_payload_len != payload_len) {
        return AICAM_FALSE;
    }
    if (payload_len > 0u &&
        memcmp(c->wire + APP_CONTROL_HEADER_SIZE, payload, payload_len) != 0) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static app_control_cmd_state_t ctl_state_from_result(uint32_t result_status)
{
    return (app_control_cmd_state_t)(3u + result_status);
}

int32_t app_host_control_session_open(const app_control_session_cfg_t *cfg,
                                      uint32_t *out_generation)
{
    uint32_t protocol_major;
    if (cfg == NULL || out_generation == NULL) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    protocol_major = (cfg->protocol_major == 0u) ? 3u : cfg->protocol_major;
    if (protocol_major != 3u && protocol_major != 4u) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    if (protocol_major == 4u &&
        (cfg->caps & APP_ADMISSION_V4_CAP_ENABLE_CONTROL) != 0u &&
        ((cfg->caps & (APP_ADMISSION_V2_CAP_APP_STATE |
                       APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
                       APP_ADMISSION_V3_CAP_CONTROL_RECEIVE)) !=
             (APP_ADMISSION_V2_CAP_APP_STATE |
              APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
              APP_ADMISSION_V3_CAP_CONTROL_RECEIVE) ||
         cfg->control_max < APP_CONTROL_ENABLE_SET_SIZE ||
         g_ctl_backend.enable_precommit == NULL ||
         g_ctl_backend.enable_receipt_check == NULL)) {
        return APP_CONTROL_UNAUTHORIZED;
    }
    if (protocol_major == 4u &&
        (cfg->caps & (APP_ADMISSION_V3_CAP_CONTROL_RECEIVE |
                      APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT)) == 0u) {
        if (cfg->control_max != 0u ||
            cfg->snapshot_max != 0u ||
            cfg->control_max > APP_CONTROL_MSG_RAM_MAX ||
            cfg->snapshot_max > APP_CONTROL_SNAPSHOT_RAM_MAX) {
            return APP_CONTROL_INVALID_ARGUMENT;
        }
    } else if (cfg->control_max < APP_CONTROL_HEADER_SIZE ||
               cfg->control_max > APP_CONTROL_MSG_RAM_MAX ||
               cfg->snapshot_max > APP_CONTROL_SNAPSHOT_RAM_MAX) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    ctl_lock();
    if (g_ctl_session.active) {
        ctl_unlock();
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    if (g_ctl_next_generation == 0u) {
        ctl_unlock();
        return (int32_t)APP_CONTROL_ERR_GENERATION_EXHAUSTED;
    }
    memset(&g_ctl_session, 0, sizeof(g_ctl_session));
    g_ctl_session.active = AICAM_TRUE;
    g_ctl_session.generation = g_ctl_next_generation;
    if (g_ctl_next_generation == APP_CONTROL_GENERATION_LAST) {
        g_ctl_next_generation = 0u;
    } else {
        g_ctl_next_generation++;
    }
    g_ctl_session.caps = cfg->caps;
    g_ctl_session.protocol_major = protocol_major;
    g_ctl_session.control_max = cfg->control_max;
    g_ctl_session.snapshot_max = cfg->snapshot_max;
    memcpy(g_ctl_session.publisher_id, cfg->publisher_id,
           sizeof(g_ctl_session.publisher_id) - 1u);
    memcpy(g_ctl_session.app_id, cfg->app_id, sizeof(g_ctl_session.app_id) - 1u);
    memset(&g_ctl_command, 0, sizeof(g_ctl_command));
    memset(g_ctl_cache, 0, sizeof(g_ctl_cache));
    g_ctl_cache_next = 0u;
    memset(&g_ctl_snapshot, 0, sizeof(g_ctl_snapshot));
    g_ctl_known_valid = AICAM_FALSE;
    g_ctl_known_generation = 0u;
    g_ctl_storage_unknown = AICAM_FALSE;
    *out_generation = g_ctl_session.generation;
    ctl_unlock();
    return APP_CONTROL_OK;
}

void app_host_control_session_close(void)
{
    ctl_lock();
    if (g_ctl_session.active) {
        if (g_ctl_command.present &&
            (g_ctl_command.state == APP_CONTROL_CMD_DELIVERED ||
             (g_ctl_command.state == APP_CONTROL_CMD_PENDING &&
              g_ctl_command.msg_type == APP_CONTROL_MSG_TYPE_ENABLE_SET))) {
            ctl_record_unknown(g_ctl_session.generation, g_ctl_command.request_id,
                               g_ctl_command.msg_type);
        }
        g_ctl_session.active = AICAM_FALSE;
    }
    memset(&g_ctl_command, 0, sizeof(g_ctl_command));
    memset(g_ctl_cache, 0, sizeof(g_ctl_cache));
    g_ctl_cache_next = 0u;
    memset(&g_ctl_snapshot, 0, sizeof(g_ctl_snapshot));
    g_ctl_known_valid = AICAM_FALSE;
    g_ctl_known_generation = 0u;
    g_ctl_storage_unknown = AICAM_FALSE;
    ctl_unlock();
}

app_control_register_result_t app_host_control_register(const app_control_request_t *req)
{
    app_control_register_result_t rc;
    uint32_t i;
    uint32_t payload_len;
    const uint8_t *payload;
    uint32_t total;
    aicam_bool_t destructive;
    aicam_bool_t enable_set;
    uint32_t desired_state = 0u;
    uint32_t expected_state_revision = 0u;

    if (req == NULL) {
        return APP_CONTROL_ERR_PARAM;
    }
    payload = (const uint8_t *)req->payload;
    payload_len = req->payload_len;
    if (payload_len > 0u && payload == NULL) {
        return APP_CONTROL_ERR_PARAM;
    }

    ctl_lock();
    if (!g_ctl_session.active || !ctl_bit6(&g_ctl_session)) {
        ctl_unlock();
        return APP_CONTROL_ERR_UNAUTHORIZED;
    }
    enable_set = (aicam_bool_t)(req->msg_type == APP_CONTROL_MSG_TYPE_ENABLE_SET);
    if (enable_set == AICAM_TRUE &&
        (g_ctl_session.protocol_major != 4u || !ctl_bit8(&g_ctl_session))) {
        ctl_unlock();
        return APP_CONTROL_ERR_UNAUTHORIZED;
    }

    for (i = 0u; i < APP_CONTROL_CACHE_SLOTS; i++) {
        if (g_ctl_cache[i].used != AICAM_TRUE ||
            g_ctl_cache[i].request_id != req->request_id) {
            continue;
        }
        if (ctl_semantics_equal(&g_ctl_cache[i], req->msg_type, req->flags,
                                req->expected_config_generation, payload,
                                payload_len) == AICAM_TRUE) {
            ctl_unlock();
            return APP_CONTROL_REG_CACHED;
        }
        ctl_unlock();
        return APP_CONTROL_ERR_DUPLICATE_ID_CONFLICT;
    }

    if (req->request_id == 0u ||
        (req->msg_type != APP_CONTROL_MSG_TYPE_CONFIG_APPLY &&
         req->msg_type != APP_CONTROL_MSG_TYPE_COUNTERS_RESET &&
         req->msg_type != APP_CONTROL_MSG_TYPE_ENABLE_SET) ||
        (req->flags & ~(uint32_t)APP_CONTROL_FLAG_KNOWN_MASK) != 0u ||
        req->expected_config_generation == 0u) {
        ctl_unlock();
        return APP_CONTROL_ERR_INVALID;
    }
    if (req->msg_type == APP_CONTROL_MSG_TYPE_CONFIG_APPLY &&
        (payload_len == 0u || payload == NULL)) {
        ctl_unlock();
        return APP_CONTROL_ERR_INVALID;
    }
    if (req->msg_type == APP_CONTROL_MSG_TYPE_COUNTERS_RESET && payload_len != 0u) {
        ctl_unlock();
        return APP_CONTROL_ERR_INVALID;
    }

    if (enable_set == AICAM_TRUE) {
        if (req->flags != 0u || payload_len != APP_CONTROL_ENABLE_PAYLOAD_SIZE ||
            payload == NULL || ctl_get_u32(payload) != APP_CONTROL_ENABLE_MAGIC ||
            (ctl_get_u32(payload + 4u) != APP_CONTROL_ENABLE_STATE_ENABLED &&
             ctl_get_u32(payload + 4u) != APP_CONTROL_ENABLE_STATE_PAUSED) ||
            ctl_get_u32(payload + 12u) != 0u ||
            req->request_id <= g_ctl_session.last_enable_request_id) {
            ctl_unlock();
            return (req->request_id != 0u &&
                    req->request_id <= g_ctl_session.last_enable_request_id)
                       ? APP_CONTROL_ERR_DUPLICATE_ID_CONFLICT
                       : APP_CONTROL_ERR_INVALID;
        }
        desired_state = ctl_get_u32(payload + 4u);
        expected_state_revision = ctl_get_u32(payload + 8u);
    }

    if (g_ctl_command.present && (g_ctl_command.state == APP_CONTROL_CMD_PENDING ||
                                  g_ctl_command.state == APP_CONTROL_CMD_DELIVERED)) {
        if (g_ctl_command.request_id == req->request_id) {
            if (ctl_cmd_semantics_equal(&g_ctl_command, req->msg_type, req->flags,
                                        req->expected_config_generation, payload,
                                        payload_len) == AICAM_TRUE) {
                ctl_unlock();
                return APP_CONTROL_REG_ALREADY;
            }
            ctl_unlock();
            return APP_CONTROL_ERR_DUPLICATE_ID_CONFLICT;
        }
        ctl_unlock();
        return APP_CONTROL_ERR_BUSY;
    }

    if (g_ctl_known_valid && req->expected_config_generation != g_ctl_known_generation) {
        ctl_unlock();
        return APP_CONTROL_ERR_CONFLICT;
    }

    if (req->msg_type == APP_CONTROL_MSG_TYPE_COUNTERS_RESET &&
        (req->flags & APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED) == 0u) {
        ctl_unlock();
        return APP_CONTROL_ERR_UNCONFIRMED;
    }

    total = APP_CONTROL_HEADER_SIZE + payload_len;
    if (total > g_ctl_session.control_max) {
        ctl_unlock();
        return APP_CONTROL_ERR_QUOTA_EXCEEDED;
    }

    if (enable_set == AICAM_TRUE) {
        int32_t precommit_rc;
        g_ctl_session.last_enable_request_id = req->request_id;
        precommit_rc = g_ctl_backend.enable_precommit(
            g_ctl_backend.ctx, g_ctl_session.generation, req->request_id,
            req->expected_config_generation, expected_state_revision,
            desired_state);
        if (precommit_rc != APP_CONTROL_OK) {
            ctl_record_unknown(g_ctl_session.generation, req->request_id,
                               req->msg_type);
            ctl_unlock();
            if (precommit_rc == APP_CONTROL_ERR_CONFLICT) {
                return APP_CONTROL_ERR_CONFLICT;
            }
            if (precommit_rc == APP_CONTROL_UNAUTHORIZED) {
                return APP_CONTROL_ERR_UNAUTHORIZED;
            }
            return APP_CONTROL_ERR_STORAGE_GUARD;
        }
    }

    destructive = (aicam_bool_t)(req->msg_type == APP_CONTROL_MSG_TYPE_COUNTERS_RESET ||
                                 (req->flags & APP_CONTROL_FLAG_DESTRUCTIVE_CONFIRMED) != 0u);
    if (destructive && g_ctl_storage_unknown) {
        ctl_unlock();
        return APP_CONTROL_ERR_STORAGE_GUARD;
    }

    memset(&g_ctl_command, 0, sizeof(g_ctl_command));
    g_ctl_command.present = AICAM_TRUE;
    g_ctl_command.state = APP_CONTROL_CMD_PENDING;
    g_ctl_command.request_id = req->request_id;
    g_ctl_command.msg_type = req->msg_type;
    g_ctl_command.flags = req->flags;
    g_ctl_command.expected_config_generation = req->expected_config_generation;
    g_ctl_command.wire_len = total;
    ctl_put_u32(g_ctl_command.wire + 0, ctl_session_magic(&g_ctl_session));
    ctl_put_u16(g_ctl_command.wire + 4, APP_CONTROL_HEADER_VERSION);
    ctl_put_u16(g_ctl_command.wire + 6, req->msg_type);
    ctl_put_u32(g_ctl_command.wire + 8, total);
    ctl_put_u32(g_ctl_command.wire + 12, g_ctl_session.generation);
    ctl_put_u32(g_ctl_command.wire + 16, req->request_id);
    ctl_put_u32(g_ctl_command.wire + 20, req->expected_config_generation);
    ctl_put_u32(g_ctl_command.wire + 24, 0u);
    ctl_put_u32(g_ctl_command.wire + 28, req->flags);
    if (payload_len > 0u) {
        memcpy(g_ctl_command.wire + APP_CONTROL_HEADER_SIZE, payload, payload_len);
    }
    rc = APP_CONTROL_REG_OK;
    ctl_unlock();
    return rc;
}

int32_t app_host_control_deliver(void *out, uint32_t cap, uint32_t *actual_len)
{
    int32_t rc;

    if (out == NULL || actual_len == NULL || cap == 0u) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    ctl_lock();
    if (!g_ctl_session.active || !ctl_bit6(&g_ctl_session)) {
        ctl_unlock();
        return APP_CONTROL_UNAUTHORIZED;
    }
    if (!g_ctl_command.present || g_ctl_command.state != APP_CONTROL_CMD_PENDING) {
        ctl_unlock();
        return APP_CONTROL_NO_EVENT;
    }
    if (cap < g_ctl_command.wire_len) {
        *actual_len = g_ctl_command.wire_len;
        ctl_unlock();
        return APP_CONTROL_BUFFER_TOO_SMALL;
    }
    memcpy(out, g_ctl_command.wire, g_ctl_command.wire_len);
    *actual_len = g_ctl_command.wire_len;
    g_ctl_command.state = APP_CONTROL_CMD_DELIVERED;
    rc = APP_CONTROL_OK;
    ctl_unlock();
    return rc;
}

int32_t app_host_control_receipt(const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t magic;
    uint32_t header_version;
    uint32_t msg_type;
    uint32_t total_len;
    uint32_t session_generation;
    uint32_t request_id;
    uint32_t config_generation;
    uint32_t state_revision;
    uint32_t result_status;
    uint32_t i;
    int32_t rc;
    uint32_t effective_state = 0u;
    const uint8_t *enable_receipt = NULL;

    if (buf == NULL || (len != APP_CONTROL_HEADER_SIZE &&
                        len != APP_CONTROL_ENABLE_RECEIPT_SIZE)) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    magic = ctl_get_u32(p + 0);
    header_version = ctl_get_u16(p + 4);
    msg_type = ctl_get_u16(p + 6);
    total_len = ctl_get_u32(p + 8);
    session_generation = ctl_get_u32(p + 12);
    request_id = ctl_get_u32(p + 16);
    config_generation = ctl_get_u32(p + 20);
    state_revision = ctl_get_u32(p + 24);
    result_status = ctl_get_u32(p + 28);
    if ((magic != APP_CONTROL_PROTO_MAGIC && magic != APP_CONTROL_PROTO_MAGIC_V4) ||
        header_version != APP_CONTROL_HEADER_VERSION ||
        msg_type != APP_CONTROL_MSG_TYPE_RECEIPT ||
        total_len != len) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    if (len == APP_CONTROL_ENABLE_RECEIPT_SIZE) {
        enable_receipt = p + APP_CONTROL_HEADER_SIZE;
        if (magic != APP_CONTROL_PROTO_MAGIC_V4 ||
            ctl_get_u32(enable_receipt) != APP_CONTROL_ENABLE_RECEIPT_MAGIC ||
            (ctl_get_u32(enable_receipt + 4u) != APP_CONTROL_ENABLE_STATE_ENABLED &&
             ctl_get_u32(enable_receipt + 4u) != APP_CONTROL_ENABLE_STATE_PAUSED &&
             ctl_get_u32(enable_receipt + 4u) != APP_CONTROL_ENABLE_STATE_UNKNOWN) ||
            ctl_get_u32(enable_receipt + 8u) != state_revision ||
            ctl_get_u32(enable_receipt + 12u) != 0u) {
            return APP_CONTROL_INVALID_ARGUMENT;
        }
        effective_state = ctl_get_u32(enable_receipt + 4u);
    }

    ctl_lock();
    if (!g_ctl_session.active) {
        ctl_unlock();
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    if (magic != ctl_session_magic(&g_ctl_session)) {
        ctl_unlock();
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    if (session_generation != g_ctl_session.generation ||
        request_id == 0u || config_generation == 0u ||
        result_status > APP_CONTROL_RESULT_MAX) {
        ctl_unlock();
        return APP_CONTROL_INVALID_ARGUMENT;
    }

    if (g_ctl_command.present && g_ctl_command.state == APP_CONTROL_CMD_DELIVERED &&
        g_ctl_command.request_id == request_id) {
        if (g_ctl_command.msg_type == APP_CONTROL_MSG_TYPE_ENABLE_SET) {
            uint32_t desired_state = ctl_get_u32(
                g_ctl_command.wire + APP_CONTROL_HEADER_SIZE + 4u);
            uint32_t expected_state_revision = ctl_get_u32(
                g_ctl_command.wire + APP_CONTROL_HEADER_SIZE + 8u);
            if (len != APP_CONTROL_ENABLE_RECEIPT_SIZE ||
                config_generation != g_ctl_command.expected_config_generation) {
                ctl_unlock();
                return APP_CONTROL_INVALID_ARGUMENT;
            }
            if (result_status == APP_CONTROL_RESULT_APPLIED &&
                (state_revision <= expected_state_revision ||
                 effective_state != desired_state ||
                 g_ctl_backend.enable_receipt_check == NULL ||
                 g_ctl_backend.enable_receipt_check(
                     g_ctl_backend.ctx, session_generation, request_id,
                     config_generation, state_revision, desired_state) !=
                     APP_CONTROL_OK)) {
                g_ctl_command.state = APP_CONTROL_CMD_UNKNOWN;
                ctl_record_unknown(session_generation, request_id,
                                   APP_CONTROL_MSG_TYPE_ENABLE_SET);
                ctl_unlock();
                return APP_CONTROL_ERR_STORAGE_GUARD;
            }
            if (result_status != APP_CONTROL_RESULT_APPLIED &&
                effective_state != APP_CONTROL_ENABLE_STATE_UNKNOWN) {
                ctl_unlock();
                return APP_CONTROL_INVALID_ARGUMENT;
            }
        } else if (len != APP_CONTROL_HEADER_SIZE) {
            ctl_unlock();
            return APP_CONTROL_INVALID_ARGUMENT;
        }
        g_ctl_command.state = ctl_state_from_result(result_status);
        g_ctl_command.result_config_generation = config_generation;
        g_ctl_command.result_state_revision = state_revision;
        ctl_cache_store(request_id, g_ctl_command.msg_type, g_ctl_command.flags,
                        g_ctl_command.expected_config_generation,
                        g_ctl_command.wire + APP_CONTROL_HEADER_SIZE,
                        g_ctl_command.wire_len - APP_CONTROL_HEADER_SIZE,
                        g_ctl_command.state, config_generation, state_revision);
        if (g_ctl_command.msg_type == APP_CONTROL_MSG_TYPE_ENABLE_SET) {
            for (i = 0u; i < APP_CONTROL_CACHE_SLOTS; i++) {
                if (g_ctl_cache[i].used == AICAM_TRUE &&
                    g_ctl_cache[i].request_id == request_id) {
                    g_ctl_cache[i].has_enable_receipt = AICAM_TRUE;
                    memcpy(g_ctl_cache[i].enable_receipt, enable_receipt,
                           APP_CONTROL_ENABLE_PAYLOAD_SIZE);
                    break;
                }
            }
        }
        g_ctl_known_valid = AICAM_TRUE;
        g_ctl_known_generation = config_generation;
        g_ctl_storage_unknown = (aicam_bool_t)(result_status ==
                                               APP_CONTROL_RESULT_FAILED_STORAGE_UNKNOWN);
        rc = APP_CONTROL_OK;
        ctl_unlock();
        return rc;
    }

    for (i = 0u; i < APP_CONTROL_CACHE_SLOTS; i++) {
        if (g_ctl_cache[i].used == AICAM_TRUE &&
            g_ctl_cache[i].request_id == request_id) {
            if (g_ctl_cache[i].msg_type == APP_CONTROL_MSG_TYPE_ENABLE_SET &&
                (len != APP_CONTROL_ENABLE_RECEIPT_SIZE ||
                 g_ctl_cache[i].has_enable_receipt != AICAM_TRUE ||
                 g_ctl_cache[i].result_config_generation != config_generation ||
                 g_ctl_cache[i].result_state_revision != state_revision ||
                 (uint32_t)(g_ctl_cache[i].state - APP_CONTROL_CMD_APPLIED) !=
                     result_status ||
                 memcmp(g_ctl_cache[i].enable_receipt, enable_receipt,
                        APP_CONTROL_ENABLE_PAYLOAD_SIZE) != 0)) {
                ctl_unlock();
                return APP_CONTROL_INVALID_ARGUMENT;
            }
            if (g_ctl_cache[i].msg_type != APP_CONTROL_MSG_TYPE_ENABLE_SET &&
                len != APP_CONTROL_HEADER_SIZE) {
                ctl_unlock();
                return APP_CONTROL_INVALID_ARGUMENT;
            }
            rc = APP_CONTROL_OK;
            ctl_unlock();
            return rc;
        }
    }
    ctl_unlock();
    return APP_CONTROL_INVALID_ARGUMENT;
}

static aicam_bool_t ctl_utf8_valid(const uint8_t *data, uint32_t len)
{
    uint32_t i = 0u;

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
        if (i + extra >= len) {
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
        if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu) || cp < min) {
            return AICAM_FALSE;
        }
    }
    return AICAM_TRUE;
}

static void json_ws(json_scanner_t *s)
{
    while (s->pos < s->len) {
        uint8_t c = s->p[s->pos];
        if (c == 0x20u || c == 0x09u || c == 0x0Au || c == 0x0Du) {
            s->pos++;
        } else {
            break;
        }
    }
}

static aicam_bool_t json_peek(json_scanner_t *s, uint8_t *out)
{
    json_ws(s);
    if (s->pos >= s->len) {
        return AICAM_FALSE;
    }
    *out = s->p[s->pos];
    return AICAM_TRUE;
}

static aicam_bool_t json_hex4(const uint8_t *p, uint32_t len, uint32_t *pos,
                              uint32_t *out)
{
    uint32_t v = 0u;
    uint32_t i = *pos;
    uint32_t k;

    if (i + 4u > len) {
        return AICAM_FALSE;
    }
    for (k = 0u; k < 4u; k++) {
        uint8_t c = p[i + k];
        uint32_t d;

        if (c >= (uint8_t)'0' && c <= (uint8_t)'9') {
            d = (uint32_t)(c - (uint8_t)'0');
        } else if (c >= (uint8_t)'a' && c <= (uint8_t)'f') {
            d = (uint32_t)(c - (uint8_t)'a') + 10u;
        } else if (c >= (uint8_t)'A' && c <= (uint8_t)'F') {
            d = (uint32_t)(c - (uint8_t)'A') + 10u;
        } else {
            return AICAM_FALSE;
        }
        v = (v << 4) | d;
    }
    *pos = i + 4u;
    *out = v;
    return AICAM_TRUE;
}

static int32_t json_decode_one(const uint8_t *p, uint32_t len, uint32_t *pos,
                               uint32_t *out_cp)
{
    uint32_t i = *pos;

    if (i >= len) {
        return -1;
    }
    if (p[i] != (uint8_t)'\\') {
        uint8_t c = p[i];
        uint32_t cp;
        uint32_t extra;
        uint32_t min;

        if (c < 0x80u) {
            *out_cp = c;
            *pos = i + 1u;
            return 0;
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
            return -1;
        }
        if (i + extra >= len) {
            return -1;
        }
        i++;
        while (extra > 0u) {
            if ((p[i] & 0xC0u) != 0x80u) {
                return -1;
            }
            cp = (cp << 6) | (uint32_t)(p[i] & 0x3Fu);
            i++;
            extra--;
        }
        if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu) || cp < min) {
            return -1;
        }
        *out_cp = cp;
        *pos = i;
        return 0;
    }
    i++;
    if (i >= len) {
        return -1;
    }
    switch (p[i]) {
    case (uint8_t)'"':
        *out_cp = (uint32_t)'"';
        *pos = i + 1u;
        return 0;
    case (uint8_t)'\\':
        *out_cp = (uint32_t)'\\';
        *pos = i + 1u;
        return 0;
    case (uint8_t)'/':
        *out_cp = (uint32_t)'/';
        *pos = i + 1u;
        return 0;
    case (uint8_t)'b':
        *out_cp = 0x08u;
        *pos = i + 1u;
        return 0;
    case (uint8_t)'f':
        *out_cp = 0x0Cu;
        *pos = i + 1u;
        return 0;
    case (uint8_t)'n':
        *out_cp = 0x0Au;
        *pos = i + 1u;
        return 0;
    case (uint8_t)'r':
        *out_cp = 0x0Du;
        *pos = i + 1u;
        return 0;
    case (uint8_t)'t':
        *out_cp = 0x09u;
        *pos = i + 1u;
        return 0;
    case (uint8_t)'u': {
        uint32_t cp;
        uint32_t lo;

        i++;
        if (json_hex4(p, len, &i, &cp) != AICAM_TRUE) {
            return -1;
        }
        if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            return -1;
        }
        if (cp >= 0xD800u && cp <= 0xDBFFu) {
            if (i + 2u > len || p[i] != (uint8_t)'\\' || p[i + 1u] != (uint8_t)'u') {
                return -1;
            }
            i += 2u;
            if (json_hex4(p, len, &i, &lo) != AICAM_TRUE) {
                return -1;
            }
            if (lo < 0xDC00u || lo > 0xDFFFu) {
                return -1;
            }
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
        }
        *out_cp = cp;
        *pos = i;
        return 0;
    }
    default:
        return -1;
    }
}

static aicam_bool_t json_string_raw(json_scanner_t *s, uint32_t *out_off,
                                    uint32_t *out_len)
{
    uint8_t q;

    if (json_peek(s, &q) != AICAM_TRUE || q != (uint8_t)'"') {
        return AICAM_FALSE;
    }
    s->pos++;
    *out_off = s->pos;
    while (s->pos < s->len) {
        uint8_t c = s->p[s->pos];

        if (c == (uint8_t)'"') {
            *out_len = s->pos - *out_off;
            s->pos++;
            return AICAM_TRUE;
        }
        if (c < 0x20u) {
            return AICAM_FALSE;
        }
        if (c == (uint8_t)'\\') {
            uint32_t cp;

            if (json_decode_one(s->p, s->len, &s->pos, &cp) != 0) {
                return AICAM_FALSE;
            }
        } else {
            s->pos++;
        }
    }
    return AICAM_FALSE;
}

static aicam_bool_t json_key_equal(const uint8_t *p, uint32_t len,
                                   uint32_t a_off, uint32_t a_len,
                                   uint32_t b_off, uint32_t b_len)
{
    uint32_t i = a_off;
    uint32_t j = b_off;
    uint32_t a_end = a_off + a_len;
    uint32_t b_end = b_off + b_len;

    for (;;) {
        uint32_t ca;
        uint32_t cb;

        if (i >= a_end || j >= b_end) {
            break;
        }
        if (json_decode_one(p, len, &i, &ca) != 0) {
            return AICAM_FALSE;
        }
        if (json_decode_one(p, len, &j, &cb) != 0) {
            return AICAM_FALSE;
        }
        if (ca != cb) {
            return AICAM_FALSE;
        }
    }
    return (i == a_end && j == b_end) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t json_number(json_scanner_t *s)
{
    uint32_t start = s->pos;

    if (s->pos < s->len && s->p[s->pos] == (uint8_t)'-') {
        s->pos++;
    }
    if (s->pos >= s->len) {
        s->pos = start;
        return AICAM_FALSE;
    }
    if (s->p[s->pos] == (uint8_t)'0') {
        s->pos++;
    } else if (s->p[s->pos] >= (uint8_t)'1' && s->p[s->pos] <= (uint8_t)'9') {
        while (s->pos < s->len && s->p[s->pos] >= (uint8_t)'0' &&
               s->p[s->pos] <= (uint8_t)'9') {
            s->pos++;
        }
    } else {
        s->pos = start;
        return AICAM_FALSE;
    }
    if (s->pos < s->len && s->p[s->pos] == (uint8_t)'.') {
        s->pos++;
        if (s->pos >= s->len || s->p[s->pos] < (uint8_t)'0' ||
            s->p[s->pos] > (uint8_t)'9') {
            s->pos = start;
            return AICAM_FALSE;
        }
        while (s->pos < s->len && s->p[s->pos] >= (uint8_t)'0' &&
               s->p[s->pos] <= (uint8_t)'9') {
            s->pos++;
        }
    }
    if (s->pos < s->len &&
        (s->p[s->pos] == (uint8_t)'e' || s->p[s->pos] == (uint8_t)'E')) {
        s->pos++;
        if (s->pos < s->len &&
            (s->p[s->pos] == (uint8_t)'+' || s->p[s->pos] == (uint8_t)'-')) {
            s->pos++;
        }
        if (s->pos >= s->len || s->p[s->pos] < (uint8_t)'0' ||
            s->p[s->pos] > (uint8_t)'9') {
            s->pos = start;
            return AICAM_FALSE;
        }
        while (s->pos < s->len && s->p[s->pos] >= (uint8_t)'0' &&
               s->p[s->pos] <= (uint8_t)'9') {
            s->pos++;
        }
    }
    return AICAM_TRUE;
}

static aicam_bool_t json_literal(json_scanner_t *s, const char *text, uint32_t tlen)
{
    if (s->pos + tlen > s->len) {
        return AICAM_FALSE;
    }
    if (memcmp(s->p + s->pos, text, tlen) != 0) {
        return AICAM_FALSE;
    }
    s->pos += tlen;
    return AICAM_TRUE;
}

static aicam_bool_t json_value(json_scanner_t *s, uint32_t depth);

static aicam_bool_t json_object(json_scanner_t *s, uint32_t depth)
{
    json_frame_t *frame;
    uint8_t c;

    if (depth >= APP_CONTROL_JSON_MAX_DEPTH) {
        return AICAM_FALSE;
    }
    frame = &g_json_frames[depth];
    frame->kind = CTL_FRAME_KIND_OBJECT;
    frame->key_count = 0u;
    s->pos++;
    if (json_peek(s, &c) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    if (c == (uint8_t)'}') {
        s->pos++;
        return AICAM_TRUE;
    }
    for (;;) {
        uint32_t k_off;
        uint32_t k_len;
        uint32_t i;

        if (json_string_raw(s, &k_off, &k_len) != AICAM_TRUE) {
            return AICAM_FALSE;
        }
        for (i = 0u; i < frame->key_count; i++) {
            if (json_key_equal(s->p, s->len, frame->key_off[i], frame->key_len[i],
                               k_off, k_len) == AICAM_TRUE) {
                return AICAM_FALSE;
            }
        }
        if (frame->key_count >= APP_CONTROL_JSON_KEYS_MAX) {
            return AICAM_FALSE;
        }
        frame->key_off[frame->key_count] = k_off;
        frame->key_len[frame->key_count] = k_len;
        frame->key_count++;
        if (json_peek(s, &c) != AICAM_TRUE || c != (uint8_t)':') {
            return AICAM_FALSE;
        }
        s->pos++;
        if (json_value(s, depth + 1u) != AICAM_TRUE) {
            return AICAM_FALSE;
        }
        if (json_peek(s, &c) != AICAM_TRUE) {
            return AICAM_FALSE;
        }
        if (c == (uint8_t)',') {
            s->pos++;
            continue;
        }
        if (c == (uint8_t)'}') {
            s->pos++;
            return AICAM_TRUE;
        }
        return AICAM_FALSE;
    }
}

static aicam_bool_t json_array(json_scanner_t *s, uint32_t depth)
{
    uint8_t c;

    if (depth >= APP_CONTROL_JSON_MAX_DEPTH) {
        return AICAM_FALSE;
    }
    s->pos++;
    if (json_peek(s, &c) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    if (c == (uint8_t)']') {
        s->pos++;
        return AICAM_TRUE;
    }
    for (;;) {
        if (json_value(s, depth + 1u) != AICAM_TRUE) {
            return AICAM_FALSE;
        }
        if (json_peek(s, &c) != AICAM_TRUE) {
            return AICAM_FALSE;
        }
        if (c == (uint8_t)',') {
            s->pos++;
            continue;
        }
        if (c == (uint8_t)']') {
            s->pos++;
            return AICAM_TRUE;
        }
        return AICAM_FALSE;
    }
}

static aicam_bool_t json_value(json_scanner_t *s, uint32_t depth)
{
    uint8_t c;

    if (depth > APP_CONTROL_JSON_MAX_DEPTH) {
        return AICAM_FALSE;
    }
    if (json_peek(s, &c) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    switch (c) {
    case (uint8_t)'{':
        return json_object(s, depth);
    case (uint8_t)'[':
        return json_array(s, depth);
    case (uint8_t)'"': {
        uint32_t off;
        uint32_t len;

        return json_string_raw(s, &off, &len);
    }
    case (uint8_t)'t':
        return json_literal(s, "true", 4u);
    case (uint8_t)'f':
        return json_literal(s, "false", 5u);
    case (uint8_t)'n':
        return json_literal(s, "null", 4u);
    default:
        if (c == (uint8_t)'-' || (c >= (uint8_t)'0' && c <= (uint8_t)'9')) {
            return json_number(s);
        }
        return AICAM_FALSE;
    }
}

aicam_bool_t app_host_control_json_object_valid(const void *payload, uint32_t len)
{
    json_scanner_t s;
    uint8_t c;

    if (payload == NULL || len == 0u || len > APP_CONTROL_SNAPSHOT_RAM_MAX) {
        return AICAM_FALSE;
    }
    if (ctl_utf8_valid((const uint8_t *)payload, len) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    s.p = (const uint8_t *)payload;
    s.len = len;
    s.pos = 0u;
    if (json_peek(&s, &c) != AICAM_TRUE || c != (uint8_t)'{') {
        return AICAM_FALSE;
    }
    if (json_object(&s, 0u) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    json_ws(&s);
    return (aicam_bool_t)(s.pos == s.len);
}

int32_t app_host_control_snapshot_submit(const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t magic;
    uint32_t header_version;
    uint32_t msg_type;
    uint32_t total_len;
    uint32_t session_generation;
    uint32_t request_id;
    uint32_t config_generation;
    uint32_t state_revision;
    uint32_t flags;
    uint32_t payload_len;
    int32_t rc;

    if (buf == NULL || len < APP_CONTROL_HEADER_SIZE) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    ctl_lock();
    if (!g_ctl_session.active || !ctl_bit7(&g_ctl_session)) {
        ctl_unlock();
        return APP_CONTROL_UNAUTHORIZED;
    }

    magic = ctl_get_u32(p + 0);
    header_version = ctl_get_u16(p + 4);
    msg_type = ctl_get_u16(p + 6);
    total_len = ctl_get_u32(p + 8);
    session_generation = ctl_get_u32(p + 12);
    request_id = ctl_get_u32(p + 16);
    config_generation = ctl_get_u32(p + 20);
    state_revision = ctl_get_u32(p + 24);
    flags = ctl_get_u32(p + 28);
    if (magic != ctl_session_magic(&g_ctl_session) ||
        header_version != APP_CONTROL_HEADER_VERSION ||
        msg_type != APP_CONTROL_MSG_TYPE_SNAPSHOT || total_len != len) {
        ctl_unlock();
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    if (total_len > g_ctl_session.snapshot_max) {
        ctl_unlock();
        return APP_CONTROL_QUOTA_EXCEEDED;
    }
    if (session_generation != 0u || request_id != 0u || flags != 0u ||
        config_generation == 0u) {
        ctl_unlock();
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    payload_len = total_len - APP_CONTROL_HEADER_SIZE;
    if (payload_len == 0u) {
        ctl_unlock();
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    if (app_host_control_json_object_valid(p + APP_CONTROL_HEADER_SIZE,
                                           payload_len) != AICAM_TRUE) {
        ctl_unlock();
        return APP_CONTROL_INVALID_ARGUMENT;
    }

    {
        uint32_t next_version = (g_ctl_snapshot.present == AICAM_TRUE)
                                    ? (g_ctl_snapshot.visible_version + 1u)
                                    : 1u;

        memset(&g_ctl_snapshot, 0, sizeof(g_ctl_snapshot));
        g_ctl_snapshot.present = AICAM_TRUE;
        g_ctl_snapshot.visible_version = next_version;
    }
    g_ctl_snapshot.session_generation = g_ctl_session.generation;
    g_ctl_snapshot.config_generation = config_generation;
    g_ctl_snapshot.state_revision = state_revision;
    g_ctl_snapshot.arrival_ms = ctl_now_ms();
    g_ctl_snapshot.payload_len = payload_len;
    memcpy(g_ctl_snapshot.payload, p + APP_CONTROL_HEADER_SIZE, payload_len);
    memcpy(g_ctl_snapshot.publisher_id, g_ctl_session.publisher_id,
           sizeof(g_ctl_snapshot.publisher_id) - 1u);
    memcpy(g_ctl_snapshot.app_id, g_ctl_session.app_id,
           sizeof(g_ctl_snapshot.app_id) - 1u);
    g_ctl_known_valid = AICAM_TRUE;
    g_ctl_known_generation = config_generation;
    rc = APP_CONTROL_OK;
    ctl_unlock();
    return rc;
}

void app_host_control_status(app_control_status_t *out)
{
    if (out == NULL) {
        return;
    }
    ctl_lock();
    out->session_active = g_ctl_session.active;
    out->session_generation = g_ctl_session.active ? g_ctl_session.generation : 0u;
    out->caps = g_ctl_session.caps;
    out->control_max = g_ctl_session.active ? g_ctl_session.control_max : 0u;
    out->snapshot_max = g_ctl_session.active ? g_ctl_session.snapshot_max : 0u;
    out->has_known_config_generation = g_ctl_known_valid;
    out->known_config_generation = g_ctl_known_valid ? g_ctl_known_generation : 0u;
    out->has_command = g_ctl_command.present;
    out->command_state = g_ctl_command.present ? g_ctl_command.state : APP_CONTROL_CMD_NONE;
    out->command_request_id = g_ctl_command.request_id;
    out->command_msg_type = g_ctl_command.msg_type;
    out->command_expected_config_generation = g_ctl_command.expected_config_generation;
    out->command_flags = g_ctl_command.flags;
    out->command_payload_len = g_ctl_command.present
                                   ? (g_ctl_command.wire_len - APP_CONTROL_HEADER_SIZE)
                                   : 0u;
    out->result_config_generation = g_ctl_command.result_config_generation;
    out->result_state_revision = g_ctl_command.result_state_revision;
    out->unknown_count = g_ctl_unknown_count;
    out->unknown_session_generation = g_ctl_unknown_session;
    out->unknown_request_id = g_ctl_unknown_request;
    out->unknown_msg_type = g_ctl_unknown_type;
    ctl_unlock();
}

int32_t app_host_control_snapshot_read(app_control_snapshot_view_t *out,
                                       void *payload_buf, uint32_t cap,
                                       uint32_t *payload_len)
{
    int32_t rc = APP_CONTROL_OK;

    if (out == NULL) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (payload_len != NULL) {
        *payload_len = 0u;
    }
    ctl_lock();
    if (g_ctl_snapshot.present != AICAM_TRUE) {
        out->freshness = APP_CONTROL_SNAPSHOT_NONE;
        ctl_unlock();
        return APP_CONTROL_NOT_FOUND;
    }
    out->session_generation = g_ctl_snapshot.session_generation;
    out->visible_version = g_ctl_snapshot.visible_version;
    out->config_generation = g_ctl_snapshot.config_generation;
    out->state_revision = g_ctl_snapshot.state_revision;
    out->age_ms = ctl_now_ms() - g_ctl_snapshot.arrival_ms;
    out->payload_len = g_ctl_snapshot.payload_len;
    memcpy(out->publisher_id, g_ctl_snapshot.publisher_id,
           sizeof(out->publisher_id) - 1u);
    memcpy(out->app_id, g_ctl_snapshot.app_id, sizeof(out->app_id) - 1u);
    out->freshness = (out->age_ms > APP_CONTROL_STALE_MS) ? APP_CONTROL_SNAPSHOT_STALE
                                                          : APP_CONTROL_SNAPSHOT_FRESH;
    if (payload_len != NULL) {
        if (cap < g_ctl_snapshot.payload_len) {
            rc = APP_CONTROL_BUFFER_TOO_SMALL;
        } else if (payload_buf != NULL) {
            memcpy(payload_buf, g_ctl_snapshot.payload, g_ctl_snapshot.payload_len);
            *payload_len = g_ctl_snapshot.payload_len;
        }
    }
    ctl_unlock();
    return rc;
}

const char *app_host_control_register_result_str(app_control_register_result_t result)
{
    switch (result) {
    case APP_CONTROL_REG_OK:
        return "OK";
    case APP_CONTROL_REG_CACHED:
        return "CACHED";
    case APP_CONTROL_REG_ALREADY:
        return "ALREADY";
    case APP_CONTROL_ERR_PARAM:
        return "PARAM";
    case APP_CONTROL_ERR_UNAUTHORIZED:
        return "UNAUTHORIZED";
    case APP_CONTROL_ERR_INVALID:
        return "INVALID";
    case APP_CONTROL_ERR_BUSY:
        return "BUSY";
    case APP_CONTROL_ERR_CONFLICT:
        return "CONFLICT";
    case APP_CONTROL_ERR_DUPLICATE_ID_CONFLICT:
        return "DUPLICATE_ID_CONFLICT";
    case APP_CONTROL_ERR_UNCONFIRMED:
        return "UNCONFIRMED";
    case APP_CONTROL_ERR_QUOTA_EXCEEDED:
        return "QUOTA_EXCEEDED";
    case APP_CONTROL_ERR_STORAGE_GUARD:
        return "STORAGE_GUARD";
    case APP_CONTROL_ERR_GENERATION_EXHAUSTED:
        return "GENERATION_EXHAUSTED";
    default:
        return "UNKNOWN";
    }
}

const char *app_host_control_cmd_state_str(app_control_cmd_state_t state)
{
    switch (state) {
    case APP_CONTROL_CMD_NONE:
        return "none";
    case APP_CONTROL_CMD_PENDING:
        return "pending";
    case APP_CONTROL_CMD_DELIVERED:
        return "delivered";
    case APP_CONTROL_CMD_APPLIED:
        return "applied";
    case APP_CONTROL_CMD_REJECTED_STALE_CONFIG:
        return "rejected_stale_config";
    case APP_CONTROL_CMD_REJECTED_INVALID:
        return "rejected_invalid";
    case APP_CONTROL_CMD_REJECTED_RESOURCE:
        return "rejected_resource";
    case APP_CONTROL_CMD_REJECTED_UNCONFIRMED:
        return "rejected_unconfirmed";
    case APP_CONTROL_CMD_FAILED_STATE_CONFLICT:
        return "failed_state_conflict";
    case APP_CONTROL_CMD_FAILED_STORAGE_IO:
        return "failed_storage_io";
    case APP_CONTROL_CMD_FAILED_STORAGE_UNKNOWN:
        return "failed_storage_unknown";
    case APP_CONTROL_CMD_UNKNOWN:
        return "unknown";
    default:
        return "unknown_state";
    }
}

const char *app_host_control_snapshot_freshness_str(app_control_snapshot_freshness_t f)
{
    switch (f) {
    case APP_CONTROL_SNAPSHOT_NONE:
        return "unavailable";
    case APP_CONTROL_SNAPSHOT_FRESH:
        return "fresh";
    case APP_CONTROL_SNAPSHOT_STALE:
        return "stale";
    default:
        return "unknown";
    }
}

const char *app_host_control_msg_type_str(uint32_t msg_type)
{
    switch (msg_type) {
    case APP_CONTROL_MSG_TYPE_CONFIG_APPLY:
        return "config_apply";
    case APP_CONTROL_MSG_TYPE_COUNTERS_RESET:
        return "counters_reset";
    case APP_CONTROL_MSG_TYPE_RECEIPT:
        return "receipt";
    case APP_CONTROL_MSG_TYPE_SNAPSHOT:
        return "snapshot";
    default:
        return "unknown";
    }
}
