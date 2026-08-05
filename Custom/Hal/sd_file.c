#include "sd_file.h"
#include "debug.h"
#include "sdmmc.h"
#include "exti.h"
#include "common_utils.h"
#include "drtc.h"
#include "mem_map.h"

#define SD_CHUNK_SIZE           4096
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

    /* Resolve UTF-8 path for FileX */
    char fx_path[FX_MAX_LONG_NAME_LEN];
    if (sd_resolve_file_path(media, path, fx_path, sizeof(fx_path)) != 0) {
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

    sd_lock();
    if (reading && !plus) { // "r"
        open_mode = FX_OPEN_FOR_READ;
        status = fx_file_open(media, file, fx_path, open_mode);
    } else if (reading && plus) { // "r+"
        open_mode = FX_OPEN_FOR_READ | FX_OPEN_FOR_WRITE;
        status = fx_file_open(media, file, fx_path, open_mode);
    } else if (writing && !appending && !plus) { // "w"
        fx_file_delete(media, fx_path);
        UINT create_status = fx_file_create(media, fx_path);
        if (create_status != FX_SUCCESS && create_status != FX_ALREADY_CREATED) {
            sd_unlock();
            LOG_DRV_ERROR("fx_file_create failed: 0x%02X path=%s\r\n", create_status, path);
            hal_mem_free(file);
            return NULL;
        }
        open_mode = FX_OPEN_FOR_WRITE;
        status = fx_file_open(media, file, fx_path, open_mode);
    } else if (writing && !appending && plus) { // "w+"
        fx_file_delete(media, fx_path);
        fx_file_create(media, fx_path);
        open_mode = FX_OPEN_FOR_READ | FX_OPEN_FOR_WRITE;
        status = fx_file_open(media, file, fx_path, open_mode);
    } else if (appending && !plus) { // "a"
        status = fx_file_open(media, file, fx_path, FX_OPEN_FOR_WRITE);
        if (status != FX_SUCCESS) {
            /* File doesn't exist - create then open. Only re-open if the first
             * open FAILED; if it succeeded the FX_FILE is already open and a
             * second fx_file_open on it errors out (the "first OK, second
             * fails" bug). */
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
    if (sd_resolve_file_path(media, path, fx_path, sizeof(fx_path)) != 0) return -1;
    sd_lock();
    UINT status = fx_file_delete(media, fx_path);
    if (status == FX_SUCCESS) {
        UINT fs = fx_media_flush(media);
        if (fs != FX_SUCCESS) LOG_DRV_ERROR("fx_media_flush after delete failed: 0x%02X\r\n", fs);
    }
    sd_unlock();
    if (status != FX_SUCCESS) {
        LOG_DRV_ERROR("fx_file_delete failed: 0x%02X, path=%s\r\n", status, path);
        return -(int)status;
    }
    return 0;
}

// Rename file (FileX fx_file_rename does NOT overwrite - remove target first)
int sd_filex_rename(void *context, const char *oldpath, const char *newpath)
{
    FX_MEDIA *media = (FX_MEDIA*)context;
    char fx_old[FX_MAX_LONG_NAME_LEN], fx_new[FX_MAX_LONG_NAME_LEN];
    if (sd_resolve_file_path(media, oldpath, fx_old, sizeof(fx_old)) != 0) return -1;
    if (sd_resolve_file_path(media, newpath, fx_new, sizeof(fx_new)) != 0) return -1;
    sd_lock();
    (void)fx_file_delete(media, fx_new);   /* remove target if exists (ignore NOT_FOUND) */
    UINT status = fx_file_rename(media, fx_old, fx_new);
    if (status == FX_SUCCESS) {
        UINT fs = fx_media_flush(media);
        if (fs != FX_SUCCESS) LOG_DRV_ERROR("fx_media_flush after rename failed: 0x%02X\r\n", fs);
    } else {
        LOG_DRV_ERROR("fx_file_rename failed: 0x%02X, old=%s, new=%s\r\n", status, oldpath, newpath);
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
        ULONG utf16_bytes = 0;
        const char *s = comp;
        while (*s && utf16_bytes + 1 < sizeof(utf16)) {
            UCHAR c = (UCHAR)*s;
            ULONG cp;
            if (c < 0x80) { cp = c; s++; }
            else if (c < 0xE0) { cp = ((c & 0x1FUL) << 6) | (*(s+1) & 0x3F); s += 2; }
            else { cp = ((c & 0x0FUL) << 12) | ((ULONG)(*(s+1) & 0x3F) << 6) | (*(s+2) & 0x3F); s += 3; }
            utf16[utf16_bytes++] = (UCHAR)(cp & 0xFF);
            utf16[utf16_bytes++] = (UCHAR)(cp >> 8);
        }
        ULONG char_count = utf16_bytes / 2;
        utf16[utf16_bytes] = 0;
        utf16[utf16_bytes + 1] = 0;

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
 *        Output is a FileX-compatible absolute path.
 */
static int sd_resolve_file_path(FX_MEDIA *media, const char *path, char *fx_out, size_t out_sz)
{
    /* Split into parent directory and filename */
    char parent[FX_MAX_LONG_NAME_LEN];
    const char *fname = strrchr(path, '/');
    if (!fname) fname = strrchr(path, '\\');
    if (fname) {
        size_t plen = fname - path;
        if (plen >= sizeof(parent)) plen = sizeof(parent) - 1;
        memcpy(parent, path, plen);
        parent[plen] = '\0';
        fname++;  /* skip the separator */
    } else {
        /* No parent - file in current/root directory */
        parent[0] = '\0';
        fname = path;
    }

    /* Resolve parent directory */
    char resolved_parent[FX_MAX_LONG_NAME_LEN];
    if (sd_resolve_dir_path(media, (parent[0] == '\0') ? "/" : parent,
                            resolved_parent, sizeof(resolved_parent)) != 0)
        return -1;

    /* Build full path: parent\filename */
    size_t pl = strlen(resolved_parent);
    size_t fl = strlen(fname);
    if (pl + 1 + fl >= out_sz) return -1;
    memcpy(fx_out, resolved_parent, pl);
    if (pl > 0 && resolved_parent[pl-1] != '\\')
        fx_out[pl++] = '\\';
    memcpy(fx_out + pl, fname, fl + 1);  /* includes '\0' */

    return 0;
}

void* sd_filex_opendir(void *context, const char *path)
{
    FX_MEDIA *media = (FX_MEDIA*)context;
    filex_dir_t *dir = (filex_dir_t*)hal_mem_alloc_fast(sizeof(filex_dir_t));
    if (!dir) return NULL;
    dir->media = media;

    char fx_path[FX_MAX_LONG_NAME_LEN];
    if (sd_resolve_dir_path(media, path, fx_path, sizeof(fx_path)) != 0) {
        hal_mem_free(dir);
        return NULL;
    }
    /* sd_resolve_dir_path restores old default - re-set to target for readdir */
    fx_directory_default_set(media, fx_path);

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

    if (status != FX_SUCCESS) {
        dir->finished = 1;
        sd_unlock();
        return 0;
    }

    // Fill info - only call fx_unicode_name_get for non-ASCII names
    // (fx_unicode_name_get does O(N) directory scan - skip it for ASCII to avoid O(N²))
    sd_info->name[0] = '\0';
    {
        bool need_unicode = false;
        for (const char *p = dir->entry_name; *p; p++) {
            if ((unsigned char)*p > 0x7F) { need_unicode = true; break; }
        }

        if (need_unicode) {
            UCHAR unicode_name[FX_MAX_LONG_NAME_LEN * 2];
            ULONG unicode_len = 0;
            UINT uni_status = fx_unicode_name_get(dir->media, dir->entry_name,
                                                   unicode_name, &unicode_len);
            if (uni_status == FX_SUCCESS && unicode_len > 0 && unicode_len * 2 <= sizeof(unicode_name)) {
                // Convert UTF-16LE to UTF-8 for sd_info->name
                size_t out = 0;
            for (ULONG u = 0; u < unicode_len && out + 3 < FX_MAX_LONG_NAME_LEN; u++) {
                ULONG cp = unicode_name[u * 2] | ((ULONG)unicode_name[u * 2 + 1] << 8);
                if (cp < 0x80) {
                    sd_info->name[out++] = (char)cp;
                } else if (cp < 0x800) {
                    sd_info->name[out++] = (char)(0xC0 | (cp >> 6));
                    sd_info->name[out++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    sd_info->name[out++] = (char)(0xE0 | (cp >> 12));
                    sd_info->name[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    sd_info->name[out++] = (char)(0x80 | (cp & 0x3F));
                }
            }
            sd_info->name[out] = '\0';
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
    /* Reset default to root so later ops start fresh */
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
    char fx_path[FX_MAX_LONG_NAME_LEN];
    if (sd_resolve_file_path(media, path, fx_path, sizeof(fx_path)) != 0) return -1;
    sd_lock();
    UINT status = fx_directory_create(media, fx_path);
    if (status == FX_SUCCESS) {
        UINT fs = fx_media_flush(media);
        if (fs != FX_SUCCESS) LOG_DRV_ERROR("fx_media_flush after mkdir failed: 0x%02X\r\n", fs);
    }
    sd_unlock();
    return (status == FX_SUCCESS) ? 0 : -1;
}

int sd_filex_stat(void *context, const char *path, struct stat *st)
{
    FX_MEDIA *media = (FX_MEDIA*)context;
    char fx_path[FX_MAX_LONG_NAME_LEN];
    if (sd_resolve_file_path(media, path, fx_path, sizeof(fx_path)) != 0) return -1;
    UINT attributes;
    ULONG size;
    UINT year, month, day, hour, minute, second;
    UINT status;

    if (!media || !path || !st) {
        return -1;
    }

    sd_lock();
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
            sd->media_status = MEDIA_OPENED;
            sd->file_ops_handle = file_ops_register(FS_SD, &sd_file_ops, &sd->sdio_disk);
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
                    sd->media_status = MEDIA_OPENED;
                    sd->mode = SD_MODE_NORMAL;
                    if(sd->file_ops_handle == -1){
                        sd->file_ops_handle = file_ops_register(FS_SD, &sd_file_ops, &sd->sdio_disk);
                        LOG_DRV_DEBUG("SD file system register. :%d \r\n", sd->file_ops_handle);
                        file_ops_switch(sd->file_ops_handle);
                    }
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
        g_sd.media_status = MEDIA_OPENED;
        g_sd.mode = SD_MODE_NORMAL;
        if(g_sd.file_ops_handle == -1){
            g_sd.file_ops_handle = file_ops_register(FS_SD, &sd_file_ops, &g_sd.sdio_disk);
            LOG_DRV_DEBUG("SD file system register. :%d \r\n", g_sd.file_ops_handle);
            file_ops_switch(g_sd.file_ops_handle);
        }
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