CC ?= cc
REPO_ROOT := ../..
APPHOST := $(REPO_ROOT)/Custom/Services/AppHost
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS
DEV_STUB_DIR := $(REPO_ROOT)/tests/app_report/gen
BUILD_DIR := build-101

INC := -I$(APPHOST) \
       -I$(REPO_ROOT)/tests/app_sidecar_lifecycle \
       -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I$(DEV_STUB_DIR) \
       -I$(REPO_ROOT)/Custom/Hal \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(REPO_ROOT)/Custom/Common/Lib \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS \
        -DAPP_REPORT_DEV_ACK_WAIT_MS=2u -DAPP_REPORT_DEV_ACK_POLL_MS=1u \
        -DAPP_REPORT_DEV_STOP_JOIN_MS=50u -DAPP_REPORT_DEV_STOP_POLL_MS=1u \
        -DAPP_REPORT_DEV_TRANSPORT_MAX_MS=2u

CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -pthread -D_POSIX_C_SOURCE=200809L $(INC) $(DEFS)
LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
LIB_OBJS := $(addprefix $(BUILD_DIR)/mbedtls_,$(notdir $(LIB_SRCS:.c=.o)))

SRCS := test_app_report_uninstall.c \
        $(APPHOST)/app_report_service.c \
        $(APPHOST)/app_report_service_device.c \
        $(APPHOST)/app_report_policy.c \
        $(APPHOST)/app_installer.c \
        $(APPHOST)/app_state_store.c \
        $(APPHOST)/app_admission.c \
        $(APPHOST)/app_trust_anchor.c \
        $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(SRCS:.c=.o)))

.PHONY: test headers clean
test: headers $(BUILD_DIR)/test_app_report_uninstall
	$(BUILD_DIR)/test_app_report_uninstall

clean:
	rm -rf $(BUILD_DIR)

headers:
	$(MAKE) -C $(REPO_ROOT)/tests/app_report gen-stamp

$(BUILD_DIR)/test_app_report_uninstall: $(OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/test_app_report_uninstall.o: test_app_report_uninstall.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(APPHOST)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/app_installer.o: $(APPHOST)/app_sidecar_verify.c $(APPHOST)/app_sidecar_lifecycle.c

$(BUILD_DIR)/generic_math.o: $(REPO_ROOT)/Custom/Common/Utils/generic_math.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mbedtls_%.o: $(MBEDTLS)/library/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -w -c $< -o $@
