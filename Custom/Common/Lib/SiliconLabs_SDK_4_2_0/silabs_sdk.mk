#
# Silicon Labs WiseConnect SDK 4.2.0 (Si917 NCP) - Makefile integration
#
# Sources from the vendor's internal 4.2.0 drop (wiseconnect build 1036 +
# matching gecko-sdk platform_core/network_stack subtrees). 4.2.0 layout
# changes vs 4.1.1:
#   - sockets/bsd_socket/asynchronous_socket/sl_websocket_client moved to
#     the Gecko SDK network_stack subtree -> wsdk/network_stack/
#   - sli_hal_si91x moved under device/silabs/si91x/mcu/hal/
#   - SPI/UART NCP buses moved to device/silabs/si91x/host_interface/ncp_bus/
#   - the sli_wifi_send_command family moved to common/src/sli_utility.c
#   - sl_si91x_status.h moved to common/inc; buffer block size is now
#     SLI_WIFI_BUFFER_BLOCK_SIZE, 2324 override via
#     SLI_WIFI_BUFFER_CONFIG_BLOCK_SIZE (replaces SPI_EXTENDED_TX_LEN_2K's
#     old SLI_WIFI_EXTENDED_BLOCK_SIZE role)
#   - sl_slist.c is now shipped by the SDK (sisdk/common/src), so the port
#     copy is retired
# Local stability patches from the 4.1.1 tree were 3-way merged onto these
# sources (memory-pool routing, payload caps, drain/teardown guards,
# bus-write status forwarding, timeout clamps).
#

SL_SDK_ROOT  = ../Custom/Common/Lib/SiliconLabs_SDK_4_2_0
SL_SISDK     = $(SL_SDK_ROOT)/sisdk
SL_WSDK      = $(SL_SDK_ROOT)/wsdk
SL_WSDK_DEV  = $(SL_WSDK)/components/device/silabs/si91x/wireless
SL_NS        = $(SL_WSDK)/network_stack/components
SL_PORT      = $(SL_SDK_ROOT)/port

# --- sisdk platform primitives (gecko-sdk platform_core) ---
C_SOURCES += $(SL_SISDK)/common/src/sl_string.c
C_SOURCES += $(SL_SISDK)/common/src/sl_slist.c
C_SOURCES += $(SL_SISDK)/mem_pool/src/sl_mem_pool.c

# --- port/ overrides (ThreadX patches) ---
# task_register: ext task reg. osThreadFlags: cmsis_rtos_threadx does NOT
# implement it; the 4.2 command engine still uses it, so we emulate via
# per-thread osEventFlags lookup.
C_SOURCES += $(SL_PORT)/src/sli_cmsis_os2_ext_task_register.c
C_SOURCES += $(SL_PORT)/src/sli_cmsis_os2_thread_flags.c
# Strong override of the SDK's __WEAK sl_debug_log(): routes every
# SL_DEBUG_LOG_V2 trace (API call/status/event callback) into generic_log as
# module "SLSDK" -> dedicated timestamped slsdk.log (module-routed output,
# kept out of the system aicam.log).
# Compiled empty unless SL_SDK_DEBUG_LOG_ENABLE is defined (see C_DEFS below).
C_SOURCES += $(SL_PORT)/src/sl_sdk_debug_log.c

# --- wsdk common ---
C_SOURCES += $(SL_WSDK)/components/common/src/sl_utility.c
# 4.2: home of the sli_wifi_send_command / async / wait-on-response family
C_SOURCES += $(SL_WSDK)/components/common/src/sli_utility.c
# 4.2: (co)ex performance-profile state + sl_wifi_set_performance_profile_v2
C_SOURCES += $(SL_WSDK)/components/common/src/sli_power_profile.c
C_SOURCES += $(SL_WSDK)/components/common/src/sl_power_profile.c

# --- internal Wi-Fi components ---
C_SOURCES += $(SL_WSDK)/components/sli_command_engine/src/sli_command_engine.c
C_SOURCES += $(SL_WSDK)/components/sli_event_engine/src/sli_event_engine.c
C_SOURCES += $(SL_WSDK)/components/sli_buffer_manager/src/sli_buffer_manager.c
C_SOURCES += $(SL_WSDK)/components/sli_queue_manager/src/sli_queue_manager.c
C_SOURCES += $(SL_WSDK)/components/sli_routing_utility/src/sli_routing_utility.c
C_SOURCES += $(SL_WSDK)/components/sli_wifi/src/sli_wifi.c
C_SOURCES += $(SL_WSDK)/components/sli_wifi/src/sli_wifi_utility.c
C_SOURCES += $(SL_WSDK)/components/sli_wifi/src/sli_wifi_power_profile.c

# --- Wi-Fi protocol ---
C_SOURCES += $(SL_WSDK)/components/protocol/wifi/src/si91x/sl_wifi.c
C_SOURCES += $(SL_WSDK)/components/protocol/wifi/src/sl_wifi_basic_credentials.c
C_SOURCES += $(SL_WSDK)/components/protocol/wifi/src/sl_wifi_callback_framework.c
C_SOURCES += $(SL_WSDK)/components/protocol/wifi/src/sli_wifi_callback_framework.c

# --- BLE (compiled; SLI_SI91X_ENABLE_BLE macro disabled) ---
C_SOURCES += $(SL_WSDK_DEV)/ble/src/rsi_ble_gap_apis.c
C_SOURCES += $(SL_WSDK_DEV)/ble/src/rsi_ble_gatt_apis.c
C_SOURCES += $(SL_WSDK_DEV)/ble/src/rsi_bt_ble.c
C_SOURCES += $(SL_WSDK_DEV)/ble/src/rsi_bt_common_apis.c
C_SOURCES += $(SL_WSDK_DEV)/ble/src/rsi_common_apis.c
C_SOURCES += $(SL_WSDK_DEV)/ble/src/rsi_utils.c
C_SOURCES += $(SL_WSDK_DEV)/ble/src/sl_si91x_ble.c

# --- device driver / NCP ---
C_SOURCES += $(SL_WSDK_DEV)/src/sl_si91x_driver.c
C_SOURCES += $(SL_WSDK_DEV)/src/sli_si91x_driver.c
C_SOURCES += $(SL_WSDK_DEV)/src/sl_rsi_utility.c
C_SOURCES += $(SL_WSDK_DEV)/src/sl_si91x_http_client_callback_framework.c
C_SOURCES += $(SL_WSDK)/components/device/silabs/si91x/mcu/hal/sli_hal_si91x/src/sli_hal_si91x.c
C_SOURCES += $(SL_WSDK_DEV)/sli_si91x_wifi_command_engine/src/sli_si91x_wifi_command_engine.c
C_SOURCES += $(SL_WSDK_DEV)/sli_si91x_wifi_command_engine/src/sli_si91x_wifi_command_engine_config.c
C_SOURCES += $(SL_WSDK_DEV)/sli_si91x_wifi_event_handler/src/sli_si91x_wifi_event_handler.c
C_SOURCES += $(SL_WSDK_DEV)/firmware_upgrade/firmware_upgradation.c
C_SOURCES += $(SL_WSDK_DEV)/icmp/sl_net_ping.c
C_SOURCES += $(SL_WSDK_DEV)/errno/src/sl_si91x_errno.c
C_SOURCES += $(SL_WSDK_DEV)/memory/mem_pool_buffer_quota.c
C_SOURCES += $(SL_WSDK)/components/device/silabs/si91x/host_interface/ncp_bus/spi/sl_si91x_spi.c
# 4.2: host control (sli_si91x_req_wakeup, sl_si91x_host_power_cycle) built on
# the Custom/Hal host hooks
C_SOURCES += $(SL_WSDK)/components/device/silabs/si91x/host_interface/src/sl_si91x_host_control.c
C_SOURCES += $(SL_WSDK_DEV)/ncp_interface/sl_si91x_ncp_driver.c
C_SOURCES += $(SL_NS)/device/silabs/si91x/wireless/socket/src/sl_si91x_socket_utility.c
C_SOURCES += $(SL_NS)/device/silabs/si91x/wireless/asynchronous_socket/src/sl_si91x_socket.c
C_SOURCES += $(SL_WSDK_DEV)/sl_net/src/sl_si91x_net_internal_stack.c
C_SOURCES += $(SL_WSDK_DEV)/sl_net/src/sl_net_si91x_callback_framework.c
C_SOURCES += $(SL_WSDK_DEV)/sl_net/src/sl_net_si91x_integration_handler.c
C_SOURCES += $(SL_WSDK_DEV)/sl_net/src/sl_net_rsi_utility.c
C_SOURCES += $(SL_WSDK_DEV)/sl_net/src/sli_net_si91x_utility.c
# 4.2: DNS response conversion + IP config helpers (network_stack_common)
C_SOURCES += $(SL_NS)/service/network_stack_common/src/sl_net_dns_utility.c
C_SOURCES += $(SL_NS)/service/network_stack_common/src/sli_net_ip_config.c
# Excluded: sl_net_si91x.c, sl_net_for_dual_stack.c (Custom/Hal sl_net_netif.c)

# --- services ---
C_SOURCES += $(SL_WSDK)/components/service/network_manager/src/sl_net.c
C_SOURCES += $(SL_WSDK)/components/service/network_manager/src/sl_net_basic_certificate_store.c
C_SOURCES += $(SL_WSDK)/components/service/network_manager/src/sl_net_basic_profiles.c
C_SOURCES += $(SL_WSDK)/components/service/network_manager/src/si91x/sl_net_credentials.c
C_SOURCES += $(SL_WSDK)/components/service/network_manager/src/sli_net_credentials.c
C_SOURCES += $(SL_WSDK)/components/service/network_manager/src/sli_net_common_utility.c
C_SOURCES += $(SL_WSDK)/components/service/mqtt/si91x/sl_mqtt_client.c
C_SOURCES += $(SL_WSDK)/components/service/http_client/si91x_socket/sl_http_client.c
C_SOURCES += $(SL_WSDK)/components/service/mdns/si91x/sl_mdns.c
C_SOURCES += $(SL_WSDK)/components/service/sntp/si91x/sl_sntp.c
C_SOURCES += $(SL_NS)/service/bsd_socket/si91x_socket/sl_si91x_bsd_socket.c
C_SOURCES += $(SL_WSDK)/components/service/sl_http_server/src/sl_http_server.c
C_SOURCES += $(SL_NS)/service/sl_websocket_client/src/sl_websocket_client.c
C_SOURCES += $(SL_NS)/service/sl_websocket_client/src/sli_websocket_client_sync.c

# --- include paths ---
# Custom/Hal: mem.h (SLI_MALLOC/SLI_FREE injected into sl_status.h route all
# SDK dynamic memory to the project memory pools)
C_INCLUDES += -I../Custom/Hal
C_INCLUDES += -I$(SL_PORT)/inc
C_INCLUDES += -I$(SL_SISDK)/common/inc
C_INCLUDES += -I$(SL_SISDK)/mem_pool/inc
C_INCLUDES += -I$(SL_WSDK)/components/common/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_wifi/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_command_engine/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_event_engine/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_buffer_manager/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_buffer_manager/inc/si91x
C_INCLUDES += -I$(SL_WSDK)/components/sli_queue_manager/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_routing_utility/inc
C_INCLUDES += -I$(SL_WSDK)/components/protocol/wifi/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/network_manager/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/mqtt/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/http_client/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/mdns/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/sntp/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/sl_http_server/inc
C_INCLUDES += -I$(SL_NS)/service/bsd_socket/inc
C_INCLUDES += -I$(SL_NS)/service/network_stack_common/inc
C_INCLUDES += -I$(SL_NS)/service/sl_websocket_client/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/inc/http_client/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/inc/mqtt/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/inc/sntp
C_INCLUDES += -I$(SL_WSDK)/components/device/silabs/si91x/host_interface/ncp_bus/spi
C_INCLUDES += -I$(SL_WSDK)/components/device/silabs/si91x/host_interface/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/sl_net/inc
C_INCLUDES += -I$(SL_NS)/device/silabs/si91x/wireless/socket/inc
C_INCLUDES += -I$(SL_NS)/device/silabs/si91x/wireless/asynchronous_socket/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/ble/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/firmware_upgrade
C_INCLUDES += -I$(SL_WSDK)/components/device/silabs/si91x/mcu/hal/sli_hal_si91x/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/sli_si91x_wifi_command_engine/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/sli_si91x_wifi_event_handler/inc
C_INCLUDES += -I$(SL_WSDK)/resources/defaults

# --- preprocessor defines ---
C_DEFS += -DSLI_SI917
# --- WiseConnect SDK debug logging (compile-macro gated) -------------------
# SL_SDK_DEBUG_LOG_ENABLE: arm the sl_debug_log() redirect in
# port/src/sl_sdk_debug_log.c (module "SLSDK"). Output goes to the UART log
# console (921600) at full DEBUG verbosity, NOT to aicam.log — the async file
# queue drops lines under this trace volume.
# SLI_HOST_CURRENT_DBGLOG_LEVEL: compile-time gate in sl_constants.h deciding
# which SL_DEBUG_LOG_V2 levels survive; SDK default is WARN-only, so force
# DEBUG to also capture API entry/return values and event callbacks.
# SL_NET_ENABLE_NWP_DEBUG_PRINTS: flips IS_ENABLE_NWP_DEBUG_PRINTS in
# Custom/Hal/Network/netif_manager/sl_net_netif.c to 1 (NWP fw UART debug
# prints + ram-dump helpers); its host-side call/callback traces (module
# "NETIF", NWP_SDK_CALL/NWP_SDK_CB) go to the same UART log console.
# Comment these three out and rebuild for release.
# C_DEFS += -DSL_SDK_DEBUG_LOG_ENABLE  (off on main: release lineage)
# C_DEFS += -DSLI_HOST_CURRENT_DBGLOG_LEVEL=SLI_HOST_DBGLOG_LVL_DEBUG  (off on main: release lineage)
# C_DEFS += -DSL_NET_ENABLE_NWP_DEBUG_PRINTS  (off on main: release lineage)
# ponytail: forces SL_SI91X_EXT_FEAT_XTAL_CLK = ENABLE(1) = bit22 (external 32kHz XTAL).
# v4 SDK changed sl_wifi_device.h #if to `SI917 && RADIO_BOARD_VER2` (was `||` in v3),
# which flips the 917 sleep-clock source to UULP_GPIO_3 bypass (bit23) and breaks
# connected-sleep (flat 7mA). ne301 has the external XTAL, so value 1 is correct.
# (Still `&&` in 4.2.0, so the define stays required.)
C_DEFS += -DSLI_SI91X_MCU_CONFIG_RADIO_BOARD_VER2
C_DEFS += -DSL_CATALOG_FREERTOS_KERNEL_PRESENT
C_DEFS += -DSL_NET_COMPONENT_INCLUDED
C_DEFS += -DSL_WIFI_COMPONENT_INCLUDED
C_DEFS += -DSL_SI91X_SPI_HIGH_SPEED_ENABLE
C_DEFS += -DSLI_SI91X_SOCKETS
# Event-engine thread stack (the thread that runs user callbacks; SDK sl_net.h
# warns it overflows). SLI_EVENT_ENGINE_THREAD_STACK_SIZE is the macro the
# thread creation actually reads (guarded by #ifndef in sli_event_engine.h);
# a -D flag is visible to every TU. SL_SI91X_EVENT_HANDLER_STACK_SIZE is kept
# for parity with the documented knob.
C_DEFS += -DSL_SI91X_EVENT_HANDLER_STACK_SIZE=3072
C_DEFS += -DSLI_EVENT_ENGINE_THREAD_STACK_SIZE=3072
# ponytail: 4.2.0 also SHRANK the other SDK thread defaults vs 4.1.1
# (hal: 2048->1024 hardcoded, command engine: 2048->1636 literal). 1024 words
# for the HAL thread (C1C2/SPI bus loop) overflows under the SLSDK debug-log
# pipeline — first boot HardFaulted with a smashed return address (PC jumped
# into the thread stack, CFSR 0x00100000). Restore the 4.1.1-proven sizes;
# the sources carry #ifndef hooks for these macros (see sli_hal_si91x.c and
# sli_si91x_wifi_command_engine_config.c).
C_DEFS += -DSLI_HAL_SI91X_THREAD_STACK=2048
C_DEFS += -DSLI_WIFI_COMMAND_ENGINE_THREAD_STACK_SIZE=2048
C_DEFS += -DSLI_SI91X_OFFLOAD_NETWORK_STACK
C_DEFS += -DSLI_SI91X_EMBEDDED_MQTT_CLIENT
C_DEFS += -DSLI_SI91X_INTERNAL_HTTP_CLIENT
C_DEFS += -DSLI_SI91X_CONFIG_WIFI6_PARAMS=1
C_DEFS += -DSLI_SI91X_NETWORK_DUAL_STACK_
# C_DEFS += -DSLI_SI91X_ENABLE_BLE
C_DEFS += -DSL_SI91X_ENABLE_LITTLE_ENDIAN
# 2K SPI transceiver packets: 4.2.0's pool-block-size knob (drives
# SLI_WIFI_BUFFER_BLOCK_SIZE and the buffer-manager pool geometry), plus the
# legacy flag still sizing the DMA buffers in Custom/Hal/si91x_ncp_host.c.
C_DEFS += -DSLI_WIFI_BUFFER_CONFIG_BLOCK_SIZE=2324
C_DEFS += -DSPI_EXTENDED_TX_LEN_2K
# CLI fault injection (test-only, validated 2026-08-17, disabled for release):
# enable + rebuild to arm "error_test" C1C2 handshake fault injection.
# C_DEFS += -DSLI_SI91X_SIMULATION_C1C2_ERROR
# Suppress all accumulated WiseConnect API-deprecation warnings (3.5/4.0/4.1/4.2).
# Each version's deprecations are guarded independently (sl_constants.h), so all
# four must be defined to silence the full set under -Werror.
C_DEFS += -DSL_SUPPRESS_DEPRECATION_WARNINGS_WISECONNECT_3_5
C_DEFS += -DSL_SUPPRESS_DEPRECATION_WARNINGS_WISECONNECT_4_0
C_DEFS += -DSL_SUPPRESS_DEPRECATION_WARNINGS_WISECONNECT_4_1
C_DEFS += -DSL_SUPPRESS_DEPRECATION_WARNINGS_WISECONNECT_4_2
