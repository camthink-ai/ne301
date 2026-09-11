
MMIOT_ROOT=.
CORE := arm-cortex-m55
BUILD_MORSELIB_FROM_SOURCE=y
BUILD_SUPPLICANT_FROM_SOURCE=y
BUILD_SUPPLICANT_WITH_DPP=y
# WPA crypto (entropy_poll) built in Appli — see NE301_WPA_CRYPTO_IN_APP in mmx108.mk
NE301_WPA_CRYPTO_IN_APP=y
# BUILD_SUPPLICANT_CRYPTO_FROM_SOURCE=y

WPA_SUPPLICANT_ENABLE_STDOUT_DEBUG=1
BUILD_DEFINES += MMLOG_LEVEL_DEFAULT=MMLOG_LEVEL_ERR

include $(MMIOT_ROOT)/core-$(CORE).mk
include $(MMIOT_ROOT)/morselib.mk
include $(MMIOT_ROOT)/wpa_supplicant.mk
# include $(MMIOT_ROOT)/mbedtls.mk

MMIOT_SRCS_C += $(MORSELIB_SRCS_C)

MMIOT_INCLUDES += $(MMIOT_ROOT)
MMIOT_INCLUDES += $(MMIOT_ROOT)/mm_shims
MMIOT_INCLUDES += $(MMIOT_ROOT)/mmutils

OBJS += $(patsubst %.c, %.o, $(MMIOT_SRCS_C))

CFLAGS += $(addprefix -D,$(BUILD_DEFINES))
CFLAGS += $(addprefix -I,$(MMIOT_INCLUDES))
CFLAGS += -g -Wall

#Used the SDK's MbedTLS
CFLAGS += -I../MbedTLS/include

ARFLAGS = rv

TARGET = libmorse.a
OBJS_RSP = objs.rsp

# Windows CMD has ~8191 char limit; pass member list via @response file.
$(TARGET): $(OBJS)
	$(file >$(OBJS_RSP),$(OBJS))
	$(AR) $(ARFLAGS) $(TARGET) @$(OBJS_RSP)


%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

PHONY: clean
clean:
	@echo "Remove linked and compiled files......"
	rm -rf $(OBJS) $(TARGET) $(OBJS_RSP)