/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "morse_ie.h"
#include "mmdrv.h"
#include "umac/umac.h"
#include "umac/interface/umac_interface.h"
#include "mmlog.h"


#define MORSE_IE_OUI_TYPE 0


static const uint8_t morse_ie_info_id[] = { 0x0C, 0xBF, 0x74, MORSE_IE_OUI_TYPE };

const struct dot11_ie_morse_info *ie_morse_info_find(const uint8_t *ies, size_t ies_len)
{
    const struct dot11_ie_morse_info *ie = (const struct dot11_ie_morse_info *)
        ie_vendor_specific_find(ies, ies_len, morse_ie_info_id, sizeof(morse_ie_info_id), NULL);
    if (ie == NULL)
    {
        return NULL;
    }

    if (ie->vs_header.header.length < (sizeof(*ie) - sizeof(ie->vs_header.header)))
    {
        MMLOG_INF("IE too short for vendor caps IE, %ubytes\n", ie->vs_header.header.length);
        return NULL;
    }

    MMLOG_VRB("Found vendor caps info element.\n");
    return ie;
}

void ie_morse_info_build(struct umac_data *umacd, struct consbuf *buf)
{
    struct dot11_ie_morse_info *ie =
        (struct dot11_ie_morse_info *)consbuf_reserve(buf, sizeof(*ie));
    if (ie != NULL)
    {
        ie->vs_header.header.element_id = DOT11_IE_VENDOR_SPECIFIC;
        ie->vs_header.header.length = sizeof(*ie) - sizeof(ie->vs_header.header);
        ie->vs_header.oui[0] = morse_ie_info_id[0];
        ie->vs_header.oui[1] = morse_ie_info_id[1];
        ie->vs_header.oui[2] = morse_ie_info_id[2];
        ie->vs_header.type = MORSE_IE_OUI_TYPE;
        ie->sw_major = 0;
        ie->sw_minor = 0;
        ie->sw_patch = 0;
        ie->hw_ver = 0;

        struct mmdrv_fw_version version;
        enum mmwlan_status status = umac_interface_get_fw_version(umacd, &version);
        if (status == MMWLAN_SUCCESS)
        {
            ie->sw_major = version.major;
            ie->sw_minor = version.minor;
            ie->sw_patch = version.patch;
        }
        ie->sw_reserved = 0;

        ie->hw_ver = htole32(umac_interface_get_chip_id(umacd));


        ie->cap0 = MORSE_VENDOR_IE_CAP0_SHORT_ACK_TIMEOUT;


        const struct morse_caps *capabilities = umac_interface_get_capabilities(umacd);


        ie->cap0 |= MORSE_VENDOR_IE_CAP0_SET_MMSS_OFFSET(
            (capabilities->ampdu_mss > 0) ? capabilities->morse_mmss_offset : 0);

        ie->ops0 = 0;
    }
}
