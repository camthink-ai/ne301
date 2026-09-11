/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "aid_request.h"

void ie_aid_request_build(struct consbuf *buf)
{
    struct dot11_ie_aid_request *ie =
        (struct dot11_ie_aid_request *)consbuf_reserve(buf, sizeof(*ie));
    if (ie != NULL)
    {
        ie->header.element_id = DOT11_IE_AID_REQUEST;
        ie->header.length = sizeof(*ie) - sizeof(ie->header);


        ie->aid_request_mode = 0;
    }
}
