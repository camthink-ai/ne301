#ifndef APP_ADMISSION_H
#define APP_ADMISSION_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_ADMISSION_CONTAINER_MAGIC_SIZE 8u
#define APP_ADMISSION_LENGTHS_SIZE 8u
#define APP_ADMISSION_MANIFEST_SIZE 160u
#define APP_ADMISSION_MANIFEST_SIZE_V2 192u
#define APP_ADMISSION_MANIFEST_SIZE_V3 192u
#define APP_ADMISSION_MANIFEST_SIZE_V4 208u
#define APP_ADMISSION_MANIFEST_SIZE_V5 208u
#define APP_ADMISSION_NATIVE_HEADER_SIZE 32u
#define APP_ADMISSION_CONTAINER_HEADER_SIZE (APP_ADMISSION_CONTAINER_MAGIC_SIZE + APP_ADMISSION_LENGTHS_SIZE)
#define APP_ADMISSION_MANIFEST_OFFSET APP_ADMISSION_CONTAINER_HEADER_SIZE
#define APP_ADMISSION_IMAGE_OFFSET (APP_ADMISSION_CONTAINER_HEADER_SIZE + APP_ADMISSION_MANIFEST_SIZE)
#define APP_ADMISSION_IMAGE_OFFSET_V2 (APP_ADMISSION_CONTAINER_HEADER_SIZE + APP_ADMISSION_MANIFEST_SIZE_V2)
#define APP_ADMISSION_DER_LEN_MIN 8u
#define APP_ADMISSION_DER_LEN_MAX 72u
#define APP_ADMISSION_MAX_NATIVE_FILE_LEN 2097152u
#define APP_ADMISSION_MAX_PACKAGE_LEN 2097400u
#define APP_ADMISSION_MAX_PACKAGE_LEN_V2 2097432u
#define APP_ADMISSION_MAX_PACKAGE_LEN_V3 2097432u
#define APP_ADMISSION_MAX_PACKAGE_LEN_V4 2097448u
#define APP_ADMISSION_MAX_PACKAGE_LEN_V5 2097376u
#define APP_ADMISSION_IMAGE_OFFSET_V3 (APP_ADMISSION_CONTAINER_HEADER_SIZE + APP_ADMISSION_MANIFEST_SIZE_V3)
#define APP_ADMISSION_IMAGE_OFFSET_V4 (APP_ADMISSION_CONTAINER_HEADER_SIZE + APP_ADMISSION_MANIFEST_SIZE_V4)
#define APP_ADMISSION_IMAGE_OFFSET_V5 (APP_ADMISSION_CONTAINER_HEADER_SIZE + APP_ADMISSION_MANIFEST_SIZE_V5)
#define APP_ADMISSION_PUBLISHER_ID_SLOT 16u
#define APP_ADMISSION_APP_ID_SLOT 32u
#define APP_ADMISSION_SHA256_LEN 32u
#define APP_ADMISSION_ABI_VERSION_V2 0x00020000u
#define APP_ADMISSION_ABI_VERSION_V3 0x00030000u
#define APP_ADMISSION_ABI_VERSION_V4 0x00040000u
#define APP_ADMISSION_V3_ABI_TABLE_SIZE 60u
#define APP_ADMISSION_CAP_LOG 0x00000001u
#define APP_ADMISSION_CAP_TICK_MS 0x00000002u
#define APP_ADMISSION_KNOWN_CAPS_MASK (APP_ADMISSION_CAP_LOG | APP_ADMISSION_CAP_TICK_MS)
#define APP_ADMISSION_V2_CAP_AI_EVENTS 0x00000004u
#define APP_ADMISSION_V2_CAP_REPORT_SUBMIT 0x00000008u
#define APP_ADMISSION_V2_CAP_APP_STATE 0x00000010u
#define APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE 0x00000020u
#define APP_ADMISSION_V2_KNOWN_CAPS_MASK (APP_ADMISSION_KNOWN_CAPS_MASK | \
                                          APP_ADMISSION_V2_CAP_AI_EVENTS | \
                                          APP_ADMISSION_V2_CAP_REPORT_SUBMIT | \
                                          APP_ADMISSION_V2_CAP_APP_STATE | \
                                          APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE)
#define APP_ADMISSION_V2_RUN_PROFILE_SESSION 1u
#define APP_ADMISSION_V2_EVENT_HEADER_SIZE 40u
#define APP_ADMISSION_V2_EVENT_RECORD_SIZE 24u
#define APP_ADMISSION_V2_EVENT_MAX_DETECTIONS 64u
#define APP_ADMISSION_V2_MIN_EVENT_MAX \
    (APP_ADMISSION_V2_EVENT_HEADER_SIZE + \
     (APP_ADMISSION_V2_EVENT_RECORD_SIZE * APP_ADMISSION_V2_EVENT_MAX_DETECTIONS))
#define APP_ADMISSION_V2_MIN_REPORT_MAX 6144u
#define APP_ADMISSION_V3_CAP_CONTROL_RECEIVE 0x00000040u
#define APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT 0x00000080u
#define APP_ADMISSION_V3_KNOWN_CAPS_MASK (APP_ADMISSION_V2_KNOWN_CAPS_MASK | \
                                          APP_ADMISSION_V3_CAP_CONTROL_RECEIVE | \
                                          APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT)
#define APP_ADMISSION_V3_RUN_PROFILE_HOT_COMMAND_SESSION 2u
#define APP_ADMISSION_V3_CONTROL_WIRE_MIN 32u
#define APP_ADMISSION_V3_EVENT_MAX 1576u
#define APP_ADMISSION_V3_REPORT_MAX 6144u
#define APP_ADMISSION_V4_CAP_ENABLE_CONTROL 0x00000100u
#define APP_ADMISSION_V4_CAP_LIVE_PUBLISH 0x00000200u
#define APP_ADMISSION_V4_KNOWN_CAPS_MASK 0x000003FFu
#define APP_ADMISSION_V4_RUN_PROFILE_SESSION 3u
#define APP_ADMISSION_V4_ABI_TABLE_SIZE 64u
#define APP_ADMISSION_V4_CONTROL_WIRE_MIN 48u
#define APP_ADMISSION_V4_LIVE_HEADER_SIZE 48u
#define APP_ADMISSION_V4_LIVE_EVENT_RECORD_SIZE 16u
#define APP_ADMISSION_V4_LIVE_TRACK_HEADER_SIZE 12u
#define APP_ADMISSION_V4_LIVE_TRACK_POINT_SIZE 8u
#define APP_ADMISSION_V4_LIVE_TRACK_POINT_MAX 16u
#define APP_ADMISSION_V4_LIVE_TRACK_MAX 64u
#define APP_ADMISSION_V4_LIVE_MIN_PUBLISH 312u
#define APP_ADMISSION_EXPERIMENT_BOARD_ID 0x00003010u
#define APP_ADMISSION_ADMIN_PUBLISHER_ID "admin-installed"

typedef enum {
    APP_ADMISSION_V3_BUSINESS_NONE = 0,
    APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64 = 1,
    APP_ADMISSION_V3_BUSINESS_GENERIC_FULL = 2
} app_admission_v3_business_profile_t;

typedef enum {
    APP_ADMISSION_OK = 0,
    APP_ADMISSION_ERR_PARAM = -1,
    APP_ADMISSION_ERR_BAD_PACKAGE = -2,
    APP_ADMISSION_ERR_SIGNATURE_INVALID = -3,
    APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED = -4,
    APP_ADMISSION_ERR_TARGET_INCOMPATIBLE = -5,
    APP_ADMISSION_ERR_ABI_INCOMPATIBLE = -6,
    APP_ADMISSION_ERR_RESOURCE_LIMIT = -7,
    APP_ADMISSION_ERR_TRUST_UNPROVEN = -8,
    APP_ADMISSION_ERR_LEGACY_SIGNED_FORMAT = -9,
    APP_ADMISSION_ERR_UNKNOWN_FORMAT = -10
} app_admission_result_t;

typedef struct {
    uint32_t target_board_id;
    uint32_t psram_mib;
    uint32_t abi_version;
    uint32_t supported_caps_mask;
    uint32_t exec_region_base;
    uint32_t exec_region_size;
    uint32_t event_capacity;
    uint32_t report_capacity;
    uint32_t state_quota_capacity;
    size_t max_package_len;
    uint32_t abi_table_size;
    uint32_t control_capacity;
    uint32_t snapshot_capacity;
    app_admission_v3_business_profile_t v3_business_profile;
    uint32_t live_publish_capacity;
    uint32_t live_track_capacity;
} app_admission_policy_t;

typedef struct {
    const char *publisher_id;
    size_t publisher_id_len;
    const uint8_t *spki_der;
    size_t spki_der_len;
} app_admission_trust_view_t;

typedef struct {
    char publisher_id[APP_ADMISSION_PUBLISHER_ID_SLOT + 1];
    char app_id[APP_ADMISSION_APP_ID_SLOT + 1];
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint32_t required_psram_mib;
    uint32_t required_host_abi;
    uint32_t required_host_caps;
    uint32_t required_exec_region_bytes;
    uint32_t native_image_size;
    uint32_t native_entry_offset;
    uint32_t native_target_addr;
    size_t tbs_len;
    size_t signature_len;
    uint32_t run_profile;
    uint32_t event_max;
    uint32_t state_quota;
    uint32_t report_max;
    uint32_t control_max;
    uint32_t snapshot_max;
    uint32_t container_major;
    uint32_t live_publish_max;
    uint32_t live_track_max;
    uint8_t tbs_sha256[APP_ADMISSION_SHA256_LEN];
    uint8_t image_sha256[APP_ADMISSION_SHA256_LEN];
    uint8_t publisher_key_sha256[APP_ADMISSION_SHA256_LEN];
} app_admission_verdict_t;

app_admission_result_t app_admission_verify_package(const uint8_t *data,
                                                    size_t len,
                                                    const app_admission_policy_t *policy,
                                                    const app_admission_trust_view_t *trust,
                                                    app_admission_verdict_t *out);

uint32_t app_admission_container_version(const uint8_t *data, size_t len);

aicam_bool_t app_admission_identifier_valid(const uint8_t *field, size_t slot_len);

const char *app_admission_result_str(app_admission_result_t result);

#ifdef __cplusplus
}
#endif

#endif
