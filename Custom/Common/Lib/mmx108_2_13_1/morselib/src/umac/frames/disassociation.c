/*
 * Utils: Frame library: Build disassociation frame
 *
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "disassociation.h"
#include "dot11/dot11.h"
#include "dot11/dot11_frames.h"
#include "dot11/dot11_utils.h"
#include "mmlog.h"

bool frame_is_disassociation(const struct dot11_hdr *header)
{
    return ((dot11_frame_control_get_type(header->frame_control) == DOT11_FC_TYPE_MGMT) &&
            (dot11_frame_control_get_subtype(header->frame_control) == DOT11_FC_SUBTYPE_DISASSOC));
}
