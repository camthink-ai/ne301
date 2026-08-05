/*
 * Utils: Frame library: Build Action frame
 *
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "action.h"
#include "dot11/dot11_utils.h"
#include "umac/data/umac_data.h"

bool frame_is_robust_action(struct mmpktview *view)
{
    const struct dot11_action *frame = (struct dot11_action *)mmpkt_get_data_start(view);

    if (!(dot11_frame_control_get_type(frame->hdr.frame_control) == DOT11_FC_TYPE_MGMT) ||
        !(dot11_frame_control_get_subtype(frame->hdr.frame_control) == DOT11_FC_SUBTYPE_ACTION))
    {
        return false;
    }

    switch (frame->field.category)
    {
        case DOT11_ACTION_CATEGORY_PUBLIC:
        case DOT11_ACTION_CATEGORY_HT:
        case DOT11_ACTION_CATEGORY_WNM_UNPROTECTED:
        case DOT11_ACTION_CATEGORY_SELF_PROTECTED:
        case DOT11_ACTION_CATEGORY_DMG_UNPROTECTED:
        case DOT11_ACTION_CATEGORY_VHT:
        case DOT11_ACTION_CATEGORY_S1G_UNPROTECTED:
        case DOT11_ACTION_CATEGORY_HE:
        case DOT11_ACTION_CATEGORY_VENDOR_SPECIFIC:
            return false;
            break;

        default:
            return true;
            break;
    }
}

void frame_action_build(struct umac_data *umacd, struct consbuf *buf, void *args)
{
    MM_UNUSED(umacd);
    const struct frame_data_action *data = (const struct frame_data_action *)args;

    struct dot11_hdr *header = (struct dot11_hdr *)consbuf_reserve(buf, sizeof(struct dot11_hdr));
    if (header != NULL)
    {
        dot11_build_pv0_mgmt_header(header,
                                    DOT11_FC_SUBTYPE_ACTION,
                                    0,
                                    data->dst_address,
                                    data->src_address,
                                    data->bssid);
    }

    consbuf_append(buf, data->action_field, data->action_field_len);
}
