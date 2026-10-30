#include "lc_app_entry_v4.h"
#include "lc_bus.h"
#include "lc_ctrl.h"
#include "lc_arena.h"
#include "lc_compat.h"
#include "lc_lvn4.h"

static int32_t op_model_meta(void *user, uint8_t *out128)
{
    const lc_app_api_v4_t *api = (const lc_app_api_v4_t *)user;
    uint32_t alen = 0;
    int32_t r = api->model_meta(out128, LC_MODEL_META_SIZE, &alen);
    if (r == LC_RET_OK && alen != LC_MODEL_META_SIZE) return LC_RET_INVALID_ARGUMENT;
    return r;
}

static int32_t op_class_name(void *user, uint32_t model_gen, uint32_t class_gen,
                             uint32_t class_index, char *out_utf8, uint32_t cap,
                             uint32_t *actual_len)
{
    const lc_app_api_v4_t *api = (const lc_app_api_v4_t *)user;
    return api->class_name(model_gen, class_gen, class_index, out_utf8, cap,
                           actual_len);
}

static uint32_t op_tick_ms(void *user)
{
    const lc_app_api_v4_t *api = (const lc_app_api_v4_t *)user;
    return (uint32_t)api->tick_ms();
}

static int32_t op_state_read(void *user, uint8_t *out, uint32_t cap,
                             uint32_t *actual_len, uint32_t *out_revision)
{
    const lc_app_api_v4_t *api = (const lc_app_api_v4_t *)user;
    return api->state_read(out, cap, actual_len, out_revision);
}

static int32_t op_state_commit(void *user, const uint8_t *blob, uint32_t len,
                               uint32_t expected_revision, uint32_t *new_revision)
{
    const lc_app_api_v4_t *api = (const lc_app_api_v4_t *)user;
    return api->state_commit(blob, len, expected_revision, new_revision);
}

static int32_t op_report_submit(void *user, const uint8_t *json, uint32_t len,
                                uint32_t app_report_seq, uint32_t *out_host_boot_id)
{
    const lc_app_api_v4_t *api = (const lc_app_api_v4_t *)user;
    return api->report_submit(json, len, app_report_seq, out_host_boot_id);
}

static int32_t op_report_status(void *user, uint32_t host_boot_id, uint32_t report_seq,
                                uint32_t *out_mqtt_state, uint32_t *out_webhook_state)
{
    const lc_app_api_v4_t *api = (const lc_app_api_v4_t *)user;
    return api->report_status(host_boot_id, report_seq, out_mqtt_state,
                              out_webhook_state);
}

static void op_log(void *user, const char *text)
{
    const lc_app_api_v4_t *api = (const lc_app_api_v4_t *)user;
    (void)api->log(text);
}

const lcbus_host_ops_t *lcbus_abi_ops_v4(void)
{
    static const lcbus_host_ops_t ops = {
        NULL,
        op_model_meta,
        op_class_name,
        op_tick_ms,
        op_state_read,
        op_state_commit,
        op_report_submit,
        op_report_status,
        op_log
    };
    return &ops;
}

static lcbus_t lc_app_bus;
static lc_ctrl_t lc_app_ctrl;
static lc_lvn4_t lc_app_live;
static uint8_t lc_app_event_buf[LC_APP_EVENT_BUF_CAP];
static uint32_t lc_app_live_entry_epoch;

static uint32_t next_live_entry_epoch(void)
{
    if (lc_app_live_entry_epoch == 0xFFFFFFFFu) return 0u;
    lc_app_live_entry_epoch++;
    return lc_app_live_entry_epoch;
}

static int32_t op_live_publish(void *user, const void *buf, uint32_t len)
{
    const lc_app_api_v4_t *api = (const lc_app_api_v4_t *)user;
    return api->live_publish(buf, len);
}

static void observe_live(void *user, const lcbus_live_facts_t *facts)
{
    lc_lvn4_observe((lc_lvn4_t *)user, facts);
}

static void sync_live_gate(void)
{
    lc_lvn4_gate_t gate;
    if (lc_app_bus.session_fatal ||
        lc_app_bus.persist_state == LCBUS_PERSIST_CORRUPT ||
        lcbus_storage_restricted(&lc_app_bus) ||
        (lc_app_bus.enable_state != LCBUS_ENABLE_ENABLED &&
         lc_app_bus.enable_state != LCBUS_ENABLE_PAUSED)) {
        gate = LC_LVN4_GATE_UNKNOWN;
    } else if (lc_app_bus.enable_state == LCBUS_ENABLE_PAUSED) {
        gate = LC_LVN4_GATE_PAUSED;
    } else {
        gate = LC_LVN4_GATE_OPEN;
    }
    lc_lvn4_set_gate(&lc_app_live, gate);
}

int32_t app_entry(const lc_app_api_v4_t *api, uint32_t abi_version)
{
    if (api == NULL) return LC_APP_EXIT_BAD_API;
    if (api->table_size != LC_APP_API_V4_TABLE_SIZE) return LC_APP_EXIT_BAD_TABLE;
    if (abi_version != LC_APP_ABI_V4) return LC_APP_EXIT_BAD_ABI;
    if (!api->log || !api->tick_ms || !api->event_next || !api->model_meta ||
        !api->class_name || !api->report_submit || !api->report_status ||
        !api->state_read || !api->state_commit || !api->should_stop ||
        !api->control_next || !api->control_reply || !api->snapshot_publish ||
        !api->live_publish) return LC_APP_EXIT_BAD_FNPTR;

    api->log("line-crossing-app " LC_APP_SW_VERSION ": v4 session entry");
    lcapp_arena_init();
    lcbus_host_ops_t ops = *lcbus_abi_ops_v4();
    ops.user = (void *)api;
    lcbus_init(&lc_app_bus, &ops);
    lc_app_bus.state_schema = 3u;
    lc_app_bus.enable_state = LCBUS_ENABLE_UNKNOWN;
    lc_app_bus.config_generation = 1u;
    lcbus_restore(&lc_app_bus);
    if (lc_app_bus.session_fatal) {
        api->log("line-crossing-app: session not authorized");
        return LC_APP_EXIT_UNAUTHORIZED;
    }
    lc_lvn4_init(&lc_app_live, &lc_app_bus, op_live_publish, (void *)api);
    lc_lvn4_profile_t live_profile = {
        next_live_entry_epoch(),
        LC_LVN4_PROFILE_PUBLISH_MAX,
        LC_LVN4_PROFILE_TRACK_MAX
    };
    (void)lc_lvn4_arm(&lc_app_live, &live_profile);
    lc_lvn4_set_gate(&lc_app_live, LC_LVN4_GATE_UNKNOWN);
    lcbus_set_live_observer(&lc_app_bus, observe_live, &lc_app_live);
    sync_live_gate();
    lc_ctrl_init_v4(&lc_app_ctrl, &lc_app_bus, api);
    lcbus_rebind(&lc_app_bus, 1);
    lc_ctrl_snapshot_force(&lc_app_ctrl);

    int32_t exit_code = LC_APP_EXIT_OK;
    uint32_t consecutive_faults = 0u;
    uint32_t iterations = 0u;
    for (;;) {
        int32_t stop = api->should_stop();
        if (stop == 1) break;
        if (stop < 0) {
            lc_app_bus.st.host_faults++;
            if (++consecutive_faults >= 3u) {
                exit_code = LC_APP_EXIT_HOST_FAULT;
                break;
            }
        }
        lc_ctrl_poll(&lc_app_ctrl);
        sync_live_gate();
        if (lc_app_bus.session_fatal) {
            exit_code = LC_APP_EXIT_UNAUTHORIZED;
            break;
        }
        uint32_t alen = 0u;
        int32_t r = api->event_next(lc_app_event_buf,
                                    (uint32_t)sizeof(lc_app_event_buf),
                                    &alen, 1000u);
        if (r == LC_RET_OK) {
            consecutive_faults = 0u;
            if (alen > (uint32_t)sizeof(lc_app_event_buf)) {
                lc_app_bus.st.host_faults++;
                if (++consecutive_faults >= 3u) {
                    exit_code = LC_APP_EXIT_HOST_FAULT;
                    break;
                }
                continue;
            }
            lcbus_on_event(&lc_app_bus, lc_app_event_buf, alen);
        } else if (r == LC_RET_NO_EVENT) {
            lcbus_on_idle(&lc_app_bus);
        } else if (r == LC_RET_STOPPING) {
            break;
        } else if (r == LC_RET_UNAUTHORIZED) {
            lc_app_bus.session_fatal = 1u;
        } else {
            lc_app_bus.st.host_faults++;
            if (++consecutive_faults >= 3u) {
                exit_code = LC_APP_EXIT_HOST_FAULT;
                break;
            }
            lcbus_on_idle(&lc_app_bus);
        }
        if (lc_app_bus.session_fatal) {
            exit_code = LC_APP_EXIT_UNAUTHORIZED;
            break;
        }
        if (lc_app_bus.stop_requested) break;
        if (lc_app_bus.resource_fatal) {
            exit_code = LC_APP_EXIT_RESOURCES;
            break;
        }
        if (++iterations > 1000000u) {
            exit_code = LC_APP_EXIT_HOST_FAULT;
            break;
        }
    }
    lcbus_flush(&lc_app_bus, 1);
    lc_lvn4_disarm(&lc_app_live);
    api->log("line-crossing-app: v4 session end");
    return exit_code;
}
