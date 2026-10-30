#include "app_admission.h"

#include <string.h>

#include "generic_math.h"
#include "mbedtls/sha256.h"

#include "app_host_abi.h"

#define APP_ADMISSION_MANIFEST_MAGIC_V4 "NMF4"
#define APP_ADMISSION_MANIFEST_MAGIC_V5 "NMF5"
#define APP_ADMISSION_NATIVE_HEADER_MAGIC 0x3141454Eu

static const uint8_t g_ad_container_magic_v1[APP_ADMISSION_CONTAINER_MAGIC_SIZE] = {
    0x4Eu, 0x45u, 0x41u, 0x50u, 0x50u, 0x01u, 0x00u, 0x00u
};

static const uint8_t g_ad_container_magic_v2[APP_ADMISSION_CONTAINER_MAGIC_SIZE] = {
    0x4Eu, 0x45u, 0x41u, 0x50u, 0x50u, 0x02u, 0x00u, 0x00u
};

static const uint8_t g_ad_container_magic_v3[APP_ADMISSION_CONTAINER_MAGIC_SIZE] = {
    0x4Eu, 0x45u, 0x41u, 0x50u, 0x50u, 0x03u, 0x00u, 0x00u
};

static const uint8_t g_ad_container_magic_v4[APP_ADMISSION_CONTAINER_MAGIC_SIZE] = {
    0x4Eu, 0x45u, 0x41u, 0x50u, 0x50u, 0x04u, 0x00u, 0x00u
};

static const uint8_t g_ad_container_magic_v5[APP_ADMISSION_CONTAINER_MAGIC_SIZE] = {
    0x4Eu, 0x45u, 0x41u, 0x50u, 0x50u, 0x05u, 0x00u, 0x00u
};

typedef struct {
    size_t image_offset;
    size_t image_len;
    size_t sig_offset;
    size_t sig_len;
} app_admission_layout_t;

typedef struct {
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint32_t target_board_id;
    uint32_t required_psram_mib;
    uint32_t required_host_abi;
    uint32_t required_host_caps;
    uint32_t required_exec_region_bytes;
    uint32_t native_file_len;
    uint32_t native_image_size;
    uint32_t native_entry_offset;
    uint32_t native_target_addr;
    const uint8_t *native_file_sha256;
    const uint8_t *publisher_key_sha256;
} app_admission_manifest_fields_t;

static size_t ad_manifest_identifier_len(const uint8_t *field, size_t slot_len);

static uint32_t ad_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t ad_u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int ad_ct_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    volatile uint8_t diff = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

aicam_bool_t app_admission_identifier_valid(const uint8_t *field, size_t slot_len)
{
    size_t text_len = 0;
    size_t i;

    if (field == NULL || slot_len == 0u) {
        return AICAM_FALSE;
    }

    while (text_len < slot_len && field[text_len] != 0x00u) {
        text_len++;
    }

    for (i = text_len; i < slot_len; i++) {
        if (field[i] != 0x00u) {
            return AICAM_FALSE;
        }
    }

    if (text_len == 0u) {
        return AICAM_FALSE;
    }

    if (field[0] < (uint8_t)'a' || field[0] > (uint8_t)'z') {
        return AICAM_FALSE;
    }

    for (i = 0; i < text_len; i++) {
        uint8_t c = field[i];
        if (!((c >= (uint8_t)'a' && c <= (uint8_t)'z') ||
              (c >= (uint8_t)'0' && c <= (uint8_t)'9') ||
              c == (uint8_t)'-')) {
            return AICAM_FALSE;
        }
    }

    if (text_len > 1u) {
        uint8_t last = field[text_len - 1u];
        if (last == (uint8_t)'-') {
            return AICAM_FALSE;
        }
    }

    return AICAM_TRUE;
}

static app_admission_result_t ad_parse_manifest_v4(const uint8_t *manifest,
                                                   app_admission_manifest_fields_t *m,
                                                   uint32_t *run_profile,
                                                   uint32_t *event_max,
                                                   uint32_t *state_quota,
                                                   uint32_t *report_max,
                                                   uint32_t *control_max,
                                                   uint32_t *snapshot_max,
                                                   uint32_t *live_publish_max,
                                                   uint32_t *live_track_max)
{
    uint32_t caps;
    size_t i;

    if (memcmp(manifest, APP_ADMISSION_MANIFEST_MAGIC_V4, 4u) != 0 ||
        !app_admission_identifier_valid(manifest + 4u, APP_ADMISSION_PUBLISHER_ID_SLOT) ||
        !app_admission_identifier_valid(manifest + 20u, APP_ADMISSION_APP_ID_SLOT) ||
        manifest[58] != 0u || manifest[59] != 0u ||
        ad_u32le(manifest + 68u) != APP_ADMISSION_ABI_VERSION_V4 ||
        ad_u32le(manifest + 160u) != APP_ADMISSION_V4_RUN_PROFILE_SESSION) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    for (i = 192u; i < APP_ADMISSION_MANIFEST_SIZE_V4; i++) {
        if (manifest[i] != 0u) {
            return APP_ADMISSION_ERR_BAD_PACKAGE;
        }
    }
    caps = ad_u32le(manifest + 72u);
    if ((caps & ~APP_ADMISSION_V4_KNOWN_CAPS_MASK) != 0u ||
        (caps & APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE) == 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    (*event_max) = ad_u32le(manifest + 164u);
    (*state_quota) = ad_u32le(manifest + 168u);
    (*report_max) = ad_u32le(manifest + 172u);
    (*control_max) = ad_u32le(manifest + 176u);
    (*snapshot_max) = ad_u32le(manifest + 180u);
    (*live_publish_max) = ad_u32le(manifest + 184u);
    (*live_track_max) = ad_u32le(manifest + 188u);
    (*run_profile) = APP_ADMISSION_V4_RUN_PROFILE_SESSION;
    if (((caps & APP_ADMISSION_V2_CAP_AI_EVENTS) != 0u) != (*event_max != 0u) ||
        ((caps & APP_ADMISSION_V2_CAP_APP_STATE) != 0u) != (*state_quota != 0u) ||
        ((caps & APP_ADMISSION_V2_CAP_REPORT_SUBMIT) != 0u) != (*report_max != 0u) ||
        ((caps & APP_ADMISSION_V3_CAP_CONTROL_RECEIVE) != 0u) != (*control_max != 0u) ||
        ((caps & APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT) != 0u) != (*snapshot_max != 0u)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((caps & APP_ADMISSION_V4_CAP_ENABLE_CONTROL) != 0u &&
        (caps & (APP_ADMISSION_V2_CAP_APP_STATE |
                 APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
                 APP_ADMISSION_V3_CAP_CONTROL_RECEIVE)) !=
            (APP_ADMISSION_V2_CAP_APP_STATE |
             APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
             APP_ADMISSION_V3_CAP_CONTROL_RECEIVE)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if ((caps & APP_ADMISSION_V4_CAP_ENABLE_CONTROL) != 0u &&
        (*control_max) < APP_ADMISSION_V4_CONTROL_WIRE_MIN) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }
    if ((caps & APP_ADMISSION_V4_CAP_LIVE_PUBLISH) != 0u) {
        uint64_t closed_max;
        if ((caps & APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE) == 0u ||
            (*live_track_max) == 0u || (*live_track_max) > APP_ADMISSION_V4_LIVE_TRACK_MAX) {
            return APP_ADMISSION_ERR_BAD_PACKAGE;
        }
        closed_max = (uint64_t)APP_ADMISSION_V4_LIVE_HEADER_SIZE +
                     (uint64_t)*live_track_max *
                         ((uint64_t)APP_ADMISSION_V4_LIVE_TRACK_HEADER_SIZE +
                          (uint64_t)APP_ADMISSION_V4_LIVE_TRACK_POINT_MAX *
                              APP_ADMISSION_V4_LIVE_TRACK_POINT_SIZE);
        if (*live_publish_max < APP_ADMISSION_V4_LIVE_MIN_PUBLISH ||
            (uint64_t)*live_publish_max < closed_max) {
            return APP_ADMISSION_ERR_RESOURCE_LIMIT;
        }
    } else if (*live_publish_max != 0u || *live_track_max != 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    m->version_major = ad_u16le(manifest + 52u);
    m->version_minor = ad_u16le(manifest + 54u);
    m->version_patch = ad_u16le(manifest + 56u);
    m->target_board_id = ad_u32le(manifest + 60u);
    m->required_psram_mib = ad_u32le(manifest + 64u);
    m->required_host_abi = ad_u32le(manifest + 68u);
    m->required_host_caps = caps;
    m->required_exec_region_bytes = ad_u32le(manifest + 76u);
    m->native_file_len = ad_u32le(manifest + 80u);
    m->native_image_size = ad_u32le(manifest + 84u);
    m->native_entry_offset = ad_u32le(manifest + 88u);
    m->native_target_addr = ad_u32le(manifest + 92u);
    m->native_file_sha256 = manifest + 96u;
    m->publisher_key_sha256 = manifest + 128u;
    return APP_ADMISSION_OK;
}

static app_admission_result_t ad_parse_manifest_v5(const uint8_t *manifest,
                                                   app_admission_manifest_fields_t *m,
                                                   uint32_t *run_profile,
                                                   uint32_t *event_max,
                                                   uint32_t *state_quota,
                                                   uint32_t *report_max,
                                                   uint32_t *control_max,
                                                   uint32_t *snapshot_max,
                                                   uint32_t *live_publish_max,
                                                   uint32_t *live_track_max)
{
    uint8_t view[APP_ADMISSION_MANIFEST_SIZE_V5];
    size_t i;
    app_admission_result_t rc;

    if (manifest == NULL || memcmp(manifest, APP_ADMISSION_MANIFEST_MAGIC_V5, 4u) != 0) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    for (i = 4u; i < 20u; i++) {
        if (manifest[i] != 0u) {
            return APP_ADMISSION_ERR_BAD_PACKAGE;
        }
    }
    for (i = 128u; i < 160u; i++) {
        if (manifest[i] != 0u) {
            return APP_ADMISSION_ERR_BAD_PACKAGE;
        }
    }
    for (i = 192u; i < APP_ADMISSION_MANIFEST_SIZE_V5; i++) {
        if (manifest[i] != 0u) {
            return APP_ADMISSION_ERR_BAD_PACKAGE;
        }
    }
    memcpy(view, manifest, sizeof(view));
    memcpy(view, APP_ADMISSION_MANIFEST_MAGIC_V4, 4u);
    memcpy(view + 4u, APP_ADMISSION_ADMIN_PUBLISHER_ID,
           strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID));
    rc = ad_parse_manifest_v4(view, m, run_profile, event_max, state_quota,
                              report_max, control_max, snapshot_max,
                              live_publish_max, live_track_max);
    if (rc == APP_ADMISSION_OK) {
        m->native_file_sha256 = manifest + 96u;
        m->publisher_key_sha256 = manifest + 128u;
    }
    memset(view, 0, sizeof(view));
    return rc;
}

static app_admission_result_t ad_verify_v5(const uint8_t *data,
                                           size_t len,
                                           const app_admission_policy_t *policy,
                                           app_admission_verdict_t *out)
{
    app_admission_layout_t layout;
    app_admission_manifest_fields_t mf = {0};
    const uint8_t *manifest;
    const uint8_t *img;
    const app_host_image_header_t *hdr;
    uint32_t run_profile = 0u;
    uint32_t event_max = 0u;
    uint32_t state_quota = 0u;
    uint32_t report_max = 0u;
    uint32_t control_max = 0u;
    uint32_t snapshot_max = 0u;
    uint32_t live_publish_max = 0u;
    uint32_t live_track_max = 0u;
    uint32_t caps;
    uint8_t tbs_sha[APP_ADMISSION_SHA256_LEN];
    uint8_t image_sha[APP_ADMISSION_SHA256_LEN];
    app_admission_result_t rc;
    size_t app_len;

    if (len < APP_ADMISSION_CONTAINER_HEADER_SIZE ||
        memcmp(data, g_ad_container_magic_v5, APP_ADMISSION_CONTAINER_MAGIC_SIZE) != 0) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    if (ad_u32le(data + 8u) != APP_ADMISSION_MANIFEST_SIZE_V5) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    layout.image_offset = APP_ADMISSION_IMAGE_OFFSET_V4;
    layout.image_len = (size_t)ad_u32le(data + 12u);
    layout.sig_offset = len;
    layout.sig_len = 0u;
    if (layout.image_len < APP_ADMISSION_NATIVE_HEADER_SIZE ||
        layout.image_len > APP_ADMISSION_MAX_NATIVE_FILE_LEN ||
        (uint64_t)layout.image_len + APP_ADMISSION_IMAGE_OFFSET_V4 != (uint64_t)len ||
        len > APP_ADMISSION_MAX_PACKAGE_LEN_V5 ||
        (policy->max_package_len != 0u && len > policy->max_package_len)) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    manifest = data + APP_ADMISSION_CONTAINER_HEADER_SIZE;
    rc = ad_parse_manifest_v5(manifest, &mf, &run_profile, &event_max,
                              &state_quota, &report_max, &control_max,
                              &snapshot_max, &live_publish_max, &live_track_max);
    if (rc != APP_ADMISSION_OK) {
        return rc;
    }
    if (mf.version_major == 0u) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    img = data + layout.image_offset;
    hdr = (const app_host_image_header_t *)(const void *)img;
    mbedtls_sha256(data, len, tbs_sha, 0);
    mbedtls_sha256(img, layout.image_len, image_sha, 0);
    if ((uint64_t)mf.native_file_len != (uint64_t)layout.image_len ||
        (uint64_t)mf.native_file_len !=
            (uint64_t)APP_ADMISSION_NATIVE_HEADER_SIZE + mf.native_image_size ||
        hdr->magic != APP_ADMISSION_NATIVE_HEADER_MAGIC ||
        hdr->header_size != APP_ADMISSION_NATIVE_HEADER_SIZE ||
        hdr->format_version != APP_HOST_IMAGE_FORMAT_VERSION || hdr->reserved0 != 0u ||
        hdr->image_size != mf.native_image_size ||
        hdr->entry_offset != mf.native_entry_offset ||
        hdr->target_addr != mf.native_target_addr ||
        hdr->abi_version != APP_ADMISSION_ABI_VERSION_V4 ||
        hdr->abi_version != mf.required_host_abi ||
        (uint64_t)mf.native_entry_offset >= mf.native_image_size ||
        (mf.native_entry_offset & APP_HOST_ENTRY_THUMB_MASK) != 0u ||
        !ad_ct_equal(mf.native_file_sha256, image_sha, APP_ADMISSION_SHA256_LEN) ||
        generic_crc32(img + APP_ADMISSION_NATIVE_HEADER_SIZE,
                      (size_t)mf.native_image_size) != hdr->crc32) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    caps = mf.required_host_caps;
    if ((caps & APP_ADMISSION_V3_CAP_CONTROL_RECEIVE) != 0u &&
        control_max < APP_ADMISSION_V3_CONTROL_WIRE_MIN) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }
    if (mf.target_board_id != policy->target_board_id ||
        (uint64_t)mf.required_psram_mib > policy->psram_mib ||
        mf.native_target_addr != policy->exec_region_base) {
        return APP_ADMISSION_ERR_TARGET_INCOMPATIBLE;
    }
    if (mf.required_host_abi != policy->abi_version ||
        policy->abi_table_size != APP_ADMISSION_V4_ABI_TABLE_SIZE ||
        (caps & ~policy->supported_caps_mask) != 0u) {
        return APP_ADMISSION_ERR_ABI_INCOMPATIBLE;
    }
    if (policy->v3_business_profile == APP_ADMISSION_V3_BUSINESS_NONE) {
        uint32_t business_caps = APP_ADMISSION_V2_CAP_AI_EVENTS |
                                 APP_ADMISSION_V2_CAP_REPORT_SUBMIT |
                                 APP_ADMISSION_V2_CAP_APP_STATE |
                                 APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT |
                                 APP_ADMISSION_V4_CAP_ENABLE_CONTROL |
                                 APP_ADMISSION_V4_CAP_LIVE_PUBLISH;
        if ((caps & business_caps) != 0u) {
            return APP_ADMISSION_ERR_ABI_INCOMPATIBLE;
        }
    }
    if ((uint64_t)mf.native_file_len > policy->exec_region_size ||
        (uint64_t)mf.required_exec_region_bytes < mf.native_image_size ||
        (uint64_t)mf.required_exec_region_bytes > policy->exec_region_size) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }
    if (policy->v3_business_profile == APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64) {
        uint32_t required_caps = APP_ADMISSION_V2_CAP_AI_EVENTS |
                                 APP_ADMISSION_V2_CAP_REPORT_SUBMIT |
                                 APP_ADMISSION_V2_CAP_APP_STATE |
                                 APP_ADMISSION_V2_CAP_SESSION_LIFECYCLE |
                                 APP_ADMISSION_V3_CAP_CONTROL_RECEIVE |
                                 APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT;
        if ((caps & required_caps) != required_caps) {
            return APP_ADMISSION_ERR_BAD_PACKAGE;
        }
        if (event_max < APP_ADMISSION_V3_EVENT_MAX ||
            report_max < APP_ADMISSION_V3_REPORT_MAX) {
            return APP_ADMISSION_ERR_RESOURCE_LIMIT;
        }
    }
    if (((caps & APP_ADMISSION_V2_CAP_AI_EVENTS) != 0u && event_max > policy->event_capacity) ||
        ((caps & APP_ADMISSION_V2_CAP_REPORT_SUBMIT) != 0u && report_max > policy->report_capacity) ||
        ((caps & APP_ADMISSION_V2_CAP_APP_STATE) != 0u && state_quota > policy->state_quota_capacity) ||
        ((caps & APP_ADMISSION_V3_CAP_CONTROL_RECEIVE) != 0u && control_max > policy->control_capacity) ||
        ((caps & APP_ADMISSION_V3_CAP_BUSINESS_SNAPSHOT) != 0u && snapshot_max > policy->snapshot_capacity) ||
        ((caps & APP_ADMISSION_V4_CAP_LIVE_PUBLISH) != 0u &&
         (live_publish_max > policy->live_publish_capacity ||
          live_track_max > policy->live_track_capacity))) {
        return APP_ADMISSION_ERR_RESOURCE_LIMIT;
    }
    app_len = ad_manifest_identifier_len(manifest + 20u, APP_ADMISSION_APP_ID_SLOT);
    if (app_len == 0u || app_len > APP_ADMISSION_APP_ID_SLOT) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }
    memcpy(out->publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID,
           strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID));
    out->publisher_id[strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID)] = '\0';
    memcpy(out->app_id, manifest + 20u, app_len);
    out->app_id[app_len] = '\0';
    out->version_major = mf.version_major;
    out->version_minor = mf.version_minor;
    out->version_patch = mf.version_patch;
    out->required_psram_mib = mf.required_psram_mib;
    out->required_host_abi = mf.required_host_abi;
    out->required_host_caps = caps;
    out->required_exec_region_bytes = mf.required_exec_region_bytes;
    out->native_image_size = mf.native_image_size;
    out->native_entry_offset = mf.native_entry_offset;
    out->native_target_addr = mf.native_target_addr;
    out->run_profile = run_profile;
    out->event_max = event_max;
    out->state_quota = state_quota;
    out->report_max = report_max;
    out->control_max = control_max;
    out->snapshot_max = snapshot_max;
    out->live_publish_max = live_publish_max;
    out->live_track_max = live_track_max;
    out->tbs_len = len;
    out->signature_len = 0u;
    memcpy(out->tbs_sha256, tbs_sha, APP_ADMISSION_SHA256_LEN);
    memcpy(out->image_sha256, image_sha, APP_ADMISSION_SHA256_LEN);
    memset(out->publisher_key_sha256, 0, APP_ADMISSION_SHA256_LEN);
    out->container_major = 5u;
    return APP_ADMISSION_OK;
}

static size_t ad_manifest_identifier_len(const uint8_t *field, size_t slot_len)
{
    size_t text_len = 0;
    while (text_len < slot_len && field[text_len] != 0x00u) {
        text_len++;
    }
    return text_len;
}

app_admission_result_t app_admission_verify_package(const uint8_t *data,
                                                    size_t len,
                                                    const app_admission_policy_t *policy,
                                                    const app_admission_trust_view_t *trust,
                                                    app_admission_verdict_t *out)
{
    uint32_t major;

    (void)trust;
    if (data == NULL || policy == NULL || out == NULL) {
        return APP_ADMISSION_ERR_PARAM;
    }

    memset(out, 0, sizeof(*out));

    if (len < (size_t)APP_ADMISSION_CONTAINER_HEADER_SIZE) {
        return APP_ADMISSION_ERR_BAD_PACKAGE;
    }

    major = app_admission_container_version(data, len);
    if (major >= 1u && major <= 4u) {
        return APP_ADMISSION_ERR_LEGACY_SIGNED_FORMAT;
    }
    if (major != 5u) {
        return APP_ADMISSION_ERR_UNKNOWN_FORMAT;
    }
    return ad_verify_v5(data, len, policy, out);
}

uint32_t app_admission_container_version(const uint8_t *data, size_t len)
{
    if (data == NULL || len < (size_t)APP_ADMISSION_CONTAINER_HEADER_SIZE) {
        return 0u;
    }

    if (memcmp(data, g_ad_container_magic_v1, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
        return 1u;
    }

    if (memcmp(data, g_ad_container_magic_v2, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
        return 2u;
    }

    if (memcmp(data, g_ad_container_magic_v3, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
        return 3u;
    }

    if (memcmp(data, g_ad_container_magic_v4, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
        return 4u;
    }

    if (memcmp(data, g_ad_container_magic_v5, APP_ADMISSION_CONTAINER_MAGIC_SIZE) == 0) {
        return 5u;
    }

    return 0u;
}

const char *app_admission_result_str(app_admission_result_t result)
{
    switch (result) {
    case APP_ADMISSION_OK:
        return "compatible";
    case APP_ADMISSION_ERR_PARAM:
        return "PARAM";
    case APP_ADMISSION_ERR_BAD_PACKAGE:
        return "BAD_PACKAGE";
    case APP_ADMISSION_ERR_SIGNATURE_INVALID:
    case APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED:
        return "LEGACY_SIGNED_FORMAT_REJECTED";
    case APP_ADMISSION_ERR_TARGET_INCOMPATIBLE:
        return "TARGET_INCOMPATIBLE";
    case APP_ADMISSION_ERR_ABI_INCOMPATIBLE:
        return "ABI_INCOMPATIBLE";
    case APP_ADMISSION_ERR_RESOURCE_LIMIT:
        return "RESOURCE_LIMIT";
    case APP_ADMISSION_ERR_TRUST_UNPROVEN:
        return "LEGACY_SIGNED_FORMAT_REJECTED";
    case APP_ADMISSION_ERR_LEGACY_SIGNED_FORMAT:
        return "LEGACY_SIGNED_FORMAT";
    case APP_ADMISSION_ERR_UNKNOWN_FORMAT:
        return "UNKNOWN_FORMAT";
    default:
        return "UNKNOWN";
    }
}
