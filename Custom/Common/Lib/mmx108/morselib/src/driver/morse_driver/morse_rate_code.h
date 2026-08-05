/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once


#include "misc.h"

typedef enum dot11_bandwidth
{
    DOT11_BANDWIDTH_1MHZ = 0,
    DOT11_BANDWIDTH_2MHZ = 1,
    DOT11_BANDWIDTH_20MHZ = DOT11_BANDWIDTH_2MHZ,
    DOT11_BANDWIDTH_4MHZ = 2,
    DOT11_BANDWIDTH_40MHZ = DOT11_BANDWIDTH_4MHZ,
    DOT11_BANDWIDTH_8MHZ = 3,
    DOT11_BANDWIDTH_80MHZ = DOT11_BANDWIDTH_8MHZ,
    DOT11_BANDWIDTH_16MHZ = 4,
    DOT11_BANDWIDTH_160MHZ = DOT11_BANDWIDTH_16MHZ,

    DOT11_MAX_BANDWIDTH = DOT11_BANDWIDTH_16MHZ,
    DOT11_INVALID_BANDWIDTH = 5
} dot11_bandwidth_t;

typedef enum morse_rate_preamble
{

    MORSE_RATE_PREAMBLE_S1G_LONG = 0,

    MORSE_RATE_PREAMBLE_S1G_SHORT = 1,

    MORSE_RATE_PREAMBLE_S1G_1M = 2,
    MORSE_RATE_PREAMBLE_MAX_S1G = MORSE_RATE_PREAMBLE_S1G_1M,

    MORSE_RATE_PREAMBLE_DSSS_LONG = 3,

    MORSE_RATE_PREAMBLE_DSSS_SHORT = 4,

    MORSE_RATE_PREAMBLE_ERP = 5,

    MORSE_RATE_PREAMBLE_HT = 6,

    MORSE_RATE_MAX_PREAMBLE = MORSE_RATE_PREAMBLE_HT,
    MORSE_RATE_INVALID_PREAMBLE = 7
} morse_rate_preamble_t;

typedef enum dot11b_mcs
{
    DOT11B_DSSS_1M = 0,
    DOT11B_DSSS_2M = 1,
    DOT11B_CCK_5_5_M = 2,
    DOT11B_CCK_11M = 3,

    DOT11B_MAX_MCS = DOT11B_CCK_11M
} dot11b_mcs_t;

typedef enum dot11g_mcs
{
    DOT11G_OFDM_6M = 0,
    DOT11G_OFDM_9M = 1,
    DOT11G_OFDM_12M = 2,
    DOT11G_OFDM_18M = 3,
    DOT11G_OFDM_24M = 4,
    DOT11G_OFDM_36M = 5,
    DOT11G_OFDM_48M = 6,
    DOT11G_OFDM_54M = 7,

    DOT11G_MAX_MCS = DOT11G_OFDM_54M
} dot11g_mcs_t;


typedef uint32_t morse_rate_code_t;

#define MORSE_RATECODE_PREAMBLE            (0x0000000F)
#define MORSE_RATECODE_MCS_INDEX           (0x000000F0)
#define MORSE_RATECODE_NSS_INDEX           (0x00000700)
#define MORSE_RATECODE_BW_INDEX            (0x00003800)
#define MORSE_RATECODE_LDPC_FLAG           (0x00004000)
#define MORSE_RATECODE_STBC_FLAG           (0x00008000)
#define MORSE_RATECODE_RTS_FLAG            (0x00010000)
#define MORSE_RATECODE_CTS2SELF_FLAG       (0x00020000)
#define MORSE_RATECODE_SHORT_GI_FLAG       (0x00040000)
#define MORSE_RATECODE_TRAV_PILOTS_FLAG    (0x00080000)
#define MORSE_RATECODE_CTRL_RESP_1MHZ_FLAG (0x00100000)
#define MORSE_RATECODE_DUP_FORMAT_FLAG     (0x00200000)
#define MORSE_RATECODE_DUP_BW_INDEX        (0x01C00000)


static inline morse_rate_preamble_t morse_ratecode_preamble_get(morse_rate_code_t rc)
{
    return (morse_rate_preamble_t)(BMGET(rc, MORSE_RATECODE_PREAMBLE));
}


static inline uint8_t morse_ratecode_mcs_index_get(morse_rate_code_t rc)
{
    return (BMGET(rc, MORSE_RATECODE_MCS_INDEX));
}


static inline uint8_t morse_ratecode_nss_index_get(morse_rate_code_t rc)
{
    return (BMGET(rc, MORSE_RATECODE_NSS_INDEX));
}


static inline dot11_bandwidth_t morse_ratecode_bw_index_get(morse_rate_code_t rc)
{
    return (dot11_bandwidth_t)(BMGET(rc, MORSE_RATECODE_BW_INDEX));
}


static inline bool morse_ratecode_rts_get(morse_rate_code_t rc)
{
    return (BMGET(rc, MORSE_RATECODE_RTS_FLAG));
}


static inline bool morse_ratecode_cts2self_get(morse_rate_code_t rc)
{
    return (BMGET(rc, MORSE_RATECODE_CTS2SELF_FLAG));
}


static inline bool morse_ratecode_sgi_get(morse_rate_code_t rc)
{
    return (BMGET(rc, MORSE_RATECODE_SHORT_GI_FLAG));
}


static inline bool morse_ratecode_trav_pilots_get(morse_rate_code_t rc)
{
    return (BMGET(rc, MORSE_RATECODE_TRAV_PILOTS_FLAG));
}


static inline bool morse_ratecode_ldpc_get(morse_rate_code_t rc)
{
    return BMGET(rc, MORSE_RATECODE_LDPC_FLAG);
}


static inline bool morse_ratecode_stbc_get(morse_rate_code_t rc)
{
    return BMGET(rc, MORSE_RATECODE_STBC_FLAG);
}


static inline bool morse_ratecode_ctrl_resp_1mhz_get(morse_rate_code_t rc)
{
    return (BMGET(rc, MORSE_RATECODE_CTRL_RESP_1MHZ_FLAG));
}


static inline bool morse_ratecode_dup_format_get(morse_rate_code_t rc)
{
    return (BMGET(rc, MORSE_RATECODE_DUP_FORMAT_FLAG));
}


static inline dot11_bandwidth_t morse_ratecode_dup_bw_index_get(morse_rate_code_t rc)
{
    return (dot11_bandwidth_t)(BMGET(rc, MORSE_RATECODE_DUP_BW_INDEX));
}


#define MORSE_RATECODE_INIT(bw_index, nss_index, mcs_index, preamble) \
    ((BMSET((bw_index), MORSE_RATECODE_BW_INDEX)) |                   \
     (BMSET((nss_index), MORSE_RATECODE_NSS_INDEX)) |                 \
     (BMSET((mcs_index), MORSE_RATECODE_MCS_INDEX)) |                 \
     (preamble))


static inline morse_rate_code_t morse_ratecode_init(dot11_bandwidth_t bw_index,
                                                    uint32_t nss_index,
                                                    uint32_t mcs_index,
                                                    morse_rate_preamble_t preamble)
{
    return MORSE_RATECODE_INIT(bw_index, nss_index, mcs_index, preamble);
}


static inline void morse_ratecode_preamble_set(morse_rate_code_t *rc,
                                               morse_rate_preamble_t preamble)
{
    *rc = ((*rc & ~(MORSE_RATECODE_PREAMBLE)) | BMSET(preamble, MORSE_RATECODE_PREAMBLE));
}


static inline void morse_ratecode_mcs_index_set(morse_rate_code_t *rc, uint32_t mcs_index)
{
    *rc = ((*rc & ~(MORSE_RATECODE_MCS_INDEX)) | BMSET(mcs_index, MORSE_RATECODE_MCS_INDEX));
}


static inline void morse_ratecode_nss_index_set(morse_rate_code_t *rc, uint32_t nss_index)
{
    *rc = ((*rc & ~(MORSE_RATECODE_NSS_INDEX)) | BMSET(nss_index, MORSE_RATECODE_NSS_INDEX));
}


static inline void morse_ratecode_bw_index_set(morse_rate_code_t *rc, dot11_bandwidth_t bw_index)
{
    *rc = ((*rc & ~(MORSE_RATECODE_BW_INDEX)) | BMSET(bw_index, MORSE_RATECODE_BW_INDEX));
}


static inline void morse_ratecode_update_s1g_bw_preamble(morse_rate_code_t *rc,
                                                         dot11_bandwidth_t bw_index)
{

    morse_rate_preamble_t pream = MORSE_RATE_PREAMBLE_S1G_SHORT;
    if (bw_index == DOT11_BANDWIDTH_1MHZ)
    {
        pream = MORSE_RATE_PREAMBLE_S1G_1M;
    }
    morse_ratecode_preamble_set(rc, pream);
    morse_ratecode_bw_index_set(rc, bw_index);
}


static inline void morse_ratecode_dup_bw_index_set(morse_rate_code_t *rc,
                                                   dot11_bandwidth_t dup_bw_index)
{
    *rc =
        ((*rc & ~(MORSE_RATECODE_DUP_BW_INDEX)) | BMSET(dup_bw_index, MORSE_RATECODE_DUP_BW_INDEX));
}


static inline void morse_ratecode_enable_rts(morse_rate_code_t *rc)
{
    *rc |= (MORSE_RATECODE_RTS_FLAG);
}


static inline void morse_ratecode_enable_ctrl_resp_1mhz(morse_rate_code_t *rc)
{
    *rc |= (MORSE_RATECODE_CTRL_RESP_1MHZ_FLAG);
}


static inline void morse_ratecode_enable_sgi(morse_rate_code_t *rc)
{
    *rc |= (MORSE_RATECODE_SHORT_GI_FLAG);
}


static inline void morse_ratecode_disable_sgi(morse_rate_code_t *rc)
{
    *rc &= ~(MORSE_RATECODE_SHORT_GI_FLAG);
}


static inline void morse_ratecode_enable_dup_format(morse_rate_code_t *rc)
{
    *rc |= (MORSE_RATECODE_DUP_FORMAT_FLAG);
}


static inline void morse_ratecode_disable_dup_format(morse_rate_code_t *rc)
{
    *rc &= ~(MORSE_RATECODE_DUP_FORMAT_FLAG);
}


static inline void morse_ratecode_enable_trav_pilots(morse_rate_code_t *rc)
{
    *rc |= (MORSE_RATECODE_TRAV_PILOTS_FLAG);
}


static inline void morse_ratecode_disable_trav_pilots(morse_rate_code_t *rc)
{
    *rc &= ~(MORSE_RATECODE_TRAV_PILOTS_FLAG);
}


static inline void morse_ratecode_enable_ldpc(morse_rate_code_t *rc)
{
    *rc |= MORSE_RATECODE_LDPC_FLAG;
}


static inline void morse_ratecode_disable_ldpc(morse_rate_code_t *rc)
{
    *rc &= ~MORSE_RATECODE_LDPC_FLAG;
}


static inline void morse_ratecode_enable_stbc(morse_rate_code_t *rc)
{
    *rc |= MORSE_RATECODE_STBC_FLAG;
}


static inline void morse_ratecode_disable_stbc(morse_rate_code_t *rc)
{
    *rc &= ~MORSE_RATECODE_STBC_FLAG;
}


static inline dot11_bandwidth_t morse_ratecode_bw_mhz_to_bw_index(uint8_t bw_mhz)
{
    return ((bw_mhz == 1) ? DOT11_BANDWIDTH_1MHZ :
            (bw_mhz == 2) ? DOT11_BANDWIDTH_2MHZ :
            (bw_mhz == 4) ? DOT11_BANDWIDTH_4MHZ :
            (bw_mhz == 8) ? DOT11_BANDWIDTH_8MHZ :
                            DOT11_BANDWIDTH_2MHZ);
}


static inline uint8_t morse_ratecode_bw_index_to_s1g_bw_mhz(dot11_bandwidth_t bw_idx)
{
    return ((bw_idx == DOT11_BANDWIDTH_1MHZ) ? 1 :
            (bw_idx == DOT11_BANDWIDTH_2MHZ) ? 2 :
            (bw_idx == DOT11_BANDWIDTH_4MHZ) ? 4 :
            (bw_idx == DOT11_BANDWIDTH_8MHZ) ? 8 :
                                               2);
}
