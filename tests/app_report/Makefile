CC ?= cc
REPO_ROOT := ../..
APPHOST := $(REPO_ROOT)/Custom/Services/AppHost
GEN_DIR := gen
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

INC := -I$(APPHOST) \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs \
       -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I$(GEN_DIR)

DEFS := -D_GNU_SOURCE -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS

LIFECYCLE_CFLAGS := -DAPP_REPORT_DEV_PUMP_PERIOD_MS=20 \
                    -DAPP_REPORT_DEV_ACK_WAIT_MS=60 \
                    -DAPP_REPORT_DEV_ACK_POLL_MS=5 \
                    -DAPP_REPORT_DEV_STOP_JOIN_MS=300 \
                    -DAPP_REPORT_DEV_STOP_POLL_MS=5 \
                    -DAPP_REPORT_DEV_TRANSPORT_MAX_MS=20

STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -pthread $(INC) $(DEFS)

CORE_OBJS := app_report_service.o app_report_service_device.o app_report_policy.o

INSTALL_OBJS := app_installer.o app_state_store.o app_admission.o generic_math.o

LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
MBOBJS := $(addprefix build/,$(notdir $(LIB_SRCS:.c=.o)))

DEV_STUB_HDRS := $(GEN_DIR)/cmsis_os2.h \
                 $(GEN_DIR)/common_utils.h \
                 $(GEN_DIR)/stm32n6xx_hal.h \
                 $(GEN_DIR)/factory_test.h \
                 $(GEN_DIR)/netif_manager.h \
                 $(GEN_DIR)/mqtt_service.h \
                 $(GEN_DIR)/webhook_service.h \
                 $(GEN_DIR)/debug.h \
                 $(GEN_DIR)/storage.h

.PHONY: test clean
test: test_app_report test_app_report_policy
	./test_app_report
	./test_app_report_policy

test_app_report: test_app_report.o $(CORE_OBJS) $(INSTALL_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) $(LIFECYCLE_CFLAGS) -o $@ $^

test_app_report_policy: test_app_report_policy.o app_report_service.o app_report_policy.o $(INSTALL_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_app_report.o: test_app_report.c $(APPHOST)/app_report_service.h $(APPHOST)/app_report_service_device.h $(DEV_STUB_HDRS)
	$(CC) $(STRICT_CFLAGS) $(LIFECYCLE_CFLAGS) -c $< -o $@

test_app_report_policy.o: test_app_report_policy.c $(APPHOST)/app_report_service.h $(APPHOST)/app_report_policy.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_report_service.o: $(APPHOST)/app_report_service.c $(APPHOST)/app_report_service.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_report_service_device.o: $(APPHOST)/app_report_service_device.c $(APPHOST)/app_report_service_device.h $(APPHOST)/app_report_service.h $(DEV_STUB_HDRS)
	$(CC) $(STRICT_CFLAGS) $(LIFECYCLE_CFLAGS) -c $< -o $@

app_report_policy.o: $(APPHOST)/app_report_policy.c $(APPHOST)/app_report_policy.h $(APPHOST)/app_report_service.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_installer.o: $(APPHOST)/app_installer.c $(APPHOST)/app_installer.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_admission.o: $(APPHOST)/app_admission.c $(APPHOST)/app_admission.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_state_store.o: $(APPHOST)/app_state_store.c $(APPHOST)/app_state_store.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

generic_math.o: $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

build:
	mkdir -p build

build/%.o: $(MBEDTLS)/library/%.c | build
	$(CC) $(STRICT_CFLAGS) -w -c $< -o $@

$(DEV_STUB_HDRS): gen-stamp
	@test -f $@

gen-stamp: Makefile
	@mkdir -p $(GEN_DIR)
	printf '%s\n' \
	  '#ifndef GEN_CMSIS_OS2_H' \
	  '#define GEN_CMSIS_OS2_H' \
	  '#include <stdint.h>' \
	  'typedef enum { osOK = 0, osError = -1, osErrorTimeout = -2, osErrorResource = -3, osErrorParameter = -4, osErrorNoMemory = -5, osErrorISR = -6 } osStatus_t;' \
	  'typedef enum { osThreadInactive = 0, osThreadReady = 1, osThreadRunning = 2, osThreadBlocked = 3, osThreadTerminated = 4, osThreadError = 5 } osThreadState_t;' \
	  'typedef enum { osPriorityNone = 0, osPriorityNormal = 24 } osPriority_t;' \
	  '#define osWaitForever 0xFFFFFFFFu' \
	  '#define osMutexRecursive 0x1u' \
	  'typedef void *osThreadId_t;' \
	  'typedef void *osMutexId_t;' \
	  'typedef struct { const char *name; uint32_t attr_bits; void *cb_mem; uint32_t cb_size; } osMutexAttr_t;' \
	  'typedef struct { const char *name; uint32_t attr_bits; void *cb_mem; uint32_t cb_size; osPriority_t priority; uint32_t stack_size; void *stack_mem; } osThreadAttr_t;' \
	  'osMutexId_t osMutexNew(const osMutexAttr_t *attr);' \
	  'osStatus_t osMutexAcquire(osMutexId_t id, uint32_t timeout);' \
	  'osStatus_t osMutexRelease(osMutexId_t id);' \
	  'osThreadId_t osThreadNew(void (*func)(void *), void *arg, const osThreadAttr_t *attr);' \
	  'osStatus_t osThreadTerminate(osThreadId_t id);' \
	  'osThreadState_t osThreadGetState(osThreadId_t id);' \
	  'uint32_t osKernelGetTickCount(void);' \
	  'osStatus_t osDelay(uint32_t ticks);' \
	  '#endif' > $(GEN_DIR)/cmsis_os2.h
	@printf '%s\n' \
	  '#ifndef GEN_COMMON_UTILS_H' \
	  '#define GEN_COMMON_UTILS_H' \
	  '#define ALIGN_32 __attribute__((aligned(32)))' \
	  '#define IN_PSRAM' \
	  '#endif' > $(GEN_DIR)/common_utils.h
	printf '%s\n' \
	  '#ifndef GEN_STM32N6XX_HAL_H' \
	  '#define GEN_STM32N6XX_HAL_H' \
	  '#include <stdint.h>' \
	  'uint32_t HAL_GetUIDw0(void);' \
	  'uint32_t HAL_GetUIDw1(void);' \
	  'uint32_t HAL_GetUIDw2(void);' \
	  '#endif' > $(GEN_DIR)/stm32n6xx_hal.h
	printf '%s\n' \
	  '#ifndef GEN_FACTORY_TEST_H' \
	  '#define GEN_FACTORY_TEST_H' \
	  '#include <stdint.h>' \
	  'int factory_mac_get_burned(uint8_t *mac);' \
	  '#endif' > $(GEN_DIR)/factory_test.h
	printf '%s\n' \
	  '#ifndef GEN_NETIF_MANAGER_H' \
	  '#define GEN_NETIF_MANAGER_H' \
	  '#include <stdint.h>' \
	  '#include "aicam_types.h"' \
	  '#define NETIF_NAME_WIFI_AP "ap"' \
	  '#define NETIF_MAC_STR_FMT "%02x:%02x:%02x:%02x:%02x:%02x"' \
	  '#define NETIF_MAC_PARAMETER(mac) mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]' \
	  'typedef struct { uint8_t if_mac[6]; } netif_info_t;' \
	  'aicam_result_t nm_get_netif_info(const char *if_name, netif_info_t *netif_info);' \
	  '#endif' > $(GEN_DIR)/netif_manager.h
	printf '%s\n' \
	  '#ifndef GEN_MQTT_SERVICE_H' \
	  '#define GEN_MQTT_SERVICE_H' \
	  '#include <stdint.h>' \
	  '#include "aicam_types.h"' \
	  'typedef struct { char data_report_topic[128]; int data_report_qos; } mqtt_service_topic_config_t;' \
	  'aicam_result_t mqtt_service_get_topic_config(mqtt_service_topic_config_t *config);' \
	  'aicam_bool_t mqtt_service_is_running(void);' \
	  'int mqtt_service_publish(const char *topic, const uint8_t *payload, int payload_len, int qos, int retain);' \
	  'aicam_result_t mqtt_service_get_acked_msg_id(int *out_msg_id, uint32_t timeout_ms);' \
	  '#endif' > $(GEN_DIR)/mqtt_service.h
	printf '%s\n' \
	  '#ifndef GEN_WEBHOOK_SERVICE_H' \
	  '#define GEN_WEBHOOK_SERVICE_H' \
	  '#include <stdint.h>' \
	  '#include "aicam_types.h"' \
	  'typedef struct { aicam_bool_t enable; char url[256]; } webhook_config_t;' \
	  'aicam_result_t json_config_get_webhook_config(webhook_config_t *config);' \
	  'aicam_result_t webhook_service_push_json(const uint8_t *json, uint32_t json_len);' \
	  '#endif' > $(GEN_DIR)/webhook_service.h
	printf '%s\n' \
	  '#ifndef GEN_DEBUG_H' \
	  '#define GEN_DEBUG_H' \
	  '#include <stdio.h>' \
	  '#include <stdint.h>' \
	  '#define LOG_SVC_ERROR(fmt, ...) printf("[svc-err] " fmt "\n", ##__VA_ARGS__)' \
	  '#define LOG_SVC_WARN(fmt, ...) printf("[svc-warn] " fmt "\n", ##__VA_ARGS__)' \
	  '#define LOG_SVC_INFO(fmt, ...) printf("[svc-info] " fmt "\n", ##__VA_ARGS__)' \
	  '#define LOG_SIMPLE(fmt, ...) printf(fmt, ##__VA_ARGS__)' \
	  'typedef struct { const char *name; const char *help; int (*fn)(int argc, char *argv[]); } debug_cmd_reg_t;' \
	  'void debug_register_commands(const debug_cmd_reg_t *table, uint32_t count);' \
	  '#endif' > $(GEN_DIR)/debug.h
	printf '%s\n' \
	  '#ifndef GEN_STORAGE_H' \
	  '#define GEN_STORAGE_H' \
	  '#include <stddef.h>' \
	  '#include <stdint.h>' \
	  'typedef enum { NVS_FACTORY = 0, NVS_USER = 1 } NVS_Type_t;' \
	  'int storage_nvs_read(NVS_Type_t type, const char *key, void *data, size_t len);' \
	  'int storage_nvs_write(NVS_Type_t type, const char *key, const void *data, size_t len);' \
	  '#endif' > $(GEN_DIR)/storage.h
	@touch $@

clean:
	rm -rf build test_app_report test_app_report_policy *.o $(GEN_DIR) gen-stamp
