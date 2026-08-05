/***************************************************************************/ /**
    * @file
    * @brief WLAN Throughput Example Application
    *******************************************************************************
    * # License
    * <b>Copyright 2022 Silicon Laboratories Inc. www.silabs.com</b>
    *******************************************************************************
    *
    * SPDX-License-Identifier: Zlib
    *
    * The licensor of this software is Silicon Laboratories Inc.
    *
    * This software is provided 'as-is', without any express or implied
    * warranty. In no event will the authors be held liable for any damages
    * arising from the use of this software.
    *
    * Permission is granted to anyone to use this software for any purpose,
    * including commercial applications, and to alter it and redistribute it
    * freely, subject to the following restrictions:
    *
    * 1. The origin of this software must not be misrepresented; you must not
    *    claim that you wrote the original software. If you use this software
    *    in a product, an acknowledgment in the product documentation would be
    *    appreciated but is not required.
    * 2. Altered source versions must be plainly marked as such, and must not be
    *    misrepresented as being the original software.
    * 3. This notice may not be removed or altered from any source distribution.
    *
  ******************************************************************************/
#include <stdint.h>
#include <string.h>
#include "main.h"
#include "misc.h"
#include "sl_wifi_callback_framework.h"
#include "sl_net_wifi_types.h"
#include "sl_wifi.h"
#include "sl_net.h"
#include "sl_rsi_utility.h"
#include "wifi.h"
#include "debug.h"
#include "generic_utils.h"
#include "generic_file.h"
#include "common_utils.h"
#include "lwip/sockets.h"
#include <errno.h>  // For ENOBUFS
#include "mem.h"
#include "storage.h"
#include "crc.h"


/******************************************************
 *                      Macros
 ******************************************************/

// Memory length for send buffer
#define MAX_TCP_SIZE 1024
#define MAX_SEND_SIZE 1024

#define SERVER_IP "192.168.10.10"

#define LISTENING_PORT 5005
#define BACK_LOG       1


/******************************************************
*                    Constants
******************************************************/
/******************************************************

*               Variable Definitions
******************************************************/
static uint8_t wifi_tread_stack[1024 * 6] ALIGN_32 IN_PSRAM;
const osThreadAttr_t wifiTask_attributes = {
    .name = "uvcTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_mem = wifi_tread_stack,
    .stack_size = sizeof(wifi_tread_stack),
};

/************************** ncp Transmit Test Configuration ****************************/
static const sl_wifi_device_configuration_t transmit_test_configuration = {
  .boot_option = LOAD_NWP_FW,
  .mac_address = NULL,
  .band        = SL_SI91X_WIFI_BAND_2_4GHZ,
  .region_code = WORLD_DOMAIN,
  .boot_config = { .oper_mode = SL_SI91X_TRANSMIT_TEST_MODE,
                   .coex_mode = SL_SI91X_WLAN_ONLY_MODE,
                   .feature_bit_map =
#ifdef SLI_SI91X_MCU_INTERFACE
                     (SL_SI91X_FEAT_SECURITY_OPEN | SL_SI91X_FEAT_WPS_DISABLE),
#else
                     (SL_SI91X_FEAT_SECURITY_OPEN),
#endif
                   .tcp_ip_feature_bit_map =
                     (SL_SI91X_TCP_IP_FEAT_DHCPV4_CLIENT | SL_SI91X_TCP_IP_FEAT_EXTENSION_VALID),
                   .custom_feature_bit_map     = SL_SI91X_CUSTOM_FEAT_EXTENTION_VALID,
                   .ext_custom_feature_bit_map = (MEMORY_CONFIG
#if defined(SLI_SI917) || defined(SLI_SI915)
                                                  | SL_SI91X_EXT_FEAT_FRONT_END_SWITCH_PINS_ULP_GPIO_4_5_0
#endif
                                                  ),
                   .bt_feature_bit_map         = SL_SI91X_BT_RF_TYPE,
                   .ext_tcp_ip_feature_bit_map = SL_SI91X_CONFIG_FEAT_EXTENTION_VALID,
                   .ble_feature_bit_map        = 0,
                   .ble_ext_feature_bit_map    = 0,
                   .config_feature_bit_map     = SL_SI91X_FEAT_SLEEP_GPIO_SEL_BITMAP }
};

static int wifi_ant_flag = 0;
static int wifi_update_flag = 0;
static uint32_t wifi_update_times = 0;
const sl_wifi_data_rate_t rate               = SL_WIFI_DATA_RATE_6;
const sl_si91x_request_tx_test_info_t default_tx_test_info = {
  .enable      = 1,
  .power       = 127,
  .rate        = rate,
  .length      = 100,
  .mode        = 0,
  .channel     = 1,
  .aggr_enable = 0,
#if defined(SLI_SI917) || defined(SLI_SI915)
  .enable_11ax            = 0,
  .coding_type            = 0,
  .nominal_pe             = 0,
  .ul_dl                  = 0,
  .he_ppdu_type           = 0,
  .beam_change            = 0,
  .bw                     = 0,
  .stbc                   = 0,
  .tx_bf                  = 0,
  .gi_ltf                 = 0,
  .dcm                    = 0,
  .nsts_midamble          = 0,
  .spatial_reuse          = 0,
  .bss_color              = 0,
  .he_siga2_reserved      = 0,
  .ru_allocation          = 0,
  .n_heltf_tot            = 0,
  .sigb_dcm               = 0,
  .sigb_mcs               = 0,
  .user_sta_id            = 0,
  .user_idx               = 0,
  .sigb_compression_field = 0,
#endif
};
/************************** ncp Transmit Test Configuration end****************************/

static const sl_wifi_device_configuration_t firmware_update_configuration = {
#if (FW_UPDATE_TYPE == NWP_FW_UPDATE)
  .boot_option = BURN_NWP_FW,
#else
  .boot_option = BURN_M4_FW,
#endif
  .mac_address = NULL,
  .band        = SL_SI91X_WIFI_BAND_2_4GHZ,
  .region_code = US,
  .boot_config = { .oper_mode              = SL_SI91X_CLIENT_MODE,
                   .coex_mode              = SL_SI91X_WLAN_ONLY_MODE,
                   .feature_bit_map        = (SL_SI91X_FEAT_SECURITY_PSK | SL_SI91X_FEAT_AGGREGATION),
                   .tcp_ip_feature_bit_map = (SL_SI91X_TCP_IP_FEAT_DHCPV4_CLIENT),
                   .custom_feature_bit_map = (SL_SI91X_CUSTOM_FEAT_EXTENTION_VALID),
                   .ext_custom_feature_bit_map =
                     (SL_SI91X_EXT_FEAT_XTAL_CLK | SL_SI91X_EXT_FEAT_UART_SEL_FOR_DEBUG_PRINTS | MEMORY_CONFIG
                      | SL_SI91X_EXT_FEAT_FRONT_END_SWITCH_PINS_ULP_GPIO_4_5_0
                      ),
                   .bt_feature_bit_map         = 0,
                   .ext_tcp_ip_feature_bit_map = SL_SI91X_CONFIG_FEAT_EXTENTION_VALID,
                   .ble_feature_bit_map        = 0,
                   .ble_ext_feature_bit_map    = 0,
                   .config_feature_bit_map     = 0 }
};

si91x_wlan_app_cb_t si91x_wlan_app_cb;
uint32_t chunk_cnt = 0u, chunk_check = 0u, offset = 0u, fw_image_size = 0u;
uint8_t recv_buffer[SI91X_CHUNK_SIZE] ALIGN_32 UNCACHED;
sl_wifi_firmware_version_t fw_version = {0};
uint8_t one_time = 1;
volatile uint32_t remaining_bytes = 0u;
uint32_t t_start = 0;
uint32_t t_end = 0;
uint32_t xfer_time = 0;
int tcp_socket = -1;

/******************************************************
*               Function Declarations
******************************************************/

static void data_send(uint8_t * buffer, uint32_t len)
{
    ssize_t sent_bytes;
    uint32_t total_sent = 0;

    if (tcp_socket != -1) {
        while (total_sent < len) {
            uint32_t send_len = (len - total_sent) > MAX_SEND_SIZE ? MAX_SEND_SIZE : (len - total_sent);
            sent_bytes = send(tcp_socket, buffer + total_sent, send_len, 0);

            if (sent_bytes < 0) {
                if (errno == ENOBUFS) {
                    osDelay(1); // 1ms
                    continue;
                } else {
                    printf("\r\nSocket send failed with bsd error: %d\r\n", errno);
                    sent_bytes = -1;
                    close(tcp_socket);
                    break;
                }
            } else if (sent_bytes == 0) {
                printf("\r\nSocket closed by peer\r\n");
                tcp_socket = -1;
                close(tcp_socket);
                break;
            } else {
                total_sent += sent_bytes;
            }
        }
    }
}

__attribute__((unused)) static void tcpConsleProcess(void *argument)
{
    int server_socket = -1, client_socket = -1;
    int socket_return_value           = 0;
    struct sockaddr_in server_address = { 0 };
    socklen_t socket_length           = sizeof(struct sockaddr_in);
    uint8_t *data_buffer;

    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket < 0) {
        printf("\r\nSocket creation failed with bsd error: %d\r\n", errno);
        return;
    }
    printf("\r\nServer Socket ID : %d\r\n", server_socket);

    server_address.sin_family = AF_INET;
    server_address.sin_port   = LISTENING_PORT;
    sl_net_inet_addr(SERVER_IP, &server_address.sin_addr.s_addr);

    socket_return_value = bind(server_socket, (struct sockaddr *)&server_address, socket_length);
    if (socket_return_value < 0) {
        printf("\r\nSocket bind failed with bsd error: %d\r\n", errno);
        close(server_socket);
        return;
    }

    socket_return_value = listen(server_socket, BACK_LOG);
    if (socket_return_value < 0) {
        printf("\r\nSocket listen failed with bsd error: %d\r\n", errno);
        close(server_socket);
        return;
    }
    printf("\r\nListening on Local Port : %d\r\n", LISTENING_PORT);
    debug_output_register((log_custom_output_func_t)data_send);
    while (1) {
        client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0) {
            printf("\r\nSocket accept failed with bsd error: %d\r\n", errno);
            osDelay(1);
            continue;
        }
        data_buffer = hal_mem_alloc_fast(MAX_TCP_SIZE);
        tcp_socket = client_socket;
        
        printf("\r\n[Echo] Client connected. Socket: %d\r\n", client_socket);

        int n = 1;
        while (n  > 0) {
            n = recv(client_socket, data_buffer, MAX_TCP_SIZE, 0);
            int sent = 0, total = 0;
            if(n > 0){
                for (uint16_t i = 0; i < n; i++) {
                    printf("%c", ((uint8_t *)data_buffer)[i]);
                    debug_cmdline_input(((uint8_t *)data_buffer)[i]);
                }
            }
            while (total < n) {
                sent = send(client_socket, data_buffer + total, n - total, 0);
                if (sent <= 0) break;
                total += sent;
            }
            osDelay(1);
        }
        printf("[Echo] Client disconnected.\r\n");
        if(data_buffer != NULL){
            hal_mem_free(data_buffer);
        }
        tcp_socket = -1;
        close(client_socket);

    }
    close(server_socket);
    osThreadExit();
}


static uint32_t get_fw_size(char *buffer)
{
  fwreq_t *fw = (fwreq_t *)buffer;
  return fw->image_size;
}

static int32_t sl_si91x_app_task_fw_update_via_xmodem(uint8_t *rx_data, uint32_t size)
{
    UNUSED_PARAMETER(size);
    int32_t status = SL_STATUS_OK;
    switch (si91x_wlan_app_cb.state) {
        case SI91X_WLAN_INITIAL_STATE:
        case SI91X_WLAN_FW_UPGRADE: {
            if (one_time == 1) {
                // Initial setup (only executed once)
                fw_image_size = get_fw_size((char *)rx_data);
                remaining_bytes = fw_image_size;
                
                chunk_check = (fw_image_size + FW_HEADER_SIZE + SI91X_CHUNK_SIZE - 1) / SI91X_CHUNK_SIZE;
                one_time = 0;
                LOG_SIMPLE("Firmware upgrade started. Total chunks: %lu\r\n", chunk_check);
            }

            if (chunk_cnt >= chunk_check) {
                break;
            }

            // Set transfer mode based on data block position
            uint32_t transfer_mode;
            if (chunk_cnt == 0) {
                transfer_mode = SI91X_START_OF_FILE;
            } else if (chunk_cnt == (chunk_check - 1)) {
                transfer_mode = SI91X_END_OF_FILE;
            } else {
                transfer_mode = SI91X_IN_BETWEEN_FILE;
            }

            // Execute firmware upgrade transfer
            status = sl_si91x_bl_upgrade_firmware(rx_data, SI91X_CHUNK_SIZE, transfer_mode);
            if (status != SL_STATUS_OK) {
                LOG_SIMPLE("ERROR at chunk %lu: 0x%lx\r\n", chunk_cnt, status);
                return status;
            }

            // Update status counter
            offset += SI91X_CHUNK_SIZE;
            chunk_cnt++;
            
            // Transfer completion handling
            if (chunk_cnt == chunk_check) {
                LOG_SIMPLE("\r\nFirmware upgrade completed\r\n");
                si91x_wlan_app_cb.state = SI91X_WLAN_FW_UPGRADE_DONE;
            }
            break;
        }
        case SI91X_WLAN_FW_UPGRADE_DONE: {
            status = sl_net_init(SL_NET_WIFI_CLIENT_INTERFACE, NULL, NULL, NULL);
            if (status != SL_STATUS_OK) {
                return status;
            }

            status = sl_wifi_get_firmware_version(&fw_version);
            if (status == SL_STATUS_OK) {
                LOG_SIMPLE("New firmware version: ");
                print_firmware_version(&fw_version);
            }

            t_end = osKernelGetTickCount();
            xfer_time = t_end - t_start;
            uint32_t secs = xfer_time / 1000;
            LOG_SIMPLE("\r\nFirmware upgrade time: %d seconds\r\n", (int)secs);
            LOG_SIMPLE("\r\nDEMO COMPLETED\r\n");

            sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
            break;
        }
        default:
            break;
    }
    return status;
}

static int firmware_upgrade_from_file(const char *file_path)
{
    void *fd = NULL;
    size_t read_size;
    int32_t status = SL_STATUS_OK;
    uint32_t total_chunks;
    uint32_t file_remaining;
    uint32_t progress_percent = 0;
    uint32_t last_reported_percent = 0;
    uint32_t total_size = 0;

    printf("\n[FW UPGRADE] Starting firmware upgrade from file: %s\r\n", file_path);

    fd = file_fopen(file_path, "rb");
    if (fd == NULL) {
        printf("[ERROR] Failed to open firmware file\r\n");
        return -1;
    }

    read_size = file_fread(fd, recv_buffer, SI91X_CHUNK_SIZE);
    if (read_size == 0) {
        printf("[ERROR] Failed to read firmware header\r\n");
        file_fclose(fd);
        return -1;
    }

    chunk_cnt = 0;
    offset = 0;
    one_time = 1;
    si91x_wlan_app_cb.state = SI91X_WLAN_FW_UPGRADE;

    t_start = osKernelGetTickCount();
    printf("[TIMER] Firmware upgrade started at tick: %lu\r\n", t_start);

    fw_image_size = get_fw_size((char *)recv_buffer);
    total_size = fw_image_size + FW_HEADER_SIZE;
    printf("\n[FIRMWARE] Firmware details:\r\n");
    printf("  - Header size: %ld bytes\r\n", FW_HEADER_SIZE);
    printf("  - Payload size: %lu bytes\r\n", fw_image_size);
    printf("  - Total size: %lu bytes\r\n", total_size);
    printf("  - Chunk size: %ld bytes\r\n", SI91X_CHUNK_SIZE);

    total_chunks = (total_size + SI91X_CHUNK_SIZE - 1) / SI91X_CHUNK_SIZE;
    file_remaining = total_size - read_size;
    printf("  - Total chunks: %lu\n", total_chunks);
    printf("  - Remaining bytes: %lu\n", file_remaining);
    printf("\r\n[PROGRESS] Starting firmware transmission...\r\n");

    printf("\r\n[BLOCK 0] Sending header block (START_OF_FILE)\r\n");
    status = sl_si91x_app_task_fw_update_via_xmodem(recv_buffer, SI91X_CHUNK_SIZE);
    if (status != SL_STATUS_OK) {
        printf("[ERROR] First chunk processing failed: 0x%lx\r\n", status);
        file_fclose(fd);
        return -1;
    }
    printf("[SUCCESS] Header block sent\r\n");

    for (uint32_t i = 1; i < total_chunks; i++) {
        memset(recv_buffer, 0, SI91X_CHUNK_SIZE);
        size_t bytes_to_read = (file_remaining > SI91X_CHUNK_SIZE) ? 
                              SI91X_CHUNK_SIZE : file_remaining;
        
        read_size = file_fread(fd, recv_buffer, bytes_to_read);
        if (read_size == 0) {
            printf("[ERROR] File read failed at chunk %lu\r\n", i);
            break;
        }
        
        file_remaining -= read_size;
        
        progress_percent = (i * 100) / total_chunks;
        if (progress_percent != last_reported_percent && 
            (progress_percent % 10 == 0 || i == total_chunks - 1)) {
            printf("\n[PROGRESS] %ld%% complete (%lu/%lu chunks)\r\n", 
                   progress_percent, i, total_chunks);
            last_reported_percent = progress_percent;
        }
        

        if (i == (total_chunks - 1)) {
            printf("\n[BLOCK %lu] Sending final block (END_OF_FILE, %zu bytes)\r\n", i, read_size);
        } else {
            printf("\n[BLOCK %lu] Sending data block (%zu bytes)\r\n", i, read_size);
        }

        status = sl_si91x_app_task_fw_update_via_xmodem(recv_buffer, SI91X_CHUNK_SIZE);
        if (status != SL_STATUS_OK) {
            printf("[ERROR] Chunk %lu processing failed: 0x%lx\r\n", i, status);
            break;
        }
        
        printf("[SUCCESS] Block %lu processed\r\n", i);
        osDelay(10);
    }

    // Close file
    file_fclose(fd);
    printf("[FILE] Firmware file closed\r\n");

    // Check if completion state needs to be triggered
    if (si91x_wlan_app_cb.state == SI91X_WLAN_FW_UPGRADE_DONE) {
        printf("\n[UPGRADE] Triggering final upgrade state\r\n");
        return sl_si91x_app_task_fw_update_via_xmodem(NULL, 0);
    }
    return -1;
}

static int firmware_upgrade_from_flash(void)
{
    flash_header_t *flash_header;
    fwreq_t *fw_header;
    const uint8_t *flash_addr;
    uint32_t calculated_crc;
    uint32_t total_chunks;
    uint32_t remaining_bytes;
    uint32_t progress_percent = 0;
    uint32_t last_reported_percent = 0;
    int32_t status = SL_STATUS_OK;
    uint32_t total_size;
    uint32_t crc_data_size;
    const uint8_t *crc_data_ptr;

    printf("\n[FW UPGRADE] Starting firmware upgrade from flash\r\n");

    // Direct memory access via memory mapping
    storage_lock();
    flash_addr = (const uint8_t *)WIFI_FLASH_BASE_ADDR;
    
    printf("[FLASH] WiFi FW base address: 0x%08lX\r\n", (unsigned long)WIFI_FLASH_BASE_ADDR);

    // Step 1: Read flash header (direct memory access)
    flash_header = (flash_header_t *)flash_addr;

    printf("[FLASH HEADER] Valid flags: 0x%08lX, Total size: %lu, CRC: 0x%08lX\r\n",
           (unsigned long)flash_header->valid_flags,
           (unsigned long)flash_header->fw_total_size,
           (unsigned long)flash_header->fw_crc);

    // Step 2: Check if flash header is valid
    if (flash_header->valid_flags != WIFI_FLASH_VALID_FLAGS) {
        printf("[ERROR] Invalid flash header flags: 0x%08lX (expected: 0x%08lX)\r\n",
               (unsigned long)flash_header->valid_flags,
               (unsigned long)WIFI_FLASH_VALID_FLAGS);
        storage_unlock();
        return -1;
    }

    // Step 3: Validate total size
    if (flash_header->fw_total_size == 0 || flash_header->fw_total_size > (4 * 1024 * 1024)) {
        printf("[ERROR] Invalid firmware total size: %lu\r\n", flash_header->fw_total_size);
        storage_unlock();
        return -1;
    }

    // Step 4: Read WiFi firmware header (direct memory access, after WiFi flash header)
    fw_header = (fwreq_t *)(flash_addr + WIFI_FLASH_HEADER_SIZE);

    printf("[WIFI HEADER] Image size: %lu, FW version: 0x%08lX\r\n",
           fw_header->image_size, fw_header->fw_version);

    // Step 5: Verify total size matches (FW_HEADER_SIZE + image_size == fw_total_size)
    total_size = FW_HEADER_SIZE + fw_header->image_size;
    if (total_size != flash_header->fw_total_size) {
        printf("[ERROR] Size mismatch: FW header+image=%lu, Flash header=%lu\r\n",
               total_size, flash_header->fw_total_size);
        storage_unlock();
        return -1;
    }

    // Step 6: Calculate CRC using hardware CRC (for data from WIFI_FLASH_HEADER_SIZE to fw_total_size)
    printf("[CRC] Calculating CRC32 for %lu bytes using hardware CRC...\r\n", flash_header->fw_total_size);
    
    // Get pointer to data starting from WIFI_FLASH_HEADER_SIZE
    crc_data_ptr = (const uint8_t *)(flash_addr + WIFI_FLASH_HEADER_SIZE);
    crc_data_size = flash_header->fw_total_size;
    
    // Use hardware CRC to calculate (byte-wise, InputDataFormat is configured as BYTES)
    calculated_crc = CRC_Calculate((void *)crc_data_ptr, crc_data_size);

    printf("[CRC] Calculated CRC: 0x%08lX, Expected CRC: 0x%08lX\r\n",
           calculated_crc, flash_header->fw_crc);

    // Step 7: Verify CRC
    if (calculated_crc != flash_header->fw_crc) {
        printf("[ERROR] CRC mismatch! Calculated: 0x%08lX, Expected: 0x%08lX\r\n",
               calculated_crc, flash_header->fw_crc);
        storage_unlock();
        return -1;
    }

    printf("[SUCCESS] Flash header and CRC validation passed\r\n");

    // Step 8: Start firmware upgrade
    chunk_cnt = 0;
    offset = 0;
    one_time = 1;
    si91x_wlan_app_cb.state = SI91X_WLAN_FW_UPGRADE;

    t_start = osKernelGetTickCount();
    printf("[TIMER] Firmware upgrade started at tick: %lu\r\n", t_start);

    fw_image_size = fw_header->image_size;
    total_size = fw_image_size + FW_HEADER_SIZE;
    printf("\n[FIRMWARE] Firmware details:\r\n");
    printf("  - Header size: %ld bytes\r\n", FW_HEADER_SIZE);
    printf("  - Payload size: %lu bytes\r\n", fw_image_size);
    printf("  - Total size: %lu bytes\r\n", total_size);
    printf("  - Chunk size: %ld bytes\r\n", SI91X_CHUNK_SIZE);

    total_chunks = (total_size + SI91X_CHUNK_SIZE - 1) / SI91X_CHUNK_SIZE;
    remaining_bytes = total_size;
    printf("  - Total chunks: %lu\r\n", total_chunks);
    printf("\r\n[PROGRESS] Starting firmware transmission from flash...\r\n");

    // Read and send firmware in chunks (direct memory access)
    for (uint32_t i = 0; i < total_chunks; i++) {
        uint32_t chunk_read_size = (remaining_bytes > SI91X_CHUNK_SIZE) ? 
                                   SI91X_CHUNK_SIZE : remaining_bytes;
        
        memset(recv_buffer, 0, SI91X_CHUNK_SIZE);
        
        // Read chunk from flash (direct memory copy)
        const uint8_t *chunk_src = flash_addr + WIFI_FLASH_HEADER_SIZE + (i * SI91X_CHUNK_SIZE);
        memcpy(recv_buffer, chunk_src, chunk_read_size);

        remaining_bytes -= chunk_read_size;
        
        progress_percent = ((i + 1) * 100) / total_chunks;
        if (progress_percent != last_reported_percent && 
            (progress_percent % 10 == 0 || i == total_chunks - 1)) {
            printf("\n[PROGRESS] %ld%% complete (%lu/%lu chunks)\r\n", 
                   progress_percent, i + 1, total_chunks);
            last_reported_percent = progress_percent;
        }

        if (i == 0) {
            printf("\r\n[BLOCK 0] Sending header block (START_OF_FILE)\r\n");
        } else if (i == (total_chunks - 1)) {
            printf("\n[BLOCK %lu] Sending final block (END_OF_FILE, %lu bytes)\r\n", i, chunk_read_size);
        } else {
            printf("\n[BLOCK %lu] Sending data block (%lu bytes)\r\n", i, chunk_read_size);
        }

        status = sl_si91x_app_task_fw_update_via_xmodem(recv_buffer, SI91X_CHUNK_SIZE);
        if (status != SL_STATUS_OK) {
            printf("[ERROR] Chunk %lu processing failed: 0x%lx\r\n", i, status);
            storage_unlock();
            return -1;
        }
        
        printf("[SUCCESS] Block %lu processed\r\n", i);
        osDelay(10);
    }

    printf("[FLASH] Firmware upgrade from flash completed\r\n");

    // Check if completion state needs to be triggered
    if (si91x_wlan_app_cb.state == SI91X_WLAN_FW_UPGRADE_DONE) {
        printf("\n[UPGRADE] Triggering final upgrade state\r\n");
        status = sl_si91x_app_task_fw_update_via_xmodem(NULL, 0);
        storage_unlock();
        return (status == SL_STATUS_OK) ? 0 : -1;
    }
    
    // If all chunks were sent successfully but state not updated yet, wait a bit and check again
    if (chunk_cnt >= total_chunks) {
        printf("[INFO] All chunks sent, waiting for upgrade completion...\r\n");
        osDelay(100);
        if (si91x_wlan_app_cb.state == SI91X_WLAN_FW_UPGRADE_DONE) {
            printf("\n[UPGRADE] Triggering final upgrade state\r\n");
            status = sl_si91x_app_task_fw_update_via_xmodem(NULL, 0);
            storage_unlock();
            return (status == SL_STATUS_OK) ? 0 : -1;
        }
    }
    
    storage_unlock();
    return -1;
}

/**
 * @brief Upgrade the firmware of the Wi-Fi module.
 *
 * This function performs the following steps:
 * 1. Initialize the Wi-Fi module as a client.
 * 2. Check if the firmware needs to be updated.
 * 3. If the firmware needs to be updated, read the firmware image from the file
 *    specified by the `WIFI_FIR_NAME` macro and upgrade the firmware.
 *
 * @return 0 if the firmware upgrade is successful, -1 otherwise.
 */
static void wifi_update_process(void) 
{
    int32_t status = SL_STATUS_OK;
    device_t *misc = NULL;
    blink_params_t blink_params = {0};
    // PowerHandle wifi_handle = pwr_manager_get_handle(PWR_WIFI);
    // pwr_manager_acquire(wifi_handle);
    // osDelay(100);
    storage_nvs_write(NVS_FACTORY, NVS_KEY_WIFI_MODE, WIFI_MODE_NORMAL, strlen(WIFI_MODE_NORMAL));
    
    misc = device_find_pattern(IND_DEVICE_NAME, DEV_TYPE_MISC);
    if (misc != NULL) {
        blink_params.blink_times = INT32_MAX;
        blink_params.interval_ms = 50;
        device_ioctl(misc, MISC_CMD_LED_SET_BLINK, (uint8_t *)&blink_params, 0);
    }
    status = sl_net_init(SL_NET_WIFI_CLIENT_INTERFACE, &firmware_update_configuration, NULL, NULL);
    if (status != SL_STATUS_OK) {
        printf("wifi_update sl_net_init failed(0x%lx) \r\n", status);
        sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
        return;
    }
    
    status = firmware_upgrade_from_file(WIFI_FIR_NAME);
    if (status != 0) {
        status = firmware_upgrade_from_flash();
    }
    
    sl_net_deinit(SL_NET_WIFI_CLIENT_INTERFACE);
    if (status == 0) {
        printf("wifi_update ok \r\n");
//         storage_nvs_flush_all();
//         osDelay(200);
// #if ENABLE_U0_MODULE
//         u0_module_clear_wakeup_flag();
//         u0_module_reset_chip_n6();
// #endif
//         HAL_NVIC_SystemReset();
    } else {
        printf("wifi_update failed \r\n");
    }
    if (misc != NULL) {
        device_ioctl(misc, MISC_CMD_LED_ON, 0, 0);
    }
    wifi_update_times++;
    storage_nvs_write(NVS_FACTORY, NVS_KEY_WIFI_UPDATE_TIMES, &wifi_update_times, sizeof(wifi_update_times));
    // pwr_manager_release(wifi_handle);
    return;
}

static void wifi_ant_process(void)
{
    int32_t status = SL_STATUS_OK;

    storage_nvs_write(NVS_FACTORY, NVS_KEY_WIFI_MODE, WIFI_MODE_NORMAL, strlen(WIFI_MODE_NORMAL));

    status = sl_net_init(SL_NET_WIFI_CLIENT_INTERFACE, &transmit_test_configuration, NULL, NULL);
    if (status != SL_STATUS_OK) {
        printf("Failed to start Wi-Fi client interface: 0x%lx\r\n", status);
        return;
    }
    printf("\r\nWi-Fi Init Done \r\n");
}

void wifi_enter_update_mode(void)
{
    storage_nvs_write(NVS_FACTORY, NVS_KEY_WIFI_MODE, WIFI_MODE_UPDATE, strlen(WIFI_MODE_UPDATE));
    LOG_SIMPLE("wifi update, System reset...\r\n");
    osDelay(200);
#if ENABLE_U0_MODULE
    u0_module_clear_wakeup_flag();
    u0_module_reset_chip_n6();
#endif
    HAL_NVIC_SystemReset();
}

void wifi_mark_update_pending(void)
{
    storage_nvs_write(NVS_FACTORY, NVS_KEY_WIFI_MODE, WIFI_MODE_UPDATE, strlen(WIFI_MODE_UPDATE));
    LOG_SIMPLE("wifi update pending, will apply on next reboot\r\n");
}

int wifi_get_running_version(char *buf, size_t size)
{
    if (!buf || size < 16) return -1;

    sl_wifi_firmware_version_t fw;
    sl_status_t status = sl_wifi_get_firmware_version(&fw);
    if (status != SL_STATUS_OK) {
        snprintf(buf, size, "N/A");
        return -1;
    }

    // Version encoding matches pack_to_hex.py / ota_packer.py:
    // Major.Minor.Patch.(Security*100 + Build)
    // e.g. {2,14,5,2,0,7} -> "2.14.5.207"
    //
    // NOTE: the SiWG917 SDK swaps the field names — security_version actually
    //       holds the patch number, and patch_num holds the security version.
    //       This mirrors the .rps binary header layout (see wifi_get_flash_version).
    int encoded_build = (int)fw.patch_num * 100 + (int)fw.build_num;
    snprintf(buf, size, "%u.%u.%u.%d",
             fw.major, fw.minor, fw.security_version, encoded_build);
    return 0;
}

int wifi_get_flash_version(char *buf, size_t size)
{
    if (!buf || size < 16) return -1;

    const uint8_t *flash_addr = (const uint8_t *)WIFI_FLASH_BASE_ADDR;
    const flash_header_t *hdr = (const flash_header_t *)flash_addr;
    if (hdr->valid_flags != WIFI_FLASH_VALID_FLAGS) {
        snprintf(buf, size, "N/A");
        return -1;
    }

    // .rps binary immediately follows the 32-byte flash_header_t.  Its first
    // 64 bytes are a sl_wifi_firmware_header_t.  Version components are split
    // across two sub-structures within that 64-byte block:
    //
    //   offset in .rps   field (SDK struct name)   our mapping
    //   ───────────────  ─────
    //   12               fw_version_info:
    //                      build_num        [7:0]   build
    //                      security_version [15:8]  patch  ← SDK naming quirk
    //                      minor           [23:16]  minor
    //                      major           [31:24]  major
    //   44               fw_version_ext_info:
    //                      patch_num        [7:0]   security ← SDK naming quirk
    storage_lock();
    const uint8_t *rps = flash_addr + WIFI_FLASH_HEADER_SIZE;
    uint32_t ver_info = *(const uint32_t *)(rps + 12);
    uint32_t ver_ext  = *(const uint32_t *)(rps + 44);
    storage_unlock();

    uint8_t major    = (ver_info >> 24) & 0xFF;
    uint8_t minor    = (ver_info >> 16) & 0xFF;
    uint8_t patch    = (ver_info >>  8) & 0xFF;  // SDK: security_version
    uint8_t security = (ver_ext  >>  0) & 0xFF;  // SDK: patch_num
    uint8_t build    = (ver_info >>  0) & 0xFF;

    // Encode to 4-part version matching pack_to_hex.py / ota_packer.py:
    // Major.Minor.Patch.(Security*100 + Build)
    int encoded_build = (int)security * 100 + (int)build;
    snprintf(buf, size, "%u.%u.%u.%d",
             major, minor, patch, encoded_build);
    return 0;
}

static int wifi_update_cmd(int argc, char* argv[])
{
    wifi_enter_update_mode();
    return 0;
}

static int wifi_test_cmd(int argc, char* argv[]) 
{
    storage_nvs_write(NVS_FACTORY, NVS_KEY_WIFI_MODE, WIFI_MODE_TX_TEST, strlen(WIFI_MODE_TX_TEST));
    LOG_SIMPLE("wifi test, System reset...\r\n");
    osDelay(200);
#if ENABLE_U0_MODULE
    u0_module_clear_wakeup_flag();
    u0_module_reset_chip_n6();
#endif
    HAL_NVIC_SystemReset();
    return 0;
}

static int wifi_set_antenna_cmd(int argc, char* argv[])
{
    sl_wifi_interface_t interface = SL_WIFI_CLIENT_2_4GHZ_INTERFACE;
    sl_wifi_antenna_t antenna = SL_WIFI_ANTENNA_INTERNAL;
    sl_status_t status = SL_STATUS_OK;

    if (!is_wifi_ant()) {
        LOG_SIMPLE("Please use [wifitest] cmd to enter wifi test mode first!\r\n");
        return -1;
    }
    // wifi_set_antenna -i client -a antenna
    for (int i = 1; i < (argc - 1); i++) {
        if (strcmp(argv[i], "-i") == 0) {
            if (strcmp(argv[i + 1], "client") == 0) {
                interface = SL_WIFI_CLIENT_2_4GHZ_INTERFACE;
            } else if (strcmp(argv[i + 1], "ap") == 0) {
                interface = SL_WIFI_AP_2_4GHZ_INTERFACE;
            }
        } else if (strcmp(argv[i], "-a") == 0) {
            antenna = (sl_wifi_antenna_t)atoi(argv[i + 1]);
        }
    }

    LOG_SIMPLE("wifi_set_antenna: %d, %d\r\n", interface, antenna);
    status = sl_wifi_set_antenna(interface, antenna);
    if (status != SL_STATUS_OK) {
        LOG_SIMPLE("Failed to start set Antenna: 0x%lx\r\n", status);
        return -1;
    }

    LOG_SIMPLE("Set Antenna Done\r\n");
    return 0;
}

static int wifi_transmit_test_start_cmd(int argc, char* argv[]) 
{
    sl_status_t status = SL_STATUS_OK;
    sl_si91x_request_tx_test_info_t tx_test_info = { 0 };

    if (!is_wifi_ant()) {
        LOG_SIMPLE("Please use [wifitest] cmd to enter wifi test mode first!\r\n");
        return -1;
    }
    memcpy(&tx_test_info, &default_tx_test_info, sizeof(sl_si91x_request_tx_test_info_t));
    // wifi_ax_transmit_test_start power data rate length mode channel aggr.enable enable_11ax coding_type nominal_pe ul_dl he_ppdu_type beam_change bw stbc tx_bf gi_ltf dcm nsts_midamble spatial_reuse bss_color he_siga2_reserved ru_allocation n_heltf_tot sigb_dcm sigb_mcs user_sta_id user_idx sigb_compression_field
    if (argc > 1) {
        tx_test_info.power = atoi(argv[1]);
    }
    if (argc > 2) {
        tx_test_info.rate = atoi(argv[2]);
    }
    if (argc > 3) {
        tx_test_info.length = atoi(argv[3]);
    }
    if (argc > 4) {
        tx_test_info.mode = atoi(argv[4]);
    }
    if (argc > 5) {
        tx_test_info.channel = atoi(argv[5]);
    }
    if (argc > 6) {
        tx_test_info.aggr_enable = atoi(argv[6]);
    }
    if (argc > 7) {
        tx_test_info.enable_11ax = atoi(argv[7]);
    }
    if (argc > 8) {
        tx_test_info.coding_type = atoi(argv[8]);
    }
    if (argc > 9) {
        tx_test_info.nominal_pe = atoi(argv[9]);
    }
    if (argc > 10) {
        tx_test_info.ul_dl = atoi(argv[10]);
    }
    if (argc > 11) {
        tx_test_info.he_ppdu_type = atoi(argv[11]);
    }
    if (argc > 12) {
        tx_test_info.beam_change = atoi(argv[12]);
    }
    if (argc > 13) {
        tx_test_info.bw = atoi(argv[13]);
    }
    if (argc > 14) {
        tx_test_info.stbc = atoi(argv[14]);
    }
    if (argc > 15) {
        tx_test_info.tx_bf = atoi(argv[15]);
    }
    if (argc > 16) {
        tx_test_info.gi_ltf = atoi(argv[16]);
    }
    if (argc > 17) {
        tx_test_info.dcm = atoi(argv[17]);
    }
    if (argc > 18) {
        tx_test_info.nsts_midamble = atoi(argv[18]);
    }
    if (argc > 19) {
        tx_test_info.spatial_reuse = atoi(argv[19]);
    }
    if (argc > 20) {
        tx_test_info.bss_color = atoi(argv[20]);
    }
    if (argc > 21) {
        tx_test_info.he_siga2_reserved = atoi(argv[21]);
    }
    if (argc > 22) {
        tx_test_info.ru_allocation = atoi(argv[22]);
    }
    if (argc > 23) {
        tx_test_info.n_heltf_tot = atoi(argv[23]);
    }
    if (argc > 24) {
        tx_test_info.sigb_dcm = atoi(argv[24]);
    }
    if (argc > 25) {
        tx_test_info.sigb_mcs = atoi(argv[25]);
    }
    if (argc > 26) {
        tx_test_info.user_sta_id = atoi(argv[26]);
    }
    if (argc > 27) {
        tx_test_info.user_idx = atoi(argv[27]);
    }
    if (argc > 28) {
        tx_test_info.sigb_compression_field = atoi(argv[28]);
    }

    LOG_SIMPLE("WiFi transmit test arguments:");
    LOG_SIMPLE("power: %d", tx_test_info.power);
    LOG_SIMPLE("rate: %d", tx_test_info.rate);
    LOG_SIMPLE("length: %d", tx_test_info.length);
    LOG_SIMPLE("mode: %d", tx_test_info.mode);
    LOG_SIMPLE("channel: %d", tx_test_info.channel);
    LOG_SIMPLE("aggr_enable: %d", tx_test_info.aggr_enable);
    LOG_SIMPLE("enable_11ax: %d", tx_test_info.enable_11ax);
    LOG_SIMPLE("coding_type: %d", tx_test_info.coding_type);
    LOG_SIMPLE("nominal_pe: %d", tx_test_info.nominal_pe);
    LOG_SIMPLE("ul_dl: %d", tx_test_info.ul_dl);
    LOG_SIMPLE("he_ppdu_type: %d", tx_test_info.he_ppdu_type);
    LOG_SIMPLE("beam_change: %d", tx_test_info.beam_change);
    LOG_SIMPLE("bw: %d", tx_test_info.bw);
    LOG_SIMPLE("stbc: %d", tx_test_info.stbc);
    LOG_SIMPLE("tx_bf: %d", tx_test_info.tx_bf);
    LOG_SIMPLE("gi_ltf: %d", tx_test_info.gi_ltf);
    LOG_SIMPLE("dcm: %d", tx_test_info.dcm);
    LOG_SIMPLE("nsts_midamble: %d", tx_test_info.nsts_midamble);
    LOG_SIMPLE("spatial_reuse: %d", tx_test_info.spatial_reuse);
    LOG_SIMPLE("bss_color: %d", tx_test_info.bss_color);
    LOG_SIMPLE("he_siga2_reserved: %d", tx_test_info.he_siga2_reserved);
    LOG_SIMPLE("ru_allocation: %d", tx_test_info.ru_allocation);
    LOG_SIMPLE("n_heltf_tot: %d", tx_test_info.n_heltf_tot);
    LOG_SIMPLE("sigb_dcm: %d", tx_test_info.sigb_dcm);
    LOG_SIMPLE("sigb_mcs: %d", tx_test_info.sigb_mcs);
    LOG_SIMPLE("user_sta_id: %d", tx_test_info.user_sta_id);
    LOG_SIMPLE("user_idx: %d", tx_test_info.user_idx);
    LOG_SIMPLE("sigb_compression_field: %d", tx_test_info.sigb_compression_field);
    status = sl_si91x_transmit_test_start(&tx_test_info);
    if (status != SL_STATUS_OK) {
        LOG_SIMPLE("\r\ntransmit test start Failed, Error Code : 0x%lX\r\n", status);
        return -1;
    }
    LOG_SIMPLE("WiFi transmit test started.\r\n");
    return 0;
}

static int wifi_transmit_test_stop_cmd(int argc, char* argv[]) 
{
    sl_status_t status;

    if (!is_wifi_ant()) {
        LOG_SIMPLE("Please use [wifitest] cmd to enter wifi test mode first!\r\n");
        return -1;
    }
    status = sl_si91x_transmit_test_stop();
    if (status != SL_STATUS_OK) {
        LOG_SIMPLE("\r\ntransmit test stop Failed, Error Code : 0x%lX\r\n", status);
        return -1;
    }
    LOG_SIMPLE("WiFi transmit test stopped.\r\n");
    return 0;
}

static int wifi_cmd_spi(int argc, char *argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: wifispi <hexdata> [count]\r\n");
        LOG_SIMPLE("Example: wifispi 0a0b0c0d 10\r\n");
        LOG_SIMPLE("         wifispi 0x0a 5\r\n");
        return -1;
    }
    // Parse hex string to byte stream, support 0x prefix
    const char *hexstr = argv[1];
    int hexlen = strlen(hexstr);
    uint8_t txbuf[256] = {0};
    uint8_t rxbuf[256] = {0};
    int txlen = 0;
    int i = 0;
    while (i < hexlen && txlen < 256) {
        // Skip 0x or 0X prefix
        if ((hexstr[i] == '0') && (hexstr[i+1] == 'x' || hexstr[i+1] == 'X')) {
            i += 2;
            continue;
        }
        unsigned int val = 0;
        if (sscanf(&hexstr[i], "%2x", &val) == 1) {
            txbuf[txlen++] = (uint8_t)val;
            i += 2;
        } else {
            LOG_SIMPLE("Invalid hexdata\r\n");
            return -1;
        }
    }
    if (txlen == 0) {
        LOG_SIMPLE("No valid hexdata\r\n");
        return -1;
    }
    int count = 1;
    if (argc >= 3) {
        count = atoi(argv[2]);
        if (count < 1) count = 1;
    }
    for (int c = 0; c < count; c++) {
        sl_status_t ret = sl_si91x_host_spi_transfer(txbuf, rxbuf, txlen);
        if (ret != SL_STATUS_OK) {
            LOG_SIMPLE("spi transfer failed, ret=%d\r\n", ret);
            return -1;
        }
        LOG_SIMPLE("spi tx:");
        for (int j = 0; j < txlen; j++) printf(" %02X", txbuf[j]);
        LOG_SIMPLE("\r\nspi rx:");
        for (int j = 0; j < txlen; j++) printf(" %02X", rxbuf[j]);
        LOG_SIMPLE("\r\n");
    }
    return 0;
}

debug_cmd_reg_t wifi_cmd_table[] = {
    {"wifiup",     "WiFi update.",      wifi_update_cmd},
    {"wifitest",   "WiFi test.",        wifi_test_cmd},
    {"wifi_set_antenna",  "WiFi set antenna.",       wifi_set_antenna_cmd},
    {"wifi_transmit_test_start",  "WiFi transmit test start.",       wifi_transmit_test_start_cmd},
    {"wifi_transmit_test_stop",  "WiFi transmit test stop.",       wifi_transmit_test_stop_cmd},
    // {"wifi_ant",  "WiFi antenna test <start|stop>",      wifi_ant_cmd},
    {"wifispi", "wifi spi <hexdata> [count]", wifi_cmd_spi},
};


static void wifi_cmd_register(void)
{
    debug_cmdline_register(wifi_cmd_table, sizeof(wifi_cmd_table) / sizeof(wifi_cmd_table[0]));
}

void wifi_register(void)
{
    driver_cmd_register_callback("wifi_mode", wifi_cmd_register);
}

int is_wifi_ant(void)
{
    return wifi_ant_flag;
}

int is_wifi_update(void)
{
    return wifi_update_flag;
}

uint32_t get_wifi_update_times(void)
{
    return wifi_update_times;
}

void wifi_mode_process(void)
{
    char wifi_mode[16] = {0};

    if (storage_nvs_read(NVS_FACTORY, NVS_KEY_WIFI_MODE, &wifi_mode, sizeof(wifi_mode)) < 0) {
        strcpy(wifi_mode, WIFI_MODE_NORMAL);
    }
    if (storage_nvs_read(NVS_FACTORY, NVS_KEY_WIFI_UPDATE_TIMES, &wifi_update_times, sizeof(wifi_update_times)) < 0) {
        wifi_update_times = 0;
    }

    // printf("\r\n wifi_mode: %s , wifi_update_times: %ld \r\n", wifi_mode, wifi_update_times);

    if (strcmp(WIFI_MODE_UPDATE, wifi_mode) == 0) {
        printf("\r\n wifi_update_process \r\n");
        wifi_update_process();
        wifi_update_flag = 1;
        return;
    }

    if (strcmp(WIFI_MODE_TX_TEST, wifi_mode) == 0) {
        printf("\r\n wifi_test_process \r\n");
        wifi_ant_process();
        wifi_ant_flag = 1;
        return;
    }
}
