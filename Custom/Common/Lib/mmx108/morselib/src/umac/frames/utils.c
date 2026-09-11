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
