#include "app_host_validate.h"

#include "generic_math.h"

app_host_check_t app_host_validate(const uint8_t *image, size_t file_size, const app_host_load_policy_t *policy, app_host_image_info_t *out)
{
    if (image == NULL || policy == NULL || out == NULL) {
        return APP_HOST_CHECK_ERR_PARAM;
    }

    if (file_size < APP_HOST_IMAGE_HEADER_SIZE) {
        return APP_HOST_CHECK_ERR_TRUNCATED;
    }

    const app_host_image_header_t *header = (const app_host_image_header_t *)image;

    if (header->magic != APP_HOST_IMAGE_MAGIC) {
        return APP_HOST_CHECK_ERR_MAGIC;
    }

    if (header->header_size < APP_HOST_IMAGE_HEADER_SIZE) {
        return APP_HOST_CHECK_ERR_HEADER;
    }

    if (header->format_version != APP_HOST_IMAGE_FORMAT_VERSION) {
        return APP_HOST_CHECK_ERR_FORMAT;
    }

    if (header->abi_version != policy->abi_version) {
        return APP_HOST_CHECK_ERR_ABI;
    }

    if (header->target_addr != policy->expected_target_addr) {
        return APP_HOST_CHECK_ERR_TARGET;
    }

    if (header->image_size == 0u || header->image_size > policy->region_size) {
        return APP_HOST_CHECK_ERR_IMAGE_SIZE;
    }

    if ((uint64_t)header->header_size + (uint64_t)header->image_size > (uint64_t)file_size) {
        return APP_HOST_CHECK_ERR_IMAGE_SIZE;
    }

    if (header->entry_offset >= header->image_size || (header->entry_offset & 1u) != 0u) {
        return APP_HOST_CHECK_ERR_ENTRY;
    }

    uint32_t crc = generic_crc32(image + header->header_size, header->image_size);
    if (crc != header->crc32) {
        return APP_HOST_CHECK_ERR_CRC;
    }

    out->image_size = header->image_size;
    out->entry_offset = header->entry_offset;
    out->entry_addr = (const uint8_t *)(uintptr_t)(policy->expected_target_addr + header->entry_offset);

    return APP_HOST_CHECK_OK;
}

const char *app_host_check_str(app_host_check_t result)
{
    switch (result) {
    case APP_HOST_CHECK_OK:
        return "ok";
    case APP_HOST_CHECK_ERR_PARAM:
        return "invalid parameter";
    case APP_HOST_CHECK_ERR_TRUNCATED:
        return "image smaller than header";
    case APP_HOST_CHECK_ERR_MAGIC:
        return "bad magic";
    case APP_HOST_CHECK_ERR_HEADER:
        return "header size invalid";
    case APP_HOST_CHECK_ERR_FORMAT:
        return "format version unsupported";
    case APP_HOST_CHECK_ERR_ABI:
        return "abi version mismatch";
    case APP_HOST_CHECK_ERR_TARGET:
        return "target address mismatch";
    case APP_HOST_CHECK_ERR_IMAGE_SIZE:
        return "image size out of bounds";
    case APP_HOST_CHECK_ERR_ENTRY:
        return "entry offset invalid";
    case APP_HOST_CHECK_ERR_CRC:
        return "crc mismatch";
    default:
        return "unknown check result";
    }
}
