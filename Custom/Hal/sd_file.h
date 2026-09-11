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
    /* FileX keeps the selected directory AND the iteration cursor in the
     * media-global default path (FX_PATH), updating it in place during the
     * first/next full entry find calls. Each handle carries its own copy:
     * readdir swaps it in for the duration of every call and saves it back,
     * so concurrent iterators and path resolutions (which rebuild the global
     * default path) cannot corrupt each other's iteration. */
    FX_PATH state;
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
    uint32_t media_generation;  /* +1 on every successful fx_media_open - lets
                                 * observers detect a card swap without watching
                                 * the close window (see sd_media_generation) */
    int file_ops_handle;
    PowerHandle     pwr_handle;
    bool hs_switched;   /* CMD6 High-Speed switch applied by sd_set_speed_mode */
} sd_t;

int sd_file_ops_switch(void);
int sd_format(void);
int sd_get_disk_info(sd_disk_info_t *info);

/* ---- SD bus speed / mode diagnostics ----------------------------------- */
typedef enum {
    SD_SPEED_DEFAULT = 0,   /* Default Speed: bus clock capped to 25 MHz        */
    SD_SPEED_HIGH    = 1,   /* High Speed:    CMD6 to HS, bus up to 50 MHz      */
    SD_SPEED_AUTO    = 2,   /* auto-select best mode supported by the card      */
    SD_SPEED_OVERCLOCK = 3, /* 100MHz in Default Speed (forces DS card mode) - OUT OF SPEC, test only */
} sd_speed_mode_e;

typedef struct {
    const char *card_type;   /* "SDSC" / "SDHC/SDXC"                         */
    const char *card_speed;  /* card capability: "Normal(<=25MHz)" / "High"..*/
    uint32_t src_clk_hz;     /* SDMMC source clock (HCLK) feeding the IP     */
    uint32_t bus_clk_hz;     /* live SD_CK frequency after divider           */
    uint32_t clkdiv;         /* live CLKCR divider field                     */
    uint32_t bus_width;      /* 1 / 4 / 8                                    */
    bool hs_switched;        /* CMD6 High-Speed switch applied by us         */
} sd_speed_info_t;

/* Current mode/clock. Returns 0 on success, <0 if card not ready. */
int sd_get_speed_info(sd_speed_info_t *info);

/* Switch SD speed mode (runtime/CLI; requires media open):
 *   DEFAULT -> 25MHz, HIGH/AUTO -> 50MHz + CMD6 HS, OVERCLOCK -> ~100MHz (test).
 * Returns 0 on success. */
int sd_set_speed_mode(sd_speed_mode_e mode);

/* Sequential write+read throughput over the FileX media into a temp file.
 * total_kb/chunk_kb default to 2048/32 when 0. The chunk auto-falls back to
 * smaller sizes if the internal-SRAM (MEM_FAST) pool can't satisfy it.
 * Rates returned in KiB/s. Self-verifies the first chunk for DMA integrity.
 * Returns 0 on success. */
int sd_speed_test(uint32_t total_kb, uint32_t chunk_kb,
                  uint32_t *write_kbps, uint32_t *read_kbps);
int sd_is_detected(void);
int sd_is_media_open(void);              /* non-blocking: true once fx_media_open completed */
uint32_t sd_media_generation(void);      /* increments on every successful media open (card swap edge) */
int sd_wait_ready_for_open(uint32_t timeout_ms);
int sd_register(void);
int sd_unregister(void);
#endif