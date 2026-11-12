/*
 * Truncation-elimination proof for the flash_fs_type widening in
 * Custom/Services/Device/device_service.h: char flash_fs_type[8] -> [16]
 * (#193 round-3 blocker B1, same family as the storage.h fs_type fix
 * deda5ab8b).
 *
 * Parses the REAL production header Custom/Services/Device/device_service.h
 * on host (shim_devsvc/ supplies only the headers device_service.h includes
 * but does not need for storage_info_t; aicam_bool_t replicates the
 * production C enum exactly, so the storage_info_t layout matches the
 * firmware build). The production writer is device_service.c:562-563:
 *
 *     strncpy(info->flash_fs_type, "littlefs", sizeof(info->flash_fs_type) - 1);
 *     info->flash_fs_type[sizeof(info->flash_fs_type) - 1] = '\0';
 *
 * Unlike the storage.c fs_type defect (1-byte NUL overflow), this writer is
 * bounds-safe and NUL-terminating at every width — the pre-fix field [8]
 * simply could not hold "littlefs" (8 chars + NUL = 9 bytes), so the writer
 * stored the truncated string "littlef" and /api/v1/device/storage
 * (api_device_module.c:246 cJSON_AddStringToObject) permanently published it:
 * the user-visible FAIL observed at @614. Post-fix field [16] holds the full
 * literal. storage_info_t is a RAM-only info struct (static cache
 * g_device_service.storage_info + one stack copy in the web API handler);
 * no NVS/protocol/binary-layout dependency on the 8-byte field.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "device_service.h" /* REAL production header (see Makefile -I order) */

static int g_total = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

/* Compile-time: the literal the production writer stores, incl. NUL, is 9 bytes. */
_Static_assert(sizeof("littlefs") == 9, "production literal length drifted");

/* Compile-time: production field holds the full literal incl. NUL.
 * (Fails on the pre-fix char[8]: 9 <= 8 is false — the B1 defect.) */
_Static_assert(sizeof("littlefs") <= sizeof(((storage_info_t *)0)->flash_fs_type),
               "storage_info_t.flash_fs_type can no longer hold \"littlefs\" + NUL");

/* Compile-time: flash_fs_type is the final member and the widest write the
 * bounded producer can make (sizeof(field) bytes) ends strictly inside the
 * struct. */
_Static_assert(offsetof(storage_info_t, flash_fs_type) + sizeof("littlefs")
                   <= sizeof(storage_info_t),
               "production write would reach past the struct end");
_Static_assert(offsetof(storage_info_t, flash_fs_type)
                   + sizeof(((storage_info_t *)0)->flash_fs_type)
                   <= sizeof(storage_info_t),
               "layout drifted: flash_fs_type must be the final member "
               "(widest write ends inside the struct incl. tail padding)");

/* Byte-exact replica of the PRE-fix struct (flash_fs_type[8]) used only to
 * demonstrate the truncation that was eliminated; never instantiated in
 * firmware. Prefix members copied verbatim from the real header. */
typedef struct {
    aicam_bool_t sd_card_connected;
    uint64_t total_capacity_mb;
    uint64_t available_capacity_mb;
    uint64_t used_capacity_mb;
    float usage_percent;
    aicam_bool_t cyclic_overwrite_enabled;
    uint32_t overwrite_threshold_percent;
    aicam_bool_t flash_fs_mounted;
    aicam_bool_t flash_fs_error;
    char flash_error[24];
    uint64_t flash_total_capacity_mb;
    uint64_t flash_available_capacity_mb;
    uint64_t flash_used_capacity_mb;
    float flash_usage_percent;
    char flash_fs_type[8];
} legacy_storage_info_replica_t;

/* The replica prefix is layout-identical to the real struct up to the field. */
_Static_assert(offsetof(storage_info_t, flash_fs_type)
                   == offsetof(legacy_storage_info_replica_t, flash_fs_type),
               "replica prefix layout drifted from the real header");

/* char array after the struct starts at the very next byte (alignment 1),
 * so any byte written past the struct end is observable in tail[]. */
typedef struct {
    storage_info_t info;
    unsigned char tail[16];
} storage_frame_t;

/* Positive example: with the widened field, the exact production writer
 * (device_service.c:562-563) stores the complete "littlefs" with in-field
 * NUL termination and touches nothing past the struct end. This is what
 * /api/v1/device/storage now publishes. */
static void test_production_writer_complete(void)
{
    storage_frame_t f;
    size_t i;

    memset(&f, 0xAA, sizeof(f));

    /* Exact production statements (Custom/Services/Device/device_service.c:562-563). */
    strncpy(f.info.flash_fs_type, "littlefs", sizeof(f.info.flash_fs_type) - 1);
    f.info.flash_fs_type[sizeof(f.info.flash_fs_type) - 1] = '\0';

    CHECK(strcmp(f.info.flash_fs_type, "littlefs") == 0);
    CHECK(strlen(f.info.flash_fs_type) == 8);
    CHECK(f.info.flash_fs_type[8] == '\0');
    CHECK(f.info.flash_fs_type[15] == '\0');

    /* No byte past the struct end is touched. */
    for (i = 0; i < sizeof(f.tail); i++) {
        if (f.tail[i] != 0xAA) {
            CHECK(f.tail[i] == 0xAA && "production field still overflows");
            return;
        }
    }
    CHECK(1);
}

/* Negative example A (the eliminated @614 user-visible FAIL): the very same
 * bounds-safe writer against the pre-fix [8] field width stores the
 * truncated "littlef" — 7 chars, NUL forced at [7], the final 's' lost.
 * No UB here: the bounded writer is exactly the production code, which is
 * why the defect was silent truncation rather than a memory fault. */
static void test_legacy_replica_truncated(void)
{
    legacy_storage_info_replica_t r;

    memset(&r, 0, sizeof(r));

    /* Same production statements, pre-fix field width. */
    strncpy(r.flash_fs_type, "littlefs", sizeof(r.flash_fs_type) - 1);
    r.flash_fs_type[sizeof(r.flash_fs_type) - 1] = '\0';

    CHECK(strcmp(r.flash_fs_type, "littlefs") != 0);
    CHECK(strcmp(r.flash_fs_type, "littlef") == 0);
    CHECK(strlen(r.flash_fs_type) == 7);
    CHECK(r.flash_fs_type[7] == '\0');
    CHECK(memcmp(r.flash_fs_type, "littlefs", 8) != 0);
}

/* Negative example B: the not-mounted path (device_service.c:572) memsets
 * the whole field, so the API publishes an empty string — no stale bytes,
 * no un-NUL-terminated read, at either width. */
static void test_not_mounted_path_empty(void)
{
    storage_frame_t f;

    memset(&f, 0xAA, sizeof(f));

    /* Exact production statement (Custom/Services/Device/device_service.c:572). */
    memset(f.info.flash_fs_type, 0, sizeof(f.info.flash_fs_type));

    CHECK(f.info.flash_fs_type[0] == '\0');
    CHECK(strlen(f.info.flash_fs_type) == 0);
}

/* Reader-side proof: the sole consumer
 * (api_device_module.c:246 cJSON_AddStringToObject(..., storage_info.flash_fs_type))
 * treats the field as a NUL-terminated C string, so it is safe at any width;
 * the ONLY behavioral change from the widening is completeness. */
static void test_web_reader_width_independent(void)
{
    storage_info_t wide;
    legacy_storage_info_replica_t legacy;

    strncpy(wide.flash_fs_type, "littlefs", sizeof(wide.flash_fs_type) - 1);
    wide.flash_fs_type[sizeof(wide.flash_fs_type) - 1] = '\0';
    strncpy(legacy.flash_fs_type, "littlefs", sizeof(legacy.flash_fs_type) - 1);
    legacy.flash_fs_type[sizeof(legacy.flash_fs_type) - 1] = '\0';

    /* strlen() is what cJSON does to size the JSON string value. */
    CHECK(strlen(wide.flash_fs_type) == 8);
    CHECK(strlen(legacy.flash_fs_type) == 7);
}

/* Struct-size impact evidence: the widening grows storage_info_t by exactly
 * 8 bytes (RAM-only consumers: the static g_device_service.storage_info cache
 * plus one stack copy in the web API handler). */
static void test_struct_size_impact(void)
{
    printf("sizeof(storage_info_t) post-fix = %zu, pre-fix replica = %zu (+%zu bytes)\n",
           sizeof(storage_info_t), sizeof(legacy_storage_info_replica_t),
           sizeof(storage_info_t) - sizeof(legacy_storage_info_replica_t));
    CHECK(sizeof(storage_info_t) - sizeof(legacy_storage_info_replica_t) == 8);
    CHECK(sizeof(((storage_info_t *)0)->flash_fs_type) == 16);
    CHECK(sizeof(((legacy_storage_info_replica_t *)0)->flash_fs_type) == 8);
}

int main(void)
{
    test_production_writer_complete();
    test_legacy_replica_truncated();
    test_not_mounted_path_empty();
    test_web_reader_width_independent();
    test_struct_size_impact();

    printf("%s: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_total, g_fail);
    return g_fail ? 1 : 0;
}
