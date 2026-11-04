
#include "lc_ctrl.h"
#include "lc_cfgcodec.h"
#include "lc_json.h"
#include "lc_compat.h"

static uint8_t lc_ctrl_cmd_buf[LC_CTRL_MSG_BUF_CAP];
static uint8_t lc_ctrl_snapshot_buf[LC_SNAPSHOT_BUF_CAP];

static uint32_t ctrl_now(const lc_ctrl_t *c)
{
    return c->bus->ops.tick_ms(c->bus->ops.user);
}

static void ctrl_log(const lc_ctrl_t *c, const char *s)
{
    if (c->bus->ops.log) c->bus->ops.log(c->bus->ops.user, s);
}

void lc_ctrl_init(lc_ctrl_t *c, lcbus_t *bus, const lc_app_api_v3_t *api)
{
    lc_memset(c, 0, sizeof(*c));
    c->bus = bus;
    c->control_next = api->control_next;
    c->control_reply = api->control_reply;
    c->snapshot_publish = api->snapshot_publish;
    c->last_snapshot_ms = ctrl_now(c);
}

void lc_ctrl_init_v4(lc_ctrl_t *c, lcbus_t *bus, const lc_app_api_v4_t *api)
{
    lc_memset(c, 0, sizeof(*c));
    c->bus = bus;
    c->control_next = api->control_next;
    c->control_reply = api->control_reply;
    c->snapshot_publish = api->snapshot_publish;
    c->protocol_major = 4u;
    c->last_snapshot_ms = ctrl_now(c);
}

static void send_reply(lc_ctrl_t *c, uint32_t session_generation,
                       uint32_t request_id, uint32_t result_status,
                       uint32_t result_generation, uint32_t result_revision)
{
    uint8_t r[LC_CTRL_HEADER_SIZE];
    lc_memset(r, 0, sizeof(r));
    lc_wr_u32(r + LC_CTRL_OFF_PROTO_MAGIC,
              c->protocol_major == 4u ? LC_CTRL_V4_PROTO_MAGIC : LC_CTRL_PROTO_MAGIC);
    r[LC_CTRL_OFF_HEADER_VERSION] = (uint8_t)LC_CTRL_HEADER_VERSION;
    r[LC_CTRL_OFF_HEADER_VERSION + 1u] = 0u;
    r[LC_CTRL_OFF_MSG_TYPE] = (uint8_t)LC_CTRL_TYPE_REPLY;
    r[LC_CTRL_OFF_MSG_TYPE + 1u] = 0u;
    lc_wr_u32(r + LC_CTRL_OFF_TOTAL_LEN, LC_CTRL_HEADER_SIZE);
    lc_wr_u32(r + LC_CTRL_OFF_SESSION_GENERATION, session_generation);
    lc_wr_u32(r + LC_CTRL_OFF_REQUEST_ID, request_id);
    lc_wr_u32(r + LC_CTRL_OFF_CONFIG_GENERATION, result_generation);
    lc_wr_u32(r + LC_CTRL_OFF_STATE_REVISION, result_revision);
    lc_wr_u32(r + LC_CTRL_OFF_FLAGS, result_status);
    int32_t rr = c->control_reply(r, (uint32_t)sizeof(r));
    if (rr == LC_RET_OK) {
        c->st.replies_sent++;
        return;
    }
    if (rr == LC_RET_UNAUTHORIZED) {
        c->bus->session_fatal = 1;
        return;
    }
    c->bus->st.host_faults++;
}

static void send_enable_receipt(lc_ctrl_t *c, uint32_t session_generation,
                                uint32_t request_id, uint32_t result_status,
                                uint32_t result_generation, uint32_t result_revision,
                                uint32_t effective_state)
{
    uint8_t r[LC_CTRL_V4_ENABLE_RECEIPT_SIZE];
    lc_memset(r, 0, sizeof(r));
    lc_wr_u32(r + LC_CTRL_OFF_PROTO_MAGIC, LC_CTRL_V4_PROTO_MAGIC);
    r[LC_CTRL_OFF_HEADER_VERSION] = (uint8_t)LC_CTRL_HEADER_VERSION;
    r[LC_CTRL_OFF_HEADER_VERSION + 1u] = 0u;
    r[LC_CTRL_OFF_MSG_TYPE] = (uint8_t)LC_CTRL_TYPE_REPLY;
    r[LC_CTRL_OFF_MSG_TYPE + 1u] = 0u;
    lc_wr_u32(r + LC_CTRL_OFF_TOTAL_LEN, (uint32_t)sizeof(r));
    lc_wr_u32(r + LC_CTRL_OFF_SESSION_GENERATION, session_generation);
    lc_wr_u32(r + LC_CTRL_OFF_REQUEST_ID, request_id);
    lc_wr_u32(r + LC_CTRL_OFF_CONFIG_GENERATION, result_generation);
    lc_wr_u32(r + LC_CTRL_OFF_STATE_REVISION, result_revision);
    lc_wr_u32(r + LC_CTRL_OFF_FLAGS, result_status);
    lc_wr_u32(r + 32u, LC_CTRL_V4_RECEIPT_MAGIC);
    lc_wr_u32(r + 36u, effective_state);
    lc_wr_u32(r + 40u, result_revision);
    int32_t rr = c->control_reply(r, (uint32_t)sizeof(r));
    if (rr == LC_RET_OK) {
        c->st.replies_sent++;
        return;
    }
    if (rr == LC_RET_UNAUTHORIZED) {
        c->bus->session_fatal = 1;
        return;
    }
    c->bus->st.host_faults++;
}

static uint32_t effective_enable_state(const lcbus_t *b)
{
    if (b->enable_state == LCBUS_ENABLE_ENABLED) return 1u;
    if (b->enable_state == LCBUS_ENABLE_PAUSED) return 2u;
    return LC_CTRL_V4_ENABLE_UNKNOWN;
}

static void send_command_reply(lc_ctrl_t *c, const uint8_t *cmd,
                               uint32_t result_status, uint32_t result_generation,
                               uint32_t result_revision, uint32_t effective_state)
{
    uint32_t type = (uint32_t)cmd[LC_CTRL_OFF_MSG_TYPE] |
                    ((uint32_t)cmd[LC_CTRL_OFF_MSG_TYPE + 1u] << 8);
    uint32_t session = lc_rd_u32(cmd + LC_CTRL_OFF_SESSION_GENERATION);
    uint32_t request_id = lc_rd_u32(cmd + LC_CTRL_OFF_REQUEST_ID);
    if (c->protocol_major == 4u && type == LC_CTRL_V4_TYPE_ENABLE_SET) {
        send_enable_receipt(c, session, request_id, result_status,
                            result_generation, result_revision, effective_state);
    } else {
        send_reply(c, session, request_id, result_status,
                   result_generation, result_revision);
    }
}

static lc_ctrl_dedup_slot_t *dedup_find(lc_ctrl_t *c, uint32_t request_id)
{
    for (uint32_t i = 0; i < LC_CTRL_DEDUP_SLOTS; i++) {
        if (c->dedup[i].in_use && c->dedup[i].request_id == request_id) {
            return &c->dedup[i];
        }
    }
    return NULL;
}

static void dedup_record(lc_ctrl_t *c, const uint8_t *cmd, uint32_t len,
                         uint32_t result_status, uint32_t result_generation,
                         uint32_t result_revision, uint32_t result_enable_state)
{
    lc_ctrl_dedup_slot_t *slot = &c->dedup[c->dedup_cursor];
    c->dedup_cursor = (c->dedup_cursor + 1u) % LC_CTRL_DEDUP_SLOTS;
    slot->in_use = 1;
    slot->request_id = lc_rd_u32(cmd + LC_CTRL_OFF_REQUEST_ID);
    slot->cmd_len = len;
    lc_memcpy(slot->cmd, cmd, len);
    slot->result_status = result_status;
    slot->result_generation = result_generation;
    slot->result_revision = result_revision;
    slot->result_enable_state = result_enable_state;
}

static void reject_result(lc_ctrl_t *c, uint32_t status)
{
    switch (status) {
    case LC_CTRL_RESULT_REJECTED_STALE_CONFIG: c->st.rejected_stale_config++; break;
    case LC_CTRL_RESULT_REJECTED_INVALID: c->st.rejected_invalid++; break;
    case LC_CTRL_RESULT_REJECTED_RESOURCE: c->st.rejected_resource++; break;
    case LC_CTRL_RESULT_REJECTED_UNCONFIRMED: c->st.rejected_unconfirmed++; break;
    case LC_CTRL_RESULT_FAILED_STATE_CONFLICT: c->st.failed_state_conflict++; break;
    case LC_CTRL_RESULT_FAILED_STORAGE_IO: c->st.failed_storage_io++; break;
    case LC_CTRL_RESULT_FAILED_STORAGE_UNKNOWN: c->st.failed_storage_unknown++; break;
    default: break;
    }
}

static void finish_command(lc_ctrl_t *c, const uint8_t *cmd, uint32_t len,
                           uint32_t result_status, uint32_t result_generation)
{
    lcbus_t *b = c->bus;
    uint32_t revision = b->revision;
    reject_result(c, result_status);
    if (result_status == LC_CTRL_RESULT_APPLIED) c->st.applied++;
    uint32_t effective = effective_enable_state(b);
    send_command_reply(c, cmd, result_status, result_generation, revision, effective);
    dedup_record(c, cmd, len, result_status, result_generation, revision, effective);
    if (result_status == LC_CTRL_RESULT_APPLIED) lc_ctrl_snapshot_force(c);
}

static int wire_header_valid(const lc_ctrl_t *c, const uint8_t *cmd, uint32_t len)
{
    if (len < LC_CTRL_HEADER_SIZE) return 0;
    uint32_t magic = c->protocol_major == 4u ? LC_CTRL_V4_PROTO_MAGIC
                                             : LC_CTRL_PROTO_MAGIC;
    if (lc_rd_u32(cmd + LC_CTRL_OFF_PROTO_MAGIC) != magic) return 0;
    if (cmd[LC_CTRL_OFF_HEADER_VERSION] != (uint8_t)LC_CTRL_HEADER_VERSION ||
        cmd[LC_CTRL_OFF_HEADER_VERSION + 1u] != 0u) return 0;
    uint32_t type = (uint32_t)cmd[LC_CTRL_OFF_MSG_TYPE] |
                    ((uint32_t)cmd[LC_CTRL_OFF_MSG_TYPE + 1u] << 8);
    if (type != LC_CTRL_TYPE_CONFIG_APPLY && type != LC_CTRL_TYPE_COUNTERS_RESET &&
        !(c->protocol_major == 4u && type == LC_CTRL_V4_TYPE_ENABLE_SET)) return 0;
    if (lc_rd_u32(cmd + LC_CTRL_OFF_TOTAL_LEN) != len) return 0;
    if (lc_rd_u32(cmd + LC_CTRL_OFF_SESSION_GENERATION) == 0u) return 0;
    if (lc_rd_u32(cmd + LC_CTRL_OFF_REQUEST_ID) == 0u) return 0;
    if (lc_rd_u32(cmd + LC_CTRL_OFF_CONFIG_GENERATION) == 0u) return 0;
    if (lc_rd_u32(cmd + LC_CTRL_OFF_STATE_REVISION) != 0u) return 0;
    if ((lc_rd_u32(cmd + LC_CTRL_OFF_FLAGS) & ~LC_CTRL_FLAGS_VALID_MASK) != 0u) return 0;
    if (type == LC_CTRL_TYPE_COUNTERS_RESET && len != LC_CTRL_HEADER_SIZE) return 0;
    if (type == LC_CTRL_TYPE_CONFIG_APPLY && len <= LC_CTRL_HEADER_SIZE) return 0;
    if (type == LC_CTRL_V4_TYPE_ENABLE_SET) {
        if (len != LC_CTRL_V4_ENABLE_SET_SIZE) return 0;
        if (lc_rd_u32(cmd + LC_CTRL_OFF_FLAGS) != 0u) return 0;
        if (lc_rd_u32(cmd + 32u) != LC_CTRL_V4_ENABLE_MAGIC) return 0;
        uint32_t desired = lc_rd_u32(cmd + 36u);
        if (desired != 1u && desired != 2u) return 0;
        if (lc_rd_u32(cmd + 44u) != 0u) return 0;
    }
    return 1;
}

static void handle_config_apply(lc_ctrl_t *c, const uint8_t *cmd, uint32_t len)
{
    lcbus_t *b = c->bus;
    uint32_t expected_gen = lc_rd_u32(cmd + LC_CTRL_OFF_CONFIG_GENERATION);
    uint32_t flags = lc_rd_u32(cmd + LC_CTRL_OFF_FLAGS);
    if (expected_gen != b->config_generation) {
        ctrl_log(c, "LC_CTRL: CONFIG_APPLY stale expected_config_generation");
        finish_command(c, cmd, len, LC_CTRL_RESULT_REJECTED_STALE_CONFIG,
                       b->config_generation);
        return;
    }
    lc_bus_config_t cand;
    lc_cfg_status_t d = lc_cfg_decode(cmd + LC_CTRL_HEADER_SIZE,
                                      len - LC_CTRL_HEADER_SIZE, &cand);
    if (d != LC_CFG_OK) {
        if (d == LC_CFG_ERR_STATE_BLOB) {
            ctrl_log(c, "LC_CTRL: candidate rejected: private state blob shape");
        } else {
            ctrl_log(c, "LC_CTRL: candidate rejected: invalid config encoding");
        }
        finish_command(c, cmd, len, LC_CTRL_RESULT_REJECTED_INVALID,
                       b->config_generation);
        return;
    }
    int target_changed = lc_strcmp(cand.target_class_name,
                                   b->cfg.target_class_name) != 0;
    if (target_changed && !(flags & LC_CTRL_FLAG_DESTRUCTIVE_CONFIRMED)) {
        ctrl_log(c, "LC_CTRL: target change without destructive confirmation");
        finish_command(c, cmd, len, LC_CTRL_RESULT_REJECTED_UNCONFIRMED,
                       b->config_generation);
        return;
    }
    lcbus_cfg_txn_t txn;
    int p = lcbus_txn_prepare_config(b, &cand, &txn);
    if (p != LCBUS_TXN_OK) {
        ctrl_log(c, "LC_CTRL: CONFIG_APPLY preparation failed");
        finish_command(c, cmd, len,
                       p == LCBUS_TXN_ERR_INVALID ? LC_CTRL_RESULT_REJECTED_INVALID
                                                  : LC_CTRL_RESULT_REJECTED_RESOURCE,
                       b->config_generation);
        return;
    }
    int cm = lcbus_txn_commit_config(b, &txn);
    switch (cm) {
    case LCBUS_TXN_OK:
        ctrl_log(c, "LC_CTRL: CONFIG_APPLY committed and switched");
        finish_command(c, cmd, len, LC_CTRL_RESULT_APPLIED, b->config_generation);
        return;
    case LCBUS_TXN_ERR_STATE_CONFLICT:
        ctrl_log(c, "LC_CTRL: CONFIG_APPLY state commit revision conflict; old state kept");
        finish_command(c, cmd, len, LC_CTRL_RESULT_FAILED_STATE_CONFLICT,
                       b->config_generation);
        return;
    case LCBUS_TXN_ERR_STORAGE_UNKNOWN:
        ctrl_log(c, "LC_CTRL: CONFIG_APPLY storage outcome unknown; restricted");
        finish_command(c, cmd, len, LC_CTRL_RESULT_FAILED_STORAGE_UNKNOWN,
                       b->config_generation);
        return;
    default:
        ctrl_log(c, "LC_CTRL: CONFIG_APPLY storage write failed; old state kept");
        finish_command(c, cmd, len, LC_CTRL_RESULT_FAILED_STORAGE_IO,
                       b->config_generation);
        return;
    }
}

static void handle_counters_reset(lc_ctrl_t *c, const uint8_t *cmd, uint32_t len)
{
    lcbus_t *b = c->bus;
    uint32_t expected_gen = lc_rd_u32(cmd + LC_CTRL_OFF_CONFIG_GENERATION);
    uint32_t flags = lc_rd_u32(cmd + LC_CTRL_OFF_FLAGS);
    if (!(flags & LC_CTRL_FLAG_DESTRUCTIVE_CONFIRMED)) {
        ctrl_log(c, "LC_CTRL: COUNTERS_RESET without destructive confirmation");
        finish_command(c, cmd, len, LC_CTRL_RESULT_REJECTED_UNCONFIRMED,
                       b->config_generation);
        return;
    }
    if (expected_gen != b->config_generation) {
        ctrl_log(c, "LC_CTRL: COUNTERS_RESET stale expected_config_generation");
        finish_command(c, cmd, len, LC_CTRL_RESULT_REJECTED_STALE_CONFIG,
                       b->config_generation);
        return;
    }
    int r = lcbus_txn_commit_reset(b, 1);
    switch (r) {
    case LCBUS_TXN_OK:
        ctrl_log(c, "LC_CTRL: COUNTERS_RESET committed at event boundary");
        finish_command(c, cmd, len, LC_CTRL_RESULT_APPLIED, b->config_generation);
        return;
    case LCBUS_TXN_ERR_STATE_CONFLICT:
        ctrl_log(c, "LC_CTRL: COUNTERS_RESET state commit revision conflict; old state kept");
        finish_command(c, cmd, len, LC_CTRL_RESULT_FAILED_STATE_CONFLICT,
                       b->config_generation);
        return;
    case LCBUS_TXN_ERR_STORAGE_UNKNOWN:
        ctrl_log(c, "LC_CTRL: COUNTERS_RESET storage outcome unknown; restricted");
        finish_command(c, cmd, len, LC_CTRL_RESULT_FAILED_STORAGE_UNKNOWN,
                       b->config_generation);
        return;
    default:
        ctrl_log(c, "LC_CTRL: COUNTERS_RESET storage write failed; old state kept");
        finish_command(c, cmd, len, LC_CTRL_RESULT_FAILED_STORAGE_IO,
                       b->config_generation);
        return;
    }
}

static void handle_enable_set(lc_ctrl_t *c, const uint8_t *cmd, uint32_t len)
{
    lcbus_t *b = c->bus;
    uint32_t desired = lc_rd_u32(cmd + 36u);
    uint32_t expected_revision = lc_rd_u32(cmd + 40u);
    uint32_t expected_generation = lc_rd_u32(cmd + LC_CTRL_OFF_CONFIG_GENERATION);
    uint32_t new_revision = b->revision;
    int result = lcbus_enable_set(b, desired, expected_generation,
                                  expected_revision, &new_revision);
    uint32_t status;
    switch (result) {
    case LCBUS_TXN_OK:
        status = LC_CTRL_RESULT_APPLIED;
        break;
    case LCBUS_TXN_ERR_STALE_CONFIG:
        status = LC_CTRL_RESULT_REJECTED_STALE_CONFIG;
        break;
    case LCBUS_TXN_ERR_INVALID:
        status = LC_CTRL_RESULT_REJECTED_INVALID;
        break;
    case LCBUS_TXN_ERR_RESOURCE:
        status = LC_CTRL_RESULT_REJECTED_RESOURCE;
        break;
    case LCBUS_TXN_ERR_STATE_CONFLICT:
        status = LC_CTRL_RESULT_FAILED_STATE_CONFLICT;
        break;
    case LCBUS_TXN_ERR_STORAGE_UNKNOWN:
        status = LC_CTRL_RESULT_FAILED_STORAGE_UNKNOWN;
        break;
    default:
        status = LC_CTRL_RESULT_FAILED_STORAGE_IO;
        break;
    }
    if (status == LC_CTRL_RESULT_APPLIED &&
        (new_revision != b->revision || (uint32_t)b->enable_state != desired)) {
        b->enable_state = LCBUS_ENABLE_UNKNOWN;
        b->storage_unknown_latch = 1u;
        b->persist_state = LCBUS_PERSIST_DEGRADED;
        status = LC_CTRL_RESULT_FAILED_STORAGE_UNKNOWN;
    }
    finish_command(c, cmd, len, status, b->config_generation);
}

static void handle_command(lc_ctrl_t *c, const uint8_t *cmd, uint32_t len)
{
    lcbus_t *b = c->bus;
    uint32_t type = (uint32_t)cmd[LC_CTRL_OFF_MSG_TYPE] |
                    ((uint32_t)cmd[LC_CTRL_OFF_MSG_TYPE + 1u] << 8);
    uint32_t request_id = lc_rd_u32(cmd + LC_CTRL_OFF_REQUEST_ID);

    lc_ctrl_dedup_slot_t *slot = dedup_find(c, request_id);
    if (slot) {
        if (slot->cmd_len == len && lc_memcmp(slot->cmd, cmd, len) == 0) {
            c->st.dedup_cached++;
            ctrl_log(c, "LC_CTRL: duplicate request_id with identical semantics; cached result reused");
            send_command_reply(c, cmd, slot->result_status,
                               slot->result_generation, slot->result_revision,
                               slot->result_enable_state);
            return;
        }
        c->st.dedup_conflict++;
        ctrl_log(c, "LC_CTRL: duplicate request_id with different semantics; rejected");
        send_command_reply(c, cmd, LC_CTRL_RESULT_REJECTED_INVALID,
                           b->config_generation, b->revision,
                           effective_enable_state(b));
        return;
    }

    if (b->session_fatal || lcbus_storage_restricted(b) ||
        (b->persist_state == LCBUS_PERSIST_CORRUPT &&
         type != LC_CTRL_TYPE_COUNTERS_RESET)) {
        ctrl_log(c, "LC_CTRL: storage restricted; side-effect command refused");
        finish_command(c, cmd, len, LC_CTRL_RESULT_FAILED_STORAGE_UNKNOWN,
                       b->config_generation);
        return;
    }

    if (type == LC_CTRL_TYPE_CONFIG_APPLY) {
        handle_config_apply(c, cmd, len);
    } else if (type == LC_CTRL_TYPE_COUNTERS_RESET) {
        handle_counters_reset(c, cmd, len);
    } else {
        handle_enable_set(c, cmd, len);
    }
}

static void take_command(lc_ctrl_t *c)
{
    uint32_t alen = 0;
    int32_t r = c->control_next(lc_ctrl_cmd_buf,
                                (uint32_t)sizeof(lc_ctrl_cmd_buf), &alen);
    if (r == LC_RET_NO_EVENT || r == LC_RET_NOT_FOUND) return;
    if (r == LC_RET_UNAUTHORIZED) {
        c->bus->session_fatal = 1;
        return;
    }
    if (r == LC_RET_STOPPING) {
        c->bus->stop_requested = 1;
        return;
    }
    if (r != LC_RET_OK) {
        c->bus->st.host_faults++;
        return;
    }
    if (alen > (uint32_t)sizeof(lc_ctrl_cmd_buf)) {
        c->bus->st.host_faults++;
        return;
    }
    c->st.commands_received++;
    if (!wire_header_valid(c, lc_ctrl_cmd_buf, alen)) {
        c->st.wire_rejected++;
        ctrl_log(c, "LC_CTRL: command failed wire header validation; dropped without reply");
        return;
    }
    if (c->protocol_major == 4u) {
        uint32_t session = lc_rd_u32(lc_ctrl_cmd_buf + LC_CTRL_OFF_SESSION_GENERATION);
        if (c->session_generation_valid && c->session_generation != session) {
            c->st.wire_rejected++;
            c->bus->session_fatal = 1u;
            return;
        }
        c->session_generation = session;
        c->session_generation_valid = 1u;
    }
    handle_command(c, lc_ctrl_cmd_buf, alen);
}

static uint32_t app_state(const lcbus_t *b)
{
    if (lcbus_storage_restricted(b) ||
        b->persist_state == LCBUS_PERSIST_CORRUPT) {
        return LC_CTRL_APP_STATE_RESTRICTED;
    }
    if (!b->binding.bound || b->model_state != LCBUS_MSTATE_RUNNING) {
        return LC_CTRL_APP_STATE_UNBOUND;
    }
    return LC_CTRL_APP_STATE_RUNNING;
}

static uint32_t storage_state(const lcbus_t *b)
{
    switch (b->persist_state) {
    case LCBUS_PERSIST_OK: return LC_CTRL_STORAGE_OK;
    case LCBUS_PERSIST_NONE: return LC_CTRL_STORAGE_NONE;
    case LCBUS_PERSIST_DEGRADED: return LC_CTRL_STORAGE_DEGRADED;
    case LCBUS_PERSIST_CORRUPT: return LC_CTRL_STORAGE_CORRUPT;
    case LCBUS_PERSIST_CONFLICT: return LC_CTRL_STORAGE_CONFLICT;
    default: return LC_CTRL_STORAGE_UNKNOWN;
    }
}

static const char *app_state_name(uint32_t st)
{
    switch (st) {
    case LC_CTRL_APP_STATE_RUNNING: return "running";
    case LC_CTRL_APP_STATE_UNBOUND: return "unbound";
    default: return "restricted";
    }
}

static const char *storage_state_name(uint32_t st)
{
    switch (st) {
    case LC_CTRL_STORAGE_OK: return "ok";
    case LC_CTRL_STORAGE_NONE: return "none";
    case LC_CTRL_STORAGE_DEGRADED: return "degraded";
    case LC_CTRL_STORAGE_CORRUPT: return "corrupt";
    case LC_CTRL_STORAGE_CONFLICT: return "conflict";
    default: return "unknown";
    }
}

static const char *enable_state_name(const lcbus_t *b)
{
    if (b->enable_state == LCBUS_ENABLE_ENABLED) return "enabled";
    if (b->enable_state == LCBUS_ENABLE_PAUSED) return "paused";
    return "unknown";
}

static uint32_t build_snapshot_json(const lcbus_t *b, char *out, uint32_t cap,
                                   uint8_t protocol_major, uint32_t now_ms)
{
    lc_json_t j;
    lcj_init(&j, out, cap);
    uint32_t astate = app_state(b);
    uint32_t sstate = storage_state(b);

    lcj_raw(&j, "{\"schema\":");
    lcj_string(&j, "line-crossing/1");
    lcj_raw(&j, ",\"config_generation\":");
    lcj_u32(&j, b->config_generation);
    lcj_raw(&j, ",\"state_revision\":");
    lcj_u32(&j, b->revision);
    if (protocol_major == 4u) {
        lcj_raw(&j, ",\"enable_state\":");
        lcj_string(&j, enable_state_name(b));
        lcj_raw(&j, ",\"video_freshness\":{\"valid\":");
        lcj_bool(&j, b->last_frame_received_valid);
        lcj_raw(&j, ",\"last_frame_received_ms\":");
        lcj_u32(&j, b->last_frame_received_ms);
        lcj_raw(&j, ",\"age_ms\":");
        lcj_u32(&j, b->last_frame_received_valid
                        ? lc_tick_diff(now_ms, b->last_frame_received_ms) : 0u);
        lcj_raw(&j, "},\"live_freshness\":\"not_connected\"");
    }
    lcj_raw(&j, ",\"app_state\":");
    lcj_string(&j, app_state_name(astate));
    lcj_raw(&j, ",\"storage\":{\"state\":");
    lcj_string(&j, storage_state_name(sstate));
    lcj_raw(&j, ",\"withheld\":");
    lcj_bool(&j, astate == LC_CTRL_APP_STATE_RESTRICTED);
    lcj_raw(&j, "},\"report_seq\":");
    lcj_u32(&j, b->report_seq);
    lcj_raw(&j, ",\"cumulative\":{\"in\":");
    lcj_u32(&j, b->total_in);
    lcj_raw(&j, ",\"out\":");
    lcj_u32(&j, b->total_out);
    lcj_raw(&j, "},\"counters\":{\"in\":");
    lcj_u32(&j, b->window_in);
    lcj_raw(&j, ",\"out\":");
    lcj_u32(&j, b->window_out);
    lcj_raw(&j, "},\"gap\":{\"gaps\":");
    lcj_u32(&j, b->gaps_window);
    lcj_raw(&j, ",\"lost\":");
    lcj_u32(&j, b->lost_window);
    lcj_raw(&j, ",\"lost_unknown\":");
    lcj_bool(&j, b->lost_unknown_window);
    lcj_raw(&j, ",\"unusable\":");
    lcj_u32(&j, b->unusable_window);
    lcj_raw(&j, "},\"model\":{\"valid\":");
    lcj_bool(&j, b->binding.bound && b->model_state == LCBUS_MSTATE_RUNNING);
    lcj_raw(&j, ",\"generation\":");
    lcj_u32(&j, b->binding.model_generation);
    lcj_raw(&j, ",\"class_generation\":");
    lcj_u32(&j, b->binding.class_generation);
    lcj_raw(&j, ",\"target_class\":");
    lcj_string(&j, b->binding.target_name);
    lcj_raw(&j, "},\"active_config\":{\"counter_name\":");
    lcj_string(&j, b->cfg.counter_name);
    lcj_raw(&j, ",\"target_class_name\":");
    lcj_string(&j, b->cfg.target_class_name);
    lcj_raw(&j, ",\"line\":{\"x1_permille\":");
    lcj_u32(&j, b->cfg.line_x1_permille);
    lcj_raw(&j, ",\"y1_permille\":");
    lcj_u32(&j, b->cfg.line_y1_permille);
    lcj_raw(&j, ",\"x2_permille\":");
    lcj_u32(&j, b->cfg.line_x2_permille);
    lcj_raw(&j, ",\"y2_permille\":");
    lcj_u32(&j, b->cfg.line_y2_permille);
    lcj_raw(&j, "},\"outside\":{\"x_permille\":");
    lcj_u32(&j, b->cfg.outside_x_permille);
    lcj_raw(&j, ",\"y_permille\":");
    lcj_u32(&j, b->cfg.outside_y_permille);
    lcj_raw(&j, "},\"conf_threshold_permille\":");
    lcj_u32(&j, b->cfg.conf_threshold_permille);
    lcj_raw(&j, ",\"max_dist_permille\":");
    lcj_u32(&j, b->cfg.max_dist_permille);
    lcj_raw(&j, ",\"track_history_k\":");
    lcj_u32(&j, b->cfg.track_history_k);
    lcj_raw(&j, ",\"max_miss\":");
    lcj_u32(&j, b->cfg.max_miss);
    lcj_raw(&j, ",\"k_confirm\":");
    lcj_u32(&j, b->cfg.k_confirm);
    lcj_raw(&j, ",\"window_minutes\":");
    lcj_u32(&j, b->cfg.window_minutes);
    lcj_raw(&j, ",\"tracks_report_enable\":");
    lcj_bool(&j, b->cfg.tracks_report_enable);
    lcj_raw(&j, ",\"heat_grid_enable\":");
    lcj_bool(&j, b->cfg.heat_grid_enable);
    lcj_raw(&j, "}}");

    if (j.overflow) return 0;
    return j.len;
}

static void publish_snapshot(lc_ctrl_t *c)
{
    lcbus_t *b = c->bus;
    uint32_t plen = build_snapshot_json(
        b, (char *)lc_ctrl_snapshot_buf + LC_CTRL_HEADER_SIZE,
        (uint32_t)sizeof(lc_ctrl_snapshot_buf) - LC_CTRL_HEADER_SIZE,
        c->protocol_major, c->protocol_major == 4u ? ctrl_now(c) : 0u);
    if (plen == 0u) {
        c->st.snapshots_rejected++;
        ctrl_log(c, "LC_CTRL: snapshot projection overflow; whole snapshot withheld");
        return;
    }
    uint8_t *hdr = lc_ctrl_snapshot_buf;
    lc_memset(hdr, 0, LC_CTRL_HEADER_SIZE);
    lc_wr_u32(hdr + LC_CTRL_OFF_PROTO_MAGIC,
              c->protocol_major == 4u ? LC_CTRL_V4_PROTO_MAGIC : LC_CTRL_PROTO_MAGIC);
    hdr[LC_CTRL_OFF_HEADER_VERSION] = (uint8_t)LC_CTRL_HEADER_VERSION;
    hdr[LC_CTRL_OFF_HEADER_VERSION + 1u] = 0u;
    hdr[LC_CTRL_OFF_MSG_TYPE] = (uint8_t)LC_CTRL_TYPE_SNAPSHOT;
    hdr[LC_CTRL_OFF_MSG_TYPE + 1u] = 0u;
    lc_wr_u32(hdr + LC_CTRL_OFF_TOTAL_LEN, LC_CTRL_HEADER_SIZE + plen);
    lc_wr_u32(hdr + LC_CTRL_OFF_SESSION_GENERATION, 0u);
    lc_wr_u32(hdr + LC_CTRL_OFF_REQUEST_ID, 0u);
    lc_wr_u32(hdr + LC_CTRL_OFF_CONFIG_GENERATION, b->config_generation);
    lc_wr_u32(hdr + LC_CTRL_OFF_STATE_REVISION, b->revision);
    lc_wr_u32(hdr + LC_CTRL_OFF_FLAGS, 0u);
    int32_t r = c->snapshot_publish(lc_ctrl_snapshot_buf, LC_CTRL_HEADER_SIZE + plen);
    if (r == LC_RET_OK) {
        c->st.snapshots_published++;
        c->last_snapshot_ms = ctrl_now(c);
        c->last_snapshot_valid = 1;
        c->snapshot_forced = 0;
        return;
    }
    if (r == LC_RET_UNAUTHORIZED) {
        b->session_fatal = 1;
        return;
    }
    c->st.snapshots_rejected++;
    if (r == LC_RET_QUOTA_EXCEEDED) {
        ctrl_log(c, "LC_CTRL: snapshot rejected by host quota; whole snapshot withheld");
    }
}

void lc_ctrl_snapshot_force(lc_ctrl_t *c)
{
    c->snapshot_forced = 1;
}

void lc_ctrl_poll(lc_ctrl_t *c)
{
    lcbus_t *b = c->bus;
    if (b->session_fatal) return;
    take_command(c);
    if (b->session_fatal) return;
    uint32_t now = ctrl_now(c);
    if (c->snapshot_forced || !c->last_snapshot_valid ||
        lc_tick_diff(now, c->last_snapshot_ms) >= LC_SNAPSHOT_INTERVAL_MS) {
        publish_snapshot(c);
    }
}
