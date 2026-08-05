/**
 * @file ota_header.h
 * @brief OTA firmware package header structure definition
 * @version 1.0
 * @date 2025-10-15
 */

#ifndef OTA_HEADER_H
#define OTA_HEADER_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>


#ifdef __cplusplus
extern "C" {
#endif

/* Header total size: 1024 bytes */
#define OTA_HEADER_SIZE         1024
#define OTA_MAGIC_NUMBER        0x4F544155  /* "OTAU" */
#define OTA_HEADER_VERSION      0x0100      /* v1.0 */
#define OTA_MAX_NAME_LEN        32
#define OTA_MAX_DESC_LEN        64
#define OTA_HASH_SIZE           32          /* SHA256 */
#define OTA_SIGNATURE_SIZE      256         /* RSA-2048 */
#define OTA_PARTITION_NAME_LEN  16
#define OTA_EXTENSION_COUNT     3
#define OTA_EXTENSION_KEY_LEN   32
#define OTA_EXTENSION_VAL_LEN   32

/* Firmware type constants */
#define OTA_FW_TYPE_UNKNOWN     0x00
#define OTA_FW_TYPE_FSBL        0x01        /* First Stage Boot Loader */
#define OTA_FW_TYPE_APP         0x02        /* Application */
#define OTA_FW_TYPE_WEB         0x03        /* Web Assets */
#define OTA_FW_TYPE_AI_MODEL    0x04        /* AI Model */
#define OTA_FW_TYPE_CONFIG      0x05        /* Configuration */
#define OTA_FW_TYPE_PATCH       0x06        /* Patch */
#define OTA_FW_TYPE_FULL        0x07        /* Full Package */
#define OTA_FW_TYPE_WIFI        0x08        /* WiFi (SiWG917) firmware
                                             * Payload written to flash is the conversion-script
                                             * output (flash_header_t + .rps); the OTA header is
                                             * used only for web verification and is NOT written. */

/* Encryption type constants */
#define OTA_ENCRYPT_NONE        0x00
#define OTA_ENCRYPT_AES128      0x01
#define OTA_ENCRYPT_AES256      0x02

/* Compression type constants */
#define OTA_COMPRESS_NONE       0x00
#define OTA_COMPRESS_GZIP       0x01
#define OTA_COMPRESS_LZ4        0x02

/**
 * @brief OTA firmware package header structure (1024 bytes)
 * @note All fields are plain types, no nested structures or enums
 */
typedef struct __attribute__((packed)) {
    /* ========== Basic Information Section (64 bytes) ========== */
    uint32_t magic;                         /* 0x00: Magic number "OTAU" (0x4F544155) */
    uint16_t header_version;                /* 0x04: Header version (0x0100 = v1.0) */
    uint16_t header_size;                   /* 0x06: Header size (1024) */
    uint32_t header_crc32;                  /* 0x08: Header CRC32 checksum */
    uint8_t  fw_type;                       /* 0x0C: Firmware type (see OTA_FW_TYPE_xxx) */
    uint8_t  encrypt_type;                  /* 0x0D: Encryption type (see OTA_ENCRYPT_xxx) */
    uint8_t  compress_type;                 /* 0x0E: Compression type (see OTA_COMPRESS_xxx) */
    uint8_t  reserved1;                     /* 0x0F: Reserved */
    uint32_t timestamp;                     /* 0x10: Creation timestamp (Unix time) */
    uint32_t sequence;                      /* 0x14: Sequence number */
    uint32_t total_package_size;            /* 0x18: Total package size (header + firmware) */
    uint8_t  reserved2[36];                 /* 0x1C-0x3F: Reserved (36 bytes) */
    
    /* ========== Firmware Information Section (160 bytes) ========== */
    char     fw_name[32];                   /* 0x40-0x5F: Firmware name */
    char     fw_desc[64];                   /* 0x60-0x9F: Firmware description */
    uint8_t  fw_ver[8];                     /* 0xA0-0xA7: Firmware version 
                                             * Format: [major, minor, patch, build_low, build_high, 0, 0, 0]
                                             * build = build_low + build_high * 256 (supports 0-65535) */
    uint8_t  min_ver[8];                    /* 0xA8-0xAF: Minimum compatible version (same format) */
    uint32_t fw_size;                       /* 0xB0: Original firmware size */
    uint32_t fw_size_compressed;            /* 0xB4: Compressed firmware size */
    uint32_t fw_crc32;                      /* 0xB8: Firmware CRC32 checksum */
    uint8_t  fw_hash[32];                   /* 0xBC-0xDB: Firmware SHA256 hash */
    uint8_t  reserved3[4];                  /* 0xDC-0xDF: Reserved (4 bytes) */
    
    /* ========== Target Information Section (64 bytes) ========== */
    uint32_t target_addr;                   /* 0xE0: Target flash address */
    uint32_t target_size;                   /* 0xE4: Target region size */
    uint32_t target_offset;                 /* 0xE8: Target offset address */
    char     target_partition[16];          /* 0xEC-0xFB: Target partition name */
    uint32_t hw_version;                    /* 0xFC: Hardware version requirement */
    uint32_t chip_id;                       /* 0x100: Chip ID requirement */
    uint8_t  reserved4[28];                 /* 0x104-0x11F: Reserved (28 bytes) */
    
    /* ========== Dependency Information Section (64 bytes) ========== */
    uint8_t  reserved_dependencies[64];     /* 0x120-0x15F: Reserved for dependencies */
    
    /* ========== Security Information Section (416 bytes) ========== */
    uint8_t  reserved5[416];                /* 0x160-0x2FF: Reserved for security */
    
    /* ========== Extension Information Section (256 bytes) ========== */
    uint8_t  reserved6[256];                /* 0x300-0x3FF: Reserved for extensions (256 to make 1024 total) */
} ota_header_t;

/* Static assertion to ensure structure size is 1024 bytes */
_Static_assert(sizeof(ota_header_t) == OTA_HEADER_SIZE, "OTA header size must be 1024 bytes");

/**
 * @brief Verify header integrity
 * @param header Header structure pointer
 * @return 0 on success, others on failure
 */
int ota_header_verify(const ota_header_t *header);

/* ==================== Version Utility Macros ==================== */

/**
 * @brief Extract version components from fw_ver[8] array
 * Format: [major, minor, patch, build_low, build_high, 0, 0, 0]
 */
#define OTA_VER_MAJOR(ver)      ((ver)[0])
#define OTA_VER_MINOR(ver)      ((ver)[1])
#define OTA_VER_PATCH(ver)      ((ver)[2])
#define OTA_VER_BUILD(ver)      ((uint16_t)((ver)[3]) | ((uint16_t)((ver)[4]) << 8))

/**
 * @brief Pack version into 32-bit integer for comparison
 * Format: 0xMMmmPPBB (MAJOR.MINOR.PATCH.BUILD_LOW)
 * Note: For full BUILD comparison, use OTA_VER_BUILD() separately
 */
#define OTA_VER_TO_U32(ver)     (((uint32_t)OTA_VER_MAJOR(ver) << 24) | \
                                 ((uint32_t)OTA_VER_MINOR(ver) << 16) | \
                                 ((uint32_t)OTA_VER_PATCH(ver) << 8)  | \
                                 ((uint32_t)((ver)[3])))

/**
 * @brief Compare two version arrays
 * @return >0 if ver1 > ver2, <0 if ver1 < ver2, 0 if equal
 */
static inline int ota_version_compare(const uint8_t *ver1, const uint8_t *ver2)
{
    // Compare MAJOR.MINOR.PATCH first
    for (int i = 0; i < 3; i++) {
        if (ver1[i] != ver2[i]) {
            return (int)ver1[i] - (int)ver2[i];
        }
    }
    // Compare BUILD (16-bit)
    uint16_t build1 = OTA_VER_BUILD(ver1);
    uint16_t build2 = OTA_VER_BUILD(ver2);
    return (int)build1 - (int)build2;
}

/**
 * @brief Format version to string (numeric only, no suffix)
 * @param ver Version array
 * @param buf Output buffer (at least 20 bytes)
 * @param buf_size Buffer size
 */
static inline void ota_version_to_string(const uint8_t *ver, char *buf, size_t buf_size)
{
    snprintf(buf, buf_size, "%u.%u.%u.%u", 
             OTA_VER_MAJOR(ver), OTA_VER_MINOR(ver), 
             OTA_VER_PATCH(ver), OTA_VER_BUILD(ver));
}

/**
 * @brief Extract full version string with suffix from OTA header
 * @param header OTA header pointer
 * @param buf Output buffer (at least 64 bytes recommended)
 * @param buf_size Buffer size
 * @return 0 on success, -1 on failure
 * 
 * @details
 * Extracts version from fw_ver array and suffix from fw_desc field.
 * fw_desc format: "Description (VERSION_WITH_SUFFIX)"
 * Example: "NE301 App (1.0.0.913_beta)" -> "1.0.0.913_beta"
 *          "NE301 App (1.0.0.0)" -> "1.0.0.0"
 */
static inline int ota_header_get_full_version(const ota_header_t *header, char *buf, size_t buf_size)
{
    if (!header || !buf || buf_size < 20) {
        return -1;
    }
    
    // Extract numeric version from fw_ver
    uint8_t major = OTA_VER_MAJOR(header->fw_ver);
    uint8_t minor = OTA_VER_MINOR(header->fw_ver);
    uint8_t patch = OTA_VER_PATCH(header->fw_ver);
    uint16_t build = OTA_VER_BUILD(header->fw_ver);
    
    // Try to extract suffix from fw_desc (format: "Description (VERSION_SUFFIX)")
    const char *desc_start = strchr((const char *)header->fw_desc, '(');
    const char *desc_end = strchr((const char *)header->fw_desc, ')');
    
    if (desc_start && desc_end && desc_end > desc_start) {
        // Extract version string from description
        size_t version_len = desc_end - desc_start - 1;
        if (version_len > 0 && version_len < 60) {
            char version_from_desc[64] = {0};
            memcpy(version_from_desc, desc_start + 1, version_len);
            version_from_desc[version_len] = '\0';
            
            // Check if it contains underscore (has suffix)
            const char *underscore = strchr(version_from_desc, '_');
            if (underscore && *(underscore + 1) != '\0') {
                // Has suffix, use it
                snprintf(buf, buf_size, "%u.%u.%u.%u%s", 
                         major, minor, patch, build, underscore);
                return 0;
            }
        }
    }
    
    // No suffix found, just use numeric version
    snprintf(buf, buf_size, "%u.%u.%u.%u", major, minor, patch, build);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* OTA_HEADER_H */
