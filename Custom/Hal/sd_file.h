#ifndef _SD_H_
#define _SD_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "generic_file.h"
#include "pwr.h"
#include "fx_stm32_sd_driver.h"

#define TF_INT_Pin GPIO_PIN_0
#define TF_INT_GPIO_Port GPIOD

#define MEDIA_CLOSED                     1UL
#define MEDIA_OPENED                     0UL

#define SD_DEBOUNCE_CHECKS 5
#define SD_DEBOUNCE_DELAY_MS 1

#define FX_SD_VOLUME_NAME "SD_DISK"

#define SD_TYPE_REG 0
#define SD_TYPE_DIR 1

typedef struct {
    FX_MEDIA *media;
    CHAR saved_dir[FX_MAX_LONG_NAME_LEN];  // saved default dir, restored on close
    CHAR path[FX_MAX_LONG_NAME_LEN];
    CHAR entry_name[FX_MAX_LONG_NAME_LEN];
    UINT attributes;
    ULONG size;
    UINT year, month, day, hour, minute, second;
    UINT first_entry;
    UINT finished;
} filex_dir_t;

struct sd_info {
    uint8_t type; // 0: file, 1: dir
    uint32_t size;
    char name[FX_MAX_LONG_NAME_LEN];
    uint32_t mtime; // modification time (Unix timestamp), at offset 264
    char short_name[14];  // 8.3 short name, filled by readdir
};

typedef enum {
    SD_MODE_UNPLUG = 0,
    SD_MODE_UNKNOWN,
    SD_MODE_NORMAL,           
    SD_MODE_FORMATING,
} sd_mode_e;

typedef struct {
    sd_mode_e mode;
    uint32_t total_KBytes;
    uint32_t free_KBytes;
    char fs_type[8]; // "exFAT" or "FAT32"
} sd_disk_info_t;

typedef struct {
    bool is_init;
    sd_mode_e mode;
    device_t *dev;
    osMutexId_t mtx_id;
    osSemaphoreId_t sem_id;
    osThreadId_t sd_processId;
    FX_MEDIA        sdio_disk;
    uint32_t media_status;
    int file_ops_handle;
    PowerHandle     pwr_handle;
} sd_t;

int sd_file_ops_switch(void);
int sd_format(void);
int sd_get_disk_info(sd_disk_info_t *info);
int sd_is_detected(void);
int sd_is_media_open(void);              /* non-blocking: true once fx_media_open completed */
int sd_wait_ready_for_open(uint32_t timeout_ms);
int sd_register(void);
int sd_unregister(void);
#endif