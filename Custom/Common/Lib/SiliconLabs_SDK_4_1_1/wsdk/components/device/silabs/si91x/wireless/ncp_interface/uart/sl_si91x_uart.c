/***************************************************************************/ /**
 * @file
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2019 Silicon Laboratories Inc. www.silabs.com</b>
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
#include "sli_wifi_utility.h"
#include "sl_si91x_driver.h"
#include "sli_buffer_manager.h"
#include "sli_queue_manager.h"
#include "sli_hal_si91x_constants.h"
#include "sl_si91x_status.h"
#include "sl_si91x_types.h"
#include "sl_si91x_constants.h"
#include "sl_si91x_spi_constants.h"
#include "sl_si91x_host_interface.h"
#include "sl_status.h"
#include "sl_additional_status.h"
#include "sl_wifi_constants.h"
#include "sl_constants.h"
#include "sl_rsi_utility.h"
#include <stdint.h>
#include <stddef.h>

#ifndef SL_SI91X_NCP_UART_BAUDRATE

// ToDo: This Macro is depricated and should be removed in upcoming releases.
//       Keeping this macro functionality intact due to backward compatability.
#if SL_SI91X_UART_HIGH_SPEED_ENABLE == 1
#define SL_SI91X_NCP_UART_BAUDRATE 921600
#else
#define SL_SI91X_NCP_UART_BAUDRATE 115200
#endif
#endif

#if SL_SI91X_NCP_UART_BAUDRATE == 9600
#define SLI_SI91X_UART_BAUDRATE_SELECTION 1
#define SLI_SI91X_BAUDRATE                '0'
#elif SL_SI91X_NCP_UART_BAUDRATE == 38400
#define SLI_SI91X_UART_BAUDRATE_SELECTION 1
#define SLI_SI91X_BAUDRATE                '1'
#elif SL_SI91X_NCP_UART_BAUDRATE == 230400
#define SLI_SI91X_UART_BAUDRATE_SELECTION 1
#define SLI_SI91X_BAUDRATE                '2'
#elif SL_SI91X_NCP_UART_BAUDRATE == 460800
#define SLI_SI91X_UART_BAUDRATE_SELECTION 1
#define SLI_SI91X_BAUDRATE                '3'
#elif SL_SI91X_NCP_UART_BAUDRATE == 921600
#define SLI_SI91X_UART_BAUDRATE_SELECTION 1
#define SLI_SI91X_BAUDRATE                '4'
#elif SL_SI91X_NCP_UART_BAUDRATE == 115200
#define SLI_SI91X_UART_BAUDRATE_SELECTION 0
#define SLI_SI91X_BAUDRATE                '5'
#else
#error "Invalid NCP UART Baudrate spedified"
#endif

#ifndef SL_SI91X_NCP_UART_HFC_MODE
// ToDo: This Macro is depricated and should be removed in upcoming releases.
//       Keeping this macro functionality intact due to backward compatability.
#if SL_SI91X_UART_HFC_ENABLE
#define SL_SI91X_NCP_UART_HFC_MODE 3
#else
#define SL_SI91X_NCP_UART_HFC_MODE 0
#endif
#endif

#if SL_SI91X_NCP_UART_HFC_MODE == 0
#define SLI_SI91X_HFC_MODE SL_SI91X_NCP_UART_HFC_MODE
#elif SL_SI91X_NCP_UART_HFC_MODE == 3
#define SLI_SI91X_HFC_MODE SL_SI91X_NCP_UART_HFC_MODE
#else
#error "Invalid NCP UART Hardware Flow Control mode Specified"
#endif

// This macro converts a 32-bit value from host to little-endian byte order
#define htole32(x) (x)

// This macro converts a 16-bit value from host to little-endian byte order
#define htole16(x) (x)

#define FRAME_SIZE 1600

static sl_wifi_buffer_t *resp_buffer     = NULL;
static sl_status_t resp_status           = SL_STATUS_OK;
static sli_queue_t sli_uart_bus_rx_queue = { 0 };

/******************************************************
 * *               Function Declarations
 * ******************************************************/
sl_status_t sli_si91x_req_wakeup(void);
sl_status_t sli_hal_si91x_notify_events(uint32_t flags);

/************************************************************************************
 ******************************** Static Functions *********************************
************************************************************************************/
static sl_status_t sli_si91x_uart_command_handler(uint8_t *cmd,
                                                  uint16_t cmd_length,
                                                  const char *expected_response,
                                                  uint32_t expected_data_count)
{
  //sl_status_t status;
  uint16_t temp     = 0;
  uint8_t *response = NULL;

  response = (uint8_t *)sli_wifi_host_get_buffer_data(resp_buffer, 0, &temp);

  SL_DEBUG_LOG_V2(DEBUG, "Command : { %c }\r\n", *((char *)(cmd)));
  sl_si91x_host_uart_transfer((const void *)cmd, NULL, cmd_length);
  sl_si91x_host_uart_transfer(NULL, (void *)response, expected_data_count);
  SL_DEBUG_LOG_V2(DEBUG, "Response(%lu bytes) %s\r\n", expected_data_count, (uintptr_t)response);
  if (NULL == strstr((const char *)response, expected_response)) {
    return SL_STATUS_FAIL;
  }

  memset(response, 0, FRAME_SIZE);
  return SL_STATUS_OK;
}

/************************************************************************************
 ******************************** Public Functions *********************************
************************************************************************************/
sl_status_t sl_si91x_bus_init(void)
{
  uint8_t boot_cmd[5] = { '|', 'U', 'b', SLI_SI91X_BAUDRATE, '\0' };
  uint8_t *response   = NULL;
  sl_status_t status;
  uint16_t temp;
  static uint32_t i = 0;
  SL_DEBUG_LOG_V2(DEBUG, "Bus Init startup\r\n");

  // Initialize the RX queue
  status = sli_queue_manager_init(&sli_uart_bus_rx_queue, SLI_BUFFER_MANAGER_QUEUE_NODE_POOL);
  VERIFY_STATUS_AND_RETURN(status);

  // Allocate packet to receive packet from module
  status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CP_CMD_RX_POOL,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                              10000,
                                              (sli_buffer_t *)&resp_buffer);
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "HEAP EXHAUSTED DURING ALLOCATION\r\n");
    return SL_STATUS_ALLOCATION_FAILED;
  }

  response = (uint8_t *)sli_wifi_host_get_buffer_data(resp_buffer, 0, &temp);
  memset(response, 0, FRAME_SIZE);

  status = sli_si91x_uart_command_handler(boot_cmd, 1, "Enter 'U'", (12 - i));
  VERIFY_STATUS_AND_RETURN(status);
  // During 1st init we receive one extra character which is not received in later init
  i = ((i > 0) ? i : (i + 1));

  status = sli_si91x_uart_command_handler(&boot_cmd[1], 1, "JTAG Selection", 485);
  VERIFY_STATUS_AND_RETURN(status);

#if SLI_SI91X_UART_BAUDRATE_SELECTION
  status = sli_si91x_uart_command_handler(&boot_cmd[2], 1, "5 115200", 78);
  VERIFY_STATUS_AND_RETURN(status);

  status = sli_si91x_uart_command_handler(&boot_cmd[3], 1, (const char *)&boot_cmd[3], 1);
  VERIFY_STATUS_AND_RETURN(status);

  sl_si91x_host_enable_high_speed_bus();
  sl_si91x_host_uart_transfer(NULL, (void *)response, 1);
  SL_DEBUG_LOG_V2(DEBUG, "Response(%u bytes) %s\r\n", 1, (uintptr_t)response);
  if (NULL == strstr((const char *)response, "U")) {
    return SL_STATUS_FAIL;
  }

  status = sli_si91x_uart_command_handler(&boot_cmd[1], 1, "Enter Next Command", 105);
  VERIFY_STATUS_AND_RETURN(status);
#endif

  SL_DEBUG_LOG_V2(DEBUG, "Bus Init Done\r\n");

  return SL_STATUS_OK;
}

sl_status_t sl_si91x_bus_write_memory(uint32_t addr, uint16_t length, const uint8_t *buffer)
{
  UNUSED_PARAMETER(addr);
  UNUSED_PARAMETER(length);
  UNUSED_PARAMETER(buffer);
  return SL_STATUS_WIFI_UNSUPPORTED;
}

sl_status_t sl_si91x_bus_read_memory(uint32_t addr, uint16_t length, const uint8_t *buffer)
{
  UNUSED_PARAMETER(addr);
  UNUSED_PARAMETER(length);
  UNUSED_PARAMETER(buffer);
  return SL_STATUS_WIFI_UNSUPPORTED;
}

sl_status_t sli_si91x_bus_write_register(uint8_t address, uint8_t register_size, uint16_t data)
{
  UNUSED_PARAMETER(address);
  UNUSED_PARAMETER(register_size);
  UNUSED_PARAMETER(data);
  return SL_STATUS_WIFI_UNSUPPORTED;
}

sl_status_t sli_si91x_bus_read_register(uint8_t address, uint8_t register_size, uint16_t *output)
{
  UNUSED_PARAMETER(address);
  UNUSED_PARAMETER(register_size);
  UNUSED_PARAMETER(output);
  return SL_STATUS_WIFI_UNSUPPORTED;
}

sl_status_t sli_si91x_bus_write_frame(sl_wifi_system_packet_t *packet, const uint8_t *payloadparam, uint16_t size_param)
{
  UNUSED_PARAMETER(payloadparam);
  sl_status_t status;

  // Write host descriptor
  status = sl_si91x_host_uart_transfer((const void *)&packet->desc, NULL, SLI_FRAME_DESC_LEN);
  SLI_VERIFY_STATUS(status);

  // Write payload if present
  if (size_param) {
    status = sl_si91x_host_uart_transfer((const void *)&packet->data, NULL, size_param);
    SLI_VERIFY_STATUS(status);
  }

  return SL_STATUS_OK;
}

sl_status_t sli_si91x_bus_read_frame(sl_wifi_buffer_t **buffer)
{
  sl_status_t status = sli_queue_manager_dequeue(&sli_uart_bus_rx_queue, (void **)buffer);
  VERIFY_STATUS_AND_RETURN(status);

  return SL_STATUS_OK;
}

// Function for reading the interrupt status
sl_status_t sli_si91x_bus_read_interrupt_status(uint16_t *interrupt_status)
{
  sl_status_t status = SL_STATUS_OK;

  if (!(SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&sli_uart_bus_rx_queue))) {
    *interrupt_status = SLI_RX_PKT_PENDING;
  }

  return status;
}

sl_status_t sli_si91x_bootup_firmware(const uint8_t select_option, uint8_t image_number)
{
  UNUSED_PARAMETER(select_option); // Unused parameter(to suppress compiler warnings)
  UNUSED_PARAMETER(image_number);  // Unused parameter(to suppress compiler warnings)
  sl_status_t status     = SL_STATUS_OK;
  uint8_t load_binary[2] = { 'H', '1' };
#if SLI_SI91X_HFC_MODE
  uint8_t i             = 0;
  uint8_t hfc_command[] = {
    0x01, 0x40, 0xA4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, SLI_SI91X_HFC_MODE
  };
  uint8_t hfc_response[] = { 0x15, 0x00, 0x04, 0x00, 0x01, 0x40, 0xA4, 0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
  uint16_t temp          = 0;
  uint8_t *response      = NULL;

  response = (uint8_t *)sli_wifi_host_get_buffer_data(resp_buffer, 0, &temp);
#endif
  SL_DEBUG_LOG_V2(DEBUG, "Bootup startup\r\n");

  status = sli_si91x_uart_command_handler(load_binary, 1, "Enter Next Command", 47);
  VERIFY_STATUS_AND_RETURN(status);

  status = sli_si91x_uart_command_handler(&load_binary[1], 1, "Loading...", 35);
  VERIFY_STATUS_AND_RETURN(status);

  sl_si91x_host_flush_uart_rx();

#if SLI_SI91X_HFC_MODE
  sl_si91x_host_uart_transfer((const void *)hfc_command, NULL, 17);
  sl_si91x_host_uart_transfer(NULL, (void *)response, 21);

  for (i = 0; i < 21; i++) {
    if (response[i] != hfc_response[i]) {
      return SL_STATUS_FAIL;
    }
  }

  sl_si91x_host_uart_enable_hardware_flow_control();
#endif
  sli_wifi_set_event(SL_WIFI_HOST_COMMON_RESPONSE_EVENT);

  SL_DEBUG_LOG_V2(DEBUG, "Bootup Done\r\n");
  return SL_STATUS_OK;
}

sl_status_t sli_si91x_bus_rx_irq_handler(void)
{
  sl_status_t status;
  uint16_t data_desc[2] = { 0 };
  uint8_t *response     = NULL;
  uint16_t temp;

  response = (uint8_t *)sli_wifi_host_get_buffer_data(resp_buffer, 0, &temp);

  // Wakeup NCP before reading data
  // May be this is not required for NCP UART since it already initiated the transfer
  status = sli_si91x_req_wakeup();

  // Read the first 4 bytes to determine the frame size
  status = sl_si91x_host_uart_transfer(NULL, (void *)data_desc, 4);
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "Data descriptor read failed\r\n");
    return SL_STATUS_FAIL;
  }

  data_desc[0] = data_desc[0] - 4;

  // Read the first 4 bytes to determine the frame size
  status = sl_si91x_host_uart_transfer(NULL, (void *)response, data_desc[0]);
  if (SL_STATUS_IN_PROGRESS == status) {
    resp_status = SL_STATUS_IN_PROGRESS;
    return status;
  } else if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "Data frame read failed\r\n");
    return SL_STATUS_FAIL;
  }

  // Check if transfer is still ongoing
  if (SL_STATUS_IN_PROGRESS == status) {
    // Put module to sleep again in case of power save mode
    sl_si91x_host_clear_sleep_indicator();
  }

  status = sli_queue_manager_enqueue(&sli_uart_bus_rx_queue, (void *)resp_buffer);

  // Allocate a buffer for the next frame
  status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CP_CMD_RX_POOL,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                              10000,
                                              (sli_buffer_t *)&resp_buffer);
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "HEAP EXHAUSTED DURING ALLOCATION\r\n");
    return SL_STATUS_ALLOCATION_FAILED;
  }

  sli_hal_si91x_notify_events(SLI_HAL_SI91X_RX_EVENT);
  return SL_STATUS_OK;
}

void sli_si91x_bus_rx_done_handler(void)
{
  sl_status_t status;

  if (SL_STATUS_IN_PROGRESS == resp_status) {
    // Put module to sleep again in case of power save mode
    sl_si91x_host_clear_sleep_indicator();

    status = sli_queue_manager_enqueue(&sli_uart_bus_rx_queue, (void *)resp_buffer);

    // Allocate a buffer for the next frame
    status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CP_CMD_RX_POOL,
                                                SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                                10000,
                                                (sli_buffer_t *)&resp_buffer);
    if (status != SL_STATUS_OK) {
      SL_DEBUG_LOG_V2(ERROR, "HEAP EXHAUSTED DURING ALLOCATION\r\n");
      return;
    }

    sli_hal_si91x_notify_events(SLI_HAL_SI91X_RX_EVENT);
    sl_si91x_host_enable_bus_interrupt();
  }
  return;
}

//! Initialize with modules UART interface on ulp wakeup.
void sli_si91x_ulp_wakeup_init(void)
{
  return;
}

sl_status_t sl_si91x_bus_deinit(void)
{
  sl_status_t status = SL_STATUS_OK;

  if (resp_buffer != NULL) {
    status      = sli_buffer_manager_free_buffer(resp_buffer);
    resp_buffer = NULL;
  }

  return status;
}
