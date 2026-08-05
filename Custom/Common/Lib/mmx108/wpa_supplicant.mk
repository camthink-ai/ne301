#
# Copyright 2022-2024 Morse Micro
#
# SPDX-License-Identifier: Apache-2.0
#

SUPP_DIR ?= ./hostap


#
# Configuration options
#
# ************************************************************************
# *                             Warning                                  *
# ************************************************************************
# * Modifying the configuration below is not recommended. The MM IoT SDK *
# * WPA Supplicant driver has only been tested with the supplied         *
# * configuration. Modifying it may result in compilation failures or    *
# * undefined behaviour.                                                 *
# ************************************************************************
#
BUILD_DEFINES += IEEE8021X_EAPOL=1
BUILD_DEFINES += CONFIG_SME=1
BUILD_DEFINES += CONFIG_SAE=1
BUILD_DEFINES += CONFIG_OWE=1
BUILD_DEFINES += CONFIG_ECC=1
BUILD_DEFINES += CONFIG_SHA256=1
BUILD_DEFINES += CONFIG_SHA512=1
BUILD_DEFINES += CONFIG_SHA384=1
BUILD_DEFINES += CONFIG_FIPS=1
BUILD_DEFINES += CONFIG_NO_ACCOUNTING=1
BUILD_DEFINES += CONFIG_NO_BSS_TRANS_MGMT=1
BUILD_DEFINES += CONFIG_NO_CONFIG_BLOBS=1
BUILD_DEFINES += CONFIG_NO_RADIUS=1
BUILD_DEFINES += CONFIG_NO_RANDOM_POOL=1
BUILD_DEFINES += CONFIG_NO_RC4=1
BUILD_DEFINES += CONFIG_NO_ROBUST_AV=1
BUILD_DEFINES += CONFIG_NO_RRM=1
BUILD_DEFINES += CONFIG_NO_VLAN=1
BUILD_DEFINES += CONFIG_WNM=1
BUILD_DEFINES += CONFIG_IEEE80211AH=1
BUILD_DEFINES += CONFIG_NO_CONFIG_WRITE=1
BUILD_DEFINES += OS_NO_C_LIB_DEFINES=1
BUILD_DEFINES += CONFIG_BGSCAN=1
BUILD_DEFINES += CONFIG_BGSCAN_SIMPLE=1
BUILD_DEFINES += CONFIG_AUTOSCAN=1
BUILD_DEFINES += CONFIG_AUTOSCAN_EXPONENTIAL=1
BUILD_DEFINES += CONFIG_S1G_TWT=1
BUILD_DEFINES += MAX_NUM_MLD_LINKS=1
BUILD_DEFINES += MAX_NUM_MLO_LINKS=1
BUILD_DEFINES += WPA_SUPPLICANT_CLEANUP_INTERVAL=120
BUILD_DEFINES += MM_IOT

# We're using hostap/src/crypto/aes-unwrap.c
BUILD_DEFINES += CONFIG_OPENSSL_INTERNAL_AES_WRAP=1

# Build define so that we can get the dpp.h definitions when compiling morse_dpp_event()
BUILD_DEFINES += MM_IOT_DPP_HEADER=1

# AP Mode
ifneq ($(BUILD_SUPPLICANT_WITH_AP),)
ifeq ($(BUILD_SUPPLICANT_FROM_SOURCE),)
$(error BUILD_SUPPLICANT_FROM_SOURCE must be set if BUILD_SUPPLICANT_WITH_AP is set.)
endif

BUILD_DEFINES += CONFIG_AP=1
BUILD_DEFINES += NEED_AP_MLME=1
endif

ifneq ($(BUILD_SUPPLICANT_WITH_DPP),)
ifeq ($(BUILD_SUPPLICANT_FROM_SOURCE),)
$(error BUILD_SUPPLICANT_FROM_SOURCE must be set if BUILD_SUPPLICANT_WITH_DPP is set.)
endif


BUILD_DEFINES += CONFIG_OFFCHANNEL=1
BUILD_DEFINES += CONFIG_DPP=1
BUILD_DEFINES += CONFIG_DPP2=1
BUILD_DEFINES += CONFIG_DPP3=1
BUILD_DEFINES += CONFIG_JSON=1
BUILD_DEFINES += CONFIG_GAS_SERVER=1
BUILD_DEFINES += CONFIG_GAS=1

BUILD_DEFINES += MM_IOT_DPP_DISABLE_DOT1X=1
BUILD_DEFINES += MM_IOT_DPP_DISABLE_TCP=1
BUILD_DEFINES += MM_IOT_DPP_DISABLE_URI_HOST=1
BUILD_DEFINES += MM_IOT_DPP_DISABLE_PRIVATE_PEER_INTRO=1

BUILD_DEFINES += MM_IOT_DPP_EVENTS=1

SUPP_SRCS_C += src/common/dpp.c
SUPP_SRCS_C += src/common/dpp_auth.c
SUPP_SRCS_C += src/common/dpp_backup.c
SUPP_SRCS_C += src/common/dpp_crypto.c
SUPP_SRCS_C += src/common/dpp_pkex.c
SUPP_SRCS_C += src/common/dpp_reconfig.c
SUPP_SRCS_C += wpa_supplicant/dpp_supplicant.c
SUPP_SRCS_C += wpa_supplicant/offchannel.c
SUPP_SRCS_C += src/crypto/aes-siv.c
SUPP_SRCS_C += src/crypto/aes-ctr.c
SUPP_SRCS_C += src/utils/json.c
SUPP_SRCS_C += src/common/gas_server.c
SUPP_SRCS_C += src/common/gas.c
SUPP_SRCS_C += wpa_supplicant/gas_query.c
SUPP_SRCS_C += src/utils/base64.c
SUPP_SRCS_C += src/tls/asn1.c

CFLAGS-$(SUPP_DIR)/src/common/dpp.c += -Wno-overflow
endif

ifeq ($(WPA_SUPPLICANT_ENABLE_STDOUT_DEBUG),)
BUILD_DEFINES += CONFIG_NO_WPA_MSG=1
BUILD_DEFINES += CONFIG_NO_STDOUT_DEBUG=1
endif

SUPP_INCLUDES += .
SUPP_INCLUDES += src
SUPP_INCLUDES += src/common
SUPP_INCLUDES += src/utils
SUPP_INCLUDES += wpa_supplicant

ifneq ($(BUILD_SUPPLICANT_FROM_SOURCE),)
#
# Source files and paths
#

SUPP_SRCS_C += drivers_morse.c
SUPP_SRCS_C += os_mmosal.c
SUPP_SRCS_C += src/common/dragonfly.c
SUPP_SRCS_C += src/common/hw_features_common.c
SUPP_SRCS_C += src/common/ieee802_11_common.c
SUPP_SRCS_C += src/common/ptksa_cache.c
SUPP_SRCS_C += src/common/sae.c
SUPP_SRCS_C += src/common/wpa_common.c
SUPP_SRCS_C += src/crypto/aes-unwrap.c
SUPP_SRCS_C += src/crypto/dh_groups.c
SUPP_SRCS_C += src/crypto/sha256-kdf.c
SUPP_SRCS_C += src/crypto/sha384-kdf.c
SUPP_SRCS_C += src/crypto/tls_none.c
SUPP_SRCS_C += src/crypto/sha512-kdf.c
SUPP_SRCS_C += src/drivers/driver_common.c
SUPP_SRCS_C += src/eap_common/eap_common.c
SUPP_SRCS_C += src/eap_peer/eap.c
SUPP_SRCS_C += src/eap_peer/eap_methods.c
SUPP_SRCS_C += src/eapol_supp/eapol_supp_sm.c
SUPP_SRCS_C += src/rsn_supp/preauth.c
SUPP_SRCS_C += src/rsn_supp/pmksa_cache.c
SUPP_SRCS_C += src/rsn_supp/wpa.c
SUPP_SRCS_C += src/rsn_supp/wpa_ie.c
SUPP_SRCS_C += src/utils/crc32.c
SUPP_SRCS_C += src/utils/bitfield.c
SUPP_SRCS_C += src/utils/common.c
SUPP_SRCS_C += src/utils/wpa_debug.c
SUPP_SRCS_C += src/utils/wpabuf.c
SUPP_SRCS_C += wpa_supplicant/bgscan.c
SUPP_SRCS_C += wpa_supplicant/bgscan_simple.c
SUPP_SRCS_C += wpa_supplicant/autoscan.c
SUPP_SRCS_C += wpa_supplicant/autoscan_exponential.c
SUPP_SRCS_C += wpa_supplicant/bss.c
SUPP_SRCS_C += wpa_supplicant/config.c
SUPP_SRCS_C += wpa_supplicant/eap_register.c
SUPP_SRCS_C += wpa_supplicant/events.c
SUPP_SRCS_C += wpa_supplicant/notify.c
SUPP_SRCS_C += wpa_supplicant/op_classes.c
SUPP_SRCS_C += wpa_supplicant/scan.c
SUPP_SRCS_C += wpa_supplicant/sme.c
SUPP_SRCS_C += wpa_supplicant/wmm_ac.c
SUPP_SRCS_C += wpa_supplicant/wpa_supplicant.c
SUPP_SRCS_C += wpa_supplicant/wpas_glue.c
SUPP_SRCS_C += wpa_supplicant/bssid_ignore.c
SUPP_SRCS_C += wpa_supplicant/wnm_sta.c

# AP Mode
ifneq ($(BUILD_SUPPLICANT_WITH_AP),)
SUPP_SRCS_C += morse_stubs.c
SUPP_SRCS_C += wpa_supplicant/ap.c
SUPP_SRCS_C += src/ap/authsrv.c
SUPP_SRCS_C += src/ap/bss_load.c
SUPP_SRCS_C += src/ap/wmm.c
SUPP_SRCS_C += src/ap/ap_list.c
SUPP_SRCS_C += src/ap/comeback_token.c
SUPP_SRCS_C += src/pasn/pasn_responder.c
SUPP_SRCS_C += src/ap/hw_features.c
SUPP_SRCS_C += src/ap/dfs.c
SUPP_SRCS_C += src/ap/tkip_countermeasures.c
SUPP_SRCS_C += src/ap/hostapd.c
SUPP_SRCS_C += src/ap/ieee802_1x.c
SUPP_SRCS_C += src/ap/ieee802_11_auth.c
SUPP_SRCS_C += src/ap/ieee802_11_s1g.c
SUPP_SRCS_C += src/ap/sta_info.c
SUPP_SRCS_C += src/ap/ap_config.c
SUPP_SRCS_C += src/ap/ap_drv_ops.c
SUPP_SRCS_C += src/ap/ap_mlme.c
SUPP_SRCS_C += src/ap/beacon.c
SUPP_SRCS_C += src/ap/drv_callbacks.c
SUPP_SRCS_C += src/ap/eap_user_db.c
SUPP_SRCS_C += src/ap/ieee802_11.c
SUPP_SRCS_C += src/ap/ieee802_11_ht.c
SUPP_SRCS_C += src/ap/ieee802_11_shared.c
SUPP_SRCS_C += src/ap/neighbor_db.c
SUPP_SRCS_C += src/ap/pmksa_cache_auth.c
SUPP_SRCS_C += src/ap/rrm.c
SUPP_SRCS_C += src/ap/utils.c
SUPP_SRCS_C += src/ap/wpa_auth.c
SUPP_SRCS_C += src/ap/wpa_auth_glue.c
SUPP_SRCS_C += src/ap/wpa_auth_ie.c
SUPP_SRCS_C += src/eapol_auth/eapol_auth_sm.c
SUPP_SRCS_C += src/eap_server/eap_server.c
SUPP_SRCS_C += src/eap_server/eap_server_methods.c
endif

SUPP_INCLUDES += .
SUPP_INCLUDES += src
SUPP_INCLUDES += src/common
SUPP_INCLUDES += src/utils
SUPP_INCLUDES += wpa_supplicant
SUPP_INCLUDES += src/crypto

MMIOT_INCLUDES += $(addprefix $(SUPP_DIR)/,$(SUPP_INCLUDES))
MMIOT_SRCS_C += $(addprefix $(SUPP_DIR)/,$(SUPP_SRCS_C))

CFLAGS-$(SUPP_DIR) += -Wno-c++-compat
CFLAGS-$(SUPP_DIR) += -Wno-unused-but-set-variable
CFLAGS-$(SUPP_DIR) += -Wno-unused-function
CFLAGS-$(SUPP_DIR) += -Wno-unused-parameter
CFLAGS-$(SUPP_DIR) += -Wno-unused-variable
CFLAGS-$(SUPP_DIR) += -Wno-format
CFLAGS-$(SUPP_DIR) += -Wno-maybe-uninitialized
CFLAGS-$(SUPP_DIR) += -Wno-dangling-else
CFLAGS-$(SUPP_DIR) += -Wno-type-limits
CFLAGS-$(SUPP_DIR) += -Wno-sign-compare
CFLAGS-$(SUPP_DIR) += -Wno-parentheses
CFLAGS-$(SUPP_DIR) += -Wno-packed-not-aligned
CFLAGS-$(SUPP_DIR) += -Wno-misleading-indentation

endif

ifneq ($(NE301_WPA_CRYPTO_IN_APP),)
# crypto_mbedtls_*.c is compiled by NE301 Appli (mmx108.mk), not libmorse.a
else ifneq ($(BUILD_SUPPLICANT_CRYPTO_FROM_SOURCE)$(BUILD_SUPPLICANT_FROM_SOURCE),)
# mbedTLS crypto shim layer.
ifneq ($(BUILD_SUPPLICANT_WITH_DPP),)
MMIOT_SRCS_C += $(addprefix $(SUPP_DIR)/,crypto_mbedtls_dpp.c)
else
MMIOT_SRCS_C += $(addprefix $(SUPP_DIR)/,crypto_mbedtls_mm.c)
endif
endif
