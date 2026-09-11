/*
 * Utils: Frame library: Build Deauthentication frame
 *
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "deauthentication.h"
#include "dot11/dot11.h"
#include "dot11/dot11_frames.h"
#include "dot11/dot11_utils.h"
#include "mmlog.h"

void frame_deauthentication_build(struct umac_data *umacd, struct consbuf *buf, void *args)
{
    const struct frame_data_deauth *data = (const struct frame_data_deauth *)args;
    struct dot11_deauth *frame = (struct dot11_deauth *)consbuf_reserve(buf, sizeof(*frame));

    MM_UNUSED(umacd);

    if (frame)
    {
        dot11_build_pv0_mgmt_header(&frame->hdr,
                                    DOT11_FC_SUBTYPE_DEAUTH,
                                    0,
                                    data->bssid,
                                    data->sta_address,
                                    data->bssid);
        frame->reason_code = htole16(data->reason_code);
    }
}

bool frame_is_deauthentication(const struct dot11_hdr *header)
{
    return ((dot11_frame_control_get_type(header->frame_control) == DOT11_FC_TYPE_MGMT) &&
            (dot11_frame_control_get_subtype(header->frame_control) == DOT11_FC_SUBTYPE_DEAUTH));
}
