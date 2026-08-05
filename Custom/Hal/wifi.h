#ifndef WIFI_H
#define WIFI_H

#include "cmsis_os2.h"
#include "dev_manager.h"
#include "mem_map.h"
#include "pwr.h"

#define WIFI_FIR_NAME "siwg917"

#define WIFI_MODE_UPDATE            "update"
#define WIFI_MODE_NORMAL            "normal"
#define WIFI_MODE_TX_TEST           "tx_test"

#define NVS_KEY_WIFI_MODE           "wifi_mode"
#define NVS_KEY_WIFI_UPDATE_TIMES   "wifi_update_times"
//! Type of FW update
#define M4_FW_UPDATE  0 // Only Supported for SoC
#define NWP_FW_UPDATE 1

//! Set FW update type
#define FW_UPDATE_TYPE NWP_FW_UPDATE

#define SI91X_CHUNK_SIZE      4096UL
#define SI91X_IN_BETWEEN_FILE 0UL
#define SI91X_START_OF_FILE   1UL
#define SI91X_END_OF_FILE     2UL
#define SI91X_FW_VER_SIZE     20UL
#define FW_HEADER_SIZE        64UL
#define XMODEM_CHUNK_SIZE     128UL
#define FIRST_PKT_XMODEM_CNT  32UL

#define WIFI_FLASH_BASE_ADDR       WIFI_FW_BASE
#define WIFI_FLASH_HEADER_SIZE     32UL
#define WIFI_FLASH_VALID_FLAGS     0x20060123UL

typedef enum si91x_wlan_app_state_e {
  SI91X_WLAN_INITIAL_STATE    = 0,
  SI91X_WLAN_RADIO_INIT_STATE = 1,
  SI91X_WLAN_FW_UPGRADE       = 2,
  SI91X_WLAN_FW_UPGRADE_DONE  = 3
} si91x_wlan_app_state_t;

#pragma pack(push, 1)
typedef struct {
  uint32_t valid_flags;
  uint32_t fw_total_size;
  uint32_t fw_crc;
  uint32_t reserved[5];
} flash_header_t;
typedef struct fwupeq_s {
  uint16_t control_flags;
  uint16_t sha_type;
  uint32_t magic_no;
  uint32_t image_size;
  uint32_t fw_version;
  uint32_t flash_loc;
  uint32_t crc;
} fwreq_t;
#pragma pack(pop)

typedef struct si91x_wlan_app_cb_s {
  //! wlan application state
  si91x_wlan_app_state_t state;
  //! length of buffer to copy
  uint32_t length;
  //! to check application buffer availability
  uint8_t buf_in_use;
  //! application events bit map
  uint32_t event_map;

} si91x_wlan_app_cb_t;


/*
 * @brief Process wifi mode
 */
void wifi_mode_process(void);

int is_wifi_ant(void);

int is_wifi_update(void);

void wifi_enter_update_mode(void);

/**
 * @brief Mark a WiFi firmware update as pending (NVS wifi_mode = "update")
 *        WITHOUT resetting the system.
 *
 * Used by the web OTA handler after writing a new .rps to WIFI_FW_BASE: the
 * HTTP response can complete normally, and on the next reboot
 * wifi_mode_process() -> firmware_upgrade_from_flash() pushes the new image to
 * the SiWG917 chip. Contrast with wifi_enter_update_mode(), which sets the same
 * NVS key and then immediately resets.
 */
void wifi_mark_update_pending(void);

uint32_t get_wifi_update_times(void);

/**
 * @brief Get the currently-running WiFi firmware version from the SiWG917 chip.
 * @param buf  Output buffer (at least 32 bytes recommended).
 * @param size Buffer size.
 * @return 0 on success, -1 if the version could not be retrieved (e.g. Wi-Fi
 *         module not initialised, or communication error).
 *
 * @details Calls sl_wifi_get_firmware_version() and formats the result as
 *          "X.Y.Z.B" where B = security_version * 100 + build_num — matching
 *          the 4-part version encoding used by the OTA / HEX packing scripts.
 */
int wifi_get_running_version(char *buf, size_t size);

/**
 * @brief Read the WiFi firmware version stored in flash at WIFI_FW_BASE.
 *
 * Reads the flash_header_t validation marker, then extracts version components
 * from the .rps binary header (sl_wifi_firmware_header_t.fw_version_info /
 * .fw_version_ext_info).  No wireless chip communication is required.
 *
 * @param buf  Output buffer.
 * @param size Buffer size.
 * @return 0 on success, -1 if no valid WiFi firmware is present in flash.
 */
int wifi_get_flash_version(char *buf, size_t size);

void wifi_register(void);
#endif