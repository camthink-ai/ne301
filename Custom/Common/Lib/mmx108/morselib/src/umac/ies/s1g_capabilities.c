/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "s1g_capabilities.h"
#include "common/common.h"
#include "dot11/dot11.h"
#include "umac/config/umac_config.h"
#include "umac/connection/umac_connection.h"
#include "umac/rc/umac_rc.h"
#include "umac/interface/umac_interface.h"

void ie_s1g_capabilities_build(struct umac_data *umacd, struct consbuf *buf)
{
    struct dot11_ie_s1g_capabilities *ie =
        (struct dot11_ie_s1g_capabilities *)consbuf_reserve(buf, sizeof(*ie));
    if (ie != NULL)
    {
        uint32_t max_bw = umac_interface_max_supported_bw(umacd);

        memset(ie, 0, sizeof(*ie));

        ie->header.element_id = DOT11_IE_S1G_CAPABILITIES;
        ie->header.length = sizeof(*ie) - sizeof(ie->header);

        int channel_width_override = umac_config_get_supported_channel_width_field_override(umacd);
        if (channel_width_override >= 0)
        {
            MMOSAL_ASSERT(channel_width_override <= 3);
            DOT11_S1G_CAP_INFO_0_SET_SUPP_CHAN_WIDTH(ie->s1g_capabilities_information[0],
                                                     channel_width_override);
        }
        else
        {

            switch (max_bw)
            {
                case 1:
                case 2:

                    break;

                case 4:
                    DOT11_S1G_CAP_INFO_0_SET_SUPP_CHAN_WIDTH(
                        ie->s1g_capabilities_information[0],
                        DOT11_S1G_CAP_SUPP_CHAN_WIDTH_1_2_4_MHZ);
                    break;

                case 8:
                    DOT11_S1G_CAP_INFO_0_SET_SUPP_CHAN_WIDTH(
                        ie->s1g_capabilities_information[0],
                        DOT11_S1G_CAP_SUPP_CHAN_WIDTH_1_2_4_8_MHZ);
                    break;

                case 16:
                    DOT11_S1G_CAP_INFO_0_SET_SUPP_CHAN_WIDTH(
                        ie->s1g_capabilities_information[0],
                        DOT11_S1G_CAP_SUPP_CHAN_WIDTH_1_2_4_8_16_MHZ);
                    break;

                default:
                    MMOSAL_ASSERT(false);
                    break;
            }
        }

        if (umac_config_rc_is_sgi_enabled(umacd))
        {

            switch (max_bw)
            {
                case 16:
                    DOT11_S1G_CAP_INFO_0_SET_SGI_16MHZ(ie->s1g_capabilities_information[0], true);
                    MM_FALLTHROUGH;

                case 8:
                    DOT11_S1G_CAP_INFO_0_SET_SGI_8MHZ(ie->s1g_capabilities_information[0], true);
                    MM_FALLTHROUGH;

                case 4:
                    DOT11_S1G_CAP_INFO_0_SET_SGI_4MHZ(ie->s1g_capabilities_information[0], true);
                    MM_FALLTHROUGH;

                case 2:
                    DOT11_S1G_CAP_INFO_0_SET_SGI_2MHZ(ie->s1g_capabilities_information[0], true);
                    MM_FALLTHROUGH;

                case 1:
                    DOT11_S1G_CAP_INFO_0_SET_SGI_1MHZ(ie->s1g_capabilities_information[0], true);
                    break;

                default:
                    MMOSAL_ASSERT(false);
            }
        }

        if (MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), TRAVELING_PILOT_TWO_STREAM))
        {
            DOT11_S1G_CAP_INFO_2_SET_TRAV_PILOT_SUPPORT(ie->s1g_capabilities_information[2],
                                                        DOT11_S1G_CAP_TRAV_PILOT_SUPP_1_AND_2_NSTS);
        }
        else if (
            MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), TRAVELING_PILOT_ONE_STREAM))
        {
            DOT11_S1G_CAP_INFO_2_SET_TRAV_PILOT_SUPPORT(ie->s1g_capabilities_information[2],
                                                        DOT11_S1G_CAP_TRAV_PILOT_SUPP_1_NSTS);
        }

        const struct mmwlan_sta_args *sta_args = umac_connection_get_sta_args(umacd);
        MMOSAL_ASSERT(sta_args != NULL);


        DOT11_S1G_CAP_INFO_4_SET_STA_TYPE_SUPPORT(ie->s1g_capabilities_information[4],
                                                  sta_args->sta_type);

        if (umac_config_is_non_tim_mode_enabled(umacd) &&
            MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), NON_TIM))
        {
            DOT11_S1G_CAP_INFO_4_SET_NON_TIM_SUPPORT(ie->s1g_capabilities_information[4], true);
        }

        if (umac_config_is_ampdu_enabled(umacd) &&
            MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), AMPDU))
        {
            DOT11_S1G_CAP_INFO_5_SET_AMPDU_SUPPORTED(ie->s1g_capabilities_information[5], true);
        }

        if (umac_connection_is_cac_enabled(sta_args))
        {
            DOT11_S1G_CAP_INFO_5_SET_CAC(ie->s1g_capabilities_information[5], true);
        }

        if (umac_connection_is_raw_enabled(sta_args))
        {

            DOT11_S1G_CAP_INFO_6_SET_RAW_OPERATION_SUPPORT(ie->s1g_capabilities_information[6],
                                                           true);
        }


        DOT11_S1G_CAP_INFO_7_SET_DUP_1MHZ_SUPPORT(ie->s1g_capabilities_information[7], true);
        if (umac_interface_get_control_response_bw_1mhz_out_enabled(umacd))
        {
            DOT11_S1G_CAP_INFO_7_SET_1MHZ_CTRL_RSP_PREAMBLE_SUPPORT(
                ie->s1g_capabilities_information[7],
                true);
        }


        uint8_t s1g_mcs_map = 0xFF;
        if (MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), MCS8) &&
            MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), MCS9))
        {
            DOT11_S1G_MCS_MAP_SET_1SS(s1g_mcs_map, DOT11_S1G_NSS_MAX_MCS_9);
        }
        else
        {
            DOT11_S1G_MCS_MAP_SET_1SS(s1g_mcs_map, DOT11_S1G_NSS_MAX_MCS_7);
        }
        DOT11_S1G_MCS_MAP_SET_2SS(s1g_mcs_map, DOT11_S1G_NSS_MAX_UNSUPPORTED);
        DOT11_S1G_MCS_MAP_SET_3SS(s1g_mcs_map, DOT11_S1G_NSS_MAX_UNSUPPORTED);
        DOT11_S1G_MCS_MAP_SET_4SS(s1g_mcs_map, DOT11_S1G_NSS_MAX_UNSUPPORTED);
        ie->supported_s1g_mcs_nss_set[0] |= (s1g_mcs_map & 0xFF);

        ie->supported_s1g_mcs_nss_set[2] |= ((s1g_mcs_map << 1) & 0xFE);
        ie->supported_s1g_mcs_nss_set[3] |= ((s1g_mcs_map >> 7) & 0x01);

        if (MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), TWT_REQUESTER))
        {
            DOT11_S1G_CAP_INFO_8_SET_TWT_REQUESTER_SUPPORT(ie->s1g_capabilities_information[8],
                                                           true);
        }
    }
}

void ie_s1g_capabilities_build_ap(struct umac_data *umacd, struct consbuf *buf)
{
    struct dot11_ie_s1g_capabilities *ie =
        (struct dot11_ie_s1g_capabilities *)consbuf_reserve(buf, sizeof(*ie));
    if (ie != NULL)
    {
        uint32_t max_bw = 8;

        memset(ie, 0, sizeof(*ie));

        ie->header.element_id = DOT11_IE_S1G_CAPABILITIES;
        ie->header.length = sizeof(*ie) - sizeof(ie->header);

        int channel_width_override = 0;
        if (channel_width_override >= 0)
        {
            MMOSAL_ASSERT(channel_width_override <= 3);
            ie->s1g_capabilities_information[0] |= channel_width_override
                                                   << DOT11_SHIFT_S1G_CAP0_SUPP_CHAN_WIDTH;
        }
        else
        {

            switch (max_bw)
            {
                case 1:
                case 2:

                    break;

                case 4:
                    ie->s1g_capabilities_information[0] |= DOT11_S1G_CAP_SUPP_CHAN_WIDTH_1_2_4_MHZ
                                                           << DOT11_SHIFT_S1G_CAP0_SUPP_CHAN_WIDTH;
                    break;

                case 8:
                    ie->s1g_capabilities_information[0] |= DOT11_S1G_CAP_SUPP_CHAN_WIDTH_1_2_4_8_MHZ
                                                           << DOT11_SHIFT_S1G_CAP0_SUPP_CHAN_WIDTH;
                    break;

                case 16:
                    ie->s1g_capabilities_information[0] |=
                        DOT11_S1G_CAP_SUPP_CHAN_WIDTH_1_2_4_8_16_MHZ
                        << DOT11_SHIFT_S1G_CAP0_SUPP_CHAN_WIDTH;
                    break;

                default:
                    MMOSAL_ASSERT(false);
                    break;
            }
        }


        if (true)
        {

            ie->s1g_capabilities_information[0] |= (max_bw | (max_bw - 1))
                                                   << DOT11_SHIFT_S1G_CAP0_SGI_1MHZ;
        }

        if (MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), TRAVELING_PILOT_TWO_STREAM))
        {
            DOT11_S1G_CAP_INFO_2_SET_TRAV_PILOT_SUPPORT(ie->s1g_capabilities_information[2],
                                                        DOT11_S1G_CAP_TRAV_PILOT_SUPP_1_AND_2_NSTS);
        }
        else if (
            MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), TRAVELING_PILOT_ONE_STREAM))
        {
            DOT11_S1G_CAP_INFO_2_SET_TRAV_PILOT_SUPPORT(ie->s1g_capabilities_information[2],
                                                        DOT11_S1G_CAP_TRAV_PILOT_SUPP_1_NSTS);
        }










        if (false)
        {
            DOT11_S1G_CAP_INFO_4_SET_NON_TIM_SUPPORT(ie->s1g_capabilities_information[4], true);
        }

        if (umac_config_is_ampdu_enabled(umacd) &&
            MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), AMPDU))
        {
            ie->s1g_capabilities_information[5] |= DOT11_MASK_S1G_CAP5_AMPDU_SUPPORTED;
        }


        if (false)
        {
            ie->s1g_capabilities_information[5] |= DOT11_MASK_S1G_CAP5_CAC;
        }


        if (false)
        {

            ie->s1g_capabilities_information[6] |= DOT11_MASK_S1G_CAP6_RAW_OPERATION_SUPPORT;
        }


        ie->s1g_capabilities_information[7] |= DOT11_MASK_S1G_CAP7_DUP_1MHZ_SUPPORT;

        if (false)
        {
            ie->s1g_capabilities_information[7] |=
                DOT11_MASK_S1G_CAP7_1MHZ_CTRL_RSP_PREAMBLE_SUPPORT;
        }

        uint8_t s1g_mcs_map = 0xFF;
        if (MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), MCS8) &&
            MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), MCS9))
        {
            DOT11_S1G_MCS_MAP_SET_1SS(s1g_mcs_map, DOT11_S1G_NSS_MAX_MCS_9);
        }
        else
        {
            DOT11_S1G_MCS_MAP_SET_1SS(s1g_mcs_map, DOT11_S1G_NSS_MAX_MCS_7);
        }
        DOT11_S1G_MCS_MAP_SET_2SS(s1g_mcs_map, DOT11_S1G_NSS_MAX_UNSUPPORTED);
        DOT11_S1G_MCS_MAP_SET_3SS(s1g_mcs_map, DOT11_S1G_NSS_MAX_UNSUPPORTED);
        DOT11_S1G_MCS_MAP_SET_4SS(s1g_mcs_map, DOT11_S1G_NSS_MAX_UNSUPPORTED);
        ie->supported_s1g_mcs_nss_set[0] |= (s1g_mcs_map & 0xFF);

        ie->supported_s1g_mcs_nss_set[2] |= ((s1g_mcs_map << 1) & 0xFE);
        ie->supported_s1g_mcs_nss_set[3] |= ((s1g_mcs_map >> 7) & 0x01);

        if (MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), TWT_REQUESTER))
        {
            DOT11_S1G_CAP_INFO_8_SET_TWT_REQUESTER_SUPPORT(ie->s1g_capabilities_information[8],
                                                           true);
        }








    }
}

uint8_t ie_s1g_capabilities_get_sgi_flags(const struct dot11_ie_s1g_capabilities *s1g_caps)
{
    MMOSAL_ASSERT(s1g_caps);

    return (s1g_caps->s1g_capabilities_information[0] & (DOT11_MASK_S1G_CAP0_SGI_1MHZ |
                                                         DOT11_MASK_S1G_CAP0_SGI_2MHZ |
                                                         DOT11_MASK_S1G_CAP0_SGI_4MHZ |
                                                         DOT11_MASK_S1G_CAP0_SGI_8MHZ |
                                                         DOT11_MASK_S1G_CAP0_SGI_16MHZ)) >>
           DOT11_SHIFT_S1G_CAP0_SGI_1MHZ;
}

uint8_t ie_s1g_capabilities_get_max_rx_mcs_for_1ss(const struct dot11_ie_s1g_capabilities *s1g_caps)
{

    switch (dot11_s1g_mcs_map_get_1ss(s1g_caps->supported_s1g_mcs_nss_set[0]))
    {
        case DOT11_S1G_NSS_MAX_MCS_2:
            return 2;

        case DOT11_S1G_NSS_MAX_MCS_7:
            return 7;

        case DOT11_S1G_NSS_MAX_MCS_9:
            return 9;

        default:

            return 0;
    }
}

bool ie_s1g_capabilities_get_traveling_pilots_support(
    const struct dot11_ie_s1g_capabilities *s1g_caps)
{
    switch (dot11_s1g_cap_info_2_get_trav_pilot_support(s1g_caps->s1g_capabilities_information[2]))
    {
        case DOT11_S1G_CAP_TRAV_PILOT_SUPP_1_AND_2_NSTS:
        case DOT11_S1G_CAP_TRAV_PILOT_SUPP_1_NSTS:
            return true;

        case DOT11_S1G_CAP_TRAV_PILOT_SUPP_NOT_SUPPORTED:
        default:
            return false;
    }
}
