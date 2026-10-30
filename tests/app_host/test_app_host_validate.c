#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "app_host_abi.h"
#include "app_host_validate.h"
#include "generic_math.h"

#define POLICY_TARGET 0x93E00000u
#define POLICY_REGION (2u * 1024u * 1024u)
#define PAYLOAD_SIZE 64u

static int g_checks;
static int g_failures;

static void expect(int cond, const char *name)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL %s\n", name);
    }
}

static size_t build_image(uint8_t *buf, size_t buf_len, uint32_t target, uint32_t abi, uint32_t entry_off, uint32_t declared_size, uint32_t payload_len, uint16_t fmt, uint16_t hdr_size, uint32_t magic, int corrupt_crc)
{
    app_host_image_header_t *h = (app_host_image_header_t *)buf;
    memset(buf, 0, buf_len);
    for (uint32_t i = 0; i < payload_len; i++) {
        buf[hdr_size + i] = (uint8_t)(i * 7u + 1u);
    }
    h->magic = magic;
    h->header_size = hdr_size;
    h->format_version = fmt;
    h->abi_version = abi;
    h->target_addr = target;
    h->image_size = declared_size;
    h->entry_offset = entry_off;
    h->crc32 = generic_crc32(buf + hdr_size, payload_len);
    if (corrupt_crc) {
        h->crc32 ^= 0xA5A5A5A5u;
    }
    return (size_t)hdr_size + payload_len;
}

int main(void)
{
    app_host_load_policy_t policy;
    policy.expected_target_addr = POLICY_TARGET;
    policy.region_size = POLICY_REGION;
    policy.abi_version = APP_HOST_ABI_VERSION;

    uint8_t buf[512];
    app_host_image_info_t info;
    app_host_check_t rc;
    size_t file_len;

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION, 0, PAYLOAD_SIZE, PAYLOAD_SIZE, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 0);
    memset(&info, 0, sizeof(info));
    rc = app_host_validate(buf, file_len, &policy, &info);
    expect(rc == APP_HOST_CHECK_OK, "valid_image_ok");
    expect(info.image_size == PAYLOAD_SIZE, "valid_image_size");
    expect(info.entry_addr == (const uint8_t *)(uintptr_t)(POLICY_TARGET + 0), "valid_entry_addr");

    memset(&info, 0, sizeof(info));
    rc = app_host_validate(buf, (size_t)sizeof(app_host_image_header_t) - 1, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_TRUNCATED, "file_shorter_than_min_header");

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION, 0, PAYLOAD_SIZE, PAYLOAD_SIZE, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 0);
    rc = app_host_validate(buf, file_len, &policy, &info);
    ((app_host_image_header_t *)buf)->magic ^= 1u;
    rc = app_host_validate(buf, file_len, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_MAGIC, "bad_magic");

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION, 0, PAYLOAD_SIZE, PAYLOAD_SIZE, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)(sizeof(app_host_image_header_t) - 1), APP_HOST_IMAGE_MAGIC, 0);
    rc = app_host_validate(buf, file_len, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_HEADER, "header_size_below_minimum");

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION, 0, PAYLOAD_SIZE, PAYLOAD_SIZE, (uint16_t)(APP_HOST_IMAGE_FORMAT_VERSION + 1u), (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 0);
    rc = app_host_validate(buf, file_len, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_FORMAT, "future_format_version");

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION + 1u, 0, PAYLOAD_SIZE, PAYLOAD_SIZE, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 0);
    rc = app_host_validate(buf, file_len, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_ABI, "wrong_abi_version");

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET + 0x1000u, APP_HOST_ABI_VERSION, 0, PAYLOAD_SIZE, PAYLOAD_SIZE, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 0);
    rc = app_host_validate(buf, file_len, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_TARGET, "wrong_target_addr");

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION, 0, 0, 0, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 0);
    rc = app_host_validate(buf, file_len, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_IMAGE_SIZE, "zero_image_size");

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION, 0, POLICY_REGION + 1u, PAYLOAD_SIZE, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 0);
    rc = app_host_validate(buf, file_len, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_IMAGE_SIZE, "image_size_exceeds_region");

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION, 0, PAYLOAD_SIZE + 1u, PAYLOAD_SIZE, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 0);
    rc = app_host_validate(buf, file_len - 1u, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_IMAGE_SIZE, "declared_size_beyond_file");

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION, PAYLOAD_SIZE + 4u, PAYLOAD_SIZE, PAYLOAD_SIZE, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 0);
    rc = app_host_validate(buf, file_len, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_ENTRY, "entry_offset_beyond_image");

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION, 3, PAYLOAD_SIZE, PAYLOAD_SIZE, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 0);
    rc = app_host_validate(buf, file_len, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_ENTRY, "entry_offset_unaligned");

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION, 0, PAYLOAD_SIZE, PAYLOAD_SIZE, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 1);
    rc = app_host_validate(buf, file_len, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_CRC, "crc_mismatch");

    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION, 16, PAYLOAD_SIZE, PAYLOAD_SIZE, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 0);
    memset(&info, 0, sizeof(info));
    rc = app_host_validate(buf, file_len, &policy, &info);
    expect(rc == APP_HOST_CHECK_OK, "entry_offset_mid_payload_ok");
    expect(info.entry_addr == (const uint8_t *)(uintptr_t)(POLICY_TARGET + 16), "entry_addr_uses_offset");

    rc = app_host_validate(NULL, 0, &policy, &info);
    expect(rc == APP_HOST_CHECK_ERR_PARAM, "null_image");
    file_len = build_image(buf, sizeof(buf), POLICY_TARGET, APP_HOST_ABI_VERSION, 0, PAYLOAD_SIZE, PAYLOAD_SIZE, APP_HOST_IMAGE_FORMAT_VERSION, (uint16_t)sizeof(app_host_image_header_t), APP_HOST_IMAGE_MAGIC, 0);
    rc = app_host_validate(buf, file_len, NULL, &info);
    expect(rc == APP_HOST_CHECK_ERR_PARAM, "null_policy");

    expect(app_host_check_str(APP_HOST_CHECK_OK) != NULL, "check_str_ok_nonnull");
    expect(app_host_check_str(APP_HOST_CHECK_ERR_CRC) != NULL, "check_str_err_nonnull");

    printf("app_host_validate: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
