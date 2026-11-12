/*
 * Overflow-elimination proof for fix 40525ccc1 (fs_type [8] -> [16]).
 *
 * Parses the REAL production header Custom/Hal/storage.h on host (shim dir
 * supplies only the headers storage.h pulls in but does not use for this
 * struct). The production writer is storage.c:781:
 *
 *     strcpy(info->fs_type, "littlefs");
 *
 * "littlefs" is 8 chars + NUL = 9 bytes. Pre-fix field was char[8] -> the
 * NUL landed 1 byte past the struct end (fs_type is the final member).
 * Post-fix field is char[16] -> write stays in bounds.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>

#include "storage.h" /* REAL production header (see Makefile -I order) */

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

/* Compile-time: production field holds the full literal incl. NUL. */
_Static_assert(sizeof("littlefs") <= sizeof(((storage_disk_info_t *)0)->fs_type),
               "storage_disk_info_t.fs_type can no longer hold \"littlefs\" + NUL");

/* Compile-time: with the production layout, the write ends strictly inside
 * the struct (fs_type is the final member, so this also means strictly
 * inside the field). */
_Static_assert(offsetof(storage_disk_info_t, fs_type) + sizeof("littlefs")
                   <= sizeof(storage_disk_info_t),
               "production strcpy would reach past the struct end");

/* Byte-exact replica of the PRE-fix struct (fs_type[8]) used only to
 * demonstrate the defect that was eliminated; never instantiated in firmware. */
typedef struct {
    bool mounted;
    uint32_t total_KBytes;
    uint32_t free_KBytes;
    char fs_type[8];
} legacy_disk_info_replica_t;

/* Replica layout fact that made the pre-fix write overflow: the NUL byte
 * lands exactly on the first byte past the struct end. */
_Static_assert(offsetof(legacy_disk_info_replica_t, fs_type) + 8
                   == sizeof(legacy_disk_info_replica_t),
               "replica layout drifted: fs_type must be the final member");

/* char array after the struct starts at the very next byte (alignment 1),
 * so a 1-byte overflow past the struct end is observable in tail[0]. */
typedef struct {
    storage_disk_info_t info;
    unsigned char tail[16];
} disk_frame_t;

typedef struct {
    legacy_disk_info_replica_t info;
    unsigned char tail[16];
} legacy_frame_t;

static void test_production_writer_in_bounds(void)
{
    disk_frame_t f;
    size_t i;

    memset(&f, 0xAA, sizeof(f));

    /* Exact production statement (Custom/Hal/storage.c:781). */
    strcpy(f.info.fs_type, "littlefs");

    /* No byte past the struct end is touched any more. */
    for (i = 0; i < sizeof(f.tail); i++) {
        if (f.tail[i] != 0xAA) {
            CHECK(f.tail[i] == 0xAA && "production field still overflows");
            return;
        }
    }
    CHECK(1);

    /* NUL terminates INSIDE the field; %s consumers (cli_cmd.c:639,
     * device_service.c:506) no longer depend on out-of-bounds memory. */
    CHECK(strcmp(f.info.fs_type, "littlefs") == 0);
    CHECK(strlen(f.info.fs_type) == 8);
    CHECK(strlen(f.info.fs_type) < sizeof(f.info.fs_type));
    CHECK(f.info.fs_type[8] == '\0');
}

static void test_legacy_replica_exhibited_overflow(void)
{
    /* The actual OOB write is not executed here (it is exactly the UB the
     * fix removed, and host fortify rightly rejects it at compile time —
     * clang: "'strcpy' will always overflow; destination buffer has size 8,
     * but the source string has length 9"). Instead the same 9 bytes are
     * placed via a union view, so every access stays inside one object
     * while still showing where each byte lands. */
    typedef union {
        legacy_frame_t frame;
        unsigned char bytes[sizeof(legacy_frame_t)];
    } legacy_view_t;

    legacy_view_t v;
    size_t i;

    _Static_assert(offsetof(legacy_frame_t, info) == 0, "frame layout drifted");

    memset(&v, 0xAA, sizeof(v));

    /* Equivalent of strcpy(info.fs_type, "littlefs"): 8 chars + NUL = 9 bytes
     * written starting at the field offset. */
    memcpy(&v.bytes[offsetof(legacy_disk_info_replica_t, fs_type)], "littlefs", 9);

    /* The 9th byte (NUL) necessarily landed past the replica struct end. */
    CHECK(v.frame.tail[0] == 0x00);

    /* In-bounds bytes carry the full literal with no room for a NUL. */
    CHECK(memcmp(v.frame.info.fs_type, "littlefs", 8) == 0);

    /* Everything past the OOB NUL is untouched (exactly 1-byte overflow). */
    for (i = 1; i < sizeof(v.frame.tail); i++) {
        if (v.frame.tail[i] != 0xAA) {
            CHECK(v.frame.tail[i] == 0xAA && "replica overflow is larger than 1 byte");
            return;
        }
    }
    CHECK(1);
}

int main(void)
{
    test_production_writer_in_bounds();
    test_legacy_replica_exhibited_overflow();

    printf("%s: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_total, g_fail);
    return g_fail ? 1 : 0;
}
