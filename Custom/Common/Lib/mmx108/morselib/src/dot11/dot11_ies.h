/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "dot11.h"


struct MM_PACKED dot11_ie_hdr
{

    uint8_t element_id;

    uint8_t length;
};


struct MM_PACKED dot11_ie_vs_hdr
{

    struct dot11_ie_hdr header;

    uint8_t oui[3];

    uint8_t type;
};


struct MM_PACKED dot11_ie_ssid
{

    struct dot11_ie_hdr header;

    uint8_t ssid[];
};


struct MM_PACKED dot11_ie_tim
{

    struct dot11_ie_hdr header;

    uint8_t dtim_count;

    uint8_t dtim_period;

    uint8_t bitmap_control;

    uint8_t partial_virtual_bitmap[];
};


struct MM_PACKED dot11_ie_tie
{

    struct dot11_ie_hdr header;

    uint8_t type;

    uint32_t interval;
};


struct MM_PACKED dot11_ie_mmie
{

    struct dot11_ie_hdr header;

    uint16_t key_id;

    uint8_t sequence_number[6];

    uint8_t mic[8];
};


struct MM_PACKED dot11_ie_aid_request
{

    struct dot11_ie_hdr header;

    uint8_t aid_request_mode;
};


struct MM_PACKED dot11_ie_aid_response
{

    struct dot11_ie_hdr header;

    uint16_t aid;

    uint8_t aid_switch_count;

    uint16_t aid_response_interval;
};


struct MM_PACKED dot11_ie_short_bcn_int
{

    struct dot11_ie_hdr header;

    uint16_t short_beacon_int;
};


struct MM_PACKED dot11_ie_twt
{

    struct dot11_ie_hdr header;

    uint8_t control;

    uint16_t request_type;

    uint64_t twt;

    uint8_t min_twt_duration;

    uint16_t mantissa;

    uint8_t channel;
};


struct MM_PACKED dot11_ie_s1g_capabilities
{

    struct dot11_ie_hdr header;

    uint8_t s1g_capabilities_information[10];

    uint8_t supported_s1g_mcs_nss_set[5];

};


struct MM_PACKED dot11_ie_s1g_operation
{

    struct dot11_ie_hdr header;

    uint8_t channel_width;

    uint8_t operating_class;

    uint8_t primary_channel_number;

    uint8_t channel_center_freq;

    uint8_t basic_s1g_mcs_nss_set[2];
};


struct MM_PACKED dot11_ac_parameter_record
{

    uint8_t aci_aifsn;

    uint8_t ecw_minmax;

    uint16_t txop_limit;
};


struct MM_PACKED dot11_ie_ecsa
{

    struct dot11_ie_hdr header;

    uint8_t channel_switch_mode;

    uint8_t new_operating_class;

    uint8_t new_channel_number;

    uint8_t channel_switch_count;
};


struct MM_PACKED dot11_ie_wide_bw_chan_switch
{

    struct dot11_ie_hdr header;

    uint8_t new_channel_width;

    uint8_t new_channel_centre_frequency_seg0;

    uint8_t new_channel_centre_frequency_seg1;
};


struct MM_PACKED dot11_ie_channel_switch_wrapper
{

    struct dot11_ie_hdr header;

    uint8_t sub_elements[];
};


struct MM_PACKED dot11_ie_wmm_info
{

    struct dot11_ie_vs_hdr vs_header;

    uint8_t subtype;

    uint8_t version;

    uint8_t qos_info;
};


struct MM_PACKED dot11_ie_wmm_param
{

    struct dot11_ie_vs_hdr vs_header;

    uint8_t subtype;

    uint8_t version;

    uint8_t qos_info;

    uint8_t reserved;

    struct dot11_ac_parameter_record ac_parameter_records[DOT11_ACI_NUM_ACS];
};


struct MM_PACKED dot11_ie_morse_info
{

    struct dot11_ie_vs_hdr vs_header;

    uint8_t sw_major;

    uint8_t sw_minor;

    uint8_t sw_patch;

    uint8_t sw_reserved;

    uint32_t hw_ver;

    uint8_t cap0;

    uint8_t ops0;
};


struct MM_PACKED dot11_ie_s1g_beacon_compatibility
{

    struct dot11_ie_hdr header;

    uint16_t compat_info;

    uint16_t beacon_int;

    uint32_t tsf_completion;
};


