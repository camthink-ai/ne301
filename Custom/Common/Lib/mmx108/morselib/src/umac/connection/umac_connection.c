/*
 * Copyright 2022-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <errno.h>
#include <stdint.h>

#include "umac/data/umac_data_private.h"
#include "umac/ies/s1g_operation.h"
#include "umac_connection.h"
#include "mmwlan.h"
#include "umac/data/umac_data.h"
#include "umac_connection_data.h"
#include "umac/datapath/umac_datapath.h"
#include "common/mac_address.h"
#include "mmlog.h"
#include "umac/core/umac_core.h"
#include "umac/supplicant_shim/umac_supp_shim.h"
#include "umac/frames/authentication.h"
#include "umac/frames/association.h"
#include "umac/rc/umac_rc.h"
#include "umac/stats/umac_stats.h"
#include "umac/ies/twt_ie.h"
#include "umac/ies/s1g_capabilities.h"
#include "umac/ies/wmm.h"
#include "umac/ies/morse_ie.h"
#include "umac/ies/aid_response.h"
#include "umac/ies/timeout_interval.h"
#include "umac/interface/umac_interface.h"
#include "umac/wnm_sleep/umac_wnm_sleep.h"
#include "umac/config/umac_config.h"
#include "umac/twt/umac_twt.h"
#include "umac/ba/umac_ba.h"
#include "umac/offload/umac_offload.h"
#include "dot11/dot11_utils.h"
#include "umac/ies/ecsa.h"
#include "umac/ies/s1g_beacon_compatibility.h"


#define UMAC_CONNECTION_MON_UNSTABLE_INTERVAL_MS 100

#define UMAC_CONNECTION_DEAUTH_THRESHOLD_AP_QUERIES (2)

#define DEFAULT_MORSE_IBSS_ACK_TIMEOUT_ADJUST_US (1000)

#define MORSE_SHORT_ACK_TIMEOUT_ADJUST_US (300)

void umac_connection_init(struct umac_data *umacd)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    data->stad = umac_sta_data_alloc_static(umacd);

    data->vif_id = UMAC_INTERFACE_VIF_ID_INVALID;
    umac_keys_init(data->stad);
    connection_fsm_init(&data->conn_fsm);
    connection_mon_fsm_init(&data->conn_mon.fsm);
    data->conn_fsm.arg = umacd;
    data->conn_mon.fsm.arg = umacd;
    data->is_initialised = true;
}

void umac_connection_deinit(struct umac_data *umacd)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    if (data->is_initialised)
    {
        if (data->sta_args.extra_assoc_ies != NULL)
        {
            mmosal_free(data->sta_args.extra_assoc_ies);
            data->sta_args.extra_assoc_ies = NULL;
            data->sta_args.extra_assoc_ies_len = 0;
        }
        memset(data, 0, sizeof(*data));
    }
}

bool umac_connection_validate_sta_args(const struct mmwlan_sta_args *args)
{
    if (args == NULL)
    {
        MMLOG_ERR("Args provided must not be NULL.\n");
        return false;
    }

    uint16_t passphrase_len = args->passphrase_len;
    if (passphrase_len == 0)
    {
        passphrase_len = strnlen((const char *)args->passphrase, MMWLAN_PASSPHRASE_MAXLEN + 1);
    }

    if (args->ssid_len > MMWLAN_SSID_MAXLEN)
    {
        MMLOG_ERR("Invalid %s length %u\n", "SSID", args->ssid_len);
        return false;
    }

    if (passphrase_len > MMWLAN_PASSPHRASE_MAXLEN)
    {
        MMLOG_ERR("Invalid passphrase length %u or not NULL-terminated.\n", passphrase_len);
        return false;
    }

    if ((args->security_type == MMWLAN_SAE) && (passphrase_len == 0))
    {
        MMLOG_ERR("SAE passphrase required for security type %u\n", args->security_type);
        return false;
    }

    if (((args->extra_assoc_ies != NULL) && (args->extra_assoc_ies_len == 0)) ||
        ((args->extra_assoc_ies == NULL) && (args->extra_assoc_ies_len != 0)))
    {
        MMLOG_ERR("Invalid extra_assoc_ies length.\n");
        return false;
    }

    return true;
}

static enum mmwlan_status umac_connection_start_interface(struct umac_data *umacd,
                                                          const char *confname)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    umac_datapath_configure_sta_mode(umacd);

    enum mmwlan_status status = umac_interface_add(umacd, UMAC_INTERFACE_STA, NULL, &data->vif_id);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_INF("Interface add failed\n");
        return status;
    }

    status = umac_supp_add_sta_interface(umacd, confname);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_INF("Supplicant interface add failed\n");
        umac_interface_remove(umacd, UMAC_INTERFACE_STA);
        data->vif_id = UMAC_INTERFACE_VIF_ID_INVALID;
        return status;
    }
    return MMWLAN_SUCCESS;
}

#if !(defined(MMWLAN_DPP_DISABLED) && MMWLAN_DPP_DISABLED)
enum mmwlan_status umac_connection_start_dpp(struct umac_data *umacd,
                                             const struct mmwlan_dpp_args *args)
{
    MMOSAL_DEV_ASSERT(args != NULL);
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (data->mode != UMAC_CONNECTION_MODE_NONE && data->mode != UMAC_CONNECTION_MODE_DPP)
    {
        return MMWLAN_UNAVAILABLE;
    }

    enum mmwlan_status status = umac_connection_start_interface(umacd, UMAC_SUPP_DPP_CONFIG_NAME);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    status = umac_supp_dpp_push_button(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    data->dpp_args.dpp_event_cb = args->dpp_event_cb;
    data->dpp_args.dpp_event_cb_arg = args->dpp_event_cb_arg;


    umac_ps_set_suspended(umacd, true);

    data->mode = UMAC_CONNECTION_MODE_DPP;
    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_connection_stop_dpp(struct umac_data *umacd)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (data->mode == UMAC_CONNECTION_MODE_NONE)
    {
        return MMWLAN_SUCCESS;
    }

    if (data->mode != UMAC_CONNECTION_MODE_DPP)
    {
        return MMWLAN_UNAVAILABLE;
    }

    umac_supp_dpp_push_button_stop(umacd);

    umac_supp_remove_sta_interface(umacd);


    umac_ps_set_suspended(umacd, false);

    data->dpp_args.dpp_event_cb = NULL;
    data->dpp_args.dpp_event_cb_arg = NULL;
    umac_interface_remove(umacd, UMAC_INTERFACE_STA);
    data->vif_id = UMAC_INTERFACE_VIF_ID_INVALID;
    data->mode = UMAC_CONNECTION_MODE_NONE;
    return MMWLAN_SUCCESS;
}

void umac_connection_handle_dpp_event(struct umac_data *umacd,
                                      const struct mmwlan_dpp_cb_args *event)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    if (data->dpp_args.dpp_event_cb != NULL)
    {
        data->dpp_args.dpp_event_cb(event, data->dpp_args.dpp_event_cb_arg);
    }
}

#endif


enum mmwlan_status umac_connection_reassoc(struct umac_data *umacd)
{
    enum mmwlan_status status;
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (data->mode != UMAC_CONNECTION_MODE_NONE && data->mode != UMAC_CONNECTION_MODE_STA)
    {
        return MMWLAN_UNAVAILABLE;
    }

    if (data->mode == UMAC_CONNECTION_MODE_NONE)
    {
        umac_datapath_configure_sta_mode(umacd);
        status = umac_interface_add(umacd, UMAC_INTERFACE_STA, NULL, &data->vif_id);
        if (status != MMWLAN_SUCCESS)
        {
            MMLOG_INF("Interface add failed\n");
            return status;
        }

        status = umac_supp_add_sta_interface(umacd, UMAC_SUPP_STA_CONFIG_NAME);
        if (status != MMWLAN_SUCCESS)
        {
            MMLOG_INF("Supplicant interface add failed\n");
            umac_interface_remove(umacd, UMAC_INTERFACE_STA);
            data->vif_id = UMAC_INTERFACE_VIF_ID_INVALID;
            return status;
        }

        data->mode = UMAC_CONNECTION_MODE_STA;

        status = umac_supp_connect(umacd);
    }
    else
    {
        status = umac_supp_reconnect(umacd);
    }

    return status;
}

enum mmwlan_status umac_connection_start(struct umac_data *umacd,
                                         const struct mmwlan_sta_args *args,
                                         mmwlan_sta_status_cb_t sta_status_cb,
                                         uint8_t *extra_assoc_ies)
{
    MMOSAL_DEV_ASSERT(args != NULL);
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    int ret;

    if (data->mode != UMAC_CONNECTION_MODE_NONE && data->mode != UMAC_CONNECTION_MODE_STA)
    {
        return MMWLAN_UNAVAILABLE;
    }

    const struct mmwlan_beacon_vendor_ie_filter *filter =
        umac_config_get_beacon_vendor_ie_filter(umacd);

    if (data->sta_args.extra_assoc_ies != NULL)
    {
        mmosal_free(data->sta_args.extra_assoc_ies);
        data->sta_args.extra_assoc_ies = NULL;
        data->sta_args.extra_assoc_ies_len = 0;
    }

    data->sta_status_cb = sta_status_cb;
    data->sta_args = *args;

    if (args->passphrase_len != 0)
    {
        data->sta_args.passphrase[args->passphrase_len] = '\0';
    }
    else
    {
        data->sta_args.passphrase_len = strlen((const char *)data->sta_args.ssid);
    }

    if (args->extra_assoc_ies_len != 0 && extra_assoc_ies != NULL)
    {
        data->sta_args.extra_assoc_ies = extra_assoc_ies;
    }

    enum mmwlan_status status = umac_connection_start_interface(umacd, UMAC_SUPP_STA_CONFIG_NAME);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    umac_stats_clear_connect_timestamps(umacd);
    umac_stats_update_connect_timestamp(umacd, MMWLAN_STATS_CONNECT_TIMESTAMP_START);

    if (filter != NULL)
    {
        ret = mmdrv_update_beacon_vendor_ie_filter(data->vif_id,
                                                   (const uint8_t *)filter->ouis,
                                                   filter->n_ouis);
        MMOSAL_ASSERT(ret == 0);
    }

    umac_offload_init(umacd, data->vif_id);


    status = umac_supp_connect(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    data->mode = UMAC_CONNECTION_MODE_STA;
    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_connection_stop(struct umac_data *umacd)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    MMLOG_DBG("Connection stop\n");

    if (data->mode == UMAC_CONNECTION_MODE_NONE)
    {
        return MMWLAN_SUCCESS;
    }

    if (data->mode != UMAC_CONNECTION_MODE_STA)
    {
        return MMWLAN_UNAVAILABLE;
    }

    if (data->sta_args.extra_assoc_ies != NULL)
    {
        mmosal_free(data->sta_args.extra_assoc_ies);
        data->sta_args.extra_assoc_ies = NULL;
        data->sta_args.extra_assoc_ies_len = 0;
    }


    umac_supp_disconnect(umacd);
    enum mmwlan_status status = umac_supp_remove_sta_interface(umacd);

    MMOSAL_DEV_ASSERT(status == MMWLAN_SUCCESS);
    umac_interface_remove(umacd, UMAC_INTERFACE_STA);
    data->vif_id = UMAC_INTERFACE_VIF_ID_INVALID;
    umac_rc_stop(data->stad);
    umac_rc_deinit(data->stad);

    umac_sta_data_set_aid(data->stad, 0);


    data->mode = UMAC_CONNECTION_MODE_NONE;
    return MMWLAN_SUCCESS;
}

const struct mmwlan_sta_args *umac_connection_get_sta_args(struct umac_data *umacd)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    if (data->is_initialised)
    {
        return &data->sta_args;
    }

    return NULL;
}

enum mmwlan_sta_state umac_connection_get_state(struct umac_data *umacd)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (!data->is_initialised || (data->mode != UMAC_CONNECTION_MODE_STA))
    {
        return MMWLAN_STA_DISABLED;
    }
    else if (data->conn_fsm.current_state == CONNECTION_FSM_STATE_CONNECTED)
    {
        return MMWLAN_STA_CONNECTED;
    }
    else
    {
        return MMWLAN_STA_CONNECTING;
    }
}

void umac_connection_roam(struct umac_data *umacd, const uint8_t *bssid)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    memcpy(data->sta_args.bssid, bssid, sizeof(data->sta_args.bssid));
    umac_supp_roam(umacd);
}

void umac_connection_handle_port_state(struct umac_data *umacd, bool authorized)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (authorized)
    {
        umac_connection_signal_sta_event(umacd, MMWLAN_STA_EVT_CTRL_PORT_OPEN);
        connection_fsm_handle_event(&data->conn_fsm, CONNECTION_FSM_EVENT_SUPP_PORT_OPEN);
        return;
    }
    else if (data->conn_fsm.current_state == CONNECTION_FSM_STATE_CONNECTED ||
             data->conn_fsm.current_state == CONNECTION_FSM_STATE_DISABLED)
    {
        connection_fsm_handle_event(&data->conn_fsm, CONNECTION_FSM_EVENT_DISCONNECT);
    }

    umac_connection_signal_sta_event(umacd, MMWLAN_STA_EVT_CTRL_PORT_CLOSED);
}

enum mmwlan_status umac_connection_register_link_cb(struct umac_data *umacd,
                                                    mmwlan_link_state_cb_t callback,
                                                    void *arg)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (data->is_initialised)
    {
        data->link_callback = callback;
        data->link_arg = arg;
        return MMWLAN_SUCCESS;
    }

    return MMWLAN_ERROR;
}

int umac_connection_get_ssid(struct umac_data *umacd, uint8_t *ssid)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    if (data->conn_fsm.current_state < CONNECTION_FSM_STATE_CONNECTING)
    {
        return 0;
    }

    memcpy(ssid, data->ssid, data->ssid_len);
    return data->ssid_len;
}

static bool default_bw_stepdown(const uint8_t max_bw, struct ie_s1g_operation *config)
{
    MMOSAL_DEV_ASSERT(max_bw >= config->primary_channel_width_mhz);
    MMOSAL_DEV_ASSERT(max_bw < config->operation_channel_width_mhz);

    if (abs((int)config->operating_channel_index - (int)config->primary_channel_number) >
        config->operation_channel_width_mhz)
    {
        MMLOG_ERR("Unable to step down bandwidth of channel %u for primary channel index %u\n",
                  config->operating_channel_index,
                  config->primary_channel_number);
        return false;
    }


    while (config->operation_channel_width_mhz > max_bw)
    {
        --config->operating_class;
        config->operation_channel_width_mhz /= 2;


        if (config->operating_channel_index > config->primary_channel_number)
        {
            config->operating_channel_index -= config->operation_channel_width_mhz;
        }
        else
        {
            config->operating_channel_index += config->operation_channel_width_mhz;
        }
    }
    return true;
}


#define IS_4MHZ_AU_REVMF_OPERATING_CLASS(x) (((x) == 39) || ((x) == 48))


#define IS_AU_REVMF_OPERATING_CLASS(x) \
    ((((x) == 39) || ((x) == 48)) || (((x) == 40) || ((x) == 49)))

static bool au_revmf_bw_stepdown(const uint8_t max_bw, struct ie_s1g_operation *config)
{
    MMOSAL_DEV_ASSERT(max_bw >= config->primary_channel_width_mhz);
    MMOSAL_DEV_ASSERT(max_bw < config->operation_channel_width_mhz);


    static const uint8_t low_pri_channel_num = 32;
    static const uint8_t upper_pri_channel_num = 46;
    if (config->primary_channel_number < low_pri_channel_num ||
        config->primary_channel_number > upper_pri_channel_num)
    {
        MMLOG_ERR("Unable to step down bandwidth of channel %u for primary channel index %u\n",
                  config->operating_channel_index,
                  config->primary_channel_number);
        return false;
    }


    while (config->operation_channel_width_mhz > max_bw)
    {
        if (IS_4MHZ_AU_REVMF_OPERATING_CLASS(config->operating_class))
        {

            config->operating_channel_index -= 16;
            config->operating_class = 24;
        }

        --config->operating_class;
        config->operation_channel_width_mhz /= 2;


        bool move_to_lower = false;
        if (config->operation_channel_width_mhz == 4)
        {

            static const uint8_t pri_center_chan_num = 39;
            move_to_lower = config->primary_channel_number < pri_center_chan_num;
        }
        else
        {
            move_to_lower = config->operating_channel_index > config->primary_channel_number;
        }

        if (move_to_lower)
        {
            config->operating_channel_index -= config->operation_channel_width_mhz;
        }
        else
        {
            config->operating_channel_index += config->operation_channel_width_mhz;
        }
    }
    return true;
}


#define IS_JP_OPERATING_CLASS(x) (((x >= 8) && (x <= 12)) || (x == 64) || (x == 65) || (x == 73))


#define IS_JP_CHANNEL_INDEX_1_MHZ(x) ((x == 9) || ((((x - 13) % 2) == 0) && (x >= 13) && (x <= 21)))

#define IS_JP_CHANNEL_INDEX_2_MHZ(x) (((x % 2) == 0) && (x >= 2) && (x <= 8))

#define IS_JP_CHANNEL_INDEX_4_MHZ(x) ((x == 36) || (x == 38))

#define IS_JP_CHANNEL_INDEX(x) \
    (IS_JP_CHANNEL_INDEX_1_MHZ(x) || IS_JP_CHANNEL_INDEX_2_MHZ(x) || IS_JP_CHANNEL_INDEX_4_MHZ(x))

static bool japan_bw_stepdown(const uint8_t max_bw, struct ie_s1g_operation *config)
{
    MMOSAL_DEV_ASSERT(max_bw >= config->primary_channel_width_mhz);
    MMOSAL_DEV_ASSERT(max_bw < config->operation_channel_width_mhz);
    MMOSAL_DEV_ASSERT(IS_JP_OPERATING_CLASS(config->operating_class));
    MMOSAL_DEV_ASSERT(IS_JP_CHANNEL_INDEX(config->operating_channel_index));
    if (!IS_JP_CHANNEL_INDEX(config->operating_channel_index))
    {
        MMLOG_ERR("Invalid channel index\n");
        return false;
    }


    if (config->primary_channel_width_mhz == max_bw)
    {
        config->operating_channel_index = config->primary_channel_number;
    }
    else if ((config->operation_channel_width_mhz == 4) &&
             (max_bw == 2) &&
             (config->primary_channel_width_mhz == 1))
    {
        switch (config->primary_channel_number)
        {
            case 13:
                config->operating_channel_index = 2;
                break;

            case 15:
                config->operating_channel_index = (config->operating_channel_index == 36) ? 2 : 4;
                break;

            case 17:
                config->operating_channel_index = (config->operating_channel_index == 36) ? 6 : 4;
                break;

            case 19:
                config->operating_channel_index = (config->operating_channel_index == 36) ? 6 : 8;
                break;

            case 21:
                config->operating_channel_index = 8;
                break;

            case 9:
            default:
                MMLOG_ERR("Unexpected primary channel number\n");
                return false;
        }
    }
    else
    {
        MMLOG_ERR("Unexpected channel configuration\n");
        return false;
    }

    config->operation_channel_width_mhz = max_bw;

    switch (config->operating_channel_index)
    {

        case 13:
        case 15:
        case 17:
        case 19:
        case 21:
            if (config->operation_channel_width_mhz != 1)
            {
                goto invalid_channel;
            }
            config->operating_class = (config->operating_class > 63) ? 73 : 8;
            break;


        case 2:
        case 6:
            if (config->operation_channel_width_mhz != 2)
            {
                goto invalid_channel;
            }
            config->operating_class = (config->operating_class > 63) ? 64 : 9;
            break;

        case 4:
        case 8:
            if (config->operation_channel_width_mhz != 2)
            {
                goto invalid_channel;
            }
            config->operating_class = (config->operating_class > 63) ? 65 : 10;
            break;


        case 38:
        case 36:
        default:
invalid_channel:
            MMLOG_ERR("Unexpected operating channel index\n");
            return false;
    }
    return true;
}


static bool restrict_channel_bandwidth_config(struct umac_data *umacd,
                                              struct ie_s1g_operation *config)
{
    const uint8_t max_bw = umac_interface_max_supported_bw(umacd);
    if (config->operation_channel_width_mhz <= max_bw)
    {

        return true;
    }

    if (config->primary_channel_width_mhz > max_bw)
    {

        MMLOG_WRN("Required primary channel width is greater than max width (%u > %u MHz)\n",
                  config->primary_channel_width_mhz,
                  max_bw);
        return false;
    }

    MMLOG_INF("Insufficient max bandwidth %u, modifying channel configuration: channel %u, "
              "width %u, op class %u, primary channel %u, primary width %u\n",
              max_bw,
              config->operating_channel_index,
              config->operation_channel_width_mhz,
              config->operating_class,
              config->primary_channel_number,
              config->primary_channel_width_mhz);

    bool success = false;
    if (IS_JP_OPERATING_CLASS(config->operating_class))
    {
        success = japan_bw_stepdown(max_bw, config);
    }
    else if (IS_AU_REVMF_OPERATING_CLASS(config->operating_class))
    {
        success = au_revmf_bw_stepdown(max_bw, config);
    }
    else
    {
        success = default_bw_stepdown(max_bw, config);
    }

    if (success)
    {
        MMLOG_INF("New channel configuration: channel %u, width %u, op class %u\n",
                  config->operating_channel_index,
                  config->operation_channel_width_mhz,
                  config->operating_class);
    }
    else
    {
        MMLOG_ERR("Unable to modify operating channel to reduced BW\n");
    }
    return success;
}

enum mmwlan_status umac_connection_set_bss_cfg(struct umac_data *umacd,
                                               const uint8_t *bssid,
                                               struct umac_connection_bss_cfg *config)
{
    int ret;
    bool success;
    enum mmwlan_status status = MMWLAN_ERROR;

    struct umac_connection_data *data = umac_data_get_connection(umacd);
    if (!data->is_initialised)
    {
        status = MMWLAN_UNAVAILABLE;
        goto exit;
    }


    success = restrict_channel_bandwidth_config(umacd, &config->channel_cfg);
    if (!success)
    {
        status = MMWLAN_CHANNEL_INVALID;
        goto exit;
    }

    umac_sta_data_set_bssid(data->stad, bssid);
    umac_sta_data_set_peer_addr(data->stad, bssid);
    memcpy(&data->bss_cfg.channel_cfg, &config->channel_cfg, sizeof(data->bss_cfg.channel_cfg));
    data->bss_cfg.beacon_interval = config->beacon_interval;

    status = umac_interface_set_channel(umacd, &data->bss_cfg.channel_cfg);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to set channel\n");
        goto exit;
    }

    ret = mmdrv_cfg_bss(data->vif_id, data->bss_cfg.beacon_interval, 0, 0);
    if (ret)
    {
        MMLOG_WRN("Failed to set bss configuration, %d\n", ret);
        status = MMWLAN_ERROR;
        goto exit;
    }

exit:
    return status;
}

enum mmwlan_status umac_connection_get_bssid(struct umac_data *umacd, uint8_t *bssid)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    if (data->conn_fsm.current_state >= CONNECTION_FSM_STATE_AUTHENTICATING)
    {
        umac_sta_data_get_bssid(data->stad, bssid);
        return MMWLAN_SUCCESS;
    }
    return MMWLAN_UNAVAILABLE;
}

bool umac_connection_bss_is_configured(struct umac_data *umacd)
{
    uint8_t bssid[MMWLAN_MAC_ADDR_LEN] = { 0 };
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    umac_sta_data_get_bssid(data->stad, bssid);
    return !mm_mac_addr_is_zero(bssid);
}

bool umac_connection_addr_matches_bssid(struct umac_data *umacd, const uint8_t *addr)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    if (data->conn_fsm.current_state >= CONNECTION_FSM_STATE_AUTHENTICATING)
    {
        return umac_sta_data_matches_bssid(data->stad, addr);
    }

    return false;
}

enum mmwlan_status umac_connection_get_aid(struct umac_data *umacd, uint16_t *aid)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (data->conn_fsm.current_state == CONNECTION_FSM_STATE_CONNECTED)
    {
        *aid = umac_sta_data_get_aid(data->stad);
        return MMWLAN_SUCCESS;
    }
    return MMWLAN_UNAVAILABLE;
}

void umac_connection_set_drv_qos_cfg_default(struct umac_data *umacd)
{
    const struct mmwlan_qos_queue_params *all_queue_params =
        umac_config_get_default_qos_queue_params(umacd);
    int aci;
    for (aci = 0; aci < MMWLAN_QOS_QUEUE_NUM_ACIS; aci++)
    {
        const struct mmwlan_qos_queue_params *queue_params = &all_queue_params[aci];
        int ret = mmdrv_cfg_qos_queue(queue_params);
        if (ret != 0)
        {
            MMLOG_WRN("Failed to configue QoS queue %d (ret=%d)\n", aci, ret);
            MMOSAL_DEV_ASSERT(false);
        }
        else
        {
            MMLOG_DBG("ACI %u: AIFS=%u, CWmin=%u, CWmax=%u, TXOP limit=%lu us\n",
                      queue_params->aci,
                      queue_params->aifs,
                      queue_params->cw_min,
                      queue_params->cw_max,
                      queue_params->txop_max_us);
        }
    }
}


static void umac_connection_update_drv_qos_cfg(const struct dot11_ie_wmm_param *wmm_ie)
{
    for (uint8_t aci = 0; aci < DOT11_ACI_NUM_ACS; aci++)
    {
        struct mmwlan_qos_queue_params queue_params = { 0 };
        const struct dot11_ac_parameter_record *rec = &(wmm_ie->ac_parameter_records[aci]);


        if (aci != dot11_aci_aifsn_get_aci(rec->aci_aifsn))
        {
            MMLOG_WRN("Record does not match expected ACI, skipping ACI %d.\n", aci);
            continue;
        }

        queue_params.aci = aci;
        queue_params.aifs = dot11_aci_aifsn_get_aifsn(rec->aci_aifsn);
        queue_params.cw_min = (1u << dot11_ecw_minmax_get_ecwmin(rec->ecw_minmax)) - 1;
        queue_params.cw_max = (1u << dot11_ecw_minmax_get_ecwmax(rec->ecw_minmax)) - 1;

        queue_params.txop_max_us = (uint32_t)(rec->txop_limit) * 32;

        int ret = mmdrv_cfg_qos_queue(&queue_params);
        if (ret != 0)
        {
            MMLOG_WRN("Failed to configue QoS queue %u (ret=%d)\n", aci, ret);
            MMOSAL_DEV_ASSERT(false);
        }
        else
        {
            MMLOG_DBG("ACI %u: AIFS=%u, CWmin=%u, CWmax=%u, TXOP limit=%lu us\n",
                      queue_params.aci,
                      queue_params.aifs,
                      queue_params.cw_min,
                      queue_params.cw_max,
                      queue_params.txop_max_us);
        }
    }
}


static void umac_connection_clear_cache_assoc_req(struct umac_connection_data *data)
{
    if (data->assoc_req_cache != NULL)
    {
        MMLOG_DBG("Clearing association request cache.\n");
        mmpkt_release(data->assoc_req_cache);
        data->assoc_req_cache = NULL;
    }
}

static void umac_connection_assoc_reassoc_req_retry(void *arg1, void *arg2)
{
    MM_UNUSED(arg2);
    struct umac_data *umacd = (struct umac_data *)arg1;
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    struct mmpkt *txbuf;
    enum mmwlan_status status;
    MMOSAL_ASSERT(data->is_initialised);

    if (data->assoc_req_cache == NULL)
    {
        MMLOG_DBG("Unable to send scheduled Assoc req as cache is empty.\n");
        goto error;
    }

    txbuf = umac_datapath_copy_tx_mmpkt(data->assoc_req_cache, MMDRV_PKT_CLASS_MGMT);
    if (txbuf == NULL)
    {
        MMLOG_DBG("Failed to alloc new buffer, falling back to cached frame.\n");
        txbuf = data->assoc_req_cache;
        data->assoc_req_cache = NULL;
    }

    status = umac_datapath_tx_mgmt_frame(data->stad, txbuf);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Error (%u) sending scheduled Assoc req.\n", status);
        goto error;
    }

    return;

error:

    umac_connection_clear_cache_assoc_req(data);
}


static bool umac_handle_assoc_refused_temporarily(struct umac_data *umacd,
                                                  struct frame_data_assoc_rsp *frame_data)
{
    const struct dot11_ie_tie *timeout_interval =
        ie_timeout_interval_find(frame_data->ies, frame_data->ies_len);

    if (timeout_interval && timeout_interval->type == DOT11_TIE_ASSOC_COMEBACK)
    {

        uint32_t timeout = le32toh(timeout_interval->interval) * 1024 / 1000;

        MMLOG_DBG("Schedule Assoc Req. Comeback duration %lu TU (%lu ms)\n",
                  timeout_interval->interval,
                  timeout);
        if (umac_core_register_timeout(umacd,
                                       timeout,
                                       umac_connection_assoc_reassoc_req_retry,
                                       umacd,
                                       NULL))
        {

            return true;
        }
    }

    return false;
}

void umac_connection_process_assoc_reassoc_rsp(struct umac_data *umacd, struct mmpktview *rxbufview)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    MMOSAL_ASSERT(data->is_initialised);

    struct frame_data_assoc_rsp frame_data = { 0 };
    bool ok = frame_association_response_parse(rxbufview, &frame_data);
    if (!ok)
    {
        MMLOG_WRN("Received malformed Assoc/Reassoc response\n");
        return;
    }

    const struct dot11_ie_s1g_capabilities *s1g_caps =
        ie_s1g_capabilities_find(frame_data.ies, frame_data.ies_len);

    if ((frame_data.status_code == DOT11_STATUS_SUCCESS) && (s1g_caps == NULL))
    {
        MMLOG_WRN("Assoc/Reassoc response missing S1G Capabilities IE\n");
        return;
    }

    if (frame_data.status_code == DOT11_STATUS_REFUSED_TEMPORARILY)
    {
        ok = umac_handle_assoc_refused_temporarily(umacd, &frame_data);
        if (ok)
        {
            return;
        }

        MMLOG_WRN("No valid TIE with Assoc Comeback interval received\n");
    }

    umac_connection_clear_cache_assoc_req(data);

    if (frame_data.status_code == DOT11_STATUS_SUCCESS)
    {
        umac_stats_update_connect_timestamp(umacd, MMWLAN_STATS_CONNECT_TIMESTAMP_RECV_ASSOC);

        const struct dot11_ie_twt *twt_ie = ie_twt_find(frame_data.ies, frame_data.ies_len);

        if (twt_ie != NULL)
        {
            enum mmwlan_status status = umac_twt_process_ie(umacd, twt_ie);
            if (status != MMWLAN_SUCCESS)
            {
                MMLOG_WRN("TWT IE processing failed\n");
            }
        }

        umac_rc_start(data->stad,
                      ie_s1g_capabilities_get_sgi_flags(s1g_caps),
                      ie_s1g_capabilities_get_max_rx_mcs_for_1ss(s1g_caps));

        data->tx_traveling_pilots_supported =
            ie_s1g_capabilities_get_traveling_pilots_support(s1g_caps);

        data->non_tim_mode_supported =
            dot11_s1g_cap_info_4_get_non_tim_support(s1g_caps->s1g_capabilities_information[4]);
        if (data->non_tim_mode_supported && umac_config_is_non_tim_mode_enabled(umacd))
        {
            mmdrv_set_param(data->vif_id,
                            MORSE_PARAM_ID_NON_TIM_MODE,
                            data->non_tim_mode_supported);
        }

        data->ampdu_mss = dot11_s1g_cap_info_3_get_min_mpdu_start_spacing(
            s1g_caps->s1g_capabilities_information[3]);

        data->control_resp_1mhz_in_en = dot11_s1g_cap_info_7_get_1mhz_ctrl_rsp_preamble_support(
            s1g_caps->s1g_capabilities_information[7]);

        mmdrv_set_control_response_bw(data->vif_id,
                                      MMDRV_DIRECTION_INCOMING,
                                      data->control_resp_1mhz_in_en);

        const struct dot11_ie_wmm_param *wmm_ie =
            ie_wmm_param_find(frame_data.ies, frame_data.ies_len);

        if (wmm_ie != NULL)
        {
            umac_connection_update_drv_qos_cfg(wmm_ie);
        }

        const struct dot11_ie_morse_info *morse_ie =
            ie_morse_info_find(frame_data.ies, frame_data.ies_len);
        if (morse_ie != NULL)
        {
            if (morse_ie->cap0 & MORSE_VENDOR_IE_CAP0_SHORT_ACK_TIMEOUT)
            {

                mmdrv_set_param(data->vif_id,
                                MORSE_PARAM_ID_EXTRA_ACK_TIMEOUT_ADJUST_US,
                                MORSE_SHORT_ACK_TIMEOUT_ADJUST_US);
            }
            else
            {

                mmdrv_set_param(data->vif_id,
                                MORSE_PARAM_ID_EXTRA_ACK_TIMEOUT_ADJUST_US,
                                DEFAULT_MORSE_IBSS_ACK_TIMEOUT_ADJUST_US);
            }

            data->morse_mmss_offset = MORSE_VENDOR_IE_CAP0_GET_MMSS_OFFSET(morse_ie->cap0);
        }
        else
        {


            mmdrv_set_param(data->vif_id,
                            MORSE_PARAM_ID_EXTRA_ACK_TIMEOUT_ADJUST_US,
                            DEFAULT_MORSE_IBSS_ACK_TIMEOUT_ADJUST_US);

            data->morse_mmss_offset = 0;
        }

        const struct dot11_ie_aid_response *aid_resp =
            ie_aid_response_find(frame_data.ies, frame_data.ies_len);

        uint16_t aid = (aid_resp != NULL) ? le16toh(aid_resp->aid) : 0;
        umac_sta_data_set_aid(data->stad, aid);
    }

    umac_supp_process_assoc_reassoc_resp(umacd, &frame_data);
}


static void umac_connection_cache_assoc_req(struct umac_connection_data *data,
                                            struct mmpkt *assoc_req)
{
    if (data->assoc_req_cache != NULL)
    {
        MMLOG_WRN("Previous association request found, cleaning.\n");
        mmpkt_release(data->assoc_req_cache);
    }

    data->assoc_req_cache = assoc_req;
}

enum mmwlan_status umac_connection_process_assoc_req(struct umac_data *umacd,
                                                     struct frame_data_assoc_req *params)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    struct mmpkt *tx_copy = NULL;
    enum mmwlan_status status =
        umac_datapath_build_copy_and_queue_mgmt_frame_tx(data->stad,
                                                         frame_association_request_build,
                                                         params,
                                                         &tx_copy);

    umac_stats_update_connect_timestamp(umacd, MMWLAN_STATS_CONNECT_TIMESTAMP_SEND_ASSOC);

    if (status == MMWLAN_SUCCESS)
    {
        umac_connection_cache_assoc_req(data, tx_copy);
        connection_fsm_handle_event(&data->conn_fsm, CONNECTION_FSM_EVENT_ASSOC_REQUEST);
        data->ssid_len = params->ssid_len;
        memcpy(data->ssid, params->ssid, data->ssid_len);
    }

    return status;
}

void umac_connection_process_disassoc_req(struct umac_data *umacd, struct mmpktview *rxbufview)
{
    MM_UNUSED(rxbufview);
    umac_supp_process_disassoc_req(umacd, 0);
}


static void umac_connection_timestamp_auth_frame(struct umac_data *umacd,
                                                 const struct frame_data_auth *auth,
                                                 bool is_tx)
{
    int32_t seq_num = frame_authentication_get_seq_num(auth, is_tx);
    switch (seq_num)
    {
        case 1:
            umac_stats_update_connect_timestamp(umacd,
                                                is_tx ? MMWLAN_STATS_CONNECT_TIMESTAMP_SEND_AUTH_1 :
                                                        MMWLAN_STATS_CONNECT_TIMESTAMP_RECV_AUTH_1);
            break;

        case 2:
            umac_stats_update_connect_timestamp(umacd,
                                                is_tx ? MMWLAN_STATS_CONNECT_TIMESTAMP_SEND_AUTH_2 :
                                                        MMWLAN_STATS_CONNECT_TIMESTAMP_RECV_AUTH_2);
            break;

        default:
            break;
    }
}

void umac_connection_process_auth_resp(struct umac_data *umacd, struct mmpktview *rxbufview)
{
    struct frame_data_auth frame_data = { 0 };
    bool ok = frame_authentication_parse(rxbufview, &frame_data);
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (ok)
    {

        umac_sta_data_set_aid(data->stad, 0);
        umac_connection_timestamp_auth_frame(umacd, &frame_data, false);

        umac_datapath_set_filter_all_beacons(umacd, true);
        umac_supp_process_auth_resp(umacd, &frame_data);

        umac_datapath_set_filter_all_beacons(umacd, false);
    }
}

enum mmwlan_status umac_connection_process_auth_req(struct umac_data *umacd,
                                                    struct frame_data_auth *params)
{
    enum mmwlan_status status;
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    umac_connection_timestamp_auth_frame(umacd, params, true);

    status = umac_datapath_build_and_tx_mgmt_frame(data->stad, frame_authentication_build, params);
    if (status != MMWLAN_SUCCESS)
    {
        goto exit;
    }

    connection_fsm_handle_event(&data->conn_fsm, CONNECTION_FSM_EVENT_AUTH_REQUEST);

exit:
    return status;
}

void umac_connection_process_deauth_rx(struct umac_data *umacd, struct mmpktview *rxbufview)
{
    MM_UNUSED(rxbufview);
    umac_supp_process_deauth(umacd);
}

enum mmwlan_status umac_connection_process_deauth_tx(struct umac_data *umacd,
                                                     struct frame_data_deauth *params)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    enum mmwlan_status status =
        umac_datapath_build_and_tx_mgmt_frame(data->stad, frame_deauthentication_build, params);


    connection_fsm_handle_event(&data->conn_fsm, CONNECTION_FSM_EVENT_DISCONNECT);

    return status;
}


static enum mmwlan_status umac_connection_set_sta_state(struct umac_connection_data *data,
                                                        enum morse_sta_state state)
{
    uint16_t aid = umac_sta_data_get_aid(data->stad);
    const uint8_t *bssid = umac_sta_data_peek_bssid(data->stad);

    int ret = mmdrv_update_sta_state(data->vif_id, aid, bssid, state);
    if (ret)
    {
        MMLOG_DBG("Unable to update sta state in mmdrv (ret: %d).\n", ret);
        return MMWLAN_ERROR;
    }
    else
    {
        return MMWLAN_SUCCESS;
    }
}

static void umac_connection_process_failed_ap_query(struct umac_connection_data *data)
{
    MMLOG_INF("Check for AP failed\n");
    data->conn_mon.failed_ap_queries++;
    if (data->conn_mon.failed_ap_queries >= UMAC_CONNECTION_DEAUTH_THRESHOLD_AP_QUERIES)
    {
        connection_mon_fsm_handle_event(&data->conn_mon.fsm, CONNECTION_MON_FSM_EVENT_UNRESPONSIVE);
    }
}

void umac_connection_handle_ack_status(struct mmpkt *mmpkt, struct umac_data *umacd, bool acked)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);


    if ((data->conn_mon.fsm.current_state != CONNECTION_MON_FSM_STATE_STABLE) &&
        (data->conn_mon.fsm.current_state != CONNECTION_MON_FSM_STATE_UNSTABLE))
    {
        return;
    }

    if (data->conn_mon.disabled)
    {
        return;
    }

    if (acked)
    {
        connection_mon_fsm_handle_event(&data->conn_mon.fsm, CONNECTION_MON_FSM_EVENT_ACKED);
    }
    else
    {
        struct mmpktview *view = mmpkt_open(mmpkt);
        const struct dot11_hdr *hdr = (const struct dot11_hdr *)mmpkt_get_data_start(view);
        uint16_t frame_ver_type_subtype =
            dot11_frame_control_get_ver_type_subtype(hdr->frame_control);
        bool is_qos_null = (frame_ver_type_subtype == DOT11_VER_TYPE_SUBTYPE(0, DATA, QOS_NULL));
        mmpkt_close(&view);

        if (data->conn_mon.fsm.current_state == CONNECTION_MON_FSM_STATE_UNSTABLE && is_qos_null)
        {
            umac_connection_process_failed_ap_query(data);
        }
    }
}

void umac_connection_handle_beacon_loss(struct umac_data *umacd)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if ((data->conn_mon.fsm.current_state != CONNECTION_MON_FSM_STATE_STABLE) &&
        (data->conn_mon.fsm.current_state != CONNECTION_MON_FSM_STATE_UNSTABLE))
    {
        return;
    }

    if (data->conn_mon.disabled)
    {
        return;
    }

    MMLOG_INF("Beacon loss occured\n");
    connection_mon_fsm_handle_event(&data->conn_mon.fsm, CONNECTION_MON_FSM_EVENT_BEACON_LOSS);
}

void umac_connection_set_monitor_disable(struct umac_data *umacd, bool disable)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (data->conn_mon.disabled == disable)
    {
        return;
    }

    if (data->conn_fsm.current_state != CONNECTION_FSM_STATE_CONNECTED)
    {

        data->conn_mon.disabled = disable;
        MMOSAL_DEV_ASSERT(data->conn_mon.fsm.current_state == CONNECTION_MON_FSM_STATE_DISABLED);
        return;
    }

    if (disable)
    {
        connection_mon_fsm_handle_event(&data->conn_mon.fsm, CONNECTION_MON_FSM_EVENT_STOP);
    }
    else
    {
        connection_mon_fsm_handle_event(&data->conn_mon.fsm, CONNECTION_MON_FSM_EVENT_START);
    }
    data->conn_mon.disabled = disable;
}

void umac_connection_set_signal_monitor(struct umac_data *umacd,
                                        int16_t threshold,
                                        int16_t hysteresis)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    data->conn_signal_mon.signal_threshold_dbm = threshold;
    data->conn_signal_mon.signal_hysteresis_dbm = hysteresis;
}

enum umac_connection_signal_change umac_connection_check_signal_change(struct umac_data *umacd,
                                                                       int16_t rssi)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    int16_t delta_rssi = (rssi - data->conn_signal_mon.signal_threshold_dbm);
    int16_t abs_delta_rssi = ((delta_rssi) > 0 ? (delta_rssi) : -(delta_rssi));

    bool current_state = (delta_rssi > 0 ? true : false);

    if ((data->conn_signal_mon.signal_threshold_dbm != 0) &&
        (abs_delta_rssi >= data->conn_signal_mon.signal_hysteresis_dbm) &&
        (current_state != data->conn_signal_mon.is_above_threshold))
    {
        data->conn_signal_mon.is_above_threshold = current_state;
        if (current_state)
        {
            return UMAC_CONNECTION_SIGNAL_CHANGE_ABOVE_THRESHOLD;
        }
        else
        {
            return UMAC_CONNECTION_SIGNAL_CHANGE_BELOW_THRESHOLD;
        }
    }

    return UMAC_CONNECTION_SIGNAL_CHANGE_NO_CHANGE;
}

enum mmwlan_status umac_connection_update_beacon_vendor_ie_filter(
    struct umac_data *umacd,
    const struct mmwlan_beacon_vendor_ie_filter *filter)
{
    int ret = 0;
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    umac_config_set_beacon_vendor_ie_filter(umacd, filter);

    if (data->conn_fsm.current_state == CONNECTION_FSM_STATE_CONNECTED)
    {
        if (filter == NULL)
        {
            ret = mmdrv_update_beacon_vendor_ie_filter(data->vif_id, NULL, 0);
        }
        else
        {
            ret = mmdrv_update_beacon_vendor_ie_filter(data->vif_id,
                                                       (const uint8_t *)filter->ouis,
                                                       filter->n_ouis);
        }
    }

    if (!ret)
    {

        umac_config_set_beacon_vendor_ie_filter(umacd, filter);
    }

    switch (ret)
    {
        case 0:
            return MMWLAN_SUCCESS;

        case -ENODEV:
            return MMWLAN_UNAVAILABLE;

        case -ENOSPC:
            return MMWLAN_INVALID_ARGUMENT;

        case -ENOMEM:
            return MMWLAN_NO_MEM;

        case -ETIMEDOUT:
            return MMWLAN_TIMED_OUT;

        default:
            return MMWLAN_ERROR;
    }
}

void umac_connection_beacon_vendor_ie_filter_process(struct umac_data *umacd,
                                                     const uint8_t *ies,
                                                     uint32_t ies_len)
{
    enum ie_result result_code;
    int ii;
    const struct mmwlan_beacon_vendor_ie_filter *filter =
        umac_config_get_beacon_vendor_ie_filter(umacd);

    if (filter == NULL)
    {

        return;
    }

    for (ii = 0; ii < filter->n_ouis; ii++)
    {

        (void)
            ie_vendor_specific_find(ies, ies_len, filter->ouis[ii], MMWLAN_OUI_SIZE, &result_code);
        if (result_code == IE_FOUND)
        {
            MMLOG_DBG("Vendor IE found, OUI: 0x%02x%02x%02x\n",
                      filter->ouis[ii][0],
                      filter->ouis[ii][1],
                      filter->ouis[ii][2]);
            filter->cb(ies, ies_len, filter->cb_arg);
            break;
        }
    }
}

static void umac_connection_handle_ecsa(void *arg1, void *arg2)
{
    MM_UNUSED(arg2);
    struct umac_data *umacd = (struct umac_data *)arg1;
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    const struct ie_s1g_operation *new_s1g_info = &data->ecsa_s1g_info;
    const struct ie_s1g_operation *old_s1g_info =
        umac_interface_get_current_s1g_operation_info(umacd);

    enum mmwlan_status status = umac_interface_set_channel(umacd, new_s1g_info);


    data->ecsa_active = false;
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_ERR("Channel switch failed with status code %u\n", status);
        goto exit;
    }

    MMLOG_INF("Succesfully switched to new operating channel %u after ECSA\n",
              new_s1g_info->operating_channel_index);


    if (new_s1g_info->operation_channel_width_mhz != old_s1g_info->operation_channel_width_mhz)
    {
        struct umac_sta_data *stad = umac_connection_get_stad(umacd);
        umac_rc_stop(stad);
        struct umac_rc_sta_data *stad_rc = umac_sta_data_get_rc(stad);
        uint8_t max_mcs = BIT_COUNT(stad_rc->active_capabilities.rates) - 1;
        umac_rc_start(stad, stad_rc->active_capabilities.sgi_per_bw, max_mcs);
        MMLOG_INF("Restarted rate table after switching to new operating bandwidth %u MHz\n",
                  new_s1g_info->operation_channel_width_mhz);
    }

exit:
    umac_datapath_unpause(umacd, UMAC_DATAPATH_PAUSE_SOURCE_ECSA);
}


static void umac_connection_extract_s1g_info_from_ecsa(
    struct ie_s1g_operation *new_s1g_info,
    const struct dot11_ie_ecsa *ecsa_ie,
    const struct dot11_ie_wide_bw_chan_switch *wide_bw_chan_switch_ie)
{
    new_s1g_info->primary_channel_number = ecsa_ie->new_channel_number;
    new_s1g_info->operating_class = ecsa_ie->new_operating_class;

    if (wide_bw_chan_switch_ie == NULL)
    {
        new_s1g_info->operation_channel_width_mhz = 1;
        new_s1g_info->operating_channel_index = ecsa_ie->new_channel_number;
        new_s1g_info->primary_channel_width_mhz = 1;
        new_s1g_info->primary_1mhz_channel_loc = 0;
        new_s1g_info->recommend_no_mcs10 = 0;
    }
    else
    {
        new_s1g_info->operating_channel_index =
            wide_bw_chan_switch_ie->new_channel_centre_frequency_seg0;


        new_s1g_info->primary_channel_width_mhz =
            dot11_s1g_op_chan_width_get_pri_chan_width(wide_bw_chan_switch_ie->new_channel_width) ?
                1 :
                2;

        new_s1g_info->operation_channel_width_mhz =
            dot11_s1g_op_chan_width_get_op_chan_width(wide_bw_chan_switch_ie->new_channel_width) +
            1;
        new_s1g_info->primary_1mhz_channel_loc =
            dot11_s1g_op_chan_width_get_pri_chan_loc(wide_bw_chan_switch_ie->new_channel_width);
        new_s1g_info->recommend_no_mcs10 =
            dot11_s1g_op_chan_width_get_no_mcs10(wide_bw_chan_switch_ie->new_channel_width);
    }
}


static void umac_connection_process_ecsa(
    struct umac_data *umacd,
    const struct dot11_ie_ecsa *ecsa_ie,
    const struct dot11_ie_wide_bw_chan_switch *wide_bw_chan_switch_ie,
    const uint32_t long_beacon_interval_ms)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    MMLOG_INF("Received ECSA with count %u\n", ecsa_ie->channel_switch_count);

    if (data->ecsa_active)
    {
        return;
    }
    umac_connection_extract_s1g_info_from_ecsa(&data->ecsa_s1g_info,
                                               ecsa_ie,
                                               wide_bw_chan_switch_ie);


    uint32_t timeout_ms = 0;

    if (ecsa_ie->channel_switch_count > 1)
    {
        timeout_ms = long_beacon_interval_ms * (ecsa_ie->channel_switch_count - 1);
    }
    MMLOG_INF("Scheduling channel switch in %ums\n", timeout_ms);
    if (umac_core_register_timeout(umacd,
                                   timeout_ms,
                                   umac_connection_handle_ecsa,
                                   (void *)umacd,
                                   NULL))
    {

        if (ecsa_ie->channel_switch_mode)
        {
            umac_datapath_pause(umacd, UMAC_DATAPATH_PAUSE_SOURCE_ECSA);
        }
        data->ecsa_active = true;
    }
    else
    {
        MMLOG_ERR("Failed to register timeout for ECSA\n");
    }
}

void umac_connection_process_beacon_ies(struct umac_data *umacd,
                                        const uint8_t *ies,
                                        uint32_t ies_len)
{

    const struct dot11_ie_ecsa *ecsa_ie = ie_ecsa_find(ies, ies_len);
    if (ecsa_ie != NULL)
    {
        MMLOG_INF("ECSA received\n");

        const struct dot11_ie_channel_switch_wrapper *chan_sw_wrapper_ie =
            ie_chan_switch_wrapper_find(ies, ies_len);
        const struct dot11_ie_wide_bw_chan_switch *wide_bw_chan_switch_ie = NULL;

        if (chan_sw_wrapper_ie != NULL)
        {
            wide_bw_chan_switch_ie = ie_wide_bw_chan_switch_find(chan_sw_wrapper_ie->sub_elements,
                                                                 chan_sw_wrapper_ie->header.length);
        }

        const struct dot11_ie_s1g_beacon_compatibility *s1g_beacon_compat =
            ie_s1g_beacon_compat_find(ies, ies_len);
        if (s1g_beacon_compat == NULL)
        {
            MMLOG_ERR("Unable to extract long beacon interval for ECSA due to missing S1G Beacon "
                      "Compatibility IE\n");
            return;
        }
        uint32_t long_beacon_int_ms = dot11_convert_tus_to_ms(s1g_beacon_compat->beacon_int);

        umac_connection_process_ecsa(umacd, ecsa_ie, wide_bw_chan_switch_ie, long_beacon_int_ms);
    }


    umac_connection_beacon_vendor_ie_filter_process(umacd, ies, ies_len);
}

void umac_connection_populate_tx_metadata(struct umac_data *umacd,
                                          struct mmdrv_tx_metadata *tx_metadata)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (data->tx_traveling_pilots_supported)
    {
        tx_metadata->flags |= MMDRV_TX_FLAG_TP_ENABLED;
    }

    if (umac_interface_get_control_response_bw_1mhz_out_enabled(umacd))
    {
        tx_metadata->flags |= MMDRV_TX_FLAG_CR_1MHZ_PRE_ENABLED;
    }

    tx_metadata->ampdu_mss = data->ampdu_mss;
    tx_metadata->mmss_offset = data->morse_mmss_offset;
}


static void umac_connection_monitor_check(void *arg1, void *arg2)
{
    MM_UNUSED(arg2);
    struct umac_data *umacd = (struct umac_data *)arg1;
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (data->conn_mon.fsm.current_state != CONNECTION_MON_FSM_STATE_UNSTABLE)
    {
        return;
    }

    (void)umac_core_register_timeout(umacd,
                                     UMAC_CONNECTION_MON_UNSTABLE_INTERVAL_MS,
                                     umac_connection_monitor_check,
                                     umacd,
                                     NULL);


    enum mmwlan_status status = umac_datapath_build_and_tx_to_ds_qos_null_frame(data->stad);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to TX connection monitor frame.\n");
        umac_connection_process_failed_ap_query(data);
    }
}

void umac_connection_handle_hw_restarted(struct umac_data *umacd)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    int ret;
    enum mmwlan_status status;
    const struct mmwlan_beacon_vendor_ie_filter *filter =
        umac_config_get_beacon_vendor_ie_filter(umacd);

    if (data->vif_id != UMAC_INTERFACE_VIF_ID_INVALID)
    {
        uint16_t aid = umac_sta_data_get_aid(data->stad);
        const uint8_t *bssid = umac_sta_data_peek_bssid(data->stad);

        MMLOG_DBG("Reinstall VIF\n");
        umac_interface_reinstall_vif(umacd, UMAC_INTERFACE_STA, &data->vif_id);
        ret = mmdrv_update_sta_state(data->vif_id, aid, bssid, MORSE_STA_NONE);
        MMOSAL_ASSERT(ret == 0);

        MMLOG_DBG("Reconfigure channel\n");
        status = umac_interface_reconfigure_channel(umacd);
        MMOSAL_ASSERT(status == MMWLAN_SUCCESS);

        MMLOG_DBG("Reinstall keys\n");
        umac_keys_reinstall_keys(data->stad, data->vif_id);

        if (data->conn_fsm.current_state == CONNECTION_FSM_STATE_CONNECTED)
        {
            MMLOG_DBG("Set STA state\n");
            ret = mmdrv_update_sta_state(data->vif_id, aid, bssid, MORSE_STA_AUTHORIZED);
            MMOSAL_ASSERT(ret == 0);


            if (filter != NULL)
            {
                ret = mmdrv_update_beacon_vendor_ie_filter(data->vif_id,
                                                           (const uint8_t *)filter->ouis,
                                                           filter->n_ouis);
                MMOSAL_ASSERT(ret == 0);
            }

            umac_wnm_sleep_report_event(umacd, UMAC_WNM_SLEEP_EVENT_HW_RESTARTED);
        }

        if (data->conn_fsm.current_state >= CONNECTION_FSM_STATE_AUTHENTICATING)
        {
            MMLOG_DBG("Configure BSS\n");
            ret = mmdrv_cfg_bss(data->vif_id, data->bss_cfg.beacon_interval, 0, 0);
            MMOSAL_ASSERT(ret == 0);
            ret = mmdrv_set_control_response_bw(data->vif_id,
                                                MMDRV_DIRECTION_INCOMING,
                                                data->control_resp_1mhz_in_en);
            MMOSAL_ASSERT(ret == 0);
        }

        MMLOG_DBG("Restore power save state\n");
        umac_ps_handle_hw_restarted(umacd);

        MMLOG_DBG("Reconfigure fragmentation threshold\n");
        unsigned fragment_threshold = umac_config_get_frag_threshold(umacd);
        if (mmdrv_set_frag_threshold(fragment_threshold))
        {
            MMLOG_WRN("Failed to reconfigure fragmentation threshold.\n");
        }

        if (data->non_tim_mode_supported && umac_config_is_non_tim_mode_enabled(umacd))
        {
            MMLOG_DBG("Re-enable Non-TIM mode\n");
            mmdrv_set_param(data->vif_id,
                            MORSE_PARAM_ID_NON_TIM_MODE,
                            data->non_tim_mode_supported);
        }

        const struct mmwlan_twt_config_args *twt_config = umac_twt_get_config(umacd);
        if ((twt_config->twt_mode == MMWLAN_TWT_REQUESTER) &&
            (data->conn_fsm.current_state == CONNECTION_FSM_STATE_CONNECTED))
        {
            status = umac_twt_install_pending_agreements(umacd, true);
            if (status != MMWLAN_SUCCESS)
            {
                MMLOG_ERR("Failed to install TWT agreement.\n");
            }
        }


        umac_offload_restore_all(umacd);

        umac_datapath_handle_hw_restarted(umacd, data->stad);
    }
}

void umac_connection_set_sta_autoconnect(struct umac_data *umacd,
                                         enum mmwlan_sta_autoconnect_mode mode)
{
    umac_supp_set_auto_reconnect_disabled(umacd, (mode == MMWLAN_STA_AUTOCONNECT_DISABLED));
}

struct umac_sta_data *umac_connection_get_stad(struct umac_data *umacd)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    if (data->vif_id != UMAC_INTERFACE_VIF_ID_INVALID)
    {
        return data->stad;
    }
    else
    {
        MMLOG_WRN("stad requested before interface added\n");
        return NULL;
    }
}

uint16_t umac_connection_get_vif_id(struct umac_data *umacd)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    return data->vif_id;
}

static void invoke_link_callback(struct umac_data *umacd,
                                 struct umac_connection_data *data,
                                 enum mmwlan_link_state state)
{
    struct mmwlan_vif_state vif_state = {
        .vif = MMWLAN_VIF_STA,
        .link_state = state,
    };
    bool callback_invoked = umac_interface_invoke_vif_state_cb(umacd, &vif_state);

    if (data->link_callback)
    {
        data->link_callback(state, data->link_arg);
        callback_invoked = true;
    }


    if (!callback_invoked)
    {
        MMLOG_ERR("Link/VIF status callback not registered.\n");
    }
}




#define CONNECTION_MON_FSM_LOG(format_str, ...) MMLOG_DBG(format_str, __VA_ARGS__)
#include "connection_mon_fsm.def"

static void connection_mon_fsm_stable_entry(struct connection_mon_fsm_instance *inst,
                                            enum connection_mon_fsm_state prev_state)
{
    MM_UNUSED(prev_state);
    struct umac_data *umacd = (struct umac_data *)inst->arg;
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    data->conn_mon.failed_ap_queries = 0;
}

static void connection_mon_fsm_unstable_entry(struct connection_mon_fsm_instance *inst,
                                              enum connection_mon_fsm_state prev_state)
{
    MM_UNUSED(prev_state);
    struct umac_data *umacd = (struct umac_data *)inst->arg;

    if (!umac_core_is_timeout_registered(umacd, umac_connection_monitor_check, umacd, NULL))
    {
        (void)umac_core_register_timeout(umacd,
                                         UMAC_CONNECTION_MON_UNSTABLE_INTERVAL_MS,
                                         umac_connection_monitor_check,
                                         umacd,
                                         NULL);
    }
}

static void connection_mon_fsm_unstable_exit(struct connection_mon_fsm_instance *inst,
                                             enum connection_mon_fsm_event event)
{
    MM_UNUSED(event);
    struct umac_data *umacd = (struct umac_data *)inst->arg;

    (void)umac_core_cancel_timeout(umacd, umac_connection_monitor_check, umacd, NULL);
}

static void connection_mon_fsm_lost_entry(struct connection_mon_fsm_instance *inst,
                                          enum connection_mon_fsm_state prev_state)
{
    MM_UNUSED(prev_state);
    struct umac_data *umacd = (struct umac_data *)inst->arg;

    umac_supp_process_deauth(umacd);
}



static void connection_mon_fsm_transition_error(struct connection_mon_fsm_instance *inst,
                                                enum connection_mon_fsm_event event)
{
    MMLOG_INF("connection_mon_fsm: invalid event %u (%s) in state %u (%s)\n",
              event,
              connection_mon_fsm_event_tostr(event),
              inst->current_state,
              connection_mon_fsm_state_tostr(inst->current_state));
    MMOSAL_DEV_ASSERT_LOG_DATA(false, inst->current_state, event);
}

static void connection_mon_fsm_reentrance_error(struct connection_mon_fsm_instance *inst)
{
    MMLOG_INF("connection_mon_fsm: invalid reentrance in state %u (%s)\n",
              inst->current_state,
              connection_mon_fsm_state_tostr(inst->current_state));
    MMOSAL_ASSERT_LOG_DATA(false, inst->current_state);
}




#define CONNECTION_FSM_LOG(format_str, ...) MMLOG_DBG(format_str, __VA_ARGS__)
#include "connection_fsm.def"



static void connection_fsm_disconnected_entry(struct connection_fsm_instance *inst,
                                              enum connection_fsm_state prev_state)
{
    struct umac_data *umacd = (struct umac_data *)inst->arg;
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    if (prev_state != CONNECTION_FSM_STATE_DISCONNECTED)
    {
        enum mmwlan_status result = umac_connection_set_sta_state(data, MORSE_STA_NONE);
        if (result != MMWLAN_SUCCESS)
        {
            MMLOG_WRN("Unable to set sta state.\n");
        }

        umac_datapath_stad_flush(umacd, data->stad);

        memset(&data->bss_cfg, 0, sizeof(data->bss_cfg));
        umac_sta_data_set_bssid(data->stad, mac_addr_zero);
        umac_sta_data_set_peer_addr(data->stad, mac_addr_zero);

        umac_ba_deinit(data->stad);

        umac_connection_set_drv_qos_cfg_default(umacd);


        umac_ps_set_suspended(umacd, false);
    }
}

static void connection_fsm_disconnected_exit(struct connection_fsm_instance *inst,
                                             enum connection_fsm_event event)
{
    MM_UNUSED(event);
    struct umac_data *umacd = (struct umac_data *)inst->arg;
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (data->sta_status_cb)
    {
        data->sta_status_cb(MMWLAN_STA_CONNECTING);
    }
}

static void connection_fsm_authenticating_entry(struct connection_fsm_instance *inst,
                                                enum connection_fsm_state prev_state)
{
    MM_UNUSED(prev_state);
    struct umac_data *umacd = (struct umac_data *)inst->arg;


    umac_ps_set_suspended(umacd, true);
}

static void connection_fsm_connecting_entry(struct connection_fsm_instance *inst,
                                            enum connection_fsm_state prev_state)
{
    MM_UNUSED(prev_state);

    struct umac_data *umacd = (struct umac_data *)inst->arg;
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    umac_datapath_stad_init(data->stad);

    enum mmwlan_status result = umac_connection_set_sta_state(data, MORSE_STA_AUTHENTICATED);
    if (result != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Unable to set sta state.\n");
    }
}

static void connection_fsm_connected_entry(struct connection_fsm_instance *inst,
                                           enum connection_fsm_state prev_state)
{
    MM_UNUSED(prev_state);

    struct umac_data *umacd = (struct umac_data *)inst->arg;
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    umac_stats_update_connect_timestamp(umacd, MMWLAN_STATS_CONNECT_TIMESTAMP_LINK_UP);


    umac_ps_set_suspended(umacd, false);

    if (data->sta_status_cb)
    {
        data->sta_status_cb(MMWLAN_STA_CONNECTED);
    }
    invoke_link_callback(umacd, data, MMWLAN_LINK_UP);

    enum mmwlan_status result = umac_connection_set_sta_state(data, MORSE_STA_AUTHORIZED);
    if (result != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Unable to set sta state.\n");
    }

    if (!data->conn_mon.disabled)
    {
        connection_mon_fsm_handle_event(&data->conn_mon.fsm, CONNECTION_MON_FSM_EVENT_START);
    }

    const struct mmwlan_twt_config_args *twt_config = umac_twt_get_config(umacd);
    if (twt_config->twt_mode == MMWLAN_TWT_REQUESTER)
    {
        result = umac_twt_install_pending_agreements(umacd, false);
        if (result != MMWLAN_SUCCESS)
        {
            MMLOG_ERR("Failed to install TWT agreement.\n");
        }
    }

    if (data->sta_args.use_4addr == MMWLAN_4ADDR_MODE_ENABLED)
    {

        enum mmwlan_status status = umac_datapath_build_and_tx_4addr_qos_null_frame(data->stad);
        if (status != MMWLAN_SUCCESS)
        {
            MMLOG_WRN("Failed to TX 4addr frame.\n");
        }
    }
}

static void connection_fsm_connected_exit(struct connection_fsm_instance *inst,
                                          enum connection_fsm_event event)
{
    MM_UNUSED(event);
    struct umac_data *umacd = (struct umac_data *)inst->arg;
    struct umac_connection_data *data = umac_data_get_connection(umacd);

    if (data->sta_status_cb)
    {
        data->sta_status_cb(MMWLAN_STA_DISABLED);
    }
    invoke_link_callback(umacd, data, MMWLAN_LINK_DOWN);

    umac_wnm_sleep_report_event(umacd, UMAC_WNM_SLEEP_EVENT_CONNECTION_LOST);

    if (!data->conn_mon.disabled)
    {
        connection_mon_fsm_handle_event(&data->conn_mon.fsm, CONNECTION_MON_FSM_EVENT_STOP);
    }
}



static void connection_fsm_transition_error(struct connection_fsm_instance *inst,
                                            enum connection_fsm_event event)
{
    MMLOG_INF("connection_fsm: invalid event %u (%s) in state %u (%s)\n",
              event,
              connection_fsm_event_tostr(event),
              inst->current_state,
              connection_fsm_state_tostr(inst->current_state));
    MMOSAL_DEV_ASSERT_LOG_DATA(false, inst->current_state, event);
}

static void connection_fsm_reentrance_error(struct connection_fsm_instance *inst)
{
    MMLOG_INF("connection_fsm: invalid reentrance in state %u (%s)\n",
              inst->current_state,
              connection_fsm_state_tostr(inst->current_state));
    MMOSAL_ASSERT_LOG_DATA(false, inst->current_state);
}

void umac_connection_signal_sta_event(struct umac_data *umacd, enum mmwlan_sta_event event)
{
    struct umac_connection_data *data = umac_data_get_connection(umacd);
    if (data->sta_args.sta_evt_cb != NULL)
    {
        struct mmwlan_sta_event_cb_args cb_args = {
            .event = event,
        };
        data->sta_args.sta_evt_cb(&cb_args, data->sta_args.sta_evt_cb_arg);
    }
}
