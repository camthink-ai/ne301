/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "frames_common.h"
#include "deauthentication.h"
#include "disassociation.h"
#include "action.h"

bool frame_is_robust_mgmt(struct mmpktview *view)
{
    const struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(view);

    if (frame_is_deauthentication(header) || frame_is_disassociation(header))
    {
        return true;
    }

    if (frame_is_robust_action(view))
    {
        return true;
    }
    return false;
}

bool mgmt_frame_is_bufferable(uint16_t frame_control_le)
{
    MMOSAL_DEV_ASSERT(dot11_frame_control_get_type(frame_control_le) == DOT11_FC_TYPE_MGMT);


    switch (dot11_frame_control_get_subtype(frame_control_le))
    {
        case DOT11_FC_SUBTYPE_ACTION:
        case DOT11_FC_SUBTYPE_DISASSOC:
        case DOT11_FC_SUBTYPE_DEAUTH:
            return true;

        default:
            return false;
    }
}
