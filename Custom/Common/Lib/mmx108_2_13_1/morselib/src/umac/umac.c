/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "mmosal.h"
#include "mmutils.h"
#include "mmwlan.h"
#include "mmwlan_internal.h"
#include "mmversion.h"
#include "common/common.h"
#include "common/mac_address.h"
#include "mmlog.h"
#include "common/morse_commands.h"
#include "umac/connection/umac_connection_data.h"
#include "umac/umac.h"
#include "umac/umac_root_data.h"
#include "umac/ap/umac_ap.h"
#include "umac/core/umac_core.h"
#include "umac/supplicant_shim/umac_supp_shim.h"
#include "umac/interface/umac_interface.h"
#include "umac/twt/umac_twt.h"
#include "umac/config/umac_config.h"
#include "umac/connection/umac_connection.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/health_check/umac_health_check.h"
#include "umac/stats/umac_stats.h"
#include "umac/rc/umac_rc.h"
#include "umac/regdb/umac_regdb.h"
#include "umac/relay/umac_relay.h"
#include "umac/ate/umac_ate.h"
#include "umac/wnm_sleep/umac_wnm_sleep.h"


#if MM_VERSION < MM_VERSION_NUMBER(0, 0, 0)

#endif

#ifdef ENABLE_UMAC_TRACE
#include "mmtrace.h"
static mmtrace_channel umac_channel_handle;
#define UMAC_TRACE_INIT()     umac_channel_handle = mmtrace_register_channel("umac")
#define UMAC_TRACE(_fmt, ...) mmtrace_printf(umac_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define UMAC_TRACE_INIT() \
    do {                  \
    } while (0)
#define UMAC_TRACE(_fmt, ...) \
    do {                      \
    } while (0)
#endif

void mmwlan_init(void)
{
    UMAC_TRACE_INIT();

    umac_data_init();

    struct umac_data *umacd = umac_data_get_umacd();
    mmdrv_pre_init();
    umac_config_init(umacd);
    umac_core_init(umacd);
    umac_interface_init(umacd);
    umac_health_check_init(umacd);
    umac_twt_init(umacd);
    umac_connection_init(umacd);
    umac_datapath_init(umacd);
    umac_scan_init(umacd);
    umac_wnm_sleep_init(umacd);
    umac_rc_init();
}

void mmwlan_deinit(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    umac_scan_deinit(umacd);
    umac_supp_deinit(umacd);
    umac_datapath_deinit(umacd);
    umac_connection_deinit(umacd);
    umac_health_check_deinit(umacd);
    umac_config_deinit(umacd);
    mmdrv_post_deinit();
    umac_data_deinit();
}

enum mmwlan_status mmwlan_set_channel_list(const struct mmwlan_s1g_channel_list *channel_list)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (umac_interface_is_active(umacd))
    {
        MMLOG_ERR("Cannot set channel list when interface is active\n");
        return MMWLAN_UNAVAILABLE;
    }

    umac_config_set_channel_list(umacd, channel_list);


    umac_config_set_selective_scan_channels(umacd, NULL, 0, 0);

    return MMWLAN_SUCCESS;
}

enum mmwlan_status mmwlan_get_version(struct mmwlan_version *version)
{
    struct umac_data *umacd = umac_data_get_umacd();
    const char *morselib_version = MM_VERSION_BUILDID;

    size_t morselib_version_len = sizeof(MM_VERSION_BUILDID);
    MM_STATIC_ASSERT(sizeof(MM_VERSION_BUILDID) <= sizeof(version->morselib_version),
                     "MM_VERSION_BUILDID define too long for morselib_version string array.");
    const char *chip_id_string = umac_interface_get_chip_id_string(umacd);

    if (version == NULL)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    if (chip_id_string == NULL)
    {
        return MMWLAN_UNAVAILABLE;
    }

    memcpy(version->morselib_version, morselib_version, morselib_version_len);
    version->morse_chip_id = umac_interface_get_chip_id(umacd);
    strncpy(version->morse_chip_id_string, chip_id_string, sizeof(version->morse_chip_id_string));
    version->morse_chip_id_string[sizeof(version->morse_chip_id_string) - 1] = '\0';

    MM_STATIC_ASSERT(MMWLAN_FW_VERSION_MAXLEN == sizeof(version->morse_fw_version),
                     "Mismatch in sizeof morse_fw_version array and public max length.");
    struct mmdrv_fw_version fw_version;
    enum mmwlan_status status = umac_interface_get_fw_version(umacd, &fw_version);
    if (status != MMWLAN_SUCCESS)
    {
        memset(version->morse_fw_version, '\0', MMWLAN_FW_VERSION_MAXLEN);
        return status;
    }

    int written = snprintf(version->morse_fw_version,
                           MMWLAN_FW_VERSION_MAXLEN,
                           "%u.%u.%u",
                           fw_version.major,
                           fw_version.minor,
                           fw_version.patch);

    if (written == MMWLAN_FW_VERSION_MAXLEN)
    {
        return MMWLAN_ERROR;
    }

    return MMWLAN_SUCCESS;
}

enum mmwlan_status mmwlan_get_bcf_metadata(struct mmwlan_bcf_metadata *metadata)
{
    return mmdrv_get_bcf_metadata(metadata);
}

enum mmwlan_status mmwlan_override_max_tx_power(uint16_t tx_power_dbm)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    umac_config_set_max_tx_power(umacd, tx_power_dbm);

    return MMWLAN_SUCCESS;
}

enum mmwlan_status mmwlan_set_rts_threshold(unsigned rts_threshold)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    umac_config_set_rts_threshold(umacd, rts_threshold);

    return MMWLAN_SUCCESS;
}

enum mmwlan_status mmwlan_set_sgi_enabled(bool sgi_enabled)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (umac_connection_get_state(umacd) != MMWLAN_STA_DISABLED)
    {
        return MMWLAN_UNAVAILABLE;
    }

    umac_config_rc_set_sgi_enabled(umacd, sgi_enabled);

    return MMWLAN_SUCCESS;
}

enum mmwlan_status mmwlan_set_subbands_enabled(bool subbands_enabled)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (umac_connection_get_state(umacd) != MMWLAN_STA_DISABLED)
    {
        return MMWLAN_UNAVAILABLE;
    }

    umac_config_rc_set_subbands_enabled(umacd, subbands_enabled);

    return MMWLAN_SUCCESS;
}

static void umac_set_power_save_mode_evt_handler(struct umac_data *umacd,
                                                 const struct umac_evt *evt)
{
    umac_config_set_ps_mode(umacd, evt->args.set_ps_mode.mode);
    umac_ps_update_mode(umacd);
}

enum mmwlan_status mmwlan_set_power_save_mode(enum mmwlan_ps_mode mode)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    struct umac_evt evt = UMAC_EVT_INIT(umac_set_power_save_mode_evt_handler);
    evt.args.set_ps_mode.mode = mode;
    bool ok = umac_core_evt_queue(umacd, &evt);
    if (!ok)
    {

        MMLOG_DBG("Failed to queue SET_PS_MODE event. Setting config directly\n");
        umac_config_set_ps_mode(umacd, mode);
    }
    return MMWLAN_SUCCESS;
}

static void umac_set_dynamic_ps_timeout_evt_handler(struct umac_data *umacd,
                                                    const struct umac_evt *evt)
{
    *evt->args.set_dynamic_ps_timeout.status =
        mmdrv_set_dynamic_ps_timeout(evt->args.set_dynamic_ps_timeout.timeout_ms);
    if (*evt->args.set_dynamic_ps_timeout.status != MMWLAN_SUCCESS)
    {
        goto exit;
    }

    umac_config_set_dynamic_ps_timeout(umacd, evt->args.set_dynamic_ps_timeout.timeout_ms);

    *evt->args.set_dynamic_ps_timeout.status = MMWLAN_SUCCESS;

exit:
    mmosal_semb_give(evt->args.set_dynamic_ps_timeout.semb);
}

enum mmwlan_status mmwlan_set_dynamic_ps_timeout(uint32_t timeout_ms)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (!umac_core_is_running(umacd))
    {
        return MMWLAN_UNAVAILABLE;
    }

    enum mmwlan_status status = MMWLAN_ERROR;
    UMAC_QUEUE_EVT_AND_WAIT(umac_set_dynamic_ps_timeout_evt_handler,
                            set_dynamic_ps_timeout,
                            &status,
                            .timeout_ms = timeout_ms);

    return status;
}

enum mmwlan_status mmwlan_set_ampdu_enabled(bool ampdu_enabled)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (umac_connection_get_state(umacd) != MMWLAN_STA_DISABLED)
    {
        return MMWLAN_UNAVAILABLE;
    }

    umac_config_set_ampdu_enabled(umacd, ampdu_enabled);
    return MMWLAN_SUCCESS;
}

enum mmwlan_status mmwlan_set_non_tim_mode_enabled(bool non_tim_mode_enabled)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (mmwlan_get_sta_state() != MMWLAN_STA_DISABLED)
    {
        return MMWLAN_UNAVAILABLE;
    }

    umac_config_set_non_tim_mode_enabled(umacd, non_tim_mode_enabled);
    return MMWLAN_SUCCESS;
}

enum mmwlan_status mmwlan_set_sta_autoconnect(enum mmwlan_sta_autoconnect_mode mode)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    umac_connection_set_sta_autoconnect(umacd, mode);
    return MMWLAN_SUCCESS;
}

static void umac_stop_core_if_no_interface(struct umac_data *umacd)
{
    if (!umac_interface_is_active(umacd))
    {
        umac_core_stop(umacd);
    }
}

void mmwlan_stop_core_if_no_interface(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    umac_stop_core_if_no_interface(umacd);
}

#if defined(ENABLE_EXTERNAL_EVENT_LOOP) && ENABLE_EXTERNAL_EVENT_LOOP
enum mmwlan_status mmwlan_dispatch_events(uint32_t *next_event_time)
{
    uint32_t next_event_time_ret;
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    next_event_time_ret = umac_core_dispatch_events(umacd);
    if (next_event_time != NULL)
    {
        *next_event_time = next_event_time_ret;
    }

    return MMWLAN_SUCCESS;
}

#endif

enum mmwlan_status mmwlan_set_default_qos_queue_params(const struct mmwlan_qos_queue_params *params,
                                                       size_t count)
{
    if ((count == 0) || (count > MMWLAN_QOS_QUEUE_NUM_ACIS))
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    struct umac_data *umacd = umac_data_get_umacd();
    uint8_t acis_updated = 0;
    for (uint8_t i = 0; i < count; i++)
    {
        if (params[i].aci > 3)
        {
            MMLOG_ERR("Invalid ACI %u in list index %u\n", params[i].aci, i);
            return MMWLAN_INVALID_ARGUMENT;
        }

        if (params[i].aifs < 2)
        {
            MMLOG_ERR("Invalid AIFS %u in list index %u\n", params[i].aifs, i);
            return MMWLAN_INVALID_ARGUMENT;
        }

        if (acis_updated & (1 << params[i].aci))
        {
            MMLOG_ERR("ACI %u repeated in list index %u\n", params[i].aci, i);
            return MMWLAN_INVALID_ARGUMENT;
        }

        umac_config_set_default_qos_queue_params(umacd, &params[i]);
        acis_updated |= (1 << params[i].aci);
    }

    return MMWLAN_SUCCESS;
}

static void umac_interface_add_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    MMOSAL_DEV_ASSERT(evt->args.interface_add.type == UMAC_INTERFACE_NONE);
    *evt->args.interface_add.status =
        umac_interface_add(umacd, UMAC_INTERFACE_NONE, NULL, evt->args.interface_add.mac_addr);
    mmosal_semb_give(evt->args.interface_add.semb);
}

enum mmwlan_status mmwlan_boot(const struct mmwlan_boot_args *args)
{

    if (args == NULL)
    {
        static const struct mmwlan_boot_args default_args = MMWLAN_BOOT_ARGS_INIT;
        args = &default_args;
    }

    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();

    status = umac_core_start(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    status = MMWLAN_ERROR;
    UMAC_QUEUE_EVT_AND_WAIT(umac_interface_add_evt_handler,
                            interface_add,
                            &status,
                            .type = UMAC_INTERFACE_NONE,
                            .mac_addr = args->sta_mac_addr_override);

    umac_stop_core_if_no_interface(umacd);

    return status;
}

bool umac_shutdown_is_in_progress(struct umac_data *umacd)
{
    struct umac_root_data *data = umac_data_get_root(umacd);

    return data->shutdown_in_progress;
}

static void umac_abort_scan_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    umac_scan_abort(umacd, evt->args.abort_scan.scan_req);
}

static void umac_connection_stop_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    enum mmwlan_status status = umac_connection_stop(umacd);
    if (evt->args.connection_stop.status)
    {
        *evt->args.connection_stop.status = status;
    }
    if (evt->args.connection_stop.semb)
    {
        mmosal_semb_give(evt->args.connection_stop.semb);
    }
}

static void umac_inactive_handler(void *arg)
{
    struct mmosal_semb *semb = (struct mmosal_semb *)arg;
    mmosal_semb_give(semb);
}

static void umac_shutdown_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    struct umac_root_data *data = umac_data_get_root(umacd);
    MM_UNUSED(evt);

    data->shutdown_in_progress = true;
#if !(defined(MMWLAN_AP_DISABLED) && MMWLAN_AP_DISABLED)
    umac_ap_disable_ap(umacd);
#endif
    (void)umac_connection_stop(umacd);
    umac_scan_abort(umacd, NULL);
    umac_interface_remove(umacd, UMAC_INTERFACE_NONE);
    umac_supp_deinit(umacd);
}

enum mmwlan_status mmwlan_shutdown(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    bool ok;

    if (!umac_core_is_running(umacd))
    {
        MMLOG_INF("Event loop not running. Nothing to shutdown.\n");
        return MMWLAN_SUCCESS;
    }

    struct mmosal_semb *semb = mmosal_semb_create("api");
    if (semb == NULL)
    {
        MMLOG_WRN("Failed to allocate semb\n");
        return MMWLAN_NO_MEM;
    }

    umac_interface_register_inactive_cb(umacd, umac_inactive_handler, semb);
    struct umac_evt evt = UMAC_EVT_INIT(umac_shutdown_evt_handler);
    ok = umac_core_evt_queue(umacd, &evt);
    if (!ok)
    {
        return MMWLAN_ERROR;
    }
    mmosal_semb_wait(semb, UINT32_MAX);
    mmosal_semb_delete(semb);

    umac_stop_core_if_no_interface(umacd);

    return MMWLAN_SUCCESS;
}

enum mmwlan_status mmwlan_shutdown_nowait(mmwlan_shutdown_cb_t shutdown_cb, void *cb_arg)
{
    struct umac_data *umacd = umac_data_get_umacd();
    bool ok;

    if (!umac_core_is_running(umacd))
    {
        MMLOG_INF("Event loop not running. Nothing to shutdown.\n");
        return MMWLAN_SUCCESS;
    }

    umac_interface_register_inactive_cb(umacd, shutdown_cb, cb_arg);
    struct umac_evt evt = UMAC_EVT_INIT(umac_shutdown_evt_handler);
    ok = umac_core_evt_queue(umacd, &evt);

    return ok ? MMWLAN_SUCCESS : MMWLAN_ERROR;
}

static void umac_connection_start_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    enum mmwlan_status status = umac_connection_start(umacd,
                                                      evt->args.connection_start.args,
                                                      evt->args.connection_start.sta_status_cb,
                                                      evt->args.connection_start.extra_assoc_ies);
    if (evt->args.connection_start.status)
    {
        *evt->args.connection_start.status = status;
    }
    else
    {
        MMOSAL_ASSERT(status == MMWLAN_SUCCESS);
    }
    if (evt->args.connection_start.semb)
    {
        mmosal_semb_give(evt->args.connection_start.semb);
    }
}

enum mmwlan_status mmwlan_sta_enable(const struct mmwlan_sta_args *args,
                                     mmwlan_sta_status_cb_t sta_status_cb)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();
    struct umac_root_data *data = umac_data_get_root(umacd);

    MMLOG_DBG("STA enable\n");

    if (data == NULL)
    {
        return MMWLAN_UNAVAILABLE;
    }

    if (umac_relay_get_state(umacd) == MMWLAN_RELAY_STATE_ROOT)
    {
        return MMWLAN_UNAVAILABLE;
    }

    bool ok = umac_connection_validate_sta_args(args);
    if (!ok)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    if (umac_config_get_channel_list(umacd) == NULL)
    {
        MMLOG_ERR("Channel list not set\n");
        return MMWLAN_CHANNEL_LIST_NOT_SET;
    }

    uint8_t *extra_assoc_ies = NULL;
    if (args->extra_assoc_ies_len)
    {
        extra_assoc_ies = (uint8_t *)mmosal_malloc(args->extra_assoc_ies_len);
        if (extra_assoc_ies == NULL)
        {
            return MMWLAN_NO_MEM;
        }
        memcpy(extra_assoc_ies, args->extra_assoc_ies, args->extra_assoc_ies_len);
    }

    status = umac_core_start(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        mmosal_free(extra_assoc_ies);
        return status;
    }

    UMAC_QUEUE_EVT_AND_WAIT(umac_connection_start_evt_handler,
                            connection_start,
                            &status,
                            .args = args,
                            .sta_status_cb = sta_status_cb,
                            .extra_assoc_ies = extra_assoc_ies);

    umac_stop_core_if_no_interface(umacd);

    return status;
}

enum mmwlan_status mmwlan_sta_enable_nowait(const struct mmwlan_sta_args *args,
                                            mmwlan_sta_status_cb_t sta_status_cb)
{
    struct umac_data *umacd = umac_data_get_umacd();
    struct umac_root_data *data = umac_data_get_root(umacd);
    if (data == NULL)
    {
        return MMWLAN_UNAVAILABLE;
    }

    if (umac_relay_get_state(umacd) == MMWLAN_RELAY_STATE_ROOT)
    {
        return MMWLAN_UNAVAILABLE;
    }

    bool ok = umac_connection_validate_sta_args(args);
    if (!ok)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    if (umac_config_get_channel_list(umacd) == NULL)
    {
        MMLOG_ERR("Channel list not set\n");
        return MMWLAN_CHANNEL_LIST_NOT_SET;
    }

    uint8_t *extra_assoc_ies = NULL;
    if (args->extra_assoc_ies_len)
    {
        extra_assoc_ies = (uint8_t *)mmosal_malloc(args->extra_assoc_ies_len);
        if (extra_assoc_ies == NULL)
        {
            return MMWLAN_NO_MEM;
        }
        memcpy(extra_assoc_ies, args->extra_assoc_ies, args->extra_assoc_ies_len);
    }

    enum mmwlan_status status = umac_core_start(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        mmosal_free(extra_assoc_ies);
        return status;
    }

    struct umac_evt evt = UMAC_EVT_INIT_ARGS(umac_connection_start_evt_handler,
                                             connection_start,
                                             .args = args,
                                             .sta_status_cb = sta_status_cb,
                                             .extra_assoc_ies = extra_assoc_ies);
    ok = umac_core_evt_queue(umacd, &evt);

    umac_stop_core_if_no_interface(umacd);

    return ok ? MMWLAN_SUCCESS : MMWLAN_ERROR;
}

static void umac_roam_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    umac_connection_roam(umacd, evt->args.roam.bssid);
}

enum mmwlan_status mmwlan_roam(const uint8_t *bssid)
{
    struct umac_data *umacd = umac_data_get_umacd();
    if (!umac_core_is_running(umacd))
    {
        return MMWLAN_UNAVAILABLE;
    }
    if (umac_connection_get_state(umacd) != MMWLAN_STA_CONNECTED)
    {
        return MMWLAN_UNAVAILABLE;
    }
    struct umac_evt evt = UMAC_EVT_INIT_ARGS(umac_roam_evt_handler, roam);
    memcpy(evt.args.roam.bssid, bssid, sizeof(evt.args.roam.bssid));
    bool ok = umac_core_evt_queue(umacd, &evt);
    return ok ? MMWLAN_SUCCESS : MMWLAN_ERROR;
}

enum mmwlan_status mmwlan_sta_disable(void)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();


    if (!umac_core_is_running(umacd))
    {
        return MMWLAN_SUCCESS;
    }

    UMAC_QUEUE_EVT_AND_WAIT(umac_connection_stop_evt_handler, connection_stop, &status);

    umac_stop_core_if_no_interface(umacd);

    return status;
}

enum mmwlan_status mmwlan_sta_disable_nowait(void)
{
    struct umac_data *umacd = umac_data_get_umacd();


    if (!umac_core_is_running(umacd))
    {
        return MMWLAN_SUCCESS;
    }

    struct umac_evt evt = UMAC_EVT_INIT_ARGS(umac_connection_stop_evt_handler, connection_stop);
    bool ok = umac_core_evt_queue(umacd, &evt);

    umac_stop_core_if_no_interface(umacd);

    return ok ? MMWLAN_SUCCESS : MMWLAN_ERROR;
}

#if !(defined(MMWLAN_DPP_DISABLED) && MMWLAN_DPP_DISABLED)
static void umac_connection_start_dpp_evt_handler(struct umac_data *umacd,
                                                  const struct umac_evt *evt)
{
    enum mmwlan_status status =
        umac_connection_start_dpp(umacd, evt->args.connection_start_dpp.args);

    if (evt->args.connection_start_dpp.status)
    {
        *evt->args.connection_start_dpp.status = status;
    }
    else
    {
        MMOSAL_ASSERT(status == MMWLAN_SUCCESS);
    }

    if (evt->args.connection_start_dpp.semb)
    {
        mmosal_semb_give(evt->args.connection_start_dpp.semb);
    }
}

enum mmwlan_status mmwlan_dpp_start(const struct mmwlan_dpp_args *args)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();

    if (umac_config_get_channel_list(umacd) == NULL)
    {
        MMLOG_ERR("Channel list not set\n");
        return MMWLAN_CHANNEL_LIST_NOT_SET;
    }

    status = umac_core_start(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    UMAC_QUEUE_EVT_AND_WAIT(umac_connection_start_dpp_evt_handler,
                            connection_start_dpp,
                            &status,
                            .args = args);

    umac_stop_core_if_no_interface(umacd);

    return status;
}

static void umac_connection_stop_dpp_evt_handler(struct umac_data *umacd,
                                                 const struct umac_evt *evt)
{
    enum mmwlan_status status = umac_connection_stop_dpp(umacd);
    if (evt->args.connection_stop_dpp.status)
    {
        *evt->args.connection_stop_dpp.status = status;
    }
    if (evt->args.connection_stop_dpp.semb)
    {
        mmosal_semb_give(evt->args.connection_stop_dpp.semb);
    }
}

enum mmwlan_status mmwlan_dpp_stop(void)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();

    UMAC_QUEUE_EVT_AND_WAIT(umac_connection_stop_dpp_evt_handler, connection_stop_dpp, &status);

    return status;
}

static void umac_dpp_get_uri_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    enum mmwlan_status status = umac_connection_get_dpp_uri(umacd,
                                                            evt->args.dpp_get_uri.uri_buf,
                                                            evt->args.dpp_get_uri.buf_len);
    if (evt->args.dpp_get_uri.status)
    {
        *evt->args.dpp_get_uri.status = status;
    }
    if (evt->args.dpp_get_uri.semb)
    {
        mmosal_semb_give(evt->args.dpp_get_uri.semb);
    }
}

enum mmwlan_status mmwlan_dpp_get_uri(char *uri_buf, size_t buf_len)
{
    if (uri_buf == NULL || buf_len == 0)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();

    UMAC_QUEUE_EVT_AND_WAIT(umac_dpp_get_uri_evt_handler,
                            dpp_get_uri,
                            &status,
                            .uri_buf = uri_buf,
                            .buf_len = buf_len);

    return status;
}

#endif

enum mmwlan_sta_state mmwlan_get_sta_state(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_connection_get_state(umacd);
}

static void umac_scan_rx_callback(struct umac_data *umacd, const struct umac_scan_response *rsp)
{
    struct mmwlan_scan_result scan_result;
    umac_scan_fill_result(&scan_result, rsp);

    struct umac_root_data *data = umac_data_get_root(umacd);
    data->scan_rx_cb(&scan_result, data->scan_cb_arg);
}


static enum mmwlan_status validate_scan_channels(struct umac_data *umacd,
                                                 const uint8_t *channels,
                                                 uint8_t num_channels)
{
    if (channels == NULL && num_channels != 0)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }
    if (num_channels == 0)
    {
        return MMWLAN_SUCCESS;
    }

    const struct mmwlan_s1g_channel_list *channel_list = umac_config_get_channel_list(umacd);
    if (channel_list == NULL)
    {
        return MMWLAN_CHANNEL_LIST_NOT_SET;
    }

    for (uint8_t i = 0; i < num_channels; i++)
    {
        bool found = false;
        for (unsigned j = 0; j < channel_list->num_channels; j++)
        {
            if (channel_list->channels[j].s1g_chan_num == channels[i] &&
                channel_list->channels[j].bw_mhz <= 2)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            MMLOG_WRN("channel[%u]=%u is not a valid <=2MHz channel\n", i, channels[i]);
            return MMWLAN_INVALID_ARGUMENT;
        }
    }
    return MMWLAN_SUCCESS;
}


static void umac_scan_free_request_buffers(struct umac_root_data *data)
{
    if (data->scan_request.args.extra_ies != NULL)
    {
        mmosal_free(data->scan_request.args.extra_ies);
        data->scan_request.args.extra_ies = NULL;
        data->scan_request.args.extra_ies_len = 0;
    }

    if (data->scan_request.args.selected_channels != NULL)
    {
        mmosal_free(data->scan_request.args.selected_channels);
        data->scan_request.args.selected_channels = NULL;
        data->scan_request.args.selected_channels_len = 0;
    }
}

static void umac_scan_complete_callback(struct umac_data *umacd, enum mmwlan_scan_state result_code)
{
    struct umac_root_data *data = umac_data_get_root(umacd);

    mmwlan_scan_complete_cb_t scan_complete_cb = data->scan_complete_cb;
    void *scan_cb_arg = data->scan_cb_arg;

    umac_scan_free_request_buffers(data);

    data->scan_rx_cb = NULL;
    data->scan_cb_arg = NULL;
    data->scan_complete_cb = NULL;

    scan_complete_cb(result_code, scan_cb_arg);
}

enum mmwlan_status mmwlan_scan_request(const struct mmwlan_scan_req *scan_req)
{
    MMOSAL_ASSERT(scan_req != NULL);
    MMOSAL_ASSERT(scan_req->scan_rx_cb != NULL);
    MMOSAL_ASSERT(scan_req->scan_complete_cb != NULL);

    struct umac_data *umacd = umac_data_get_umacd();
    struct umac_root_data *data = umac_data_get_root(umacd);

    if (data == NULL)
    {
        return MMWLAN_UNAVAILABLE;
    }

    if (data->scan_complete_cb != NULL)
    {
        MMLOG_ERR("Scan already in progress.\n");
        return MMWLAN_UNAVAILABLE;
    }

    if (umac_config_get_channel_list(umacd) == NULL)
    {
        MMLOG_ERR("Channel list not set\n");
        return MMWLAN_CHANNEL_LIST_NOT_SET;
    }

    enum mmwlan_status validate_status =
        validate_scan_channels(umacd,
                               scan_req->args.selected_channels,
                               scan_req->args.selected_channels_len);
    if (validate_status != MMWLAN_SUCCESS)
    {
        return validate_status;
    }

    enum mmwlan_status status = umac_core_start(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }


    data->scan_rx_cb = scan_req->scan_rx_cb;
    data->scan_complete_cb = scan_req->scan_complete_cb;
    data->scan_cb_arg = scan_req->scan_cb_arg;


    data->scan_request.rx_cb = umac_scan_rx_callback;
    data->scan_request.complete_cb = umac_scan_complete_callback;
    if (scan_req->args.ssid_len)
    {
        data->scan_request.args.ssid_len = scan_req->args.ssid_len;
        memcpy(data->scan_request.args.ssid, scan_req->args.ssid, scan_req->args.ssid_len);
    }
    else
    {
        data->scan_request.args.ssid_len = 0;
        memset(data->scan_request.args.ssid, 0, MMWLAN_SSID_MAXLEN);
    }
    data->scan_request.args.dwell_time_ms = scan_req->args.dwell_time_ms;
    data->scan_request.args.dwell_on_home_ms =
        umac_connection_get_state(umacd) == MMWLAN_STA_CONNECTED ? scan_req->args.dwell_on_home_ms :
                                                                   0;
    data->scan_request.args.extra_ies = NULL;
    data->scan_request.args.extra_ies_len = 0;
    if (scan_req->args.extra_ies_len)
    {

        data->scan_request.args.extra_ies = (uint8_t *)mmosal_malloc(scan_req->args.extra_ies_len);
        if (data->scan_request.args.extra_ies == NULL)
        {
            MMLOG_ERR("Failed to allocate buffer for extra_ies\n");
            return MMWLAN_NO_MEM;
        }
        memcpy(data->scan_request.args.extra_ies,
               scan_req->args.extra_ies,
               scan_req->args.extra_ies_len);
        data->scan_request.args.extra_ies_len = scan_req->args.extra_ies_len;
    }

    data->scan_request.args.selected_channels = NULL;
    data->scan_request.args.selected_channels_len = 0;
    if (scan_req->args.selected_channels_len)
    {
        data->scan_request.args.selected_channels =
            (uint8_t *)mmosal_malloc(scan_req->args.selected_channels_len);
        if (data->scan_request.args.selected_channels == NULL)
        {
            MMLOG_WRN("Failed to allocate buffer for selected_channels\n");
            umac_scan_free_request_buffers(data);
            return MMWLAN_NO_MEM;
        }
        uint8_t *selected_channels = (uint8_t *)data->scan_request.args.selected_channels;
        memcpy(selected_channels,
               scan_req->args.selected_channels,
               scan_req->args.selected_channels_len);
        data->scan_request.args.selected_channels_len = scan_req->args.selected_channels_len;
    }

    status = umac_scan_queue_request(umacd, &data->scan_request);
    if (status != MMWLAN_SUCCESS)
    {

        umac_scan_free_request_buffers(data);
        umac_stop_core_if_no_interface(umacd);
    }

    return status;
}

enum mmwlan_status mmwlan_scan_abort(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    struct umac_root_data *data = umac_data_get_root(umacd);

    if (data == NULL)
    {

        return MMWLAN_SUCCESS;
    }

    if (data->scan_complete_cb == NULL)
    {

        return MMWLAN_SUCCESS;
    }

    struct umac_evt evt = UMAC_EVT_INIT(umac_abort_scan_evt_handler);
    evt.args.req_scan.scan_req = &data->scan_request;
    bool ok = umac_core_evt_queue(umacd, &evt);
    if (!ok)
    {
        MMLOG_INF("Failed to queue event\n");
        umac_stop_core_if_no_interface(umacd);
        return MMWLAN_ERROR;
    }

    return MMWLAN_SUCCESS;
}

static void umac_ap_start_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    enum mmwlan_status status = umac_ap_enable_ap(umacd, evt->args.ap_start.args);
    if (evt->args.ap_start.status)
    {
        *evt->args.ap_start.status = status;
    }
    else
    {
        MMOSAL_ASSERT(status == MMWLAN_SUCCESS);
    }

    if (evt->args.ap_start.semb)
    {
        mmosal_semb_give(evt->args.ap_start.semb);
    }
}

enum mmwlan_status mmwlan_ap_enable(const struct mmwlan_ap_args *args)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();
    struct umac_root_data *data = umac_data_get_root(umacd);
    if (data == NULL)
    {
        return MMWLAN_UNAVAILABLE;
    }

    status = umac_core_start(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    UMAC_QUEUE_EVT_AND_WAIT(umac_ap_start_evt_handler, ap_start, &status, .args = args);
    if (status != MMWLAN_SUCCESS)
    {
        umac_stop_core_if_no_interface(umacd);
        return status;
    }

    if (args->async_start)
    {
        return MMWLAN_SUCCESS;
    }


    status = MMWLAN_TIMED_OUT;
    uint32_t excessive_timeout = mmosal_get_time_ms() + 100000;
    while (!mmosal_time_has_passed(excessive_timeout))
    {
        uint16_t vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP);
        if (vif_id != MMDRV_VIF_ID_INVALID)
        {
            return MMWLAN_SUCCESS;
        }
        mmosal_task_sleep(100);
    }

    MMOSAL_ASSERT(false);

    return status;
}

static void umac_ap_stop_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    enum mmwlan_status status = MMWLAN_SUCCESS;
    status = umac_ap_disable_ap(umacd);
    if (evt->args.ap_stop.status)
    {
        *evt->args.ap_stop.status = status;
    }
    if (evt->args.ap_stop.semb)
    {
        mmosal_semb_give(evt->args.ap_stop.semb);
    }
}

enum mmwlan_status mmwlan_ap_disable(void)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();


    if (!umac_core_is_running(umacd))
    {
        return MMWLAN_SUCCESS;
    }

    UMAC_QUEUE_EVT_AND_WAIT(umac_ap_stop_evt_handler, ap_stop, &status);

    umac_stop_core_if_no_interface(umacd);

    return status;
}

static void umac_set_relay_depth_override_evt_handler(struct umac_data *umacd,
                                                      const struct umac_evt *evt)
{
    if (umac_connection_get_state(umacd) != MMWLAN_STA_DISABLED || umac_data_get_ap(umacd) != NULL)
    {
        *evt->args.set_relay_depth_override.status = MMWLAN_UNAVAILABLE;
    }
    else
    {
        umac_config_set_relay_depth_override(umacd, evt->args.set_relay_depth_override.depth);
        *evt->args.set_relay_depth_override.status = MMWLAN_SUCCESS;
    }

    mmosal_semb_give(evt->args.set_relay_depth_override.semb);
}

enum mmwlan_status mmwlan_relay_set_depth_override(uint8_t depth)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (depth > MMWLAN_RELAY_MAX_DEPTH)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    if (!umac_core_is_running(umacd))
    {
        umac_config_set_relay_depth_override(umacd, depth);
        return MMWLAN_SUCCESS;
    }

    enum mmwlan_status status = MMWLAN_ERROR;
    UMAC_QUEUE_EVT_AND_WAIT(umac_set_relay_depth_override_evt_handler,
                            set_relay_depth_override,
                            &status,
                            .depth = depth);

    return status;
}

static void umac_relay_start_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    enum mmwlan_status status = umac_relay_enable(umacd, evt->args.relay_start.args);
    if (evt->args.relay_start.status)
    {
        *evt->args.relay_start.status = status;
    }
    else
    {
        MMOSAL_ASSERT(status == MMWLAN_SUCCESS);
    }

    if (evt->args.relay_start.semb)
    {
        mmosal_semb_give(evt->args.relay_start.semb);
    }
}

enum mmwlan_status mmwlan_relay_enable(const struct mmwlan_relay_args *args)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();

    bool ok = umac_relay_validate_relay_args(umacd, args);
    if (!ok)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    status = umac_core_start(umacd);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    UMAC_QUEUE_EVT_AND_WAIT(umac_relay_start_evt_handler, relay_start, &status, .args = args);

    umac_stop_core_if_no_interface(umacd);

    return status;
}

static void umac_relay_stop_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    enum mmwlan_status status = umac_relay_disable(umacd);
    if (evt->args.relay_stop.status)
    {
        *evt->args.relay_stop.status = status;
    }
    else
    {
        MMOSAL_ASSERT(status == MMWLAN_SUCCESS);
    }

    if (evt->args.relay_stop.semb)
    {
        mmosal_semb_give(evt->args.relay_stop.semb);
    }
}

enum mmwlan_status mmwlan_relay_disable(void)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();


    if (!umac_core_is_running(umacd))
    {
        return MMWLAN_SUCCESS;
    }

    UMAC_QUEUE_EVT_AND_WAIT(umac_relay_stop_evt_handler, relay_stop, &status);

    umac_stop_core_if_no_interface(umacd);

    return status;
}

enum mmwlan_relay_state mmwlan_relay_get_state(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_relay_get_state(umacd);
}

uint8_t mmwlan_relay_get_depth(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_relay_get_depth(umacd);
}

enum mmwlan_status mmwlan_register_depth_change_cb(mmwlan_relay_depth_change_cb_t callback,
                                                   void *arg)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_relay_register_depth_change_cb(umacd, callback, arg);
}

enum mmwlan_status mmwlan_get_vif_mac_addr(enum mmwlan_vif vif, uint8_t *mac_addr)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_interface_get_vif_mac_addr(umacd, vif, mac_addr);
}

enum mmwlan_status mmwlan_get_bssid(uint8_t *bssid)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_connection_get_bssid(umacd, bssid);
}

enum mmwlan_status mmwlan_get_vif_channel_info(enum mmwlan_vif vif,
                                               struct mmwlan_vif_channel_info *info)
{
    struct umac_data *umacd = umac_data_get_umacd();
    switch (vif)
    {
        case MMWLAN_VIF_STA:
            return umac_connection_get_channel_info(umacd, info);
        case MMWLAN_VIF_AP:
            return umac_ap_get_channel_info(umacd, info);
        default:
            return MMWLAN_VIF_ERROR;
    }
}

int32_t mmwlan_get_rssi(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_stats_get_rssi(umacd);
}

enum mmwlan_status mmwlan_set_control_response_preamble_1mhz_out_en(bool enabled)
{
    struct umac_data *umacd = umac_data_get_umacd();
    if (mmwlan_get_sta_state() != MMWLAN_STA_DISABLED)
    {
        return MMWLAN_UNAVAILABLE;
    }
    umac_config_set_ctrl_resp_out_1mhz_enabled(umacd, enabled);
    return MMWLAN_SUCCESS;
}

enum mmwlan_status mmwlan_register_link_state_cb(mmwlan_link_state_cb_t callback, void *arg)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_connection_register_link_cb(umacd, callback, arg);
}

enum mmwlan_status mmwlan_register_vif_state_cb(enum mmwlan_vif vif,
                                                mmwlan_vif_state_cb_t callback,
                                                void *arg)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_interface_register_vif_state_cb(umacd, vif, callback, arg);
}

enum mmwlan_status mmwlan_register_rx_cb(mmwlan_rx_cb_t callback, void *arg)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_datapath_register_rx_cb(umacd, callback, arg);
}

enum mmwlan_status mmwlan_register_rx_pkt_cb(mmwlan_rx_pkt_cb_t callback, void *arg)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_datapath_register_rx_pkt_cb(umacd, callback, arg);
}

enum mmwlan_status mmwlan_register_rx_pkt_ext_cb(enum mmwlan_vif vif,
                                                 mmwlan_rx_pkt_ext_cb_t callback,
                                                 void *arg)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_datapath_register_rx_pkt_ext_cb(umacd, vif, callback, arg);
}

enum mmwlan_status mmwlan_tx_pkt(struct mmpkt *pkt, const struct mmwlan_tx_metadata *_metadata)
{
    struct umac_data *umacd = umac_data_get_umacd();
    MMLOG_VRB("TX packet\n");

    struct mmwlan_tx_metadata metadata = MMWLAN_TX_METADATA_INIT;
    if (_metadata != NULL)
    {
        metadata = *_metadata;
    }

    if (metadata.tid > MMWLAN_MAX_QOS_TID)
    {
        MMLOG_DBG("Given TID (%d) is out of range, max %d.\n", metadata.tid, MMWLAN_MAX_QOS_TID);
        mmpkt_release(pkt);
        return MMWLAN_INVALID_ARGUMENT;
    }

    UMAC_TRACE("tx %x", pkt);

    mmdrv_get_tx_metadata(pkt)->tid = metadata.tid;

    umac_relay_update_tx_metadata(umacd, pkt, &metadata);

    uint16_t vif_id = MMDRV_VIF_ID_INVALID;

    if (metadata.vif == MMWLAN_VIF_STA)
    {
        vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_STA);
    }
    else if (metadata.vif == MMWLAN_VIF_AP)
    {
        vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP);
    }
    else if (metadata.vif == MMWLAN_VIF_UNSPECIFIED)
    {
        uint16_t vif_id_sta = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_STA);
        bool sta_vif_valid = vif_id_sta != MMDRV_VIF_ID_INVALID;
        uint16_t vif_id_ap = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP);
        bool ap_vif_valid = vif_id_ap != MMDRV_VIF_ID_INVALID;
        if (sta_vif_valid && ap_vif_valid)
        {
            MMLOG_ERR("Unable to infer VIF ID\n");
            mmpkt_release(pkt);
            return MMWLAN_VIF_ERROR;
        }
        else if (ap_vif_valid)
        {
            vif_id = vif_id_ap;
            MMLOG_DBG("Inferring AP VIF type (id %u)\n", vif_id);
        }
        else
        {
            vif_id = vif_id_sta;
            MMLOG_DBG("Inferring STA VIF type (id %u)\n", vif_id);
        }
    }

    if (vif_id == MMDRV_VIF_ID_INVALID)
    {
        MMLOG_WRN("No matching VIF (type=%u)\n", metadata.vif);
        mmpkt_release(pkt);
        return MMWLAN_VIF_ERROR;
    }

    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(pkt);
    tx_metadata->tid = metadata.tid;
    tx_metadata->vif_id = vif_id;

    return umac_datapath_tx_frame(umacd, pkt, ENCRYPTION_ENABLED, metadata.ra);
}

enum mmwlan_status mmwlan_tx_wait_until_ready(uint32_t timeout_ms)
{
    struct umac_data *umacd = umac_data_get_umacd();

    return umac_datapath_wait_for_tx_ready(umacd, timeout_ms);
}

static void umac_get_rc_stats_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
    if (stad != NULL)
    {
        MMOSAL_DEV_ASSERT(evt->args.get_rc_stats.stats != NULL);
        *(evt->args.get_rc_stats.stats) = umac_rc_get_rc_stats(stad);
    }
    mmosal_semb_give(evt->args.get_rc_stats.semb);
}

struct mmwlan_rc_stats *mmwlan_get_rc_stats(void)
{
    struct umac_data *umacd = umac_data_get_umacd();

    struct mmwlan_rc_stats *stats = NULL;

    struct umac_evt evt = UMAC_EVT_INIT_ARGS(umac_get_rc_stats_handler,
                                             get_rc_stats,
                                             .semb = mmosal_semb_create("ev"),
                                             .stats = &stats);

    if (evt.args.get_rc_stats.semb == NULL)
    {
        return NULL;
    }

    bool ok = umac_core_evt_queue(umacd, &evt);
    if (ok)
    {
        mmosal_semb_wait(evt.args.get_rc_stats.semb, UINT32_MAX);
    }
    mmosal_semb_delete(evt.args.get_rc_stats.semb);

    return stats;
}

void mmwlan_free_rc_stats(struct mmwlan_rc_stats *stats)
{
    umac_rc_free_rc_stats(stats);
}

static void umac_set_wnm_sleep_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{

    if (umac_connection_get_state(umacd) == MMWLAN_STA_CONNECTED)
    {
        enum mmwlan_status status = umac_wnm_sleep_register_semb(umacd,
                                                                 evt->args.set_wnm_sleep.semb,
                                                                 evt->args.set_wnm_sleep.status);
        if (status != MMWLAN_SUCCESS)
        {
            *evt->args.set_wnm_sleep.status = status;
            mmosal_semb_give(evt->args.set_wnm_sleep.semb);
            return;
        }

        if (evt->args.set_wnm_sleep.enabled)
        {
            umac_wnm_sleep_set_chip_powerdown(umacd,
                                              evt->args.set_wnm_sleep.chip_powerdown_enabled);
            umac_wnm_sleep_report_event(umacd, UMAC_WNM_SLEEP_EVENT_REQUEST_ENTRY);
        }
        else
        {
            umac_wnm_sleep_report_event(umacd, UMAC_WNM_SLEEP_EVENT_REQUEST_EXIT);
        }
    }
    else if (evt->args.set_wnm_sleep.semb != NULL)
    {
        *evt->args.set_wnm_sleep.status = MMWLAN_UNAVAILABLE;
        mmosal_semb_give(evt->args.set_wnm_sleep.semb);
    }
}

enum mmwlan_status mmwlan_set_wnm_sleep_enabled_ext(
    const struct mmwlan_set_wnm_sleep_enabled_args *args)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    UMAC_QUEUE_EVT_AND_WAIT(umac_set_wnm_sleep_evt_handler,
                            set_wnm_sleep,
                            &status,
                            .enabled = args->wnm_sleep_enabled,
                            .chip_powerdown_enabled = args->chip_powerdown_enabled);

    return status;
}

static void umac_set_beacon_vendor_ie_filter_evt_handler(struct umac_data *umacd,
                                                         const struct umac_evt *evt)
{
    *evt->args.update_beacon_vendor_ie_filter.status =
        umac_connection_update_beacon_vendor_ie_filter(
            umacd,
            evt->args.update_beacon_vendor_ie_filter.filter);
    mmosal_semb_give(evt->args.update_beacon_vendor_ie_filter.semb);
}

enum mmwlan_status mmwlan_update_beacon_vendor_ie_filter(
    const struct mmwlan_beacon_vendor_ie_filter *filter)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (filter != NULL)
    {

        if ((filter->cb == NULL) || filter->n_ouis > MMWLAN_BEACON_VENDOR_IE_MAX_OUI_FILTERS)
        {
            return MMWLAN_INVALID_ARGUMENT;
        }
    }

    UMAC_QUEUE_EVT_AND_WAIT(umac_set_beacon_vendor_ie_filter_evt_handler,
                            update_beacon_vendor_ie_filter,
                            &status,
                            .filter = filter);
    if (status == MMWLAN_NOT_RUNNING)
    {

        MMLOG_DBG("Failed to queue UPDATE_BEACON_VENDOR_IE_FILTER. Setting config directly\n");
        umac_config_set_beacon_vendor_ie_filter(umacd, filter);
        return MMWLAN_SUCCESS;
    }

    return status;
}

static void umac_add_vendor_ie_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    const struct mmwlan_vendor_ie *ie = evt->args.add_vendor_ie.ie;
    *evt->args.add_vendor_ie.status = umac_config_add_vendor_ie(umacd, ie);

    mmosal_semb_give(evt->args.add_vendor_ie.semb);
}

enum mmwlan_status mmwlan_add_vendor_ie(const struct mmwlan_vendor_ie *ie)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (ie == NULL ||
        ie->data == NULL ||
        ie->data_len < MMWLAN_VENDOR_IE_MIN_SIZE ||
        ie->mgmt_type_mask & ~MMWLAN_VENDOR_IE_MGMT_ALL ||
        ie->mgmt_type_mask == 0)
    {
        MMLOG_DBG("Invalid argument\n");
        return MMWLAN_INVALID_ARGUMENT;
    }

    UMAC_QUEUE_EVT_AND_WAIT(umac_add_vendor_ie_evt_handler, add_vendor_ie, &status, .ie = ie);
    if (status == MMWLAN_NOT_RUNNING)
    {

        MMLOG_DBG("Failed to queue ADD_VENDOR_IE. Setting config directly\n");
        status = umac_config_add_vendor_ie(umacd, ie);
    }

    return status;
}

static void umac_clear_vendor_ies_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    const uint8_t mask = evt->args.clear_vendor_ies.mgmt_type_mask;
    umac_config_clear_vendor_ies(umacd, mask);

    enum mmwlan_status status = MMWLAN_SUCCESS;

    *evt->args.clear_vendor_ies.status = status;
    mmosal_semb_give(evt->args.clear_vendor_ies.semb);
}

enum mmwlan_status mmwlan_clear_vendor_ies(uint8_t mgmt_type_mask)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (mgmt_type_mask == 0 || mgmt_type_mask & ~MMWLAN_VENDOR_IE_MGMT_ALL)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    UMAC_QUEUE_EVT_AND_WAIT(umac_clear_vendor_ies_evt_handler,
                            clear_vendor_ies,
                            &status,
                            .mgmt_type_mask = mgmt_type_mask);
    if (status == MMWLAN_NOT_RUNNING)
    {

        MMLOG_DBG("Failed to queue CLEAR_VENDOR_IES. Setting config directly\n");
        umac_config_clear_vendor_ies(umacd, mgmt_type_mask);
        status = MMWLAN_SUCCESS;
    }

    return status;
}

enum mmwlan_status mmwlan_set_fragment_threshold(unsigned fragment_threshold)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (fragment_threshold != 0 && fragment_threshold < MMWLAN_MINIMUM_FRAGMENT_THRESHOLD)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    enum mmwlan_status status = mmdrv_set_frag_threshold(fragment_threshold);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to set fragmentation threshold.\n");
        return status;
    }

    umac_config_set_frag_threshold(umacd, fragment_threshold);

    return MMWLAN_SUCCESS;
}

#if !(defined(DISABLE_MMWLAN_HEALTH_CHECK) && DISABLE_MMWLAN_HEALTH_CHECK)
enum mmwlan_status mmwlan_set_health_check_interval(uint32_t min_interval_ms,
                                                    uint32_t max_interval_ms)
{
    struct umac_data *umacd = umac_data_get_umacd();
    enum mmwlan_status status =
        umac_health_check_set_interval(umacd, min_interval_ms, max_interval_ms);
    if (status == MMWLAN_SUCCESS)
    {
        umac_health_check_schedule_timeout(umacd, 0);
    }
    return status;
}
#else
enum mmwlan_status mmwlan_set_health_check_interval(uint32_t min_interval_ms,
                                                    uint32_t max_interval_ms)
{
    if ((min_interval_ms == 0) && (max_interval_ms == 0))
    {

        return MMWLAN_SUCCESS;
    }

    return MMWLAN_NOT_SUPPORTED;
}
#endif

static void umac_set_scan_config_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    umac_config_set_supp_scan_dwell_time(umacd, evt->args.scan_config.dwell_time_ms);
    umac_config_set_supp_scan_home_dwell_time(umacd,
                                              evt->args.scan_config.home_channel_dwell_time_ms);
    umac_config_set_ndp_probe_support(umacd, evt->args.scan_config.ndp_probe_enabled);
    (void)umac_interface_set_ndp_probe_support(umacd, evt->args.scan_config.ndp_probe_enabled);

    umac_config_set_selective_scan_channels(umacd,
                                            evt->args.scan_config.selected_channels,
                                            evt->args.scan_config.selected_channels_len,
                                            evt->args.scan_config.selective_scan_attempts);
}

enum mmwlan_status mmwlan_set_scan_config(const struct mmwlan_scan_config *config)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (config->dwell_time_ms < MMWLAN_SCAN_MIN_DWELL_TIME_MS)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    enum mmwlan_status validate_status =
        validate_scan_channels(umacd, config->selected_channels, config->selected_channels_len);
    if (validate_status != MMWLAN_SUCCESS)
    {
        return validate_status;
    }


    uint8_t *selective_channels = NULL;
    uint8_t selective_channels_len = 0;
    if (config->selected_channels_len != 0)
    {
        selective_channels = (uint8_t *)mmosal_malloc(config->selected_channels_len);
        if (selective_channels == NULL)
        {
            return MMWLAN_NO_MEM;
        }
        memcpy(selective_channels, config->selected_channels, config->selected_channels_len);
        selective_channels_len = config->selected_channels_len;
    }

    struct umac_evt evt = UMAC_EVT_INIT(umac_set_scan_config_evt_handler);
    memcpy(&evt.args.scan_config, config, sizeof(evt.args.scan_config));

    evt.args.scan_config.selected_channels = selective_channels;
    evt.args.scan_config.selected_channels_len = selective_channels_len;
    evt.args.scan_config.selective_scan_attempts = config->selective_scan_attempts;

    bool ok = umac_core_evt_queue(umacd, &evt);
    if (!ok)
    {

        MMLOG_DBG("Failed to queue SET_SCAN_CONFIG event. Setting config directly\n");

        umac_set_scan_config_evt_handler(umacd, &evt);
    }

    return MMWLAN_SUCCESS;
}

enum mmwlan_status mmwlan_twt_add_configuration(
    const struct mmwlan_twt_config_args *twt_config_args)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }


    if (umac_connection_get_state(umacd) != MMWLAN_STA_DISABLED)
    {
        return MMWLAN_UNAVAILABLE;
    }

    if (twt_config_args->twt_min_wake_duration_us >= twt_config_args->twt_wake_interval_us)
    {
        MMLOG_DBG(
            "Failed to add twt configuration. Wake duration must be less than wake interval.");
        return MMWLAN_INVALID_ARGUMENT;
    }

    return umac_twt_add_configuration(umacd, twt_config_args);
}

static void umac_morse_stats_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    MM_UNUSED(umacd);

    enum mmwlan_status status = mmdrv_get_stats(evt->args.morse_stats.core_num,
                                                evt->args.morse_stats.buf,
                                                evt->args.morse_stats.buf_len);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_INF("Failed to get stats (%u)\n", status);
        *evt->args.morse_stats.status = status;
        goto exit;
    }

    if (evt->args.morse_stats.reset_stats)
    {
        MMLOG_INF("Resetting stats for core %lu\n", evt->args.morse_stats.core_num);
        status = mmdrv_reset_stats(evt->args.morse_stats.core_num);
        if (status != MMWLAN_SUCCESS)
        {
            MMLOG_WRN("Stats reset failed (%u)\n", status);
        }
    }

    *evt->args.morse_stats.status = MMWLAN_SUCCESS;
exit:
    mmosal_semb_give(evt->args.morse_stats.semb);
}

#define MORSE_STATS_BUF_LEN (1600)

struct mmwlan_morse_stats *mmwlan_get_morse_stats(uint32_t core_num, bool reset)
{
    struct umac_data *umacd = umac_data_get_umacd();

    uint8_t *buf = (uint8_t *)mmosal_malloc(MORSE_STATS_BUF_LEN);
    if (buf == NULL)
    {
        MMLOG_WRN("Failed to allocate buffer for stats\n");
        return NULL;
    }

    uint8_t *stats_start = buf;
    uint32_t stats_len = MORSE_STATS_BUF_LEN;

    enum mmwlan_status status = MMWLAN_ERROR;
    UMAC_QUEUE_EVT_AND_WAIT(umac_morse_stats_evt_handler,
                            morse_stats,
                            &status,
                            .buf = &stats_start,
                            .buf_len = &stats_len,
                            .core_num = core_num,
                            .reset_stats = reset);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to get stats for core %u with status %u\n", core_num, status);
        mmosal_free(buf);
        return NULL;
    }


    struct mmwlan_morse_stats *stats = (struct mmwlan_morse_stats *)buf;

    MMOSAL_ASSERT((stats_start - buf) >= (int32_t)sizeof(*stats));
    stats->buf = stats_start;
    stats->len = stats_len;

    return stats;
}

void mmwlan_free_morse_stats(struct mmwlan_morse_stats *stats)
{
    mmosal_free(stats);
}

enum mmwlan_status mmwlan_get_umac_stats(struct mmwlan_stats_umac_data *stats_dest)
{
    if (stats_dest == NULL)
    {
        MMLOG_WRN("Unable to store data in NULL pointer stats_dest.\n");
        return MMWLAN_INVALID_ARGUMENT;
    }

    struct umac_data *umacd = umac_data_get_umacd();
    return umac_stats_get_all(umacd, stats_dest);
}

enum mmwlan_status mmwlan_clear_umac_stats(void)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_stats_clear_all(umacd);
}

static void umac_core_assert_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    MM_UNUSED(umacd);

    (void)mmdrv_trigger_core_assert(evt->args.core_assert.core_id);
}

void mmwlan_trigger_core_assert(uint32_t core_id)
{
    struct umac_data *umacd = umac_data_get_umacd();

    struct umac_evt evt = UMAC_EVT_INIT(umac_core_assert_evt_handler);
    evt.args.core_assert.core_id = core_id;
    bool ok = umac_core_evt_queue(umacd, &evt);
    if (!ok)
    {
        MMLOG_DBG("Failed to queue CORE_ASSERT event.\n");
    }
}

enum mmwlan_status mmwlan_set_mcs10_mode(enum mmwlan_mcs10_mode mcs10_mode)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    umac_config_set_mcs10_mode(umacd, mcs10_mode);
    return MMWLAN_SUCCESS;
}

enum mmwlan_status mmwlan_set_duty_cycle_mode(enum mmwlan_duty_cycle_mode duty_cycle_mode)
{
    struct umac_data *umacd = umac_data_get_umacd();

    switch (duty_cycle_mode)
    {
        case MMWLAN_DUTY_CYCLE_MODE_SPREAD:
        case MMWLAN_DUTY_CYCLE_MODE_BURST:
            break;

        default:
            MMLOG_ERR("Unknown duty cycle mode 0x%02x\n", duty_cycle_mode);
            return MMWLAN_INVALID_ARGUMENT;
    }

    MMLOG_INF("Duty cycle set to %s mode\n", duty_cycle_mode ? "burst" : "spread");

    umac_config_set_duty_cycle_mode(umacd, duty_cycle_mode);
    return MMWLAN_SUCCESS;
}

enum mmwlan_status mmwlan_get_duty_cycle_stats(struct mmwlan_duty_cycle_stats *stats)
{
    if (stats == NULL)
    {
        MMLOG_ERR("stats pointer is NULL\n");
        return MMWLAN_INVALID_ARGUMENT;
    }
    return mmdrv_get_duty_cycle(stats);
}





enum mmwlan_status mmwlan_ate_override_rate_control(enum mmwlan_mcs tx_rate_override,
                                                    enum mmwlan_bw bandwidth_override,
                                                    enum mmwlan_gi gi_override)
{
    struct umac_data *umacd = umac_data_get_umacd();

    struct umac_config_rc_override rc_override = {
        .tx_rate = tx_rate_override,
        .bandwidth = bandwidth_override,
        .guard_interval = gi_override,
    };

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    umac_config_rc_set_override(umacd, &rc_override);

    return MMWLAN_SUCCESS;
}

static void umac_exec_cmd_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    *evt->args.exec_cmd.status = umac_ate_execute_command(umacd,
                                                          evt->args.exec_cmd.command,
                                                          evt->args.exec_cmd.command_len,
                                                          evt->args.exec_cmd.response,
                                                          evt->args.exec_cmd.response_len);
    mmosal_semb_give(evt->args.exec_cmd.semb);
}

enum mmwlan_status mmwlan_ate_execute_command(uint8_t *command,
                                              uint32_t command_len,
                                              uint8_t *response,
                                              uint32_t *response_len)
{
    volatile enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_data *umacd = umac_data_get_umacd();

    UMAC_QUEUE_EVT_AND_WAIT(umac_exec_cmd_handler,
                            exec_cmd,
                            &status,
                            .command = command,
                            .command_len = command_len,
                            .response = response,
                            .response_len = response_len);

    return status;
}

enum mmwlan_status mmwlan_set_listen_interval(uint16_t interval)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (umac_connection_get_state(umacd) != MMWLAN_STA_DISABLED)
    {
        return MMWLAN_UNAVAILABLE;
    }

    umac_config_set_listen_interval(umacd, interval);

    return MMWLAN_SUCCESS;
}

static void umac_set_beacon_loss_count_evt_handler(struct umac_data *umacd,
                                                   const struct umac_evt *evt)
{
    uint8_t beacon_loss_count = evt->args.set_beacon_loss_count.beacon_loss_count;

    umac_config_set_beacon_loss_count(umacd, beacon_loss_count);

    *evt->args.set_beacon_loss_count.status = MMWLAN_SUCCESS;


    uint16_t vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_STA);
    if (vif_id != MMDRV_VIF_ID_INVALID)
    {
        *evt->args.set_beacon_loss_count.status =
            mmdrv_set_param(vif_id, MORSE_PARAM_ID_BEACON_LOSS_COUNT, beacon_loss_count);
    }

    mmosal_semb_give(evt->args.set_beacon_loss_count.semb);
}

enum mmwlan_status mmwlan_set_beacon_loss_threshold(uint8_t threshold)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (threshold == UINT8_MAX)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    if (!umac_core_is_running(umacd))
    {
        umac_config_set_beacon_loss_count(umacd, threshold);
        return MMWLAN_SUCCESS;
    }

    enum mmwlan_status status = MMWLAN_ERROR;
    UMAC_QUEUE_EVT_AND_WAIT(umac_set_beacon_loss_count_evt_handler,
                            set_beacon_loss_count,
                            &status,
                            .beacon_loss_count = threshold);

    return status;
}

enum mmwlan_status mmwlan_ate_get_key_info(struct mmwlan_key_info *key_info,
                                           uint32_t *key_info_count)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    return umac_ate_get_key_info(umacd, key_info, key_info_count);
}

enum mmwlan_status mmwlan_register_sleep_cb(mmwlan_sleep_cb_t callback, void *arg)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_core_register_sleep_cb(umacd, callback, arg);
}

static void umac_tx_mgmt_frame_evt_handler(struct umac_data *umacd, const struct umac_evt *evt)
{
    enum mmwlan_status status;
    bool has_sta_interface = false;
    struct mmpkt *txbuf = evt->args.tx_mgmt_frame.txbuf;
    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
    if (stad == NULL)
    {
        mmpkt_release(txbuf);
        return;
    }

    has_sta_interface = (umac_sta_data_get_vif_id(stad) != MMDRV_VIF_ID_INVALID);
    if (!has_sta_interface)
    {
        status = umac_connection_start_preassoc(umacd);
        if (status != MMWLAN_SUCCESS)
        {
            mmpkt_release(txbuf);
            return;
        }
    }
    umac_datapath_tx_mgmt_frame(stad, txbuf);
    if (!has_sta_interface)
    {
        umac_connection_stop_preassoc(umacd);
    }
}

enum mmwlan_status mmwlan_tx_mgmt_frame(struct mmpkt *txbuf)
{
    struct umac_data *umacd = umac_data_get_umacd();

    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (txbuf == NULL)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    struct umac_evt evt =
        UMAC_EVT_INIT_ARGS(umac_tx_mgmt_frame_evt_handler, tx_mgmt_frame, .txbuf = txbuf);
    bool ok = umac_core_evt_queue(umacd, &evt);
    return ok ? MMWLAN_SUCCESS : MMWLAN_ERROR;
}

enum mmwlan_status mmwlan_register_rx_frame_cb(uint32_t filter,
                                               mmwlan_rx_frame_cb_t callback,
                                               void *arg)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_datapath_register_rx_frame_cb(umacd, filter, callback, arg);
}

enum mmwlan_status mmwlan_get_serialized_umac_stats(uint8_t *buf, size_t *len)
{
    struct umac_data *umacd = umac_data_get_umacd();
    int ret = umac_stats_serialise(umacd, buf, *len);
    if (ret < 0)
    {
        MMLOG_WRN("Stats buffer too short\n");
        return MMWLAN_NO_MEM;
    }

    *len = ret;
    return MMWLAN_SUCCESS;
}

static void umac_get_ap_sta_status(struct umac_data *umacd, const struct umac_evt *evt)
{
    *evt->args.ap_get_sta_status.status =
        umac_ap_get_sta_status(umacd,
                               evt->args.ap_get_sta_status.sta_addr,
                               evt->args.ap_get_sta_status.sta_status);
    mmosal_semb_give(evt->args.ap_get_sta_status.semb);
}

enum mmwlan_status mmwlan_ap_get_sta_status(const uint8_t *sta_addr,
                                            struct mmwlan_ap_sta_status *sta_status)
{
    if (sta_status != NULL)
    {
        memset(sta_status, 0, sizeof(*sta_status));
    }

    struct umac_data *umacd = umac_data_get_umacd();
    if (!umac_data_is_initialised(umacd))
    {
        return MMWLAN_NOT_INITIALIZED;
    }

    if (umac_core_evtloop_is_active(umacd))
    {
        return umac_ap_get_sta_status(umacd, sta_addr, sta_status);
    }
    else
    {
        enum mmwlan_status status = MMWLAN_ERROR;
        UMAC_QUEUE_EVT_AND_WAIT(umac_get_ap_sta_status,
                                ap_get_sta_status,
                                &status,
                                .sta_addr = sta_addr,
                                .sta_status = sta_status);

        return status;
    }
}

enum mmwlan_status mmwlan_register_tx_flow_control_cb(mmwlan_tx_flow_control_cb_t cb, void *arg)
{
    struct umac_data *umacd = umac_data_get_umacd();
    return umac_datapath_register_tx_flow_control_cb(umacd, cb, arg);
}

void mmwlan_register_fatal_error_handler(mmwlan_fatal_error_handler_t handler, void *arg)
{
    struct umac_data *umacd = umac_data_get_umacd();
    struct umac_root_data *data = umac_data_get_root(umacd);
    data->fatal_error_handler = handler;
    data->fatal_error_handler_arg = arg;
}

void umac_fatal_error(struct umac_data *umacd, uint32_t fileid, uint32_t line)
{

    struct umac_root_data *data = umac_data_get_root(umacd);
    if (data->fatal_error)
    {
        MMLOG_WRN("umac_fatal_error() called during fatal error handling.\n");
        return;
    }
    data->fatal_error = true;
    MMLOG_ERR("Fatal error occurred on line %d of file %08X\n", line, fileid);

    if (umac_core_evtloop_is_active(umacd))
    {

        struct umac_evt evt;
        umac_shutdown_evt_handler(umacd, &evt);
    }
    else
    {

        MMOSAL_DEV_ASSERT(false);
        struct umac_evt evt = UMAC_EVT_INIT(umac_shutdown_evt_handler);
        umac_core_evt_queue_at_start(umacd, &evt);
    }
    struct mmwlan_fatal_error_args args = {
        .fileid = fileid,
        .line = line,
    };
    data->fatal_error_handler_arg = (void *)&args;

    umac_core_stop(umacd);
}

void umac_last_will_and_testament(struct umac_data *umacd)
{
    struct umac_root_data *data = umac_data_get_root(umacd);
    mmwlan_fatal_error_handler_t handler = data->fatal_error_handler;
    bool fatal_error = data->fatal_error;
    data->fatal_error = false;
    if (fatal_error && handler != NULL)
    {
        handler((struct mmwlan_fatal_error_args *)data->fatal_error_handler_arg);
    }
}


