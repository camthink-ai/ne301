#
# Copyright 2022-2024 Morse Micro
#
# SPDX-License-Identifier: Apache-2.0
#

MMX108_COMMON_PATH = ../Custom/Common/Lib/mmx108

# === HaLow SDK tree selection ============================================
#   mmx108_2_13_1 (default) — official mm-iot-sdk 2.13.1 port (active line)
#   mmx108                  — vendored mm-iot-sdk 2.10.4 + local patches (rollback)
# Override on the command line:  make HALOW_SDK=mmx108 app
# (Same layout as SiliconLabs_SDK_4_1_1 active / SiliconLabs_SDK rollback.)
# Shared between trees: mm_shims, freertos_mmiot, MbedTLS. Versioned per SDK
# tree (sources + headers + firmware): morselib, hostap, mmipal, mmpktmem,
# mmregdb, mmutils, and the paired mmhal_wlan_binaries.c.
# Appli/Makefile stamps BUILD_DIR with the selected config and auto-cleans it
# on change, so both trees share the single "build" output directory.
HALOW_SDK ?= mmx108_2_13_1
ifeq ($(HALOW_SDK),mmx108)
MMX108_ROOT_PATH = $(MMX108_COMMON_PATH)
C_SOURCES += $(wildcard $(MMX108_COMMON_PATH)/mm_shims/*.c)
else
MMX108_ROOT_PATH = ../Custom/Common/Lib/$(HALOW_SDK)
# Shared shim minus its firmware file; the selected tree embeds its own.
C_SOURCES += $(filter-out %mmhal_wlan_binaries.c,$(wildcard $(MMX108_COMMON_PATH)/mm_shims/*.c))
C_SOURCES += $(wildcard $(MMX108_ROOT_PATH)/mmhal_wlan_binaries.c)
endif
# hostap is versioned per SDK tree: the 2.13.1 supplicant shim needs the
# 2.13.1 hostap headers (driver ops / DPP event union changed upstream).
MMX108_HOSTAP_DIR = $(MMX108_ROOT_PATH)/hostap

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
C_INCLUDES += -I$(MMX108_COMMON_PATH)/freertos_mmiot
C_INCLUDES += -I$(MMX108_COMMON_PATH)/mm_shims
# These five dirs are versioned per SDK tree (sources above come from
# $(MMX108_ROOT_PATH)), so their headers must resolve there too — a stale
# common-tree -I would shadow the selected SDK's headers.
C_INCLUDES += -I$(MMX108_ROOT_PATH)/mmipal
C_INCLUDES += -I$(MMX108_ROOT_PATH)/mmipal/lwip
C_INCLUDES += -I$(MMX108_ROOT_PATH)/mmpktmem
C_INCLUDES += -I$(MMX108_ROOT_PATH)/mmregdb
C_INCLUDES += -I$(MMX108_ROOT_PATH)/mmutils
C_INCLUDES += -I../Custom/Hal/Network/netif_manager

# No -DHALT_ON_ASSERT: that branch of mmosal_impl_assert() disables IRQs then
# executes a raw bkpt — without a debugger attached it escalates to HardFault
# (lockup) and silently hangs shipping units. The default branch prints and
# resets, which is the production-safe behavior.

# === HaLow target chip — toggle to rebuild for the other silicon ===
#   mm8108 (default) — current NE301 silicon, matches the tested working tree
#   mm6108           — legacy
# Override on the command line:  make HALOW_CHIP=mm6108
HALOW_CHIP ?= mm8108
ifeq ($(HALOW_CHIP),mm8108)
CFLAGS += -DHALOW_CHIP_MM8108
endif

# NE301 SPI HaLow: disable 802.11 PS and long bus idle timeout (default 100 ms sleeps chip).
# CFLAGS += -DMMWLAN_DEFAULT_DYNAMIC_PS_TIMEOUT_MS=3600000U
# CFLAGS += -DMMWLAN_DEFAULT_PS_MODE=0

# HaLow SPI backend (pick at most one):
#   (default)     HAL SPI6 in Appli/Core/Src/spi.c + mmhal_wlan.c
#   MMHAL_WLAN_USE_SOFT_SPI — GPIO bit-bang fallback
# CFLAGS += -DMMHAL_WLAN_USE_SOFT_SPI

# DPP (mmwlan_dpp_*) needs wpa_supplicant built with CONFIG_DPP in libmorse.a.
# After changing hostap/crypto_mbedtls_dpp.c or mmhal RNG: cd mmx108 && make clean && make
# then rebuild Appli (stale libmorse.a keeps old entropy code without HAL RNG).
LDFLAGS += -L$(MMX108_ROOT_PATH) -lmorse
