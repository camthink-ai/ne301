/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "common/common.h"
#include "mmosal.h"
#include "mmpkt.h"
#include "mmdrv.h"
#include "mmwlan_internal.h"

#include "umac/data/umac_data.h"
#include "umac/frames/association.h"
#include "umac/frames/authentication.h"
#include "umac/frames/deauthentication.h"
#include "umac/interface/umac_interface.h"
#include "umac/scan/umac_scan.h"
#include "umac/offload/umac_offload.h"




struct umac_evt;


typedef void (*umac_evt_handler_t)(struct umac_data *umacd, const struct umac_evt *evt);


struct umac_evt
{

    umac_evt_handler_t handler;


    struct umac_evt *next;


    union
    {
        struct
        {

            struct umac_scan_req *scan_req;
        } req_scan;

        struct
        {

            struct umac_scan_req *scan_req;
        } abort_scan;

        struct
        {

            enum mmwlan_scan_state state;
        } hw_scan_done;

        struct
        {

            uint8_t *command;

            uint32_t command_len;

            uint8_t *response;

            uint32_t *response_len;

            volatile enum mmwlan_status *status;

            struct mmosal_semb *semb;
        } exec_cmd;

        struct
        {

            uint32_t core_num;

            uint8_t **buf;

            uint32_t *buf_len;

            bool reset_stats;

            volatile enum mmwlan_status *status;

            struct mmosal_semb *semb;
        } morse_stats;

        struct
        {

            uint32_t core_id;
        } core_assert;

        struct
        {

            enum umac_interface_type type;

            struct mmosal_semb *semb;

            volatile enum mmwlan_status *status;
        } interface_add;

        struct
        {

            enum umac_interface_type type;

            struct mmosal_semb *semb;

            volatile enum mmwlan_status *status;
        } interface_remove;

        struct
        {

            const struct mmwlan_sta_args *args;

            mmwlan_sta_status_cb_t sta_status_cb;

            uint8_t *extra_assoc_ies;

            struct mmosal_semb *semb;

            volatile enum mmwlan_status *status;
        } connection_start;

        struct
        {

            struct mmosal_semb *semb;

            volatile enum mmwlan_status *status;
        } connection_stop;

        struct
        {

            const struct mmwlan_dpp_args *args;

            struct mmosal_semb *semb;

            volatile enum mmwlan_status *status;
        } connection_start_dpp;

        struct
        {

            struct mmosal_semb *semb;

            volatile enum mmwlan_status *status;
        } connection_stop_dpp;

        struct
        {

            enum mmwlan_ps_mode mode;
        } set_ps_mode;

        struct
        {

            bool enabled;

            bool chip_powerdown_enabled;

            struct mmosal_semb *semb;

            volatile enum mmwlan_status *status;
        } set_wnm_sleep;

        struct
        {
            const struct mmwlan_beacon_vendor_ie_filter *filter;

            struct mmosal_semb *semb;

            volatile enum mmwlan_status *status;
        } update_beacon_vendor_ie_filter;

        struct
        {

            uint32_t arp_addr;
        } arp_response_offload;

        struct
        {

            uint32_t interval_s;

            uint32_t dest_ip;

            bool send_as_garp;
        } arp_refresh_offload;

        struct
        {

            mmwlan_dhcp_lease_update_cb_t dhcp_lease_update_cb;

            void *dhcp_lease_update_cb_arg;
        } dhcp_offload;

        struct
        {

            struct mmwlan_dhcp_lease_info lease_info;
        } dhcp_offload_lease_update;

        struct mmwlan_standby_enter_args standby_enter;

        struct mmwlan_standby_set_status_payload_args standby_set_status_payload;

        struct mmwlan_standby_set_wake_filter_args standby_set_wake_filter;

        struct mmwlan_standby_config standby_set_config;

        struct mmwlan_tcp_keepalive_offload_args tcp_keepalive_offload;

        struct mmwlan_config_whitelist whitelist_filter;

        struct
        {

            uint32_t reason;
        } connection_loss;

        struct mmwlan_scan_config scan_config;

        struct
        {
            uint8_t bssid[MMWLAN_MAC_ADDR_LEN];
        } roam;

        struct
        {
            uint32_t freq_khz;
            uint32_t duration_ms;
        } remain_on_channel;

        struct
        {
            const struct mmwlan_ap_args *args;

            struct mmosal_semb *semb;

            volatile enum mmwlan_status *status;
        } ap_start;

        struct
        {

            const uint8_t *sta_addr;

            struct mmwlan_ap_sta_status *sta_status;

            struct mmosal_semb *semb;

            volatile enum mmwlan_status *status;
        } ap_get_sta_status;

        struct
        {

            struct mmosal_semb *semb;

            struct mmwlan_rc_stats **stats;
        } get_rc_stats;

        struct
        {

            uint32_t timeout_ms;

            struct mmosal_semb *semb;

            volatile enum mmwlan_status *status;
        } set_dynamic_ps_timeout;
    } args;
};


#define UMAC_EVT_INIT(_handler) { .handler = (_handler) }


#define UMAC_EVT_INIT_ARGS(_handler, _args_type, ...) \
    {                                                 \
        .handler = (_handler),                        \
        .args = { ._args_type = { __VA_ARGS__ } },    \
    }


typedef void (*umac_core_timeout_handler_t)(void *arg1, void *arg2);




void umac_core_init(struct umac_data *umacd);


void umac_core_deinit(struct umac_data *umacd);


enum mmwlan_status umac_core_start(struct umac_data *umacd);


void umac_core_stop(struct umac_data *umacd);


bool umac_core_is_running(struct umac_data *umacd);


bool umac_core_evtloop_is_active(struct umac_data *umacd);

#if defined(ENABLE_EXTERNAL_EVENT_LOOP) && ENABLE_EXTERNAL_EVENT_LOOP

uint32_t umac_core_dispatch_events(struct umac_data *umacd);

#endif


enum mmwlan_status umac_core_register_sleep_cb(struct umac_data *umacd,
                                               mmwlan_sleep_cb_t callback,
                                               void *arg);




bool umac_core_evt_queue(struct umac_data *umacd, const struct umac_evt *evt);


bool umac_core_evt_queue_at_start(struct umac_data *umacd, const struct umac_evt *evt);


void umac_core_evt_wake(struct umac_data *umacd);


#define UMAC_QUEUE_EVT_AND_WAIT(_evt_hdlr, _evt_data_type, _status, ...)                    \
    do {                                                                                    \
        bool _ok;                                                                           \
        struct umac_evt evt = UMAC_EVT_INIT_ARGS(_evt_hdlr, _evt_data_type, ##__VA_ARGS__); \
        evt.args._evt_data_type.semb = mmosal_semb_create("ev");                            \
        if (evt.args._evt_data_type.semb != NULL)                                           \
        {                                                                                   \
            evt.args._evt_data_type.status = _status;                                       \
            _ok = umac_core_evt_queue(umacd, &evt);                                         \
            if (_ok)                                                                        \
            {                                                                               \
                mmosal_semb_wait(evt.args._evt_data_type.semb, UINT32_MAX);                 \
            }                                                                               \
            else                                                                            \
            {                                                                               \
                MMLOG_INF("Failed to queue event\n");                                       \
                (*(_status)) = MMWLAN_NOT_RUNNING;                                          \
            }                                                                               \
            mmosal_semb_delete(evt.args._evt_data_type.semb);                               \
        }                                                                                   \
        else                                                                                \
        {                                                                                   \
            MMLOG_INF("Failed to alloc semaphore\n");                                       \
            (*(_status)) = MMWLAN_NO_MEM;                                                   \
        }                                                                                   \
    } while (0)




bool umac_core_register_timeout(struct umac_data *umacd,
                                uint32_t delta_ms,
                                umac_core_timeout_handler_t handler,
                                void *arg1,
                                void *arg2);


int umac_core_cancel_timeout(struct umac_data *umacd,
                             umac_core_timeout_handler_t handler,
                             void *arg1,
                             void *arg2);


int umac_core_cancel_timeout_one(struct umac_data *umacd,
                                 umac_core_timeout_handler_t handler,
                                 void *arg1,
                                 void *arg2,
                                 uint32_t *remaining);


bool umac_core_is_timeout_registered(struct umac_data *umacd,
                                     umac_core_timeout_handler_t handler,
                                     void *arg1,
                                     void *arg2);


int umac_core_deplete_timeout(struct umac_data *umacd,
                              uint32_t delta_ms,
                              umac_core_timeout_handler_t handler,
                              void *arg1,
                              void *arg2);


enum mmwlan_status umac_core_alloc_extra_timeouts(struct umac_data *umacd);


