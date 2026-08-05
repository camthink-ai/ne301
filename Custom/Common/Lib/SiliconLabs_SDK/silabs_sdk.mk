#
# Silicon Labs WiseConnect SDK (Si917 NCP) - Makefile integration
#

SL_SDK_ROOT  = ../Custom/Common/Lib/SiliconLabs_SDK
SL_SISDK     = $(SL_SDK_ROOT)/sisdk
SL_WSDK      = $(SL_SDK_ROOT)/wsdk
SL_WSDK_DEV  = $(SL_WSDK)/components/device/silabs/si91x/wireless
SL_PORT      = $(SL_SDK_ROOT)/port

# --- sisdk platform primitives ---
C_SOURCES += $(SL_SISDK)/src/sl_string.c
C_SOURCES += $(SL_SISDK)/src/sl_mem_pool.c

# port/ overrides (ThreadX patch + sl_slist implementation)
C_SOURCES += $(SL_PORT)/src/sli_buffer_manager.c
C_SOURCES += $(SL_PORT)/src/sli_cmsis_os2_ext_task_register.c
C_SOURCES += $(SL_PORT)/src/sl_slist.c

# --- wsdk common ---
C_SOURCES += $(SL_WSDK)/components/common/src/sl_utility.c

# --- internal Wi-Fi components ---
C_SOURCES += $(SL_WSDK)/components/sli_wifi_command_engine/src/sli_wifi_command_engine.c
C_SOURCES += $(SL_WSDK)/components/sli_wifi/src/sli_wifi.c
C_SOURCES += $(SL_WSDK)/components/sli_wifi/src/sli_wifi_utility.c
C_SOURCES += $(SL_WSDK)/components/sli_si91x_wifi_event_handler/src/sli_si91x_wifi_event_handler.c

# --- Wi-Fi protocol ---
C_SOURCES += $(SL_WSDK)/components/protocol/wifi/si91x/sl_wifi.c
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
C_SOURCES += $(SL_WSDK_DEV)/src/sl_rsi_utility.c
C_SOURCES += $(SL_WSDK_DEV)/src/sli_wifi_power_profile.c
C_SOURCES += $(SL_WSDK_DEV)/src/sli_wifi_memory_manager.c
C_SOURCES += $(SL_WSDK_DEV)/src/sl_si91x_http_client_callback_framework.c
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
# Excluded: sl_net_si91x.c (replaced by Custom/Hal sl_net_netif.c)

# --- services ---
C_SOURCES += $(SL_WSDK)/components/service/network_manager/src/sl_net.c
C_SOURCES += $(SL_WSDK)/components/service/network_manager/src/sl_net_basic_certificate_store.c
C_SOURCES += $(SL_WSDK)/components/service/network_manager/src/sl_net_basic_profiles.c
C_SOURCES += $(SL_WSDK)/components/service/network_manager/src/sl_net_credentials.c
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
C_INCLUDES += -I$(SL_PORT)/inc
C_INCLUDES += -I$(SL_SISDK)/inc
C_INCLUDES += -I$(SL_WSDK)/components/common/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_wifi/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_wifi_command_engine/inc
C_INCLUDES += -I$(SL_WSDK)/components/sli_si91x_wifi_event_handler/inc
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
# Port mqtt types override (NWP firmware-compatible INIT layout); must precede SDK mqtt/inc
C_INCLUDES += -I$(SL_PORT)/inc/mqtt/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/inc/mqtt/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/inc/sntp
C_INCLUDES += -I$(SL_WSDK_DEV)/sl_net/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/socket/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/asynchronous_socket/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/ble/inc
C_INCLUDES += -I$(SL_WSDK_DEV)/firmware_upgrade
C_INCLUDES += -I$(SL_WSDK)/resources/defaults

# --- preprocessor defines ---
C_DEFS += -DSLI_SI917
C_DEFS += -DSL_CATALOG_FREERTOS_KERNEL_PRESENT
C_DEFS += -DSL_NET_COMPONENT_INCLUDED
C_DEFS += -DSL_WIFI_COMPONENT_INCLUDED
C_DEFS += -DSL_SI91X_SPI_HIGH_SPEED_ENABLE
C_DEFS += -DSLI_SI91X_SOCKETS
C_DEFS += -DSL_SI91X_EVENT_HANDLER_STACK_SIZE=3072
C_DEFS += -DSLI_SI91X_OFFLOAD_NETWORK_STACK
C_DEFS += -DSLI_SI91X_EMBEDDED_MQTT_CLIENT
C_DEFS += -DSLI_SI91X_INTERNAL_HTTP_CLIENT
C_DEFS += -DSLI_SI91X_CONFIG_WIFI6_PARAMS=1
C_DEFS += -DSLI_SI91X_NETWORK_DUAL_STACK_
# C_DEFS += -DSLI_SI91X_ENABLE_BLE
C_DEFS += -DSL_SI91X_ENABLE_LITTLE_ENDIAN
C_DEFS += -DSPI_EXTENDED_TX_LEN_2K
# C_DEFS += -DSLI_SI91X_SIMULATION_C1C2_ERROR
C_DEFS += -DSL_SUPPRESS_DEPRECATION_WARNINGS_WISECONNECT_4_0
