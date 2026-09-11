/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "dot11.h"


struct MM_PACKED dot11_qos_ctrl
{

    uint16_t field;
};


struct MM_PACKED dot11_s1g_beacon_hdr
{

    uint16_t frame_control;

    uint16_t duration;

    uint8_t source_addr[DOT11_MAC_ADDR_LEN];

    uint32_t time_stamp;

    uint8_t change_sequence;
};


struct MM_PACKED dot11_hdr
{

    uint16_t frame_control;

    uint16_t duration;

    uint8_t addr1[DOT11_MAC_ADDR_LEN];

    uint8_t addr2[DOT11_MAC_ADDR_LEN];

    uint8_t addr3[DOT11_MAC_ADDR_LEN];

    uint16_t sequence_control;
};


struct MM_PACKED dot11_data_hdr
{

    struct dot11_hdr base;

    uint8_t addr4[DOT11_MAC_ADDR_LEN];
};


MM_STATIC_ASSERT(offsetof(struct dot11_data_hdr, base) == 0,
                 "Must be able to cast between a struct dot11_data_hdr * and a struct dot11_hdr *");


struct MM_PACKED dot11_assoc_req
{

    struct dot11_hdr hdr;

    uint16_t capability;

    uint16_t listen_interval;

    uint8_t ies[];
};


struct MM_PACKED dot11_assoc_rsp
{

    struct dot11_hdr hdr;

    uint16_t capability;

    uint16_t status_code;

    uint8_t ies[];
};


struct MM_PACKED dot11_reassoc_req
{

    struct dot11_hdr hdr;

    uint16_t capability;

    uint16_t listen_interval;

    uint8_t current_ap_addr[DOT11_MAC_ADDR_LEN];

    uint8_t ies[];
};


struct MM_PACKED dot11_probe_response
{

    struct dot11_hdr hdr;

    uint8_t timestamp[8];

    uint16_t beacon_interval;

    uint16_t capability_info;

    uint8_t ies[];
};


struct MM_PACKED dot11_auth_hdr
{

    struct dot11_hdr hdr;

    uint16_t auth_alg;
};


struct MM_PACKED dot11_auth_seq_status
{

    uint16_t seq;

    uint16_t status_code;
};


struct MM_PACKED dot11_action_field
{

    uint8_t category;

    uint8_t action_details[];
};


struct MM_PACKED dot11_action
{

    struct dot11_hdr hdr;

    struct dot11_action_field field;
};


struct MM_PACKED dot11_disassoc
{

    struct dot11_hdr hdr;

    uint16_t reason_code;
};


struct MM_PACKED dot11_deauth
{

    struct dot11_hdr hdr;

    uint16_t reason_code;
};


struct MM_PACKED dot11_action_field_addba_req
{

    uint8_t category;

    uint8_t ba_action;

    uint8_t dialog_token;

    uint16_t ba_param_set;

    uint16_t ba_timeout;

    uint16_t ba_ssc;
};


struct MM_PACKED dot11_action_field_addba_resp
{

    uint8_t category;

    uint8_t ba_action;

    uint8_t dialog_token;

    uint16_t status_code;

    uint16_t ba_param_set;

    uint16_t ba_timeout;
};


struct MM_PACKED dot11_action_field_delba
{

    uint8_t category;

    uint8_t ba_action;

    uint16_t delba_param_set;

    uint16_t reason_code;
};


