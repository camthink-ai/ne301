/*
 * Utils: Frame library: Build Authentication frame
 *
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "authentication.h"
#include "dot11/dot11.h"
#include "dot11/dot11_frames.h"
#include "dot11/dot11_utils.h"
#include "mmlog.h"

void frame_authentication_build(struct umac_data *umacd, struct consbuf *buf, void *args)
{
    const struct frame_data_auth *data = (const struct frame_data_auth *)args;
    struct dot11_auth_hdr *frame = (struct dot11_auth_hdr *)consbuf_reserve(buf, sizeof(*frame));

    MM_UNUSED(umacd);

    if (frame)
    {
        dot11_build_pv0_mgmt_header(&frame->hdr,
                                    DOT11_FC_SUBTYPE_AUTH,
                                    0,
                                    data->bssid,
                                    data->sta_address,
                                    data->bssid);
        frame->auth_alg = htole16(data->auth_alg);
    }

    if (data->auth_alg == DOT11_AUTH_ALG_OPEN)
    {
        struct dot11_auth_seq_status *open_auth_fields =
            (struct dot11_auth_seq_status *)consbuf_reserve(buf, sizeof(*open_auth_fields));
        if (open_auth_fields)
        {
            open_auth_fields->seq = htole16(1);
            open_auth_fields->status_code = htole16(DOT11_STATUS_SUCCESS);
        }
    }
    else
    {
        consbuf_append(buf, data->auth_data, data->auth_data_len);
    }
}

bool frame_authentication_parse(struct mmpktview *view, struct frame_data_auth *result)
{
    MMOSAL_ASSERT(view);

    struct dot11_auth_hdr *auth_hdr =
        (struct dot11_auth_hdr *)mmpkt_remove_from_start(view, sizeof(*auth_hdr));
    struct dot11_auth_seq_status *seq_status =
        (struct dot11_auth_seq_status *)mmpkt_remove_from_start(view, sizeof(*seq_status));

    if (auth_hdr == NULL || seq_status == NULL)
    {
        MMLOG_DBG("Auth Frame: Parse: Frame length too small\n");
        return false;
    }

    result->sta_address = dot11_get_sa(&auth_hdr->hdr);
    result->bssid = dot11_mgmt_get_bssid(&auth_hdr->hdr);
    result->auth_alg = le16toh(auth_hdr->auth_alg);
    result->seq = le16toh(seq_status->seq);
    result->status_code = le16toh(seq_status->status_code);
    result->auth_data_len = mmpkt_get_data_length(view);
    result->auth_data = mmpkt_get_data_start(view);

    return true;
}

int32_t frame_authentication_get_seq_num(const struct frame_data_auth *auth, bool is_tx)
{
    if (!is_tx)
    {
        return auth->seq;
    }
    else
    {
        if (auth->auth_alg == DOT11_AUTH_ALG_OPEN)
        {
            return 1;
        }
        else
        {
            const struct dot11_auth_seq_status *seq_status =
                (const struct dot11_auth_seq_status *)(auth->auth_data);
            if (auth->auth_data_len < sizeof(*seq_status))
            {
                MMLOG_WRN("Auth data too short %u\n", auth->auth_data_len);
                return -1;
            }
            return le16toh(seq_status->seq);
        }
    }
}
