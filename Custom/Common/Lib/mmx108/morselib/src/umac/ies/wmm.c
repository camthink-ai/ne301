/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "wmm.h"
#include "mmlog.h"


#define WMM_VERSION 1

#define WMM_INFO_ELEMENT_SUBTYPE 0

#define WMM_PARAM_ELEMENT_SUBTYPE 1

#define WMM_OUI_TYPE 2


static const uint8_t wmm_info_id[] = { 0x00, 0x50, 0xf2, WMM_OUI_TYPE, WMM_INFO_ELEMENT_SUBTYPE };

static const uint8_t wmm_param_id[] = { 0x00, 0x50, 0xf2, WMM_OUI_TYPE, WMM_PARAM_ELEMENT_SUBTYPE };

const struct dot11_ie_wmm_param *ie_wmm_param_find(const uint8_t *ies, size_t ies_len)
{
    const struct dot11_ie_wmm_param *ie = (const struct dot11_ie_wmm_param *)
        ie_vendor_specific_find(ies, ies_len, wmm_param_id, sizeof(wmm_param_id), NULL);
    if (ie == NULL)
    {
        return NULL;
    }

    if (ie->vs_header.header.length < (sizeof(*ie) - sizeof(ie->vs_header.header)))
    {
        MMLOG_INF("IE too short for WMM IE, %ubytes\n", ie->vs_header.header.length);
        return NULL;
    }

    MMLOG_VRB("Found WMM parameter element.\n");
    return ie;
}


void ie_wmm_info_build(struct consbuf *buf)
{
    struct dot11_ie_wmm_info *ie = (struct dot11_ie_wmm_info *)consbuf_reserve(buf, sizeof(*ie));
    if (ie != NULL)
    {
        ie->vs_header.header.element_id = DOT11_IE_VENDOR_SPECIFIC;
        ie->vs_header.header.length = sizeof(*ie) - sizeof(ie->vs_header.header);
        ie->vs_header.oui[0] = wmm_info_id[0];
        ie->vs_header.oui[1] = wmm_info_id[1];
        ie->vs_header.oui[2] = wmm_info_id[2];
        ie->vs_header.type = WMM_OUI_TYPE;

        ie->subtype = WMM_INFO_ELEMENT_SUBTYPE;
        ie->version = WMM_VERSION;
        ie->qos_info = 0;
    }
}
