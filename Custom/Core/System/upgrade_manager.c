#include "upgrade_manager.h"
#include "stm32n6xx_hal.h"
static upgrade_flash_read flash_read = NULL;
static upgrade_flash_write flash_write = NULL;
static upgrade_flash_erase flash_erase = NULL;

static SystemState g_sys_state __attribute__ ((aligned (32))) = {0};

static firmware_partition_t g_partitions[FIRMWARE_TYPE_COUNT] = {
    {FIRMWARE_FSBL, 0, FSBL_BASE - FLASH_BASE, FSBL_BASE - FLASH_BASE, FSBL_SIZE},
    {FIRMWARE_APP, APP_MAGIC, APP1_BASE - FLASH_BASE, APP2_BASE - FLASH_BASE, APP1_SIZE},
    {FIRMWARE_WEB, WEB_MAGIC, WEB_BASE - FLASH_BASE, WEB_BASE - FLASH_BASE, WEB_SIZE},
    {FIRMWARE_AI_1, AI_MAGIC, AI_1_BASE - FLASH_BASE, AI_1_BASE - FLASH_BASE, AI_1_SIZE},
    {FIRMWARE_AI_2, AI_MAGIC, AI_2_BASE - FLASH_BASE, AI_2_BASE - FLASH_BASE, AI_2_SIZE},
    // WiFi firmware: single-slot (A==B) at WIFI_FW_BASE. The OTA header is NOT
    // written to flash for WiFi (only flash_header_t + .rps), mirroring FSBL.
    {FIRMWARE_WIFI, WIFI_MAGIC, WIFI_FW_BASE - FLASH_BASE, WIFI_FW_BASE - FLASH_BASE, WIFI_FW_SIZE},
    {FIRMWARE_RESERVED2, 0, 0, 0, 0},
};

uint32_t crc32_checksum(uint32_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

void save_system_state(void) 
{
    if(!flash_write || !flash_erase) return;
    g_sys_state.crc32 = crc32_checksum((uint32_t *)&g_sys_state, (sizeof(SystemState) - sizeof(g_sys_state.crc32)) / 4);
    size_t erase_blocks = (sizeof(SystemState) + FLASH_BLK_SIZE - 1) / FLASH_BLK_SIZE;
    flash_erase(OTA_BASE - FLASH_BASE, erase_blocks);
    flash_write(OTA_BASE - FLASH_BASE, &g_sys_state, sizeof(SystemState));
}

void clean_system_state(void)
{
    if(!flash_write || !flash_erase) return;
    
    // Erase flash first
    size_t erase_blocks = (sizeof(SystemState) + FLASH_BLK_SIZE - 1) / FLASH_BLK_SIZE;
    flash_erase(OTA_BASE - FLASH_BASE, erase_blocks);
    
    // Initialize a valid system state structure (similar to init_system_state when invalid)
    memset(&g_sys_state, 0, sizeof(SystemState));
    g_sys_state.magic = SYS_MAGIC;
    
    // Initialize all firmware types with default values
    ota_header_t header;
    for(int i = 0; i < FIRMWARE_TYPE_COUNT; i++){
        // Set default active slot to SLOT_A
        g_sys_state.active_slot[i] = SLOT_A;
        
        // Initialize slot A to PENDING_VERIFICATION, slot B to IDLE
        g_sys_state.slot[i][SLOT_A].status = PENDING_VERIFICATION;
        g_sys_state.slot[i][SLOT_A].boot_success = 0;
        g_sys_state.slot[i][SLOT_A].try_count = 0;
        g_sys_state.slot[i][SLOT_A].firmware_size = 0;
        g_sys_state.slot[i][SLOT_A].crc32 = 0;
        memset(g_sys_state.slot[i][SLOT_A].version, 0, sizeof(g_sys_state.slot[i][SLOT_A].version));
        
        g_sys_state.slot[i][SLOT_B].status = IDLE;
        g_sys_state.slot[i][SLOT_B].boot_success = 0;
        g_sys_state.slot[i][SLOT_B].try_count = 0;
        g_sys_state.slot[i][SLOT_B].firmware_size = 0;
        g_sys_state.slot[i][SLOT_B].crc32 = 0;
        memset(g_sys_state.slot[i][SLOT_B].version, 0, sizeof(g_sys_state.slot[i][SLOT_B].version));
        
        // Try to read existing firmware header from slot A if flash_read is available
        if (flash_read && g_partitions[i].offset_A > 0) {
            if (flash_read(g_partitions[i].offset_A, (void *)&header, sizeof(ota_header_t)) == 0) {
                if (ota_header_verify(&header) == 0) {
                    g_sys_state.slot[i][SLOT_A].firmware_size = header.total_package_size;
                    g_sys_state.slot[i][SLOT_A].crc32 = header.fw_crc32;
                    memcpy(g_sys_state.slot[i][SLOT_A].version, header.fw_ver, sizeof(header.fw_ver));
                }
            }
        }
    }
    
    // Calculate and set CRC32
    g_sys_state.crc32 = crc32_checksum((uint32_t *)&g_sys_state, (sizeof(SystemState) - sizeof(g_sys_state.crc32)) / 4);
    
    // Save the initialized state to flash
    flash_write(OTA_BASE - FLASH_BASE, &g_sys_state, sizeof(SystemState));
}


void init_system_state(upgrade_flash_read read, upgrade_flash_write write, upgrade_flash_erase erase) 
{
    flash_read = read;
    flash_write = write;
    flash_erase = erase;

    if(flash_read){
        flash_read(OTA_BASE - FLASH_BASE, (void *)&g_sys_state, sizeof(SystemState));
    }

    if (g_sys_state.magic != SYS_MAGIC || g_sys_state.crc32 != crc32_checksum((uint32_t *)&g_sys_state, (sizeof(SystemState) - sizeof(g_sys_state.crc32)) / 4)) {
        memset(&g_sys_state, 0, sizeof(SystemState));
        g_sys_state.magic = SYS_MAGIC;
        ota_header_t header;
        for(int i = 0; i < FIRMWARE_TYPE_COUNT; i++){
            flash_read(g_partitions[i].offset_A, (void *)&header, sizeof(ota_header_t));
            g_sys_state.active_slot[i] = SLOT_A;
            g_sys_state.slot[i][SLOT_A].status = PENDING_VERIFICATION;
            g_sys_state.slot[i][SLOT_B].status = IDLE;

            if (ota_header_verify(&header) == 0) {
                g_sys_state.slot[i][SLOT_A].firmware_size = header.total_package_size;
                g_sys_state.slot[i][SLOT_A].crc32 = header.fw_crc32;
                memcpy(g_sys_state.slot[i][SLOT_A].version, header.fw_ver, sizeof(header.fw_ver));
            }
        }
        save_system_state();
    }
}

void verify_fsbl_version(const char *version)
{
    if (version == NULL) {
        return;
    }

    unsigned int parsed_ver[4] = {0};
    char fsbl_version[16] = {0};

    ota_version_to_string(
        g_sys_state.slot[FIRMWARE_FSBL][SLOT_A].version,
        fsbl_version, sizeof(fsbl_version)
    );

    if (strcmp(version, fsbl_version) == 0) {
        return;
    }

    int cnt = sscanf(
        version,
        "%u.%u.%u.%u%*s",
        &parsed_ver[0], &parsed_ver[1],
        &parsed_ver[2], &parsed_ver[3]
    );

    if (cnt != 4) {
        printf("verify_fsbl_version: invalid version format: %s\r\n", version);
        return;
    }

    memset(g_sys_state.slot[FIRMWARE_FSBL][SLOT_A].version, 0, sizeof(g_sys_state.slot[FIRMWARE_FSBL][SLOT_A].version));
    g_sys_state.slot[FIRMWARE_FSBL][SLOT_A].version[0] = parsed_ver[0] & 0xFF;
    g_sys_state.slot[FIRMWARE_FSBL][SLOT_A].version[1] = parsed_ver[1] & 0xFF;
    g_sys_state.slot[FIRMWARE_FSBL][SLOT_A].version[2] = parsed_ver[2] & 0xFF;
    g_sys_state.slot[FIRMWARE_FSBL][SLOT_A].version[3] = parsed_ver[3] & 0xFF;

    save_system_state();
}



SystemState *get_system_state(void) 
{
    return &g_sys_state;
}

uint32_t get_active_partition(FirmwareType type) 
{
    if(type >= FIRMWARE_TYPE_COUNT) return 0;
        if(g_sys_state.active_slot[type] == SLOT_A){
        return g_partitions[type].offset_A;
    }else{
        return g_partitions[type].offset_B;
    }
}

uint32_t get_update_partition(FirmwareType type) 
{
    if(type >= FIRMWARE_TYPE_COUNT) return 0;
    if(g_sys_state.active_slot[type] == SLOT_A){
        return g_partitions[type].offset_B;
    }else{
        return g_partitions[type].offset_A;
    }
}

int upgrade_begin(upgrade_handle_t *handle, FirmwareType type, firmware_header_t *header)
{
    if (!flash_erase || type >= FIRMWARE_TYPE_COUNT || !handle || !header) return -1;
    uint32_t update_offset = get_update_partition(type);
    if (update_offset == 0 && type != FIRMWARE_FSBL) return -1;

    size_t erase_blocks = (header->file_size + FLASH_BLK_SIZE - 1) / FLASH_BLK_SIZE;
    flash_erase(update_offset, erase_blocks);

    handle->type = type;
    handle->header = header;
    handle->base_offset = update_offset;
    handle->current_offset = 0;
    handle->total_size = header->file_size;
    return 0;
}

int upgrade_write_chunk(upgrade_handle_t *handle, const void *chunk_data, size_t chunk_size)
{
    if (!flash_write || !handle || !handle->header) return -1;

    if (handle->current_offset + chunk_size > handle->total_size) return -1;

    uint32_t write_offset = handle->base_offset + handle->current_offset;
    flash_write(write_offset, (void *)chunk_data, chunk_size);

    handle->current_offset += chunk_size;
    return 0;
}

int upgrade_finish(upgrade_handle_t *handle)
{
    if (!flash_read || !handle || !handle->header) return -1;

    handle->crc32 = 0xFFFFFFFF;
    // uint8_t buffer[1024];
    // uint32_t remain = handle->total_size;
    // uint32_t offset = handle->base_offset;
    // while (remain > 0) {
    //     size_t read_size = remain > sizeof(buffer) ? sizeof(buffer) : remain;
    //     flash_read(offset, buffer, read_size);
    //     handle->crc32 = handle->crc32 ^ crc32_checksum(buffer, read_size);
    //     offset += read_size;
    //     remain -= read_size;
    // }

    // if (handle->crc32 != handle->header->crc32) {
    //     return -1;
    // }

    FirmwareType type = handle->type;
    slot_info_t *slot = &g_sys_state.slot[type][(g_sys_state.active_slot[type] == SLOT_A) ? SLOT_B : SLOT_A];
    slot->status = PENDING_VERIFICATION;
    slot->boot_success = 0;
    slot->try_count = 0;
    memcpy(slot->version, &handle->header->version, sizeof(slot->version));
    slot->firmware_size = handle->header->file_size;
    slot->crc32 = handle->crc32;
    g_sys_state.active_slot[type] = (g_sys_state.active_slot[type] == SLOT_A) ? SLOT_B : SLOT_A;
    save_system_state();

    return 0;
}

int upgrade_read_begin(upgrade_handle_t *handle, FirmwareType type, int slot_idx)
{
    if(!flash_read || type >= FIRMWARE_TYPE_COUNT || !handle || !handle->header) return -1;
    handle->type = type;
    if(slot_idx == SLOT_A){
        handle->base_offset = g_partitions[type].offset_A;
    }else if(slot_idx == SLOT_B){
        handle->base_offset = g_partitions[type].offset_B;
    }
    handle->current_offset = 0;
    handle->total_size = g_sys_state.slot[type][slot_idx].firmware_size;
    handle->crc32 = g_sys_state.slot[type][slot_idx].crc32;
    // handle->header->magic = APP_MAGIC;
    memcpy(handle->header->version, g_sys_state.slot[type][slot_idx].version, sizeof(handle->header->version));
    handle->header->file_size = handle->total_size;
    handle->header->crc32 = handle->crc32;
    if(handle->total_size == 0){
        handle->total_size = g_partitions[type].default_size;
    }else if(handle->total_size > g_partitions[type].default_size){
        return -1;
    }
    return 0;
}

uint32_t upgrade_read_chunk(upgrade_handle_t *handle, void *buffer, size_t read_size)
{
    if (!flash_read || !handle || !handle->header || !buffer) return 0;

    if (handle->current_offset >= handle->total_size) return 0;
    if (handle->current_offset + read_size > handle->total_size) {
        read_size = handle->total_size - handle->current_offset;
    }

    flash_read(handle->base_offset + handle->current_offset, buffer, read_size);
    handle->current_offset += read_size;

    return read_size;
}

uint32_t get_slot_try_count(FirmwareType type)
{
    if (type >= FIRMWARE_TYPE_COUNT) return 0;
    uint8_t active = g_sys_state.active_slot[type];
    return g_sys_state.slot[type][active].try_count;
}

void set_slot_boot_success(FirmwareType type, bool success)
{
    if (type >= FIRMWARE_TYPE_COUNT) return;
    uint8_t active = g_sys_state.active_slot[type];
    if(g_sys_state.slot[type][active].status == ACTIVE) return;

    if(success){
        g_sys_state.slot[type][active].boot_success += 1;
    }

    if(g_sys_state.slot[type][active].boot_success >= 3){
        g_sys_state.slot[type][active].status = ACTIVE;
        g_sys_state.slot[type][active].boot_success = 1;
    }
    save_system_state();
}

int switch_active_slot(FirmwareType type)
{
    if (type >= FIRMWARE_TYPE_COUNT) return -1;
    uint8_t current = g_sys_state.active_slot[type];
    uint8_t next = (current == SLOT_A) ? SLOT_B : SLOT_A;

    SlotStatus status = g_sys_state.slot[type][next].status;
    if (status == PENDING_VERIFICATION || status == ACTIVE) {
        g_sys_state.active_slot[type] = next;
        save_system_state();
        return 0;
    }
    return -1;
}

int check_and_select_boot_slot(FirmwareType type)
{
    if (type >= FIRMWARE_TYPE_COUNT) return -1;

    uint8_t active = g_sys_state.active_slot[type];
    uint8_t other = (active == SLOT_A) ? SLOT_B : SLOT_A;

    slot_info_t *slot = &g_sys_state.slot[type][active];
    slot_info_t *other_slot = &g_sys_state.slot[type][other];

    switch (slot->status) {
        case PENDING_VERIFICATION:
            slot->try_count++;
            if (slot->try_count > MAX_BOOT_TRY) {
                slot->status = UNBOOTABLE;
                save_system_state();
                if (other_slot->status == PENDING_VERIFICATION || other_slot->status == ACTIVE) {
                    g_sys_state.active_slot[type] = other;
                    if(other_slot->status == PENDING_VERIFICATION)
                        other_slot->try_count = 1;
                    save_system_state();
                    return 0;
                }
                return -1;
            }
            save_system_state();
            return 0;

        case ACTIVE:
            return 0;

        default:
            if (other_slot->status == PENDING_VERIFICATION || other_slot->status == ACTIVE) {
                g_sys_state.active_slot[type] = other;
                if(other_slot->status == PENDING_VERIFICATION)
                    other_slot->try_count = 1;
                save_system_state();
                return 0;
            }
            return -1;
    }
}
