/***************************************************************************/ /**
 * @file
 * @brief
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
/* Include order: own header first (sl_si91x_driver.h includes sli_si91x_driver.h),
 * then system, then project (sl_ then sli_). */
#include "sl_si91x_driver.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "cmsis_os2.h"
#include "sl_cmsis_utility.h"
#include "sl_core.h"
#include "sl_rsi_utility.h"
#include "sl_si91x_core_utilities.h"
#include "sl_si91x_host_interface.h"
#include "sl_si91x_protocol_types.h"
#include "sl_si91x_status.h"
#include "sl_si91x_types.h"
#include "sl_wifi.h"
#include "sl_wifi_device.h"
#include "sl_wifi_types.h"
#ifdef SLI_SI91X_SOCKETS
#include "sl_si91x_socket_callback_framework.h"
#include "sl_si91x_socket_utility.h"
#endif
#ifdef SL_WDT_MANAGER_PRESENT
#if defined SL_WDT_MANAGER_PRESENT_M4_UC_RC
#include "sl_si91x_wdt_rc_config.h"
#elif defined SL_WDT_MANAGER_PRESENT_M4_UC_XTAL
#include "sl_si91x_wdt_xtal_config.h"
#endif
#endif

#include "sli_buffer_manager.h"
#include "sli_hal_si91x.h"
#include "sli_queue_manager.h"
#include "sli_routing_utility.h"
#include "sli_si91x_wifi_command_engine_packet.h"
#include "sli_wifi.h"
#include "sli_wifi_command_engine_config.h"
#include "sli_wifi_constants.h"
#include "sli_wifi_power_profile.h"
#include "sli_wifi_utility.h"
#ifdef SLI_SI91X_MCU_INTERFACE
#include "rsi_m4.h"
#include "rsi_rom_clks.h"
#include "rsi_wisemcu_hardware_setup.h"
#include "sli_siwx917_soc.h"
#endif
#ifdef SLI_SI91X_ENABLE_BLE
#include "rsi_common_apis.h"
#endif

#ifdef SL_SI91X_SIDE_BAND_CRYPTO
#define SLI_SIDE_BAND_DONE (1 << 2) //! had to be redefined as this macro is not in .h
rsi_m4ta_desc_t crypto_desc[2];
extern osEventFlagsId_t ta_events;
extern osMutexId_t side_band_crypto_mutex;
#endif

#ifndef NULL
#define NULL (void *)0
#endif

#define SLI_SI91X_GET_TCP_IP_TOTAL_SELECTS_BITS(x) ((x & 0x0000F000) >> 12)

#ifdef SL_SI91X_SIDE_BAND_CRYPTO
#define SL_HOST_DESC_LEN  16
#define SL_CRYPTO_PKT_LEN 128
#endif

#ifdef SLI_SI91X_MCU_INTERFACE
// Combined Image defines
#define SLI_COMBINED_IMAGE             BIT(7)
#define SLI_COMBINED_IMAGE_SIZE_OFFSET 48
#endif

// Enterprise configuration command parameters
/*=======================================================================*/

// Internal Wi-Fi transceiver mode configurations are currently not supported.
#define SLI_EIA_BIT_IN_CTRL_FLAG         BIT(6) //< Extended Information Available bit in Control Flags
#define SLI_EIA_BIT_IN_HOST_DESC         BIT(4) //< Extended Information Available bit in Host Descriptor
#define SLI_IMMEDIATE_TRF_CTRL_FLAGS     BIT(7) //< Immediate Transfer bit in Control Flags
#define SLI_IMMEDIATE_TRF_HOST_DESC      BIT(6) //< Immediate Transfer bit in Host Descriptor
#define SLI_LAST_PKT                     BIT(0) //< Last Packet bit in Control Flags1
#define SLI_IS_LAST_PKT(ctrl_flags1)     (ctrl_flags1 & SLI_LAST_PKT)
#define SLI_IS_EIA_PKT(ctrl_flags)       (ctrl_flags & SLI_EIA_BIT_IN_CTRL_FLAG)
#define SLI_IS_IMMEDIATE_TRF(ctrl_flags) (ctrl_flags & SLI_IMMEDIATE_TRF_CTRL_FLAGS)
#define SLI_EXT_DESC_SIZE_IF_EIA_PKT     5

// Buffer size and count for the buffer manager pools for the SI91x driver
#define SLI_WIFI_BUFFER_BLOCK_SIZE  1640
#define SLI_WIFI_BUFFER_BLOCK_COUNT 10

// Note: This is the extended block size for the transceiver APIs.
#define SLI_WIFI_EXTENDED_BLOCK_SIZE 2324

#ifdef SPI_EXTENDED_TX_LEN_2K
#define SLI_WIFI_PACKET_BLOCK_SIZE SLI_WIFI_EXTENDED_BLOCK_SIZE
#else
#define SLI_WIFI_PACKET_BLOCK_SIZE SLI_WIFI_BUFFER_BLOCK_SIZE
#endif
// Max payload fitting one CE TX/DATA pool block (block size minus hidden pool
// management pointer and sl_wifi_system_packet_t header). Larger payloads
// would memcpy past the block and corrupt the pool.
#define SLI_WIFI_MAX_PACKET_PAYLOAD \
  (SLI_WIFI_PACKET_BLOCK_SIZE - sizeof(void *) - sizeof(sl_wifi_system_packet_t))

/*========================================================================*/
// 11ax params
/*========================================================================*/
#define SLI_GUARD_INTERVAL 3

#ifdef SLI_SI91X_MCU_INTERFACE
#define SLI_TX_PKT_TRANSFER_DONE_INTERRUPT BIT(2)
#define SLI_RX_PKT_TRANSFER_DONE_INTERRUPT BIT(1)

// Function declarations related to M4 interface
static sl_status_t sl_si91x_soft_reset(void);
void sli_m4_ta_interrupt_init(void);
#endif

static sl_wifi_timeout_t timeout_glbl = { .auth_assoc_timeout_value       = SL_WIFI_DEFAULT_AUTH_ASSOCIATION_TIMEOUT,
                                          .active_chan_scan_timeout_value = SL_WIFI_DEFAULT_ACTIVE_CHANNEL_SCAN_TIME,
                                          .keep_alive_timeout_value       = SL_WIFI_DEFAULT_KEEP_ALIVE_TIMEOUT,
                                          .passive_scan_timeout_value     = SL_WIFI_DEFAULT_PASSIVE_CHANNEL_SCAN_TIME };
sl_status_t sl_si91x_driver_raw_send_command(uint8_t command,
                                             const void *data,
                                             uint32_t data_length,
                                             uint32_t wait_time);
sl_status_t sl_si91x_driver_init_wifi_radio(const sl_wifi_device_configuration_t *config);
sl_status_t sli_verify_device_boot(uint32_t *rom_version);
sl_status_t sl_si91x_enable_radio(void);
sl_status_t sli_wifi_select_option(const uint8_t configuration);
sl_status_t sli_si91x_bootup_firmware(const uint8_t select_option, uint8_t image_number);
sl_status_t sl_si91x_host_power_cycle(void);

// This variable stores the frame status of response packet in case of API executed being failed.
// Note: This will not store the error values of asynchronous events.
sl_wifi_event_handler_t si91x_event_handler = NULL;

// Global variables for device and driver management
sl_wifi_interface_t default_interface;
bool device_initialized = false;

bool interface_is_up[SL_WIFI_MAX_INTERFACE_INDEX] = { false, false, false, false, false };
bool bg_enabled                                   = false;
uint32_t frontend_switch_control                  = 0;
static uint32_t feature_bit_map                   = 0;
//! Currently, initialized_opermode is used only to handle concurrent mode using sl_net_init()
extern uint16_t initialized_opermode;

#ifdef SLI_SI91X_ENABLE_BLE
//! Memory length for driver
#define GLOBAL_BUFF_LEN 1500

//! Memory to initialize driver
uint8_t global_buf[GLOBAL_BUFF_LEN] = { 0 };
#endif
// clang-format off
const sl_wifi_scan_configuration_t default_wifi_scan_configuration = { .type  = SL_WIFI_SCAN_TYPE_ACTIVE,
                                                                       .flags = 0,
                                                                       .periodic_scan_interval = 0,
                                                                       .channel_bitmap_2g4     = 0xFFFF,
                                                                       .channel_bitmap_5g      = { 0xFFFFFFFF,
                                                                                              0xFFFFFFFF,
                                                                                              0xFFFFFFFF,
                                                                                              0xFFFFFFFF,
                                                                                              0xFFFFFFFF,
                                                                                              0xFFFFFFFF,
                                                                                              0xFFFFFFFF,
                                                                                              0xFFFFFFFF }, 
                                                                       .lp_mode = 0};

// clang-format on

const sl_wifi_ap_configuration_t default_wifi_ap_configuration = {
  .ssid                = { .value = "SILICON_LABS_AP", .length = sizeof("SILICON_LABS_AP") - 1 },
  .security            = SL_WIFI_WPA2,
  .encryption          = SL_WIFI_CCMP_ENCRYPTION,
  .channel             = { .channel = 11 },
  .rate_protocol       = SL_WIFI_RATE_PROTOCOL_AUTO,
  .options             = 0,
  .credential_id       = 2, //SL_NET_DEFAULT_WIFI_AP_CREDENTIAL_ID,
  .keepalive_type      = SL_WIFI_AP_NULL_BASED_KEEP_ALIVE,
  .beacon_interval     = 100,
  .client_idle_timeout = 255,
  .dtim_beacon_count   = 4,
  .maximum_clients     = 4
};

#ifdef SLI_SI91X_MCU_INTERFACE
extern sl_wifi_buffer_t *rx_pkt_buffer;
#endif

sl_status_t sl_si91x_driver_init_wifi_radio(const sl_wifi_device_configuration_t *config)
{
  sl_status_t status;

// Set 11ax configuration with guard interval if SLI_SI91X_CONFIG_WIFI6_PARAMS is supported
// Note: 802.11ax is supported only in client-capable modes (SL_WIFI_CLIENT_MODE, SL_WIFI_ENTERPRISE_CLIENT_MODE, SL_WIFI_CONCURRENT_MODE).
// SL_WIFI_ACCESS_POINT_MODE does not support 802.11ax and the firmware will reject it with error 0x6C.
#ifdef SLI_SI91X_CONFIG_WIFI6_PARAMS
  if (config->boot_config.oper_mode != SL_SI91X_ACCESS_POINT_MODE) {
    sl_wifi_11ax_config_params_t config_11ax_params = { 0 };
    config_11ax_params.gi_ltf                       = SL_WIFI_4HE_LTF_3200_NSEC_GI;
    config_11ax_params.dcm_enable                   = SL_WIFI_DCM_ENABLE_DISABLED;
    config_11ax_params.beamformee_support           = SL_WIFI_BEAMFORMEE_SUPPORT_ENABLED;
    config_11ax_params.config_er_su                 = SL_WIFI_CONFIG_ER_SU_NO;
    status                                          = sl_wifi_set_11ax_config_v2(&config_11ax_params);
    VERIFY_STATUS_AND_RETURN(status);
  }
#endif

  // Send WLAN request to set the operating band (2.4GHz or 5GHz)
  status = sli_wifi_send_command(SLI_WIFI_REQ_BAND,
                                 SLI_WIFI_WLAN_CMD,
                                 &config->band,
                                 1,
                                 SLI_WIFI_RSP_BAND_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);

  if (config->boot_config.oper_mode != SL_SI91X_ACCESS_POINT_MODE) {
    if (timeout_glbl.active_chan_scan_timeout_value != SL_WIFI_DEFAULT_ACTIVE_CHANNEL_SCAN_TIME) {
      status = sli_wifi_configure_timeout(SL_WIFI_CLIENT_INTERFACE,
                                          SL_WIFI_CHANNEL_ACTIVE_SCAN_TIMEOUT,
                                          timeout_glbl.active_chan_scan_timeout_value);
      VERIFY_STATUS_AND_RETURN(status);
    }

    if (timeout_glbl.auth_assoc_timeout_value != SL_WIFI_DEFAULT_AUTH_ASSOCIATION_TIMEOUT) {
      status = sli_wifi_configure_timeout(SL_WIFI_CLIENT_INTERFACE,
                                          SL_WIFI_AUTHENTICATION_ASSOCIATION_TIMEOUT,
                                          timeout_glbl.auth_assoc_timeout_value);
      VERIFY_STATUS_AND_RETURN(status);
    }

    if (timeout_glbl.keep_alive_timeout_value != SL_WIFI_DEFAULT_KEEP_ALIVE_TIMEOUT) {
      status = sli_wifi_configure_timeout(SL_WIFI_CLIENT_INTERFACE,
                                          SL_WIFI_KEEP_ALIVE_TIMEOUT,
                                          timeout_glbl.keep_alive_timeout_value);
      VERIFY_STATUS_AND_RETURN(status);
    }
    if (timeout_glbl.passive_scan_timeout_value != SL_WIFI_DEFAULT_PASSIVE_CHANNEL_SCAN_TIME) {
      status = sli_wifi_configure_timeout(SL_WIFI_CLIENT_INTERFACE,
                                          SL_WIFI_CHANNEL_PASSIVE_SCAN_TIMEOUT,
                                          timeout_glbl.passive_scan_timeout_value);
      VERIFY_STATUS_AND_RETURN(status);
    }
  }

  // Initialize the WLAN subsystem
  status =
    sli_wifi_send_command(SLI_WIFI_REQ_INIT, SLI_WIFI_WLAN_CMD, NULL, 0, SLI_WIFI_RSP_INIT_WAIT_TIME, NULL, NULL);
  VERIFY_STATUS_AND_RETURN(status);

#ifndef SL_SI91X_ACX_MODULE
  if (SL_WIFI_IGNORE_REGION != config->region_code) {
    // Set the device's region based on configuration
    status = sl_si91x_set_device_region(config->boot_config.oper_mode, config->band, config->region_code);
    SL_DEBUG_LOG_V2(DEBUG, "Region code set to %d\r\n", config->region_code);
    VERIFY_STATUS_AND_RETURN(status);
  }
#else
  SL_DEBUG_LOG_V2(DEBUG, "Region code configuration skipped for Modules\r\n");
#endif

  // Configure the RTS threshold for WLAN
  sli_wifi_config_request_t config_request = { .config_type = SLI_WIFI_CONFIG_RTS_THRESHOLD,
                                               .value       = SLI_WIFI_RTS_THRESHOLD };
  status                                   = sli_wifi_send_command(SLI_WIFI_REQ_CONFIG,
                                 SLI_WIFI_WLAN_CMD,
                                 &config_request,
                                 sizeof(config_request),
                                 SLI_WIFI_RSP_CONFIG_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

static void sli_si91x_set_device_initialized_status(const sl_wifi_device_configuration_t *config)
{
  device_initialized   = true;
  initialized_opermode = config->boot_config.oper_mode;

  // Set interface status flags based on operating mode and band
  if ((config->boot_config.oper_mode == SL_SI91X_CLIENT_MODE)
      || (config->boot_config.oper_mode == SL_SI91X_ENTERPRISE_CLIENT_MODE)
      || (config->boot_config.oper_mode == SL_SI91X_CONCURRENT_MODE)
      || (config->boot_config.oper_mode == SL_SI91X_TRANSMIT_TEST_MODE)) {
    if (config->band == SL_WIFI_DUAL_BAND_MODE) {
      interface_is_up[SL_WIFI_CLIENT_DUAL_INTERFACE_INDEX] = true;
    } else if (config->band == SL_WIFI_BAND_MODE_5GHZ) {
      interface_is_up[SL_WIFI_CLIENT_5GHZ_INTERFACE_INDEX] = true;
    } else if (config->band == SL_WIFI_BAND_MODE_2_4GHZ) {
      interface_is_up[SL_WIFI_CLIENT_2_4GHZ_INTERFACE_INDEX] = true;
    }
  }
}

// Roll back a partially completed driver init so a retry starts clean.
// Without this a failed init leaks pools/threads and the next attempt
// double-allocates (lab-port fix, SDK_LOCAL_MODIFICATIONS.md §3.2).
static void sli_si91x_driver_init_rollback(void)
{
  sl_si91x_host_disable_bus_interrupt();
  sli_si91x_wifi_platform_deinit();
  sl_si91x_host_deinit();
  sli_buffer_manager_deinit();
}

sl_status_t sl_si91x_driver_init(const sl_wifi_device_configuration_t *config, sl_wifi_event_handler_t event_handler)
{
  sl_status_t status;
  sl_si91x_host_init_configuration_t init_config = { 0 };
  const uint8_t select_option                    = config->boot_option;
  if (config->boot_config.coex_mode == SL_SI91X_WLAN_MODE) { // Check for not support mode
    return SL_STATUS_NOT_SUPPORTED;
  }

  // Determine the default interface based on operating mode (AP or Client)
  if (config->boot_config.oper_mode == SL_SI91X_ACCESS_POINT_MODE) {
    default_interface = SL_WIFI_AP_INTERFACE;
  } else {
    default_interface = SL_WIFI_CLIENT_INTERFACE;
  }

  // Configure the interface for 5GHz band if selected (currently not supported for Si91x)
  switch (config->band) {
    case SL_WIFI_DUAL_BAND_MODE:
      default_interface |= SL_WIFI_DUAL_INTERFACE;
      break;
    case SL_WIFI_BAND_MODE_5GHZ:
      default_interface |= SL_WIFI_5GHZ_INTERFACE;
      break;
    case SL_WIFI_BAND_MODE_2_4GHZ:
      default_interface |= SL_WIFI_2_4GHZ_INTERFACE;
      break;
  }

  // Set the event handler for the SI91x wireless driver
  si91x_event_handler = event_handler;

  // Check if the device is already initialized
  if (device_initialized) {
    // If it's already initialized, ensure the requested operating mode is compatible
    if (initialized_opermode == SL_SI91X_CONCURRENT_MODE) {
      return (initialized_opermode == config->boot_config.oper_mode) ? SL_STATUS_OK : SL_STATUS_WIFI_INVALID_OPERMODE;
    }
    return SL_STATUS_ALREADY_INITIALIZED;
  }
  // Initialize BLE if BLE is enabled
#ifdef SLI_SI91X_ENABLE_BLE
  int32_t rsi_status = 0;
  rsi_status         = rsi_ble_driver_init(global_buf, GLOBAL_BUFF_LEN);
  if ((rsi_status < 0) || (rsi_status > GLOBAL_BUFF_LEN)) {
    return SL_STATUS_FAIL;
  }
#endif

#ifndef SLI_SI91X_ENABLE_IPV6
  const uint32_t *tcp_ip_feature_bit_map = &(config->boot_config.tcp_ip_feature_bit_map);

  // check if the module network stack is in IPV6 mode.
  if (*tcp_ip_feature_bit_map & (SLI_SI91X_IPV6_MODE)) {
    return SL_STATUS_NOT_AVAILABLE;
  }
#endif

  sli_buffer_manager_pool_info_t default_buffer_configuration[SLI_BUFFER_MANAGER_MAX_POOL] = {
    {
#ifdef SPI_EXTENDED_TX_LEN_2K
      .block_size = SLI_WIFI_EXTENDED_BLOCK_SIZE,
#else
      .block_size = SLI_WIFI_BUFFER_BLOCK_SIZE,
#endif
      .block_count = 4,
    },
    {
#ifdef SPI_EXTENDED_TX_LEN_2K
      .block_size = SLI_WIFI_EXTENDED_BLOCK_SIZE,
#else
      .block_size = SLI_WIFI_BUFFER_BLOCK_SIZE,
#endif
      .block_count = 4,
    },

    {
#ifdef SPI_EXTENDED_TX_LEN_2K
      .block_size = SLI_WIFI_EXTENDED_BLOCK_SIZE,
#else
      .block_size = SLI_WIFI_BUFFER_BLOCK_SIZE,
#endif
      .block_count = 4,
    },

    {
      .block_size  = sizeof(sli_command_engine_metadata_t),
      .block_count = SLI_WIFI_BUFFER_BLOCK_COUNT,
    },

    {
      .block_size  = sizeof(sli_command_engine_metadata_t),
      .block_count = SLI_WIFI_BUFFER_BLOCK_COUNT,
    },

    {
      .block_size  = SLI_WIFI_BUFFER_BLOCK_SIZE,
      .block_count = 0,
    },

    {
      .block_size  = SLI_WIFI_BUFFER_BLOCK_SIZE,
      .block_count = 10,
    },

    {
      .block_size  = SLI_WIFI_BUFFER_BLOCK_SIZE,
      .block_count = 0,
    },
    {
      .block_size  = SLI_WIFI_BUFFER_BLOCK_SIZE,
      .block_count = 0,
    },
    {
      .block_size  = sizeof(sli_queue_node_t),
      .block_count = 20,
    },
  };

  // Initialize the buffer manager
  sli_buffer_manager_configuration_t config_buffer = { 0 };

  for (uint8_t i = 0; i < SLI_BUFFER_MANAGER_MAX_POOL; i++) {
    config_buffer.pool_info[i] = &default_buffer_configuration[i];
  }

  config_buffer.common_pool_info.block_size  = default_buffer_configuration[0].block_size;
  config_buffer.common_pool_info.block_count = 4;

  // Initialize the buffer manager
  status = sli_buffer_manager_init(&config_buffer);
  if (status != SL_STATUS_OK) {
    return status;
  }

  init_config.rx_irq      = sli_si91x_bus_rx_irq_handler;
  init_config.rx_done     = sli_si91x_bus_rx_done_handler;
  init_config.boot_option = config->boot_option;

  // Initialize the SI91x host
  status = sl_si91x_host_init(&init_config);
  if (status != SL_STATUS_OK) {
    sli_si91x_driver_init_rollback();
    return status;
  }

  // Initialize the SI91x platform
  status = sli_si91x_wifi_platform_init();
  if (status != SL_STATUS_OK) {
    sli_si91x_driver_init_rollback();
    return status;
  }

#ifdef SLI_SI91X_MCU_INTERFACE
  // firmware bootup is require only for the first time, no need to do it again if we call init after deinit
  static bool is_bootup_firmware_required = true;
  if (is_bootup_firmware_required) {
    status = sli_si91x_bootup_firmware(select_option, config->nwp_fw_image_number);
    if (status != SL_STATUS_OK) {
      sli_si91x_driver_init_rollback();
      return status;
    }
    is_bootup_firmware_required = false;
  } else {
    // Initialize NWP interrupt and submit RX packets
    sli_m4_ta_interrupt_init();
  }
#else
  status = sli_si91x_bootup_firmware(select_option, config->nwp_fw_image_number);
  if (status != SL_STATUS_OK) {
    sli_si91x_driver_init_rollback();
    return status;
  }
#endif

  if (select_option == BURN_NWP_FW) {
    sli_si91x_set_device_initialized_status(config);
    return SL_STATUS_OK;
  }

#ifndef __ZEPHYR__
  // Initialize task register index to save firmware status
  status = sli_fw_status_storage_index_init();
  VERIFY_STATUS_AND_RETURN(status);
#endif

#ifdef SL_SI91X_SPI_HIGH_SPEED_ENABLE
  // Enable high speed bus on the device and the host
  status = sli_si91x_bus_enable_high_speed();
  VERIFY_STATUS_AND_RETURN(status);
  sl_si91x_host_enable_high_speed_bus();
#endif
  sl_si91x_host_enable_bus_interrupt();

// Wait for card ready command response
#ifdef SLI_SI91X_MCU_INTERFACE
  // NWP would not send card ready command response, if we call init after deinit

  if (sli_wifi_get_card_ready_required()) {
    uint32_t events = sli_si91x_wait_for_event(SL_WIFI_HOST_COMMON_RESPONSE_EVENT, 5000);
    if (!(events & SL_WIFI_HOST_COMMON_RESPONSE_EVENT)) {
      sli_si91x_driver_init_rollback();
      return SL_STATUS_CARD_READY_TIMEOUT;
    }
    sli_wifi_set_card_ready_required(false);
  }
#else
  uint32_t events = sli_si91x_wait_for_event(SL_WIFI_HOST_COMMON_RESPONSE_EVENT, 5000);
  if (!(events & SL_WIFI_HOST_COMMON_RESPONSE_EVENT)) {
    sli_si91x_driver_init_rollback();
    return SL_STATUS_CARD_READY_TIMEOUT;
  }
#endif

  // Apply XTAL and PMU good times (provided via sl_si91x_set_nwp_config_request() before init).
  status = sli_si91x_apply_pending_xtal_pmu_good_time();
  VERIFY_STATUS_AND_RETURN(status);

  sl_wifi_system_boot_configuration_t boot_config = config->boot_config;

  // In coex mode, 160 MHz SoC clock is not supported.
  // If 160 MHz is configured in coex mode, it will automatically fall back to 120 MHz.
  if ((boot_config.coex_mode) && (boot_config.custom_feature_bit_map & SL_SI91X_CUSTOM_FEAT_SOC_CLK_CONFIG_160MHZ)) {
    SL_DEBUG_LOG_V2(WARN, "\r\n 160 MHz clock is not supported in coex mode. Falling back to 120 MHz.\r\n");
    // Clamp the configuration to 120 MHz
    boot_config.custom_feature_bit_map &= ~SL_SI91X_CUSTOM_FEAT_SOC_CLK_CONFIG_160MHZ;
    boot_config.custom_feature_bit_map |= SL_SI91X_CUSTOM_FEAT_SOC_CLK_CONFIG_120MHZ;
  }

  // Send WLAN request to set the operating mode and configuration
  status = sli_wifi_send_command(SLI_WIFI_REQ_OPERMODE,
                                 SLI_WIFI_COMMON_CMD,
                                 &boot_config,
                                 sizeof(sl_wifi_system_boot_configuration_t),
                                 SLI_WIFI_RSP_OPERMODE_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
#if (SL_WDT_MANAGER_PRESENT_NWP == 1)
  sl_si91x_nwp_configuration_t nwp_config;

  // NWP WDT configuration
  memset(&nwp_config, 0, sizeof(sl_si91x_nwp_configuration_t));
  nwp_config.code                    = SL_SI91X_ENABLE_NWP_WDT_FROM_HOST;
  nwp_config.values.wdt_timer_val    = SL_SI91X_WATCHDOG_MANAGER_TIMEOUT_PERIOD_NWP;
  nwp_config.values.wdt_enable_in_ps = 0;
  status                             = sl_si91x_set_nwp_config_request(nwp_config);
  VERIFY_STATUS_AND_RETURN(status);
#endif

  feature_bit_map = config->boot_config.feature_bit_map;
  sli_si91x_set_config_feature_bit_map(config->boot_config.config_feature_bit_map);

#ifdef SLI_SI91X_ENABLE_BLE
  if (config->boot_config.coex_mode == SL_SI91X_BLE_MODE || config->boot_config.coex_mode == SL_SI91X_WLAN_BLE_MODE) {
    // Wait for BT card ready
    rsi_bt_common_init();
    // Set BLE runtime state to enabled
    rsi_ble_set_opermode_state(true);
  } else {
    // Set BLE runtime state to disabled if BLE is not enabled in coex_mode
    rsi_ble_set_opermode_state(false);
  }
#endif

  status = sli_wifi_send_command(SLI_WIFI_REQ_DYNAMIC_POOL,
                                 SLI_WIFI_WLAN_CMD,
                                 &config->ta_pool,
                                 sizeof(sl_wifi_system_dynamic_pool_t),
                                 SLI_WIFI_RSP_DYNAMIC_POOL_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);

  // Configure various wireless features
  sli_wifi_feature_frame_config_t current_config = sli_wifi_get_feature_frame_config();

  sli_wifi_feature_frame_request feature_frame_request = { .pll_mode        = (uint8_t)current_config.pll_mode,
                                                           .rf_type         = RF_TYPE,
                                                           .wireless_mode   = (uint8_t)current_config.power_chain,
                                                           .enable_ppp      = ENABLE_PPP,
                                                           .afe_type        = AFE_TYPE,
                                                           .feature_enables = SLI_FEATURE_ENABLES };

  // Set PLL mode to 1 when 120 MHz or 160 MHz SoC clock is configured
  if (boot_config.custom_feature_bit_map
      & (SL_SI91X_CUSTOM_FEAT_SOC_CLK_CONFIG_160MHZ | SL_SI91X_CUSTOM_FEAT_SOC_CLK_CONFIG_120MHZ)) {
    feature_frame_request.pll_mode = 1;
  } else {
    feature_frame_request.pll_mode = 0;
  }

  // For the transmit test mode we need to disable BIT 0, 4, 5. These bitmaps are only required in powersave.
  // To receive broadcast data packets in transceiver opermode, we need to enable BIT 1.
  if (config->boot_config.oper_mode == SL_SI91X_TRANSMIT_TEST_MODE) {
    feature_frame_request.feature_enables &= ~(SLI_FEATURE_ENABLES);
  } else if (config->boot_config.oper_mode == SL_SI91X_TRANSCEIVER_MODE) {
    feature_frame_request.feature_enables |= SLI_FEAT_FRAME_PERMIT_UNDESTINED_PACKETS;
  } else {
    feature_frame_request.feature_enables = feature_frame_request.feature_enables;
  }

  // Dispatch a feature request frame to the SI91x driver
  status = sli_wifi_send_command(SLI_COMMON_REQ_FEATURE_FRAME,
                                 SLI_WIFI_COMMON_CMD,
                                 &feature_frame_request,
                                 sizeof(feature_frame_request),
                                 SLI_COMMON_RSP_FEATURE_FRAME_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);

  // if 16th bit of ext_tcp_ip_feature_bit_map is not set, then firmware auto closes the TCP socket on remote termination.
  sli_save_tcp_auto_close_choice(
    (config->boot_config.ext_tcp_ip_feature_bit_map & SL_SI91X_EXT_TCP_IP_WAIT_FOR_SOCKET_CLOSE) == 0);

#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
  sli_si91x_socket_init(SLI_SI91X_GET_TCP_IP_TOTAL_SELECTS_BITS(config->boot_config.ext_tcp_ip_feature_bit_map));
  VERIFY_STATUS_AND_RETURN(status);
#endif

  // Set the MAC address if provided in the configuration
  if (config->mac_address != NULL) {
    status = sl_wifi_set_mac_address(default_interface, config->mac_address);
    VERIFY_STATUS_AND_RETURN(status);
  }

  // Initialize the WiFi radio if the coexistence mode is not BLE
  if (config->boot_config.coex_mode != SL_SI91X_BLE_MODE) {
    status = sl_si91x_driver_init_wifi_radio(config);
    VERIFY_STATUS_AND_RETURN(status);
  }

  // Check and update the frontend switch control based on custom feature bit map
  if (config->boot_config.custom_feature_bit_map & SL_WIFI_SYSTEM_CUSTOM_FEAT_EXTENSION_VALID) {
    frontend_switch_control = (config->boot_config.ext_custom_feature_bit_map & (BIT(29) | (BIT(30))));
  }

#ifdef SLI_SI91X_MCU_INTERFACE
  // Program wireless GPIO front-end switch controls
  if (frontend_switch_control != 0) {
    sli_si91x_configure_wireless_frontend_controls(frontend_switch_control);
  }
#endif
  sli_si91x_set_device_initialized_status(config);
  // Save the coexistence mode in the driver
  sli_save_coex_mode(config->boot_config.coex_mode);
#ifdef SL_SI91X_GET_EFUSE_DATA
  {
    sli_wifi_efuse_data_t efuse_data;
    status = sli_si91x_get_flash_efuse_data(&efuse_data, config->efuse_data_type);
    if (status == SL_STATUS_OK) {
      sli_si91x_set_efuse_data(&efuse_data);
    }
  }
#endif
#ifdef SLI_SI91X_MCU_INTERFACE
  if (status == SL_STATUS_OK) {
    /* send a notification to the NWP indicating whether the M4 core is currently utilizing the XTAL as its clock source*/
    sli_si91x_send_m4_xtal_usage_notification_to_ta();
  }
#endif

  return status;
}

sl_status_t sl_si91x_driver_deinit(void)
{
  sl_status_t status = SL_STATUS_OK;

  // Check if the device has been initialized if not, return an error
  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  // Disable the bus interrupt first: nothing may touch host buffers/pools
  // while they are being torn down (lab-port fix for incomplete power-down).
  sl_si91x_host_disable_bus_interrupt();

#ifdef SLI_SI91X_MCU_INTERFACE
  // If the SLI_SI91X_MCU_INTERFACE is defined, perform a soft reset
  status = sl_si91x_soft_reset();
  VERIFY_STATUS_AND_RETURN(status);

  // Mask specific interrupts related to packet transfer
  mask_ta_interrupt(SLI_TX_PKT_TRANSFER_DONE_INTERRUPT | SLI_RX_PKT_TRANSFER_DONE_INTERRUPT);
#endif

#ifdef SLI_SI91X_ENABLE_BLE
  int32_t rsi_status = 0;
  // If SLI_SI91X_ENABLE_BLE is defined, deinitialize the BLE driver and check for errors
  rsi_status = rsi_ble_driver_deinit();
  if (rsi_status != RSI_SUCCESS) {
    return SL_STATUS_FAIL;
  }
#endif

  // Deinitialize the SI91x platform
  status = sli_si91x_wifi_platform_deinit();
  VERIFY_STATUS_AND_RETURN(status);

#if defined(SLI_SI91X_OFFLOAD_NETWORK_STACK) && defined(SLI_SI91X_SOCKETS)
  // Shutdown and change the state of the client VAP sockets
  status = sli_si91x_vap_shutdown(SL_WIFI_CLIENT_VAP_ID, SLI_SI91X_BSD_DISCONNECT_REASON_INTERFACE_DOWN);
  VERIFY_STATUS_AND_RETURN(status);

  // Shutdown and change the state of the AP VAP sockets
  status = sli_si91x_vap_shutdown(SL_WIFI_AP_VAP_ID, SLI_SI91X_BSD_DISCONNECT_REASON_INTERFACE_DOWN);
  VERIFY_STATUS_AND_RETURN(status);

  // Deinitialize and free all socket-related resources
  status = sli_si91x_socket_deinit();
  VERIFY_STATUS_AND_RETURN(status);

#endif // End of check for offloaded network stack and sockets

#ifdef SLI_SI91X_MCU_INTERFACE
  // Check the RX buffer valid bit is set or not.
  if (M4SS_P2P_INTR_SET_REG & RX_BUFFER_VALID) {

    // Clear the RX buffer valid bit.
    M4SS_P2P_INTR_CLR_REG = (RX_BUFFER_VALID);

    // Clear the RX buffer.
    sli_buffer_manager_free_buffer(rx_pkt_buffer);
  }
#endif

  status = sl_si91x_bus_deinit();
  VERIFY_STATUS_AND_RETURN(status);

  // Deinitialize the buffer manager.
  // When the WiFi chip malfunctions this used to fail with BUSY (pools not
  // fully drained) and the VERIFY below aborted the teardown BEFORE the
  // power cycle and the device_initialized reset — a half-torn-down zombie.
  // sli_buffer_manager_deinit() now force-frees and always returns OK
  // (ponytail; see the buffer manager for details).
  status = sli_buffer_manager_deinit();
  VERIFY_STATUS_AND_RETURN(status);

  status = sl_si91x_host_power_cycle();
  VERIFY_STATUS_AND_RETURN(status);

  // Clear the event handler and reset initialization status
  si91x_event_handler  = NULL;
  device_initialized   = false;
  initialized_opermode = SLI_WIFI_INVALID_MODE;

  // Reset config feature bit map
  sli_si91x_set_config_feature_bit_map(0);

  // Reset all the interfaces
  memset(interface_is_up, 0, sizeof(interface_is_up));

  // Deinitialize the SI91x host last so power-cycling completes cleanly
  // (lab-port fix: running it before power_cycle left the chip powered).
  status = sl_si91x_host_deinit();
  VERIFY_STATUS_AND_RETURN(status);

  return status;
}

sl_status_t sl_si91x_driver_raw_send_command(uint8_t command,
                                             const void *data,
                                             uint32_t data_length,
                                             uint32_t wait_time)
{
  UNUSED_PARAMETER(wait_time);
  sl_wifi_system_packet_t *packet = NULL;
  sl_status_t status              = SL_STATUS_OK;

  // Reject payloads that cannot fit in one pool block
  if (data_length > SLI_WIFI_MAX_PACKET_PAYLOAD) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Allocate a buffer for the command with appropriate size
  status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_DATA_POOL,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                              SLI_WIFI_ALLOCATE_COMMAND_BUFFER_WAIT_TIME,
                                              (sli_buffer_t)&packet);
  VERIFY_STATUS_AND_RETURN(status);

  // If the packet is not allocated successfully, return an allocation failed error
  if (packet == NULL) {
    return SL_STATUS_ALLOCATION_FAILED;
  }
  // Clear the packet descriptor and copy the command data if available
  memset(packet->desc, 0, sizeof(packet->desc));
  if (data != NULL) {
    memcpy(packet->data, data, data_length);
  }
  packet->length  = data_length & 0xFFF;
  packet->desc[2] = command & 0xFF;
  packet->desc[3] = (command >> 8) & 0xFF;
  // Fill the firmware queue id
  packet->desc[1] |= (SLI_WLAN_DATA_Q << 4);

  // Route (send) the packet via routing utility (may be async).
  uint32_t packet_size = (packet->length & 0xFFF) + sizeof(sl_wifi_system_packet_t);
  status               = sli_routing_utility_route_packet(&wifi_command_engine_routing_table,
                                            SLI_WIFI_DATA_PACKET,
                                            packet,
                                            (uint16_t)packet_size,
                                            NULL);
  return (SL_STATUS_IN_PROGRESS == status) ? SL_STATUS_OK : status;
}

#ifdef SL_SI91X_SIDE_BAND_CRYPTO
sl_status_t sl_si91x_driver_send_side_band_crypto(uint32_t command,
                                                  const void *data,
                                                  uint32_t data_length,
                                                  sli_wifi_wait_period_t wait_period)
{
  sl_wifi_buffer_t *buffer        = NULL;
  sl_wifi_system_packet_t *packet = NULL;
  uint32_t result;
  sl_status_t status = SL_STATUS_OK;

  // Allocate a buffer for the command with appropriate size
  status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_TX_POOL,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                              SLI_WIFI_ALLOCATE_COMMAND_BUFFER_WAIT_TIME,
                                              (sli_buffer_t)&buffer);
  VERIFY_STATUS_AND_RETURN(status);
  packet = (sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(buffer, 0, NULL);

  // Clear the packet descriptor and copy the command data if available
  memset(packet->desc, 0, sizeof(packet->desc));
  if (data != NULL) {
    memcpy(packet->data, data, data_length);
  }

  // Fill frame type
  packet->length  = data_length & 0xFFF;
  packet->command = command;

  // Acquire Mutex
  osMutexAcquire(side_band_crypto_mutex, 0xFFFFFFFFUL);

  // fill crypto desc
  crypto_desc[0].addr   = (uint32_t)packet->desc;
  crypto_desc[0].length = SL_HOST_DESC_LEN;
  crypto_desc[1].addr   = (uint32_t)packet->data;
  crypto_desc[1].length = SL_CRYPTO_PKT_LEN;

  //! Enter Critical Section
  __disable_irq();

  sli_si91x_raise_side_band_interrupt_to_ta();

  //! Exit Critical Section
  __enable_irq();

  result =
    osEventFlagsWait(ta_events, SLI_SIDE_BAND_DONE, (osFlagsWaitAny), (wait_period & ~SLI_WIFI_WAIT_FOR_RESPONSE_BIT));
  if (result == (uint32_t)osErrorTimeout || result == (uint32_t)osErrorResource) {
    osMutexRelease(side_band_crypto_mutex);
    sli_buffer_manager_free_buffer(buffer);
    return SL_STATUS_TIMEOUT;
  }

  status =
    (((uint8_t *)crypto_desc[0].addr)[12] + (((uint8_t *)crypto_desc[0].addr)[13] << 8)); // Extract the frame status

  // Release Mutex
  osMutexRelease(side_band_crypto_mutex);

  sli_buffer_manager_free_buffer(buffer);
  return status;
}
#endif

sl_status_t sl_si91x_enable_radio(void)
{
  uint8_t data = 1;
  sl_status_t status =
    sli_wifi_send_command(SLI_WIFI_REQ_RADIO, SLI_WIFI_WLAN_CMD, &data, 1, SLI_WIFI_RSP_RADIO_WAIT_TIME, NULL, NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_disable_radio(void)
{
  uint8_t data = 0;
  sl_status_t status =
    sli_wifi_send_command(SLI_WIFI_REQ_RADIO, SLI_WIFI_WLAN_CMD, &data, 1, SLI_WIFI_RSP_RADIO_WAIT_TIME, NULL, NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_write_calibration_data(const si91x_calibration_data_t *data)
{
  sl_status_t status = sli_wifi_send_command(SLI_WIFI_REQ_CALIB_WRITE,
                                             SLI_WIFI_WLAN_CMD,
                                             data,
                                             sizeof(si91x_calibration_data_t),
                                             SLI_WIFI_RSP_CALIB_WRITE_WAIT_TIME,
                                             NULL,
                                             NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_wifi_set_certificate_index(uint8_t certificate_type,
                                                uint8_t certificate_index,
                                                const uint8_t *buffer,
                                                uint32_t certificate_length)
{
  uint32_t rem_len                          = 0;
  uint16_t chunk_size                       = 0;
  uint16_t data_size                        = 0;
  uint8_t chunks_remaining                  = 0;
  uint32_t offset                           = 0;
  sl_status_t status                        = SL_STATUS_OK;
  sli_si91x_req_set_certificate_t chunk_ptr = { 0 };

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  // Get the certificate chunk size
  chunk_size = (SLI_SI91X_MAX_CERT_SEND_SIZE - sizeof(sli_si91x_cert_info_t));

  // Get certificate length
  rem_len = certificate_length;

  do {
    // Memset the PKT
    memset(&chunk_ptr, 0, sizeof(sli_si91x_req_set_certificate_t));

    // If certificate length is 0, erase certificate
    if (rem_len == 0) {
      // Mark Data Size
      data_size = 0;

      // More chunks to send
      chunks_remaining = 0;
    } else if (rem_len >= chunk_size) {
      // Mark Data Size
      data_size = chunk_size;

      // More chunks to send
      chunks_remaining = 1;
    } else {
      // Mark Data Size
      data_size = (uint16_t)rem_len;

      // More chunks to send
      chunks_remaining = 0;
    }

    if ((data_size > 0) && (NULL != buffer)) {
      // Copy the certificate chunk
      memcpy(chunk_ptr.certificate, buffer + offset, data_size);
    }

    // Move the offset by chunk size
    offset += data_size;

    // Subtract the rem_len by the chunk size
    rem_len -= data_size;

    //Set the total length of certificate
    memcpy(&chunk_ptr.cert_info.total_len, &certificate_length, sizeof(chunk_ptr.cert_info.total_len));

    // Set the certificate type
    chunk_ptr.cert_info.certificate_type = certificate_type;

    // Set the certificate index
    chunk_ptr.cert_info.certificate_inx = certificate_index;

    // More chunks to send
    chunk_ptr.cert_info.more_chunks = chunks_remaining;

    // Set the length of the certificate chunk
    chunk_ptr.cert_info.certificate_length = data_size;

    // Send the driver command
    status = sli_wifi_send_command(SLI_WIFI_REQ_SET_CERTIFICATE,
                                   SLI_WIFI_WLAN_CMD,
                                   &chunk_ptr,
                                   (sizeof(sli_si91x_cert_info_t) + data_size),
                                   SLI_WIFI_RSP_SET_CERTIFICATE_WAIT_TIME,
                                   NULL,
                                   NULL);
    VERIFY_STATUS_AND_RETURN(status);
  } while (rem_len > 0);

  // Return status
  return status;
}

sl_status_t sl_si91x_set_rtc_timer(const sl_si91x_module_rtc_time_t *timer)
{
  sl_status_t status = SL_STATUS_OK;

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if ((timer->tm_sec > 59) || (timer->tm_min > 59) || (timer->tm_hour > 23)
      || ((timer->tm_mday < 1) || (timer->tm_mday > 31)) || (timer->tm_mon > 11)
      || ((timer->tm_wday < 1) || (timer->tm_wday > 7))) {

    // Checking Invalid Parameters
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Send set RTC timer request
  status = sli_wifi_send_command(SLI_COMMON_REQ_SET_RTC_TIMER,
                                 SLI_WIFI_COMMON_CMD,
                                 timer,
                                 sizeof(sl_si91x_module_rtc_time_t),
                                 SLI_COMMON_RSP_SET_RTC_TIMER_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_get_rtc_timer(sl_si91x_module_rtc_time_t *response)
{
  sl_status_t status       = SL_STATUS_OK;
  sl_wifi_buffer_t *buffer = NULL;

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  SL_WIFI_ARGS_CHECK_NULL_POINTER(response);

  // Send get RTC timer request
  status = sli_wifi_send_command(SLI_COMMON_REQ_GET_RTC_TIMER,
                                 SLI_WIFI_COMMON_CMD,
                                 NULL,
                                 0,
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_COMMON_RSP_GET_RTC_TIMER_WAIT_TIME),
                                 NULL,
                                 (void **)&buffer);

  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer(buffer);
  }
  VERIFY_STATUS_AND_RETURN(status);

  // Extract the RTC timer data from the response
  const sl_wifi_system_packet_t *packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);
  memcpy(response, packet->data, sizeof(sl_si91x_module_rtc_time_t));
  sli_buffer_manager_free_buffer(buffer);
  return SL_STATUS_OK;
}

sl_status_t sl_si91x_set_device_region(sl_wifi_operation_mode_t operation_mode,
                                       sl_wifi_band_mode_t band,
                                       sl_wifi_region_code_t region_code)
{
  if (band == SL_WIFI_BAND_MODE_5GHZ || band == SL_WIFI_DUAL_BAND_MODE) {
    return SL_STATUS_NOT_SUPPORTED;
  }
  return sli_wifi_set_device_region(operation_mode, band, region_code);
}

#ifdef SLI_SI91X_MCU_INTERFACE

sl_status_t sl_si91x_command_to_write_common_flash(uint32_t write_address,
                                                   const uint8_t *write_data,
                                                   uint16_t write_data_length,
                                                   uint8_t flash_sector_erase_enable)
{
  // Check if write_data_length is non-zero
  if (write_data_length == 0) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sl_status_t status                         = SL_STATUS_OK;
  sli_si91x_request_ta2m4_t ta_to_m4_request = { 0 };
  uint32_t send_size                         = 0;
  uint16_t remaining_length                  = write_data_length;

  // If flash_sector_erase_enable is 1, Send request to NWP in chunks of 4k
  if (flash_sector_erase_enable == 1) {
    while (remaining_length > 0) {
      // Calculate the chunk size, capped at 4k
      size_t chunkSize = (remaining_length < FLASH_SECTOR_SIZE) ? remaining_length : FLASH_SECTOR_SIZE;

      // Fill the request structure
      memset(&ta_to_m4_request, 0, sizeof(sli_si91x_request_ta2m4_t));
      ta_to_m4_request.sub_cmd                   = SL_SI91X_WRITE_TO_COMMON_FLASH;
      ta_to_m4_request.addr                      = write_address;
      ta_to_m4_request.input_buffer_length       = (uint16_t)chunkSize;
      ta_to_m4_request.flash_sector_erase_enable = flash_sector_erase_enable;

      send_size = sizeof(sli_si91x_request_ta2m4_t);

      status = sli_wifi_send_command(SLI_COMMON_REQ_TA_M4_COMMANDS,
                                     SLI_WIFI_COMMON_CMD,
                                     &ta_to_m4_request,
                                     send_size,
                                     SLI_COMMON_RSP_TA_M4_COMMANDS_WAIT_TIME,
                                     NULL,
                                     NULL);
      VERIFY_STATUS_AND_RETURN(status);

      // Adjust write_address for the next chunk
      write_address += chunkSize;

      // Adjust remaining_length for the next chunk
      remaining_length -= chunkSize;
    }
  }

  else {
    // Check if write_data pointer is valid
    SL_VERIFY_POINTER_OR_RETURN(write_data, SL_STATUS_INVALID_PARAMETER);

    // Write in chunks of MAX_CHUNK_SIZE for flash_sector_erase_enable != 1
    while (write_data_length > 0) {
      size_t chunkSize = (write_data_length < MAX_CHUNK_SIZE) ? write_data_length : MAX_CHUNK_SIZE;

      // Fill the request structure
      memset(&ta_to_m4_request, 0, sizeof(sli_si91x_request_ta2m4_t));
      ta_to_m4_request.sub_cmd                   = SL_SI91X_WRITE_TO_COMMON_FLASH;
      ta_to_m4_request.addr                      = write_address;
      ta_to_m4_request.input_buffer_length       = (uint16_t)chunkSize;
      ta_to_m4_request.flash_sector_erase_enable = flash_sector_erase_enable;

      // Copy write_data into the request structure
      memcpy(&ta_to_m4_request.input_data, write_data, chunkSize);

      // Calculate the send size and send the command to write to common flash
      send_size = sizeof(sli_si91x_request_ta2m4_t) - MAX_CHUNK_SIZE + chunkSize;
      status    = sli_wifi_send_command(SLI_COMMON_REQ_TA_M4_COMMANDS,
                                     SLI_WIFI_COMMON_CMD,
                                     &ta_to_m4_request,
                                     send_size,
                                     SLI_COMMON_RSP_TA_M4_COMMANDS_WAIT_TIME,
                                     NULL,
                                     NULL);
      VERIFY_STATUS_AND_RETURN(status);

      // Adjust pointers and counters
      write_address += chunkSize;
      write_data += chunkSize;
      write_data_length -= chunkSize;
    }
  }
  return status;
}

sl_status_t sl_si91x_command_to_read_common_flash(uint32_t read_address, size_t length, uint8_t *output_buffer)
{
  // Check if output_buffer pointer is valid
  SL_VERIFY_POINTER_OR_RETURN(output_buffer, SL_STATUS_INVALID_PARAMETER);

  // Check if length is non-zero
  if (length == 0) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sl_status_t status                    = SL_STATUS_OK;
  sl_wifi_buffer_t *buffer              = NULL;
  const sl_wifi_system_packet_t *packet = NULL;

  while (length > 0) {
    size_t chunkSize = (length < MAX_CHUNK_SIZE) ? length : MAX_CHUNK_SIZE;

    sli_si91x_read_flash_request_t m4_to_ta_read_request = { 0 };
    memset(&m4_to_ta_read_request, 0, sizeof(sli_si91x_read_flash_request_t));
    m4_to_ta_read_request.sub_cmd              = SL_SI91X_READ_FROM_COMMON_FLASH;
    m4_to_ta_read_request.nwp_address          = read_address;
    m4_to_ta_read_request.output_buffer_length = (uint16_t)chunkSize;

    uint32_t send_size = sizeof(sli_si91x_read_flash_request_t);

    status = sli_wifi_send_command(SLI_COMMON_REQ_TA_M4_COMMANDS,
                                   SLI_WIFI_COMMON_CMD,
                                   &m4_to_ta_read_request,
                                   send_size,
                                   SLI_WIFI_WAIT_FOR_RESPONSE(SLI_COMMON_RSP_TA_M4_COMMANDS_WAIT_TIME),
                                   NULL,
                                   (void **)&buffer);
    if (status != SL_STATUS_OK) {
      if (buffer != NULL)
        sli_buffer_manager_free_buffer(buffer);
      return status;
    }
    VERIFY_STATUS_AND_RETURN(status);

    packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);
    memcpy(output_buffer, packet->data, packet->length);
    sli_buffer_manager_free_buffer(buffer);

    // Adjust pointers and counters
    read_address += chunkSize;
    output_buffer += chunkSize;
    length -= chunkSize;
  }

  return status;
}

sl_status_t sl_si91x_m4_ta_secure_handshake(uint8_t sub_cmd_type,
                                            uint8_t input_len,
                                            const uint8_t *input_data,
                                            uint8_t output_len,
                                            const uint8_t *output_data)
{
  UNUSED_PARAMETER(output_len);
  UNUSED_PARAMETER(output_data);
  sli_si91x_ta_m4_handshake_parameters_t *handshake_request = NULL;
  sl_status_t status                                        = SL_STATUS_OK;

  SL_VERIFY_POINTER_OR_RETURN(input_data, SL_STATUS_INVALID_PARAMETER);

  handshake_request = SLI_MALLOC(sizeof(sli_si91x_ta_m4_handshake_parameters_t) + input_len);
  SL_VERIFY_POINTER_OR_RETURN(handshake_request, SL_STATUS_ALLOCATION_FAILED);
  memset(handshake_request, 0, sizeof(sli_si91x_ta_m4_handshake_parameters_t) + input_len);
  handshake_request->sub_cmd         = sub_cmd_type;
  handshake_request->input_data_size = input_len;
  memcpy(handshake_request->input_data, input_data, input_len);

  // Send the secure handshake command to the M4 core
  status = sli_wifi_send_command(SLI_COMMON_REQ_TA_M4_COMMANDS,
                                 SLI_WIFI_COMMON_CMD,
                                 handshake_request,
                                 sizeof(sli_si91x_ta_m4_handshake_parameters_t) + input_len,
                                 SLI_COMMON_RSP_TA_M4_COMMANDS_WAIT_TIME,
                                 NULL,
                                 NULL);
  SLI_FREE(handshake_request);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_configure_timestamp_memory_location(uint8_t addr_len, const uint32_t *address)
{
  sli_si91x_ta_m4_handshake_parameters_t *handshake_request = NULL;
  sl_status_t status                                        = SL_STATUS_OK;

  SL_VERIFY_POINTER_OR_RETURN(address, SL_STATUS_INVALID_PARAMETER);

  handshake_request = SLI_MALLOC(sizeof(sli_si91x_ta_m4_handshake_parameters_t) + addr_len);
  SL_VERIFY_POINTER_OR_RETURN(handshake_request, SL_STATUS_ALLOCATION_FAILED);
  memset(handshake_request, 0, sizeof(sli_si91x_ta_m4_handshake_parameters_t) + addr_len);
  handshake_request->sub_cmd         = SL_SI91X_SET_TIMESTAMP_MEMORY_ADDRESS;
  handshake_request->input_data_size = addr_len;
  memcpy(handshake_request->input_data, address, addr_len);

  status = sli_wifi_send_command(SLI_COMMON_REQ_TA_M4_COMMANDS,
                                 SLI_WIFI_COMMON_CMD,
                                 handshake_request,
                                 sizeof(sli_si91x_ta_m4_handshake_parameters_t) + addr_len,
                                 SLI_COMMON_RSP_TA_M4_COMMANDS_WAIT_TIME,
                                 NULL,
                                 NULL);
  SLI_FREE(handshake_request);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_read_status(sl_si91x_read_status_t read_id, uint8_t *output)
{
  sl_wifi_buffer_t *buffer              = NULL;
  const sl_wifi_system_packet_t *packet = NULL;
  sl_status_t status                    = SL_STATUS_OK;
  sli_si91x_read_status_t read_request  = { 0 };

  SL_VERIFY_POINTER_OR_RETURN(output, SL_STATUS_NULL_POINTER);

  if ((read_id < SL_SI91X_READ_NWP_DEBUG_PORT_STATUS) || (read_id > SL_SI91X_READ_MCU_DEBUG_PORT_STATUS)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  read_request.sub_cmd = (uint8_t)read_id;
  status               = sli_wifi_send_command(SLI_COMMON_REQ_TA_M4_COMMANDS,
                                 SLI_WIFI_COMMON_CMD,
                                 &read_request,
                                 sizeof(sli_si91x_read_status_t),
                                 SLI_COMMON_RSP_TA_M4_COMMANDS_WAIT_TIME,
                                 NULL,
                                 (void **)&buffer);
  if (status != SL_STATUS_OK) {
    if (buffer != NULL)
      sli_buffer_manager_free_buffer(buffer);
    return status;
  }
  VERIFY_STATUS_AND_RETURN(status);

  packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);
  if (packet->length > 0)
    memcpy(output, packet->data, packet->length);
  sli_buffer_manager_free_buffer(buffer);

  return status;
}

// Perform a soft reset
static sl_status_t sl_si91x_soft_reset(void)
{
  sl_status_t status;
  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  status = sli_wifi_send_command(SLI_COMMON_REQ_SOFT_RESET,
                                 SLI_WIFI_COMMON_CMD,
                                 NULL,
                                 0,
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_COMMON_RSP_TA_M4_COMMANDS_WAIT_TIME),
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}
#endif

sl_status_t sl_si91x_assert()
{
  sl_status_t status = SL_STATUS_OK;

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  status = sli_wifi_send_command(SLI_COMMON_REQ_ASSERT,
                                 SLI_WIFI_WLAN_CMD,
                                 NULL,
                                 0,
                                 SLI_COMMON_RSP_ASSERT_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_get_ram_log(uint32_t address, uint32_t length)
{
  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  sl_status_t status       = SL_STATUS_OK;
  sli_si91x_ram_dump_t ram = { .address = address, .length = length };

  // Check for invalid length parameter
  if (length == 0) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  // Send RAM log request
  status = sli_wifi_send_command(SLI_COMMON_REQ_GET_RAM_DUMP,
                                 SLI_WIFI_COMMON_CMD,
                                 &ram,
                                 sizeof(sli_si91x_ram_dump_t),
                                 SLI_COMMON_RSP_GET_RAM_DUMP_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_transmit_test_start(const sl_wifi_request_tx_test_info_t *tx_test_info)
{
  sl_status_t status = SL_STATUS_OK;

  status = sli_wifi_send_command(SLI_WIFI_REQ_TX_TEST_MODE,
                                 SLI_WIFI_WLAN_CMD,
                                 tx_test_info,
                                 sizeof(sl_wifi_request_tx_test_info_t),
                                 SLI_WIFI_RSP_TX_TEST_MODE_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_transmit_test_stop(void)
{
  sl_status_t status                          = SL_STATUS_OK;
  sl_wifi_request_tx_test_info_t tx_test_info = { 0 };
  tx_test_info.enable                         = 0;
  // Send the transmit test stop command
  status = sli_wifi_send_command(SLI_WIFI_REQ_TX_TEST_MODE,
                                 SLI_WIFI_WLAN_CMD,
                                 &tx_test_info,
                                 sizeof(sl_wifi_request_tx_test_info_t),
                                 SLI_WIFI_RSP_TX_TEST_MODE_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_calibration_write(sl_si91x_calibration_write_t calib_write)
{
  sl_status_t status = SL_STATUS_OK;

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  status = sli_wifi_send_command(SLI_WIFI_REQ_CALIB_WRITE,
                                 SLI_WIFI_WLAN_CMD,
                                 &calib_write,
                                 sizeof(sl_si91x_calibration_write_t),
                                 SLI_WIFI_RSP_CALIB_WRITE_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_calibration_read(sl_si91x_calibration_read_t target, sl_si91x_calibration_read_t *calibration_read)
{
  sl_wifi_buffer_t *buffer = NULL;
  sl_status_t status       = SL_STATUS_OK;

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  SL_VERIFY_POINTER_OR_RETURN(calibration_read, SL_STATUS_NULL_POINTER);

  status = sli_wifi_send_command(SLI_WIFI_REQ_CALIB_READ,
                                 SLI_WIFI_WLAN_CMD,
                                 &target,
                                 sizeof(sl_si91x_calibration_read_t),
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_WIFI_RSP_CALIB_READ_WAIT_TIME),
                                 NULL,
                                 (void **)&buffer);

  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer(buffer);
    return status;
  }

  const sl_wifi_system_packet_t *packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);
  memcpy(calibration_read, packet->data, sizeof(sl_si91x_calibration_read_t));
  sli_buffer_manager_free_buffer(buffer);
  return status;
}

sl_status_t sl_si91x_frequency_offset(const sl_si91x_freq_offset_t *frequency_calibration)
{
  sl_status_t status = SL_STATUS_OK;

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  SL_VERIFY_POINTER_OR_RETURN(frequency_calibration, SL_STATUS_NULL_POINTER);

  // Send the frequency offset calibration command to the SI91x WLAN module
  status = sli_wifi_send_command(SLI_WIFI_REQ_FREQ_OFFSET,
                                 SLI_WIFI_WLAN_CMD,
                                 frequency_calibration,
                                 sizeof(sl_si91x_freq_offset_t),
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_WIFI_RSP_FREQ_OFFSET_WAIT_TIME),
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_evm_offset(const sl_si91x_evm_offset_t *evm_offset)
{
  sl_status_t status = SL_STATUS_OK;

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  SL_VERIFY_POINTER_OR_RETURN(evm_offset, SL_STATUS_NULL_POINTER);

  status = sli_wifi_send_command(SLI_WIFI_REQ_EVM_OFFSET,
                                 SLI_WIFI_WLAN_CMD,
                                 evm_offset,
                                 sizeof(sl_si91x_evm_offset_t),
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_RSP_EVM_OFFSET_WAIT_TIME),
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return SL_STATUS_OK;
}

sl_status_t sl_si91x_evm_write(const sl_si91x_evm_write_t *evm_write)
{
  sl_status_t status = SL_STATUS_OK;

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  SL_VERIFY_POINTER_OR_RETURN(evm_write, SL_STATUS_NULL_POINTER);

  status = sli_wifi_send_command(SLI_WIFI_REQ_EVM_WRITE,
                                 SLI_WIFI_WLAN_CMD,
                                 evm_write,
                                 sizeof(sl_si91x_evm_write_t),
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_WIFI_RSP_EVM_WRITE_WAIT_TIME),
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return SL_STATUS_OK;
}

sl_status_t sl_si91x_dpd_calibration(const sl_si91x_get_dpd_calib_data_t *dpd_calib_data)
{
  sl_status_t status = SL_STATUS_OK;

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  SL_VERIFY_POINTER_OR_RETURN(dpd_calib_data, SL_STATUS_NULL_POINTER);

  status = sli_wifi_send_command(SLI_WIFI_REQ_GET_DPD_DATA,
                                 SLI_WIFI_WLAN_CMD,
                                 dpd_calib_data,
                                 sizeof(sl_si91x_get_dpd_calib_data_t),
                                 SLI_WIFI_RSP_GET_DPD_DATA_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_si91x_efuse_read(const sl_si91x_efuse_read_t *efuse_read, uint8_t *efuse_read_buf)
{
  sl_wifi_buffer_t *buffer = NULL;
  sl_status_t status       = SL_STATUS_OK;

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  SL_VERIFY_POINTER_OR_RETURN(efuse_read, SL_STATUS_NULL_POINTER);
  SL_VERIFY_POINTER_OR_RETURN(efuse_read_buf, SL_STATUS_NULL_POINTER);

  status = sli_wifi_send_command(SLI_WIFI_REQ_EFUSE_READ,
                                 SLI_WIFI_WLAN_CMD,
                                 efuse_read,
                                 sizeof(sl_si91x_efuse_read_t),
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_WIFI_RSP_EFUSE_READ_WAIT_TIME),
                                 NULL,
                                 (void **)&buffer);

  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer(buffer);
    return status;
  }

  const sl_wifi_system_packet_t *packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);
  memcpy(efuse_read_buf, packet->data, efuse_read->efuse_read_data_len);
  sli_buffer_manager_free_buffer(buffer);
  return status;
}

sl_status_t sl_si91x_set_join_configuration(sl_wifi_interface_t interface, uint8_t join_feature_bitmap)
{
  return sli_wifi_set_join_configuration(interface, join_feature_bitmap);
}

sl_status_t sl_si91x_get_join_configuration(sl_wifi_interface_t interface, uint8_t *join_feature_bitmap)
{
  SL_WIFI_ARGS_CHECK_NULL_POINTER(join_feature_bitmap);

  return sli_wifi_get_join_configuration(interface, join_feature_bitmap);
}

void sl_si91x_set_listen_interval(uint32_t listen_interval)
{
  sl_wifi_listen_interval_v2_t v2_params = {
    .listen_interval            = listen_interval,
    .listen_interval_multiplier = DEFAULT_LISTEN_INTERVAL_MULTIPLIER,
  };
  /* Deprecated API is void; cannot propagate sli_wifi status to caller. */
  (void)sli_wifi_set_listen_interval_v2(SL_WIFI_CLIENT_INTERFACE, v2_params);
  return;
}

void sl_si91x_set_timeout(const sl_wifi_timeout_t *timeout_config)
{
  memcpy(&timeout_glbl, timeout_config, sizeof(sl_wifi_timeout_t));
  return;
}

sl_status_t sl_si91x_configure_timeout(sl_wifi_timeout_type_t timeout_type, uint16_t timeout_value)
{
  if (timeout_type > SL_SI91X_CHANNEL_PASSIVE_SCAN_TIMEOUT) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  return sli_wifi_configure_timeout(SL_WIFI_CLIENT_INTERFACE, timeout_type, timeout_value);
}

sl_status_t sl_si91x_driver_send_transceiver_data(sl_wifi_transceiver_tx_data_control_t *control,
                                                  const uint8_t *payload,
                                                  uint16_t payload_len,
                                                  uint32_t wait_time)
{
  UNUSED_PARAMETER(wait_time);
  sl_wifi_system_packet_t *packet = NULL;
  sl_status_t status              = SL_STATUS_OK;
  uint8_t *pkt_offset;
  uint8_t ext_desc_size;
  uint8_t *host_desc;
  uint32_t mac_hdr_len = MAC80211_HDR_MIN_LEN;

  if (IS_QOS_PKT(control->ctrl_flags) && !IS_BCAST_MCAST_MAC(control->addr1[0])) {
    mac_hdr_len += MAC80211_HDR_QOS_CTRL_LEN;
  }

  if (IS_4ADDR(control->ctrl_flags)) {
    mac_hdr_len += MAC80211_HDR_ADDR4_LEN;
  }

  // Initialize ext_desc_size with the base size for transceiver TX data
  ext_desc_size = TRANSCEIVER_TX_DATA_EXT_DESC_SIZE;

  // Check if the control flags indicate an EIA packet
  // Note: Bits 6 and 7 of ctrl_flags, bit 0 of ctrl_flags1, and the channel and tx_power fields are currently not supported.
  if (SLI_IS_EIA_PKT(control->ctrl_flags)) {
    // If it is an EIA packet, add 5 bytes for the following fields: channel, tx_power, is_last_packet, reserved1, reserved2
    ext_desc_size += SLI_EXT_DESC_SIZE_IF_EIA_PKT;
  }

  // Reject frames that cannot fit in one pool block
  if ((uint32_t)(ext_desc_size + mac_hdr_len) + payload_len > SLI_WIFI_MAX_PACKET_PAYLOAD) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Allocate a buffer for the command with appropriate size
  status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_DATA_POOL,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                              SLI_WIFI_ALLOCATE_COMMAND_BUFFER_WAIT_TIME,
                                              (sli_buffer_t)&packet);
  VERIFY_STATUS_AND_RETURN(status);

  // If the packet is not allocated successfully, return an allocation failed error
  if (packet == NULL) {
    return SL_STATUS_ALLOCATION_FAILED;
  }

  pkt_offset = packet->data + ext_desc_size;
  status     = sli_encapsulate_tx_data_packet(control, pkt_offset, mac_hdr_len, feature_bit_map);
  if (status != SL_STATUS_OK) {
    sli_buffer_manager_free_buffer(packet);
    return status;
  }

  memcpy(pkt_offset + mac_hdr_len, payload, payload_len);

#ifdef TX_RX_FRAME_DUMP_BYTE_COUNT
  print_80211_packet(pkt_offset, mac_hdr_len + payload_len, TX_RX_FRAME_DUMP_BYTE_COUNT);
#endif

  // Clear the packet descriptor and copy the command data if available
  memset(packet->desc, 0, sizeof(packet->desc));

  // Fill length in first 2 host_desc bytes
  packet->length = (ext_desc_size + mac_hdr_len + payload_len) & 0xFFF;

  // fill the firmware queue id in the packet descriptor
  packet->desc[1] |= (SLI_WLAN_DATA_Q << 4);

  // Fill packet type
  host_desc = packet->desc;

  // Clear the extended descriptor if EIA is set
  if (SLI_IS_EIA_PKT(control->ctrl_flags)) {
    memset(packet->data, 0, ext_desc_size); //! Clear ext_desc_size bytes starting from packet->data
  }

  host_desc[2] = 0x01; //! Frame Type
  if (IS_CFM_TO_HOST_SET(control->ctrl_flags)) {
    host_desc[3] |= CONFIRM_REQUIRED_TO_HOST; //! This bit is used to set CONFIRM_REQUIRED_TO_HOST in firmware.
  }
  host_desc[4] = ext_desc_size;                     //! xtend_desc size
  host_desc[5] = (uint8_t)((mac_hdr_len + 3) & ~3); //! Mac_header length

  if (IS_BCAST_MCAST_MAC(control->addr1[0])) {
    host_desc[7] |= BCAST_INDICATION; //! Bcast_indication
    //! If auto-rate is enabled for bcast/mcast pkts, use 1 Mbps
    if (!IS_FIXED_DATA_RATE(control->ctrl_flags)) {
      host_desc[6] |= MAC_INFO_ENABLE; //! Fixed Rate
      host_desc[8] = SL_WIFI_DATA_RATE_1;
    }
  }

  if (IS_FIXED_DATA_RATE(control->ctrl_flags)) {
    host_desc[6] |= MAC_INFO_ENABLE; //! Fixed Rate
    host_desc[8] = (uint8_t)control->rate;
  }

  if (IS_QOS_PKT(control->ctrl_flags) && !IS_BCAST_MCAST_MAC(control->addr1[0])) {
    host_desc[13] |= QOS_ENABLE; // QOS ENABLE
  }

  host_desc[14] =
    (uint8_t)(((WME_AC_TO_TID(control->priority) & 0xf) << 4) | (WME_AC_TO_QNUM(control->priority) & 0xf));

  //! Initialize extended desc
  memcpy(&host_desc[16], &control->token, TRANSCEIVER_TX_DATA_EXT_DESC_SIZE);

  // Note: Bits 6 and 7 of ctrl_flags, bit 0 of ctrl_flags1, and the channel and tx_power fields are currently not supported.
  //! If it is an EIA packet, update extended descriptor fields
  if (SLI_IS_EIA_PKT(control->ctrl_flags)) {
    host_desc[7] |= SLI_EIA_BIT_IN_HOST_DESC; //! EIA Enable
    if (SLI_IS_IMMEDIATE_TRF(control->ctrl_flags)) {
      host_desc[7] |= SLI_IMMEDIATE_TRF_HOST_DESC; //! Immediate Transfer
    }
    host_desc[20] = control->channel;  //! Channel
    host_desc[21] = control->tx_power; //! Transmission Power
    if (SLI_IS_LAST_PKT(control->ctrl_flags1)) {
      host_desc[22] |= SLI_LAST_PKT; //! Last Packet
    }
  }

  // invoke routing utility to send the packet to the firmware
  status = sli_routing_utility_route_packet(&wifi_command_engine_routing_table,
                                            SLI_WIFI_DATA_PACKET,
                                            packet,
                                            (packet->length & 0xFFF),
                                            NULL);
  return (SL_STATUS_IN_PROGRESS == status) ? SL_STATUS_OK : status;
}

sl_status_t sl_si91x_get_firmware_version(sl_si91x_firmware_version_t *version)
{
  sl_status_t status       = SL_STATUS_OK;
  sl_wifi_buffer_t *buffer = NULL;

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  SL_WIFI_ARGS_CHECK_NULL_POINTER(version);

  status = sli_wifi_send_command(SLI_WIFI_REQ_FULL_FW_VERSION,
                                 SLI_WIFI_WLAN_CMD,
                                 NULL,
                                 0,
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_WIFI_RSP_FULL_FW_VERSION_WAIT_TIME),
                                 NULL,
                                 (void **)&buffer);
  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer(buffer);
  }
  VERIFY_STATUS_AND_RETURN(status);

  const sl_wifi_system_packet_t *packet =
    (const sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(buffer, 0, NULL);

  if (packet == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  if (packet->length > 0) {
    const sl_wifi_firmware_version_t *response = (const sl_wifi_firmware_version_t *)packet->data;

    if (response == NULL) {
      return SL_STATUS_NULL_POINTER;
    }

    version->chip_id          = response->chip_id;
    version->rom_id           = response->rom_id;
    version->major            = response->major;
    version->minor            = response->minor;
    version->security_version = response->security_version;
    version->patch_num        = response->patch_num;
    version->customer_id      = response->customer_id;
    version->build_num        = (packet->data[7] | packet->data[8]);
  }

  sli_buffer_manager_free_buffer(buffer);
  return status;
}

sl_status_t sl_si91x_get_firmware_size(const void *buffer, uint32_t *fw_image_size)
{
  SL_WIFI_ARGS_CHECK_NULL_POINTER(buffer);
  const sl_wifi_firmware_header_t *firmware_header = (const sl_wifi_firmware_header_t *)buffer;

#ifdef SLI_SI91X_MCU_INTERFACE
  // Check for SLI_COMBINED_IMAGE flag in control_flags
  if (firmware_header->control_flags & SLI_COMBINED_IMAGE) {
    // If SLI_COMBINED_IMAGE is set, read the combined image size from firmware_header[48-51]
    *fw_image_size = *(const uint32_t *)((const uint8_t *)firmware_header + SLI_COMBINED_IMAGE_SIZE_OFFSET);
  } else {
    // If SLI_COMBINED_IMAGE is not set, calculate the single image size
    *fw_image_size = firmware_header->image_size + sizeof(sl_wifi_firmware_header_t);
  }
#else
  *fw_image_size = firmware_header->image_size + sizeof(sl_wifi_firmware_header_t);
#endif

  return SL_STATUS_OK;
}

sl_status_t sl_si91x_set_nwp_config_request(sl_si91x_nwp_configuration_t nwp_config)
{
  sl_status_t status = SL_STATUS_OK;

  // XTAL good time must be stored and applied during init (firmware not ready before sl_wifi_init)
  if (nwp_config.code & SL_SI91X_SET_XTAL_GOOD_TIME_FROM_HOST) {
    if ((nwp_config.values.config_val < SLI_SI91X_XTAL_GOOD_TIME_MIN)
        || (nwp_config.values.config_val > SLI_SI91X_XTAL_GOOD_TIME_MAX)) {
      return SL_STATUS_INVALID_PARAMETER;
    }
    sli_si91x_set_xtal_pmu_good_time_from_host(SL_SI91X_SET_XTAL_GOOD_TIME_FROM_HOST, nwp_config.values.config_val);
    return SL_STATUS_OK;
  }

  // PMU good time must be stored and applied during init (firmware not ready before sl_wifi_init)
  // Note: SoC requires 900-2000µs, NCP accepts 600-2000µs
  if (nwp_config.code & SL_SI91X_SET_PMU_GOOD_TIME_FROM_HOST) {
    if ((nwp_config.values.config_val < SLI_SI91X_PMU_GOOD_TIME_MIN)
        || (nwp_config.values.config_val > SLI_SI91X_PMU_GOOD_TIME_MAX)) {
      return SL_STATUS_INVALID_PARAMETER;
    }
    sli_si91x_set_xtal_pmu_good_time_from_host(SL_SI91X_SET_PMU_GOOD_TIME_FROM_HOST, nwp_config.values.config_val);
    return SL_STATUS_OK;
  }

  if ((nwp_config.code & SL_SI91X_XO_CTUNE_FROM_HOST) || (nwp_config.code & SL_SI91X_ENABLE_NWP_WDT_FROM_HOST)
      || (nwp_config.code & SL_SI91X_DISABLE_NWP_WDT_FROM_HOST)) {
    status = sli_wifi_send_command(SLI_COMMON_REQ_SET_CONFIG,
                                   SLI_WIFI_COMMON_CMD,
                                   &nwp_config,
                                   sizeof(sl_si91x_nwp_configuration_t),
                                   SLI_WIFI_WAIT_FOR_RESPONSE(SLI_COMMON_RSP_SET_CONFIG_WAIT_TIME),
                                   NULL,
                                   NULL);
    VERIFY_STATUS_AND_RETURN(status);
  } else {
    return SL_STATUS_NOT_SUPPORTED;
  }

  return status;
}

sl_status_t sl_si91x_get_nwp_config(const sl_si91x_nwp_get_configuration_t *nwp_config, uint8_t *response)
{
  sl_status_t status                    = SL_STATUS_OK;
  sl_wifi_buffer_t *buffer              = NULL;
  const sl_wifi_system_packet_t *packet = NULL;

  if (nwp_config->sub_command_type == GET_OPN_BOARD_CONFIG) {
    status = sli_wifi_send_command(SLI_COMMON_REQ_GET_CONFIG,
                                   SLI_WIFI_COMMON_CMD,
                                   nwp_config,
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

    memcpy(response, packet->data, packet->length);
    sli_buffer_manager_free_buffer(buffer);
  } else {
    return SL_STATUS_NOT_SUPPORTED;
  }
  return status;
}

sl_status_t sl_si91x_debug_log(const sl_si91x_assertion_t *assertion)
{
  sl_status_t status                 = SL_STATUS_OK;
  sli_si91x_debug_log_t debug_config = { 0 };

  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if ((debug_config.assertion_type > SL_SI91X_ASSERTION_TYPE_ALL)
      || (debug_config.assertion_level > SL_SI91X_ASSERTION_LEVEL_MAX)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  debug_config.assertion_type  = assertion->assert_type;
  debug_config.assertion_level = assertion->assert_level;

  status = sli_wifi_send_command(SLI_COMMON_REQ_DEBUG_LOG,
                                 SLI_WIFI_COMMON_CMD,
                                 &debug_config,
                                 sizeof(sli_si91x_debug_log_t),
                                 SLI_COMMON_RSP_DEBUG_LOG_WAIT_TIME,
                                 NULL,
                                 NULL);

  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

void print_firmware_version(const sl_wifi_firmware_version_t *firmware_version)
{
  printf("\r\nFirmware version is: %x%x.%d.%d.%d.%d.%d.%d\r\n",
         firmware_version->chip_id,
         firmware_version->rom_id,
         firmware_version->major,
         firmware_version->minor,
         firmware_version->security_version,
         firmware_version->patch_num,
         firmware_version->customer_id,
         firmware_version->build_num);
}