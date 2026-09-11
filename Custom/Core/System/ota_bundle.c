/**
 * @file ota_bundle.c
 * @brief OTA bundle header verification and layout comparison (device side)
 */

#include "ota_bundle.h"
#include "ota_header.h"

/* Device compile-time partition table derived from mem_map.h. Order matches
 * bundle_part_id_t so index == id. */
static const bundle_part_t s_device_parts[BUNDLE_PART_ID_COUNT] = {
    {BUNDLE_PART_FSBL,     0, 0, FSBL_BASE,     FSBL_SIZE},
    {BUNDLE_PART_NVS,      0, 0, NVS_BASE,      NVS_SIZE},
    {BUNDLE_PART_OTA,      0, 0, OTA_BASE,      OTA_SIZE},
    {BUNDLE_PART_SWAP,     0, 0, SWAP_BASE,     SWAP_SIZE},
    {BUNDLE_PART_RESERVE1, 0, 0, RESERVE1_BASE, RESERVE1_SIZE},
    {BUNDLE_PART_APP1,     0, 0, APP1_BASE,     APP1_SIZE},
    {BUNDLE_PART_APP2,     0, 0, APP2_BASE,     APP2_SIZE},
    {BUNDLE_PART_AI_1,     0, 0, AI_1_BASE,     AI_1_SIZE},
    {BUNDLE_PART_AI_2,     0, 0, AI_2_BASE,     AI_2_SIZE},
    {BUNDLE_PART_WEB,      0, 0, WEB_BASE,      WEB_SIZE},
    {BUNDLE_PART_WIFI,     0, 0, WIFI_FW_BASE,  WIFI_FW_SIZE},
    {BUNDLE_PART_LITTLEFS, 0, 0, LITTLEFS_BASE, LITTLEFS_SIZE},
    {BUNDLE_PART_RESERVE2, 0, 0, RESERVE2_BASE, RESERVE2_SIZE},
};

/* zlib-convention CRC32 (same polynomial/init/final as zlib.crc32 and
 * ota_packer.py), fed incrementally. */
static uint32_t s_crc32_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320) : (crc >> 1);
        }
    }
    return crc;
}

int ota_bundle_header_verify(const ota_bundle_header_t *hdr)
{
    if (!hdr) return -1;
    if (hdr->magic != BUNDLE_MAGIC) return -1;
    if (hdr->header_size != BUNDLE_HEADER_SIZE) return -1;
    if (hdr->header_version != BUNDLE_HEADER_VER) return -1;
    if (hdr->bundle_type != BUNDLE_TYPE_FULL) return -1;

    /* CRC over the whole header with the crc field (bytes 8..11) treated as
     * zero — same convention as ota_header_t / ota_packer.py (zlib.crc32).
     * Fed byte-by-byte to avoid a 4KB RAM copy; precheck is rare. */
    const uint8_t *p = (const uint8_t *)hdr;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < BUNDLE_HEADER_SIZE; i++) {
        uint8_t b = (i >= 8 && i < 12) ? 0 : p[i];
        crc = s_crc32_update(crc, &b, 1);
    }
    if ((crc ^ 0xFFFFFFFF) != hdr->header_crc32) return -1;

    return 0;
}

uint32_t ota_bundle_part_table_crc(void)
{
    /* Over the packed s_device_parts records — byte-identical to what
     * ota_packer.py's part_table_crc() hashes into ota_header_t's 0xE0. */
    return s_crc32_update(0xFFFFFFFF, (const uint8_t *)s_device_parts,
                          sizeof(s_device_parts)) ^ 0xFFFFFFFF;
}

int ota_bundle_entries_sane(const ota_bundle_header_t *hdr)
{
    if (!hdr || hdr->entry_count == 0 || hdr->entry_count > BUNDLE_MAX_ENTRIES) return -1;

    uint32_t prev_end = 0;
    uint8_t seen[OTA_FW_TYPE_WIFI + 1] = {0};

    for (uint32_t i = 0; i < hdr->entry_count; i++) {
        const bundle_entry_t *e = &hdr->entries[i];
        if (e->size == 0 || e->size < sizeof(ota_header_t)) return -1;
        if (e->offset < prev_end) return -1;               /* must be sorted & non-overlapping */
        if (e->offset > hdr->payload_total || e->size > hdr->payload_total - e->offset) return -1;
        prev_end = e->offset + e->size;

        switch (e->fw_type) {
            case OTA_FW_TYPE_FSBL:
            case OTA_FW_TYPE_APP:
            case OTA_FW_TYPE_WEB:
            case OTA_FW_TYPE_AI_MODEL:
            case OTA_FW_TYPE_WIFI:
                if (e->fw_type < sizeof(seen)) {
                    if (seen[e->fw_type]) return -1;       /* duplicate type */
                    seen[e->fw_type] = 1;
                }
                break;
            default:
                return -1;                                 /* unknown fw_type */
        }
    }
    return 0;
}

/* Matches the module-wide convention (0 = good): 0 when the bundle table
 * matches this firmware's compile-time mem_map.h table for every partition id
 * (a missing id counts as a mismatch), -1 when any differ. Informational
 * only — feeds layout_changed / layout_diff, never blocks a burn. */
int ota_bundle_layout_matches_device(const ota_bundle_header_t *hdr)
{
    if (!hdr) return -1;

    for (int id = 0; id < BUNDLE_PART_ID_COUNT; id++) {
        uint32_t base = 0, size = 0;
        if (ota_bundle_part_lookup(hdr, (uint8_t)id, &base, &size) != 0 ||
            base != s_device_parts[id].base || size != s_device_parts[id].size) {
            return -1;
        }
    }
    return 0;
}

/* NE301: a layout-changing bundle that MOVES or RESIZES a burned partition
 * must also carry that partition's firmware. The new FSBL boots and the system
 * state is rebuilt against the NEW addresses, so a moved partition without its
 * image (e.g. a bundle packed with --exclude app) points boot selection at
 * blank flash - unbootable. A same-base resize is just as bad: shrinking APP1
 * while later partitions shift down lets another bundled image burn over the
 * stale tail of the existing app. Returns the first moved/resized-but-missing
 * burned part id (bundle_part_id_t), or -1 when every such part is covered by
 * an entry. A partition missing from the bundle table counts as moved: the
 * table IS the layout after the burn. */
int ota_bundle_moved_burn_part_missing_fw(const ota_bundle_header_t *hdr)
{
    static const struct {
        bundle_part_id_t part;
        uint8_t          fw;
    } burned_parts[] = {
        { BUNDLE_PART_APP1, OTA_FW_TYPE_APP },
        { BUNDLE_PART_WEB,  OTA_FW_TYPE_WEB },
        { BUNDLE_PART_AI_1, OTA_FW_TYPE_AI_MODEL },
        { BUNDLE_PART_WIFI, OTA_FW_TYPE_WIFI },
    };

    if (!hdr) return -1;

    /* FSBL is covered by the dedicated "layout-changing bundle must contain
     * FSBL" rule in the precheck; every other burned partition is checked
     * here. */
    for (size_t k = 0; k < sizeof(burned_parts) / sizeof(burned_parts[0]); k++) {
        uint32_t base = 0, size = 0;
        bundle_part_id_t id = burned_parts[k].part;

        if (ota_bundle_part_lookup(hdr, (uint8_t)id, &base, &size) == 0 &&
            base == s_device_parts[id].base &&
            size == s_device_parts[id].size) {
            continue;   /* declared and unchanged (address + size) */
        }
        /* moved/resized (or missing from the table): its firmware must be on board */
        int found = 0;
        for (uint32_t i = 0; i < hdr->entry_count; i++) {
            if (hdr->entries[i].fw_type == burned_parts[k].fw) {
                found = 1;
                break;
            }
        }
        if (!found) return (int)id;
    }
    return -1;
}

/* Partitions the bundle burn path resolves addresses from — mirrors
 * bundle_direct_part_id() in api_ota_module.c (entries burn at their type's
 * partition). Everything else in the table is a layout declaration: after
 * reboot the firmware's own mem_map governs, so an out-of-flash declaration
 * is a layout mismatch (reported via layout_changed), never a burn hazard.
 * Keep in sync with that mapping; drift fails safe — a newly burnable
 * partition outside flash is still caught by upgrade_begin_direct's bound. */
static int bundle_part_is_burned(bundle_part_id_t id)
{
    switch (id) {
        case BUNDLE_PART_FSBL:
        case BUNDLE_PART_APP1:
        case BUNDLE_PART_WEB:
        case BUNDLE_PART_AI_1:
        case BUNDLE_PART_WIFI:
            return 1;
        default:
            return 0;
    }
}

/* Structural validity of the bundle's partition table. Only two things are
 * hard-rejected: physically impossible tables (unknown/duplicate ids, zero
 * sizes, burn targets outside flash, overlapping ranges) and a moved FSBL —
 * the boot ROM fetches the FSBL at a silicon-fixed address, so a relocated
 * FSBL can never take effect and its burn would only clobber whatever now
 * lives at the new address. Every other partition may move: the cost is
 * user-visible data reset (NVS = settings back to defaults, LITTLEFS =
 * storage reformatted) and is surfaced via layout_changed/layout_diff instead
 * of blocking. The flash bound applies to burn targets only — tables packed
 * before RESERVE2 was pulled back to the chip boundary declare a 1MiB-oversized
 * tail and must keep validating. */
int ota_bundle_layout_change_valid(const ota_bundle_header_t *hdr)
{
    if (!hdr || hdr->part_count == 0 || hdr->part_count > BUNDLE_MAX_PARTS) return -1;

    uint32_t flash_end = RESERVE2_END;

    uint8_t seen[BUNDLE_PART_ID_COUNT] = {0};
    int fsbl_seen = 0;
    for (uint32_t i = 0; i < hdr->part_count; i++) {
        const bundle_part_t *p = &hdr->parts[i];
        if (p->part_id >= BUNDLE_PART_ID_COUNT) return -1;
        if (seen[p->part_id]) return -1;
        seen[p->part_id] = 1;

        if (p->size == 0) return -1;
        /* Subtraction form: `p->base + p->size` wraps in uint32_t, which let a
         * crafted table (base near UINT32_MAX + a size that wraps the sum back
         * below flash_end) through this gate and on to the unchecked XSPI
         * erase/write path. With base <= flash_end the right side cannot
         * underflow either. Declarations (non-burn partitions) skip the bound:
         * see bundle_part_is_burned(). */
        if (bundle_part_is_burned((bundle_part_id_t)p->part_id) &&
            (p->base < FLASH_BASE || p->base > flash_end ||
             p->size > flash_end + 1 - p->base)) return -1;

        if (p->part_id == BUNDLE_PART_FSBL) {
            fsbl_seen = 1;
            if (p->base != s_device_parts[BUNDLE_PART_FSBL].base ||
                p->size != s_device_parts[BUNDLE_PART_FSBL].size) {
                return -1;
            }
        }
    }
    if (!fsbl_seen) return -1;   /* FSBL must be declared */
    /* no overlap (sort-free O(n^2), n <= 16) */
    for (uint32_t i = 0; i < hdr->part_count; i++) {
        for (uint32_t j = i + 1; j < hdr->part_count; j++) {
            uint32_t a0 = hdr->parts[i].base, a1 = a0 + hdr->parts[i].size;
            uint32_t b0 = hdr->parts[j].base, b1 = b0 + hdr->parts[j].size;
            if (a0 < b1 && b0 < a1) return -1;
        }
    }
    return 0;
}

int ota_bundle_part_lookup(const ota_bundle_header_t *hdr, uint8_t part_id,
                           uint32_t *base, uint32_t *size)
{
    if (!hdr) return -1;
    for (uint32_t i = 0; i < hdr->part_count && i < BUNDLE_MAX_PARTS; i++) {
        if (hdr->parts[i].part_id == part_id) {
            if (base) *base = hdr->parts[i].base;
            if (size) *size = hdr->parts[i].size;
            return 0;
        }
    }
    return -1;
}

static const char *const s_part_names[BUNDLE_PART_ID_COUNT] = {
    "FSBL", "NVS", "OTA", "SWAP", "RESERVE1", "APP1", "APP2",
    "AI_1", "AI_2", "WEB", "WIFI", "LITTLEFS", "RESERVE2",
};

const char *ota_bundle_part_name(uint8_t part_id)
{
    return (part_id < BUNDLE_PART_ID_COUNT) ? s_part_names[part_id] : "?";
}

int ota_bundle_device_part(uint8_t part_id, uint32_t *base, uint32_t *size)
{
    if (part_id >= BUNDLE_PART_ID_COUNT || !base || !size) return -1;
    *base = s_device_parts[part_id].base;
    *size = s_device_parts[part_id].size;
    return 0;
}
