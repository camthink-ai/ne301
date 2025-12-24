######################################
# App Makefile
######################################
# Inherits common configuration from root Makefile (via export)

######################################
# Helper Functions
######################################
rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

######################################
# Target Configuration
######################################
TARGET = $(PROJECT_NAME)_App
MODEL_DIR = ../Model
BINARY_DIR = Binary

# Local build directory
BUILD_DIR = build

######################################
# Source Files
######################################
# C source files - Application layer
C_SOURCES =
C_SOURCES += Core/Src/main.c
C_SOURCES += Core/Src/syscalls.c
C_SOURCES += Core/Src/sysmem.c
C_SOURCES += Core/Src/app_fuseprogramming.c
C_SOURCES += Core/Src/app_threadx.c
C_SOURCES += Core/Src/stm32n6xx_it.c
C_SOURCES += Core/Src/stm32n6xx_hal_msp.c
C_SOURCES += Core/Src/stm32n6xx_hal_timebase_tim.c
C_SOURCES += Core/Src/system_stm32n6xx_s.c
C_SOURCES += $(wildcard Core/Src/adc.c Core/Src/cacheaxi.c Core/Src/csi.c)
C_SOURCES += $(wildcard Core/Src/dma2d.c Core/Src/gpdma.c Core/Src/gpio.c)
C_SOURCES += $(wildcard Core/Src/hpdma.c Core/Src/i2c.c Core/Src/icache.c)
C_SOURCES += $(wildcard Core/Src/iwdg.c Core/Src/jpeg.c Core/Src/ramcfg.c)
C_SOURCES += $(wildcard Core/Src/rng.c Core/Src/rtc.c Core/Src/sai.c)
C_SOURCES += $(wildcard Core/Src/sdmmc.c Core/Src/spi.c Core/Src/tim.c)
C_SOURCES += $(wildcard Core/Src/usart.c Core/Src/usb_otg.c Core/Src/venc.c)
C_SOURCES += $(wildcard Core/Src/wwdg.c Core/Src/xspim.c Core/Src/secure_nsc.c)

# Custom - Core
C_SOURCES += ../Custom/Core/core_init.c

# Custom - Core/Data
C_SOURCES += ../Custom/Core/Data/buffer_mgr.c

# Custom - Core/Example
C_SOURCES += ../Custom/Core/Example/core_example.c
C_SOURCES += ../Custom/Core/Example/driver_test.c
C_SOURCES += ../Custom/Core/Example/tls_test.c

# Custom - Core/Log
C_SOURCES += ../Custom/Core/Log/cli_cmd.c
C_SOURCES += ../Custom/Core/Log/debug.c

# Custom - Core/Security
C_SOURCES += ../Custom/Core/Security/auth_mgr.c

# Custom - Core/System
C_SOURCES += ../Custom/Core/System/dev_manager.c
C_SOURCES += ../Custom/Core/System/event_bus.c
C_SOURCES += ../Custom/Core/System/framework.c
C_SOURCES += ../Custom/Core/System/json_config_mgr.c
C_SOURCES += ../Custom/Core/System/json_config_nvs.c
C_SOURCES += ../Custom/Core/System/json_config_json.c
C_SOURCES += ../Custom/Core/System/json_config_utils.c
C_SOURCES += ../Custom/Core/System/ota_header.c
C_SOURCES += ../Custom/Core/System/power_manager.c
C_SOURCES += ../Custom/Core/System/scheduler_manager.c
C_SOURCES += ../Custom/Core/System/upgrade_manager.c

# Custom - Core/Video
C_SOURCES += ../Custom/Core/Video/ai_draw_service.c
C_SOURCES += ../Custom/Core/Video/video_ai_node.c
C_SOURCES += ../Custom/Core/Video/video_camera_node.c
C_SOURCES += ../Custom/Core/Video/video_encoder_node.c
C_SOURCES += ../Custom/Core/Video/video_frame_mgr.c
C_SOURCES += ../Custom/Core/Video/video_pipeline.c

# Custom - Hal
C_SOURCES += ../Custom/Hal/ai_draw.c
C_SOURCES += ../Custom/Hal/camera.c
C_SOURCES += ../Custom/Hal/cat1.c
C_SOURCES += ../Custom/Hal/codec.c
C_SOURCES += ../Custom/Hal/draw.c
C_SOURCES += ../Custom/Hal/driver_core.c
C_SOURCES += ../Custom/Hal/drtc.c
C_SOURCES += ../Custom/Hal/enc.c
C_SOURCES += ../Custom/Hal/exti.c
C_SOURCES += ../Custom/Hal/jpegc.c
C_SOURCES += ../Custom/Hal/mem.c
C_SOURCES += ../Custom/Hal/misc.c
C_SOURCES += ../Custom/Hal/nn.c
C_SOURCES += ../Custom/Hal/pixel_format_map.c
C_SOURCES += ../Custom/Hal/pwr.c
C_SOURCES += ../Custom/Hal/sd_file.c
C_SOURCES += ../Custom/Hal/si91x_ncp_host.c
C_SOURCES += ../Custom/Hal/storage.c
C_SOURCES += ../Custom/Hal/system_top.c
C_SOURCES += ../Custom/Hal/u0_module.c
C_SOURCES += ../Custom/Hal/usbx_host.c
C_SOURCES += ../Custom/Hal/usb_host_video.c
C_SOURCES += ../Custom/Hal/usb_host_ecm.c
C_SOURCES += ../Custom/Hal/uvc.c
C_SOURCES += ../Custom/Hal/wdg.c
C_SOURCES += ../Custom/Hal/wifi.c
C_SOURCES += ../Custom/Hal/usbd_conf.c

# Custom - Hal/Network
C_SOURCES += ../Custom/Hal/Network/dhcpserver/dhcpserver.c
C_SOURCES += ../Custom/Hal/Network/icmp_client/icmp_client.c
C_SOURCES += ../Custom/Hal/Network/icmp_client/ping.c
C_SOURCES += ../Custom/Hal/Network/iperf_test/iperf_test.c
C_SOURCES += ../Custom/Hal/Network/iperf_test/lwiperf.c
C_SOURCES += ../Custom/Hal/Network/mqtt_client/mqtt_outbox.c
C_SOURCES += ../Custom/Hal/Network/mqtt_client/ms_mqtt_client.c
C_SOURCES += ../Custom/Hal/Network/mqtt_client/ms_mqtt_client_test.c
C_SOURCES += ../Custom/Hal/Network/mqtt_client/si91x_mqtt_client.c
C_SOURCES += ../Custom/Hal/Network/ms_network_port/ms_network.c
C_SOURCES += ../Custom/Hal/Network/ms_network_port/ms_network_test.c
C_SOURCES += ../Custom/Hal/Network/netif_manager/eg912u_gl_netif.c
C_SOURCES += ../Custom/Hal/Network/netif_manager/netif_manager.c
C_SOURCES += ../Custom/Hal/Network/netif_manager/sl_net_netif.c
C_SOURCES += ../Custom/Hal/Network/netif_manager/w5500_netif.c
C_SOURCES += ../Custom/Hal/Network/netif_manager/usb_ecm_netif.c
C_SOURCES += ../Custom/Hal/Network/netif_manager/netif_init_manager.c
C_SOURCES += ../Custom/Hal/Network/sl_rsi_ble/sl_rsi_ble.c
C_SOURCES += ../Custom/Hal/Network/w5500/eth_tool.c
C_SOURCES += ../Custom/Hal/Network/w5500/w5500.c
C_SOURCES += ../Custom/Hal/Network/ms_modem/ms_modem.c
C_SOURCES += ../Custom/Hal/Network/ms_modem/ms_modem_at.c

# Custom - Services
C_SOURCES += ../Custom/Services/service_init.c
C_SOURCES += ../Custom/Services/AI/ai_service.c
C_SOURCES += ../Custom/Services/Communication/communication_service.c
C_SOURCES += ../Custom/Services/Device/device_service.c
C_SOURCES += ../Custom/Services/MQTT/mqtt_service.c
C_SOURCES += ../Custom/Services/OTA/ota_service.c
C_SOURCES += ../Custom/Services/System/system_service.c

# Custom - Services/Web
C_SOURCES += ../Custom/Services/Web/web_api.c
C_SOURCES += ../Custom/Services/Web/web_assets.c
C_SOURCES += ../Custom/Services/Web/web_server.c
C_SOURCES += ../Custom/Services/Web/web_service.c
C_SOURCES += ../Custom/Services/Web/websocket_stream_server.c
C_SOURCES += ../Custom/Services/Web/api/api_ai_management_module.c
C_SOURCES += ../Custom/Services/Web/api/api_auth_module.c
C_SOURCES += ../Custom/Services/Web/api/api_device_module.c
C_SOURCES += ../Custom/Services/Web/api/api_model_validation_module.c
C_SOURCES += ../Custom/Services/Web/api/api_mqtt_module.c
C_SOURCES += ../Custom/Services/Web/api/api_network_module.c
C_SOURCES += ../Custom/Services/Web/api/api_ota_module.c
C_SOURCES += ../Custom/Services/Web/api/api_work_mode_module.c
C_SOURCES += ../Custom/Services/Web/api/api_business_error.c


# Custom - Common/Inc
C_SOURCES += ../Custom/Common/Inc/aicam_error.c

# Custom - Common/Utils
C_SOURCES += ../Custom/Common/Utils/generic_cmdline.c
C_SOURCES += ../Custom/Common/Utils/generic_file.c
C_SOURCES += ../Custom/Common/Utils/generic_key.c
C_SOURCES += ../Custom/Common/Utils/generic_led.c
C_SOURCES += ../Custom/Common/Utils/generic_log.c
C_SOURCES += ../Custom/Common/Utils/generic_math.c
C_SOURCES += ../Custom/Common/Utils/generic_utils.c
C_SOURCES += ../Custom/Common/Utils/generic_ymodem.c

# Custom - Common/Lib - Individual libraries
C_SOURCES += ../Custom/Common/Lib/atc/atc.c
C_SOURCES += ../Custom/Common/Lib/cJSON/cJSON.c
C_SOURCES += ../Custom/Common/Lib/littlefs/lfs.c
C_SOURCES += ../Custom/Common/Lib/littlefs/lfs_util.c
C_SOURCES += ../Custom/Common/Lib/littlefs/bd/lfs_emubd.c
C_SOURCES += ../Custom/Common/Lib/littlefs/bd/lfs_filebd.c
C_SOURCES += ../Custom/Common/Lib/littlefs/bd/lfs_rambd.c
C_SOURCES += ../Custom/Common/Lib/mongoose/mongoose.c
C_SOURCES += ../Custom/Common/Lib/mpool/mpool.c
C_SOURCES += ../Custom/Common/Lib/nau881x/nau881x.c
C_SOURCES += ../Custom/Common/Lib/nvs/nvs.c

# Custom - Common/Lib/pp
C_SOURCES += ../Custom/Common/Lib/pp/pp.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_od_yolo_v2_uf.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_od_yolo_v2_ui.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_od_yolo_v5_uu.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_od_st_yolox_uf.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_od_st_yolox_ui.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_od_st_ssd_uf.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_od_fd_blazeface_uf.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_od_fd_blazeface_ui.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_od_fd_blazeface_uu.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_od_yolo_v8_uf.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_od_yolo_v8_ui.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_mpe_yolo_v8_uf.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_mpe_yolo_v8_ui.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_mpe_pd_uf.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_spe_movenet_uf.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_spe_movenet_ui.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_iseg_yolo_v8_ui.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_sseg_deeplab_v3_uf.c
C_SOURCES += ../Custom/Common/Lib/pp/pp_sseg_deeplab_v3_ui.c

# Custom - Common/Lib/ms_bridging
C_SOURCES += ../Custom/Common/Lib/ms_bridging/ms_bridging.c

# Custom - Common/Lib/rtmpdump
C_SOURCES += ../Custom/Hal/Network/rtmp_push_client/rtmp_publisher.c
C_SOURCES += ../Custom/Hal/Network/rtmp_push_client/rtmp_push_test.c
C_SOURCES += ../Custom/Common/Lib/rtmpdump/librtmp/rtmp.c
C_SOURCES += ../Custom/Common/Lib/rtmpdump/librtmp/amf.c
C_SOURCES += ../Custom/Common/Lib/rtmpdump/librtmp/log.c
C_SOURCES += ../Custom/Common/Lib/rtmpdump/librtmp/parseurl.c
C_SOURCES += ../Custom/Common/Lib/rtmpdump/librtmp/hashswf.c

# Custom - Common/Lib/PahoMqtt
C_SOURCES += ../Custom/Common/Lib/PahoMqtt/MQTTConnectClient.c
C_SOURCES += ../Custom/Common/Lib/PahoMqtt/MQTTConnectServer.c
C_SOURCES += ../Custom/Common/Lib/PahoMqtt/MQTTDeserializePublish.c
C_SOURCES += ../Custom/Common/Lib/PahoMqtt/MQTTFormat.c
C_SOURCES += ../Custom/Common/Lib/PahoMqtt/MQTTPacket.c
C_SOURCES += ../Custom/Common/Lib/PahoMqtt/MQTTSerializePublish.c
C_SOURCES += ../Custom/Common/Lib/PahoMqtt/MQTTSubscribeClient.c
C_SOURCES += ../Custom/Common/Lib/PahoMqtt/MQTTSubscribeServer.c
C_SOURCES += ../Custom/Common/Lib/PahoMqtt/MQTTUnsubscribeClient.c
C_SOURCES += ../Custom/Common/Lib/PahoMqtt/MQTTUnsubscribeServer.c

# Custom - Common/Lib/MbedTLS
C_SOURCES += $(wildcard ../Custom/Common/Lib/MbedTLS/library/*.c)
C_SOURCES += $(wildcard ../Custom/Common/Lib/MbedTLS/port/*.c)

# Custom - Common/Lib/lwip
C_SOURCES += $(wildcard ../Custom/Common/Lib/lwip/api/*.c)
C_SOURCES += ../Custom/Common/Lib/lwip/apps/sntp/sntp.c
C_SOURCES += $(wildcard ../Custom/Common/Lib/lwip/core/*.c)
C_SOURCES += $(wildcard ../Custom/Common/Lib/lwip/core/ipv4/*.c)
C_SOURCES += $(wildcard ../Custom/Common/Lib/lwip/core/ipv6/*.c)
C_SOURCES += $(wildcard ../Custom/Common/Lib/lwip/netif/*.c)
C_SOURCES += $(wildcard ../Custom/Common/Lib/lwip/netif/ppp/*.c)
C_SOURCES += $(wildcard ../Custom/Common/Lib/lwip/netif/ppp/polarssl/*.c)
C_SOURCES += ../Custom/Common/Lib/lwip/port/sys_arch.c

# Custom - Common/Lib/si91x
C_SOURCES += ../Custom/Common/Lib/si91x/common/src/sl_slist.c
C_SOURCES += ../Custom/Common/Lib/si91x/common/src/sl_utility.c
C_SOURCES += ../Custom/Common/Lib/si91x/common/src/sli_wifi_command_engine.c
C_SOURCES += ../Custom/Common/Lib/si91x/protocol/ble/src/rsi_ble_gap_apis.c
C_SOURCES += ../Custom/Common/Lib/si91x/protocol/ble/src/rsi_ble_gatt_apis.c
C_SOURCES += ../Custom/Common/Lib/si91x/protocol/ble/src/rsi_bt_ble.c
C_SOURCES += ../Custom/Common/Lib/si91x/protocol/ble/src/rsi_bt_common_apis.c
C_SOURCES += ../Custom/Common/Lib/si91x/protocol/ble/src/rsi_common_apis.c
C_SOURCES += ../Custom/Common/Lib/si91x/protocol/ble/src/rsi_utils.c
C_SOURCES += ../Custom/Common/Lib/si91x/protocol/ble/src/sl_si91x_ble.c
C_SOURCES += ../Custom/Common/Lib/si91x/protocol/wifi/src/sl_wifi.c
C_SOURCES += ../Custom/Common/Lib/si91x/protocol/wifi/src/sl_wifi_basic_credentials.c
C_SOURCES += ../Custom/Common/Lib/si91x/protocol/wifi/src/sl_wifi_callback_framework.c
C_SOURCES += ../Custom/Common/Lib/si91x/service/si91x/sl_http_client.c
C_SOURCES += ../Custom/Common/Lib/si91x/service/si91x/sl_mdns.c
C_SOURCES += ../Custom/Common/Lib/si91x/service/si91x/sl_mqtt_client.c
# C_SOURCES += ../Custom/Common/Lib/si91x/service/si91x/sl_net_si91x.c  # Excluded per .cproject
C_SOURCES += ../Custom/Common/Lib/si91x/service/si91x/sl_si91x_bsd_socket.c
C_SOURCES += ../Custom/Common/Lib/si91x/service/si91x/sl_sntp.c
C_SOURCES += ../Custom/Common/Lib/si91x/service/si91x/sli_net_common_utility.c
C_SOURCES += ../Custom/Common/Lib/si91x/service/src/sl_http_server.c
C_SOURCES += ../Custom/Common/Lib/si91x/service/src/sl_net.c
C_SOURCES += ../Custom/Common/Lib/si91x/service/src/sl_net_basic_certificate_store.c
C_SOURCES += ../Custom/Common/Lib/si91x/service/src/sl_net_basic_profiles.c
C_SOURCES += ../Custom/Common/Lib/si91x/service/src/sl_net_credentials.c
C_SOURCES += ../Custom/Common/Lib/si91x/service/src/sl_websocket_client.c
C_SOURCES += ../Custom/Common/Lib/si91x/silabs_utility/src/sl_mem_pool.c
C_SOURCES += ../Custom/Common/Lib/si91x/silabs_utility/src/sl_string.c
C_SOURCES += ../Custom/Common/Lib/si91x/silabs_utility/src/sli_cmsis_os2_ext_task_register.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/firmware_upgradation.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/mem_pool_buffer_quota.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_net_ping.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_net_rsi_utility.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_net_si91x_callback_framework.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_net_si91x_integration_handler.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_rsi_utility.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_si91x_driver.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_si91x_errno.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_si91x_http_client_callback_framework.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_si91x_ncp_driver.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_si91x_net_credentials.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_si91x_net_internal_stack.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_si91x_socket.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_si91x_socket_utility.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sl_si91x_spi.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sli_net_si91x_utility.c
C_SOURCES += ../Custom/Common/Lib/si91x/wireless/src/sli_si91x_wifi_event_handler.c
# Excluded: sli_si91x_multithreaded.c per .cproject

# GCC support files
C_SOURCES += ../Gcc/Src/console.c
C_SOURCES += ../Gcc/Src/fast_memcpy.c
C_SOURCES += ../Gcc/Src/threadx_libc.c

# Middlewares - AI Runtime
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/Devices/STM32N6XX/mcu_cache.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/Devices/STM32N6XX/npu_cache.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ecloader.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_cipher.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_dbgtrc.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_debug.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_lib.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_lib_sw_operators.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_osal_freertos.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_osal_threadx.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_osal_zephyr.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_profiler.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_reloc_network.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_rt_main.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_runtime.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_aton_util.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_sw_float.c
C_SOURCES += ../Middlewares/ST/AI_Runtime/Npu/ll_aton/ll_sw_integer.c

# Middlewares - Camera
C_SOURCES += ../Middlewares/ST/STM32_Camera_Middleware/cmw_camera.c
C_SOURCES += ../Middlewares/ST/STM32_Camera_Middleware/cmw_utils.c
C_SOURCES += ../Middlewares/ST/STM32_Camera_Middleware/sensors/cmw_os04c10.c
C_SOURCES += ../Middlewares/ST/STM32_Camera_Middleware/sensors/os04c10/os04c10.c
C_SOURCES += ../Middlewares/ST/STM32_Camera_Middleware/sensors/os04c10/os04c10_reg.c

# Middlewares - ISP Library
C_SOURCES += ../Middlewares/ST/STM32_ISP_Library/isp/Src/isp_algo.c
C_SOURCES += ../Middlewares/ST/STM32_ISP_Library/isp/Src/isp_cmd_parser.c
C_SOURCES += ../Middlewares/ST/STM32_ISP_Library/isp/Src/isp_core.c
C_SOURCES += ../Middlewares/ST/STM32_ISP_Library/isp/Src/isp_services.c
C_SOURCES += ../Middlewares/ST/STM32_ISP_Library/isp/Src/isp_tool_com.c

# Middlewares - ISP Library USB Device (for ISP tuning tool)
C_SOURCES += ../Middlewares/ST/STM32_ISP_Library/isp/USB_Device/Src/usb_device.c
C_SOURCES += ../Middlewares/ST/STM32_ISP_Library/isp/USB_Device/Src/usbd_cdc_if.c
C_SOURCES += ../Middlewares/ST/STM32_ISP_Library/isp/USB_Device/Src/usbd_desc.c

# Middlewares - ThreadX (based on .project linked files)
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_block_allocate.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_block_pool_cleanup.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_block_pool_create.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_block_pool_delete.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_block_pool_info_get.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_block_pool_initialize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_block_pool_prioritize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_block_release.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_byte_allocate.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_byte_pool_create.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_byte_pool_delete.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_byte_pool_info_get.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_byte_pool_initialize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_byte_pool_cleanup.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_byte_pool_prioritize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_byte_pool_search.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_byte_release.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_event_flags_cleanup.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_event_flags_create.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_event_flags_delete.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_event_flags_get.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_event_flags_info_get.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_event_flags_initialize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_event_flags_set.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_event_flags_set_notify.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_initialize_high_level.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_initialize_kernel_enter.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_initialize_kernel_setup.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_mutex_cleanup.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_mutex_create.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_mutex_delete.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_mutex_get.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_mutex_info_get.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_mutex_initialize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_mutex_prioritize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_mutex_priority_change.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_mutex_put.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_queue_cleanup.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_queue_create.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_queue_delete.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_queue_flush.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_queue_front_send.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_queue_info_get.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_queue_initialize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_queue_prioritize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_queue_receive.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_queue_send.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_queue_send_notify.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_semaphore_ceiling_put.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_semaphore_cleanup.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_semaphore_create.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_semaphore_delete.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_semaphore_get.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_semaphore_info_get.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_semaphore_initialize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_semaphore_prioritize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_semaphore_put.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_semaphore_put_notify.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_create.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_delete.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_entry_exit_notify.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_identify.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_info_get.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_initialize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_preemption_change.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_priority_change.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_relinquish.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_reset.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_resume.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_shell_entry.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_sleep.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_stack_analyze.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_stack_error_handler.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_stack_error_notify.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_suspend.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_system_preempt_check.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_system_resume.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_system_suspend.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_terminate.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_time_slice.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_time_slice_change.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_timeout.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_thread_wait_abort.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_time_get.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_time_set.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_timer_activate.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_timer_change.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_timer_create.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_timer_deactivate.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_timer_delete.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_timer_expiration_process.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_timer_info_get.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_timer_initialize.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_timer_system_activate.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_timer_system_deactivate.c
C_SOURCES += ../Middlewares/ST/threadx/common/src/tx_timer_thread_entry.c

# ThreadX port - C files only (assembly versions are in ASM_SOURCES_S)
C_SOURCES += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_thread_secure_stack.c

# ThreadX utilities
C_SOURCES += ../Middlewares/ST/threadx/utility/execution_profile_kit/tx_execution_profile.c
C_SOURCES += ../Middlewares/ST/threadx/utility/low_power/tx_low_power.c
C_SOURCES += ../Middlewares/ST/threadx/utility/rtos_compatibility_layers/FreeRTOS/tx_freertos.c
C_SOURCES += ../Middlewares/ST/cmsis_rtos_threadx/cmsis_os2.c

# Middlewares - ThreadX error checking (txe_*.c files from .project)
C_SOURCES += $(wildcard ../Middlewares/ST/threadx/common/src/txe_*.c)
C_SOURCES += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/txe_thread_secure_stack_free.c

# Middlewares - UVCL (USB Video Class Library)
C_SOURCES += ../Middlewares/ST/uvcl/Src/uvcl.c
C_SOURCES += ../Middlewares/ST/uvcl/Src/uvcl_desc.c
C_SOURCES += ../Middlewares/ST/uvcl/Src/usbx/uvcl_usbx.c

# Middlewares - USBX Core System
C_SOURCES += $(wildcard ../Middlewares/ST/usbx/common/core/src/ux_system_*.c)
C_SOURCES += $(wildcard ../Middlewares/ST/usbx/common/core/src/ux_utility_*.c)

# Middlewares - USBX Device Classes
C_SOURCES += $(wildcard ../Middlewares/ST/usbx/common/usbx_device_classes/src/ux_device_class_audio*.c)
C_SOURCES += $(wildcard ../Middlewares/ST/usbx/common/usbx_device_classes/src/ux_device_class_cdc_acm*.c)
C_SOURCES += $(wildcard ../Middlewares/ST/usbx/common/usbx_device_classes/src/ux_device_class_video*.c)

# Middlewares - USBX Device Controllers
C_SOURCES += $(wildcard ../Middlewares/ST/usbx/common/usbx_stm32_device_controllers/ux_dcd_stm32*.c)

# Middlewares - USBX Device CoreStack
C_SOURCES += $(wildcard ../Middlewares/ST/usbx/common/core/src/ux_device_stack_*.c)

# Middlewares - USBX Host Classes
C_SOURCES += $(wildcard ../Middlewares/ST/usbx/common/usbx_host_classes/src/ux_host_class_video*.c)
C_SOURCES += $(wildcard ../Middlewares/ST/usbx/common/usbx_host_classes/src/ux_host_class_cdc_ecm*.c)

# Middlewares - USBX Host Controllers
C_SOURCES += $(wildcard ../Middlewares/ST/usbx/common/usbx_stm32_host_controllers/ux_hcd_stm32*.c)

# Middlewares - USBX Host CoreStack
C_SOURCES += $(wildcard ../Middlewares/ST/usbx/common/core/src/ux_host_stack_*.c)

# Middlewares - STM32 USB Device Library (for ISP tuning tool)
C_SOURCES += ../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c
C_SOURCES += ../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
C_SOURCES += ../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c
C_SOURCES += $(wildcard ../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/*.c)

# Middlewares - FileX
C_SOURCES += ../Middlewares/ST/filex/common/drivers/fx_stm32_sd_driver.c
C_SOURCES += $(wildcard ../Middlewares/ST/filex/common/src/fx_*.c)
C_SOURCES += $(wildcard ../Middlewares/ST/filex/common/src/fxe_*.c)

# Middlewares - Vision Models Post-processing
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/iseg_pp_yolov8.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/mpe_pp_yolov8.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/od_pp_centernet.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/od_pp_fd_blazeface.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/od_pp_ssd.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/od_pp_ssd_st.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/od_pp_st_yolox.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/od_pp_yolov2.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/od_pp_yolov4.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/od_pp_yolov5.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/od_pp_yolov8.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/pd_pp_model.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/spe_movenet_pp.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/sseg_pp_deeplabv3.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/vision_models_pp.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/vision_models_pp_maxi_if32.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/vision_models_pp_maxi_is8.c
C_SOURCES += ../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Src/vision_models_pp_maxi_iu8.c

# Middlewares - VideoEncoder
C_SOURCES += ../Middlewares/ST/VideoEncoder_EWL/ewl_impl.c
C_SOURCES += $(wildcard ../Middlewares/Third_Party/VideoEncoder/source/common/*.c)
C_SOURCES += $(wildcard ../Middlewares/Third_Party/VideoEncoder/source/h264/*.c)
C_SOURCES += $(wildcard ../Middlewares/Third_Party/VideoEncoder/source/jpeg/*.c)

# HAL drivers
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_cortex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dcmipp.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma2d.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_gpio.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_ltdc.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_ltdc_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rif.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_ramcfg.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_cacheaxi.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_xspi.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_bsec.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_adc.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_adc_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_cryp.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_cryp_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_exti.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_hash.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_hcd.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_icache.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_iwdg.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_jpeg.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_mce.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_mmc.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_mmc_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pcd.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pcd_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pka.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rng.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rtc.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rtc_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sai.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sai_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sd.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sd_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sdio.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_sdmmc.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_spi.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_spi_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_tim.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_tim_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_wwdg.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_usb.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_venc.c

# BSP
C_SOURCES += ../BSP/STM32N6570-DK/stm32n6570_discovery.c
C_SOURCES += ../BSP/STM32N6570-DK/stm32n6570_discovery_bus.c
C_SOURCES += ../BSP/STM32N6570-DK/stm32n6570_discovery_xspi.c
C_SOURCES += ../BSP/Components/aps256xx/aps256xx.c

# BSP - Utilities
C_SOURCES += ../BSP/Utilities/Fonts/font12.c
C_SOURCES += ../BSP/Utilities/Fonts/font16.c
C_SOURCES += ../BSP/Utilities/JPEG/jpeg_utils.c

# Assembly source files (.s lowercase)
ASM_SOURCES =
ASM_SOURCES += Core/Startup/startup_stm32n657l0hxq.s

# Assembly source files (.S uppercase - from .project)
ASM_SOURCES_S =
ASM_SOURCES_S += Core/Src/tx_initialize_low_level.S
ASM_SOURCES_S += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_thread_context_restore.S
ASM_SOURCES_S += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_thread_context_save.S
ASM_SOURCES_S += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_thread_interrupt_control.S
ASM_SOURCES_S += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_thread_interrupt_disable.S
ASM_SOURCES_S += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_thread_interrupt_restore.S
ASM_SOURCES_S += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_thread_schedule.S
ASM_SOURCES_S += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_thread_secure_stack_allocate.S
ASM_SOURCES_S += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_thread_secure_stack_free.S
ASM_SOURCES_S += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_thread_secure_stack_initialize.S
ASM_SOURCES_S += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_thread_stack_build.S
ASM_SOURCES_S += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_thread_system_return.S
ASM_SOURCES_S += ../Middlewares/ST/threadx/ports/cortex_M55/gnu/src/tx_timer_interrupt.S

######################################
# C Compiler Defines (Organized by Category)
######################################
C_DEFS = $(COMMON_DEFS)
C_DEFS += -DBOOT_IN_PSRAM
# C_DEFS += -DSTM32N6_DK_BOARD

# Basic
C_DEFS += -DDEBUG
C_DEFS += -DVECT_TAB_SRAM
C_DEFS += -DUSE_HAL_DRIVER
C_DEFS += -DUSE_FULL_LL_DRIVER
C_DEFS += -DUSE_MEMORY_POOL_ALLOCATION
C_DEFS += -DUSE_OS04C10_SENSOR
C_DEFS += -DSTM32_THREAD_SAFE_STRATEGY=2
C_DEFS += -DENABLE_U0_MODULE=1
# C_DEFS += -DISP_MW_TUNING_TOOL_SUPPORT

# AI Runtime
C_DEFS += -DAPP_HAS_PARALLEL_NETWORKS=1
C_DEFS += -DLL_ATON_DUMP_DEBUG_API
C_DEFS += -DLL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
C_DEFS += -DLL_ATON_OSAL=LL_ATON_OSAL_THREADX
C_DEFS += -DLL_ATON_RT_MODE=LL_ATON_RT_ASYNC
C_DEFS += -DLL_ATON_SW_FALLBACK
C_DEFS += -DLL_ATON_EB_DBG_INFO
C_DEFS += -DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=1
C_DEFS += -DLL_ATON_RT_RELOC

# ThreadX
C_DEFS += -DFEAT_THREADX
C_DEFS += -DTX_INCLUDE_USER_DEFINE_FILE
C_DEFS += -DTX_SINGLE_MODE_SECURE=1
C_DEFS += -DTX_HAS_PARALLEL_NETWORKS=0
C_DEFS += -DTX_MAX_PARALLEL_NETWORKS=1
C_DEFS += -DTX_EXECUTION_PROFILE_ENABLE
C_DEFS += -DTX_CORTEX_M_EPK
C_DEFS += -DTX_THREAD_ENABLE_PERFORMANCE_INFO
C_DEFS += -DTX_ENABLE_STACK_CHECKING

# FileX
C_DEFS += -DFX_ENABLE_EXFAT=1

# USBX
C_DEFS += -DUX_INCLUDE_USER_DEFINE_FILE
C_DEFS += -DUVC_LIB_USE_USBX
C_DEFS += -DUVC_LIB_USE_DMA
C_DEFS += -DUSBL_PACKET_PER_MICRO_FRAME=3
C_DEFS += -DUX_HOST_CLASS_NETWORK_NO_NX

# MbedTLS
C_DEFS += '-DMBEDTLS_CONFIG_FILE="config-ccm-psk-tls1_2.h"'
C_DEFS += -DUSE_HAL_HASH_SUSPEND_RESUME=1
C_DEFS += -DUSE_HAL_CRYP_SUSPEND_RESUME=1

# Mongoose
C_DEFS += -DMG_ARCH=MG_ARCH_CMSIS_RTOS2
C_DEFS += -DMG_ENABLE_LWIP=1

# RTMP Library - Enable LWIP and STM32N6 adapters
C_DEFS += -DUSE_LWIP
C_DEFS += -DSTM32N6

# Si91x WiFi
C_DEFS += -DSLI_SI917
C_DEFS += -DSL_CATALOG_FREERTOS_KERNEL_PRESENT
C_DEFS += -DSL_NET_COMPONENT_INCLUDED
C_DEFS += -DSL_WIFI_COMPONENT_INCLUDED
C_DEFS += -DSL_SI91X_SPI_HIGH_SPEED_ENABLE
C_DEFS += -DSLI_SI91X_SOCKETS
C_DEFS += -DSL_SI91X_EVENT_HANDLER_STACK_SIZE=3072
C_DEFS += -DSLI_SI91X_OFFLOAD_NETWORK_STACK
C_DEFS += -DSLI_SI91X_EMBEDDED_MQTT_CLIENT
C_DEFS += -DSLI_SI91X_CONFIG_WIFI6_PARAMS=1
C_DEFS += -DSLI_SI91X_NETWORK_DUAL_STACK_
C_DEFS += -DSLI_SI91X_ENABLE_BLE
C_DEFS += -DSL_SI91X_ENABLE_LITTLE_ENDIAN
C_DEFS += -DSPI_EXTENDED_TX_LEN_2K

######################################
# Assembly-specific Defines
######################################
AS_DEFS =
AS_DEFS += -DDEBUG
AS_DEFS += -DVECT_TAB_SRAM
AS_DEFS += -DTX_MAX_PARALLEL_NETWORKS=1
AS_DEFS += -DLL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
AS_DEFS += -DLL_ATON_OSAL=LL_ATON_OSAL_THREADX
AS_DEFS += -DLL_ATON_RT_MODE=LL_ATON_RT_ASYNC
AS_DEFS += -DLL_ATON_SW_FALLBACK
AS_DEFS += -DLL_ATON_DBG_BUFFER_INFO_EXCLUDED=1
AS_DEFS += -DTX_HAS_PARALLEL_NETWORKS=0
AS_DEFS += -DTX_SINGLE_MODE_SECURE=1
AS_DEFS += -DFEAT_THREADX
AS_DEFS += -DTX_INCLUDE_USER_DEFINE_FILE
AS_DEFS += -DTX_EXECUTION_PROFILE_ENABLE
AS_DEFS += -DUVC_LIB_USE_USBX
AS_DEFS += -DUX_INCLUDE_USER_DEFINE_FILE
AS_DEFS += -DUSBL_PACKET_PER_MICRO_FRAME=3
AS_DEFS += -DUVC_LIB_USE_DMA
AS_DEFS += -DTX_LOW_POWER
AS_DEFS += -DTX_ENABLE_EXECUTION_CHANGE_NOTIFY

######################################
# Assembly Include Paths
######################################
AS_INCLUDES =
AS_INCLUDES += -ICore/Inc
AS_INCLUDES += -I../Secure_nsclib
AS_INCLUDES += -I../Middlewares/Third_Party/CMSIS/RTOS2/Include
AS_INCLUDES += -I../Drivers/STM32N6xx_HAL_Driver/Inc
AS_INCLUDES += -I../Drivers/CMSIS/Device/ST/STM32N6xx/Include
AS_INCLUDES += -I../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy
AS_INCLUDES += -I../Middlewares/ST/usbx/common/core/inc
AS_INCLUDES += -I../Middlewares/ST/usbx/ports/generic/inc
AS_INCLUDES += -I../Drivers/CMSIS/Include
AS_INCLUDES += -I../Middlewares/ST/AI_Runtime/Npu/Devices/STM32N6XX
AS_INCLUDES += -I../Middlewares/ST/AI_Runtime/Inc
AS_INCLUDES += -I../Middlewares/ST/AI_Runtime/Npu/ll_aton
AS_INCLUDES += -I../Middlewares/ST/threadx/common/inc
AS_INCLUDES += -I../Middlewares/ST/threadx/ports/cortex_M55/gnu/inc
AS_INCLUDES += -I../Middlewares/ST/threadx/utility/low_power
AS_INCLUDES += -I../Middlewares/ST/usbx/common/usbx_stm32_device_controllers
AS_INCLUDES += -I../Middlewares/ST/usbx/common/usbx_device_classes/inc

######################################
# C Include Paths (Organized by Category)
######################################
C_INCLUDES =

# Core
C_INCLUDES += -ICore/Inc
C_INCLUDES += -ICore/ThreadSafe

# Custom - Application
C_INCLUDES += -I../Custom
C_INCLUDES += -I../Custom/Core
C_INCLUDES += -I../Custom/Core/Data
C_INCLUDES += -I../Custom/Core/Example
C_INCLUDES += -I../Custom/Core/Log
C_INCLUDES += -I../Custom/Core/Security
C_INCLUDES += -I../Custom/Core/System
C_INCLUDES += -I../Custom/Core/Video

# Custom - HAL
C_INCLUDES += -I../Custom/Hal
C_INCLUDES += -I../Custom/Hal/Network/dhcpserver
C_INCLUDES += -I../Custom/Hal/Network/icmp_client
C_INCLUDES += -I../Custom/Hal/Network/iperf_test
C_INCLUDES += -I../Custom/Hal/Network/mqtt_client
C_INCLUDES += -I../Custom/Hal/Network/ms_network_port
C_INCLUDES += -I../Custom/Hal/Network/netif_manager
C_INCLUDES += -I../Custom/Hal/Network/sl_rsi_ble
C_INCLUDES += -I../Custom/Hal/Network/w5500
C_INCLUDES += -I../Custom/Hal/Network/ms_modem

# Custom - Services
C_INCLUDES += -I../Custom/Services
C_INCLUDES += -I../Custom/Services/AI
C_INCLUDES += -I../Custom/Services/Communication
C_INCLUDES += -I../Custom/Services/Device
C_INCLUDES += -I../Custom/Services/MQTT
C_INCLUDES += -I../Custom/Services/OTA
C_INCLUDES += -I../Custom/Services/System
C_INCLUDES += -I../Custom/Services/Web
C_INCLUDES += -I../Custom/Services/Web/api

# Custom - Tasks & Common
C_INCLUDES += -I../Custom/Tasks
C_INCLUDES += -I../Custom/Common/Inc
C_INCLUDES += -I../Custom/Common/Utils

# Custom - Libraries
C_INCLUDES += -I../Custom/Common/Lib
C_INCLUDES += -I../Custom/Common/Lib/atc
C_INCLUDES += -I../Custom/Common/Lib/cJSON
C_INCLUDES += -I../Custom/Common/Lib/littlefs
C_INCLUDES += -I../Custom/Common/Lib/littlefs/bd
C_INCLUDES += -I../Custom/Common/Lib/littlefs/runners
C_INCLUDES += -I../Custom/Common/Lib/lwip/include
C_INCLUDES += -I../Custom/Common/Lib/lwip/port
C_INCLUDES += -I../Custom/Common/Lib/MbedTLS/configs
C_INCLUDES += -I../Custom/Common/Lib/MbedTLS/include
C_INCLUDES += -I../Custom/Common/Lib/MbedTLS/library
C_INCLUDES += -I../Custom/Common/Lib/MbedTLS/port
C_INCLUDES += -I../Custom/Common/Lib/mongoose
C_INCLUDES += -I../Custom/Common/Lib/mpool
C_INCLUDES += -I../Custom/Common/Lib/nau881x
C_INCLUDES += -I../Custom/Common/Lib/nvs
C_INCLUDES += -I../Custom/Common/Lib/PahoMqtt
C_INCLUDES += -I../Custom/Common/Lib/pp
C_INCLUDES += -I../Custom/Common/Lib/si91x
C_INCLUDES += -I../Custom/Common/Lib/si91x/common/inc
C_INCLUDES += -I../Custom/Common/Lib/si91x/protocol/ble/inc
C_INCLUDES += -I../Custom/Common/Lib/si91x/protocol/wifi/inc
C_INCLUDES += -I../Custom/Common/Lib/si91x/service/inc
C_INCLUDES += -I../Custom/Common/Lib/si91x/silabs_utility/inc
C_INCLUDES += -I../Custom/Common/Lib/si91x/wireless/inc
C_INCLUDES += -I../Custom/Common/Lib/ms_bridging
C_INCLUDES += -I../Custom/Hal/Network/rtmp_push_client
C_INCLUDES += -I../Custom/Common/Lib/rtmpdump
C_INCLUDES += -I../Custom/Common/Lib/rtmpdump/librtmp

# Drivers - HAL & CMSIS
C_INCLUDES += -I../Drivers/STM32N6xx_HAL_Driver/Inc
C_INCLUDES += -I../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy
C_INCLUDES += -I../Drivers/CMSIS/Device/ST/STM32N6xx/Include
C_INCLUDES += -I../Drivers/CMSIS/Include
C_INCLUDES += -I../Drivers/CMSIS/DSP/Include

# BSP
C_INCLUDES += -I../BSP/STM32N6570-DK
C_INCLUDES += -I../BSP/Components/aps256xx
C_INCLUDES += -I../BSP/Components/mx66uw1g45g
C_INCLUDES += -I../BSP/Utilities/Fonts
C_INCLUDES += -I../BSP/Utilities/JPEG

# Middlewares - ST
C_INCLUDES += -I../Middlewares/ST/AI_Runtime/Inc
C_INCLUDES += -I../Middlewares/ST/AI_Runtime/Npu/ll_aton
C_INCLUDES += -I../Middlewares/ST/AI_Runtime/Npu/Devices/STM32N6XX
C_INCLUDES += -I../Middlewares/ST/STM32_Camera_Middleware
C_INCLUDES += -I../Middlewares/ST/STM32_Camera_Middleware/sensors
C_INCLUDES += -I../Middlewares/ST/STM32_Camera_Middleware/sensors/imx335
C_INCLUDES += -I../Middlewares/ST/STM32_Camera_Middleware/sensors/os04c10
C_INCLUDES += -I../Middlewares/ST/STM32_ISP_Library/evision/Inc
C_INCLUDES += -I../Middlewares/ST/STM32_ISP_Library/isp/Inc
C_INCLUDES += -I../Middlewares/ST/STM32_ISP_Library/isp/USB_Device/Inc
C_INCLUDES += -I../Middlewares/ST/cmsis_rtos_threadx
C_INCLUDES += -I../Middlewares/ST/filex/common/inc
C_INCLUDES += -I../Middlewares/ST/filex/ports/generic/inc
C_INCLUDES += -I../Middlewares/ST/lib_vision_models_pp/lib_vision_models_pp/Inc
C_INCLUDES += -I../Middlewares/ST/threadx/common/inc
C_INCLUDES += -I../Middlewares/ST/threadx/ports/cortex_M55/gnu/inc
C_INCLUDES += -I../Middlewares/ST/threadx/utility/execution_profile_kit
C_INCLUDES += -I../Middlewares/ST/threadx/utility/low_power
C_INCLUDES += -I../Middlewares/ST/threadx/utility/rtos_compatibility_layers/FreeRTOS
C_INCLUDES += -I../Middlewares/ST/usbx/common/core/inc
C_INCLUDES += -I../Middlewares/ST/usbx/common/usbx_device_classes/inc
C_INCLUDES += -I../Middlewares/ST/usbx/common/usbx_host_classes/inc
C_INCLUDES += -I../Middlewares/ST/usbx/common/usbx_host_controllers/inc
C_INCLUDES += -I../Middlewares/ST/usbx/common/usbx_network/inc
C_INCLUDES += -I../Middlewares/ST/usbx/common/usbx_stm32_device_controllers
C_INCLUDES += -I../Middlewares/ST/usbx/common/usbx_stm32_host_controllers
C_INCLUDES += -I../Middlewares/ST/usbx/ports/generic/inc
C_INCLUDES += -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc
C_INCLUDES += -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc
C_INCLUDES += -I../Middlewares/ST/uvcl
C_INCLUDES += -I../Middlewares/ST/uvcl/Inc
C_INCLUDES += -I../Middlewares/ST/uvcl/Inc/usbd
C_INCLUDES += -I../Middlewares/ST/uvcl/Inc/usbx
C_INCLUDES += -I../Middlewares/ST/uvcl/Src
C_INCLUDES += -I../Middlewares/ST/uvcl/Src/usbd
C_INCLUDES += -I../Middlewares/ST/uvcl/Src/usbx
C_INCLUDES += -I../Middlewares/ST/VideoEncoder_EWL
C_INCLUDES += -I../Middlewares/Third_Party/VideoEncoder/source/h264

# Middlewares - Third Party
C_INCLUDES += -I../Middlewares/Third_Party/CMSIS
C_INCLUDES += -I../Middlewares/Third_Party/CMSIS/RTOS2/Include
C_INCLUDES += -I../Middlewares/Third_Party/VideoEncoder/inc
C_INCLUDES += -I../Middlewares/Third_Party/VideoEncoder/source/common

# Secure
C_INCLUDES += -I../Secure_nsclib

######################################
# Compiler and Linker Flags
######################################
# C compiler flags (inherits COMMON_CFLAGS)
CFLAGS = $(COMMON_CFLAGS) $(C_DEFS) $(C_INCLUDES)
# Generate dependency information
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"
# CMSE support for App
CFLAGS += -mcmse

# Assembler flags (inherits COMMON_ASFLAGS)
ASFLAGS = $(COMMON_ASFLAGS) $(AS_DEFS) $(AS_INCLUDES)
# CMSE support for App
ASFLAGS += -mcmse

######################################
# Linker Configuration
######################################
# Linker script
LDSCRIPT = STM32N657L0HXQ_LRUN.ld

# Libraries (in correct order for linking)
LIBS = -ln6-evision-st-ae_gcc
LIBS += -ln6-evision-awb_gcc
LIBS += -lc -lm -lnosys
LIBS += -l:NetworkRuntime1020_CM55_GCC.a

LIBDIR =
LIBDIR += -L../Middlewares/ST/STM32_ISP_Library/evision/Lib
LIBDIR += -L../Middlewares/ST/AI_Runtime/Lib/GCC/ARMCortexM55

# Linker flags (inherits COMMON_LDFLAGS from root)
LDFLAGS = $(COMMON_LDFLAGS) -T$(LDSCRIPT)
LDFLAGS += $(LIBDIR)
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -static
# CMSE support - generate secure library
LDFLAGS += -mcmse
LDFLAGS += -Wl,--cmse-implib
LDFLAGS += -Wl,--out-implib=$(BUILD_DIR)/secure_nsclib.o
# Link libraries with start-group/end-group for circular dependencies
LDFLAGS += -Wl,--start-group $(LIBS) -Wl,--end-group

######################################
# Build Targets
######################################
.PHONY: all
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

######################################
# Build Rules
######################################
# Object files (filter out ../ from paths)
OBJECTS = $(addprefix $(BUILD_DIR)/, $(subst ../,,$(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/, $(subst ../,,$(ASM_SOURCES:.s=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/, $(subst ../,,$(ASM_SOURCES_S:.S=.o)))

# Pre-create all build directories (avoid race conditions in parallel builds)
.PHONY: build-dirs
build-dirs: $(BUILD_DIR)
	@mkdir -p $(sort $(dir $(OBJECTS)))

# Make all objects depend on directories
$(OBJECTS): | build-dirs

# Compile C source files
$(BUILD_DIR)/%.o: %.c Makefile
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: ../%.c Makefile
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

# Compile assembly source files (.S)
$(BUILD_DIR)/%.o: %.S Makefile
	@echo "AS $<"
	@$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: ../%.S Makefile
	@echo "AS $<"
	@$(AS) -c $(ASFLAGS) $< -o $@

# Compile assembly source files (.s)
$(BUILD_DIR)/%.o: %.s Makefile
	@echo "AS $<"
	@$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: ../%.s Makefile
	@echo "AS $<"
	@$(AS) -c $(ASFLAGS) $< -o $@

# Object list file
$(BUILD_DIR)/$(TARGET).list: $(OBJECTS)
	@echo "Generating object list..."
	@$(file > $@,$(OBJECTS))

# Link ELF file
$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile $(BUILD_DIR)/$(TARGET).list
	@echo "LD $@"
	@$(CC) @$(BUILD_DIR)/$(TARGET).list $(LDFLAGS) -o $@
	@$(SZ) $@

# Generate HEX file
$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "HEX $@"
	@$(HEX) $< $@

# Generate BIN file
$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "BIN $@"
	@$(BIN) $< $@

# Create build directory
$(BUILD_DIR):
	@mkdir -p $@

######################################
# Clean
######################################
.PHONY: clean
clean:
	@echo "Cleaning App build files..."
	@rm -rf $(BUILD_DIR)

######################################
# Dependencies
######################################
-include $(call rwildcard,$(BUILD_DIR),*.d)

######################################
# Debug Information
######################################
.PHONY: info
info:
	@echo "App Makefile Configuration:"
	@echo "  TARGET      = $(TARGET)"
	@echo "  BUILD_DIR   = $(BUILD_DIR)"
	@echo "  C_SOURCES   = $(words $(C_SOURCES)) files"
	@echo "  ASM_SOURCES = $(words $(ASM_SOURCES)) files"
	@echo "  ASM_SOURCES_S = $(words $(ASM_SOURCES_S)) files"
	@echo "  LDSCRIPT    = $(LDSCRIPT)"
	@echo "  ASM (.s)    = $(words $(ASM_SOURCES)) files"
	@echo "  ASM (.S)    = $(words $(ASM_SOURCES_S)) files"
