/*
 * Utils: Frame library: Build S1G Action frame
 *
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "s1g_action.h"
#include "dot11/dot11_utils.h"
#include "umac/data/umac_data.h"

void frame_s1g_action_build(struct umac_data *umacd, struct consbuf *buf, void *args)
{
    MM_UNUSED(umacd);
    const struct frame_data_s1g_action *data = (const struct frame_data_s1g_action *)args;

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

    const uint8_t category = DOT11_ACTION_CATEGORY_S1G;
    consbuf_append(buf, &category, sizeof(category));
    consbuf_append(buf, &data->s1g_action, sizeof(data->s1g_action));

    if (data->payload_len && data->payload != NULL)
    {
        consbuf_append(buf, data->payload, data->payload_len);
    }
}

bool frame_s1g_action_parse(struct mmpktview *view, struct frame_data_s1g_action *result)
{
    MMOSAL_DEV_ASSERT(view);

    const uint8_t *data = mmpkt_get_data_start(view);
    uint32_t data_len = mmpkt_get_data_length(view);
    const uint32_t min_len = sizeof(struct dot11_action) + sizeof(uint8_t);
    if (data_len < min_len)
    {
        MMLOG_DBG("S1G Action Frame: Parse: Frame length too small\n");
        return false;
    }

    const struct dot11_action *frame = (const struct dot11_action *)data;
    uint16_t frame_ver_type_subtype =
        dot11_frame_control_get_ver_type_subtype(frame->hdr.frame_control);
    if (frame_ver_type_subtype != DOT11_VER_TYPE_SUBTYPE(0, MGMT, ACTION))
    {
        MMLOG_VRB("S1G Action Frame: not an action frame\n");
        return false;
    }

    if (frame->field.category != DOT11_ACTION_CATEGORY_S1G)
    {
        MMLOG_VRB("S1G Action Frame: Parse: Unexpected category %u\n", frame->field.category);
        return false;
    }

    result->bssid = dot11_mgmt_get_bssid(&frame->hdr);
    result->dst_address = dot11_get_ra(&frame->hdr);
    result->src_address = dot11_get_ta(&frame->hdr);

    const uint8_t *action_payload = frame->field.action_details;
    result->s1g_action = action_payload[0];
    result->payload_len = data_len - min_len;
    result->payload = (result->payload_len > 0) ? (action_payload + 1) : NULL;

    return true;
}
