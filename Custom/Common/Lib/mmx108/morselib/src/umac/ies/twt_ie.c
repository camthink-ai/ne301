/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "twt_ie.h"
#include "umac/connection/umac_connection.h"
#include "umac/twt/umac_twt.h"

void ie_twt_build(struct umac_data *umacd, struct consbuf *buf)
{

    const struct umac_twt_agreement_data *agreement = umac_twt_get_agreement(umacd, 0);
    const struct mmwlan_twt_config_args *twt_config = umac_twt_get_config(umacd);

    if ((twt_config->twt_mode != MMWLAN_TWT_REQUESTER) || (agreement == NULL))
    {
        return;
    }

    struct dot11_ie_twt *ie = (struct dot11_ie_twt *)consbuf_reserve(buf, sizeof(*ie));
    if (ie != NULL)
    {
        memset(ie, 0, sizeof(*ie));

        ie->header.element_id = DOT11_IE_TWT;
        ie->header.length = sizeof(*ie) - sizeof(ie->header);

        ie->control = agreement->control;
        ie->request_type = agreement->params.req_type;
        ie->twt = agreement->params.twt;
        ie->min_twt_duration = agreement->params.min_twt_dur;
        ie->mantissa = agreement->params.mantissa;
        ie->channel = agreement->params.channel;
    }
}
