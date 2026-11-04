
#ifndef LCSTUB_H
#define LCSTUB_H

#include <stdint.h>
#include "lc_app_abi_v2.h"
#include "lc_app_abi_v3.h"
#include "lc_app_abi_v4.h"

#define LCSTUB_MAX_CLASSES  16
#define LCSTUB_MAX_EVENTS   64
#define LCSTUB_MAX_REPORTS  16
#define LCSTUB_LOG_LINES    64
#define LCSTUB_LOG_LINE_LEN 96
#define LCSTUB_STATE_CAP    4096
#define LCSTUB_EVENT_MAX    1600
#define LCSTUB_MAX_COMMANDS 8
#define LCSTUB_MAX_REPLIES  16
#define LCSTUB_MAX_SNAPSHOTS 8

typedef enum {
    LCSTUB_FN_LOG = 0,
    LCSTUB_FN_TICK,
    LCSTUB_FN_EVENT_NEXT,
    LCSTUB_FN_MODEL_META,
    LCSTUB_FN_CLASS_NAME,
    LCSTUB_FN_REPORT_SUBMIT,
    LCSTUB_FN_REPORT_STATUS,
    LCSTUB_FN_STATE_READ,
    LCSTUB_FN_STATE_COMMIT,
    LCSTUB_FN_SHOULD_STOP,
    LCSTUB_FN_CONTROL_NEXT,
    LCSTUB_FN_CONTROL_REPLY,
    LCSTUB_FN_SNAPSHOT_PUBLISH,
    LCSTUB_FN_LIVE_PUBLISH,
    LCSTUB_FN_COUNT
} lcstub_fn_t;

typedef int32_t (*lcstub_live_publish_hook_t)(void *user, const void *buf,
                                               uint32_t len);

typedef struct {
    float    x, y, w, h, conf;
    uint32_t class_index;
} lcstub_det_t;

typedef struct {
    int      present;
    uint32_t seq;
    uint32_t len;
    uint8_t  bytes[LCSTUB_EVENT_MAX];
    uint32_t mqtt_state;
    uint32_t web_state;
} lcstub_report_t;

typedef struct lcstub {

    int      authorized;
    int      stop_flag;
    uint32_t tick_now;
    uint32_t tick_step;

    int      model_loaded;
    uint32_t model_gen, class_gen, class_count;
    const char *meta_result_type_override;
    char     model_name[64];
    char     model_version[32];
    char     classes[LCSTUB_MAX_CLASSES][32];

    uint8_t  meta_raw[LC_MODEL_META_SIZE];
    uint32_t meta_raw_len;
    uint32_t meta_cap_override;

    uint8_t  events[LCSTUB_MAX_EVENTS][LCSTUB_EVENT_MAX];
    uint32_t event_len[LCSTUB_MAX_EVENTS];
    int      ev_head, ev_count;
    uint32_t auto_stop_after_no_events;
    uint32_t no_event_streak;

    uint8_t  state_blob[LCSTUB_STATE_CAP];
    uint32_t state_len;
    int      state_present;
    uint32_t state_revision;
    uint32_t state_revision_reply;
    int32_t  state_read_fault;
    int      state_read_fault_times;
    int32_t  state_commit_fault;
    int      state_commit_fault_times;
    int      state_torn_write;

    lcstub_report_t reports[LCSTUB_MAX_REPORTS];
    int      report_count;
    uint32_t boot_id;
    uint32_t default_mqtt_state;
    uint32_t default_webhook_state;
    int      verify_report_seq;

    uint8_t  commands[LCSTUB_MAX_COMMANDS][LC_CTRL_MSG_BUF_CAP];
    uint32_t command_len[LCSTUB_MAX_COMMANDS];
    int      cmd_head, cmd_count;
    int      ctrl_unauthorized;
    int      ctrl_delivered_valid;
    uint32_t delivered_type;
    uint32_t delivered_magic;
    uint32_t delivered_session;
    uint32_t delivered_request;
    uint32_t control_max;
    uint32_t snapshot_ram;
    uint32_t snapshot_wire_rejects;
    uint32_t reply_wire_rejects;
    uint32_t live_publish_calls;
    lcstub_live_publish_hook_t live_publish_hook;
    void *live_publish_user;

    struct {
        int      present;
        uint32_t len;
        uint8_t  bytes[LC_CTRL_V4_ENABLE_RECEIPT_SIZE];
    } replies[LCSTUB_MAX_REPLIES];
    int reply_count;

    struct {
        int      present;
        uint32_t len;
        uint8_t  bytes[LC_SNAPSHOT_BUF_CAP];
    } snapshots[LCSTUB_MAX_SNAPSHOTS];
    int snapshot_count;
    int snapshot_slot;

    int32_t  fault_code[LCSTUB_FN_COUNT];
    int      fault_times[LCSTUB_FN_COUNT];
    int      fault_skip[LCSTUB_FN_COUNT];

    uint32_t calls[LCSTUB_FN_COUNT];
    char     logs[LCSTUB_LOG_LINES][LCSTUB_LOG_LINE_LEN];
    int      log_head, log_count;
} lcstub_t;

lcstub_t *lcstub_new(void);
void      lcstub_destroy(lcstub_t *s);

void lcstub_set_session(lcstub_t *s, int authorized);
void lcstub_set_stop(lcstub_t *s, int stop);
void lcstub_set_tick(lcstub_t *s, uint32_t t);
void lcstub_set_tick_step(lcstub_t *s, uint32_t step);
void lcstub_set_model(lcstub_t *s, int loaded, uint32_t model_gen, uint32_t class_gen,
                      uint32_t class_count, const char *model_name, const char *model_version);
void lcstub_set_class(lcstub_t *s, uint32_t index, const char *name);
void lcstub_set_meta_raw(lcstub_t *s, const uint8_t *bytes, uint32_t len);
void lcstub_set_state(lcstub_t *s, const void *blob, uint32_t len, uint32_t revision);
void lcstub_set_boot_id(lcstub_t *s, uint32_t boot_id);
void lcstub_set_report_channels(lcstub_t *s, uint32_t mqtt_state, uint32_t webhook_state);
void lcstub_set_report_state(lcstub_t *s, uint32_t seq, uint32_t mqtt_state, uint32_t web_state);
void lcstub_fault(lcstub_t *s, lcstub_fn_t fn, int32_t code, int times);
void lcstub_fault_after(lcstub_t *s, lcstub_fn_t fn, int32_t code, int skip,
                        int times);
void lcstub_state_read_fault(lcstub_t *s, int32_t code, int times);
void lcstub_state_commit_fault(lcstub_t *s, int32_t code, int times);

void lcstub_push_raw(lcstub_t *s, const void *bytes, uint32_t len);
void lcstub_push_kind(lcstub_t *s, uint32_t kind, uint32_t seq, uint32_t mono_ms,
                      uint32_t model_gen, uint32_t class_gen,
                      uint32_t flags, uint32_t lost);
void lcstub_push_frame(lcstub_t *s, uint32_t seq, uint32_t mono_ms,
                       uint32_t model_gen, uint32_t class_gen,
                       uint32_t flags, uint32_t lost,
                       const lcstub_det_t *dets, uint32_t n);

void lcstub_wr32(uint8_t *p, uint32_t v);

void lcstub_make_table(lcstub_t *s, lc_app_api_v2_t *out);

void lcstub_make_table_v3(lcstub_t *s, lc_app_api_v3_t *out);

void lcstub_make_table_v4(lcstub_t *s, lc_app_api_v4_t *out);

uint32_t lcstub_make_command(uint8_t *out, uint32_t cap, uint32_t msg_type,
                             uint32_t session_gen, uint32_t request_id,
                             uint32_t expected_gen, uint32_t flags,
                             const void *payload, uint32_t payload_len);

void lcstub_push_command(lcstub_t *s, const void *bytes, uint32_t len);

void lcstub_set_control_capacity(lcstub_t *s, uint32_t control_max, uint32_t snapshot_ram);

int  lcstub_reply_count(const lcstub_t *s);

const uint8_t *lcstub_reply(const lcstub_t *s, int index);

int lcstub_snapshot_count(const lcstub_t *s);

const uint8_t *lcstub_snapshot(const lcstub_t *s, int index, uint32_t *len);

int lcstub_snapshot_valid_json(const uint8_t *payload, uint32_t len);

uint32_t    lcstub_calls(const lcstub_t *s, lcstub_fn_t fn);
const char *lcstub_last_log(const lcstub_t *s);

const lcstub_report_t *lcstub_report_by_seq(const lcstub_t *s, uint32_t seq);

#endif
