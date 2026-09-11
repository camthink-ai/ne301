/*
 * UMAC Supplicant Shim private header
 *
 * Copyright 2022-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include "umac_supp_shim.h"

#include "umac_supp_shim_data.h"

#include "dot11/dot11_utils.h"
#include "hostap_morse_common.h"
#include "common/common.h"
#include "mmlog.h"
#include "umac/config/umac_config.h"
#include "umac/connection/umac_connection.h"
#include "umac/twt/umac_twt.h"
#include "mmwlan.h"


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wc++-compat"
#pragma GCC diagnostic ignored "-Wcast-qual"
#include "hostap/src/utils/includes.h"
#include "hostap/src/utils/common.h"
#include "hostap/src/utils/base64.h"
#include "hostap/src/utils/eloop.h"
#include "hostap/src/utils/os.h"
#include "hostap/src/common/eapol_common.h"
#include "hostap/src/crypto/aes_wrap.h"
#include "hostap/src/crypto/crypto.h"
#include "hostap/src/drivers/driver.h"
#include "hostap/src/l2_packet/l2_packet.h"
#include "hostap/wpa_supplicant/config.h"
#include "hostap/wpa_supplicant/config_ssid.h"
#include "hostap/wpa_supplicant/scan.h"
#include "hostap/wpa_supplicant/wpa_supplicant_i.h"
#include "hostap/wpa_supplicant/bss.h"
#include "hostap/wpa_supplicant/dpp_supplicant.h"
#pragma GCC diagnostic pop

#ifdef ENABLE_SUP_TRACE
#include "mmtrace.h"
extern mmtrace_channel sup_channel_handle;
#define SUP_TRACE_DECLARE    mmtrace_channel sup_channel_handle;
#define SUP_TRACE_INIT()     sup_channel_handle = mmtrace_register_channel("sup")
#define SUP_TRACE(_fmt, ...) mmtrace_printf(sup_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define SUP_TRACE_DECLARE
#define SUP_TRACE_INIT() \
    do {                 \
    } while (0)
#define SUP_TRACE(_fmt, ...) \
    do {                     \
    } while (0)
#endif


#define UMAC_SUPP_AP_CONFIG_NAME "AP"


#define UMAC_SUPP_STA_DRIVER_NAME ""


#define UMAC_SUPP_AP_DRIVER_NAME "AP"


enum mmwlan_status umac_supp_start_supp(struct umac_data *umacd);


void umac_supp_event(void *ctx, enum wpa_event_type event, union wpa_event_data *data);
