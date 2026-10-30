#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_admission.h"
#include "app_host_control.h"
#include "app_installer.h"
#include "app_native_vnext_floor.h"
#include "app_state_store.h"
#include "lc_app_entry_v4.h"
#include "lc_ctrl.h"
#include "lc_stateblob.h"
#include "mbedtls/sha256.h"
#include "mem_io_backend.h"
#include "v4_vectors.h"

#include "host_stub.h"

#define CHECK(v) do { \
    g_total++; \
    if (!(v)) { \
        g_fail++; \
        printf("FAIL cross_owner_matrix:%d %s\n", g_total, #v); \
    } \
} while (0)

static int g_total;
static int g_fail;
static app_state_session_t g_state_session;
static uint32_t g_state_commits;
static app_install_record_t g_active_record;
static uint32_t g_control_generation;
static uint32_t g_boot_id;
static uint32_t g_witness_request;
static uint32_t g_witness_config;
static uint32_t g_witness_expected_revision;
static uint32_t g_witness_desired;
static uint32_t g_witness_revision;
static uint32_t g_witness_blob_len;
static uint8_t g_witness_blob_sha[APP_INSTALL_SHA256_LEN];
static int g_witness_active;
static int g_witness_committed;
static int g_witness_readback;
static uint8_t g_last_receipt[APP_CONTROL_ENABLE_RECEIPT_SIZE];
static uint32_t g_last_receipt_len;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4u) << 32);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void print_sha256(const uint8_t *data, size_t len)
{
    uint8_t digest[32];
    if (mbedtls_sha256(data, len, digest, 0) != 0) {
        printf("sha256=ERROR");
        return;
    }
    for (uint32_t i = 0u; i < sizeof(digest); i++) {
        printf("%02x", digest[i]);
    }
}

static void policy_fill(app_admission_policy_t *policy)
{
    memset(policy, 0, sizeof(*policy));
    policy->target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy->psram_mib = 64u;
    policy->abi_version = APP_ADMISSION_ABI_VERSION_V4;
    policy->abi_table_size = APP_ADMISSION_V4_ABI_TABLE_SIZE;
    policy->supported_caps_mask = APP_ADMISSION_V4_KNOWN_CAPS_MASK;
    policy->exec_region_base = 0x93E00000u;
    policy->exec_region_size = 2u * 1024u * 1024u;
    policy->event_capacity = 8192u;
    policy->report_capacity = 8192u;
    policy->state_quota_capacity = 8192u;
    policy->control_capacity = 1024u;
    policy->snapshot_capacity = 4096u;
    policy->live_publish_capacity = 9008u;
    policy->live_track_capacity = 64u;
    policy->max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V4;
    policy->v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;
}

static app_admission_result_t verify_fixture(const uint8_t *data, size_t len,
                                             app_admission_verdict_t *out,
                                             void *ctx)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    (void)ctx;
    policy_fill(&policy);
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = "ne301-exp-pub";
    trust.publisher_id_len = strlen(trust.publisher_id);
    trust.spki_der = V4_TEST_SPKI_DER;
    trust.spki_der_len = V4_TEST_SPKI_DER_LEN;
    return app_admission_verify_package(data, len, &policy, &trust, out);
}

static int32_t app_state_read_v4(void *out, uint32_t cap,
                                 uint32_t *actual_len, uint32_t *revision)
{
    int32_t rc = app_state_read(&g_state_session, out, cap, actual_len, revision);
    if (rc == APP_STATE_OK && out != NULL && g_witness_active &&
        g_witness_committed && actual_len[0] == g_witness_blob_len &&
        revision[0] == g_witness_revision) {
        uint8_t sha[APP_INSTALL_SHA256_LEN];
        if (mbedtls_sha256((const uint8_t *)out, *actual_len, sha, 0) == 0 &&
            memcmp(sha, g_witness_blob_sha, sizeof(sha)) == 0) {
            g_witness_readback = 1;
        }
    }
    return rc;
}

static int32_t app_state_commit_v4(const void *blob, uint32_t len,
                                   uint32_t expected_revision,
                                   uint32_t *new_revision)
{
    int32_t rc;

    g_state_commits++;
    rc = app_state_commit(&g_state_session, blob, len, expected_revision,
                          new_revision);
    if (rc == APP_STATE_OK && g_witness_active &&
        expected_revision == g_witness_expected_revision &&
        new_revision[0] == expected_revision + 1u &&
        mbedtls_sha256((const uint8_t *)blob, len, g_witness_blob_sha, 0) == 0) {
        g_witness_committed = 1;
        g_witness_readback = 0;
        g_witness_revision = *new_revision;
        g_witness_blob_len = len;
    }
    return rc;
}

static int32_t joint_enable_precommit(void *ctx, uint32_t session_generation,
                                      uint32_t request_id,
                                      uint32_t config_generation,
                                      uint32_t expected_state_revision,
                                      uint32_t desired_state)
{
    uint32_t actual_len = 0u;
    uint32_t revision = 0u;
    int32_t rc;

    (void)ctx;
    if (session_generation != g_control_generation || request_id == 0u ||
        config_generation == 0u) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    rc = app_state_read(&g_state_session, NULL, 0u, &actual_len, &revision);
    if ((expected_state_revision == 0u &&
         (rc != APP_STATE_NOT_FOUND || revision != 0u)) ||
        (expected_state_revision != 0u &&
         (rc != APP_STATE_BUFFER_TOO_SMALL || revision != expected_state_revision))) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    if (desired_state == APP_CONTROL_ENABLE_STATE_PAUSED) {
        app_install_set_app_running(AICAM_TRUE);
        rc = app_native_vnext_floor_raise(g_state_session.io, &g_active_record,
                                          session_generation, g_boot_id,
                                          request_id, config_generation,
                                          expected_state_revision);
        app_install_set_app_running(AICAM_FALSE);
        if (rc != APP_INSTALL_OK ||
            app_native_vnext_floor_verify(g_state_session.io, &g_active_record,
                                          session_generation, g_boot_id,
                                          request_id, config_generation,
                                          expected_state_revision) != APP_INSTALL_OK) {
            return APP_CONTROL_INVALID_ARGUMENT;
        }
    }
    g_witness_active = 1;
    g_witness_committed = 0;
    g_witness_readback = 0;
    g_witness_request = request_id;
    g_witness_config = config_generation;
    g_witness_expected_revision = expected_state_revision;
    g_witness_desired = desired_state;
    return APP_CONTROL_OK;
}

static int32_t joint_enable_receipt_check(void *ctx, uint32_t session_generation,
                                          uint32_t request_id,
                                          uint32_t config_generation,
                                          uint32_t state_revision,
                                          uint32_t desired_state)
{
    uint8_t *blob;
    uint32_t actual_len = 0u;
    uint32_t revision = 0u;
    uint8_t sha[APP_INSTALL_SHA256_LEN];
    int32_t rc;

    (void)ctx;
    if (!g_witness_active || !g_witness_committed || !g_witness_readback ||
        session_generation != g_control_generation ||
        request_id != g_witness_request || config_generation != g_witness_config ||
        desired_state != g_witness_desired || state_revision != g_witness_revision ||
        state_revision <= g_witness_expected_revision) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    blob = (uint8_t *)malloc(g_witness_blob_len);
    if (blob == NULL) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    rc = app_state_read(&g_state_session, blob, g_witness_blob_len,
                        &actual_len, &revision);
    if (rc == APP_STATE_OK && actual_len == g_witness_blob_len &&
        revision == state_revision &&
        mbedtls_sha256(blob, actual_len, sha, 0) == 0 &&
        memcmp(sha, g_witness_blob_sha, sizeof(sha)) == 0) {
        rc = APP_CONTROL_OK;
    } else {
        rc = APP_CONTROL_INVALID_ARGUMENT;
    }
    free(blob);
    if (rc != APP_CONTROL_OK) {
        return rc;
    }
    if (desired_state == APP_CONTROL_ENABLE_STATE_PAUSED &&
        app_native_vnext_floor_verify(g_state_session.io, &g_active_record,
                                      session_generation, g_boot_id, request_id,
                                      config_generation,
                                      g_witness_expected_revision) != APP_INSTALL_OK) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    return APP_CONTROL_OK;
}

static int32_t joint_control_next(void *out, uint32_t cap, uint32_t *actual_len)
{
    return app_host_control_deliver(out, cap, actual_len);
}

static int32_t joint_control_reply(const void *buf, uint32_t len)
{
    if (buf == NULL || len != sizeof(g_last_receipt)) {
        return APP_CONTROL_INVALID_ARGUMENT;
    }
    memcpy(g_last_receipt, buf, len);
    g_last_receipt_len = len;
    return app_host_control_receipt(buf, len);
}

static void joint_control_open(void)
{
    app_control_backend_t backend;
    app_control_session_cfg_t cfg;

    memset(&backend, 0, sizeof(backend));
    backend.enable_precommit = joint_enable_precommit;
    backend.enable_receipt_check = joint_enable_receipt_check;
    app_host_control_init(&backend);
    memset(&cfg, 0, sizeof(cfg));
    cfg.control_max = APP_CONTROL_MSG_RAM_MAX;
    cfg.caps = 0x170u;
    cfg.protocol_major = 4u;
    snprintf(cfg.publisher_id, sizeof(cfg.publisher_id), "%s", g_active_record.publisher_id);
    snprintf(cfg.app_id, sizeof(cfg.app_id), "%s", g_active_record.app_id);
    CHECK(app_host_control_session_open(&cfg, &g_control_generation) == APP_CONTROL_OK);
    g_last_receipt_len = 0u;
}

static void state_identity_from_record(const app_install_record_t *record,
                                       app_state_identity_t *identity)
{
    memset(identity, 0, sizeof(*identity));
    memcpy(identity->spki_sha256, record->publisher_key_sha256,
           sizeof(identity->spki_sha256));
    snprintf(identity->publisher_id, sizeof(identity->publisher_id), "%s",
             record->publisher_id);
    snprintf(identity->app_id, sizeof(identity->app_id), "%s", record->app_id);
}

static uint32_t make_enable_command(uint8_t *out, uint32_t session,
                                    uint32_t request, uint32_t config_generation,
                                    uint32_t desired, uint32_t expected_revision)
{
    memset(out, 0, LC_CTRL_V4_ENABLE_SET_SIZE);
    wr32(out, LC_CTRL_V4_PROTO_MAGIC);
    out[4] = 1u;
    out[6] = LC_CTRL_V4_TYPE_ENABLE_SET;
    wr32(out + 8u, LC_CTRL_V4_ENABLE_SET_SIZE);
    wr32(out + 12u, session);
    wr32(out + 16u, request);
    wr32(out + 20u, config_generation);
    wr32(out + 32u, LC_CTRL_V4_ENABLE_MAGIC);
    wr32(out + 36u, desired);
    wr32(out + 40u, expected_revision);
    return LC_CTRL_V4_ENABLE_SET_SIZE;
}

static void test_host_control_seam(void)
{
    app_control_backend_t backend;
    app_control_session_cfg_t cfg;
    app_control_request_t req;
    uint8_t payload[16] = { 0x45u, 0x4Eu, 0x42u, 0x31u };
    uint8_t receipt[LC_CTRL_V4_ENABLE_RECEIPT_SIZE] = { 0u };
    uint32_t generation = 0u;

    memset(&backend, 0, sizeof(backend));
    memset(&cfg, 0, sizeof(cfg));
    app_host_control_init(&backend);
    cfg.control_max = APP_CONTROL_MSG_RAM_MAX;
    cfg.snapshot_max = APP_CONTROL_SNAPSHOT_RAM_MAX;
    cfg.caps = 0x40u | APP_ADMISSION_V4_CAP_ENABLE_CONTROL;
    cfg.protocol_major = 4u;
    snprintf(cfg.publisher_id, sizeof(cfg.publisher_id), "%s", "ne301-exp-pub");
    snprintf(cfg.app_id, sizeof(cfg.app_id), "%s", "app-pr37");
    CHECK(app_host_control_session_open(&cfg, &generation) == APP_CONTROL_UNAUTHORIZED);

    cfg.caps = 0x40u;
    CHECK(app_host_control_session_open(&cfg, &generation) == APP_CONTROL_OK);
    memset(&req, 0, sizeof(req));
    req.request_id = 401u;
    req.msg_type = 5u;
    req.payload = payload;
    req.payload_len = sizeof(payload);
    req.expected_config_generation = 1u;
    CHECK(app_host_control_register(&req) == APP_CONTROL_ERR_UNAUTHORIZED);
    CHECK(app_host_control_receipt(receipt, sizeof(receipt)) ==
          APP_CONTROL_INVALID_ARGUMENT);
    app_host_control_session_close();
    printf("HOST_CONTROL_SEAM=PASS missing_hooks=%d type5=%d receipt48=%d\n",
           APP_CONTROL_UNAUTHORIZED, APP_CONTROL_ERR_UNAUTHORIZED,
           APP_CONTROL_INVALID_ARGUMENT);
}

static void test_floor_write_failure_characterization(
    const app_install_io_t *io, const app_install_record_t *record)
{
    app_install_result_t raise_rc;
    app_install_result_t old_major_rc;
    app_install_set_app_running(AICAM_TRUE);
    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s",
             APP_NATIVE_VNEXT_FLOOR_KEY);
    g_io.fail_kv_write_key_skip = 0;
    raise_rc = app_native_vnext_floor_raise(io, record, 17u, 9001u, 400u,
                                            1u, 0u);
    app_install_set_app_running(AICAM_FALSE);
    old_major_rc = app_native_vnext_floor_check(io, record, 3u);
    CHECK(raise_rc == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_NATIVE_VNEXT_FLOOR_KEY) == NULL);
    CHECK(old_major_rc == APP_INSTALL_OK);
    printf("HOST_FLOOR_WRITE_PRIMITIVE=PASS raise=%d floor_present=%d major3_check=%d\n",
           raise_rc, mem_find_key(APP_NATIVE_VNEXT_FLOOR_KEY) != NULL,
           old_major_rc);
}

static void test_joint_floor_and_app_pause(const app_install_io_t *io,
                                           const app_install_record_t *record)
{
    app_state_identity_t identity;
    app_state_session_t state_session;
    lcstub_t *stub;
    lc_app_api_v4_t api;
    lcbus_t bus;
    lc_ctrl_t ctrl;
    lcbus_host_ops_t ops;
    app_control_request_t request;
    app_control_status_t control_status;
    app_state_identity_t state_identity;
    mem_kv_t *floor;
    uint8_t command[LC_CTRL_MSG_BUF_CAP];
    uint8_t state_blob[LC_ST_BLOB_MAX];
    uint32_t command_len;
    uint32_t state_len = 0u;
    uint32_t state_revision = 0u;
    uint32_t expected_revision;
    uint32_t commits_before_retry;
    uint32_t total_in = 0u;
    uint32_t total_out = 0u;
    uint32_t window_in = 0u;
    uint32_t window_out = 0u;
    uint32_t report_seq = 0u;
    uint32_t config_generation = 0u;
    uint32_t enable_state = 0u;
    uint32_t source_schema = 0u;
    lc_bus_config_t cfg;
    lc_st_status_t decode_status;
    uint32_t session_generation;
    const uint32_t boot_id = 9001u;
    const uint32_t failed_request_id = 401u;
    const uint32_t request_id = 402u;
    uint32_t receipt_revision;

    state_identity_from_record(record, &identity);
    CHECK(app_state_session_bind(&state_session, io, &identity, 8192u) == APP_STATE_OK);
    g_state_session = state_session;
    g_active_record = *record;
    g_boot_id = boot_id;
    joint_control_open();
    session_generation = g_control_generation;
    memset(&api, 0, sizeof(api));
    stub = lcstub_new();
    CHECK(stub != NULL);
    if (stub == NULL) return;
    lcstub_set_model(stub, 1, 1u, 1u, 1u, "joint-model", "1");
    lcstub_set_class(stub, 0u, "person");
    lcstub_make_table_v4(stub, &api);
    api.state_read = app_state_read_v4;
    api.state_commit = app_state_commit_v4;
    api.control_next = joint_control_next;
    api.control_reply = joint_control_reply;
    ops = *lcbus_abi_ops_v4();
    ops.user = &api;
    lcbus_init(&bus, &ops);
    bus.state_schema = LC_ST_BLOB_SCHEMA_ENABLE;
    bus.enable_state = LCBUS_ENABLE_UNKNOWN;
    bus.config_generation = 1u;
    lcbus_restore(&bus);
    lc_ctrl_init_v4(&ctrl, &bus, &api);
    CHECK(bus.enable_state == LCBUS_ENABLE_ENABLED);
    CHECK(bus.config_generation == 1u);
    CHECK(bus.revision > 0u);
    expected_revision = bus.revision;

    command_len = make_enable_command(command, session_generation, failed_request_id,
                                      bus.config_generation, LC_ST_ENABLE_PAUSED,
                                      expected_revision);
    memset(&request, 0, sizeof(request));
    request.request_id = failed_request_id;
    request.msg_type = APP_CONTROL_MSG_TYPE_ENABLE_SET;
    request.payload = command + APP_CONTROL_HEADER_SIZE;
    request.payload_len = APP_CONTROL_ENABLE_PAYLOAD_SIZE;
    request.expected_config_generation = bus.config_generation;
    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s",
             APP_NATIVE_VNEXT_FLOOR_KEY);
    CHECK(app_host_control_register(&request) == APP_CONTROL_ERR_STORAGE_GUARD);
    CHECK(mem_find_key(APP_NATIVE_VNEXT_FLOOR_KEY) == NULL);
    CHECK(app_host_control_deliver(command, sizeof(command), &command_len) ==
          APP_CONTROL_NO_EVENT);
    CHECK(app_native_vnext_floor_check(io, record, 3u) == APP_INSTALL_OK);

    command_len = make_enable_command(command, session_generation, request_id,
                                      bus.config_generation, LC_ST_ENABLE_PAUSED,
                                      expected_revision);
    request.request_id = request_id;
    request.payload = command + APP_CONTROL_HEADER_SIZE;
    CHECK(app_host_control_register(&request) == APP_CONTROL_REG_OK);
    CHECK(app_native_vnext_floor_check(io, record, 4u) == APP_INSTALL_OK);
    floor = mem_find_key(APP_NATIVE_VNEXT_FLOOR_KEY);
    CHECK(floor != NULL);
    if (floor == NULL) {
        lcstub_destroy(stub);
        return;
    }
    CHECK(floor->len == 192u);
    CHECK(rd32(floor->data + 12u) == 4u);
    CHECK(rd64(floor->data + 16u) == 1u);
    CHECK(rd32(floor->data + 24u) == session_generation);
    CHECK(rd32(floor->data + 28u) == boot_id);
    CHECK(rd32(floor->data + 32u) == request_id);
    CHECK(rd32(floor->data + 36u) == bus.config_generation);
    CHECK(rd32(floor->data + 40u) == expected_revision);
    CHECK(memcmp(floor->data + 92u, record->publisher_key_sha256, 32u) == 0);
    CHECK(memcmp(floor->data + 124u, record->tbs_sha256, 32u) == 0);
    CHECK(memcmp(floor->data + 156u, record->package_sha256, 32u) == 0);
    CHECK(strncmp((const char *)floor->data + 44u, record->publisher_id,
                  APP_INSTALL_PUBLISHER_SLOT) == 0);
    CHECK(strncmp((const char *)floor->data + 60u, record->app_id,
                  APP_INSTALL_APP_SLOT) == 0);
    state_identity_from_record(record, &state_identity);
    CHECK(memcmp(state_identity.spki_sha256, identity.spki_sha256, 32u) == 0);
    CHECK(strcmp(state_identity.publisher_id, identity.publisher_id) == 0);
    CHECK(strcmp(state_identity.app_id, identity.app_id) == 0);

    commits_before_retry = g_state_commits;
    lc_ctrl_poll(&ctrl);
    CHECK(bus.enable_state == LCBUS_ENABLE_PAUSED);
    CHECK(app_state_read(&g_state_session, state_blob, sizeof(state_blob),
                         &state_len, &state_revision) == APP_STATE_OK);
    CHECK(state_revision == expected_revision + 1u);
    CHECK(g_state_commits > commits_before_retry);
    CHECK(g_last_receipt_len == APP_CONTROL_ENABLE_RECEIPT_SIZE);
    CHECK(memcmp(g_last_receipt, "NFC4", 4u) == 0);
    CHECK(g_last_receipt[6] == APP_CONTROL_MSG_TYPE_RECEIPT);
    CHECK(g_last_receipt[32] == 'E' && g_last_receipt[33] == 'N' &&
          g_last_receipt[34] == 'R' && g_last_receipt[35] == '1');
    CHECK(rd32(g_last_receipt + 28u) == APP_CONTROL_RESULT_APPLIED);
    CHECK(rd32(g_last_receipt + 12u) == session_generation);
    CHECK(rd32(g_last_receipt + 16u) == request_id);
    CHECK(rd32(g_last_receipt + 20u) == bus.config_generation);
    receipt_revision = rd32(g_last_receipt + 24u);
    CHECK(receipt_revision == state_revision);
    CHECK(rd32(g_last_receipt + 32u) == LC_CTRL_V4_RECEIPT_MAGIC);
    CHECK(rd32(g_last_receipt + 36u) == LC_ST_ENABLE_PAUSED);
    CHECK(rd32(g_last_receipt + 40u) == receipt_revision);
    app_host_control_status(&control_status);
    CHECK(control_status.command_state == APP_CONTROL_CMD_APPLIED);
    CHECK(control_status.result_state_revision == state_revision);
    CHECK(app_host_control_receipt(g_last_receipt, g_last_receipt_len) == APP_CONTROL_OK);
    g_last_receipt[36] = LC_ST_ENABLE_ENABLED;
    CHECK(app_host_control_receipt(g_last_receipt, g_last_receipt_len) ==
          APP_CONTROL_INVALID_ARGUMENT);
    request.request_id = failed_request_id;
    CHECK(app_host_control_register(&request) == APP_CONTROL_ERR_DUPLICATE_ID_CONFLICT);
    decode_status = lc_st_decode_enable(state_blob, state_len, &cfg,
                                        &total_in, &total_out, &window_in,
                                        &window_out, &report_seq,
                                        &config_generation, &enable_state,
                                        &source_schema);
    CHECK(decode_status == LC_ST_OK);
    CHECK(enable_state == LC_ST_ENABLE_PAUSED);
    CHECK(config_generation == bus.config_generation);
    CHECK(app_native_vnext_floor_check(io, record, 4u) == APP_INSTALL_OK);

    printf("CROSS_PAUSE=PASS floor_sha256=");
    print_sha256(floor->data, floor->len);
    printf(" state_sha256=");
    print_sha256(state_blob, state_len);
    printf(" owner=%s/%s session=%u boot=%u request=%u config=%u expected_revision=%u receipt_revision=%u\n",
           record->publisher_id, record->app_id, session_generation, boot_id,
           request_id, bus.config_generation, expected_revision, receipt_revision);

    app_state_session_end(&g_state_session);
    app_host_control_session_close();
    app_install_set_app_running(AICAM_FALSE);
    lcstub_destroy(stub);
}

static void test_host_backed_app_cas_conflict(const app_install_io_t *io,
                                             const app_install_record_t *record)
{
    app_state_identity_t identity;
    app_state_session_t state_session;
    lcstub_t *stub;
    lc_app_api_v4_t api;
    lcbus_t bus;
    lc_ctrl_t ctrl;
    lcbus_host_ops_t ops;
    app_control_request_t request;
    app_control_status_t control_status;
    uint8_t command[LC_CTRL_MSG_BUF_CAP];
    uint8_t state_blob[LC_ST_BLOB_MAX];
    uint32_t state_len = 0u;
    uint32_t current_revision = 0u;
    uint32_t raced_revision = 0u;
    uint32_t command_len;
    uint32_t session_generation;
    const uint32_t boot_id = 9002u;

    state_identity_from_record(record, &identity);
    CHECK(app_state_session_bind(&state_session, io, &identity, 8192u) == APP_STATE_OK);
    g_state_session = state_session;
    g_active_record = *record;
    g_boot_id = boot_id;
    joint_control_open();
    session_generation = g_control_generation;
    stub = lcstub_new();
    CHECK(stub != NULL);
    if (stub == NULL) return;
    lcstub_set_model(stub, 1, 1u, 1u, 1u, "joint-model", "1");
    lcstub_set_class(stub, 0u, "person");
    lcstub_make_table_v4(stub, &api);
    api.state_read = app_state_read_v4;
    api.state_commit = app_state_commit_v4;
    api.control_next = joint_control_next;
    api.control_reply = joint_control_reply;
    ops = *lcbus_abi_ops_v4();
    ops.user = &api;
    lcbus_init(&bus, &ops);
    bus.state_schema = LC_ST_BLOB_SCHEMA_ENABLE;
    bus.enable_state = LCBUS_ENABLE_UNKNOWN;
    bus.config_generation = 1u;
    lcbus_restore(&bus);
    lc_ctrl_init_v4(&ctrl, &bus, &api);
    CHECK(app_state_read(&g_state_session, state_blob, sizeof(state_blob),
                         &state_len, &current_revision) == APP_STATE_OK);
    bus.window_in = 7u;
    bus.window_out = 3u;
    bus.report_seq = 12u;
    command_len = make_enable_command(command, session_generation, 502u,
                                      bus.config_generation,
                                      LC_ST_ENABLE_ENABLED, bus.revision);
    CHECK(command_len == APP_CONTROL_ENABLE_SET_SIZE);
    memset(&request, 0, sizeof(request));
    request.request_id = 502u;
    request.msg_type = APP_CONTROL_MSG_TYPE_ENABLE_SET;
    request.payload = command + APP_CONTROL_HEADER_SIZE;
    request.payload_len = APP_CONTROL_ENABLE_PAYLOAD_SIZE;
    request.expected_config_generation = bus.config_generation;
    CHECK(app_host_control_register(&request) == APP_CONTROL_REG_OK);
    CHECK(app_state_commit(&g_state_session, state_blob, state_len,
                           current_revision, &raced_revision) == APP_STATE_OK);
    CHECK(raced_revision == current_revision + 1u);
    lc_ctrl_poll(&ctrl);
    CHECK(lcbus_storage_restricted(&bus));
    CHECK(bus.enable_state == LCBUS_ENABLE_UNKNOWN);
    CHECK(bus.window_in == 0u);
    CHECK(bus.window_out == 0u);
    CHECK(bus.report_seq == 12u);
    CHECK(stub->report_count == 0);
    CHECK(g_last_receipt_len == APP_CONTROL_ENABLE_RECEIPT_SIZE);
    CHECK(rd32(g_last_receipt + 28u) ==
          LC_CTRL_RESULT_FAILED_STATE_CONFLICT);
    CHECK(rd32(g_last_receipt + 36u) == LC_CTRL_V4_ENABLE_UNKNOWN);
    app_host_control_status(&control_status);
    CHECK(control_status.command_state == APP_CONTROL_CMD_FAILED_STATE_CONFLICT);
    CHECK(g_witness_committed == 0);
    printf("APP_HOST_CAS_CONFLICT=PASS host_revision=%u app_revision=%u status=%u effective=%u partial_window=%u reports=%d report_seq=%u\n",
           raced_revision, bus.revision,
           rd32(g_last_receipt + 28u),
           rd32(g_last_receipt + 36u), bus.window_in,
           stub->report_count, bus.report_seq);
    app_state_session_end(&g_state_session);
    app_host_control_session_close();
    app_install_set_app_running(AICAM_FALSE);
    lcstub_destroy(stub);
}

int main(void)
{
    app_install_io_t io;
    app_install_record_t record;

    mem_reset();
    mem_io_init(&io);
    CHECK(app_install(&io, verify_fixture, NULL, V4_GENERIC_PACKAGE,
                      V4_GENERIC_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    CHECK(count_files() == 1);
    test_floor_write_failure_characterization(&io, &record);
    test_host_control_seam();
    test_joint_floor_and_app_pause(&io, &record);
    test_host_backed_app_cas_conflict(&io, &record);
    printf("cross_owner_matrix: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
