/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "s1g_tim.h"
#include "mmlog.h"
#include "umac/ap/traffic_bitmap.h"

enum
{

    AID_PAGE_INDEX_SHIFT = 11,
    AID_BLOCK_OFFSET_SHIFT = 6,
    AID_BLOCK_MODE_M_SHIFT = 3,
    AID_BLOCK_MODE_Q_SHIFT = 0,
    AID_PAGE_INDEX_MASK = 0x03ul << AID_PAGE_INDEX_SHIFT,
    AID_BLOCK_OFFSET_MASK = 0x1ful << AID_BLOCK_OFFSET_SHIFT,
    AID_BLOCK_MODE_M_MASK = 0x07ul << AID_BLOCK_MODE_M_SHIFT,
    AID_BLOCK_MODE_Q_MASK = 0x07ul << AID_BLOCK_MODE_Q_SHIFT,


    AID_SINGLE_MODE_AID_SHIFT = 0,
    AID_SINGLE_MODE_AID_MASK = 0x3ful << AID_SINGLE_MODE_AID_SHIFT,


    AID_OLB_MODE_M_MOD_8_SHIFT = 3,
    AID_OLB_MODE_Q_SHIFT = 0,
    AID_OLB_MODE_M_MOD_8_MASK = 0x07ul << AID_OLB_MODE_M_MOD_8_SHIFT,
    AID_OLB_MODE_Q_MASK = 0x07ul << AID_OLB_MODE_Q_SHIFT,
};

const struct dot11_ie_tim *ie_s1g_tim_find(const uint8_t *ies, size_t ies_len)
{
    const struct dot11_ie_tim *tim =
        (const struct dot11_ie_tim *)ie_find(ies, ies_len, DOT11_IE_TIM, NULL);
    if ((tim != NULL) && (tim->header.length >= (sizeof(*tim) - sizeof(tim->header))))
    {
        return tim;
    }

    MMLOG_DBG("TIM ie too short\n");
    return NULL;
}


struct tim_parse_state
{

    uint32_t page_index;
    uint32_t page_slice;
    uint32_t block_encoding_mode;
    bool inverse_bitmap;
    uint32_t block_offset;


    const uint8_t *block;

    int data_length_remaining;
};


bool ie_s1g_tim_block_bitmap_has_aid(struct tim_parse_state *state, uint16_t aid)
{
    uint8_t block_bitmap = state->block[0];
    unsigned subblock_count = __builtin_popcount(block_bitmap);
    uint32_t pos_m;
    uint32_t pos_q;
    const uint8_t *subblock = state->block + 1;
    bool result;

    state->block += (1 + subblock_count);
    state->data_length_remaining -= (1 + subblock_count);
    if (state->data_length_remaining < 0)
    {

        MMLOG_DBG("TIM IE too short\n");
        return false;
    }

    if ((uint32_t)((aid & AID_PAGE_INDEX_MASK) >> AID_PAGE_INDEX_SHIFT) != state->page_index)
    {
        return false;
    }

    if ((uint32_t)((aid & AID_BLOCK_OFFSET_MASK) >> AID_BLOCK_OFFSET_SHIFT) != state->block_offset)
    {
        return false;
    }

    pos_m = ((aid & AID_BLOCK_MODE_M_MASK) >> AID_BLOCK_MODE_M_SHIFT);

    if (!(block_bitmap & (1 << pos_m)))
    {

        return state->inverse_bitmap;
    }

    while (pos_m != 0)
    {
        if (block_bitmap & 1)
        {
            subblock++;
        }
        block_bitmap >>= 1;
        pos_m--;
    }

    pos_q = (aid & AID_BLOCK_MODE_Q_MASK) >> AID_BLOCK_MODE_Q_SHIFT;

    block_bitmap = *subblock;

    result = (block_bitmap & (1 << pos_q));
    if (state->inverse_bitmap)
    {
        result = !result;
    }

    return result;
}


bool ie_s1g_tim_single_aid_has_aid(struct tim_parse_state *state, uint16_t aid)
{
    uint8_t single_aid = state->block[0];
    bool result;

    state->block++;
    state->data_length_remaining--;

    if (state->data_length_remaining < 0)
    {
        return false;
    }

    if ((uint32_t)((aid & AID_BLOCK_OFFSET_MASK) >> AID_BLOCK_OFFSET_SHIFT) != state->block_offset)
    {
        return false;
    }

    if ((uint32_t)((aid & AID_PAGE_INDEX_MASK) >> AID_PAGE_INDEX_SHIFT) != state->page_index)
    {
        return false;
    }

    result = (((aid & AID_SINGLE_MODE_AID_MASK) >> AID_SINGLE_MODE_AID_SHIFT) == single_aid);

    if (state->inverse_bitmap)
    {
        result = !result;
    }

    return result;
}


bool ie_s1g_tim_olb_has_aid(struct tim_parse_state *state, uint16_t aid)
{
    int length = state->block[0];
    int tmp;
    int pos_m;
    int pos_q;
    int num_blocks;
    unsigned aid_block_num;
    const uint8_t *subblocks = state->block + 1;

    state->data_length_remaining -= (1 + length);
    state->block += (1 + length);

    if (state->data_length_remaining < 0)
    {

        MMLOG_DBG("TIM IE too short\n");
        return false;
    }

    if ((uint32_t)((aid & AID_PAGE_INDEX_MASK) >> AID_PAGE_INDEX_SHIFT) != state->page_index)
    {
        return false;
    }


    num_blocks = FAST_ROUND_UP(length * 8, DOT11_TIM_BLOCK_SIZE) / DOT11_TIM_BLOCK_SIZE;
    aid_block_num = ((aid & AID_BLOCK_OFFSET_MASK) >> AID_BLOCK_OFFSET_SHIFT);
    if (aid_block_num < state->block_offset || aid_block_num >= state->block_offset + num_blocks)
    {
        return false;
    }


    tmp = ((aid & AID_BLOCK_OFFSET_MASK) >> AID_BLOCK_OFFSET_SHIFT);
    pos_m = (tmp - state->block_offset) * 8;
    MMLOG_VRB("[%02x] %d -> %d + %d = %d\n",
              aid,
              tmp,
              pos_m,
              ((aid & AID_OLB_MODE_M_MOD_8_MASK) >> AID_OLB_MODE_M_MOD_8_SHIFT),
              pos_m + ((aid & AID_OLB_MODE_M_MOD_8_MASK) >> AID_OLB_MODE_M_MOD_8_SHIFT));
    tmp = ((aid & AID_OLB_MODE_M_MOD_8_MASK) >> AID_OLB_MODE_M_MOD_8_SHIFT);
    pos_m += tmp;

    if (pos_m >= length)
    {

        return state->inverse_bitmap;
    }

    pos_q = (aid & AID_OLB_MODE_Q_MASK) >> AID_OLB_MODE_Q_SHIFT;
    if (!(subblocks[pos_m] & (1 << pos_q)))
    {

        return state->inverse_bitmap;
    }

    MMLOG_VRB("aid=%u, bo=%lu pos_m=%d pos_q=%d subblock=%02x\n",
              aid,
              state->block_offset,
              pos_m,
              pos_q,
              subblocks[pos_m]);


    return !state->inverse_bitmap;
}


bool ie_s1g_tim_ade_has_aid(struct tim_parse_state *state, uint16_t aid)
{
    int word_length;
    int length;
    int offset;
    const uint8_t *delta_aids = state->block + 1;
    uint16_t current_aid = state->page_index * 2048 + state->block_offset * 64;
    uint8_t delta_aid_mask;

    word_length = dot11_tim_ade_get_ewl(state->block[0]) + 1;
    delta_aid_mask = (1 << word_length) - 1;
    length = dot11_tim_ade_get_length(state->block[0]);

    state->block += (1 + length);
    state->data_length_remaining -= (1 + length);

    if (state->data_length_remaining < 0)
    {

        MMLOG_DBG("TIM IE too short\n");
        return false;
    }

    if (length != 0)
    {

        current_aid += (*delta_aids & delta_aid_mask);
        MMLOG_VRB("First AID %u (%d %02x %u %lu %lu)\n",
                  current_aid,
                  word_length,
                  delta_aid_mask,
                  *delta_aids & delta_aid_mask,
                  state->page_index,
                  state->block_offset);
        if (current_aid == aid)
        {

            return !state->inverse_bitmap;
        }

        for (offset = word_length; (offset + word_length) <= (length * 8); offset += word_length)
        {
            int octet = offset / 8;
            int shift = offset & 0x7;
            int bits_remaining = word_length + shift - 8;

            uint8_t delta_aid = (delta_aids[octet] >> shift);

            if (bits_remaining > 0)
            {
                delta_aid |= (delta_aids[octet] << (8 - shift));
            }

            delta_aid &= delta_aid_mask;
            if (delta_aid == 0)
            {
                break;
            }

            current_aid += delta_aid;
            MMLOG_VRB("Delta aid=%u, current_aid=%u\n", delta_aid, current_aid);

            if (current_aid == aid)
            {

                return !state->inverse_bitmap;
            }
        }
    }


    if (state->inverse_bitmap)
    {
        uint16_t start_aid = state->page_index * 2048 + state->block_offset * 64;
        uint16_t end_aid = FAST_ROUND_UP(current_aid + 1, DOT11_TIM_BLOCK_SIZE);
        if (aid >= start_aid && aid < end_aid)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

bool ie_s1g_tim_has_aid(const struct dot11_ie_tim *s1g_tim, uint16_t aid)
{
    int block_num = 0;
    int block_info_len = s1g_tim->header.length - (sizeof(*s1g_tim) - sizeof(s1g_tim->header));
    struct tim_parse_state state = {
        .block = s1g_tim->partial_virtual_bitmap,
        .data_length_remaining = block_info_len,
    };


    if (block_info_len <= 0)
    {
        MMLOG_VRB("No block info (IE len = %u)\n", s1g_tim->header.length);
        return false;
    }

    state.page_index = (s1g_tim->bitmap_control & DOT11_MASK_TIM_BITMAP_CTRL_PAGE_INDEX) >>
                       DOT11_SHIFT_TIM_BITMAP_CTRL_PAGE_INDEX;
    state.page_slice = (s1g_tim->bitmap_control & DOT11_MASK_TIM_BITMAP_CTRL_PAGE_SLICE_NUM) >>
                       DOT11_SHIFT_TIM_BITMAP_CTRL_PAGE_SLICE_NUM;

    if ((state.page_slice > 0) && (state.page_slice != 31))
    {
        MMLOG_VRB("page slice %lu\n", state.page_slice);
        return false;
    }

    while (state.data_length_remaining > 1)
    {
        state.block_encoding_mode = dot11_tim_block_hdr_get_block_encoding(state.block[0]);
        state.inverse_bitmap = dot11_tim_block_hdr_get_inverse_bitmap(state.block[0]) != 0;
        state.block_offset = dot11_tim_block_hdr_get_block_offset(state.block[0]);
        state.block++;
        state.data_length_remaining--;

        switch (state.block_encoding_mode)
        {
            case DOT11_TIM_BLOCK_ENCODING_BLOCK_BITMAP:
                if (ie_s1g_tim_block_bitmap_has_aid(&state, aid))
                {
                    MMLOG_VRB("Matched %s @ block # %d\n", "block bitmap", block_num);
                    return true;
                }
                break;

            case DOT11_TIM_BLOCK_ENCODING_SINGLE_AID:
                if (ie_s1g_tim_single_aid_has_aid(&state, aid))
                {
                    MMLOG_VRB("Matched %s @ block # %d\n", "single AID", block_num);
                    return true;
                }
                break;

            case DOT11_TIM_BLOCK_ENCODING_OLB:
                if (ie_s1g_tim_olb_has_aid(&state, aid))
                {
                    MMLOG_VRB("Matched %s @ block # %d\n", "OLB", block_num);
                    return true;
                }
                break;

            case DOT11_TIM_BLOCK_ENCODING_ADE:
                if (ie_s1g_tim_ade_has_aid(&state, aid))
                {
                    MMLOG_VRB("Matched %s @ block # %d\n", "ADE", block_num);
                    return true;
                }
                break;

            default:

                MMOSAL_ASSERT(false);
                break;
        }
        block_num += 1;
    }

    return false;
}

void ie_s1g_tim_build(struct consbuf *buf,
                      uint8_t dtim_count,
                      uint8_t dtim_period,
                      bool traffic_indicator,
                      const uint8_t *traffic_bitmap)
{
    MMOSAL_ASSERT(traffic_bitmap);

    MMOSAL_DEV_ASSERT((traffic_bitmap[0] & 0x01) == 0);

    enum
    {
        MAX_SUBBLOCKS_IN_BLOCK = 8,
        MAX_ENCODED_BLOCKS =
            (S1G_BITMAP_SUBBLOCKS + MAX_SUBBLOCKS_IN_BLOCK - 1) / MAX_SUBBLOCKS_IN_BLOCK,
        BASE_BLOCK_LEN = 2,
        MAX_ENCODED_BLOCK_SIZE = (BASE_BLOCK_LEN + MAX_SUBBLOCKS_IN_BLOCK),
        MAX_SUPPORTED_PVB_LEN = MAX_ENCODED_BLOCKS * MAX_ENCODED_BLOCK_SIZE,
        MAX_PVB_LEN = 251,
    };

    MM_STATIC_ASSERT(MAX_SUPPORTED_PVB_LEN <= MAX_PVB_LEN, "Must not exceed spec limit");

    if (consbuf_reserve(buf, 0) == NULL)
    {

        consbuf_reserve(buf, sizeof(struct dot11_ie_tim) + MAX_PVB_LEN);
        return;
    }


    uint8_t pvb[MAX_SUPPORTED_PVB_LEN] = {};
    MM_STATIC_ASSERT(MAX_SUPPORTED_PVB_LEN == 10, "Review the TIM logic if changing this limit");

    size_t pvb_len = 0;


    for (size_t block = 0; block < MAX_ENCODED_BLOCKS; ++block)
    {
        uint8_t *block_control = &pvb[pvb_len];
        uint8_t *block_bitmap = &pvb[pvb_len + 1];
        size_t block_size = 0;
        for (size_t subblock = 0; subblock < MAX_SUBBLOCKS_IN_BLOCK; ++subblock)
        {
            size_t sub_block_idx = block * MAX_SUBBLOCKS_IN_BLOCK + subblock;
            if (sub_block_idx >= S1G_BITMAP_SUBBLOCKS)
            {
                break;
            }
            uint8_t sub_block = traffic_bitmap[sub_block_idx];
            if (sub_block == 0)
            {

                continue;
            }
            if (!block_size)
            {

                block_size += BASE_BLOCK_LEN;
            }

            *block_bitmap |= BIT(subblock);
            pvb[pvb_len + block_size] = sub_block;
            ++block_size;
        }
        if (block_size == 0)
        {

            continue;
        }

        DOT11_TIM_BLOCK_HDR_SET_BLOCK_ENCODING(*block_control,
                                               DOT11_TIM_BLOCK_ENCODING_BLOCK_BITMAP);
        DOT11_TIM_BLOCK_HDR_SET_BLOCK_OFFSET(*block_control, block);
        pvb_len += block_size;
    }
    MMOSAL_DEV_ASSERT(pvb_len <= MAX_SUPPORTED_PVB_LEN);
    MMOSAL_DEV_ASSERT(pvb_len <= S1G_TIM_MAX_BLOCK_SIZE - 5);

    size_t tim_len = 2;
    if (traffic_indicator || pvb_len > 0)
    {

        tim_len += 1 + pvb_len;
    }

    struct dot11_ie_tim *tim_ie =
        (struct dot11_ie_tim *)consbuf_reserve(buf, sizeof(tim_ie->header) + tim_len);
    MMOSAL_DEV_ASSERT(tim_ie != NULL);

    tim_ie->header.element_id = DOT11_IE_TIM;
    tim_ie->header.length = tim_len;
    tim_ie->dtim_count = dtim_count;
    tim_ie->dtim_period = dtim_period;
    if (tim_len == 2)
    {
        return;
    }
    uint8_t bitmap_control_le = 0;
    uint8_t page_index = 0;
    uint8_t page_slice = 0x1F;
    DOT11_TIM_BITMAP_CTRL_SET_TRAFFIC_INDICATOR(bitmap_control_le, traffic_indicator);
    DOT11_TIM_BITMAP_CTRL_SET_PAGE_SLICE_NUM(bitmap_control_le, page_slice);
    DOT11_TIM_BITMAP_CTRL_SET_PAGE_INDEX(bitmap_control_le, page_index);
    tim_ie->bitmap_control = bitmap_control_le;
    if (tim_len == 3)
    {
        return;
    }
    memcpy(tim_ie->partial_virtual_bitmap, pvb, pvb_len);
}


MM_STATIC_ASSERT(MAX_SUPPORTED_AID < 512, "Page index now requires handling");

