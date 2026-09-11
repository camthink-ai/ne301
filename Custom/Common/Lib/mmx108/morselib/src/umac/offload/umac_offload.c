/*
 * Copyright 2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "mmwlan.h"
#include "mmdrv.h"
#include "mmutils.h"
#include "umac/umac.h"
#include "umac/connection/umac_connection.h"
#include "umac/interface/umac_interface.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/config/umac_config.h"
#include "umac/scan/umac_scan.h"
#include "umac_offload.h"
#include "umac_offload_data.h"

void umac_offload_init(struct umac_data *umacd, uint16_t vif_id)
{
    struct umac_offload_data *data = umac_data_get_offload(umacd);
    data->vif_id = vif_id;
}

void umac_offload_set_arp_response_offload(struct umac_data *umacd, uint32_t arp_addr)
{
    struct umac_offload_data *data = umac_data_get_offload(umacd);
    data->arp_addr = arp_addr;
    mmdrv_enable_arp_response_offload(data->vif_id, data->arp_addr);
}

void umac_offload_restore_all(struct umac_data *umacd)
{
    struct umac_offload_data *data = umac_data_get_offload(umacd);
    uint8_t monitor_bssid[ETH_ALEN];

    if (data->arp_addr)
    {
        mmdrv_enable_arp_response_offload(data->vif_id, data->arp_addr);
    }

    if (data->arp_refresh_interval_s)
    {
        mmdrv_enable_arp_refresh_offload(data->vif_id,
                                         data->arp_refresh_interval_s,
                                         data->arp_refresh_dest_ip,
                                         data->arp_refresh_garp);
    }

    if (data->dhcp_offload_enabled)
    {
        mmdrv_enable_dhcp_offload(data->vif_id);
        mmdrv_do_dhcp_discovery(data->vif_id);
    }
    if (data->tcp_keepalive_enabled)
    {
        mmdrv_set_tcp_keepalive_offload(data->vif_id, &data->tcp_keepalive_args);
    }

    if (data->standby_mode_enabled)
    {
        mmdrv_standby_set_config(data->vif_id, &data->standby_config);

        if (data->standby_set_status_payload.payload_len)
        {
            mmdrv_standby_set_status_payload(data->vif_id,
                                             data->standby_set_status_payload.payload,
                                             data->standby_set_status_payload.payload_len);
        }

        if (data->standby_wake_filter.filter_len)
        {
            mmdrv_standby_set_wake_filter(data->vif_id,
                                          data->standby_wake_filter.filter,
                                          data->standby_wake_filter.filter_len,
                                          data->standby_wake_filter.offset);
        }

        mmdrv_set_health_check_veto(MMDRV_HEALTH_CHECK_VETO_ID_STANDBY);
        umac_connection_get_bssid(umacd, monitor_bssid);
        mmdrv_standby_enter(data->vif_id, monitor_bssid);
    }
}

void umac_offload_set_arp_refresh(struct umac_data *umacd,
                                  uint32_t interval_s,
                                  uint32_t dest_ip,
                                  bool send_as_garp)
{
    struct umac_offload_data *data = umac_data_get_offload(umacd);
    data->arp_refresh_interval_s = interval_s;
    data->arp_refresh_dest_ip = dest_ip;
    data->arp_refresh_garp = send_as_garp;
    mmdrv_enable_arp_refresh_offload(data->vif_id, interval_s, dest_ip, send_as_garp);
}

void umac_offload_dhcp_enable(struct umac_data *umacd,
                              mmwlan_dhcp_lease_update_cb_t dhcp_lease_update_cb,
                              void *arg)
{
    struct umac_offload_data *data = umac_data_get_offload(umacd);
    data->dhcp_offload_enabled = true;
    data->dhcp_lease_update_cb = dhcp_lease_update_cb;
    data->dhcp_lease_update_cb_arg = arg;
    mmdrv_enable_dhcp_offload(data->vif_id);


    mmdrv_do_dhcp_discovery(data->vif_id);
}

void umac_offload_dhcp_lease_update(struct umac_data *umacd,
                                    const struct mmwlan_dhcp_lease_info *lease_info)
{
    struct umac_offload_data *data = umac_data_get_offload(umacd);


    if (data->dhcp_lease_update_cb)
    {
        data->dhcp_lease_update_cb(lease_info, data->dhcp_lease_update_cb_arg);
    }
}

void umac_offload_config_tcp_keepalive(struct umac_data *umacd,
                                       const struct mmwlan_tcp_keepalive_offload_args *args)
{
    struct umac_offload_data *data = umac_data_get_offload(umacd);

    if (args != NULL)
    {
        data->tcp_keepalive_enabled = 1;
        if (args->set_cfgs & MMWLAN_TCP_KEEPALIVE_SET_CFG_PERIOD)
        {
            data->tcp_keepalive_args.period_s = args->period_s;
        }

        if (args->set_cfgs & MMWLAN_TCP_KEEPALIVE_SET_CFG_RETRY_COUNT)
        {
            data->tcp_keepalive_args.retry_count = args->retry_count;
        }

        if (args->set_cfgs & MMWLAN_TCP_KEEPALIVE_SET_CFG_RETRY_INTERVAL)
        {
            data->tcp_keepalive_args.retry_interval_s = args->retry_interval_s;
        }

        if (args->set_cfgs & MMWLAN_TCP_KEEPALIVE_SET_CFG_SRC_IP_ADDR)
        {
            data->tcp_keepalive_args.src_ip = args->src_ip;
        }

        if (args->set_cfgs & MMWLAN_TCP_KEEPALIVE_SET_CFG_DEST_IP_ADDR)
        {
            data->tcp_keepalive_args.dest_ip = args->dest_ip;
        }

        if (args->set_cfgs & MMWLAN_TCP_KEEPALIVE_SET_CFG_SRC_PORT)
        {
            data->tcp_keepalive_args.src_port = args->src_port;
        }

        if (args->set_cfgs & MMWLAN_TCP_KEEPALIVE_SET_CFG_DEST_PORT)
        {
            data->tcp_keepalive_args.dest_port = args->dest_port;
        }
        data->tcp_keepalive_args.set_cfgs |= args->set_cfgs;
    }
    else
    {
        data->tcp_keepalive_enabled = 0;
        memset(&data->tcp_keepalive_args, 0, sizeof(data->tcp_keepalive_args));
    }

    mmdrv_set_tcp_keepalive_offload(data->vif_id, args);
}

enum mmwlan_status umac_offload_standby_enter(
    struct umac_data *umacd,
    const struct mmwlan_standby_enter_args *standby_enter_args)
{
    uint8_t monitor_bssid[ETH_ALEN];

    struct umac_offload_data *data = umac_data_get_offload(umacd);
    data->standby_mode_enabled = true;
    memcpy(&data->standby_enter_args, standby_enter_args, sizeof(struct mmwlan_standby_enter_args));

    struct mmwlan_scan_args scan_args = MMWLAN_SCAN_ARGS_INIT;
    scan_args.dwell_time_ms = umac_config_get_supp_scan_dwell_time(umacd);
    scan_args.ssid_len = umac_connection_get_ssid(umacd, scan_args.ssid);

    (void)umac_scan_store_scan_config(umacd, &scan_args);


    umac_datapath_pause(umacd, UMAC_DATAPATH_PAUSE_SOURCE_STANDBY);
    mmdrv_set_health_check_veto(MMDRV_HEALTH_CHECK_VETO_ID_STANDBY);
    umac_connection_set_monitor_disable(umacd, true);

    umac_connection_get_bssid(umacd, monitor_bssid);
    if (mmdrv_standby_enter(data->vif_id, monitor_bssid) == 0)
    {
        return MMWLAN_SUCCESS;
    }
    return MMWLAN_ERROR;
}

enum mmwlan_status umac_offload_standby_exit(struct umac_data *umacd)
{
    uint8_t reason;
    struct umac_offload_data *data = umac_data_get_offload(umacd);
    if (mmdrv_standby_exit(data->vif_id, &reason) == 0)
    {
        data->standby_mode_enabled = false;
        umac_datapath_unpause(umacd, UMAC_DATAPATH_PAUSE_SOURCE_STANDBY);
        mmdrv_unset_health_check_veto(MMDRV_HEALTH_CHECK_VETO_ID_STANDBY);
        umac_connection_set_monitor_disable(umacd, false);


        if (data->standby_enter_args.standby_exit_cb)
        {
            data->standby_enter_args.standby_exit_cb(reason,
                                                     data->standby_enter_args.standby_exit_arg);
        }
        return MMWLAN_SUCCESS;
    }
    return MMWLAN_ERROR;
}

enum mmwlan_status umac_offload_standby_set_status_payload(
    struct umac_data *umacd,
    const struct mmwlan_standby_set_status_payload_args *standby_set_status_payload_args)
{
    struct umac_offload_data *data = umac_data_get_offload(umacd);
    if (standby_set_status_payload_args->payload_len >
        sizeof(data->standby_set_status_payload.payload))
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    memcpy(&data->standby_set_status_payload,
           standby_set_status_payload_args,
           sizeof(struct mmwlan_standby_set_status_payload_args));
    if (mmdrv_standby_set_status_payload(data->vif_id,
                                         standby_set_status_payload_args->payload,
                                         standby_set_status_payload_args->payload_len) == 0)
    {
        return MMWLAN_SUCCESS;
    }
    return MMWLAN_ERROR;
}

enum mmwlan_status umac_offload_standby_set_wake_filter(
    struct umac_data *umacd,
    const struct mmwlan_standby_set_wake_filter_args *standby_set_wake_filter_args)
{
    struct umac_offload_data *data = umac_data_get_offload(umacd);
    if (standby_set_wake_filter_args->filter_len > sizeof(data->standby_wake_filter.filter))
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    memcpy(&data->standby_wake_filter,
           standby_set_wake_filter_args,
           sizeof(struct mmwlan_standby_set_wake_filter_args));
    if (mmdrv_standby_set_wake_filter(data->vif_id,
                                      standby_set_wake_filter_args->filter,
                                      standby_set_wake_filter_args->filter_len,
                                      standby_set_wake_filter_args->offset) == 0)
    {
        return MMWLAN_SUCCESS;
    }
    return MMWLAN_ERROR;
}

enum mmwlan_status umac_offload_standby_set_config(struct umac_data *umacd,
                                                   const struct mmwlan_standby_config *config)
{
    struct umac_offload_data *data = umac_data_get_offload(umacd);
    memcpy(&data->standby_config, config, sizeof(data->standby_config));
    if (mmdrv_standby_set_config(data->vif_id, config) == 0)
    {
        return MMWLAN_SUCCESS;
    }
    return MMWLAN_ERROR;
}

enum mmwlan_status umac_offload_set_whitelist_filter(
    struct umac_data *umacd,
    const struct mmwlan_config_whitelist *whitelist)
{
    struct umac_offload_data *data = umac_data_get_offload(umacd);
    memcpy(&data->whitelist_filter, whitelist, sizeof(data->whitelist_filter));
    if (mmdrv_set_whitelist_filter(data->vif_id, whitelist) == 0)
    {
        return MMWLAN_SUCCESS;
    }
    return MMWLAN_ERROR;
}
