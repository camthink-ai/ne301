#
# Copyright 2022-2024 Morse Micro
#
# SPDX-License-Identifier: Apache-2.0
#

MMX108_ROOT_PATH = ../Custom/Common/Lib/mmx108
MMX108_HOSTAP_DIR = $(MMX108_ROOT_PATH)/hostap

C_SOURCES += $(wildcard $(MMX108_ROOT_PATH)/mm_shims/*.c)

# WPA crypto in Appli (always fresh); libmorse built with NE301_WPA_CRYPTO_IN_APP=y omits it.
NE301_WPA_CRYPTO_IN_APP ?= y
ifneq ($(NE301_WPA_CRYPTO_IN_APP),)
C_SOURCES += $(MMX108_HOSTAP_DIR)/crypto_mbedtls_dpp.c
C_INCLUDES += -I$(MMX108_HOSTAP_DIR)
C_INCLUDES += -I$(MMX108_HOSTAP_DIR)/src
C_INCLUDES += -I$(MMX108_HOSTAP_DIR)/src/common
C_INCLUDES += -I$(MMX108_HOSTAP_DIR)/src/crypto
C_INCLUDES += -I$(MMX108_HOSTAP_DIR)/src/utils
C_INCLUDES += -I$(MMX108_HOSTAP_DIR)/wpa_supplicant
CFLAGS += -DCONFIG_DPP=1 -DCONFIG_DPP2=1 -DCONFIG_DPP3=1 -DCONFIG_ECC=1
CFLAGS += -DCONFIG_SHA256=1 -DCONFIG_SHA384=1 -DCONFIG_SHA512=1 -DCONFIG_FIPS=1
CFLAGS += -DMM_IOT -DIEEE8021X_EAPOL=1 -DCONFIG_SME=1 -DCONFIG_SAE=1
CFLAGS += -DCONFIG_OWE=1 -DCONFIG_IEEE80211AH=1 -DCONFIG_OPENSSL_INTERNAL_AES_WRAP=1
CFLAGS += -DCONFIG_NO_RANDOM_POOL=1 -DOS_NO_C_LIB_DEFINES=1
CFLAGS += -Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable
CFLAGS += -Wno-unused-function -Wno-unused-but-set-variable
endif
C_INCLUDES += -I$(MMX108_ROOT_PATH)/../MbedTLS/port
# DPP push-button: libmorse references dpp_tcp_* even when TCP is disabled in Morse build.
C_SOURCES += $(wildcard $(MMX108_ROOT_PATH)/mmipal/lwip/*.c)
C_SOURCES += $(wildcard $(MMX108_ROOT_PATH)/mmpktmem/*.c)
C_SOURCES += $(wildcard $(MMX108_ROOT_PATH)/mmregdb/*.c)
C_SOURCES += $(wildcard $(MMX108_ROOT_PATH)/mmutils/*.c)

# halow_example.c logic moved to Custom/Hal/Network/netif_manager/mm_halow_netif.c

C_INCLUDES += -I$(MMX108_ROOT_PATH)
C_INCLUDES += -I$(MMX108_ROOT_PATH)/morselib/include
C_INCLUDES += -I$(MMX108_ROOT_PATH)/freertos_mmiot
C_INCLUDES += -I$(MMX108_ROOT_PATH)/mm_shims
C_INCLUDES += -I$(MMX108_ROOT_PATH)/mmipal
C_INCLUDES += -I$(MMX108_ROOT_PATH)/mmipal/lwip
C_INCLUDES += -I$(MMX108_ROOT_PATH)/mmpktmem
C_INCLUDES += -I$(MMX108_ROOT_PATH)/mmregdb
C_INCLUDES += -I$(MMX108_ROOT_PATH)/mmutils
C_INCLUDES += -I../Custom/Hal/Network/netif_manager

CFLAGS += -DHALT_ON_ASSERT

# NE301 SPI HaLow: disable 802.11 PS and long bus idle timeout (default 100 ms sleeps chip).
CFLAGS += -DMMWLAN_DEFAULT_DYNAMIC_PS_TIMEOUT_MS=3600000U
CFLAGS += -DMMWLAN_DEFAULT_PS_MODE=0

# HaLow SPI backend (pick at most one):
#   (default)     HAL SPI6 in Appli/Core/Src/spi.c + mmhal_wlan.c
#   MMHAL_WLAN_USE_SOFT_SPI — GPIO bit-bang fallback
# CFLAGS += -DMMHAL_WLAN_USE_SOFT_SPI

# DPP (mmwlan_dpp_*) needs wpa_supplicant built with CONFIG_DPP in libmorse.a.
# After changing hostap/crypto_mbedtls_dpp.c or mmhal RNG: cd mmx108 && make clean && make
# then rebuild Appli (stale libmorse.a keeps old entropy code without HAL RNG).
LDFLAGS += -L$(MMX108_ROOT_PATH) -lmorse
