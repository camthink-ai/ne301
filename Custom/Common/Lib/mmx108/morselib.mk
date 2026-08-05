#
# Copyright 2022-2023 Morse Micro
#
# SPDX-License-Identifier: Apache-2.0
#

SRCS_H += $(wildcard $(MMIOT_DIR)/morselib/include/*.h)
MMIOT_INCLUDES += morselib/include

MORSELIB_SRC_DIR = morselib/src

ifneq ($(BUILD_MORSELIB_FROM_SOURCE),)
    # Build morselib from source code
    MMIOT_SRCS_C += $(MORSELIB_SRCS_C)
    MMIOT_SRCS_H += $(MORSELIB_SRCS_H)
    MMIOT_INCLUDES += $(MORSELIB_SRC_DIR)
    MMIOT_INCLUDES += $(MORSELIB_SRC_DIR)/internal
    MMIOT_INCLUDES += $(MORSELIB_SRC_DIR)/emmet
    MMIOT_INCLUDES += $(MORSELIB_SRC_DIR)/umac/rc/mmrc_osal
    MMIOT_INCLUDES += morselib/mmrc/src/core
    CFLAGS-$(MORSELIB_SRC_DIR) += -Wno-c++-compat
else
    # Use a prebuilt morselib
    ifneq ($(BUILD_SUPPLICANT_FROM_SOURCE),)
        MMIOT_LIBS += morselib/lib/$(CORE)/libmorse_nosupplicant.a
    else
        ifneq ($(BUILD_SUPPLICANT_CRYPTO_FROM_SOURCE),)
            MMIOT_LIBS += morselib/lib/$(CORE)/libmorse_nocrypto.a
        else
            MMIOT_LIBS += morselib/lib/$(CORE)/libmorse.a
        endif
    endif
endif

# For MMRC
BUILD_DEFINES += LOOKAROUND_FAIL_MAX=50

MORSELIB_SRCS_C += morselib/mmrc/src/core/mmrc.c 
MORSELIB_SRCS_C += morselib/src/common/mac_address.c 
MORSELIB_SRCS_C += morselib/src/common/mmpkt.c 
MORSELIB_SRCS_C += morselib/src/common/mmpkt_list.c 
MORSELIB_SRCS_C += morselib/src/dot11/dot11_utils.c 
MORSELIB_SRCS_C += morselib/src/umac/ap/umac_ap.c 
MORSELIB_SRCS_C += morselib/src/umac/ate/umac_ate.c 
MORSELIB_SRCS_C += morselib/src/umac/ba/umac_ba.c 
MORSELIB_SRCS_C += morselib/src/umac/connection/umac_connection.c 
MORSELIB_SRCS_C += morselib/src/umac/config/umac_config.c 
MORSELIB_SRCS_C += morselib/src/umac/core/umac_core.c 
MORSELIB_SRCS_C += morselib/src/umac/core/umac_evtloop.c 
MORSELIB_SRCS_C += morselib/src/umac/core/umac_evtq.c 
MORSELIB_SRCS_C += morselib/src/umac/core/umac_timeout.c 
MORSELIB_SRCS_C += morselib/src/umac/data/umac_data.c 
MORSELIB_SRCS_C += morselib/src/umac/data/umac_sta_data.c 
MORSELIB_SRCS_C += morselib/src/umac/datapath/datapath_defrag.c 
MORSELIB_SRCS_C += morselib/src/umac/datapath/umac_datapath.c 
MORSELIB_SRCS_C += morselib/src/umac/datapath/umac_datapath_ap.c 
MORSELIB_SRCS_C += morselib/src/umac/frames/action.c 
MORSELIB_SRCS_C += morselib/src/umac/frames/association.c 
MORSELIB_SRCS_C += morselib/src/umac/frames/authentication.c 
MORSELIB_SRCS_C += morselib/src/umac/frames/deauthentication.c 
MORSELIB_SRCS_C += morselib/src/umac/frames/disassociation.c 
MORSELIB_SRCS_C += morselib/src/umac/frames/frame_constructor.c 
MORSELIB_SRCS_C += morselib/src/umac/frames/probe_request.c 
MORSELIB_SRCS_C += morselib/src/umac/frames/probe_response.c 
MORSELIB_SRCS_C += morselib/src/umac/frames/utils.c 
MORSELIB_SRCS_C += morselib/src/umac/ies/aid_request.c 
MORSELIB_SRCS_C += morselib/src/umac/ies/twt_ie.c 
MORSELIB_SRCS_C += morselib/src/umac/ies/s1g_capabilities.c 
MORSELIB_SRCS_C += morselib/src/umac/ies/s1g_operation.c 
MORSELIB_SRCS_C += morselib/src/umac/ies/s1g_tim.c 
MORSELIB_SRCS_C += morselib/src/umac/ies/ies_common.c 
MORSELIB_SRCS_C += morselib/src/umac/ies/wmm.c 
MORSELIB_SRCS_C += morselib/src/umac/ies/morse_ie.c 
MORSELIB_SRCS_C += morselib/src/umac/interface/umac_interface.c 
MORSELIB_SRCS_C += morselib/src/umac/keys/connection_keys.c 
MORSELIB_SRCS_C += morselib/src/umac/keys/umac_keys.c 
MORSELIB_SRCS_C += morselib/src/umac/ps/umac_ps.c 
MORSELIB_SRCS_C += morselib/src/umac/rc/mmrc_osal/mmrc_osal.c 
MORSELIB_SRCS_C += morselib/src/umac/rc/umac_rc.c 
MORSELIB_SRCS_C += morselib/src/umac/regdb/umac_regdb.c 
MORSELIB_SRCS_C += morselib/src/umac/scan/umac_scan.c 
MORSELIB_SRCS_C += morselib/src/umac/scan/hw_scan.c 
MORSELIB_SRCS_C += morselib/src/umac/stats/umac_stats.c 
MORSELIB_SRCS_C += morselib/src/umac/supplicant_shim/bip.c 
MORSELIB_SRCS_C += morselib/src/umac/supplicant_shim/ccmp.c 
MORSELIB_SRCS_C += morselib/src/umac/supplicant_shim/config.c 
MORSELIB_SRCS_C += morselib/src/umac/supplicant_shim/driver.c 
MORSELIB_SRCS_C += morselib/src/umac/supplicant_shim/driver_ap.c 
MORSELIB_SRCS_C += morselib/src/umac/supplicant_shim/eloop.c 
MORSELIB_SRCS_C += morselib/src/umac/supplicant_shim/morse_dpp_event.c 
MORSELIB_SRCS_C += morselib/src/umac/supplicant_shim/random.c 
MORSELIB_SRCS_C += morselib/src/umac/supplicant_shim/supplicant_core.c 
MORSELIB_SRCS_C += morselib/src/umac/supplicant_shim/supplicant_core_ap.c 
MORSELIB_SRCS_C += morselib/src/umac/twt/umac_twt.c 
MORSELIB_SRCS_C += morselib/src/umac/umac.c 
MORSELIB_SRCS_C += morselib/src/umac/umac_mmdrv_shim.c 
MORSELIB_SRCS_C += morselib/src/umac/wnm_sleep/umac_wnm_sleep.c 
MORSELIB_SRCS_C += morselib/src/umac/offload/umac_offload.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/mm6108/mm6108.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/mm6108/pager_if_hw.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/mm6108/pager_if.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/mm6108/pageset.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/mm8108/mm8108.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/mm8108/yaps-hw.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/mm8108/yaps.c 
MORSELIB_SRCS_C += morselib/src/driver/beacon/beacon.c 
MORSELIB_SRCS_C += morselib/src/driver/driver.c 
MORSELIB_SRCS_C += morselib/src/driver/driver_task.c 
MORSELIB_SRCS_C += morselib/src/driver/health/driver_health.c 
MORSELIB_SRCS_C += morselib/src/driver/puff/puff.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_crc/morse_crc.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/command.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/event.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/ext_host_table.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/firmware.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/firmware_mbin.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/hw.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/ps.c 
MORSELIB_SRCS_C += morselib/src/driver/morse_driver/skbq.c 
MORSELIB_SRCS_C += morselib/src/driver/transport/sdio.c 
MORSELIB_SRCS_C += morselib/src/driver/transport/sdio_spi.c 
MORSELIB_SRCS_C += morselib/src/emmet/emmet_stubs.c
