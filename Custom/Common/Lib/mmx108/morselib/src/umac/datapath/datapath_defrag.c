/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "mmlog.h"
#include "umac/core/umac_core.h"
#include "umac/datapath/datapath_defrag.h"
#include "dot11/dot11_utils.h"


#define DEFRAG_TIMEOUT_MS (1000)


#define FRAG_CHAIN_MIN_ALLOC_LENGTH (1600)

#define FRAG_CHAIN_MAX_ALLOC_LENGTH (DOT11_MAX_PAYLOAD_LEN + sizeof(struct dot11_data_hdr))


static struct datapath_defrag_data_chain *datapath_defrag_get_frag_chain(
    struct datapath_defrag_data *data,
    uint8_t tid_idx)
{
    if (tid_idx >= MAX_FRAG_CHAINS)
    {
        MMLOG_WRN("tid_idx (%d) exceeds cache limit (%d).\n", tid_idx, MAX_FRAG_CHAINS);
        return NULL;
    }

    return &data->frag_chains[tid_idx];
}


static bool datapath_defrag_is_in_frag_chain(struct datapath_defrag_data_chain *frag_chain,
                                             uint16_t sequence_control)
{
    return (frag_chain->sequence_number ==
            dot11_sequence_control_get_sequence_number(sequence_control));
}

void datapath_defrag_timeout(void *arg1, void *arg2)
{
    struct datapath_defrag_data_chain *frag_chain = (struct datapath_defrag_data_chain *)arg1;

    MM_UNUSED(arg2);

    MMOSAL_DEV_ASSERT(frag_chain->buf != NULL);
    MMLOG_INF("Frag chain timed out %p\n", frag_chain);


    mmpkt_release(frag_chain->buf);
    frag_chain->buf = NULL;
}


static void datapath_defrag_init_buffer(struct umac_data *umacd,
                                        struct datapath_defrag_data_chain *frag_chain)
{
    if (frag_chain->buf != NULL)
    {
        MMLOG_WRN("Dropping existing frag_chain_buffer as new chain has begun.\n");
        mmpkt_release(frag_chain->buf);
        frag_chain->buf = NULL;
    }

    (void)umac_core_cancel_timeout(umacd, datapath_defrag_timeout, frag_chain, NULL);

    frag_chain->buf =
        mmdrv_alloc_mmpkt_for_defrag(FRAG_CHAIN_MIN_ALLOC_LENGTH, FRAG_CHAIN_MAX_ALLOC_LENGTH);
    if (frag_chain->buf == NULL)
    {
        MMLOG_WRN("Failed to allocate a frag chain buffer\n");
        return;
    }
    mmpkt_adjust_start_offset(frag_chain->buf, sizeof(struct dot11_data_hdr));

    bool ok = umac_core_register_timeout(umacd,
                                         DEFRAG_TIMEOUT_MS,
                                         datapath_defrag_timeout,
                                         frag_chain,
                                         NULL);

    if (!ok)
    {

        MMLOG_WRN("Failed to register timeout for defrag\n");
    }
}


static bool datapath_defrag_add_fragment(struct datapath_defrag_data_chain *frag_chain,
                                         struct mmpktview *rxbufview)
{
    struct mmpktview *frag_chain_view = mmpkt_open(frag_chain->buf);

    if (mmpkt_available_space_at_end(frag_chain_view) < mmpkt_get_data_length(rxbufview))
    {
        MMLOG_WRN("Fragment buffer space exceeded.\n");
        mmpkt_close(&frag_chain_view);
        return false;
    }

    mmpkt_append_data(frag_chain_view,
                      mmpkt_get_data_start(rxbufview),
                      mmpkt_get_data_length(rxbufview));

    mmpkt_close(&frag_chain_view);
    return true;
}


static bool datapath_defrag_is_first_fragment(const struct dot11_hdr *header)
{
    return (dot11_sequence_control_get_fragment_number(header->sequence_control) == 0);
}

static bool datapath_defrag_is_fragment(const struct dot11_hdr *header)
{
    uint16_t fragment_number = dot11_sequence_control_get_fragment_number(header->sequence_control);
    return (fragment_number || dot11_frame_control_get_more_fragments(header->frame_control));
}

struct mmpkt *datapath_defrag(struct umac_data *umacd,
                              struct datapath_defrag_data *data,
                              const struct dot11_data_hdr **data_hdr,
                              struct mmpktview **rxbufview,
                              struct mmpkt *rxbuf,
                              uint8_t tid_idx)
{
    const size_t data_hdr_len = dot11_data_hdr_get_len(*data_hdr);
    const struct dot11_hdr *header = &(*data_hdr)->base;

    if (!datapath_defrag_is_fragment(header))
    {
        return rxbuf;
    }

    struct mmpkt *return_buffer = NULL;
    struct mmpktview *return_view = NULL;
    struct datapath_defrag_data_chain *frag_chain = datapath_defrag_get_frag_chain(data, tid_idx);
    if (frag_chain == NULL)
    {
        goto exit;
    }


    if (datapath_defrag_is_first_fragment(header))
    {
        datapath_defrag_init_buffer(umacd, frag_chain);
        frag_chain->sequence_number =
            dot11_sequence_control_get_sequence_number(header->sequence_control);
        frag_chain->is_protected = dot11_frame_control_get_protected(header->frame_control);
    }

    if (!datapath_defrag_is_in_frag_chain(frag_chain, header->sequence_control) ||
        frag_chain->buf == NULL)
    {
        MMLOG_INF("Missed the first fragment for this chain.\n");
        goto exit;
    }

    if (frag_chain->is_protected != dot11_frame_control_get_protected(header->frame_control))
    {
        MMLOG_WRN("Fragment does not match the current chain's protection status.\n");
        goto exit;
    }

    if (!datapath_defrag_add_fragment(frag_chain, *rxbufview))
    {

        mmpkt_release(frag_chain->buf);
        frag_chain->buf = NULL;

        (void)umac_core_cancel_timeout(umacd, datapath_defrag_timeout, frag_chain, NULL);
        goto exit;
    }

    if (dot11_frame_control_get_more_fragments(header->frame_control))
    {

        goto exit;
    }


    return_buffer = frag_chain->buf;
    frag_chain->buf = NULL;

    return_view = mmpkt_open(return_buffer);
    mmpkt_prepend_data(return_view, (const uint8_t *)(*data_hdr), data_hdr_len);
    *data_hdr = (const struct dot11_data_hdr *)mmpkt_remove_from_start(return_view, data_hdr_len);


    (void)umac_core_cancel_timeout(umacd, datapath_defrag_timeout, frag_chain, NULL);

exit:
    mmpkt_close(rxbufview);
    *rxbufview = return_view;
    mmpkt_release(rxbuf);
    return return_buffer;
}

void datapath_defrag_deinit(struct umac_data *umacd, struct datapath_defrag_data *data)
{
    int i = 0;
    for (i = 0; i < MAX_FRAG_CHAINS; i++)
    {
        struct datapath_defrag_data_chain *frag_chain = datapath_defrag_get_frag_chain(data, i);
        if (frag_chain->buf != NULL)
        {
            mmpkt_release(frag_chain->buf);
            frag_chain->buf = NULL;
        }
        (void)umac_core_cancel_timeout(umacd, datapath_defrag_timeout, frag_chain, NULL);
    }
}
