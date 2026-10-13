#include <string.h>
#include "sl_constants.h"
#include "sli_power_profile.h"
#include "sli_types.h"
#include "sli_utility.h"
#include "sl_utility.h"
#include "sli_constants.h"
/* Power save sequence in progress flag (used internally during NWP power mode transitions) */
volatile bool power_save_sequence_in_progress = false;

sli_wifi_performance_profile_t performance_profile;

static sl_wifi_system_coex_mode_t coex_mode = 0;

void sli_save_coex_mode(sl_wifi_system_coex_mode_t mode)
{
  coex_mode = mode;
}

sl_wifi_system_coex_mode_t sli_get_coex_mode(void)
{
  return coex_mode;
}

void sli_wifi_reset_coex_current_performance_profile(void)
{
  memset(&performance_profile, 0, sizeof(sli_wifi_performance_profile_t));
}

// Get the current Wi-Fi performance profile
void sli_wifi_get_current_performance_profile(sl_wifi_performance_profile_v2_t *profile)
{
  SL_ASSERT(profile != NULL);
  memcpy(profile, &performance_profile.wifi_performance_profile, sizeof(sl_wifi_performance_profile_v2_t));
}

void sli_save_bt_current_performance_profile(const sl_bt_performance_profile_t *profile)
{
  SL_ASSERT(profile != NULL);
  memcpy(&performance_profile.bt_performance_profile, profile, sizeof(sl_bt_performance_profile_t));
}

void sli_get_bt_current_performance_profile(sl_bt_performance_profile_t *profile)
{
  SL_ASSERT(profile != NULL);
  memcpy(profile, &performance_profile.bt_performance_profile, sizeof(sl_bt_performance_profile_t));
}

void sli_reset_coex_current_performance_profile(void)
{
  if (!power_save_sequence_in_progress) {
    memset(&performance_profile, 0, sizeof(sli_wifi_performance_profile_t));
  }
}

void sli_wifi_save_current_performance_profile(const sl_wifi_performance_profile_v2_t *profile)
{
  SL_ASSERT(profile != NULL);
  memcpy(&performance_profile.wifi_performance_profile, profile, sizeof(sl_wifi_performance_profile_v2_t));

  performance_profile.coex_mode = sli_get_coex_mode();
}

// Get the coexistence performance profile based on the current coexistence mode
void sli_get_coex_performance_profile(sl_wifi_system_performance_profile_t *profile)
{
  SL_ASSERT(profile != NULL);
  uint8_t mode_decision                       = 0;
  sl_wifi_system_coex_mode_t stored_coex_mode = performance_profile.coex_mode;
  if (stored_coex_mode == SL_WIFI_SYSTEM_WLAN_ONLY_MODE) { // Treat WLAN_ONLY as WLAN_MODE
    stored_coex_mode = SL_WIFI_SYSTEM_WLAN_MODE;
  }
  // Determine the mode decision based on the coexistence mode
  switch (stored_coex_mode) {
    case SL_WIFI_SYSTEM_WLAN_MODE: {
      // Wi-Fi only mode
      mode_decision = (uint8_t)((performance_profile.wifi_performance_profile.profile << 4)
                                | (performance_profile.wifi_performance_profile.profile));
    } break;
    case SL_WIFI_SYSTEM_BLUETOOTH_MODE:
    case SL_WIFI_SYSTEM_BLE_MODE:
    case SL_WIFI_SYSTEM_DUAL_MODE: {
      // Bluetooth only or dual-mode (BT + Wi-Fi) mode
      mode_decision = (uint8_t)((performance_profile.bt_performance_profile.profile << 4)
                                | (performance_profile.bt_performance_profile.profile));
    } break;
    case SL_WIFI_SYSTEM_WLAN_BLUETOOTH_MODE:
    case SL_WIFI_SYSTEM_WLAN_DUAL_MODE:
    case SL_WIFI_SYSTEM_WLAN_BLE_MODE: {
      // Wi-Fi + Bluetooth mode
      mode_decision = (uint8_t)((performance_profile.wifi_performance_profile.profile << 4)
                                | (performance_profile.bt_performance_profile.profile));
    } break;
    default:
      break;
  }

  // Determine the performance profile based on the mode decision
  switch (mode_decision) {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x10:
    case 0x20:
    case 0x30:
    case 0x40: {
      *profile = SL_WIFI_SYSTEM_HIGH_PERFORMANCE; // High performance mode
    } break;
    case 0x11:
    case 0x12:
    case 0x31:
    case 0x13:
    case 0x14:
    case 0x41: {
      *profile = SL_WIFI_SYSTEM_ASSOCIATED_POWER_SAVE; // Power save mode
    } break;
    case 0x22:
    case 0x21:
    case 0x32:
    case 0x23:
    case 0x42:
    case 0x24: {
      *profile = SL_WIFI_SYSTEM_ASSOCIATED_POWER_SAVE_LOW_LATENCY; // Low latency power save mode
    } break;
    case 0x33: {
      *profile = SL_WIFI_SYSTEM_DEEP_SLEEP_WITHOUT_RAM_RETENTION; // Power save mode
    } break;
    case 0x44: {
      *profile = SL_WIFI_SYSTEM_DEEP_SLEEP_WITH_RAM_RETENTION; // Power save mode with RAM retention
    } break;
    default: {
      // Do nothing
    } break;
  }
  return;
}

void sli_convert_performance_profile_to_power_save_command(sl_wifi_system_performance_profile_t profile,
                                                           sli_wifi_power_save_request_t *power_save_request)
{
  SL_ASSERT(power_save_request != NULL);
  if (performance_profile.wifi_performance_profile.monitor_interval) {
    power_save_request->monitor_interval = performance_profile.wifi_performance_profile.monitor_interval;
  } else {
    power_save_request->monitor_interval = SLI_DEFAULT_MONITOR_INTERVAL;
  }

  power_save_request->ulp_mode_enable   = SLI_ULP_WITH_RAM_RETENTION;
  power_save_request->dtim_aligned_type = performance_profile.wifi_performance_profile.dtim_aligned_type;
  power_save_request->num_of_dtim_skip  = performance_profile.wifi_performance_profile.num_of_dtim_skip;
  power_save_request->listen_interval   = (uint16_t)performance_profile.wifi_performance_profile.listen_interval;
  power_save_request->psp_type          = SLI_MAX_PSP;
  if (performance_profile.wifi_performance_profile.beacon_miss_ignore_limit) {
    power_save_request->beacon_miss_ignore_limit =
      performance_profile.wifi_performance_profile.beacon_miss_ignore_limit;
  } else {
    power_save_request->beacon_miss_ignore_limit = DEFAULT_BEACON_MISS_IGNORE_LIMIT;
  }

  // Depending on the specified performance profile, configure the power_save_request structure
  switch (profile) {
    case SL_WIFI_SYSTEM_HIGH_PERFORMANCE: {
      // For HIGH_PERFORMANCE profile, reset all fields in the power_save_request structure to zero
      memset(power_save_request, 0, sizeof(sli_wifi_power_save_request_t));
      break;
    }

    case SL_WIFI_SYSTEM_ASSOCIATED_POWER_SAVE: {
#ifdef SLI_SI91X_MCU_INTERFACE
      power_save_request->power_mode = SLI_CONNECTED_M4_BASED_PS;
#else
      power_save_request->power_mode = SLI_CONNECTED_GPIO_BASED_PS;
#endif
      break;
    }

    case SL_WIFI_SYSTEM_ASSOCIATED_POWER_SAVE_LOW_LATENCY: {
#ifdef SLI_SI91X_MCU_INTERFACE
      power_save_request->power_mode = SLI_CONNECTED_M4_BASED_PS;
#else
      power_save_request->power_mode = SLI_CONNECTED_GPIO_BASED_PS;
#endif
      power_save_request->psp_type = SLI_FAST_PSP;
      break;
    }

    case SL_WIFI_SYSTEM_DEEP_SLEEP_WITHOUT_RAM_RETENTION: {
#ifdef SLI_SI91X_MCU_INTERFACE
      power_save_request->power_mode = SLI_M4_BASED_DEEP_SLEEP;
#else
      power_save_request->power_mode = SLI_GPIO_BASED_DEEP_SLEEP;
#endif
      power_save_request->ulp_mode_enable = SLI_ULP_WITHOUT_RAM_RET_RETENTION;
      break;
    }

    case SL_WIFI_SYSTEM_DEEP_SLEEP_WITH_RAM_RETENTION: {
#ifdef SLI_SI91X_MCU_INTERFACE
      power_save_request->power_mode = SLI_M4_BASED_DEEP_SLEEP;
#else
      power_save_request->power_mode = SLI_GPIO_BASED_DEEP_SLEEP;
#endif
      break;
    }
    default: {
      // Do nothing
    } break;
  }

  return;
}

sl_status_t sli_wifi_send_power_save_request(const sl_wifi_performance_profile_v2_t *wifi_profile,
                                             const sl_bt_performance_profile_t *bt_profile)
{
  sl_status_t status;
  sli_wifi_power_save_request_t power_save_request                = { 0 };
  sl_wifi_system_performance_profile_t selected_coex_profile_mode = { 0 };

  power_save_sequence_in_progress = true;

  // Disable power save mode by setting it to SL_WIFI_SYSTEM_HIGH_PERFORMANCE profile
  status = sli_wifi_send_command(SLI_WIFI_REQ_PWRMODE,
                                 SLI_WIFI_COMMON_CMD,
                                 &power_save_request,
                                 sizeof(sli_wifi_power_save_request_t),
                                 SLI_WIFI_RSP_PWRMODE_WAIT_TIME,
                                 NULL,
                                 NULL);
  if (status != SL_STATUS_OK) {
    // Reset flag on failure
    power_save_sequence_in_progress = false;
    return status;
  }

  if (NULL != wifi_profile) {
    // Save the new Wi-Fi profile
    sli_wifi_save_current_performance_profile(wifi_profile);
  }

  if (NULL != bt_profile) {
    // Save the new BT/BLE profile
    sli_save_bt_current_performance_profile(bt_profile);
  }

  // get the updated coex profile
  sli_get_coex_performance_profile(&selected_coex_profile_mode);

  // If the requested performance profile is HIGH_PERFORMANCE, no need to send the request to firmware
  if (selected_coex_profile_mode == SL_WIFI_SYSTEM_HIGH_PERFORMANCE) {
    // Reset flag before returning
    power_save_sequence_in_progress = false;
    return SL_STATUS_OK;
  }

  // Convert the performance profile to a power save request.
  sli_convert_performance_profile_to_power_save_command(selected_coex_profile_mode, &power_save_request);

  status = sli_wifi_send_command(SLI_WIFI_REQ_PWRMODE,
                                 SLI_WIFI_COMMON_CMD,
                                 &power_save_request,
                                 sizeof(sli_wifi_power_save_request_t),
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_WIFI_RSP_PWRMODE_WAIT_TIME),
                                 NULL,
                                 NULL);
  // Reset flag before returning
  power_save_sequence_in_progress = false;
  return status;
}

sl_status_t sli_wifi_set_performance_profile_v2(const sl_wifi_performance_profile_v2_t *profile)
{
  sl_status_t status;
  sl_wifi_system_performance_profile_t selected_coex_profile_mode = { 0 };
  sl_wifi_performance_profile_v2_t current_wifi_profile_mode      = { 0 };

  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  SL_WIFI_ARGS_CHECK_NULL_POINTER(profile);

  if (profile->profile > SL_WIFI_SYSTEM_DEEP_SLEEP_WITH_RAM_RETENTION) {
    return SL_STATUS_INVALID_MODE;
  }

  // Take backup of current wifi profile
  sli_wifi_get_current_performance_profile(&current_wifi_profile_mode);

  // Send the power save command for the requested profile
  status = sli_wifi_send_power_save_request(profile, NULL);
  if (status != SL_STATUS_OK) {
    sli_wifi_save_current_performance_profile(&current_wifi_profile_mode);
    return status;
  }
  sli_get_coex_performance_profile(&selected_coex_profile_mode);

  if (selected_coex_profile_mode == SL_WIFI_SYSTEM_DEEP_SLEEP_WITHOUT_RAM_RETENTION) {
#ifdef SLI_SI91X_MCU_INTERFACE
    // In soc mode m4 does not get the card ready for next init after deinit, but if device in DEEP_SLEEP_WITHOUT_RAM_RETENTION mode, m4 should wait for card ready for next init
    sli_wifi_set_card_ready_required(true);
#endif
    sli_wifi_reset_coex_current_performance_profile();
  }

  return SL_STATUS_OK;
}
