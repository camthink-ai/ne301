#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "app_installer.h"
#include "app_trust_anchor.h"
#include "generic_math.h"
#include "installer_vectors.h"
#include "mem_io_backend.h"

static int g_total;
static int g_fail;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_MSG(cond, what) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what); \
    } \
} while (0)


static app_admission_result_t test_verify(const uint8_t *data, size_t len,
                                          app_admission_verdict_t *out, void *ctx)
{
    app_admission_policy_t *policy = (app_admission_policy_t *)ctx;
    const app_trust_anchor_t *anchor = app_trust_anchor_get();
    app_admission_trust_view_t trust;

    trust.publisher_id = anchor->publisher_id;
    trust.publisher_id_len = anchor->publisher_id_len;
    trust.spki_der = anchor->spki_der;
    trust.spki_der_len = anchor->spki_der_len;
    return app_admission_verify_package(data, len, policy, &trust, out);
}

static void fill_policy(app_admission_policy_t *policy)
{
    memset(policy, 0, sizeof(*policy));
    policy->target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy->psram_mib = 64u;
    policy->abi_version = 0x00010000u;
    policy->supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    policy->max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN;
    policy->exec_region_base = 0x93E00000u;
    policy->exec_region_size = 2u * 1024u * 1024u;
}

static void hex_str(const uint8_t *data, size_t len, char *out)
{
    size_t i;
    for (i = 0; i < len; i++) {
        snprintf(&out[i * 2], 3, "%02x", data[i]);
    }
    out[len * 2] = '\0';
}

static void set_pkg_path(app_install_record_t *rec)
{
    char hex[APP_INSTALL_SHA256_LEN * 2 + 1];

    hex_str(rec->package_sha256, APP_INSTALL_SHA256_LEN, hex);
    snprintf(rec->package_path, sizeof(rec->package_path), "%s/%s.neapp",
             APP_INSTALL_PKG_DIR, hex);
}

static void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void write_record_blob_with_path(const app_install_record_t *rec, const char *path)
{
    app_install_record_t tmp = *rec;
    uint8_t blob[APP_INSTALL_RECORD_BIN_SIZE];
    size_t path_off = APP_INSTALL_RECORD_BIN_SIZE - APP_INSTALL_PATH_SLOT - 4u;
    size_t crc_off = APP_INSTALL_RECORD_BIN_SIZE - 4u;

    set_pkg_path(&tmp);
    CHECK(app_install_record_encode(&tmp, blob, sizeof(blob)) == AICAM_TRUE);
    memset(blob + path_off, 0, APP_INSTALL_PATH_SLOT);
    memcpy(blob + path_off, path, strlen(path));
    put_u32_le(blob + crc_off, generic_crc32(blob, crc_off));
    CHECK(mem_kv_write(&g_io, APP_INSTALL_KEY_RECORD, blob, sizeof(blob)) ==
          APP_INSTALL_IO_OK);
}

static void test_record_roundtrip(void)
{
    app_install_record_t rec;
    app_install_record_t dec;
    uint8_t blob[APP_INSTALL_RECORD_BIN_SIZE];
    uint8_t bad[APP_INSTALL_RECORD_BIN_SIZE];
    size_t path_off = APP_INSTALL_RECORD_BIN_SIZE - APP_INSTALL_PATH_SLOT - 4u;
    size_t crc_off = APP_INSTALL_RECORD_BIN_SIZE - 4u;
    char hex[APP_INSTALL_SHA256_LEN * 2 + 1];

    memset(&rec, 0, sizeof(rec));
    snprintf(rec.publisher_id, sizeof(rec.publisher_id), "%s", "ne301-exp-pub");
    snprintf(rec.app_id, sizeof(rec.app_id), "%s", "hello-app");
    rec.version_major = 2u;
    rec.version_minor = 3u;
    rec.version_patch = 4u;
    rec.package_len = 1234u;
    rec.package_sha256[0] = 0xAAu;
    rec.package_sha256[1] = 0xABu;
    rec.tbs_sha256[1] = 0xBBu;
    rec.image_sha256[2] = 0xCCu;
    rec.publisher_key_sha256[3] = 0xDDu;
    set_pkg_path(&rec);

    CHECK(app_install_record_encode(&rec, blob, sizeof(blob)) == AICAM_TRUE);
    CHECK(app_install_record_decode(blob, sizeof(blob), &dec) == AICAM_TRUE);
    CHECK(strcmp(dec.publisher_id, "ne301-exp-pub") == 0);
    CHECK(strcmp(dec.app_id, "hello-app") == 0);
    CHECK(dec.version_major == 2u && dec.version_minor == 3u && dec.version_patch == 4u);
    CHECK(dec.package_len == 1234u);
    CHECK(dec.package_sha256[0] == 0xAAu);
    CHECK(dec.tbs_sha256[1] == 0xBBu);
    CHECK(dec.image_sha256[2] == 0xCCu);
    CHECK(dec.publisher_key_sha256[3] == 0xDDu);
    hex_str(rec.package_sha256, APP_INSTALL_SHA256_LEN, hex);
    snprintf(rec.package_path, sizeof(rec.package_path), "%s/%s.neapp",
             APP_INSTALL_PKG_DIR, hex);
    CHECK(strcmp(dec.package_path, rec.package_path) == 0);

    memcpy(bad, blob, sizeof(bad));
    bad[40] ^= 0x01u;
    CHECK(app_install_record_decode(bad, sizeof(bad), &dec) == AICAM_FALSE);
    CHECK(app_install_record_decode(blob, sizeof(blob) - 1, &dec) == AICAM_FALSE);
    CHECK(app_install_record_encode(&rec, blob, APP_INSTALL_RECORD_BIN_SIZE - 1) == AICAM_FALSE);

    memcpy(bad, blob, sizeof(bad));
    memset(bad + path_off, 0, APP_INSTALL_PATH_SLOT);
    snprintf((char *)(bad + path_off), APP_INSTALL_PATH_SLOT, "%s", "/captures/evil.neapp");
    put_u32_le(bad + crc_off, generic_crc32(bad, crc_off));
    CHECK(app_install_record_decode(bad, sizeof(bad), &dec) == AICAM_FALSE);

    memcpy(bad, blob, sizeof(bad));
    bad[path_off + 10u] ^= 0x01u;
    put_u32_le(bad + crc_off, generic_crc32(bad, crc_off));
    CHECK(app_install_record_decode(bad, sizeof(bad), &dec) == AICAM_FALSE);

    memcpy(bad, blob, sizeof(bad));
    snprintf((char *)(bad + path_off), APP_INSTALL_PATH_SLOT, "%s", "/apps/pkg/shortname.neapp");
    put_u32_le(bad + crc_off, generic_crc32(bad, crc_off));
    CHECK(app_install_record_decode(bad, sizeof(bad), &dec) == AICAM_FALSE);

    snprintf(rec.package_path, sizeof(rec.package_path), "%s", "/captures/evil.neapp");
    CHECK(app_install_record_encode(&rec, blob, sizeof(blob)) == AICAM_FALSE);
    set_pkg_path(&rec);
    CHECK(app_install_record_encode(&rec, blob, sizeof(blob)) == AICAM_TRUE);
}

static void test_fresh_install_v1(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_install_record_t rec;
    app_install_query_t query;
    char pkg_hex[APP_INSTALL_SHA256_LEN * 2 + 1];
    mem_file_t *f;
    uint8_t buf[MEM_MAX_FILE];
    size_t len = 0;

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);
    memset(&rec, 0, sizeof(rec));

    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(strcmp(rec.publisher_id, "ne301-exp-pub") == 0);
    CHECK(strcmp(rec.app_id, "hello-app") == 0);
    CHECK(rec.version_major == 1u && rec.version_minor == 0u && rec.version_patch == 0u);
    CHECK(rec.package_len == HELLO_V1_PACKAGE_LEN);
    hex_str(rec.package_sha256, APP_INSTALL_SHA256_LEN, pkg_hex);
    CHECK(strcmp(pkg_hex,
                 "f98811f8f5eab265ad9e0df532bf71b1e54bdb2b8cc111866c2c4c87f1b7dcd8") == 0);
    CHECK(strncmp(rec.package_path, "/apps/pkg/", 10) == 0);
    CHECK(strlen(rec.package_path) > 10);

    f = mem_find_file(rec.package_path);
    CHECK(f != NULL);
    CHECK(f->len == HELLO_V1_PACKAGE_LEN);
    CHECK(memcmp(f->data, HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN) == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL);
    CHECK(count_files() == 1);

    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(strcmp(query.record.app_id, "hello-app") == 0);
    CHECK(query.record.version_major == 1u);
    CHECK(strcmp(query.record.package_path, rec.package_path) == 0);

    CHECK(mem_file_read(&g_io, rec.package_path, buf, sizeof(buf), &len) ==
          APP_INSTALL_IO_OK);
    CHECK(len == HELLO_V1_PACKAGE_LEN);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK_ALREADY);
    CHECK(count_files() == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
}

static void test_upgrade_v1_to_v2_and_uninstall(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_install_record_t rec;
    app_install_record_t removed;
    app_install_query_t query;
    char v1_path[APP_INSTALL_PATH_MAX];
    char v2_path[APP_INSTALL_PATH_MAX];

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    snprintf(v1_path, sizeof(v1_path), "%s", rec.package_path);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(rec.version_major == 2u);
    snprintf(v2_path, sizeof(v2_path), "%s", rec.package_path);
    CHECK(strcmp(v1_path, v2_path) != 0);
    CHECK(mem_find_file(v1_path) == NULL);
    CHECK(mem_find_file(v2_path) != NULL);
    CHECK(count_files() == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);

    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 2u);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_DOWNGRADE_REFUSED);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.record.version_major == 2u);
    CHECK(mem_find_file(v2_path) != NULL);

    CHECK(app_install_uninstall(&io, &removed) == APP_INSTALL_OK);
    CHECK(removed.version_major == 2u);
    CHECK(mem_find_file(v2_path) == NULL);
    CHECK(count_files() == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_NONE);
    CHECK(app_install_uninstall(&io, &removed) == APP_INSTALL_ERR_NOT_FOUND);
}

static void test_version_conflicts(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_install_record_t rec;
    app_install_query_t query;

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_ALT_PACKAGE,
                      HELLO_V2_ALT_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_VERSION_CONFLICT);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.record.version_major == 2u);
    {
        char pkg_hex[APP_INSTALL_SHA256_LEN * 2 + 1];
        hex_str(query.record.package_sha256, APP_INSTALL_SHA256_LEN, pkg_hex);
        CHECK(strcmp(pkg_hex,
                     "2a86a2982c042385850daac8962b3cef1d49f838e03a503032b0c6a5874a644f") == 0);
    }
    CHECK(count_files() == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
}

static void test_refused_candidates_leave_state_intact(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_install_record_t rec;
    app_install_query_t query;
    uint8_t tampered[HELLO_V1_PACKAGE_LEN];

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(count_files() == 1);

    memcpy(tampered, HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN);
    tampered[HELLO_V1_PACKAGE_LEN - 1] ^= 0x01u;
    CHECK(app_install(&io, test_verify, &policy, tampered,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_SIGNATURE_INVALID);
    CHECK(app_install(&io, test_verify, &policy, HELLO_BADBOARD_PACKAGE,
                      HELLO_BADBOARD_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_TARGET_INCOMPATIBLE);
    CHECK(app_install(&io, test_verify, &policy, HELLO_FOREIGN_PACKAGE,
                      HELLO_FOREIGN_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_SIGNATURE_INVALID);
    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      15u, &rec) == APP_INSTALL_ERR_BAD_PACKAGE);
    CHECK(app_install(&io, test_verify, &policy, NULL,
                      0u, &rec) == APP_INSTALL_ERR_PARAM);
    CHECK(app_install(&io, NULL, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_PARAM);
    CHECK(count_files() == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 2u);
}

static void test_identity_conflicts(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_install_record_t rec;
    app_install_query_t query;
    uint8_t blob[APP_INSTALL_RECORD_BIN_SIZE];

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);

    memset(&rec, 0, sizeof(rec));
    snprintf(rec.publisher_id, sizeof(rec.publisher_id), "%s", "ne301-legacy-pub");
    snprintf(rec.app_id, sizeof(rec.app_id), "%s", "hello-app");
    rec.version_major = 9u;
    rec.package_len = HELLO_V1_PACKAGE_LEN;
    rec.package_sha256[0] = 0x11u;
    rec.tbs_sha256[0] = 0x22u;
    rec.image_sha256[0] = 0x33u;
    rec.publisher_key_sha256[0] = 0x44u;
    set_pkg_path(&rec);
    CHECK(app_install_record_encode(&rec, blob, sizeof(blob)) == AICAM_TRUE);
    CHECK(mem_kv_write(&g_io, APP_INSTALL_KEY_RECORD, blob, sizeof(blob)) ==
          APP_INSTALL_IO_OK);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_PUBLISHER_CONFLICT);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(strcmp(query.record.publisher_id, "ne301-legacy-pub") == 0);

    snprintf(rec.publisher_id, sizeof(rec.publisher_id), "%s", "ne301-exp-pub");
    snprintf(rec.app_id, sizeof(rec.app_id), "%s", "other-app");
    memcpy(rec.publisher_key_sha256, INSTALLER_VECTOR_SPKI_SHA256, APP_INSTALL_SHA256_LEN);
    set_pkg_path(&rec);
    CHECK(app_install_record_encode(&rec, blob, sizeof(blob)) == AICAM_TRUE);
    CHECK(mem_kv_write(&g_io, APP_INSTALL_KEY_RECORD, blob, sizeof(blob)) ==
          APP_INSTALL_IO_OK);
    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_IDENTITY_CONFLICT);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(strcmp(query.record.app_id, "other-app") == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(app_install_uninstall(&io, &rec) == APP_INSTALL_OK);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_NONE);
}

static void test_committed_query_paths(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_install_record_t rec;
    app_install_record_t committed;
    aicam_bool_t present = AICAM_FALSE;

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);

    CHECK(app_install_query_committed(&io, &committed, &present) == APP_INSTALL_OK);
    CHECK(present == AICAM_FALSE);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(app_install_query_committed(&io, &committed, &present) == APP_INSTALL_OK);
    CHECK(present == AICAM_TRUE);
    CHECK(strcmp(committed.publisher_id, "ne301-exp-pub") == 0);
    CHECK(strcmp(committed.app_id, "hello-app") == 0);
    CHECK(committed.version_major == 2u);
    CHECK(committed.package_len == HELLO_V2_PACKAGE_LEN);

    {
        mem_kv_t *kv = mem_find_key(APP_INSTALL_KEY_RECORD);
        uint8_t blob[APP_INSTALL_RECORD_BIN_SIZE];

        CHECK(kv != NULL);
        memcpy(blob, kv->data, sizeof(blob));
        blob[30] ^= 0x80u;
        memcpy(kv->data, blob, sizeof(blob));
    }
    CHECK(app_install_query_committed(&io, &committed, &present) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL);
}

static void test_busy_and_running(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_install_record_t rec;
    app_install_record_t removed;
    app_install_query_t query;

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);

    g_io.tx_held = 1;
    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_BUSY);
    CHECK(app_install_uninstall(&io, &removed) == APP_INSTALL_ERR_BUSY);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_ERR_BUSY);
    g_io.tx_held = 0;

    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    app_install_set_app_running(AICAM_TRUE);
    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_BUSY);
    CHECK(app_install_uninstall(&io, &removed) == APP_INSTALL_ERR_BUSY);
    CHECK(app_install_app_running() == AICAM_TRUE);
    app_install_set_app_running(AICAM_FALSE);
    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
}

static void test_query_unavailable_states(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_install_record_t rec;
    app_install_query_t query;
    uint8_t blob[APP_INSTALL_RECORD_BIN_SIZE];
    mem_file_t *f;

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);

    CHECK(mem_file_remove(&g_io, rec.package_path) == APP_INSTALL_IO_OK);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_UNAVAILABLE);
    CHECK(strcmp(query.reason, "file_missing") == 0);
    CHECK(strcmp(query.record.app_id, "hello-app") == 0);
    CHECK(query.record.version_major == 2u);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(mem_find_file(rec.package_path) != NULL);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);

    f = mem_find_file(rec.package_path);
    CHECK(f != NULL);
    f->corrupt_reads = 1;
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_UNAVAILABLE);
    f->corrupt_reads = 0;

    memset(&rec, 0, sizeof(rec));
    snprintf(rec.publisher_id, sizeof(rec.publisher_id), "%s", "ne301-exp-pub");
    snprintf(rec.app_id, sizeof(rec.app_id), "%s", "hello-app");
    rec.version_major = 2u;
    rec.package_len = 10u;
    rec.package_sha256[0] = 0x01u;
    rec.tbs_sha256[0] = 0x02u;
    rec.image_sha256[0] = 0x03u;
    rec.publisher_key_sha256[0] = 0x04u;
    set_pkg_path(&rec);
    CHECK(app_install_record_encode(&rec, blob, sizeof(blob)) == AICAM_TRUE);
    CHECK(mem_kv_write(&g_io, APP_INSTALL_KEY_RECORD, blob, sizeof(blob)) ==
          APP_INSTALL_IO_OK);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_UNAVAILABLE);
    CHECK(strcmp(query.reason, "file_missing") == 0);
}

static void test_same_tbs_alt_signature_idempotent(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_admission_verdict_t v_low;
    app_admission_verdict_t v_high;
    app_install_record_t rec;
    app_install_record_t before;
    app_install_record_t committed;
    app_install_query_t query;
    aicam_bool_t present = AICAM_FALSE;
    mem_file_t *f;
    uint8_t file_before[MEM_MAX_FILE];
    size_t file_len = 0;

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);

    CHECK(test_verify(HELLO_V2_PACKAGE, HELLO_V2_PACKAGE_LEN, &v_low, &policy) ==
          APP_ADMISSION_OK);
    CHECK(test_verify(HELLO_V2_HISIG_PACKAGE, HELLO_V2_HISIG_PACKAGE_LEN, &v_high, &policy) ==
          APP_ADMISSION_OK);
    CHECK(memcmp(v_low.tbs_sha256, v_high.tbs_sha256, APP_INSTALL_SHA256_LEN) == 0);
    CHECK(v_low.version_major == v_high.version_major &&
          v_low.version_minor == v_high.version_minor &&
          v_low.version_patch == v_high.version_patch);
    CHECK(HELLO_V2_HISIG_PACKAGE_LEN != HELLO_V2_PACKAGE_LEN);
    CHECK(memcmp(HELLO_V2_PACKAGE, HELLO_V2_HISIG_PACKAGE,
                 (HELLO_V2_PACKAGE_LEN < HELLO_V2_HISIG_PACKAGE_LEN) ?
                     HELLO_V2_PACKAGE_LEN : HELLO_V2_HISIG_PACKAGE_LEN) != 0);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    present = AICAM_FALSE;
    CHECK(app_install_query_committed(&io, &before, &present) == APP_INSTALL_OK);
    CHECK(present == AICAM_TRUE);
    f = mem_find_file(before.package_path);
    CHECK(f != NULL);
    file_len = f->len;
    memcpy(file_before, f->data, file_len);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_HISIG_PACKAGE,
                      HELLO_V2_HISIG_PACKAGE_LEN, &rec) == APP_INSTALL_OK_ALREADY);
    present = AICAM_FALSE;
    memset(&committed, 0, sizeof(committed));
    CHECK(app_install_query_committed(&io, &committed, &present) == APP_INSTALL_OK);
    CHECK(present == AICAM_TRUE);
    CHECK(memcmp(&committed, &before, sizeof(before)) == 0);
    f = mem_find_file(before.package_path);
    CHECK(f != NULL);
    CHECK(f->len == file_len);
    CHECK(memcmp(f->data, file_before, file_len) == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(count_files() == 1);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(strcmp(query.record.package_path, before.package_path) == 0);
    CHECK(query.record.package_len == HELLO_V2_PACKAGE_LEN);
    CHECK(memcmp(query.record.package_sha256, before.package_sha256,
                 APP_INSTALL_SHA256_LEN) == 0);

    mem_reset();
    mem_io_init(&io);
    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_HISIG_PACKAGE,
                      HELLO_V2_HISIG_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    present = AICAM_FALSE;
    CHECK(app_install_query_committed(&io, &before, &present) == APP_INSTALL_OK);
    CHECK(present == AICAM_TRUE);
    f = mem_find_file(before.package_path);
    CHECK(f != NULL);
    file_len = f->len;
    memcpy(file_before, f->data, file_len);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK_ALREADY);
    present = AICAM_FALSE;
    memset(&committed, 0, sizeof(committed));
    CHECK(app_install_query_committed(&io, &committed, &present) == APP_INSTALL_OK);
    CHECK(present == AICAM_TRUE);
    CHECK(memcmp(&committed, &before, sizeof(before)) == 0);
    f = mem_find_file(before.package_path);
    CHECK(f != NULL);
    CHECK(f->len == file_len);
    CHECK(memcmp(f->data, file_before, file_len) == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(count_files() == 1);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(strcmp(query.record.package_path, before.package_path) == 0);
    CHECK(query.record.package_len == HELLO_V2_HISIG_PACKAGE_LEN);
    CHECK(memcmp(query.record.package_sha256, before.package_sha256,
                 APP_INSTALL_SHA256_LEN) == 0);
}

static app_admission_result_t test_verify_unproven(const uint8_t *data, size_t len,
                                                   app_admission_verdict_t *out, void *ctx)
{
    app_admission_policy_t *policy = (app_admission_policy_t *)ctx;
    app_admission_trust_view_t broken;

    (void)data;
    (void)len;
    memset(&broken, 0, sizeof(broken));
    broken.publisher_id = NULL;
    broken.publisher_id_len = 0u;
    broken.spki_der = NULL;
    broken.spki_der_len = 0u;
    return app_admission_verify_package(data, len, policy, &broken, out);
}

static app_admission_result_t test_verify_rotated_key(const uint8_t *data, size_t len,
                                                      app_admission_verdict_t *out, void *ctx)
{
    app_admission_result_t arc = test_verify(data, len, out, ctx);

    if (arc == APP_ADMISSION_OK) {
        out->publisher_key_sha256[0] ^= 0xFFu;
    }
    return arc;
}

static void test_cross_key_takeover_refused(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_install_record_t rec;
    app_install_record_t removed;
    app_install_query_t query;

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(app_install(&io, test_verify_rotated_key, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_PUBLISHER_CONFLICT);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(count_files() == 1);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 1u);

    CHECK(app_install(&io, test_verify_rotated_key, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_PUBLISHER_CONFLICT);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(count_files() == 1);

    CHECK(app_install_uninstall(&io, &removed) == APP_INSTALL_OK);
    CHECK(app_install(&io, test_verify_rotated_key, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(rec.publisher_key_sha256[0] == (INSTALLER_VECTOR_SPKI_SHA256[0] ^ 0xFFu));
    CHECK(memcmp(rec.publisher_key_sha256, INSTALLER_VECTOR_SPKI_SHA256,
                 APP_INSTALL_SHA256_LEN) != 0);
    CHECK(app_install_query(&io, test_verify_rotated_key, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);

    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_UNAVAILABLE);
    CHECK(strcmp(query.reason, "identity_mismatch") == 0);
    CHECK(strcmp(query.record.publisher_id, "ne301-exp-pub") == 0);
    CHECK(strcmp(query.record.app_id, "hello-app") == 0);
    CHECK(query.record.version_major == 1u);
    CHECK(query.record.package_len == HELLO_V1_PACKAGE_LEN);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_PUBLISHER_CONFLICT);
    CHECK(count_files() == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);

    CHECK(app_install_uninstall(&io, &removed) == APP_INSTALL_OK);
    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(app_install_query(&io, test_verify, &policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
}

static void test_record_path_namespace_guards(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_install_record_t rec;
    app_install_record_t removed;
    app_install_query_t query;
    const uint8_t user_data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[16];
    size_t len = 0;
    char hex[APP_INSTALL_SHA256_LEN * 2 + 1];
    char wrong_path[APP_INSTALL_PATH_MAX];

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);
    CHECK(mem_file_write(&g_io, "/captures/vacation.jpg", user_data,
                         sizeof(user_data)) == APP_INSTALL_IO_OK);
    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);

    memset(&rec, 0, sizeof(rec));
    snprintf(rec.publisher_id, sizeof(rec.publisher_id), "%s", "ne301-exp-pub");
    snprintf(rec.app_id, sizeof(rec.app_id), "%s", "hello-app");
    rec.version_major = 5u;
    rec.package_len = 64u;
    rec.package_sha256[0] = 0x5Au;
    rec.tbs_sha256[0] = 0x5Bu;
    rec.image_sha256[0] = 0x5Cu;
    rec.publisher_key_sha256[0] = 0x5Du;
    write_record_blob_with_path(&rec, "/captures/vacation.jpg");

    CHECK(app_install_query(&io, test_verify, &policy, &query) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL);
    CHECK(app_install_uninstall(&io, &removed) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL);
    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL);
    CHECK(mem_file_read(&g_io, "/captures/vacation.jpg", buf, sizeof(buf), &len) ==
          APP_INSTALL_IO_OK);
    CHECK(len == sizeof(user_data));
    CHECK(memcmp(buf, user_data, sizeof(user_data)) == 0);

    memset(hex, '0', sizeof(hex) - 1u);
    hex[sizeof(hex) - 1u] = '\0';
    snprintf(wrong_path, sizeof(wrong_path), "%s/%s.neapp", APP_INSTALL_PKG_DIR, hex);
    write_record_blob_with_path(&rec, wrong_path);

    CHECK(app_install_query(&io, test_verify, &policy, &query) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL);
    CHECK(app_install_uninstall(&io, &removed) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL);
    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_file_read(&g_io, "/captures/vacation.jpg", buf, sizeof(buf), &len) ==
          APP_INSTALL_IO_OK);
    CHECK(len == sizeof(user_data));
    CHECK(memcmp(buf, user_data, sizeof(user_data)) == 0);
    CHECK(count_files() == 2);
}

static void test_trust_unproven_refused(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_admission_trust_view_t broken;
    app_install_record_t rec;

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);

    memset(&broken, 0, sizeof(broken));
    broken.publisher_id = NULL;
    broken.publisher_id_len = 0u;
    broken.spki_der = NULL;
    broken.spki_der_len = 0u;

    CHECK(app_admission_verify_package(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN,
                                       &policy, &broken, &(app_admission_verdict_t){0}) ==
          APP_ADMISSION_ERR_TRUST_UNPROVEN);

    CHECK(app_install(&io, test_verify_unproven, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_TRUST_UNPROVEN);
    CHECK(count_files() == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(count_files() == 1);
}

static void test_user_files_untouched(void)
{
    app_install_io_t io;
    app_admission_policy_t policy;
    app_install_record_t rec;
    app_install_record_t removed;
    const uint8_t user_data[] = {0xCA, 0xFE, 0xBA, 0xBE};

    mem_reset();
    mem_io_init(&io);
    fill_policy(&policy);
    CHECK(mem_file_write(&g_io, "/captures/user_photo.jpg", user_data,
                         sizeof(user_data)) == APP_INSTALL_IO_OK);

    CHECK(app_install(&io, test_verify, &policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(app_install(&io, test_verify, &policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(app_install_uninstall(&io, &removed) == APP_INSTALL_OK);
    {
        uint8_t buf[16];
        size_t len = 0;
        CHECK(mem_file_read(&g_io, "/captures/user_photo.jpg", buf, sizeof(buf), &len) ==
              APP_INSTALL_IO_OK);
        CHECK(len == sizeof(user_data));
        CHECK(memcmp(buf, user_data, sizeof(user_data)) == 0);
    }
    CHECK(count_files() == 1);
}

static void test_result_strings(void)
{
    CHECK(strcmp(app_install_result_str(APP_INSTALL_OK), "OK") == 0);
    CHECK(strcmp(app_install_result_str(APP_INSTALL_OK_ALREADY), "ALREADY_INSTALLED") == 0);
    CHECK(strcmp(app_install_result_str(APP_INSTALL_ERR_BUSY), "BUSY") == 0);
    CHECK(strcmp(app_install_result_str(APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN),
                 "STORAGE_STATE_UNKNOWN") == 0);
    CHECK(strcmp(app_install_result_str(APP_INSTALL_ERR_STORAGE_FULL), "STORAGE_FULL") == 0);
    CHECK(strcmp(app_install_result_str(APP_INSTALL_ERR_VERSION_CONFLICT),
                 "VERSION_CONFLICT") == 0);
    CHECK(strcmp(app_install_result_str(APP_INSTALL_ERR_DOWNGRADE_REFUSED),
                 "DOWNGRADE_REFUSED") == 0);
    CHECK(strcmp(app_install_result_str(APP_INSTALL_ERR_PUBLISHER_CONFLICT),
                 "PUBLISHER_CONFLICT") == 0);
    CHECK(strcmp(app_install_result_str(APP_INSTALL_ERR_IDENTITY_CONFLICT),
                 "IDENTITY_CONFLICT") == 0);
    CHECK(strcmp(app_install_result_str(APP_INSTALL_ERR_NOT_FOUND), "NOT_FOUND") == 0);
    CHECK(strcmp(app_install_state_str(APP_INSTALL_STATE_NONE), "none") == 0);
    CHECK(strcmp(app_install_state_str(APP_INSTALL_STATE_INSTALLED), "installed") == 0);
    CHECK(strcmp(app_install_state_str(APP_INSTALL_STATE_UNAVAILABLE), "unavailable") == 0);
}

static void test_anchor_binding(void)
{
    char fp_hex[APP_INSTALL_SHA256_LEN * 2 + 1];
    const app_trust_anchor_t *anchor = app_trust_anchor_get();

    CHECK(HELLO_V1_PACKAGE_LEN > 200u);
    CHECK(HELLO_V1_PACKAGE[0] == 0x4Eu && HELLO_V1_PACKAGE[1] == 0x45u);
    hex_str(INSTALLER_VECTOR_SPKI_SHA256, APP_INSTALL_SHA256_LEN, fp_hex);
    CHECK_MSG(strcmp(fp_hex,
                     "06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda") == 0,
              "installer_vectors_bound_to_current_firmware_anchor");
    CHECK(anchor != NULL);
    CHECK(strcmp(anchor->publisher_id, "ne301-exp-pub") == 0);
    CHECK(HELLO_V2_PACKAGE_LEN != HELLO_V2_ALT_PACKAGE_LEN ||
          memcmp(HELLO_V2_PACKAGE, HELLO_V2_ALT_PACKAGE, HELLO_V2_PACKAGE_LEN) != 0);
}

int main(void)
{
    app_admission_policy_t policy;

    fill_policy(&policy);

    test_record_roundtrip();
    test_anchor_binding();
    test_result_strings();
    test_fresh_install_v1();
    test_upgrade_v1_to_v2_and_uninstall();
    test_version_conflicts();
    test_refused_candidates_leave_state_intact();
    test_identity_conflicts();
    test_cross_key_takeover_refused();
    test_record_path_namespace_guards();
    test_committed_query_paths();
    test_busy_and_running();
    test_query_unavailable_states();
    test_same_tbs_alt_signature_idempotent();
    test_trust_unproven_refused();
    test_user_files_untouched();

    printf("test_app_installer: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
