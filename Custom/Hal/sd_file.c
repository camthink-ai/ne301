#include "sd_file.h"
#include "debug.h"
#include "sdmmc.h"
#include "exti.h"
#include "common_utils.h"
#include "drtc.h"
#include "mem_map.h"

#define SD_CHUNK_SIZE           (32 * 1024)
#define SD_CHUNK_ALIGN          32
#define SD_CHUNK_TYPE           MEM_FAST

extern SD_HandleTypeDef hsd1;
static sd_t g_sd = {0};
static uint8_t sd_tread_stack[1024 * 4] ALIGN_32 IN_PSRAM;
const osThreadAttr_t sdTask_attributes = {
    .name = "sdTask",
    .priority = (osPriority_t) osPriorityLow,
    .stack_mem = sd_tread_stack,
    .stack_size = sizeof(sd_tread_stack),
};
uint32_t fx_sd_media_memory[FX_STM32_SD_DEFAULT_SECTOR_SIZE / sizeof(uint32_t)] ALIGN_32;

osSemaphoreId_t sd_sem_tx;
osSemaphoreId_t sd_sem_rx;


/* USER CODE BEGIN 0 */
static int sd_apply_speed_mode_locked(sd_speed_mode_e mode);   /* core: no media-open gate */
/* USER CODE END 0 */

/**
* @brief Initializes the SD IP instance
* @param unsigned int instance SD instance to initialize
* @retval 0 on success error value otherwise
*/
INT fx_stm32_sd_init(unsigned int instance)
{
    INT ret = 0;
    int retry = 0;

    /* USER CODE BEGIN PRE_FX_SD_INIT */
    UNUSED(instance);
    /* USER CODE END PRE_FX_SD_INIT */

#if (FX_STM32_SD_INIT == 1)
    // LOG_DRV_DEBUG("fx_stm32_sd_init \r\n");
    for (retry = 0; retry < 3; retry++) {
        ret = MX_SDMMC1_SD_Init();
        if (ret == 0) {
            break;
        }
        LOG_DRV_ERROR("MX_SDMMC1_SD_Init failed, retry=%d\r\n", retry + 1);
        HAL_SD_DeInit(&hsd1);
        osDelay(100);
    }

#endif

    /* USER CODE BEGIN POST_FX_SD_INIT */
#if (FX_STM32_SD_INIT == 1)
    /* Boot default: spec-compliant 50MHz High Speed. HAL_SD_Init leaves
     * UHS-grade cards at CLKDIV=0 (~100MHz, out of spec) in Default Speed;
     * normalize to HS. `sdswitch overclock` re-enters 100MHz for testing.
     * Runs on every (re)insert: fx_media_close -> UNINIT clears the driver's
     * is_initialized flag, so the next fx_media_open re-inits and re-applies. */
    if (ret == 0) {
        if (sd_apply_speed_mode_locked(SD_SPEED_HIGH) == 0) {
            LOG_DRV_DEBUG("SD init: boot mode set to 50MHz HS\r\n");
        }
    }
#endif
    /* USER CODE END POST_FX_SD_INIT */

    return ret;
}

/**
* @brief Deinitializes the SD IP instance
* @param unsigned int instance SD instance to deinitialize
* @retval 0 on success error value otherwise
*/
INT fx_stm32_sd_deinit(unsigned int instance)
{
    INT ret = 0;

    UNUSED(instance);

#if (FX_STM32_SD_INIT == 1)
    // LOG_DRV_DEBUG("fx_stm32_sd_deinit \r\n");
    if(HAL_SD_DeInit(&hsd1) != HAL_OK)
    {
        ret = 1;
    }
#endif

    return ret;
}

/**
* @brief Check the SD IP status.
* @param unsigned int instance SD instance to check
* @retval 0 when ready 1 when busy
*/
INT fx_stm32_sd_get_status(unsigned int instance)
{
    INT ret = 0;

    UNUSED(instance);

    if(g_sd.media_status == MEDIA_CLOSED){
        return 0;
    }
    if(HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER)
    {
        ret = 1;
    }


    return ret;
}

/**
* @brief Read Data from the SD device into a buffer.
* @param unsigned int instance SD IP instance to read from.
* @param unsigned int *buffer buffer into which the data is to be read.
* @param unsigned int start_block the first block to start reading from.
* @param unsigned int total_blocks total number of blocks to read.
* @retval 0 on success error code otherwise
*/
INT fx_stm32_sd_read_blocks(unsigned int instance, unsigned int *buffer, unsigned int start_block, unsigned int total_blocks)
{
    INT ret = 0;

    UNUSED(instance);

    if(HAL_SD_ReadBlocks_DMA(&hsd1, (uint8_t *)buffer, start_block, total_blocks) != HAL_OK)
    {
        ret = 1;
    }
    if(ret != 0){
        LOG_DRV_ERROR("sd read: start_block=%lu, total_blocks=%lu\r\n", start_block, total_blocks);
    }
    return ret;
}

/**
* @brief Write data buffer into the SD device.
* @param unsigned int instance SD IP instance to write into.
* @param unsigned int *buffer buffer to write into the SD device.
* @param unsigned int start_block the first block to start writing into.
* @param unsigned int total_blocks total number of blocks to write.
* @retval 0 on success error code otherwise
*/
INT fx_stm32_sd_write_blocks(unsigned int instance, unsigned int *buffer, unsigned int start_block, unsigned int total_blocks)
{
    UNUSED(instance);
    int retry = 0;
    INT ret = 0;

    for (retry = 0; retry < 3; retry++) {
        ret = HAL_SD_WriteBlocks_DMA(&hsd1, (uint8_t *)buffer, start_block, total_blocks);
        if (ret == HAL_OK) {
            return 0;
        }
        LOG_DRV_ERROR("sd write: start_block=%lu, total_blocks=%lu ErrorCode=%d, retry=%d\r\n", 
               start_block, total_blocks, hsd1.ErrorCode, retry + 1);
        osDelay(1);
    }
    return 1;
}

/**
* @brief SD DMA Tx Transfer completed callbacks
* @param Instance the sd instance
* @retval None
*/
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
    osSemaphoreRelease(sd_sem_tx);
}

/**
* @brief SD DMA Rx Transfer completed callbacks
* @param Instance the sd instance
* @retval None
*/
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    osSemaphoreRelease(sd_sem_rx);
}

/**
* @brief SD error callbacks
* @param hsd: Pointer to SD handle
* @retval None
*/
void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    uint32_t error_code = HAL_SD_GetError(hsd);
    printf("HAL_SD_ErrorCallback: ErrorCode=0x%08lX\r\n", error_code);
}

void sd_lock(void)
{
    osMutexAcquire(g_sd.mtx_id, osWaitForever);
}

void sd_unlock(void)
{
    osMutexRelease(g_sd.mtx_id);
}

/**
 * @brief  Detects if SD card is correctly plugged in the memory slot or not.
 * @param Instance  SD Instance
 * @retval Returns if SD is detected or not
 */
static bool SD_IsDetected(void)
{
    /* Check SD card detect pin */
    uint8_t stable = 0;
    for(int i=0; i < SD_DEBOUNCE_CHECKS; i++)
    {
        if(HAL_GPIO_ReadPin(TF_INT_GPIO_Port, TF_INT_Pin) == GPIO_PIN_RESET)
            stable++;
        osDelay(SD_DEBOUNCE_DELAY_MS);
    }
    return (stable == SD_DEBOUNCE_CHECKS);

}

static void sd_gpio_interrupt(void)
{
    __HAL_GPIO_EXTI_CLEAR_IT(TF_INT_Pin);
    // printf("sd_gpio_interrupt\r\n");
    osSemaphoreRelease(g_sd.sem_id);
}

static void media_close_callback(FX_MEDIA *media_ptr)
{
    g_sd.media_status = MEDIA_CLOSED;
    osSemaphoreRelease(g_sd.sem_id);
}

/* Forward declaration */
static int sd_resolve_dir_path(FX_MEDIA *media, const char *path, char *fx_out, size_t out_sz);
static int sd_resolve_file_path(FX_MEDIA *media, const char *path, char *fx_out, size_t out_sz);
static bool sd_str_is_ascii(const char *s);
static bool sd_media_is_exfat(FX_MEDIA *media);
static bool sd_name_needs_unicode(FX_MEDIA *media, const char *name);
static int sd_utf8_to_utf16(const char *utf8, UCHAR *u16, size_t u16_max);
static int sd_utf16_to_utf8(const UCHAR *u16, ULONG u16_len, char *out, size_t out_sz);
static const char *sd_split_path(const char *path, char *parent, size_t parent_sz);
static int sd_fx_join(const char *parent, const char *name, char *out, size_t out_sz);

void* sd_filex_fopen(void *context, const char *path, const char *mode)
{
    FX_MEDIA *media = (FX_MEDIA*)context;
    FX_FILE *file = hal_mem_alloc_fast(sizeof(FX_FILE));
    UINT status;

    if (!file) {
        LOG_DRV_ERROR("sd_filex_fopen: FX_FILE alloc failed path=%s\r\n", path);
        return NULL;
    }
    if (!media || media->fx_media_id != FX_MEDIA_ID) {
        LOG_DRV_ERROR("sd_filex_fopen: SD media not open (id=0x%08lX) path=%s\r\n",
                      media ? (unsigned long)media->fx_media_id : 0UL, path);
        hal_mem_free(file);
        return NULL;
    }

    int reading = 0, writing = 0, appending = 0, plus = 0;

    // Parse mode
    if (mode[0] == 'r') reading = 1;
    else if (mode[0] == 'w') writing = 1;
    else if (mode[0] == 'a') { writing = 1; appending = 1; }

    if (strchr(mode, '+')) plus = 1;

    ULONG open_mode = 0;

    /* Non-ASCII (e.g. Chinese) filenames must go through FileX's unicode
     * API family: the CHAR* APIs zero-extend every UTF-8 byte into a UTF-16
     * code unit, so names they create render as mojibake on a PC. The
     * unicode APIs write a genuine UTF-16 LFN (plus a numeric 8.3 alias)
     * and address bare names inside the current default directory, so the
     * parent is resolved and selected first. */
    char parent[FX_MAX_LONG_NAME_LEN];
    const char *fname = sd_split_path(path, parent, sizeof(parent));
    int name_unicode = sd_name_needs_unicode(media, fname);
    UCHAR u16[FX_MAX_LONG_NAME_LEN * 2];
    int u16n = 0;
    CHAR u16_short[FX_MAX_SHORT_NAME_LEN + 1] = {0};
    int fresh = 0;   /* entry just (re)created via the unicode API */

    char fx_path[FX_MAX_LONG_NAME_LEN];
    sd_lock();
    if (name_unicode) {
        char fx_parent[FX_MAX_LONG_NAME_LEN];
        u16n = sd_utf8_to_utf16(fname, u16, FX_MAX_LONG_NAME_LEN);
        if (u16n <= 0 ||
            sd_resolve_dir_path(media, (parent[0] == '\0') ? "/" : parent,
                                fx_parent, sizeof(fx_parent)) != 0 ||
            fx_directory_default_set(media, fx_parent) != FX_SUCCESS) {
            sd_unlock();
            LOG_DRV_ERROR("sd_filex_fopen: resolve failed path=%s\r\n", path);
            hal_mem_free(file);
            return NULL;
        }
        UINT ls = fx_unicode_short_name_get_extended(media, u16, (ULONG)u16n,
                                                     u16_short, FX_MAX_SHORT_NAME_LEN);
        if (ls == FX_SUCCESS && writing && !appending) {
            /* "w"/"w+" truncate: delete + recreate keeps a valid LFN (a plain
             * in-place truncate would strand the old LFN entries). */
            char tmp[FX_MAX_LONG_NAME_LEN];
            if (sd_fx_join(fx_parent, u16_short, tmp, sizeof(tmp)) == 0)
                fx_file_delete(media, tmp);
            ls = FX_NOT_FOUND;
        }
        if (ls != FX_SUCCESS) {
            if (reading) {
                sd_unlock();
                hal_mem_free(file);
                return NULL;
            }
            ls = fx_unicode_file_create(media, u16, (ULONG)u16n, u16_short);
            if (ls != FX_SUCCESS) {
                sd_unlock();
                LOG_DRV_ERROR("fx_unicode_file_create failed: 0x%02X path=%s\r\n", ls, path);
                hal_mem_free(file);
                return NULL;
            }
            fresh = 1;
        }
        if (sd_fx_join(fx_parent, u16_short, fx_path, sizeof(fx_path)) != 0) {
            sd_unlock();
            hal_mem_free(file);
            return NULL;
        }
    } else if (sd_resolve_file_path(media, path, fx_path, sizeof(fx_path)) != 0) {
        /* Resolve UTF-8 path for FileX under the lock: resolution flips the
         * media-global default directory and must not interleave with a
         * concurrent directory scan. */
        sd_unlock();
        hal_mem_free(file);
        return NULL;
    }

    if (reading && !plus) { // "r"
        open_mode = FX_OPEN_FOR_READ;
        status = fx_file_open(media, file, fx_path, open_mode);
    } else if (reading && plus) { // "r+"
        open_mode = FX_OPEN_FOR_READ | FX_OPEN_FOR_WRITE;
        status = fx_file_open(media, file, fx_path, open_mode);
    } else if (writing && !appending && !plus) { // "w"
        if (!fresh) {
            fx_file_delete(media, fx_path);
            UINT create_status = fx_file_create(media, fx_path);
            if (create_status != FX_SUCCESS && create_status != FX_ALREADY_CREATED) {
                sd_unlock();
                LOG_DRV_ERROR("fx_file_create failed: 0x%02X path=%s\r\n", create_status, path);
                hal_mem_free(file);
                return NULL;
            }
        }
        open_mode = FX_OPEN_FOR_WRITE;
        status = fx_file_open(media, file, fx_path, open_mode);
    } else if (writing && !appending && plus) { // "w+"
        if (!fresh) {
            fx_file_delete(media, fx_path);
            fx_file_create(media, fx_path);
        }
        open_mode = FX_OPEN_FOR_READ | FX_OPEN_FOR_WRITE;
        status = fx_file_open(media, file, fx_path, open_mode);
    } else if (appending && !plus) { // "a"
        status = fx_file_open(media, file, fx_path, FX_OPEN_FOR_WRITE);
        if (status != FX_SUCCESS) {
            /* File doesn't exist - create then open. Only re-open if the first
             * open FAILED; if it succeeded the FX_FILE is already open and a
             * second fx_file_open on it errors out (the "first OK, second
             * fails" bug). */
            if (name_unicode)
                fx_unicode_file_create(media, u16, (ULONG)u16n, u16_short);
            else
                fx_file_create(media, fx_path);
            status = fx_file_open(media, file, fx_path, FX_OPEN_FOR_WRITE);
        }
        if (status == FX_SUCCESS) {
            ULONG file_size = file->fx_file_current_file_size;
            fx_file_seek(file, file_size);
        }
    } else if (appending && plus) { // "a+"
        status = fx_file_open(media, file, fx_path, FX_OPEN_FOR_READ | FX_OPEN_FOR_WRITE);
        if (status != FX_SUCCESS) {
            if (name_unicode)
                fx_unicode_file_create(media, u16, (ULONG)u16n, u16_short);
            else
                fx_file_create(media, fx_path);
            status = fx_file_open(media, file, fx_path, FX_OPEN_FOR_READ | FX_OPEN_FOR_WRITE);
        }
        if (status == FX_SUCCESS) {
            ULONG file_size = file->fx_file_current_file_size;
            fx_file_seek(file, file_size);
        }
    } else {
        sd_unlock();
        hal_mem_free(file);
        return NULL;
    }
    sd_unlock();

    if (status != FX_SUCCESS) {
        LOG_DRV_ERROR("fx_file_open failed: 0x%02X path=%s\r\n", status, path);
        hal_mem_free(file);
        return NULL;
    }
    return file;
}


int sd_filex_fclose(void *context, void *fd)
{
    FX_FILE *file = (FX_FILE*)fd;
    UINT status;

    sd_lock();
    status = fx_file_close(file);
    sd_unlock();
    if (status != FX_SUCCESS) {
        LOG_DRV_ERROR("fx_file_close failed: 0x%02X\r\n", status);
        hal_mem_free(file);
        // FileX returns UINT (unsigned int), so status is always >= 0
        return -(int)status;
    }

    hal_mem_free(file);
    return 0;
}

// Write file
int sd_filex_fwrite(void *context, void *fd, const void *buf, size_t size)
{
    FX_MEDIA *media = (FX_MEDIA*)context;
    FX_FILE *file = (FX_FILE*)fd;
    void *buf_aligned = NULL;
    UINT status;

    sd_lock();
    if ((uint32_t)buf < SRAM_POOL_BASE || (uint32_t)buf >= SRAM_AI_BASE) {
        buf_aligned = hal_mem_alloc_aligned(SD_CHUNK_SIZE, SD_CHUNK_ALIGN, SD_CHUNK_TYPE);
        if (buf_aligned == NULL) {
            sd_unlock();
            LOG_DRV_ERROR("sd_filex_fwrite: cannot malloc buf_aligned\r\n");
            return -1;
        }
        // cycle write
        for (size_t i = 0; i < size; i += SD_CHUNK_SIZE) {
            size_t chunk_size = SD_CHUNK_SIZE;
            if (size - i < SD_CHUNK_SIZE) {
                chunk_size = size - i;
            }
            memcpy(buf_aligned, (void *)buf + i, chunk_size);
            status = fx_file_write(file, (void *)buf_aligned, chunk_size);
            if (status != FX_SUCCESS) {
                hal_mem_free(buf_aligned);
                sd_unlock();
                LOG_DRV_ERROR("fx_file_write failed: 0x%02X\r\n", status);
                return -(int)status;
            }
        }
        hal_mem_free(buf_aligned);
    } else {
        status = fx_file_write(file, (void*)buf, size);
        if (status != FX_SUCCESS) {
            sd_unlock();
            LOG_DRV_ERROR("fx_file_write failed: 0x%02X\r\n", status);
            return -(int)status;
        }
    }

    status = fx_media_flush(media);
    sd_unlock();
    if (status != FX_SUCCESS) {
        LOG_DRV_ERROR("fx_media_flush failed: 0x%02X\r\n", status);
        return -(int)status;
    }

    return size;
}

// Read file
static uint8_t sd_read_buf[SD_CHUNK_SIZE] ALIGN_32 UNCACHED;
int sd_filex_fread(void *context, void *fd, void *buf, size_t size)
{
    FX_FILE *file = (FX_FILE*)fd;
    unsigned long actual = 0;
    int actual_total = 0;
    UINT status = 0;

    sd_lock();
    if ((uint32_t)buf < SRAM_POOL_BASE || (uint32_t)buf >= SRAM_AI_BASE) {
        // cycle read using the shared aligned buffer (protected by sd_lock)
        while (actual_total < size) {
            size_t chunk_size = SD_CHUNK_SIZE;
            if (size - actual_total < SD_CHUNK_SIZE) {
                chunk_size = size - actual_total;
            }
            actual = 0;
            status = fx_file_read(file, sd_read_buf, chunk_size, &actual);
            if (status == FX_END_OF_FILE) {
                memcpy((void *)buf + actual_total, sd_read_buf, actual);
                actual_total += actual;
                break;
            }
            if (status != FX_SUCCESS) {
                sd_unlock();
                LOG_DRV_ERROR("fx_file_read failed: 0x%02X\r\n", status);
                return -(int)status;
            }
            memcpy((void *)buf + actual_total, sd_read_buf, actual);
            actual_total += actual;
            if (actual != chunk_size) break;
        }
        sd_unlock();
        return (int)actual_total;
    } else {
        status = fx_file_read(file, buf, size, &actual);
        sd_unlock();
        if (status == FX_END_OF_FILE) {
            return (int)actual;
        }
        if (status != FX_SUCCESS) {
            LOG_DRV_ERROR("fx_file_read failed: 0x%02X\r\n", status);
            return -(int)status;
        }
    }

    return (int)actual;
}

// Delete file
int sd_filex_remove(void *context, const char *path)
{
    FX_MEDIA *media = (FX_MEDIA*)context;
    char fx_path[FX_MAX_LONG_NAME_LEN];
    sd_lock();
    if (sd_resolve_file_path(media, path, fx_path, sizeof(fx_path)) != 0) {
        sd_unlock();
        return -1;
    }
    UINT status = fx_file_delete(media, fx_path);
    if (status == FX_NOT_A_FILE) {
        /* fx_file_delete cannot remove a directory (returns FX_NOT_A_FILE).
         * Retry with fx_directory_delete. Without this, any directory removal
         * on SD — e.g. upload_coordinator's best-effort empty hour/date dir
         * cleanup, and recursive folder delete — failed with 0x05 every time. */
        status = fx_directory_delete(media, fx_path);
    }
    if (status == FX_SUCCESS) {
        UINT fs = fx_media_flush(media);
        if (fs != FX_SUCCESS) LOG_DRV_ERROR("fx_media_flush after delete failed: 0x%02X\r\n", fs);
    }
    sd_unlock();
    if (status != FX_SUCCESS) {
        /* FX_DIR_NOT_EMPTY (0x10) is the expected outcome of best-effort
         * empty-dir cleanup (e.g. deleting a record whose hour dir still holds
         * other records) — not an error. Stay silent like flash's lfs_remove,
         * which also returns failure for a non-empty dir without logging. */
        if (status != FX_DIR_NOT_EMPTY) {
            LOG_DRV_ERROR("fx delete failed: 0x%02X path=%s\r\n", status, path);
        }
        return -(int)status;
    }
    return 0;
}

// Case-insensitive path compare: FAT names are case-insensitive and both
// arguments are resolved absolute fx paths, so an equal pair means FileX will
// treat the rename as a case-only change of the same entry.
static int sd_path_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Rename over plain single-byte paths. sd_lock must be held. Destination is
 * parked under ~RNMBAK.TMP and restored if the swap fails. */
static int sd_rename_plain_locked(FX_MEDIA *media, const char *oldpath, const char *newpath)
{
    char fx_old[FX_MAX_LONG_NAME_LEN], fx_new[FX_MAX_LONG_NAME_LEN];
    if (sd_resolve_file_path(media, oldpath, fx_old, sizeof(fx_old)) != 0) return -1;
    if (sd_resolve_file_path(media, newpath, fx_new, sizeof(fx_new)) != 0) return -1;

    UINT attr;
    ULONG size;
    UINT year, month, day, hour, minute, second;

    /* Source must exist; pick the FileX call matching its type (fx_file_rename
     * rejects directories with FX_NOT_A_FILE). */
    UINT status = fx_directory_information_get(
        media, fx_old, &attr, &size,
        &year, &month, &day, &hour, &minute, &second);
    if (status != FX_SUCCESS) {
        LOG_DRV_ERROR("rename: source not found: 0x%02X path=%s\r\n", status, oldpath);
        return -1;
    }
    int src_is_dir = (attr & FX_DIRECTORY) ? 1 : 0;

    /* Park an existing destination aside so it can be restored if the swap
     * fails. A case-only rename of the same entry is skipped: FileX handles
     * it directly and the destination IS the source. */
    char fx_bak[FX_MAX_LONG_NAME_LEN];
    int have_backup = 0, bak_is_dir = 0;
    if (!sd_path_ieq(fx_old, fx_new)) {
        status = fx_directory_information_get(
            media, fx_new, &attr, &size,
            &year, &month, &day, &hour, &minute, &second);
        if (status == FX_SUCCESS) {
            bak_is_dir = (attr & FX_DIRECTORY) ? 1 : 0;
            if (bak_is_dir != src_is_dir) {
                /* Replacing a file with a dir (or vice versa) was never
                 * possible before either - refuse without touching anything */
                LOG_DRV_ERROR("rename: destination exists with different type: %s\r\n", newpath);
                return -1;
            }
            /* Backup name next to the destination, 8.3-safe so it never
             * needs a long-name entry */
            const char *sep = strrchr(fx_new, '\\');
            size_t plen = sep ? (size_t)(sep - fx_new + 1) : 0;
            if (plen + sizeof("~RNMBAK.TMP") > sizeof(fx_bak)) {
                LOG_DRV_ERROR("rename: destination path too long for backup\r\n");
                return -1;
            }
            memcpy(fx_bak, fx_new, plen);
            strcpy(fx_bak + plen, "~RNMBAK.TMP");
            /* Drop a stale backup left by an earlier interrupted rename */
            if (bak_is_dir) (void)fx_directory_delete(media, fx_bak);
            else            (void)fx_file_delete(media, fx_bak);
            status = bak_is_dir ? fx_directory_rename(media, fx_new, fx_bak)
                                : fx_file_rename(media, fx_new, fx_bak);
            if (status != FX_SUCCESS) {
                /* Could not park - destination stays untouched, just fail */
                LOG_DRV_ERROR("rename: parking destination failed: 0x%02X path=%s\r\n", status, newpath);
                return -1;
            }
            have_backup = 1;
        }
    }

    status = src_is_dir ? fx_directory_rename(media, fx_old, fx_new)
                        : fx_file_rename(media, fx_old, fx_new);
    if (status == FX_SUCCESS) {
        if (have_backup) {
            UINT ds = bak_is_dir ? fx_directory_delete(media, fx_bak)
                                 : fx_file_delete(media, fx_bak);
            if (ds != FX_SUCCESS)
                LOG_DRV_ERROR("rename: replaced destination backup left at %s (0x%02X)\r\n", fx_bak, ds);
        }
        UINT fs = fx_media_flush(media);
        if (fs != FX_SUCCESS) LOG_DRV_ERROR("fx_media_flush after rename failed: 0x%02X\r\n", fs);
    } else {
        LOG_DRV_ERROR("fx rename failed: 0x%02X, old=%s, new=%s\r\n", status, oldpath, newpath);
        if (have_backup) {
            /* Put the original destination back */
            UINT rs = bak_is_dir ? fx_directory_rename(media, fx_bak, fx_new)
                                 : fx_file_rename(media, fx_bak, fx_new);
            if (rs != FX_SUCCESS) {
                LOG_DRV_ERROR("rename: restoring destination failed: old copy left at %s (0x%02X)\r\n", fx_bak, rs);
            } else {
                (void)fx_media_flush(media);
            }
        }
    }
    return (status == FX_SUCCESS) ? 0 : -1;
}

// Rename file or directory. FileX rename never overwrites an existing target
// (FX_ALREADY_CREATED), and the destination must survive a failed swap (e.g.
// upload_coordinator renames tmp metadata over the committed manifest): an
// existing destination is parked under a backup name and restored if the
// rename fails, instead of being deleted up front.
//
// Non-ASCII names go through FileX's unicode API family: CHAR* renames
// zero-extend UTF-8 bytes into LFN code units (mojibake on a PC), while the
// unicode variants write real UTF-16 LFNs but only address bare names inside
// the current default directory - hence the parent-resolve + default-set
// dance below.
int sd_filex_rename(void *context, const char *oldpath, const char *newpath)
{
    FX_MEDIA *media = (FX_MEDIA*)context;

    /* Plain byte-oriented flow for pure-ASCII paths, and for any path on
     * exFAT media (fx_unicode_* rejects exFAT - see sd_media_is_exfat). */
    if ((sd_str_is_ascii(oldpath) && sd_str_is_ascii(newpath)) ||
        sd_media_is_exfat(media)) {
        sd_lock();
        int r = sd_rename_plain_locked(media, oldpath, newpath);
        sd_unlock();
        return r;
    }

    char old_parent[FX_MAX_LONG_NAME_LEN], new_parent[FX_MAX_LONG_NAME_LEN];
    const char *old_name = sd_split_path(oldpath, old_parent, sizeof(old_parent));
    const char *new_name = sd_split_path(newpath, new_parent, sizeof(new_parent));

    UCHAR u16_old[FX_MAX_LONG_NAME_LEN * 2], u16_new[FX_MAX_LONG_NAME_LEN * 2];
    UCHAR u16_bak[FX_MAX_LONG_NAME_LEN * 2];
    int n_old = sd_utf8_to_utf16(old_name, u16_old, FX_MAX_LONG_NAME_LEN);
    int n_new = sd_utf8_to_utf16(new_name, u16_new, FX_MAX_LONG_NAME_LEN);
    int n_bak = sd_utf8_to_utf16("~RNMBAK.TMP", u16_bak, FX_MAX_LONG_NAME_LEN);
    if (n_old <= 0 || n_new <= 0 || n_bak <= 0) return -1;

    sd_lock();

    char P1[FX_MAX_LONG_NAME_LEN], P2[FX_MAX_LONG_NAME_LEN];
    if (sd_resolve_dir_path(media, (old_parent[0] == '\0') ? "/" : old_parent, P1, sizeof(P1)) != 0 ||
        sd_resolve_dir_path(media, (new_parent[0] == '\0') ? "/" : new_parent, P2, sizeof(P2)) != 0) {
        sd_unlock();
        return -1;
    }

    /* Source must exist; map it to its 8.3 short name and learn its type */
    if (fx_directory_default_set(media, P1) != FX_SUCCESS) {
        sd_unlock();
        return -1;
    }
    CHAR s_old[FX_MAX_SHORT_NAME_LEN + 1] = {0};
    if (fx_unicode_short_name_get_extended(media, u16_old, (ULONG)n_old, s_old, FX_MAX_SHORT_NAME_LEN) != FX_SUCCESS) {
        sd_unlock();
        LOG_DRV_ERROR("rename: source not found path=%s\r\n", oldpath);
        return -1;
    }
    UINT attr;
    ULONG size;
    UINT year, month, day, hour, minute, second;
    char src_abs[FX_MAX_LONG_NAME_LEN];
    if (sd_fx_join(P1, s_old, src_abs, sizeof(src_abs)) != 0 ||
        fx_directory_information_get(media, src_abs, &attr, &size,
                                     &year, &month, &day, &hour, &minute, &second) != FX_SUCCESS) {
        sd_unlock();
        return -1;
    }
    int src_is_dir = (attr & FX_DIRECTORY) ? 1 : 0;

    if (sd_path_ieq(P1, P2)) {
        /* Same directory: one unicode rename; park an existing destination
         * under ~RNMBAK.TMP so a failure can be rolled back. */
        int have_bak = 0, bak_is_dir = 0;
        if (n_old != n_new || memcmp(u16_old, u16_new, (size_t)n_old * 2) != 0) {
            CHAR s_new[FX_MAX_SHORT_NAME_LEN + 1] = {0};
            if (fx_unicode_short_name_get_extended(media, u16_new, (ULONG)n_new,
                                                   s_new, FX_MAX_SHORT_NAME_LEN) == FX_SUCCESS &&
                strcmp(s_new, s_old) != 0) {   /* same 8.3 = same entry, case-only rename */
                char dst_abs[FX_MAX_LONG_NAME_LEN];
                if (sd_fx_join(P1, s_new, dst_abs, sizeof(dst_abs)) != 0 ||
                    fx_directory_information_get(media, dst_abs, &attr, &size,
                                                 &year, &month, &day, &hour, &minute, &second) != FX_SUCCESS) {
                    sd_unlock();
                    return -1;
                }
                bak_is_dir = (attr & FX_DIRECTORY) ? 1 : 0;
                if (bak_is_dir != src_is_dir) {
                    LOG_DRV_ERROR("rename: destination exists with different type: %s\r\n", newpath);
                    sd_unlock();
                    return -1;
                }
                char bak_abs[FX_MAX_LONG_NAME_LEN];
                if (sd_fx_join(P1, "~RNMBAK.TMP", bak_abs, sizeof(bak_abs)) != 0) {
                    sd_unlock();
                    return -1;
                }
                if (bak_is_dir) (void)fx_directory_delete(media, bak_abs);
                else            (void)fx_file_delete(media, bak_abs);
                CHAR s_ign[FX_MAX_SHORT_NAME_LEN + 1] = {0};
                UINT ps = bak_is_dir
                    ? fx_unicode_directory_rename(media, u16_new, (ULONG)n_new, u16_bak, (ULONG)n_bak, s_ign)
                    : fx_unicode_file_rename(media, u16_new, (ULONG)n_new, u16_bak, (ULONG)n_bak, s_ign);
                if (ps != FX_SUCCESS) {
                    LOG_DRV_ERROR("rename: parking destination failed: 0x%02X path=%s\r\n", ps, newpath);
                    sd_unlock();
                    return -1;
                }
                have_bak = 1;
            }
        }

        CHAR s_out[FX_MAX_SHORT_NAME_LEN + 1] = {0};
        UINT status = src_is_dir
            ? fx_unicode_directory_rename(media, u16_old, (ULONG)n_old, u16_new, (ULONG)n_new, s_out)
            : fx_unicode_file_rename(media, u16_old, (ULONG)n_old, u16_new, (ULONG)n_new, s_out);
        if (status == FX_SUCCESS) {
            if (have_bak) {
                CHAR s_bak[FX_MAX_SHORT_NAME_LEN + 1] = {0};
                if (fx_unicode_short_name_get_extended(media, u16_bak, (ULONG)n_bak,
                                                       s_bak, FX_MAX_SHORT_NAME_LEN) == FX_SUCCESS) {
                    char bak_abs[FX_MAX_LONG_NAME_LEN];
                    if (sd_fx_join(P1, s_bak, bak_abs, sizeof(bak_abs)) == 0) {
                        UINT ds = bak_is_dir ? fx_directory_delete(media, bak_abs)
                                             : fx_file_delete(media, bak_abs);
                        if (ds != FX_SUCCESS)
                            LOG_DRV_ERROR("rename: backup left at %s (0x%02X)\r\n", bak_abs, ds);
                    }
                }
            }
            UINT fs = fx_media_flush(media);
            if (fs != FX_SUCCESS) LOG_DRV_ERROR("fx_media_flush after rename failed: 0x%02X\r\n", fs);
        } else {
            LOG_DRV_ERROR("unicode rename failed: 0x%02X, old=%s, new=%s\r\n", status, oldpath, newpath);
            if (have_bak) {
                CHAR s_ign[FX_MAX_SHORT_NAME_LEN + 1] = {0};
                UINT rs = bak_is_dir
                    ? fx_unicode_directory_rename(media, u16_bak, (ULONG)n_bak, u16_new, (ULONG)n_new, s_ign)
                    : fx_unicode_file_rename(media, u16_bak, (ULONG)n_bak, u16_new, (ULONG)n_new, s_ign);
                if (rs != FX_SUCCESS)
                    LOG_DRV_ERROR("rename: restoring destination failed, old copy left at ~RNMBAK.TMP (0x%02X)\r\n", rs);
                else
                    (void)fx_media_flush(media);
            }
        }
        sd_unlock();
        return (status == FX_SUCCESS) ? 0 : -1;
    }

    /* Cross-directory move with a non-ASCII component: FileX unicode renames
     * only work inside one directory, so first plain-rename the entry onto a
     * temp 8.3 name in the target directory (this drops its LFN), then give
     * it the final display name there. */
    char tmp_abs[FX_MAX_LONG_NAME_LEN];
    if (sd_fx_join(P2, "~MVBAK.TMP", tmp_abs, sizeof(tmp_abs)) != 0) {
        sd_unlock();
        return -1;
    }
    fx_file_delete(media, tmp_abs);
    fx_directory_delete(media, tmp_abs);
    UINT status = src_is_dir ? fx_directory_rename(media, src_abs, tmp_abs)
                              : fx_file_rename(media, src_abs, tmp_abs);
    if (status != FX_SUCCESS) {
        LOG_DRV_ERROR("rename: move to target dir failed: 0x%02X, %s -> %s\r\n", status, oldpath, newpath);
        sd_unlock();
        return -1;
    }
    if (!sd_str_is_ascii(new_name)) {
        UCHAR u16_tmp[FX_MAX_LONG_NAME_LEN * 2];
        int n_tmp = sd_utf8_to_utf16("~MVBAK.TMP", u16_tmp, FX_MAX_LONG_NAME_LEN);
        CHAR s_ign[FX_MAX_SHORT_NAME_LEN + 1] = {0};
        if (n_tmp > 0 && fx_directory_default_set(media, P2) == FX_SUCCESS) {
            status = src_is_dir
                ? fx_unicode_directory_rename(media, u16_tmp, (ULONG)n_tmp, u16_new, (ULONG)n_new, s_ign)
                : fx_unicode_file_rename(media, u16_tmp, (ULONG)n_tmp, u16_new, (ULONG)n_new, s_ign);
        } else {
            status = FX_INVALID_NAME;
        }
    } else {
        char dst_abs[FX_MAX_LONG_NAME_LEN];
        if (sd_fx_join(P2, new_name, dst_abs, sizeof(dst_abs)) == 0) {
            status = src_is_dir ? fx_directory_rename(media, tmp_abs, dst_abs)
                                  : fx_file_rename(media, tmp_abs, dst_abs);
        } else {
            status = FX_INVALID_PATH;
        }
    }
    if (status != FX_SUCCESS) {
        LOG_DRV_ERROR("rename: final name failed: 0x%02X, old=%s, new=%s\r\n", status, oldpath, newpath);
        /* Roll the entry back to its origin */
        UINT rb = src_is_dir ? fx_directory_rename(media, tmp_abs, src_abs)
                              : fx_file_rename(media, tmp_abs, src_abs);
        if (rb != FX_SUCCESS)
            LOG_DRV_ERROR("rename: rollback failed, entry left at %s\r\n", tmp_abs);
    } else {
        UINT fs = fx_media_flush(media);
        if (fs != FX_SUCCESS) LOG_DRV_ERROR("fx_media_flush after rename failed: 0x%02X\r\n", fs);
    }
    sd_unlock();
    return (status == FX_SUCCESS) ? 0 : -1;
}

// File pointer position
long sd_filex_ftell(void *context, void *fd)
{
    FX_FILE *file = (FX_FILE*)fd;
    sd_lock();
    long off = file->fx_file_current_file_offset;
    sd_unlock();
    return off;
}

// Move file pointer
int sd_filex_fseek(void *context, void *fd, long offset, int whence)
{
    FX_FILE *file = (FX_FILE*)fd;
    unsigned long new_offset = 0;
    UINT status;

    if (whence == SEEK_SET) {
        new_offset = offset;
    } else if (whence == SEEK_CUR) {
        new_offset = file->fx_file_current_file_offset + offset;
    } else if (whence == SEEK_END) {
        new_offset = file->fx_file_current_file_size + offset;
    }

    sd_lock();
    status = fx_file_seek(file, new_offset);
    sd_unlock();
    if (status != FX_SUCCESS) {
        LOG_DRV_ERROR("fx_file_seek failed: 0x%02X, offset=%ld, whence=%d\r\n", status, offset, whence);
        return -(int)status;
    }
    return 0;
}

// Flush file
int sd_filex_fflush(void *context, void *fd)
{
    FX_MEDIA *media = (FX_MEDIA*)context;
    sd_lock();
    UINT status = fx_media_flush(media);
    sd_unlock();
    if (status != FX_SUCCESS) {
        LOG_DRV_ERROR("fx_media_flush failed: 0x%02X\r\n", status);
        return -(int)status;
    }
    return 0;
}


/**
 * @brief Resolve a directory path to FileX-compatible short-name path.
 *        All components are resolved (last component is a directory).
 */
static bool sd_str_is_ascii(const char *s)
{
    for (; *s; s++)
        if ((unsigned char)*s > 0x7F) return false;
    return true;
}

/* FileX 6.1's fx_unicode_* API family rejects exFAT media with
 * FX_NOT_IMPLEMENTED. On exFAT cards we therefore stay on the CHAR* path for
 * non-ASCII names; the FileX exFAT read/write/search patch layer (see
 * fx_directory_exFAT_entry_{read,write}.c and fx_directory_search.c)
 * transparently converts between the CHAR* UTF-8 names used by this module
 * and the real UTF-16 on-disk names, so PC interop works as well. */
static bool sd_media_is_exfat(FX_MEDIA *media)
{
#ifdef FX_ENABLE_EXFAT
    return media->fx_media_FAT_type == FX_exFAT;
#else
    (void)media;
    return false;
#endif
}

/* True when the final path component needs the unicode LFN API (FAT media
 * with a non-ASCII name). */
static bool sd_name_needs_unicode(FX_MEDIA *media, const char *name)
{
    return !sd_str_is_ascii(name) && !sd_media_is_exfat(media);
}

/* Decode UTF-8 into UTF-16LE (surrogate pairs for code points > 0xFFFF).
 * Returns the number of UTF-16 code units written, or -1 on malformed
 * input / buffer overflow. */
static int sd_utf8_to_utf16(const char *utf8, UCHAR *u16, size_t u16_max)
{
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)utf8;

    while (*p) {
        unsigned long cp;
        int len;
        unsigned char c = *p;

        if (c < 0x80)            { cp = c;           len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else return -1;          /* stray continuation byte */

        if (len > 1) {
            for (int i = 1; i < len; i++) {
                if ((p[i] & 0xC0) != 0x80) return -1;
                cp = (cp << 6) | (p[i] & 0x3F);
            }
        }
        /* reject overlong forms and surrogates encoded in UTF-8 */
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
            (len == 4 && cp < 0x10000) || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
            return -1;

        if (cp >= 0x10000) {
            if (n + 2 > u16_max) return -1;
            cp -= 0x10000;
            u16[n * 2]     = (UCHAR)((cp >> 10) & 0xFF);
            u16[n * 2 + 1] = (UCHAR)((((cp >> 10) >> 8) & 0x03) | 0xD8);
            n++;
            u16[n * 2]     = (UCHAR)(cp & 0x3FF);
            u16[n * 2 + 1] = (UCHAR)(((cp & 0x3FF) >> 8) | 0xDC);
            n++;
        } else {
            if (n + 1 > u16_max) return -1;
            u16[n * 2]     = (UCHAR)(cp & 0xFF);
            u16[n * 2 + 1] = (UCHAR)(cp >> 8);
            n++;
        }
        p += len;
    }
    return (int)n;
}

/* Encode UTF-16LE (with surrogate pairs) into UTF-8. u16_len is the number
 * of UTF-16 code units. Returns bytes written (NUL-terminated), or -1. */
static int sd_utf16_to_utf8(const UCHAR *u16, ULONG u16_len, char *out, size_t out_sz)
{
    size_t o = 0;

    for (ULONG i = 0; i < u16_len; i++) {
        unsigned long cp = u16[i * 2] | ((unsigned long)u16[i * 2 + 1] << 8);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < u16_len) {
            unsigned long lo = u16[(i + 1) * 2] | ((unsigned long)u16[(i + 1) * 2 + 1] << 8);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        if (cp < 0x80) {
            if (o + 1 >= out_sz) return -1;
            out[o++] = (char)cp;
        } else if (cp < 0x800) {
            if (o + 2 >= out_sz) return -1;
            out[o++] = (char)(0xC0 | (cp >> 6));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            if (o + 3 >= out_sz) return -1;
            out[o++] = (char)(0xE0 | (cp >> 12));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            if (o + 4 >= out_sz) return -1;
            out[o++] = (char)(0xF0 | (cp >> 18));
            out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    if (o + 1 > out_sz) return -1;
    out[o] = '\0';
    return (int)o;
}

/* Split "dir/name" into the parent path (written to parent) and a pointer to
 * the final component inside path. No separator -> parent is "" (root). */
static const char *sd_split_path(const char *path, char *parent, size_t parent_sz)
{
    const char *fname = strrchr(path, '/');
    if (!fname) fname = strrchr(path, '\\');
    if (fname) {
        size_t plen = (size_t)(fname - path);
        if (plen >= parent_sz) plen = parent_sz - 1;
        memcpy(parent, path, plen);
        parent[plen] = '\0';
        return fname + 1;
    }
    parent[0] = '\0';
    return path;
}

/* Join a resolved absolute fx parent path and an 8.3 short name. */
static int sd_fx_join(const char *parent, const char *name, char *out, size_t out_sz)
{
    size_t pl = strlen(parent), nl = strlen(name);
    if (pl + 1 + nl + 1 > out_sz) return -1;
    memcpy(out, parent, pl);
    if (pl == 0 || out[pl - 1] != '\\') out[pl++] = '\\';
    memcpy(out + pl, name, nl + 1);
    return 0;
}

static int sd_resolve_dir_path(FX_MEDIA *media, const char *path, char *fx_out, size_t out_sz)
{
    char saved_buf[FX_MAX_LONG_NAME_LEN];
    CHAR *saved_ptr;
    fx_directory_default_get(media, &saved_ptr);
    saved_buf[0] = '\0';
    if (saved_ptr && saved_ptr[0])
        strncpy(saved_buf, saved_ptr, sizeof(saved_buf) - 1);
    saved_buf[sizeof(saved_buf) - 1] = '\0';

    fx_directory_default_set(media, "\\");
    fx_out[0] = '\\';
    fx_out[1] = '\0';
    size_t fx_len = 1;

    char comp[FX_MAX_LONG_NAME_LEN];
    const char *p = path;
    while (*p) {
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;

        size_t ci = 0;
        while (*p && *p != '/' && *p != '\\' && ci < sizeof(comp) - 1)
            comp[ci++] = *p++;
        comp[ci] = '\0';
        if (ci == 0) continue;

        /* Convert UTF-8 → UTF-16LE */
        UCHAR utf16[512];
        int utf16_units = sd_utf8_to_utf16(comp, utf16, sizeof(utf16) / 2);
        if (utf16_units < 0) continue;
        ULONG char_count = (ULONG)utf16_units;

        char short_name[14] = {0};
        const char *resolved = comp;  /* default: use as-is */
        if (fx_unicode_short_name_get(media, utf16, char_count, short_name) == FX_SUCCESS)
            resolved = short_name;

        size_t rl = strlen(resolved);
        if (fx_len + 1 + rl < out_sz) {
            if (fx_len > 1) fx_out[fx_len++] = '\\';
            memcpy(fx_out + fx_len, resolved, rl);
            fx_len += rl;
            fx_out[fx_len] = '\0';
        }
        if (fx_directory_default_set(media, (CHAR *)resolved) != FX_SUCCESS) {
            if (saved_buf[0]) fx_directory_default_set(media, saved_buf);
            else fx_directory_default_set(media, "\\");
            return -1;
        }
    }

    if (saved_buf[0]) fx_directory_default_set(media, saved_buf);
    else fx_directory_default_set(media, "\\");
    return 0;
}

/**
 * @brief Resolve a file path: resolve parent directory, keep filename as-is.
 *        Output is a FileX-compatible absolute path. For non-ASCII filenames
 *        the final component is additionally mapped to its 8.3 short name via
 *        the unicode LFN lookup, so entries created on a PC (real UTF-16 LFN)
 *        are reachable. Returns -1 when the entry does not exist.
 */
static int sd_resolve_file_path(FX_MEDIA *media, const char *path, char *fx_out, size_t out_sz)
{
    /* Split into parent directory and filename */
    char parent[FX_MAX_LONG_NAME_LEN];
    const char *fname = sd_split_path(path, parent, sizeof(parent));

    /* Resolve parent directory */
    char resolved_parent[FX_MAX_LONG_NAME_LEN];
    if (sd_resolve_dir_path(media, (parent[0] == '\0') ? "/" : parent,
                            resolved_parent, sizeof(resolved_parent)) != 0)
        return -1;

    /* Non-ASCII final name on FAT media: an entry only exists under a
     * matching LFN - map it to its 8.3 short name (fx_* CHAR* APIs can
     * address short names). exFAT keeps the raw name (see sd_media_is_exfat). */
    char resolved_name[FX_MAX_LONG_NAME_LEN];
    if (sd_name_needs_unicode(media, fname)) {
        UCHAR u16[FX_MAX_LONG_NAME_LEN * 2];
        int u16n = sd_utf8_to_utf16(fname, u16, FX_MAX_LONG_NAME_LEN);
        if (u16n <= 0) return -1;
        if (fx_directory_default_set(media, resolved_parent) != FX_SUCCESS)
            return -1;
        CHAR short_name[FX_MAX_SHORT_NAME_LEN + 1] = {0};
        UINT status = fx_unicode_short_name_get_extended(
            media, u16, (ULONG)u16n, short_name, FX_MAX_SHORT_NAME_LEN);
        if (status != FX_SUCCESS || short_name[0] == '\0') return -1;
        strncpy(resolved_name, short_name, sizeof(resolved_name) - 1);
        resolved_name[sizeof(resolved_name) - 1] = '\0';
    } else {
        if (strlen(fname) >= sizeof(resolved_name)) return -1;
        strcpy(resolved_name, fname);
    }

    /* Build full path: parent\filename */
    return sd_fx_join(resolved_parent, resolved_name, fx_out, out_sz);
}

void* sd_filex_opendir(void *context, const char *path)
{
    FX_MEDIA *media = (FX_MEDIA*)context;
    filex_dir_t *dir = (filex_dir_t*)hal_mem_alloc_fast(sizeof(filex_dir_t));
    if (!dir) return NULL;
    dir->media = media;

    char fx_path[FX_MAX_LONG_NAME_LEN];

    /* Resolve and select under the lock, then snapshot the whole default
     * path into the handle (see filex_dir_t): readdir swaps this copy in
     * for every call, so other tasks' iterators and path resolutions -
     * which rebuild the media-global default path - cannot corrupt this
     * iteration, and vice versa. */
    sd_lock();
    if (sd_resolve_dir_path(media, path, fx_path, sizeof(fx_path)) != 0) {
        sd_unlock();
        hal_mem_free(dir);
        return NULL;
    }
    if (fx_directory_default_set(media, fx_path) != FX_SUCCESS) {
        sd_unlock();
        hal_mem_free(dir);
        return NULL;
    }
    dir->state = media->fx_media_default_path;
    dir->state.fx_path_current_entry = 0;
    sd_unlock();

    dir->first_entry = 1;
    dir->finished = 0;
    return dir;
}

int sd_filex_readdir(void *context, void *dd, char *info)
{
    filex_dir_t *dir = (filex_dir_t*)dd;
    UINT status;
    struct sd_info *sd_info = (struct sd_info *)info;
    if (dir->finished) return 0;

    sd_lock();
    /* Swap this handle's saved default path into the media for the duration
     * of the call: the find calls below advance the iteration cursor and
     * directory-size cache in place, and any default_set() by another task
     * between our calls would discard them (FileX keeps no other iteration
     * state). Saved back below before unlocking. */
    dir->media->fx_media_default_path = dir->state;
    if (dir->first_entry) {
        /* directory_name is OUTPUT - receives entry name in current default dir */
        CHAR entry_name[FX_MAX_LONG_NAME_LEN] = {0};
        status = fx_directory_first_full_entry_find(
            dir->media,
            entry_name,
            &dir->attributes,
            &dir->size,
            &dir->year,
            &dir->month,
            &dir->day,
            &dir->hour,
            &dir->minute,
            &dir->second);
        if (status == FX_SUCCESS) {
            strncpy(dir->entry_name, entry_name, FX_MAX_LONG_NAME_LEN - 1);
            dir->entry_name[FX_MAX_LONG_NAME_LEN - 1] = '\0';
        }
        dir->first_entry = 0;
    } else {
        // Continue iteration, pass previous entry_name
        status = fx_directory_next_full_entry_find(
            dir->media,
            dir->entry_name,
            &dir->attributes,
            &dir->size,
            &dir->year,
            &dir->month,
            &dir->day,
            &dir->hour,
            &dir->minute,
            &dir->second);
    }

    /* Capture the advanced iteration state (cursor, directory size cache)
     * for the next call before anyone else can touch the global path. */
    dir->state = dir->media->fx_media_default_path;

    if (status != FX_SUCCESS) {
        dir->finished = 1;
        sd_unlock();
        return 0;
    }

    // Fill info - only call fx_unicode_name_get when the CHAR* name is not
    // the display name: either it has non-ASCII bytes, or it is a numeric
    // 8.3 alias ("NAME~1.TXT") FileX falls back to when the LFN holds real
    // UTF-16 (e.g. Chinese names created on a PC). The lookup is an O(N)
    // directory scan, so plain ASCII names skip it to avoid O(N²) listings.
    sd_info->name[0] = '\0';
    {
        bool need_unicode = false;
        for (const char *p = dir->entry_name; *p; p++) {
            if ((unsigned char)*p > 0x7F || *p == '~') { need_unicode = true; break; }
        }

        if (need_unicode) {
            UCHAR unicode_name[FX_MAX_LONG_NAME_LEN * 2];
            ULONG unicode_len = 0;
            UINT uni_status = fx_unicode_name_get_extended(dir->media, dir->entry_name,
                                                   unicode_name, &unicode_len, sizeof(unicode_name));
            if (uni_status == FX_SUCCESS && unicode_len > 0 && unicode_len * 2 <= sizeof(unicode_name)) {
                // Convert UTF-16LE to UTF-8 for sd_info->name
                if (sd_utf16_to_utf8(unicode_name, unicode_len,
                                     sd_info->name, FX_MAX_LONG_NAME_LEN) < 0)
                    sd_info->name[0] = '\0';
            }
        }
    }
    // Fallback: use short name if Unicode conversion failed
    if (sd_info->name[0] == '\0') {
        strncpy(sd_info->name, dir->entry_name, FX_MAX_LONG_NAME_LEN -1);
        sd_info->name[FX_MAX_LONG_NAME_LEN - 1] = '\0';
    }
    sd_info->name[FX_MAX_LONG_NAME_LEN - 1] = '\0';

    // Also expose the 8.3 short name for reliable directory navigation
    sd_info->short_name[0] = '\0';
    strncpy(sd_info->short_name, dir->entry_name, 13);
    sd_info->short_name[13] = '\0';

    sd_info->size = dir->size;
    /* Calculate mtime directly - avoid calling stat to not break readdir iteration */
    {
        struct tm t;
        memset(&t, 0, sizeof(t));
        t.tm_year = (int)dir->year - 1900;  // FileX returns full year
        t.tm_mon  = (int)dir->month - 1;
        t.tm_mday = (int)dir->day;
        t.tm_hour = (int)dir->hour;
        t.tm_min  = (int)dir->minute;
        t.tm_sec  = (int)dir->second;
        sd_info->mtime = (uint32_t)mktime(&t);
    }
    sd_info->type = (dir->attributes & FX_DIRECTORY) ? SD_TYPE_DIR : SD_TYPE_REG;
    sd_unlock();
    return 1;
}

int sd_filex_closedir(void *context, void *dd)
{
    /* Reset default to root so later ops start fresh. Iterators keep their
     * own path state in the handle, so this cannot disturb an in-flight
     * concurrent iteration (it restores its state on its next call). */
    sd_lock();
    fx_directory_default_set(((filex_dir_t*)dd)->media, "\\");
    sd_unlock();
    hal_mem_free(dd);
    return 0;
}

int sd_filex_mkdir(void *context, const char *path)
{
    FX_MEDIA *media = (FX_MEDIA*)context;
    if (!media || !path) return -1;

    char parent[FX_MAX_LONG_NAME_LEN];
    const char *fname = sd_split_path(path, parent, sizeof(parent));

    sd_lock();
    UINT status;
    if (!sd_name_needs_unicode(media, fname)) {
        char fx_path[FX_MAX_LONG_NAME_LEN];
        if (sd_resolve_file_path(media, path, fx_path, sizeof(fx_path)) != 0) {
            sd_unlock();
            return -1;
        }
        status = fx_directory_create(media, fx_path);
    } else {
        /* Non-ASCII name: create through the unicode API (proper UTF-16 LFN,
         * see sd_filex_fopen) with the parent selected as default dir. */
        char fx_parent[FX_MAX_LONG_NAME_LEN];
        UCHAR u16[FX_MAX_LONG_NAME_LEN * 2];
        int n = sd_utf8_to_utf16(fname, u16, FX_MAX_LONG_NAME_LEN);
        if (n <= 0 ||
            sd_resolve_dir_path(media, (parent[0] == '\0') ? "/" : parent,
                                fx_parent, sizeof(fx_parent)) != 0 ||
            fx_directory_default_set(media, fx_parent) != FX_SUCCESS) {
            sd_unlock();
            return -1;
        }
        CHAR short_name[FX_MAX_SHORT_NAME_LEN + 1] = {0};
        status = fx_unicode_directory_create(media, u16, (ULONG)n, short_name);
    }
    if (status == FX_SUCCESS) {
        UINT fs = fx_media_flush(media);
        if (fs != FX_SUCCESS) LOG_DRV_ERROR("fx_media_flush after mkdir failed: 0x%02X\r\n", fs);
    } else {
        LOG_DRV_ERROR("fx_directory_create failed: 0x%02X path=%s\r\n", status, path);
    }
    sd_unlock();
    return (status == FX_SUCCESS) ? 0 : -1;
}

int sd_filex_stat(void *context, const char *path, struct stat *st)
{
    FX_MEDIA *media = (FX_MEDIA*)context;
    UINT attributes;
    ULONG size;
    UINT year, month, day, hour, minute, second;
    UINT status;

    if (!media || !path || !st) {
        return -1;
    }

    char fx_path[FX_MAX_LONG_NAME_LEN];
    sd_lock();
    if (sd_resolve_file_path(media, path, fx_path, sizeof(fx_path)) != 0) {
        sd_unlock();
        return -1;
    }
    status = fx_directory_information_get(
        media,
        fx_path,
        &attributes,
        &size,
        &year, &month, &day, &hour, &minute, &second
    );
    sd_unlock();

    if (status != FX_SUCCESS) {
        return -1; // File does not exist or other error
    }

    memset(st, 0, sizeof(struct stat));

    // File size
    st->st_size = (off_t)size;

    // File type and permissions
    if (attributes & FX_DIRECTORY) {
        st->st_mode = S_IFDIR | 0755; // Directory, permissions can be customized
    } else {
        st->st_mode = S_IFREG | 0644; // Regular file, permissions can be customized
    }

    // Modification time - FileX year since 1980, struct tm since 1900
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = (int)year - 1900;    // FileX returns full year from FX_BASE_YEAR(1980) + FAT raw value
    t.tm_mon  = (int)month - 1;      // struct tm: 0-11
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = second;

    time_t mtime = mktime(&t);
    st->st_mtime = mtime;
    st->st_atime = mtime;
    st->st_ctime = mtime;

    // Other fields (such as inode, uid, gid, etc.) can be set or zeroed based on actual situation

    return 0;
}

static file_ops_t sd_file_ops = {
    .fopen   = sd_filex_fopen,
    .fclose  = sd_filex_fclose,
    .fwrite  = sd_filex_fwrite,
    .fread   = sd_filex_fread,
    .remove  = sd_filex_remove,
    .rename  = sd_filex_rename,
    .mkdir   = sd_filex_mkdir,
    .ftell   = sd_filex_ftell,
    .fseek   = sd_filex_fseek,
    .fflush  = sd_filex_fflush,
    .opendir = sd_filex_opendir,
    .readdir = sd_filex_readdir,
    .closedir= sd_filex_closedir,
    .stat = sd_filex_stat,
};


static void sdProcess(void *argument)
{
    sd_t *sd = (sd_t *)argument;
    unsigned int sd_status = FX_SUCCESS;
    LOG_DRV_DEBUG("sdProcess start\r\n");
    pwr_manager_acquire(sd->pwr_handle);
    osDelay(5);
    fx_system_initialize();
    
    // Set FileX system time and date from RTC
    RTC_TIME_S rtc_time = rtc_get_time();
    if (rtc_time.year > 0) {  // Check if RTC is initialized
        UINT year = rtc_time.year + 1960;  // RTC year is offset from 1960
        UINT month = rtc_time.month;
        UINT day = rtc_time.date;
        UINT hour = rtc_time.hour;
        UINT minute = rtc_time.minute;
        UINT second = rtc_time.second;
        
        fx_system_date_set(year, month, day);
        fx_system_time_set(hour, minute, second);
        LOG_DRV_DEBUG("FileX time set from RTC: %04d-%02d-%02d %02d:%02d:%02d\r\n", 
                      year, month, day, hour, minute, second);
    } else {
        LOG_DRV_DEBUG("RTC not initialized, using default FileX time\r\n");
    }
    
    sd->mode = SD_MODE_UNPLUG;
    if(SD_IsDetected()){
        sd_status =  fx_media_open(&sd->sdio_disk, FX_SD_VOLUME_NAME, fx_stm32_sd_driver, (VOID *)FX_NULL, (VOID *) fx_sd_media_memory, FX_STM32_SD_DEFAULT_SECTOR_SIZE);
        /* Check the media open sd_status */
        if (sd_status != FX_SUCCESS){
            LOG_DRV_ERROR("sd_init error 0x%x\r\n",sd_status);
            sd->mode = SD_MODE_UNKNOWN;
        }else{
            /* USER CODE BEGIN fx_app_thread_entry 1 */
            fx_media_close_notify_set(&sd->sdio_disk, media_close_callback);
            /* Register ops BEFORE publishing MEDIA_OPENED: sd_is_media_open()
             * gates disk_file(FS_SD) callers (upload storage switch, count
             * rebuild). If the flag went out first, a reader landing between
             * the two would see "open" but instances[FS_SD].ops == NULL and
             * cache an empty result (racy 0 record counts after hot-plug). */
            sd->file_ops_handle = file_ops_register(FS_SD, &sd_file_ops, &sd->sdio_disk);
            sd->media_status = MEDIA_OPENED;
            sd->media_generation++;
            sd->mode = SD_MODE_NORMAL;
            file_ops_switch(sd->file_ops_handle);
        }
    }
    sd->is_init = true;

    exti0_irq_register(sd_gpio_interrupt);
    HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);

    while (sd->is_init) {
        if (osSemaphoreAcquire(sd->sem_id, osWaitForever) == osOK) {
            if(sd->mode == SD_MODE_FORMATING){
                continue;
            }
            HAL_NVIC_DisableIRQ(EXTI0_IRQn);
            if(SD_IsDetected() && sd->media_status == MEDIA_CLOSED){
                LOG_DRV_DEBUG("SD card Detected.\r\n");
                sd_status = fx_media_open(&sd->sdio_disk, FX_SD_VOLUME_NAME, fx_stm32_sd_driver, (VOID *)FX_NULL, (VOID *) fx_sd_media_memory, sizeof(fx_sd_media_memory));
                if (sd_status != FX_SUCCESS){
                    LOG_DRV_ERROR("sd_init error 0x%x\r\n",sd_status);
                    memset(&sd->sdio_disk, 0, sizeof(FX_MEDIA));
                    sd->mode = SD_MODE_UNKNOWN;
                }else{
                    fx_media_close_notify_set(&sd->sdio_disk, media_close_callback);
                    /* Same publish-last ordering as the boot open above:
                     * register ops first, MEDIA_OPENED last - readers between
                     * the two must not see "open" with no registered ops. */
                    if(sd->file_ops_handle == -1){
                        sd->file_ops_handle = file_ops_register(FS_SD, &sd_file_ops, &sd->sdio_disk);
                        LOG_DRV_DEBUG("SD file system register. :%d \r\n", sd->file_ops_handle);
                        file_ops_switch(sd->file_ops_handle);
                    }
                    sd->media_status = MEDIA_OPENED;
                    sd->media_generation++;
                    sd->mode = SD_MODE_NORMAL;
                }

            }else if(!SD_IsDetected() && sd->media_status == MEDIA_OPENED){
                LOG_DRV_DEBUG("Remove the SD card.\r\n");
                if(sd->file_ops_handle != -1){
                    sd->media_status = MEDIA_CLOSED;
                    fx_media_close(&sd->sdio_disk);
                    sd->mode = SD_MODE_UNPLUG;
                    memset(&sd->sdio_disk, 0, sizeof(FX_MEDIA));
                    if(file_ops_unregister(sd->file_ops_handle) == 0){
                        sd->file_ops_handle = -1;
                        LOG_DRV_DEBUG("SD file system unregister. now handle:%d \r\n", sd->file_ops_handle);
                    }
                }
                osSemaphoreAcquire(sd->sem_id, 1000);
            }
            HAL_NVIC_EnableIRQ(EXTI0_IRQn);
        }
    }
    osThreadExit();
}

int sd_file_ops_switch(void)
{
    if(g_sd.media_status != MEDIA_OPENED){
        return -1;
    }
    if(g_sd.file_ops_handle != -1){
        return file_ops_switch(g_sd.file_ops_handle);
    }
    return -1;
}

int sd_format(void)
{
    if (g_sd.is_init != true) {
        LOG_DRV_ERROR("SD not initialized, cannot format.\n");
        return AICAM_ERROR;
    }

    if (g_sd.media_status == MEDIA_OPENED) {
        fx_media_close(&g_sd.sdio_disk);
        g_sd.media_status = MEDIA_CLOSED;
        memset(&g_sd.sdio_disk, 0, sizeof(FX_MEDIA));
        if(file_ops_unregister(g_sd.file_ops_handle) == 0){
            g_sd.file_ops_handle = -1;
            LOG_DRV_DEBUG("SD file system unregister. now handle:%d \r\n", g_sd.file_ops_handle);
        }
        LOG_DRV_INFO("Media closed before formatting.\n");
    }

    FX_MEDIA temp_media;
    memset(&temp_media, 0, sizeof(FX_MEDIA));

    LOG_DRV_INFO("Starting SD format...\n");
    HAL_SD_CardInfoTypeDef cardinfo;
    HAL_SD_GetCardInfo(&hsd1, &cardinfo);
    ULONG total_sectors = cardinfo.LogBlockNbr;

    g_sd.mode = SD_MODE_FORMATING;
    unsigned int status = fx_media_exFAT_format(
        &temp_media,
        fx_stm32_sd_driver,
        0,
        (UCHAR*)fx_sd_media_memory,
        FX_STM32_SD_DEFAULT_SECTOR_SIZE,
        FX_SD_VOLUME_NAME,
        1,              // number_of_fats
        0,              // hidden_sectors
        total_sectors,  // total_sectors
        512,            // bytes_per_sector
        256,             // sectors_per_cluster
        0x1234,         // volume_serial_number
        0               // boundary_unit
    );

    if (status == FX_SUCCESS) {
        LOG_DRV_INFO("exFAT format successful.\n");
    } else {
        g_sd.mode = SD_MODE_UNKNOWN;
        LOG_DRV_ERROR("exFAT format failed: 0x%02X (%u)\n", status, status);
        return AICAM_ERROR;
    }

    // Open after formatting
    status = fx_media_open(
        &g_sd.sdio_disk,
        FX_SD_VOLUME_NAME,
        fx_stm32_sd_driver,
        (VOID *)0,
        (UCHAR*)fx_sd_media_memory,
        sizeof(fx_sd_media_memory)
    );

    if (status == FX_SUCCESS) {
        LOG_DRV_INFO("SD opened successfully after format.\n");
        sd_disk_info_t info;
        fx_media_close_notify_set(&g_sd.sdio_disk, media_close_callback);
        /* Same publish-last ordering: ops registered before MEDIA_OPENED. */
        if(g_sd.file_ops_handle == -1){
            g_sd.file_ops_handle = file_ops_register(FS_SD, &sd_file_ops, &g_sd.sdio_disk);
            LOG_DRV_DEBUG("SD file system register. :%d \r\n", g_sd.file_ops_handle);
            file_ops_switch(g_sd.file_ops_handle);
        }
        g_sd.media_status = MEDIA_OPENED;
        g_sd.mode = SD_MODE_NORMAL;
        if (sd_get_disk_info(&info) == 0) {
            LOG_DRV_INFO("Format verification: Total %lu KB, Free %lu KB\n", 
                   info.total_KBytes, info.free_KBytes);
        }
    } else {
        LOG_DRV_ERROR("SD open after format failed: 0x%02X (%u)\n", status, status);
        g_sd.mode = SD_MODE_UNKNOWN;
    }
    osSemaphoreRelease(g_sd.sem_id);

    return AICAM_OK;
}

int sd_get_disk_info(sd_disk_info_t *info)
{
    if (!info) {
        return -1;
    }
    /* Zero everything first - callers must never see stale/garbage free_KBytes. */
    memset(info, 0, sizeof(*info));
    info->mode = g_sd.mode;
    if(g_sd.media_status != MEDIA_OPENED){
        /* Media not open: report the mode but signal failure so callers do not
         * mistake an unmounted card for an empty/full one. */
        return -1;
    }
    FX_MEDIA *media = &g_sd.sdio_disk;
    ULONG64 available_bytes = 0;
    UINT status = fx_media_extended_space_available(&g_sd.sdio_disk, &available_bytes);
    if (status != FX_SUCCESS) {
        return -2;
    }

    ULONG total_clusters = media->fx_media_total_clusters;
    ULONG sectors_per_cluster = media->fx_media_sectors_per_cluster;
    ULONG bytes_per_sector = media->fx_media_bytes_per_sector;

    info->total_KBytes = total_clusters * (sectors_per_cluster * bytes_per_sector / 1024 );
    info->free_KBytes  = ((ULONG64)available_bytes  / 1024);

#if defined(FX_ENABLE_EXFAT) && (FX_ENABLE_EXFAT == 1)
    if (media->fx_media_FAT_type == FX_exFAT)
        strcpy(info->fs_type, "exFAT");
    else
        strcpy(info->fs_type, "FAT32");
#else
    strcpy(info->fs_type, "FAT32");
#endif

    return 0;
}

/* ---- SD bus speed / mode diagnostics ----------------------------------- */
/* SD spec speed ceilings (HAL keeps its own private copies; redefine here). */
#define SD_SPEED_NORMAL_HZ   25000000U   /* Default Speed cap */
#define SD_SPEED_HIGH_HZ     50000000U   /* High Speed cap     */

int sd_get_speed_info(sd_speed_info_t *info)
{
    if (info == NULL) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    if (!g_sd.is_init) {
        return -1;   /* card not initialized */
    }

    HAL_SD_CardInfoTypeDef card;
    if (HAL_SD_GetCardInfo(&hsd1, &card) != HAL_OK) {
        return -1;
    }

    info->card_type = (card.CardType == CARD_SDHC_SDXC) ? "SDHC/SDXC" : "SDSC";
    /* NOTE: HAL_SD_GetCardInfo() does NOT copy CardSpeed - read the cached HAL
     * value directly. A UHS-graded card is tagged ULTRA here even though UHS
     * needs a 1.8V transceiver this build lacks (USE_SD_TRANSCEIVER=0); the
     * real bus state is in bus_clk_hz / hs_switched below. */
    switch (hsd1.SdCard.CardSpeed) {
        case CARD_ULTRA_HIGH_SPEED: info->card_speed = "UltraHigh(UHS-grade, no 1.8V xcvr)"; break;
        case CARD_HIGH_SPEED:       info->card_speed = "High(<=50MHz)";   break;
        default:                    info->card_speed = "Normal(<=25MHz)"; break;
    }

    uint32_t src = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC1);
    info->src_clk_hz = src;
    uint32_t clkcr = hsd1.Instance->CLKCR;
    uint32_t div = clkcr & SDMMC_CLKCR_CLKDIV_Msk;
    info->clkdiv = div;
    /* SDMMC_CK = SDMMCCLK / (2 * CLKDIV); CLKDIV == 0 means /2 */
    info->bus_clk_hz = (div != 0U) ? (src / (2U * div)) : (src / 2U);
    uint32_t widbus = (clkcr & SDMMC_CLKCR_WIDBUS_Msk) >> SDMMC_CLKCR_WIDBUS_Pos;
    info->bus_width = (widbus == 0U) ? 1U : (widbus == 1U) ? 4U : 8U;
    info->hs_switched = g_sd.hs_switched;
    return 0;
}

/* Apply a speed mode assuming the card is initialized and in TRANSFER state.
 * No media-open gate - used both at init (before fx_media_open) and at runtime.
 *   DEFAULT   : bus <= 25MHz, card mode untouched
 *   HIGH/AUTO : bus <= 50MHz, CMD6 card to High Speed (AUTO collapses to HS:
 *               real UHS needs the 1.8V transceiver this build lacks)
 *   OVERCLOCK : bus ~100MHz (CLKDIV=0). Forces the CARD to Default Speed first
 *               - HS+100MHz is unstable on UHS cards (HS receivers tuned for
 *               50MHz); the known-working overclock is DS+100MHz. OUT OF SPEC.
 * The CLKDIV force works because the HAL's ULTRA cap branch uses Init.ClockDiv
 * verbatim (see HAL_SD_ConfigWideBusOperation); OVERCLOCK thus relies on the
 * card being tagged CARD_ULTRA_HIGH_SPEED. */
static int sd_apply_speed_mode_locked(sd_speed_mode_e mode)
{
    uint32_t src = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC1);
    uint32_t saved_div = hsd1.Init.ClockDiv;
    HAL_StatusTypeDef st = HAL_OK;

    uint32_t div;
    switch (mode) {
        case SD_SPEED_OVERCLOCK: div = 0U; break;                              /* ~100MHz */
        case SD_SPEED_DEFAULT:   div = (src != 0U) ? src / (2U * SD_SPEED_NORMAL_HZ) : 1U; break;
        default:                 div = (src != 0U) ? src / (2U * SD_SPEED_HIGH_HZ)   : 1U; break; /* HIGH/AUTO */
    }
    if (div == 0U && mode != SD_SPEED_OVERCLOCK) {
        div = 1U;
    }

    sd_lock();
    if (mode == SD_SPEED_OVERCLOCK) {
        /* Force the card to Default Speed at the current (lower) clock BEFORE
         * raising it - CMD6 at 100MHz HS would be unreliable. Best-effort:
         * ignore the return (card may already be in DS). */
        (void)HAL_SD_ConfigSpeedBusOperation(&hsd1, SDMMC_SPEED_MODE_DEFAULT);
        hsd1.Init.ClockDiv = 0U;
        st = HAL_SD_ConfigWideBusOperation(&hsd1, hsd1.Init.BusWide);
        hsd1.Init.ClockDiv = saved_div;
    } else {
        /* DEFAULT/HIGH/AUTO: set the (safe) clock first, then CMD6 to HS. */
        hsd1.Init.ClockDiv = div;
        st = HAL_SD_ConfigWideBusOperation(&hsd1, hsd1.Init.BusWide);
        hsd1.Init.ClockDiv = saved_div;
        if (st == HAL_OK && (mode == SD_SPEED_HIGH || mode == SD_SPEED_AUTO)) {
            uint32_t sm = (mode == SD_SPEED_AUTO) ? SDMMC_SPEED_MODE_AUTO
                                                  : SDMMC_SPEED_MODE_HIGH;
            st = HAL_SD_ConfigSpeedBusOperation(&hsd1, sm);
        }
    }
    sd_unlock();

    if (st != HAL_OK) {
        LOG_DRV_ERROR("sd_apply_speed_mode(%d): HAL failed\r\n", (int)mode);
        return -1;
    }
    if (mode == SD_SPEED_OVERCLOCK) {
        g_sd.hs_switched = false;             /* card forced to Default Speed */
    } else if (mode == SD_SPEED_HIGH || mode == SD_SPEED_AUTO) {
        g_sd.hs_switched = true;
    }
    /* SD_SPEED_DEFAULT: card mode untouched, hs_switched unchanged */
    return 0;
}

int sd_set_speed_mode(sd_speed_mode_e mode)
{
    if (g_sd.media_status != MEDIA_OPENED) {
        LOG_DRV_ERROR("sd_set_speed_mode: media not open\r\n");
        return -1;
    }
    return sd_apply_speed_mode_locked(mode);
}

#define SD_SPEED_TEST_FILE   "~sdrwtest.tmp"

int sd_speed_test(uint32_t total_kb, uint32_t chunk_kb,
                  uint32_t *write_kbps, uint32_t *read_kbps)
{
    if (write_kbps != NULL) *write_kbps = 0;
    if (read_kbps  != NULL) *read_kbps  = 0;

    if (g_sd.media_status != MEDIA_OPENED) {
        LOG_DRV_ERROR("sd_speed_test: media not open\r\n");
        return -1;
    }
    if (total_kb == 0U) total_kb = 2048U;
    if (chunk_kb == 0U) chunk_kb = 32U;

    uint32_t total_bytes = total_kb * 1024U;
    /* SDMMC DMA requires an internal-SRAM buffer; the MEM_FAST pool is small
     * (~184KB, shared with stacks/FX), so try the requested chunk then fall
     * back to safe sizes so the test still runs under memory pressure. Never
     * grow beyond the requested chunk. */
    uint32_t chunk_bytes = chunk_kb * 1024U;
    uint8_t *buf = NULL;
    uint32_t try_kb = chunk_kb;
    const uint32_t steps_kb[] = {chunk_kb, 32, 16, 8, 4};
    for (size_t i = 0; i < sizeof(steps_kb) / sizeof(steps_kb[0]) && buf == NULL; i++) {
        try_kb = steps_kb[i];
        if (try_kb * 1024U > chunk_bytes) {
            continue;   /* don't grow the chunk */
        }
        buf = (uint8_t *)hal_mem_alloc_aligned(try_kb * 1024U, 32, MEM_FAST);
    }
    if (buf != NULL) {
        chunk_bytes = try_kb * 1024U;
    } else {
        LOG_DRV_ERROR("sd_speed_test: alloc failed (tried down to %luKB)\r\n",
                      (unsigned long)try_kb);
        return -1;
    }
    /* DMA-integrity pattern: byte value = offset within chunk (0..255 repeat) */
    for (uint32_t i = 0; i < chunk_bytes; i++) {
        buf[i] = (uint8_t)(i & 0xFF);
    }

    FX_MEDIA *media = &g_sd.sdio_disk;
    FX_FILE file;
    UINT status;
    uint32_t t0 = 0, t1 = 0, t2 = 0, t3 = 0;
    uint32_t written = 0, read_bytes = 0;
    int ret = 0;

    sd_lock();
    (void)fx_file_delete(media, SD_SPEED_TEST_FILE);   /* ignore if absent */

    status = fx_file_create(media, SD_SPEED_TEST_FILE);
    if (status != FX_SUCCESS && status != FX_ALREADY_CREATED) {
        LOG_DRV_ERROR("sd_speed_test: fx_file_create 0x%02X\r\n", status);
        ret = -1;
        goto done;
    }
    status = fx_file_open(media, &file, SD_SPEED_TEST_FILE, FX_OPEN_FOR_WRITE);
    if (status != FX_SUCCESS) {
        LOG_DRV_ERROR("sd_speed_test: fx_file_open(w) 0x%02X\r\n", status);
        ret = -1;
        goto done;
    }

    t0 = HAL_GetTick();
    while (written < total_bytes) {
        uint32_t n = (total_bytes - written < chunk_bytes) ? (total_bytes - written) : chunk_bytes;
        status = fx_file_write(&file, buf, n);
        if (status != FX_SUCCESS) {
            LOG_DRV_ERROR("sd_speed_test: fx_file_write 0x%02X @%lu\r\n", status, written);
            ret = -2;
            break;
        }
        written += n;
    }
    if (ret == 0) {
        status = fx_media_flush(media);
        if (status != FX_SUCCESS) {
            LOG_DRV_ERROR("sd_speed_test: fx_media_flush 0x%02X\r\n", status);
        }
    }
    t1 = HAL_GetTick();
    fx_file_close(&file);

    if (ret != 0) {
        goto done;
    }

    status = fx_file_open(media, &file, SD_SPEED_TEST_FILE, FX_OPEN_FOR_READ);
    if (status != FX_SUCCESS) {
        LOG_DRV_ERROR("sd_speed_test: fx_file_open(r) 0x%02X\r\n", status);
        ret = -1;
        goto done;
    }

    t2 = HAL_GetTick();
    while (read_bytes < total_bytes) {
        uint32_t n = (total_bytes - read_bytes < chunk_bytes) ? (total_bytes - read_bytes) : chunk_bytes;
        ULONG actual = 0;
        status = fx_file_read(&file, buf, n, &actual);
        if (status != FX_SUCCESS && status != FX_END_OF_FILE) {
            LOG_DRV_ERROR("sd_speed_test: fx_file_read 0x%02X @%lu\r\n", status, read_bytes);
            ret = -3;
            break;
        }
        read_bytes += actual;
        if (status == FX_END_OF_FILE || actual < n) break;
    }
    t3 = HAL_GetTick();

    /* Untimed integrity sanity check on the first chunk - catches DMA/cache
     * corruption without skewing the read throughput number. */
    if (fx_file_seek(&file, 0) == FX_SUCCESS) {
        ULONG chk = 0;
        if (fx_file_read(&file, buf, chunk_bytes, &chk) == FX_SUCCESS) {
            for (ULONG k = 0; k < chk; k++) {
                if (buf[k] != (uint8_t)(k & 0xFF)) {
                    LOG_DRV_ERROR("sd_speed_test: data mismatch @%lu\r\n", (unsigned long)k);
                    ret = -4;
                    break;
                }
            }
        }
    }
    fx_file_close(&file);

done:
    (void)fx_file_delete(media, SD_SPEED_TEST_FILE);
    (void)fx_media_flush(media);
    sd_unlock();
    hal_mem_free(buf);

    if (ret == 0) {
        uint32_t ms;
        ms = t1 - t0;
        if (write_kbps != NULL) {
            *write_kbps = (ms > 0U) ? (uint32_t)(((written / 1024U) * 1000U) / ms) : 0U;
        }
        ms = t3 - t2;
        if (read_kbps != NULL) {
            *read_kbps = (ms > 0U) ? (uint32_t)(((read_bytes / 1024U) * 1000U) / ms) : 0U;
        }
    }
    return ret;
}

int sd_is_detected(void)
{
    return SD_IsDetected();
}
int sd_is_media_open(void)
{
    /* Non-blocking readiness check: true only after sdProcess has completed
     * fx_media_open(). Used to avoid mkdir/write attempts on a closed media. */
    return (g_sd.is_init == true && g_sd.media_status == MEDIA_OPENED) ? 1 : 0;
}

uint32_t sd_media_generation(void)
{
    return g_sd.media_generation;
}

int sd_wait_ready_for_open(uint32_t timeout_ms)
{
    if (g_sd.is_init == true && g_sd.media_status == MEDIA_OPENED) {
        return AICAM_OK;
    }
    if (sd_is_detected()) {
        while (g_sd.media_status != MEDIA_OPENED && timeout_ms > 0) {
            osDelay(5);
            if (timeout_ms > 5) timeout_ms -= 5;
            else return AICAM_ERROR_TIMEOUT;
        }
        return AICAM_OK;
    }
    return AICAM_ERROR_TIMEOUT;
}

int sd_init(void *priv)
{
    sd_t *sd = (sd_t *)priv;
    sd->mtx_id = osMutexNew(NULL);
    sd->sem_id = osSemaphoreNew(1, 0, NULL);
    sd_sem_rx = osSemaphoreNew(1, 0, NULL);
    sd_sem_tx = osSemaphoreNew(1, 0, NULL);
    sd->pwr_handle = pwr_manager_get_handle(PWR_TF_NAME);
    
    // MX_SDMMC1_SD_Init();
    /* EXTI interrupt init*/
    sd->media_status = MEDIA_CLOSED;
    sd->file_ops_handle = -1;
    sd->sd_processId = osThreadNew(sdProcess, sd, &sdTask_attributes);
    return 0;
}

static int sd_deinit(void *priv)
{
    sd_t *sd = (sd_t *)priv;

    sd->is_init = false;
    if(sd->media_status == MEDIA_OPENED){
        fx_media_close(&sd->sdio_disk);
    }

    osSemaphoreRelease(sd->sem_id);
    osDelay(100);

    if (sd->sd_processId != NULL) {
        osThreadTerminate(sd->sd_processId);
        sd->sd_processId = NULL;
    }

    if (sd->sem_id != NULL) {
        osSemaphoreDelete(sd->sem_id);
        sd->sem_id = NULL;
    }
    if (sd->mtx_id != NULL) {
        osMutexDelete(sd->mtx_id);
        sd->mtx_id = NULL;
    }

    if (sd_sem_rx != NULL) {
        osSemaphoreDelete(sd_sem_rx);
        sd_sem_rx = NULL;
    }
    if (sd_sem_tx != NULL) {
        osSemaphoreDelete(sd_sem_tx);
        sd_sem_tx = NULL;
    }

    if (sd->pwr_handle != 0) {
        pwr_manager_release(sd->pwr_handle);
        sd->pwr_handle = 0;
    }

    HAL_NVIC_DisableIRQ(EXTI0_IRQn);

    HAL_GPIO_DeInit(TF_INT_GPIO_Port, TF_INT_Pin);

    return 0;
}


int sd_register(void)
{
    static dev_ops_t sd_ops = {
        .init = sd_init,
        .deinit = sd_deinit
    };
    if(g_sd.is_init == true){
        return AICAM_ERROR_BUSY;
    }
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_sd.dev = dev;
    strcpy(dev->name, SD_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &sd_ops;
    dev->priv_data = &g_sd;

    device_register(g_sd.dev);
    return AICAM_OK;
}

int sd_unregister(void)
{
    if (g_sd.dev) {
        device_unregister(g_sd.dev);
        hal_mem_free(g_sd.dev);
        g_sd.dev = NULL;
    }
    return AICAM_OK;
}