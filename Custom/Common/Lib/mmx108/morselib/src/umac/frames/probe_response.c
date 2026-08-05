/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "probe_response.h"

#include "mmlog.h"
#include "dot11/dot11_frames.h"
#include "dot11/dot11_utils.h"
#include "umac/ies/ssid.h"
#include "umac/ies/s1g_capabilities.h"
#include "umac/ies/s1g_operation.h"
#include "umac/ies/short_bcn_ie.h"

bool frame_probe_response_parse(struct mmpktview *view, struct frame_data_probe_response *result)
{
    MMOSAL_ASSERT(view);

    uint32_t data_length = mmpkt_get_data_length(view);
    struct dot11_probe_response *frame = (struct dot11_probe_response *)mmpkt_get_data_start(view);
    if (data_length < sizeof(*frame))
    {
        MMLOG_DBG("Probe response too short\n");
        return false;
    }

    uint32_t ies_len = data_length - sizeof(*frame);
    if (ies_len > UINT16_MAX)
    {

        return false;
    }

    result->timestamp = frame->timestamp;
    result->destination_address = dot11_get_da(&frame->hdr);
    result->bssid = dot11_mgmt_get_bssid(&frame->hdr);
    result->capability_info = le16toh(frame->capability_info);
    result->ies = frame->ies;
    result->ies_len = ies_len;

    const struct dot11_ie_ssid *ssid_ie = ie_ssid_find(result->ies, result->ies_len);
    if (ssid_ie == NULL)
    {
        MMLOG_INF("Probe response missing SSID IE\n");
        return false;
    }

    result->ssid = ssid_ie->ssid;
    result->ssid_len = ssid_ie->header.length;


    const struct dot11_ie_short_bcn_int *short_bcn =
        ie_short_bcn_find(result->ies, result->ies_len);
    if (short_bcn == NULL)
    {
        result->beacon_interval = le16toh(frame->beacon_interval);
    }
    else
    {
        result->beacon_interval = le16toh(short_bcn->short_beacon_int);
    }


    const struct dot11_ie_s1g_capabilities *ie_s1g_caps =
        ie_s1g_capabilities_find(result->ies, result->ies_len);
    if (ie_s1g_caps == NULL)
    {
        MMLOG_INF("Probe response missing or malformed IE: %s\n", "S1G Capabilities");
        return false;
    }

    const struct dot11_ie_s1g_operation *ie_s1g_op =
        ie_s1g_operation_find(result->ies, result->ies_len);
    if (ie_s1g_op == NULL)
    {
        MMLOG_INF("Probe response missing or malformed IE: %s\n", "S1G Operation");
        return false;
    }

    return true;
}

void frame_probe_response_build(struct umac_data *umacd, struct consbuf *buf, void *args)
{
    MM_UNUSED(umacd);

    const struct frame_data_probe_response *data = (const struct frame_data_probe_response *)args;

    struct dot11_hdr *hdr = (struct dot11_hdr *)consbuf_reserve(buf, sizeof(*hdr));
    if (hdr)
    {
        dot11_build_pv0_mgmt_header(hdr,
                                    DOT11_FC_SUBTYPE_PROBE_RSP,
                                    0,
                                    data->destination_address,
                                    data->bssid,
                                    data->bssid);
    }

    const uint8_t zero_timestamp[8] = { 0 };
    const uint8_t *timestamp = data->timestamp ? data->timestamp : zero_timestamp;
    consbuf_append(buf, timestamp, sizeof(zero_timestamp));

    uint16_t beacon_interval = htole16(data->beacon_interval);
    consbuf_append(buf, (const uint8_t *)&beacon_interval, sizeof(beacon_interval));

    uint16_t capability_info = htole16(data->capability_info);
    consbuf_append(buf, (const uint8_t *)&capability_info, sizeof(capability_info));

    ie_ssid_build(buf, data->ssid, data->ssid_len);

    if (data->ies_len != 0)
    {
        MMOSAL_ASSERT(data->ies != NULL);
        consbuf_append(buf, data->ies, data->ies_len);
    }
}
