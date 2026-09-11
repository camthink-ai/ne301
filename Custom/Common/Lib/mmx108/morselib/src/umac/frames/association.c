/*
 * Utils: Frame library: Build (Re)Association request
 *
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "association.h"
#include "dot11/dot11.h"
#include "dot11/dot11_frames.h"
#include "dot11/dot11_utils.h"
#include "umac/config/umac_config.h"
#include "umac/ies/aid_request.h"
#include "umac/ies/aid_response.h"
#include "umac/ies/twt_ie.h"
#include "umac/ies/s1g_capabilities.h"
#include "umac/ies/ssid.h"
#include "umac/ies/wmm.h"
#include "umac/ies/morse_ie.h"
#include "umac/rc/umac_rc.h"
#include "mmwlan.h"
#include "mmlog.h"

void frame_association_request_build(struct umac_data *umacd, struct consbuf *buf, void *args)
{
    const struct frame_data_assoc_req *data = (const struct frame_data_assoc_req *)args;


    bool is_reassoc = false;
    if (data->prev_bssid)
    {
        is_reassoc = mm_mac_addr_is_equal(data->prev_bssid, data->bssid);
    }
    uint16_t subtype = is_reassoc ? DOT11_FC_SUBTYPE_REASSOC_REQ : DOT11_FC_SUBTYPE_ASSOC_REQ;


    struct dot11_assoc_req *frame = (struct dot11_assoc_req *)consbuf_reserve(
        buf,
        (is_reassoc) ? sizeof(struct dot11_reassoc_req) : sizeof(struct dot11_assoc_req));
    if (frame != NULL)
    {
        dot11_build_pv0_mgmt_header(&(frame->hdr),
                                    subtype,
                                    0,
                                    data->bssid,
                                    data->sta_address,
                                    data->bssid);


        frame->capability = htole16(UMAC_SUPPORTED_CAPABILITY_MASK);
        frame->listen_interval = umac_config_get_listen_interval(umacd);

        if (is_reassoc)
        {

            struct dot11_reassoc_req *reassoc_frame = (struct dot11_reassoc_req *)frame;
            memcpy(reassoc_frame->current_ap_addr,
                   data->bssid,
                   sizeof(reassoc_frame->current_ap_addr));
        }
    }



    ie_ssid_build(buf, data->ssid, data->ssid_len);
    ie_aid_request_build(buf);

    ie_twt_build(umacd, buf);

    ie_s1g_capabilities_build(umacd, buf);

    ie_wmm_info_build(buf);

    ie_morse_info_build(umacd, buf);


    consbuf_append(buf, data->wpa_ie, data->wpa_ie_len);


    if (data->extra_assoc_ies_len)
    {
        consbuf_append(buf, data->extra_assoc_ies, data->extra_assoc_ies_len);
    }
}

bool frame_association_response_parse(struct mmpktview *view, struct frame_data_assoc_rsp *result)
{
    MMOSAL_ASSERT(view != NULL);


    const struct dot11_assoc_rsp *frame =
        (struct dot11_assoc_rsp *)mmpkt_remove_from_start(view, sizeof(*frame));

    if (frame == NULL)
    {
        MMLOG_INF("Assoc Response Frame: Parse: Frame length too small %lu\n",
                  mmpkt_get_data_length(view));
        return false;
    }


    result->is_reassoc_rsp = dot11_frame_control_get_subtype(frame->hdr.frame_control) ==
                             DOT11_FC_SUBTYPE_REASSOC_RSP;
    result->bssid = frame->hdr.addr3;


    result->capability = le16toh(frame->capability);
    result->status_code = le16toh(frame->status_code);

    result->ies_len = mmpkt_get_data_length(view);
    result->ies = mmpkt_get_data_start(view);

    return true;
}
