#ifndef APP_HOST_ABI_H
#define APP_HOST_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_HOST_ABI_VERSION 0x00010000u
#define APP_HOST_ABI_VERSION_MAJOR(v) (((v) >> 16u) & 0xFFu)
#define APP_HOST_ABI_VERSION_MINOR(v) ((v) & 0xFFu)

#define APP_HOST_IMAGE_MAGIC 0x3141454Eu
#define APP_HOST_IMAGE_FORMAT_VERSION 1u
#define APP_HOST_IMAGE_HEADER_SIZE 32u

#define APP_HOST_ENTRY_THUMB_MASK 0x1u

typedef struct {
    uint32_t magic;
    uint16_t header_size;
    uint16_t format_version;
    uint32_t abi_version;
    uint32_t target_addr;
    uint32_t image_size;
    uint32_t entry_offset;
    uint32_t reserved0;
    uint32_t crc32;
} app_host_image_header_t;

_Static_assert(sizeof(app_host_image_header_t) == APP_HOST_IMAGE_HEADER_SIZE, "app_host_image_header_t layout");

typedef int (*app_host_fn_log_t)(const char *text);
typedef int (*app_host_fn_tick_ms_t)(void);

typedef struct {
    uint32_t table_size;
    uint32_t abi_version;
    app_host_fn_log_t log;
    app_host_fn_tick_ms_t tick_ms;
} app_host_api_table_t;

typedef int (*app_host_entry_fn_t)(const app_host_api_table_t *api, uint32_t abi_version);

#ifdef __cplusplus
}
#endif

#endif
