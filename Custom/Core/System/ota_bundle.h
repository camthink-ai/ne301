/**
 * @file ota_bundle.h
 * @brief OTA full bundle (one-click upgrade package) header structure
 *
 * A bundle = 4096-byte bundle header + concatenated per-firmware OTA packages
 * (the existing *_pkg.bin files, each already carrying its own 1KB
 * ota_header_t). The header describes which firmwares the bundle contains,
 * their versions, and the partition table the bundle burns at — forced direct
 * burn: every entry is re-burned (no AB slots, no skip/force policy).
 *
 * Layout of the bundle payload is fixed at pack time in burn order:
 *   Model -> WEB -> WiFi -> APP -> FSBL  (least to most critical).
 *
 * Design doc: docs/NE301_OTA_Bundle_Design.md
 */

#ifndef OTA_BUNDLE_H
#define OTA_BUNDLE_H

#include <stdint.h>
#include <stddef.h>
#include "mem_map.h"
#include "ota_header.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUNDLE_HEADER_SIZE   4096
#define BUNDLE_MAGIC         0x4F544146  /* "OTAF" */
#define BUNDLE_HEADER_VER    0x0100      /* v1.0 */
#define BUNDLE_MAX_ENTRIES   10
#define BUNDLE_MAX_PARTS     16

/* Bundle types */
#define BUNDLE_TYPE_FULL     0x01        /* full one-click upgrade package */

/* flags0 and the per-entry flags byte are reserved (zero): the forced-direct
 * revision removed the layout-changed / reset-ota-info / force-all and
 * per-entry-force signalling — a bundle always burns everything it carries. */

/* Partition ids for the bundle partition table */
typedef enum {
    BUNDLE_PART_FSBL = 0,
    BUNDLE_PART_NVS,
    BUNDLE_PART_OTA,
    BUNDLE_PART_SWAP,
    BUNDLE_PART_RESERVE1,
    BUNDLE_PART_APP1,
    BUNDLE_PART_APP2,
    BUNDLE_PART_AI_1,
    BUNDLE_PART_AI_2,
    BUNDLE_PART_WEB,
    BUNDLE_PART_WIFI,
    BUNDLE_PART_LITTLEFS,
    BUNDLE_PART_RESERVE2,
    BUNDLE_PART_ID_COUNT
} bundle_part_id_t;

/* Only FSBL is immovable: the boot ROM fetches it at a silicon-fixed address,
 * so a relocated FSBL never takes effect and its burn would only clobber
 * another region — precheck hard-rejects that. Every other partition may move
 * in a bundle; the user-visible cost (NVS = settings reset, LITTLEFS =
 * storage reformatted) is reported via layout_changed/layout_diff and warned
 * in the UI, not blocked. */

typedef struct __attribute__((packed)) {
    uint8_t  fw_type;        /* OTA_FW_TYPE_xxx (0x01 FSBL / 0x02 APP / 0x03 WEB / 0x04 AI / 0x08 WiFi) */
    uint8_t  flags;          /* reserved (0) */
    uint16_t reserved;
    uint8_t  version[8];     /* same format as ota_header_t.fw_ver */
    uint32_t offset;         /* byte offset of this pkg within the payload area (right after the 4096B header) */
    uint32_t size;           /* pkg size in bytes (incl. its 1KB OTA header) */
    uint32_t pkg_crc32;      /* CRC32 of the pkg payload (cross-check against inner fw_crc32) */
    uint32_t addr_override;  /* pack-time burn address, informational only: the
                              *   device re-resolves every address from the
                              *   partition table at precheck */
    uint32_t reserved2;
} bundle_entry_t;            /* 32 bytes */

typedef struct __attribute__((packed)) {
    uint8_t  part_id;        /* bundle_part_id_t */
    uint8_t  reserved;
    uint16_t reserved2;
    uint32_t base;           /* absolute flash address */
    uint32_t size;
} bundle_part_t;            /* 12 bytes */

typedef struct __attribute__((packed)) {
    /* ===== Basic section (64 bytes) ===== */
    uint32_t magic;            /* 0x00: BUNDLE_MAGIC */
    uint16_t header_version;   /* 0x04: BUNDLE_HEADER_VER */
    uint16_t header_size;      /* 0x06: BUNDLE_HEADER_SIZE */
    uint32_t header_crc32;     /* 0x08: CRC32 of the whole header with this field zeroed */
    uint8_t  bundle_type;      /* 0x0C: BUNDLE_TYPE_FULL */
    uint8_t  flags0;           /* 0x0D: reserved (0) */
    uint8_t  reserved1[2];
    uint32_t timestamp;        /* 0x10: build timestamp (Unix) */
    uint32_t entry_count;      /* 0x14 */
    uint32_t payload_total;    /* 0x18: sum of all entry sizes */
    uint32_t reserved5;        /* 0x1C: reserved (0) */
    uint32_t reserved6;        /* 0x20: reserved (0) — 0x1C/0x20 were the old
                                *            layout_ver_old/new pair; forced-direct
                                *            burns compare the partition tables
                                *            themselves, so the version numbers
                                *            had no consumer and were dropped */
    uint32_t device_model;     /* 0x24: device model id this bundle targets
                                *            (OTA_DEVICE_MODEL; 0 = unstamped/legacy).
                                *            Checked at precheck — before bundle/begin
                                *            erases OTA info — so a cross-model
                                *            bundle is rejected with no side effects. */
    uint8_t  reserved2b[24];   /* 0x28-0x3F: reserved */

    /* ===== Entry table (0x40, 320 bytes) ===== */
    bundle_entry_t entries[BUNDLE_MAX_ENTRIES];

    /* ===== Partition table the bundle burns at (always authoritative) ===== */
    uint16_t part_count;
    uint16_t reserved3;
    bundle_part_t parts[BUNDLE_MAX_PARTS];  /* 0x184, 192 bytes */

    /* ===== Reserved for future use (signatures, manifest strings, ...) ===== */
    uint8_t reserved4[BUNDLE_HEADER_SIZE - 64 - 320 - 4 - 192];  /* 3516 bytes */
} ota_bundle_header_t;

_Static_assert(sizeof(ota_bundle_header_t) == BUNDLE_HEADER_SIZE, "bundle header must be 4096 bytes");
_Static_assert(sizeof(bundle_entry_t) == 32, "bundle entry must be 32 bytes");
_Static_assert(sizeof(bundle_part_t) == 12, "bundle part must be 12 bytes");

/* Verifies magic / header_size / header_version / header_crc32.
 * @return 0 on success, -1 on failure */
int ota_bundle_header_verify(const ota_bundle_header_t *hdr);

/* Structural sanity of the entry table: count in range, offsets sorted and
 * non-overlapping inside [0, payload_total), known fw_type, no duplicates.
 * @return 0 on success, -1 on failure */
int ota_bundle_entries_sane(const ota_bundle_header_t *hdr);

/* 0 when the bundle table matches this firmware's compile-time mem_map.h
 * table for every partition id (missing ids count as mismatch), -1 when any
 * differ. Informational only — feeds layout_changed/layout_diff. */
int ota_bundle_layout_matches_device(const ota_bundle_header_t *hdr);

/* Layout-change safety: returns the first BURNED partition that the bundle
 * table moves or resizes (or omits) while carrying no firmware entry for it
 * (-1 when every such partition is covered). Burning such a bundle would
 * leave the device booting from blank or partially overwritten flash. */
int ota_bundle_moved_burn_part_missing_fw(const ota_bundle_header_t *hdr);

/* Structural validity: part ids known and unique, sizes > 0, no overlap,
 * inside flash bounds, and the FSBL part present-and-equal to this firmware's
 * table (silicon boot address — the only immovable partition).
 * @return 0 ok, -1 bad */
int ota_bundle_layout_change_valid(const ota_bundle_header_t *hdr);

/* Look up a partition's (base, size) in the bundle's NEW layout table.
 * @return 0 found, -1 not found */
int ota_bundle_part_lookup(const ota_bundle_header_t *hdr, uint8_t part_id,
                           uint32_t *base, uint32_t *size);

/* Name of a partition id ("FSBL" .. "RESERVE2"; "?" for unknown ids) —
 * used for the layout_diff diagnostics in the precheck plan JSON. */
const char *ota_bundle_part_name(uint8_t part_id);

/* This firmware's compile-time (base, size) for a partition id.
 * @return 0 ok, -1 unknown id */
int ota_bundle_device_part(uint8_t part_id, uint32_t *base, uint32_t *size);

/* CRC32 (zlib convention) of this firmware's compile-time partition table,
 * hashed as BUNDLE_PART_ID_COUNT packed bundle_part_t records — the same
 * value ota_packer.py stamps into ota_header_t.part_table_crc. Compared by
 * the single-firmware precheck to detect partition-layout drift. */
uint32_t ota_bundle_part_table_crc(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_BUNDLE_H */
