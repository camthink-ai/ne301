/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */
#pragma once

#include "driver/shim/driver_types.h"
#include "mmpkt.h"
#include "misc.h"
#include "morse_rate_code.h"


#define MORSE_SKB_HEADER_SYNC (0xAA)

#define MORSE_SKB_HEADER_CHIP_OWNED_SYNC (0xBB)


enum morse_tx_status_and_conf_flags
{
    MORSE_TX_STATUS_FLAGS_NO_ACK = BIT(0),
    MORSE_TX_STATUS_FLAGS_NO_REPORT = BIT(1),
    MORSE_TX_CONF_FLAGS_CTL_AMPDU = BIT(2),
    MORSE_TX_CONF_FLAGS_HW_ENCRYPT = BIT(3),
    MORSE_TX_CONF_FLAGS_VIF_ID =
        (BIT(4) | BIT(5) | BIT(6) | BIT(7) | BIT(8) | BIT(9) | BIT(10) | BIT(11)),
    MORSE_TX_CONF_FLAGS_KEY_IDX = (BIT(12) | BIT(13) | BIT(14)),
    MORSE_TX_STATUS_FLAGS_PS_FILTERED = (BIT(15)),
    MORSE_TX_CONF_IGNORE_TWT = (BIT(16)),
    MORSE_TX_STATUS_PAGE_INVALID = (BIT(17)),
    MORSE_TX_CONF_NO_PS_BUFFER = (BIT(18)),
    MORSE_TX_STATUS_DUTY_CYCLE_CANT_SEND = (BIT(19)),
    MORSE_TX_CONF_HAS_PV1_BPN_IN_BODY = (BIT(21)),
    MORSE_TX_CONF_FLAGS_SEND_AFTER_DTIM = (BIT(22)),
    MORSE_TX_STATUS_WAS_AGGREGATED = (BIT(23)),
    MORSE_TX_CONF_FLAGS_FULLMAC_REPORT = BIT(24),
    MORSE_TX_CONF_FLAGS_IMMEDIATE_REPORT = (BIT(31))
};


#define MORSE_TX_CONF_FLAGS_VIF_ID_MASK   (0xFF)
#define MORSE_TX_CONF_FLAGS_VIF_ID_SET(x) (((x) & MORSE_TX_CONF_FLAGS_VIF_ID_MASK) << 4)
#define MORSE_TX_CONF_FLAGS_VIF_ID_GET(x) (((x) & MORSE_TX_CONF_FLAGS_VIF_ID) >> 4)


#define MORSE_TX_CONF_FLAGS_KEY_IDX_SET(x) (((x) & 0x07) << 12)
#define MORSE_TX_CONF_FLAGS_KEY_IDX_GET(x) (((x) & MORSE_TX_CONF_FLAGS_KEY_IDX) >> 12)


enum morse_rx_status_flags
{
    MORSE_RX_STATUS_FLAGS_ERROR = BIT(0),
    MORSE_RX_STATUS_FLAGS_DECRYPTED = BIT(1),
    MORSE_RX_STATUS_FLAGS_FCS_INCLUDED = BIT(2),
    MORSE_RX_STATUS_FLAGS_EOF = BIT(3),
    MORSE_RX_STATUS_FLAGS_AMPDU = BIT(4),
    MORSE_RX_STATUS_FLAGS_NDP = BIT(7),
    MORSE_RX_STATUS_FLAGS_UPLINK = BIT(8),
    MORSE_RX_STATUS_FLAGS_RI = (BIT(9) | BIT(10)),
    MORSE_RX_STATUS_FLAGS_NDP_TYPE = (BIT(11) | BIT(12) | BIT(13)),
    MORSE_RX_STATUS_FLAGS_CRC_ERROR = BIT(14),
    MORSE_RX_STATUS_FLAGS_VIF_ID = GENMASK(24, 17),
};


#define MORSE_RX_STATUS_FLAGS_VIF_ID_MASK     (0xFF)
#define MORSE_RX_STATUS_FLAGS_VIF_ID_SET(x)   (((x) & MORSE_RX_STATUS_FLAGS_VIF_ID_MASK) << 17)
#define MORSE_RX_STATUS_FLAGS_VIF_ID_GET(x)   (((x) & MORSE_RX_STATUS_FLAGS_VIF_ID) >> 17)
#define MORSE_RX_STATUS_FLAGS_VIF_ID_CLEAR(x) ((x) & ~(MORSE_RX_STATUS_FLAGS_VIF_ID_MASK << 17))


#define MORSE_RX_STATUS_FLAGS_UPL_IND_GET(x) (((x) & MORSE_RX_STATUS_FLAGS_UPLINK) >> 8)


#define MORSE_RX_STATUS_FLAGS_RI_GET(x) (((x) & MORSE_RX_STATUS_FLAGS_RI) >> 9)


#define MORSE_RX_STATUS_FLAGS_NDP_TYPE_GET(x) (((x) & MORSE_RX_STATUS_FLAGS_NDP_TYPE) >> 11)


enum morse_skb_channel
{
    MORSE_SKB_CHAN_DATA = 0x0,
    MORSE_SKB_CHAN_NDP_FRAMES = 0x1,
    MORSE_SKB_CHAN_DATA_NOACK = 0x2,
    MORSE_SKB_CHAN_BEACON = 0x3,
    MORSE_SKB_CHAN_MGMT = 0x4,
    MORSE_SKB_CHAN_WIPHY = 0x5,
    MORSE_SKB_CHAN_INTERNAL_CRIT_BEACON = 0x80,
    MORSE_SKB_CHAN_LOOPBACK = 0xEE,
    MORSE_SKB_CHAN_COMMAND = 0xFE,
    MORSE_SKB_CHAN_TX_STATUS = 0xFF
};


#define MORSE_SKB_MAX_RATES (4)


struct MM_PACKED morse_skb_rate_info
{
    morse_rate_code_t morse_rc;
    uint8_t count;
};


struct MM_PACKED morse_skb_tx_status
{
    uint32_t flags;
    uint32_t pkt_id;
    uint8_t tid;

    uint8_t channel;

    uint16_t ampdu_info;
    struct morse_skb_rate_info rates[MORSE_SKB_MAX_RATES];
};


#define MORSE_TXSTS_AMPDU_INFO_GET_TAG(x) (((x) >> 10) & 0x3F)
#define MORSE_TXSTS_AMPDU_INFO_GET_LEN(x) (((x) >> 5) & 0x1F)
#define MORSE_TXSTS_AMPDU_INFO_GET_SUC(x) ((x) & 0x1F)


struct MM_PACKED morse_skb_tx_info
{
    uint32_t flags;
    uint32_t pkt_id;
    uint8_t tid;
    uint8_t tid_params;
    uint8_t mmss_params;
    uint8_t padding[1];
    struct morse_skb_rate_info rates[MORSE_SKB_MAX_RATES];
};


#define TX_INFO_TID_PARAMS_MAX_REORDER_BUF 0x1f
#define TX_INFO_TID_PARAMS_AMPDU_ENABLED   0x20
#define TX_INFO_TID_PARAMS_AMSDU_SUPPORTED 0x40
#define TX_INFO_TID_PARAMS_USE_LEGACY_BA   0x80


#define TX_INFO_MMSS_PARAMS_MMSS_MASK         (0x0F)
#define TX_INFO_MMSS_PARAMS_MMSS_OFFSET_START 4
#define TX_INFO_MMSS_PARAMS_MMSS_OFFSET_MASK  (0xF0)
#define TX_INFO_MMSS_PARAMS_SET_MMSS(x)       ((x) & TX_INFO_MMSS_PARAMS_MMSS_MASK)
#define TX_INFO_MMSS_PARAMS_SET_MMSS_OFFSET(x) \
    (((x) << TX_INFO_MMSS_PARAMS_MMSS_OFFSET_START) & TX_INFO_MMSS_PARAMS_MMSS_OFFSET_MASK)


struct MM_PACKED morse_skb_rx_status
{
    uint32_t flags;
    morse_rate_code_t morse_rc;
    uint16_t rssi;
    uint16_t freq_100khz;
    uint8_t bss_color;
    int8_t noise_dbm;

    uint8_t padding[2];
    uint64_t rx_timestamp_us;
};


struct MM_PACKED morse_buff_skb_header
{
    uint8_t sync;
    uint8_t channel;
    uint16_t len;
    uint8_t offset;
    uint8_t checksum_lower;
    uint16_t checksum_upper;

    union
    {
        struct morse_skb_tx_info tx_info;
        struct morse_skb_tx_status tx_status;
        struct morse_skb_rx_status rx_status;
    };
};
