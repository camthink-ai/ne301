/***************************************************************************/ /**
 * @file
 * @brief Internal Si91x driver implementation (sli API).
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
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
/* Include order: own header first, then public driver (sli before sl), then
 * system, then project (sl_ then sli_). */
#include "sli_si91x_driver.h"
#include "sl_si91x_driver.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cmsis_os2.h"
#include "sl_cmsis_utility.h"
#include "sl_rsi_utility.h"
#include "sl_si91x_constants.h"
#include "sl_si91x_core_utilities.h"
#include "sl_si91x_host_interface.h"
#include "sl_si91x_protocol_types.h"
#include "sl_si91x_status.h"
#include "sl_si91x_types.h"
#include "sl_wifi.h"
#include "sl_wifi_device.h"
#include "sl_wifi_types.h"

#include "sli_buffer_manager.h"
#include "sli_hal_si91x.h"
#include "sli_queue_manager.h"
#include "sli_routing_utility.h"
#include "sli_si91x_wifi_command_engine.h"
#include "sli_si91x_wifi_command_engine_packet.h"
#include "sli_wifi.h"
#include "sli_wifi_command_engine_config.h"
#include "sli_wifi_constants.h"
#include "sli_wifi_utility.h"

#ifdef SL_SI91X_SIDE_BAND_CRYPTO
extern osEventFlagsId_t ta_events;
osMutexId_t side_band_crypto_mutex = NULL;
#endif

#ifndef NULL
#define NULL (void *)0
#endif

#define SLI_EIA_BIT_IN_CTRL_FLAG         BIT(6)
#define SLI_EIA_BIT_IN_HOST_DESC         BIT(4)
#define SLI_IMMEDIATE_TRF_CTRL_FLAGS     BIT(7)
#define SLI_IMMEDIATE_TRF_HOST_DESC      BIT(6)
#define SLI_LAST_PKT                     BIT(0)
#define SLI_IS_LAST_PKT(ctrl_flags1)     (ctrl_flags1 & SLI_LAST_PKT)
#define SLI_IS_EIA_PKT(ctrl_flags)       (ctrl_flags & SLI_EIA_BIT_IN_CTRL_FLAG)
#define SLI_IS_IMMEDIATE_TRF(ctrl_flags) (ctrl_flags & SLI_IMMEDIATE_TRF_CTRL_FLAGS)
#define SLI_EXT_DESC_SIZE_IF_EIA_PKT     5

#define SLI_VERIFY_STATUS(s) \
  do {                       \
    if (s != SL_STATUS_OK)   \
      return s;              \
  } while (0)

// XTAL and PMU good time values (stored when set before init, applied during init).
static uint16_t sli_xtal_good_time_us = 0;
static uint16_t sli_pmu_good_time_us  = 0;

static uint32_t config_feature_bit_map        = 0;
static sli_wifi_efuse_data_t si91x_efuse_data = { 0 };

sli_queue_t cmd_queues[SI91X_CMD_MAX] = { 0 };
osEventFlagsId_t sli_wifi_events      = NULL;

uint8_t firmware_queue_id[SI91X_CMD_MAX] = { [SLI_WIFI_COMMON_CMD]   = SLI_WLAN_MGMT_Q,
                                             [SLI_WIFI_WLAN_CMD]     = SLI_WLAN_MGMT_Q,
                                             [SLI_SI91X_NETWORK_CMD] = SLI_WLAN_MGMT_Q,
                                             [SLI_SI91X_SOCKET_CMD]  = SLI_WLAN_MGMT_Q,
                                             [SLI_SI91X_BT_CMD]      = SLI_BT_Q };

uint8_t command_packet_type[SLI_WLAN_CMD_MAX] = {
  [SLI_WLAN_COMMON_CMD]   = SLI_WIFI_COMMAND_ENGINE_COMMON_COMMAND_PACKET,
  [SLI_WIFI_WLAN_CMD]     = SLI_WIFI_COMMAND_ENGINE_WIFI_COMMAND_PACKET,
  [SLI_SI91X_NETWORK_CMD] = SLI_WIFI_COMMAND_ENGINE_NETWORK_COMMAND_PACKET,
  [SLI_SI91X_BT_CMD]      = SLI_WIFI_COMMAND_ENGINE_BLE_COMMAND_PACKET,
  [SLI_SI91X_SOCKET_CMD]  = SLI_WIFI_COMMAND_ENGINE_SOCKET_COMMAND_PACKET,
};

void sli_si91x_set_xtal_pmu_good_time_from_host(uint32_t code, uint16_t value)
{
  if (code & SL_SI91X_SET_XTAL_GOOD_TIME_FROM_HOST) {
    sli_xtal_good_time_us = value;
  }
  if (code & SL_SI91X_SET_PMU_GOOD_TIME_FROM_HOST) {
    sli_pmu_good_time_us = value;
  }
}

static sl_status_t sli_apply_xtal_pmu_good_time(uint16_t value, uint32_t code)
{
  if ((value == 0) || (code == 0)) {
    return SL_STATUS_OK;
  }
  sl_si91x_nwp_configuration_t cfg = { 0 };
  cfg.code                         = code;
  cfg.values.config_val            = value;
  return sli_wifi_send_command(SLI_COMMON_REQ_SET_CONFIG,
                               SLI_WIFI_COMMON_CMD,
                               &cfg,
                               sizeof(sl_si91x_nwp_configuration_t),
                               SLI_WIFI_WAIT_FOR_RESPONSE(SLI_COMMON_RSP_SET_CONFIG_WAIT_TIME),
                               NULL,
                               NULL);
}

sl_status_t sli_si91x_apply_pending_xtal_pmu_good_time(void)
{
  sl_status_t status;
  if (sli_xtal_good_time_us != 0) {
    status = sli_apply_xtal_pmu_good_time(sli_xtal_good_time_us, SL_SI91X_SET_XTAL_GOOD_TIME_FROM_HOST);
    if (status != SL_STATUS_OK) {
      return status;
    }
    sli_xtal_good_time_us = 0;
  }
  if (sli_pmu_good_time_us != 0) {
    status = sli_apply_xtal_pmu_good_time(sli_pmu_good_time_us, SL_SI91X_SET_PMU_GOOD_TIME_FROM_HOST);
    if (status != SL_STATUS_OK) {
      return status;
    }
    sli_pmu_good_time_us = 0;
  }
  return SL_STATUS_OK;
}

void sli_si91x_set_config_feature_bit_map(uint32_t config_feature_bit_map_val)
{
  config_feature_bit_map = config_feature_bit_map_val;
}

void sli_si91x_get_efuse_data(sli_wifi_efuse_data_t *efuse_data)
{
  memcpy(efuse_data, &si91x_efuse_data, sizeof(sli_wifi_efuse_data_t));
}

void sli_si91x_set_efuse_data(const sli_wifi_efuse_data_t *efuse_data)
{
  memcpy(&si91x_efuse_data, efuse_data, sizeof(sli_wifi_efuse_data_t));
}

sl_status_t sli_si91x_get_flash_efuse_data(sli_wifi_efuse_data_t *efuse_data, uint8_t efuse_data_type)
{
  sl_status_t status;
  sl_wifi_buffer_t *buffer = NULL;
  SL_WIFI_ARGS_CHECK_NULL_POINTER(efuse_data);

  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  status = sli_wifi_send_command(SLI_COMMON_REQ_GET_EFUSE_DATA,
                                 SLI_WIFI_COMMON_CMD,
                                 &efuse_data_type,
                                 sizeof(efuse_data_type),
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_COMMON_RSP_GET_EFUSE_DATA_WAIT_TIME),
                                 NULL,
                                 (void **)&buffer);
  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer(buffer);
  }
  VERIFY_STATUS_AND_RETURN(status);

  if (buffer == NULL) {
    return SL_STATUS_FAIL;
  }

  const sl_wifi_system_packet_t *packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);
  if (packet != NULL && packet->length > 0) {
    switch (efuse_data_type) {
      case SL_SI91X_EFUSE_MFG_SW_VERSION:
        memcpy(&efuse_data->mfg_sw_version, packet->data, packet->length);
        break;
      case SL_SI91X_EFUSE_PTE_CRC:
        memcpy(&efuse_data->pte_crc, packet->data, packet->length);
        break;
      default:
        break;
    }
  }
  // Success path: always free buffer to avoid leak (single return path)
  sli_buffer_manager_free_buffer(buffer);
  return SL_STATUS_OK;
}

sl_status_t sli_si91x_driver_send_bt_command(sli_wifi_request_commands_t command,
                                             sli_wifi_command_type_t command_type,
                                             sl_wifi_system_packet_t *packet)
{
  UNUSED_PARAMETER(command);
  sl_status_t status = SL_STATUS_OK;

  // Validate parameters
  if (packet == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  // Only accept BT commands; on rejection caller retains ownership and must free the packet
  if (command_type != SLI_SI91X_BT_CMD) {
    return SL_STATUS_INVALID_INDEX;
  }

  // Route (send) the packet via routing utility (may be async)
  status = sli_routing_utility_route_packet(&wifi_command_engine_routing_table,
                                            SLI_BT_PACKET,
                                            packet,
                                            sizeof(sl_wifi_system_packet_t) + (packet->length & 0xFFF),
                                            (void *)packet);

  return status;
}

sl_status_t sli_verify_device_boot(uint32_t *rom_version)
{
  sl_status_t status;
  uint8_t value[2] = { 0, 0 };

  // Read a memory register to check if it's valid
  status = sl_si91x_bus_read_memory(SLI_HOST_INTF_REG_OUT, 2, (const uint8_t *)&value[0]);
  SLI_VERIFY_STATUS(status);

  // Verify register read was valid
  if (value[1] != SLI_WIFI_REGISTER_VALID) {
    return SL_STATUS_WAITING_FOR_BOARD_READY;
  }

  // Verify register value
  if (value[0] == SLI_BOOTUP_OPTIONS_LAST_CONFIG_NOT_SAVED) {
    return SL_STATUS_BOOTUP_OPTIONS_NOT_SAVED;
  } else if (value[0] == SLI_BOOTUP_OPTIONS_CHECKSUM_FAIL) {
    return SL_STATUS_BOOTUP_OPTIONS_CHECKSUM_FAILURE;
  }
#if RSI_BOOTLOADER_VERSION_CHECK
  else if (value[0] == RSI_BOOTLOADER_VERSION) {
  } else {
    return SL_STATUS_BOOTLOADER_VERSION_MISMATCH;
  }
#endif

  // Extract ROM version info
  if (value[0] == SLI_BOOTLOADER_VERSION_1P0) {
    *rom_version = SLI_ROM_VERSION_1P0;
  } else if (value[0] == SLI_BOOTLOADER_VERSION_1P1) {
    *rom_version = SLI_ROM_VERSION_1P1;
  }

  return SL_STATUS_OK;
}

sl_status_t sli_wifi_select_option(const uint8_t configuration)
{
  uint16_t boot_command = 0;
  sl_status_t status    = 0;
  uint16_t read_value   = 0;

  // Write a boot command to initiate the option selection
  status = sl_si91x_bus_write_memory(SLI_HOST_INTF_REG_OUT, 2, (uint8_t *)&boot_command);
  VERIFY_STATUS_AND_RETURN(status);

  if ((configuration == BURN_NWP_FW) || (configuration == BURN_M4_FW)) {
    boot_command = SLI_HOST_INTERACT_REG_VALID_FW | configuration;
  } else {
    boot_command = SLI_HOST_INTERACT_REG_VALID | configuration;
  }

  if (configuration == BURN_M4_FW) {
    boot_command |= SLI_M4_FW_IMAGE_NUMBER;
  }

  // Write the configuration to the SI91x host for option selection
  status = sl_si91x_bus_write_memory(SLI_HOST_INTF_REG_IN, 2, (uint8_t *)&boot_command);
  VERIFY_STATUS_AND_RETURN(status);

  // Check for a specific response to ensure successful option selection
  if ((configuration != LOAD_NWP_FW) && (configuration != LOAD_DEFAULT_NWP_FW_ACTIVE_LOW)) {
    uint32_t timestamp = sl_si91x_host_get_timestamp();
    while (sl_si91x_host_elapsed_time(timestamp) < 300) {
      status = sl_si91x_bus_read_memory(SLI_HOST_INTF_REG_OUT, 2, (const uint8_t *)&read_value);
      VERIFY_STATUS_AND_RETURN(status);

      if ((configuration == BURN_NWP_FW) || (configuration == BURN_M4_FW)) {
        if (read_value == (SLI_HOST_INTERACT_REG_VALID | SLI_SEND_RPS_FILE)) {
          return SL_STATUS_OK;
        }
      } else if (read_value == (SLI_HOST_INTERACT_REG_VALID | configuration)) {
        return SL_STATUS_OK;
      }
    }
  } else {
    // Check up to 3 seconds for firmware load or upgrade status
    uint32_t timestamp               = sl_si91x_host_get_timestamp();
    uint16_t default_nwp_fw_selected = 0;
    while (sl_si91x_host_elapsed_time(timestamp) < 3000) {
      status = sl_si91x_bus_read_memory(SLI_HOST_INTF_REG_OUT, 2, (const uint8_t *)&read_value);
      if (status != SL_STATUS_OK) {
        continue;
      }

      if ((read_value & 0xF000) == (SLI_HOST_INTERACT_REG_VALID_FW & 0xF000)) {
        if ((read_value & 0xFF) == SLI_VALID_FIRMWARE_NOT_PRESENT) {
          if (default_nwp_fw_selected == 0) {
            boot_command = SLI_HOST_INTERACT_REG_VALID_FW | SLI_SELECT_DEFAULT_NWP_FW_IMAGE_NUMBER;
            status       = sl_si91x_bus_write_memory(SLI_HOST_INTF_REG_IN, 2, (uint8_t *)&boot_command);
            if (status != SL_STATUS_OK) {
              return status;
            }

            while (sl_si91x_host_elapsed_time(timestamp) < 2000) {
              status = sl_si91x_bus_read_memory(SLI_HOST_INTF_REG_OUT, 2, (const uint8_t *)&read_value);
              if (status != SL_STATUS_OK) {
                continue;
              }
              if (read_value == (SLI_HOST_INTERACT_REG_VALID | SLI_SELECT_DEFAULT_NWP_FW_IMAGE_NUMBER)) {
                break;
              }
            }

            boot_command = SLI_HOST_INTERACT_REG_VALID_FW | configuration;
            status       = sl_si91x_bus_write_memory(SLI_HOST_INTF_REG_IN, 2, (uint8_t *)&boot_command);
            if (status != SL_STATUS_OK) {
              return status;
            }
            default_nwp_fw_selected = 1;
            continue;
          } else {
            return SL_STATUS_VALID_FIRMWARE_NOT_PRESENT;
          }
        }
        if ((read_value & 0xFF) == SLI_INVALID_OPTION) {
          return SL_STATUS_INVALID_OPTION;
        }
        if ((read_value & 0xFF) == SLI_CHECKSUM_SUCCESS) {
          return status;
        }
      }
    }
  }
  return SL_STATUS_FW_LOAD_OR_UPGRADE_TIMEOUT;
}

uint16_t sli_get_seq_ctrl(uint8_t is_qos)
{
  static uint16_t qos_pkt_count     = 0;
  static uint16_t non_qos_pkt_count = 0;

  if (qos_pkt_count > 4095) {
    qos_pkt_count = 0;
  }
  if (non_qos_pkt_count > 4095) {
    non_qos_pkt_count = 0;
  }

  return is_qos ? qos_pkt_count++ : non_qos_pkt_count++;
}

int32_t sli_encapsulate_tx_data_packet(sl_wifi_transceiver_tx_data_control_t *control,
                                       uint8_t *pkt_data,
                                       uint32_t mac_hdr_len,
                                       uint32_t feature_bit_map)
{
  uint16_t seq_ctrl = 0;
  uint16_t *frame_ctrl;
  uint32_t qos_ctrl_off = MAC80211_HDR_MIN_LEN;

  SL_VERIFY_POINTER_OR_RETURN(control, SL_STATUS_NULL_POINTER);

  if (IS_MAC_ZERO(control->addr1)) {
    return SL_STATUS_TRANSCEIVER_INVALID_MAC_ADDRESS;
  }

  if ((!IS_PEER_DS_SUPPORT_ENABLED(feature_bit_map)) && !IS_FIXED_DATA_RATE(control->ctrl_flags)) {
    return SL_STATUS_TRANSCEIVER_INVALID_DATA_RATE;
  }

  if (IS_BCAST_MCAST_MAC(control->addr1[0])) {
    control->ctrl_flags &= ~TX_DATA_CTRL_FLAG_QOS_BIT;
  }

  if ((IS_QOS_PKT(control->ctrl_flags) && !IS_BCAST_MCAST_MAC(control->addr1[0])) && (control->priority > 3)) {
    return SL_STATUS_TRANSCEIVER_INVALID_QOS_PRIORITY;
  }

  if (IS_4ADDR(control->ctrl_flags)) {
    qos_ctrl_off += MAC80211_HDR_ADDR4_LEN;
  }

  memset(pkt_data, 0, mac_hdr_len);
  frame_ctrl = (uint16_t *)&pkt_data[0];
  *frame_ctrl |= FC_TYPE_DATA;

  if (IS_4ADDR(control->ctrl_flags)) {
    *frame_ctrl |= FC_TO_DS;
    *frame_ctrl |= FC_FROM_DS;
  } else {
    *frame_ctrl |= IS_TODS(control->ctrl_flags) ? FC_TO_DS : 0;
    *frame_ctrl |= IS_FROMDS(control->ctrl_flags) ? FC_FROM_DS : 0;
  }

  memcpy(&pkt_data[4], control->addr1, 6);
  memcpy(&pkt_data[10], control->addr2, 6);
  memcpy(&pkt_data[16], control->addr3, 6);

  if (!IS_PEER_DS_SUPPORT_ENABLED(feature_bit_map)) {
    seq_ctrl = (uint16_t)(sli_get_seq_ctrl(IS_QOS_PKT(control->ctrl_flags)) << 4);
    memcpy(&pkt_data[22], &seq_ctrl, 2);
  }

  if (IS_4ADDR(control->ctrl_flags)) {
    memcpy(&pkt_data[24], control->addr4, 6);
  }
  if (IS_QOS_PKT(control->ctrl_flags) && !IS_BCAST_MCAST_MAC(control->addr1[0])) {
    *frame_ctrl |= FC_SUBTYPE_QOS_DATA;
    pkt_data[qos_ctrl_off] = WME_AC_TO_TID(control->priority);
  }

  return SL_STATUS_OK;
}

sl_status_t sli_get_nwp_timestamp(uint32_t *timestamp)
{
  if (timestamp == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  sl_status_t status                          = SL_STATUS_OK;
  sl_wifi_buffer_t *buffer                    = NULL;
  const sl_wifi_system_packet_t *packet       = NULL;
  sl_si91x_nwp_get_configuration_t nwp_config = { .sub_command_type = GET_NWP_TIMESTAMP };

  status = sli_wifi_send_command(SLI_COMMON_REQ_GET_CONFIG,
                                 SLI_WIFI_COMMON_CMD,
                                 &nwp_config,
                                 sizeof(sl_si91x_nwp_get_configuration_t),
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_COMMON_RSP_GET_CONFIG_WAIT_TIME),
                                 NULL,
                                 (void **)&buffer);

  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer(buffer);
  }
  VERIFY_STATUS_AND_RETURN(status);

  packet = (const sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(buffer, 0, NULL);

  if (packet == NULL) {
    sli_buffer_manager_free_buffer(buffer);
    return SL_STATUS_NULL_POINTER;
  }

  memcpy(timestamp, packet->data, packet->length);
  sli_buffer_manager_free_buffer(buffer);

  return status;
}

static sl_status_t sli_si91x_check_thread_priority_order(void)
{
  // check the order of priorties of the Event engine, HAL and Command engine
  osPriority_t hal_thread_priority            = sli_hal_si91x_get_thread_priority();
  osPriority_t command_engine_thread_priority = sli_wifi_command_engine_config.priority;
  osPriority_t event_engine_thread_priority   = sli_event_engine_get_thread_priority();

  // check the order of priorties of the Event engine, HAL and Command engine
  // order is Event engine > HAL > Command engine
  if (event_engine_thread_priority > hal_thread_priority && hal_thread_priority > command_engine_thread_priority) {
    // clang-format off
    SL_DEBUG_LOG_V2(INFO,
                    "\r\nevent_engine_thread_priority: %" PRIu32
                    ", hal_thread_priority: %" PRIu32
                    ", command_engine_thread_priority: %" PRIu32
                    "\r\nThread priority order is correct \r\n",
                    (uint32_t)event_engine_thread_priority,
                    (uint32_t)hal_thread_priority,
                    (uint32_t)command_engine_thread_priority);
    // clang-format on
    return SL_STATUS_OK;
  }
  // clang-format off
    SL_DEBUG_LOG_V2(INFO,
                    "\r\nevent_engine_thread_priority: %" PRIu32
                    ", hal_thread_priority: %" PRIu32
                    ", command_engine_thread_priority: %" PRIu32
                    "Expected order is Event engine > HAL > Command engine\r\n",
                    (uint32_t)event_engine_thread_priority,
                    (uint32_t)hal_thread_priority,
                    (uint32_t)command_engine_thread_priority);
  // clang-format on

  return SL_STATUS_INVALID_CONFIGURATION;
}

uint32_t sli_si91x_get_config_feature_bit_map(void)
{
  return config_feature_bit_map;
}

sl_status_t sli_si91x_wifi_platform_init(void)
{
  sl_status_t status = SL_STATUS_OK;

  memset(cmd_queues, 0, sizeof(cmd_queues));

  if (NULL == sli_wifi_events) {
    sli_wifi_events = osEventFlagsNew(NULL);
    if (NULL == sli_wifi_events) {
      return SL_STATUS_ALLOCATION_FAILED;
    }
  }

  for (int i = 0; i < SI91X_CMD_MAX; i++) {
    status = sli_queue_manager_init(&cmd_queues[i], SLI_BUFFER_MANAGER_QUEUE_NODE_POOL);
    VERIFY_STATUS_AND_RETURN(status);
  }

  status = sli_hal_si91x_init();
  VERIFY_STATUS_AND_RETURN(status);

  status = sli_si91x_wifi_command_engine_init();
  VERIFY_STATUS_AND_RETURN(status);

  status = sli_si91x_check_thread_priority_order();
  VERIFY_STATUS_AND_RETURN(status);

#ifdef SL_SI91X_SIDE_BAND_CRYPTO
  side_band_crypto_mutex = osMutexNew(NULL);
#endif

  return status;
}

sl_status_t sli_si91x_wifi_platform_deinit(void)
{
  sl_status_t status = SL_STATUS_OK;

  status = sli_hal_si91x_deinit();
  VERIFY_STATUS_AND_RETURN(status);

  status = sli_si91x_wifi_command_engine_deinit();
  VERIFY_STATUS_AND_RETURN(status);

  if (NULL != sli_wifi_events) {
    osEventFlagsDelete(sli_wifi_events);
    sli_wifi_events = NULL;
  }

  return SL_STATUS_OK;
}

uint32_t sli_si91x_wait_for_event(uint32_t event_mask, uint32_t timeout)
{
  if (timeout != osWaitForever) {
    timeout = SLI_SYSTEM_MS_TO_TICKS(timeout);
  }

  uint32_t result = osEventFlagsWait(sli_wifi_events, event_mask, osFlagsWaitAny, timeout);

  // ponytail: catch the 0xFFFFFFFx error family, but keep the legitimate
  // "no event" outcomes (timeout; resource on a non-blocking poll) silent
  // and instant as the original code did — sync command waits routinely
  // time out. Only the abnormal members (dead/NULL flags object returns
  // osFlagsErrorParameter instantly on EVERY call) get logged once and a
  // 5ms yield, so callers looping on a dead object degrade to a slow
  // poll instead of hot-spinning at this thread's priority.
  if ((result & (uint32_t)osFlagsError) != 0u) {
    if ((result != (uint32_t)osErrorTimeout) && (result != (uint32_t)osErrorResource)) {
      static bool flags_error_logged = false;
      if (!flags_error_logged) {
        flags_error_logged = true;
        SL_DEBUG_LOG_V2(ERROR, "driver: osEventFlagsWait failed 0x%lX\r\n", result);
      }
      osDelay(5);
    }
    return 0;
  }
  return result;
}

void sli_wifi_set_event(uint32_t event_mask)
{
  if (event_mask != 0 && sli_wifi_events != NULL) {
    osEventFlagsSet(sli_wifi_events, event_mask);
  }
}

sl_status_t sli_convert_si91x_status_to_sl_status(si91x_status_t si91x_status)
{
  switch (si91x_status) {
    case RSI_ERROR_NONE:
      return SL_STATUS_OK;
    case RSI_ERROR_TIMEOUT:
      return SL_STATUS_TIMEOUT;
    case RSI_ERROR_INVALID_PARAM:
      return SL_STATUS_INVALID_PARAMETER;
    case RSI_ERROR_COMMAND_GIVEN_IN_WRONG_STATE:
      return SL_STATUS_INVALID_STATE;
    case RSI_ERROR_PKT_ALLOCATION_FAILURE:
      return SL_STATUS_ALLOCATION_FAILED;
    case RSI_ERROR_COMMAND_NOT_SUPPORTED:
      return SL_STATUS_NOT_SUPPORTED;
    case RSI_ERROR_INSUFFICIENT_BUFFER:
      return SL_STATUS_NO_MORE_RESOURCE;
    case RSI_ERROR_IN_OS_OPERATION:
      return SL_STATUS_OS_OPERATION_FAILURE;
    case RSI_ERROR_BOOTUP_OPTIONS_NOT_SAVED:
      return SL_STATUS_BOOTUP_OPTIONS_NOT_SAVED;
    case RSI_ERROR_BOOTLOADER_VERSION_NOT_MATCHING:
      return SL_STATUS_BOOTLOADER_VERSION_MISMATCH;
    case RSI_ERROR_WAITING_FOR_BOARD_READY:
      return SL_STATUS_WAITING_FOR_BOARD_READY;
    case RSI_ERROR_VALID_FIRMWARE_NOT_PRESENT:
      return SL_STATUS_VALID_FIRMWARE_NOT_PRESENT;
    case RSI_ERROR_INVALID_OPTION:
      return SL_STATUS_INVALID_OPTION;
    case RSI_ERROR_SPI_BUSY:
      return SL_STATUS_SPI_BUSY;
    case RSI_ERROR_CARD_READY_TIMEOUT:
      return SL_STATUS_CARD_READY_TIMEOUT;
    case RSI_ERROR_FW_LOAD_OR_UPGRADE_TIMEOUT:
      return SL_STATUS_FW_LOAD_OR_UPGRADE_TIMEOUT;
    default:
      return SL_STATUS_FAIL;
  }
}

sl_status_t sli_wifi_send_data_packet(const void *data, uint16_t length, const void *context)
{
  // Route (send) the packet via routing utility (may be async)
  sl_status_t status = sli_routing_utility_route_packet(&wifi_command_engine_routing_table,
                                                        SLI_WIFI_DATA_PACKET,
                                                        data,
                                                        (length & 0xFFF),
                                                        context);

  return (SL_STATUS_IN_PROGRESS == status) ? SL_STATUS_OK : status;
}
