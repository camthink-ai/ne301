#
# Silicon Labs WiseConnect SDK 4.1.1 (Si917 NCP) - Makefile integration
#
# Sources refreshed from wiseconnect/4.1.1 conan pkg (wiseca1ca655dfba28).
# Component layout differs from 4.0.2: legacy sli_wifi_command_engine ->
# sli_command_engine, the old top-level sli_si91x_wifi_event_handler ->
# sli_event_engine, plus new infra (sli_buffer_manager replaces
# sli_wifi_memory_manager, sli_queue_manager, sli_routing_utility) and device
# sub-components (sli_hal_si91x, sli_si91x_wifi_command_engine/event_handler).
#

SL_SDK_ROOT  = ../Custom/Common/Lib/SiliconLabs_SDK_4_1_1
SL_SISDK     = $(SL_SDK_ROOT)/sisdk
SL_WSDK      = $(SL_SDK_ROOT)/wsdk
SL_WSDK_DEV  = $(SL_WSDK)/components/device/silabs/si91x/wireless
SL_PORT      = $(SL_SDK_ROOT)/port

# --- sisdk platform primitives ---
C_SOURCES += $(SL_SISDK)/src/sl_string.c
C_SOURCES += $(SL_SISDK)/src/sl_mem_pool.c

# --- port/ overrides (ThreadX patches) ---
# sl_slist: SDK header needs an impl. task_register: ext task reg.
# osThreadFlags: cmsis_rtos_threadx does NOT implement it; 4.1.x command/event
# engines use it, so we emulate via per-thread osEventFlags lookup.
# (4.0.2's buffer_manager stub + mqtt types shim are gone: 4.1.1 ships the real
#  sli_buffer_manager component and native mqtt types match the flashed fw.)
C_SOURCES += $(SL_PORT)/src/sl_slist.c
C_SOURCES += $(SL_PORT)/src/sli_cmsis_os2_ext_task_register.c
C_SOURCES += $(SL_PORT)/src/sli_cmsis_os2_thread_flags.c

# --- wsdk common ---
C_SOURCES += $(SL_WSDK)/components/common/src/sl_utility.c

# --- internal Wi-Fi components (4.1.x restructured) ---
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
C_SOURCES += $(SL_WSDK_DEV)/sli_hal_si91x/src/sli_hal_si91x.c
C_SOURCES += $(SL_WSDK_DEV)/sli_si91x_wifi_command_engine/src/sli_si91x_wifi_command_engine.c
C_SOURCES += $(SL_WSDK_DEV)/sli_si91x_wifi_command_engine/src/sli_si91x_wifi_command_engine_config.c
C_SOURCES += $(SL_WSDK_DEV)/sli_si91x_wifi_event_handler/src/sli_si91x_wifi_event_handler.c
C_SOURCES += $(SL_WSDK_DEV)/firmware_upgrade/firmware_upgradation.c
C_SOURCES += $(SL_WSDK_DEV)/icmp/sl_net_ping.c
C_SOURCES += $(SL_WSDK_DEV)/errno/src/sl_si91x_errno.c
C_SOURCES += $(SL_WSDK_DEV)/memory/mem_pool_buffer_quota.c
C_SOURCES += $(SL_WSDK_DEV)/ncp_interface/spi/sl_si91x_spi.c
C_SOURCES += $(SL_WSDK_DEV)/ncp_interface/sl_si91x_ncp_driver.c
C_SOURCES += $(SL_WSDK_DEV)/socket/src/sl_si91x_socket_utility.c
C_SOURCES += $(SL_WSDK_DEV)/asynchronous_socket/src/sl_si91x_socket.c
C_SOURCES += $(SL_WSDK_DEV)/sl_net/src/sl_si91x_net_internal_stack.c
C_SOURCES += $(SL_WSDK_DEV)/sl_net/src/sl_net_si91x_callback_framework.c
C_SOURCES += $(SL_WSDK_DEV)/sl_net/src/sl_net_si91x_integration_handler.c
C_SOURCES += $(SL_WSDK_DEV)/sl_net/src/sl_net_rsi_utility.c
C_SOURCES += $(SL_WSDK_DEV)/sl_net/src/sli_net_si91x_utility.c
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
C_SOURCES += $(SL_WSDK)/components/service/bsd_socket/si91x_socket/sl_si91x_bsd_socket.c
C_SOURCES += $(SL_WSDK)/components/service/sl_http_server/src/sl_http_server.c
C_SOURCES += $(SL_WSDK)/components/service/sl_websocket_client/src/sl_websocket_client.c
C_SOURCES += $(SL_WSDK)/components/service/sl_websocket_client/src/sli_websocket_client_sync.c

# --- include paths ---
# Custom/Hal: mem.h (SLI_MALLOC/SLI_FREE injected into sl_status.h route all
# SDK dynamic memory to the project memory pools)
C_INCLUDES += -I../Custom/Hal
C_INCLUDES += -I$(SL_PORT)/inc
C_INCLUDES += -I$(SL_SISDK)/inc
C_INCLUDES += -I$(SL_WSDK)/components/common/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_wifi/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_command_engine/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_event_engine/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_buffer_manager/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_queue_manager/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_routing_utility/inc
C_INCLUDES += -I$(SL_WSDK)/components/protocol/wifi/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/network_manager/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/mqtt/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/http_client/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/mdns/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/sntp/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/bsd_socket/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/sl_http_server/inc
C_INCLUDES += -I$(SL_WSDK)/components/service/sl_websocket_client/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/inc/http_client/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/inc/mqtt/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/inc/sntp
C_INCLUDES += -I$(SL_WSDK_DEV)/sl_net/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/socket/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/asynchronous_socket/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/ble/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/firmware_upgrade
C_INCLUDES += -I$(SL_WSDK_DEV)/sli_hal_si91x/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/sli_si91x_wifi_command_engine/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/sli_si91x_wifi_event_handler/inc
C_INCLUDES += -I$(SL_WSDK)/resources/defaults

# --- preprocessor defines ---
C_DEFS += -DSLI_SI917
# ponytail: forces SL_SI91X_EXT_FEAT_XTAL_CLK = ENABLE(1) = bit22 (external 32kHz XTAL).
# v4 SDK changed sl_wifi_device.h #if to `SI917 && RADIO_BOARD_VER2` (was `||` in v3),
# which flips the 917 sleep-clock source to UULP_GPIO_3 bypass (bit23) and breaks
# connected-sleep (flat 7mA). ne301 has the external XTAL, so value 1 is correct.
C_DEFS += -DSLI_SI91X_MCU_CONFIG_RADIO_BOARD_VER2
C_DEFS += -DSL_CATALOG_FREERTOS_KERNEL_PRESENT
C_DEFS += -DSL_NET_COMPONENT_INCLUDED
C_DEFS += -DSL_WIFI_COMPONENT_INCLUDED
C_DEFS += -DSL_SI91X_SPI_HIGH_SPEED_ENABLE
C_DEFS += -DSLI_SI91X_SOCKETS
# Event-engine thread stack (the thread that runs user callbacks; SDK sl_net.h
# warns it overflows). The documented knob SL_SI91X_EVENT_HANDLER_STACK_SIZE only
# takes effect via sli_wifi_command_engine_config.h, which sli_event_engine.c —
# the TU that creates this thread — does NOT include, so that override is dead
# there and the thread silently used the 1536 default. Define the macro the
# thread actually reads (sli_event_engine.c:252, guarded by #ifndef in
# sli_event_engine.h:59) directly; a -D flag is visible to every TU.
C_DEFS += -DSL_SI91X_EVENT_HANDLER_STACK_SIZE=3072
C_DEFS += -DSLI_EVENT_ENGINE_THREAD_STACK_SIZE=3072
C_DEFS += -DSLI_SI91X_OFFLOAD_NETWORK_STACK
C_DEFS += -DSLI_SI91X_EMBEDDED_MQTT_CLIENT
C_DEFS += -DSLI_SI91X_INTERNAL_HTTP_CLIENT
C_DEFS += -DSLI_SI91X_CONFIG_WIFI6_PARAMS=1
C_DEFS += -DSLI_SI91X_NETWORK_DUAL_STACK_
# C_DEFS += -DSLI_SI91X_ENABLE_BLE
C_DEFS += -DSL_SI91X_ENABLE_LITTLE_ENDIAN
C_DEFS += -DSPI_EXTENDED_TX_LEN_2K
# CLI fault injection (test-only, validated 2026-08-17, disabled for release):
# enable + rebuild to arm "error_test" C1C2 handshake fault injection.
# C_DEFS += -DSLI_SI91X_SIMULATION_C1C2_ERROR
# Suppress all accumulated WiseConnect API-deprecation warnings (3.5/4.0/4.1).
# Each version's deprecations are guarded independently (sl_constants.h), so all
# three must be defined to silence the full set under -Werror. The 4.0.2 build
# only needed _4_0; 4.1.1 carries _4_0 + _4_1 markers over legacy sl_si91x_* aliases.
C_DEFS += -DSL_SUPPRESS_DEPRECATION_WARNINGS_WISECONNECT_3_5
C_DEFS += -DSL_SUPPRESS_DEPRECATION_WARNINGS_WISECONNECT_4_0
C_DEFS += -DSL_SUPPRESS_DEPRECATION_WARNINGS_WISECONNECT_4_1
