#include <errno.h>
#include <string.h>

#include "app_report_policy.h"
#include "app_report_service.h"

#define POLICY_STORE_MAGIC 0x31505241u
#define POLICY_STORE_VERSION 2u
#define POLICY_STORE_HEADER_SIZE 8u
#define POLICY_STORE_CRC_SIZE 4u
#define POLICY_STORE_WIRE_SIZE \
    (APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY - POLICY_STORE_CRC_SIZE)

typedef struct {
    app_report_policy_identity_t identity;
    app_report_policy_values_t values;
    uint32_t revision;
    uint64_t cleanup_intent;
    aicam_bool_t purged;
} policy_record_t;

typedef struct {
    uint32_t count;
    policy_record_t records[APP_REPORT_POLICY_MAX_RECORDS];
} policy_db_t;

static app_report_policy_storage_t g_storage;
static aicam_bool_t g_storage_configured;
static policy_db_t g_work_db;
static uint8_t g_old_raw[APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY];
static uint8_t g_next_raw[APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY];
static uint8_t g_check_raw[APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY];

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t crc32_bytes(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0u; i < len; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static int identity_valid(const app_report_policy_identity_t *identity)
{
    uint8_t tbs_or = 0u;

    if (identity == NULL || identity->publisher_id[0] == '\0' ||
        identity->app_id[0] == '\0' ||
        memchr(identity->publisher_id, '\0', sizeof(identity->publisher_id)) == NULL ||
        memchr(identity->app_id, '\0', sizeof(identity->app_id)) == NULL) {
        return 0;
    }
    for (uint32_t i = 0u; i < sizeof(identity->native_tbs_sha256); i++) {
        tbs_or |= identity->native_tbs_sha256[i];
    }
    return (tbs_or != 0u) ? 1 : 0;
}

static int identity_equal(const app_report_policy_identity_t *a,
                          const app_report_policy_identity_t *b)
{
    return strcmp(a->publisher_id, b->publisher_id) == 0 &&
           strcmp(a->app_id, b->app_id) == 0 &&
           memcmp(a->publisher_key_sha256, b->publisher_key_sha256, 32u) == 0 &&
           memcmp(a->native_tbs_sha256, b->native_tbs_sha256, 32u) == 0;
}

static int identity_base_equal(const app_report_policy_identity_t *a,
                               const app_report_policy_identity_t *b)
{
    return strcmp(a->publisher_id, b->publisher_id) == 0 &&
           strcmp(a->app_id, b->app_id) == 0 &&
           memcmp(a->publisher_key_sha256, b->publisher_key_sha256, 32u) == 0;
}

static int values_valid(const app_report_policy_values_t *values)
{
    if (values == NULL ||
        (values->mqtt_enabled != AICAM_TRUE && values->mqtt_enabled != AICAM_FALSE) ||
        (values->webhook_enabled != AICAM_TRUE && values->webhook_enabled != AICAM_FALSE) ||
        values->backlog_capacity < APP_REPORT_POLICY_MIN_CAPACITY ||
        values->backlog_capacity > APP_REPORT_POLICY_MAX_CAPACITY) {
        return 0;
    }
    return 1;
}

static int32_t find_record(const policy_db_t *db,
                           const app_report_policy_identity_t *identity)
{
    for (uint32_t i = 0u; i < db->count; i++) {
        if (identity_equal(&db->records[i].identity, identity)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static void encode_record(uint8_t *p, const policy_record_t *record)
{
    memcpy(p, record->identity.publisher_id, 17u);
    memcpy(p + 17u, record->identity.app_id, 33u);
    memcpy(p + 50u, record->identity.publisher_key_sha256, 32u);
    memcpy(p + 82u, record->identity.native_tbs_sha256, 32u);
    put_u32(p + 114u, record->revision);
    p[118u] = (uint8_t)record->values.mqtt_enabled;
    p[119u] = (uint8_t)record->values.webhook_enabled;
    put_u32(p + 120u, record->values.backlog_capacity);
    put_u32(p + 124u, (uint32_t)record->cleanup_intent);
    put_u32(p + 128u, (uint32_t)(record->cleanup_intent >> 32));
}

static int decode_record(const uint8_t *p, policy_record_t *record)
{
    memset(record, 0, sizeof(*record));
    memcpy(record->identity.publisher_id, p, 17u);
    memcpy(record->identity.app_id, p + 17u, 33u);
    memcpy(record->identity.publisher_key_sha256, p + 50u, 32u);
    memcpy(record->identity.native_tbs_sha256, p + 82u, 32u);
    record->revision = get_u32(p + 114u);
    record->values.mqtt_enabled = (aicam_bool_t)p[118u];
    record->values.webhook_enabled = (aicam_bool_t)p[119u];
    record->values.backlog_capacity = get_u32(p + 120u);
    record->cleanup_intent = (uint64_t)get_u32(p + 124u) |
                             ((uint64_t)get_u32(p + 128u) << 32);
    if (!identity_valid(&record->identity)) {
        return 0;
    }
    if (record->revision == 0u) {
        record->purged = AICAM_TRUE;
        return record->values.mqtt_enabled == AICAM_FALSE &&
               record->values.webhook_enabled == AICAM_FALSE &&
               record->values.backlog_capacity == 0u &&
               record->cleanup_intent != 0u;
    }
    record->purged = AICAM_FALSE;
    return values_valid(&record->values);
}

static int encode_db(const policy_db_t *db, uint8_t *out, uint32_t *out_len)
{
    uint32_t len = POLICY_STORE_HEADER_SIZE +
                   db->count * APP_REPORT_POLICY_WIRE_RECORD_SIZE +
                   POLICY_STORE_CRC_SIZE;

    if (db->count > APP_REPORT_POLICY_MAX_RECORDS ||
        len > APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY) {
        return -1;
    }
    put_u32(out, POLICY_STORE_MAGIC);
    put_u16(out + 4u, POLICY_STORE_VERSION);
    put_u16(out + 6u, (uint16_t)db->count);
    for (uint32_t i = 0u; i < db->count; i++) {
        encode_record(out + POLICY_STORE_HEADER_SIZE +
                      i * APP_REPORT_POLICY_WIRE_RECORD_SIZE, &db->records[i]);
    }
    put_u32(out + len - POLICY_STORE_CRC_SIZE,
            crc32_bytes(out, len - POLICY_STORE_CRC_SIZE));
    *out_len = len;
    return 0;
}

static int decode_db(const uint8_t *data, uint32_t len, policy_db_t *db)
{
    uint32_t count;
    uint32_t expected;

    if (data == NULL || db == NULL || len < POLICY_STORE_HEADER_SIZE + POLICY_STORE_CRC_SIZE ||
        get_u32(data) != POLICY_STORE_MAGIC || get_u16(data + 4u) != POLICY_STORE_VERSION) {
        return -1;
    }
    count = get_u16(data + 6u);
    expected = POLICY_STORE_HEADER_SIZE + count * APP_REPORT_POLICY_WIRE_RECORD_SIZE +
               POLICY_STORE_CRC_SIZE;
    if (count > APP_REPORT_POLICY_MAX_RECORDS || len != expected ||
        get_u32(data + len - POLICY_STORE_CRC_SIZE) !=
            crc32_bytes(data, len - POLICY_STORE_CRC_SIZE)) {
        return -1;
    }
    memset(db, 0, sizeof(*db));
    db->count = count;
    for (uint32_t i = 0u; i < count; i++) {
        const uint8_t *p = data + POLICY_STORE_HEADER_SIZE +
                           i * APP_REPORT_POLICY_WIRE_RECORD_SIZE;
        if (!decode_record(p, &db->records[i])) {
            return -1;
        }
        for (uint32_t j = 0u; j < i; j++) {
            if (identity_equal(&db->records[i].identity, &db->records[j].identity)) {
                return -1;
            }
        }
    }
    return 0;
}

static int32_t load_db(policy_db_t *db, uint8_t *raw, uint32_t *raw_len)
{
    int32_t rc;

    memset(db, 0, sizeof(*db));
    *raw_len = 0u;
    if (g_storage_configured != AICAM_TRUE || g_storage.read == NULL ||
        g_storage.write == NULL) {
        return APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    rc = g_storage.read(g_storage.user, raw,
                        APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY, raw_len);
    if (rc == APP_REPORT_POLICY_STORAGE_NOT_FOUND) {
        *raw_len = 0u;
        return APP_REPORT_ABI_OK;
    }
    if (rc != APP_REPORT_POLICY_STORAGE_OK || *raw_len == 0u ||
        *raw_len > APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY ||
        decode_db(raw, *raw_len, db) != 0) {
        return APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    return APP_REPORT_ABI_OK;
}

static int32_t persist_db(const policy_db_t *db, const uint8_t *old_raw,
                          uint32_t old_len)
{
    uint32_t next_len = 0u;
    uint32_t check_len = 0u;
    int32_t write_rc;
    int32_t read_rc;

    if (encode_db(db, g_next_raw, &next_len) != 0) {
        return APP_REPORT_ABI_STORAGE_UNKNOWN;
    }
    write_rc = g_storage.write(g_storage.user, g_next_raw, next_len);
    read_rc = g_storage.read(g_storage.user, g_check_raw,
                             sizeof(g_check_raw), &check_len);
    if (read_rc == APP_REPORT_POLICY_STORAGE_OK && check_len == next_len &&
        memcmp(g_check_raw, g_next_raw, next_len) == 0) {
        return APP_REPORT_ABI_OK;
    }
    if (old_len == 0u && read_rc == APP_REPORT_POLICY_STORAGE_NOT_FOUND) {
        return (write_rc == APP_REPORT_POLICY_STORAGE_OK) ?
               APP_REPORT_ABI_STORAGE_UNKNOWN : APP_REPORT_ABI_STORAGE_ERROR;
    }
    if (old_len != 0u && read_rc == APP_REPORT_POLICY_STORAGE_OK &&
        check_len == old_len && memcmp(g_check_raw, old_raw, old_len) == 0) {
        return APP_REPORT_ABI_STORAGE_ERROR;
    }
    return APP_REPORT_ABI_STORAGE_UNKNOWN;
}

void app_report_policy_store_set_storage(const app_report_policy_storage_t *storage)
{
    memset(&g_storage, 0, sizeof(g_storage));
    g_storage_configured = AICAM_FALSE;
    if (storage != NULL && storage->read != NULL && storage->write != NULL) {
        g_storage = *storage;
        g_storage_configured = AICAM_TRUE;
    }
}

int32_t app_report_policy_store_get(const app_report_policy_identity_t *identity,
                                    app_report_policy_values_t *out_values,
                                    uint32_t *out_revision)
{
    uint32_t raw_len = 0u;
    int32_t rc;
    int32_t index;

    if (!identity_valid(identity) || out_values == NULL || out_revision == NULL) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    rc = load_db(&g_work_db, g_old_raw, &raw_len);
    if (rc != APP_REPORT_ABI_OK) {
        return rc;
    }
    index = find_record(&g_work_db, identity);
    if (index < 0) {
        return APP_REPORT_ABI_NOT_FOUND;
    }
    if (g_work_db.records[(uint32_t)index].purged == AICAM_TRUE) {
        out_values->mqtt_enabled = AICAM_TRUE;
        out_values->webhook_enabled = AICAM_TRUE;
        out_values->backlog_capacity = APP_REPORT_POLICY_DEFAULT_CAPACITY;
        *out_revision = 0u;
        return APP_REPORT_ABI_NOT_FOUND;
    }
    *out_values = g_work_db.records[(uint32_t)index].values;
    *out_revision = g_work_db.records[(uint32_t)index].revision;
    return APP_REPORT_ABI_OK;
}

int32_t app_report_policy_store_has_base(const app_report_policy_identity_t *identity,
                                         aicam_bool_t *out_found)
{
    uint32_t raw_len = 0u;
    int32_t rc;

    if (!identity_valid(identity) || out_found == NULL) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    *out_found = AICAM_FALSE;
    rc = load_db(&g_work_db, g_old_raw, &raw_len);
    if (rc != APP_REPORT_ABI_OK) {
        return rc;
    }
    for (uint32_t i = 0u; i < g_work_db.count; i++) {
        if (g_work_db.records[i].purged != AICAM_TRUE &&
            identity_base_equal(&g_work_db.records[i].identity, identity)) {
            *out_found = AICAM_TRUE;
            break;
        }
    }
    return APP_REPORT_ABI_OK;
}

int32_t app_report_policy_store_get_base(const app_report_policy_identity_t *identity,
                                         app_report_policy_identity_t *out_identity,
                                         app_report_policy_values_t *out_values,
                                         uint32_t *out_revision)
{
    uint32_t raw_len = 0u;
    int32_t rc;

    if (!identity_valid(identity) || out_identity == NULL || out_values == NULL ||
        out_revision == NULL) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    memset(out_identity, 0, sizeof(*out_identity));
    memset(out_values, 0, sizeof(*out_values));
    *out_revision = 0u;
    rc = load_db(&g_work_db, g_old_raw, &raw_len);
    if (rc != APP_REPORT_ABI_OK) {
        return rc;
    }
    for (uint32_t i = 0u; i < g_work_db.count; i++) {
        if (g_work_db.records[i].purged != AICAM_TRUE &&
            identity_base_equal(&g_work_db.records[i].identity, identity)) {
            *out_identity = g_work_db.records[i].identity;
            *out_values = g_work_db.records[i].values;
            *out_revision = g_work_db.records[i].revision;
            return APP_REPORT_ABI_OK;
        }
    }
    return APP_REPORT_ABI_NOT_FOUND;
}

int32_t app_report_policy_store_rebase_base_tbs(
    const app_report_policy_identity_t *identity,
    const uint8_t new_tbs[APP_REPORT_SHA256_LEN],
    aicam_bool_t *out_moved)
{
    uint32_t raw_len = 0u;
    int32_t rc;

    if (!identity_valid(identity) || new_tbs == NULL || out_moved == NULL) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    *out_moved = AICAM_FALSE;
    rc = load_db(&g_work_db, g_old_raw, &raw_len);
    if (rc != APP_REPORT_ABI_OK) {
        return rc;
    }
    for (uint32_t i = 0u; i < g_work_db.count; i++) {
        if (g_work_db.records[i].purged != AICAM_TRUE &&
            identity_base_equal(&g_work_db.records[i].identity, identity) &&
            memcmp(g_work_db.records[i].identity.native_tbs_sha256, new_tbs,
                   APP_REPORT_SHA256_LEN) != 0) {
            memcpy(g_work_db.records[i].identity.native_tbs_sha256, new_tbs,
                   APP_REPORT_SHA256_LEN);
            *out_moved = AICAM_TRUE;
            return persist_db(&g_work_db, g_old_raw, raw_len);
        }
    }
    return APP_REPORT_ABI_OK;
}

int32_t app_report_policy_store_commit(const app_report_policy_identity_t *identity,
                                       uint32_t expected_revision,
                                       const app_report_policy_values_t *values,
                                       uint32_t *out_revision)
{
    uint32_t old_len = 0u;
    uint32_t current_revision = 0u;
    int32_t rc;
    int32_t index;

    if (!identity_valid(identity) || !values_valid(values) || out_revision == NULL) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    *out_revision = 0u;
    rc = load_db(&g_work_db, g_old_raw, &old_len);
    if (rc != APP_REPORT_ABI_OK) {
        return rc;
    }
    index = find_record(&g_work_db, identity);
    if (index >= 0) {
        current_revision = (g_work_db.records[(uint32_t)index].purged == AICAM_TRUE) ?
                           0u : g_work_db.records[(uint32_t)index].revision;
    }
    if (current_revision != expected_revision || current_revision == UINT32_MAX) {
        *out_revision = current_revision;
        return APP_REPORT_ABI_CONFLICT;
    }
    if (index < 0) {
        for (uint32_t i = 0u; i < g_work_db.count;) {
            if (!identity_base_equal(&g_work_db.records[i].identity, identity)) {
                i++;
                continue;
            }
            for (uint32_t j = i; j + 1u < g_work_db.count; j++) {
                g_work_db.records[j] = g_work_db.records[j + 1u];
            }
            g_work_db.count--;
            memset(&g_work_db.records[g_work_db.count], 0,
                   sizeof(g_work_db.records[g_work_db.count]));
        }
        if (g_work_db.count >= APP_REPORT_POLICY_MAX_RECORDS) {
            return APP_REPORT_ABI_QUOTA_EXCEEDED;
        }
        index = (int32_t)g_work_db.count++;
        memset(&g_work_db.records[(uint32_t)index], 0,
               sizeof(g_work_db.records[0]));
        g_work_db.records[(uint32_t)index].identity = *identity;
    }
    g_work_db.records[(uint32_t)index].values = *values;
    g_work_db.records[(uint32_t)index].revision = current_revision + 1u;
    g_work_db.records[(uint32_t)index].purged = AICAM_FALSE;
    rc = persist_db(&g_work_db, g_old_raw, old_len);
    if (rc == APP_REPORT_ABI_OK) {
        *out_revision = current_revision + 1u;
    }
    return rc;
}

int32_t app_report_policy_store_remove(const app_report_policy_identity_t *identity,
                                       uint64_t cleanup_intent)
{
    uint32_t old_len = 0u;
    int32_t rc;
    int32_t index;

    if (!identity_valid(identity) || cleanup_intent == 0u) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    rc = load_db(&g_work_db, g_old_raw, &old_len);
    if (rc != APP_REPORT_ABI_OK) {
        return rc;
    }
    index = find_record(&g_work_db, identity);
    if (index >= 0 &&
        g_work_db.records[(uint32_t)index].cleanup_intent == cleanup_intent) {
        return APP_REPORT_ABI_NOT_FOUND;
    }
    if (index < 0) {
        if (g_work_db.count >= APP_REPORT_POLICY_MAX_RECORDS) {
            return APP_REPORT_ABI_QUOTA_EXCEEDED;
        }
        index = (int32_t)g_work_db.count++;
        memset(&g_work_db.records[(uint32_t)index], 0,
               sizeof(g_work_db.records[0]));
        g_work_db.records[(uint32_t)index].identity = *identity;
    }
    g_work_db.records[(uint32_t)index].revision = 0u;
    g_work_db.records[(uint32_t)index].values.mqtt_enabled = AICAM_FALSE;
    g_work_db.records[(uint32_t)index].values.webhook_enabled = AICAM_FALSE;
    g_work_db.records[(uint32_t)index].values.backlog_capacity = 0u;
    g_work_db.records[(uint32_t)index].cleanup_intent = cleanup_intent;
    g_work_db.records[(uint32_t)index].purged = AICAM_TRUE;
    return persist_db(&g_work_db, g_old_raw, old_len);
}

int32_t app_report_policy_store_get_cleanup_intent(
    const app_report_policy_identity_t *identity, uint64_t *out_cleanup_intent)
{
    uint32_t raw_len = 0u;
    int32_t rc;
    int32_t index;

    if (!identity_valid(identity) || out_cleanup_intent == NULL) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    *out_cleanup_intent = 0u;
    rc = load_db(&g_work_db, g_old_raw, &raw_len);
    if (rc != APP_REPORT_ABI_OK) {
        return rc;
    }
    index = find_record(&g_work_db, identity);
    if (index < 0) {
        return APP_REPORT_ABI_NOT_FOUND;
    }
    *out_cleanup_intent = g_work_db.records[(uint32_t)index].cleanup_intent;
    return APP_REPORT_ABI_OK;
}
