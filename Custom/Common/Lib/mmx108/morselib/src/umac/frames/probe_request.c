/*
 * Utils: Frame library: Build Probe Request frame
 *
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "probe_request.h"
#include "dot11/dot11.h"
#include "dot11/dot11_frames.h"
#include "umac/ies/ssid.h"
#include "mmwlan.h"
#include "dot11/dot11_utils.h"
#include "umac/ies/s1g_capabilities.h"
#include "umac/rc/umac_rc.h"

void frame_probe_request_build(struct umac_data *umacd, struct consbuf *buf, void *args)
{
    const struct frame_data_probe_request *data = (const struct frame_data_probe_request *)args;

    struct dot11_hdr *hdr = (struct dot11_hdr *)consbuf_reserve(buf, sizeof(*hdr));
    if (hdr)
    {
        dot11_build_pv0_mgmt_header(hdr,
                                    DOT11_FC_SUBTYPE_PROBE_REQ,
                                    0,
                                    mac_addr_broadcast,
                                    data->sta_address,
                                    mac_addr_broadcast);
    }

    ie_ssid_build(buf, data->ssid, data->ssid_len);

    ie_s1g_capabilities_build(umacd, buf);

    if (data->extra_ies_len && data->extra_ies != NULL)
    {
        consbuf_append(buf, data->extra_ies, data->extra_ies_len);
    }
}
