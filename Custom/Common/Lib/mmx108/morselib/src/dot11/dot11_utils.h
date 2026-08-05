/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include <endian.h>
#include <string.h>

#include "dot11.h"
#include "dot11_frames.h"
#include "common/common.h"
#include "common/mac_address.h"
#include "mmlog.h"


#define DOT11_VER_TYPE_SUBTYPE(_ver, _type, _subtype)   \
    ((_ver) |                                           \
     ((DOT11_FC_TYPE_##_type) << DOT11_SHIFT_FC_TYPE) | \
     ((DOT11_FC_SUBTYPE_##_subtype) << DOT11_SHIFT_FC_SUBTYPE))


static inline bool dot11_is_4addr_hdr(uint16_t fc_le)
{
    return (dot11_frame_control_get_type(fc_le) == DOT11_FC_TYPE_DATA) &&
           (dot11_frame_control_get_to_ds(fc_le) && dot11_frame_control_get_from_ds(fc_le));
}


static inline uint8_t dot11_data_hdr_get_len(const struct dot11_data_hdr *hdr)
{
    return dot11_is_4addr_hdr(hdr->base.frame_control) ? sizeof(*hdr) : sizeof(hdr->base);
}


static inline const uint8_t *dot11_mgmt_get_bssid(const struct dot11_hdr *hdr)
{
    return (hdr->addr3);
}


static inline const uint8_t *dot11_get_ra(const struct dot11_hdr *hdr)
{

    return (hdr->addr1);
}


static inline const uint8_t *dot11_get_ta(const struct dot11_hdr *hdr)
{

    return (hdr->addr2);
}


static inline const uint8_t *dot11_get_sa_data(const struct dot11_data_hdr *hdr)
{


    bool to_ds = dot11_frame_control_get_to_ds(hdr->base.frame_control);
    bool from_ds = dot11_frame_control_get_from_ds(hdr->base.frame_control);
    if (!to_ds && from_ds)
    {
        return (hdr->base.addr3);
    }
    else if (from_ds && to_ds)
    {
        return (hdr->addr4);
    }
    else
    {
        return (hdr->base.addr2);
    }
}


static inline const uint8_t *dot11_get_sa(const struct dot11_hdr *hdr)
{
    MMOSAL_DEV_ASSERT(!dot11_is_4addr_hdr(hdr->frame_control));
    return dot11_get_sa_data((const struct dot11_data_hdr *)(hdr));
}


static inline const uint8_t *dot11_get_da(const struct dot11_hdr *hdr)
{


    if (dot11_frame_control_get_to_ds(hdr->frame_control))
    {
        return (hdr->addr3);
    }
    else
    {
        return (hdr->addr1);
    }
}


void dot11_build_pv0_mgmt_header(struct dot11_hdr *hdr,
                                 uint16_t subtype,
                                 uint8_t to_ds,
                                 const uint8_t *dst_address,
                                 const uint8_t *src_address,
                                 const uint8_t *bssid);


static inline uint16_t dot11_get_next_sequence_control(const struct dot11_hdr *header)
{

    uint16_t sequence_control = le16toh(header->sequence_control);
    bool more_fragments = dot11_frame_control_get_more_fragments(header->frame_control);
    if (more_fragments)
    {
        sequence_control += 1;
    }
    else
    {
        sequence_control = sequence_control & DOT11_MASK_SC_SEQUENCE_NUMBER;
        sequence_control += (1 << DOT11_SHIFT_SC_SEQUENCE_NUMBER);
    }
    return sequence_control;
}


static inline bool dot11_sequence_control_lt(uint16_t a, uint16_t b)
{
    int16_t delta = (int16_t)(a - b);
    return delta < 0;
}


static inline uint32_t dot11_convert_tus_to_ms(uint16_t time_tus)
{
    return ((uint32_t)time_tus * 1024) / 1000;
}


