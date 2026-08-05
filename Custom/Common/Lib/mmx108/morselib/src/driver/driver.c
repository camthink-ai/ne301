/*
 * Copyright 2021-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <errno.h>

#include "common/morse_commands.h"
#include "common/morse_command_utils.h"
#include "common/mac_address.h"
#include "mmdrv.h"
#include "mmutils.h"
#include "mmwlan.h"
#include "driver/morse_driver/firmware.h"
#include "driver/morse_driver/skb_header.h"
#include "driver/morse_driver/command.h"
#include "driver/morse_driver/ps.h"
#include "driver/morse_driver/hw.h"
#include "driver/transport/morse_transport.h"
#include "driver.h"
#include "driver/health/driver_health.h"
#include "driver/beacon/beacon.h"
#include "mmhal_wlan.h"

#ifdef ENABLE_DRV_TRACE
#include "mmtrace.h"
static mmtrace_channel drv_channel_handle;
#define DRV_TRACE_INIT()     drv_channel_handle = mmtrace_register_channel("drv")
#define DRV_TRACE(_fmt, ...) mmtrace_printf(drv_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define DRV_TRACE_INIT() \
    do {                 \
    } while (0)
#define DRV_TRACE(_fmt, ...) \
    do {                     \
    } while (0)
#endif


SPINLOCK_TRACE_DECLARE


static struct driver_data driver_data;

void mmdrv_pre_init(void)
{
    morse_trns_init();
}

void mmdrv_post_deinit(void)
{
    morse_trns_deinit();
}

static void morse_stale_tx_status_timer_cb(struct mmosal_timer *timer)
{
    struct driver_data *driverd = (struct driver_data *)mmosal_timer_get_arg(timer);
    MMOSAL_ASSERT(driverd == &driver_data);

    if (!driverd || !driverd->stale_status.enabled)
    {
        return;
    }

    driver_task_notify_event(driverd, DRV_EVT_STALE_TX_STATUS_PEND);
}

static int morse_stale_tx_status_timer_init(struct driver_data *driverd)
{
    driverd->stale_status.timer = mmosal_timer_create("stale_status_timer",
                                                      morse_skbq_get_tx_status_lifetime_ms(),
                                                      false,
                                                      (void *)(uintptr_t)driverd,
                                                      morse_stale_tx_status_timer_cb);

    if (driverd->stale_status.timer == NULL)
    {
        MMLOG_ERR("Failed to init stale_status_timer\n");
        return -1;
    }

    driverd->stale_status.enabled = 1;

    return 0;
}

static int morse_stale_tx_status_timer_finish(struct driver_data *driverd)
{
    if (!driverd->stale_status.enabled)
    {
        return 0;
    }

    driverd->stale_status.enabled = 0;

    mmosal_timer_delete(driverd->stale_status.timer);
    driverd->stale_status.timer = NULL;

    return 0;
}


static int mmdrv_fetch_fw_version(struct driver_data *driverd, struct mmdrv_fw_version *fw_version)
{
    int result = -ENOSPC;
    char version_string[MMWLAN_FW_VERSION_MAXLEN] = { 0 };
    int major;
    int minor;
    int patch;


    size_t max_version_resp_size =
        sizeof(struct morse_cmd_resp_get_version) +
        (sizeof(((struct morse_cmd_resp_get_version *)0)->version[0]) * MORSE_CMD_MAX_VERSION_LEN +
         1);


    struct morse_cmd_resp_get_version *resp =
        (struct morse_cmd_resp_get_version *)mmosal_malloc(max_version_resp_size);
    if (resp == NULL)
    {
        return -ENOSPC;
    }

    struct morse_cmd_req_get_version cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_GET_VERSION, UNKNOWN_VIF_ID);
	
    result = morse_cmd_tx(driverd,
                          (struct morse_cmd_resp *)resp,
                          (struct morse_cmd_req *)&cmd,
                          max_version_resp_size,
                          0);
    if (result == 0)
    {
        uint32_t length = MM_MIN(resp->length, (int32_t)(sizeof(version_string) - 1));
        memcpy(version_string, resp->version, length);
        version_string[length] = '\0';
    }
    else
    {
        MMLOG_ERR("Get version failed\n");
    }

    mmosal_free(resp);

    if (result != 0)
    {
        return result;
    }

    MMLOG_INF("Chip raw firmware version: %s\n", version_string);

    if (sscanf(version_string, "rel_%d_%d_%d", &major, &minor, &patch) != 3)
    {
        MMLOG_ERR("Unreleased FW version detected: %s\n", version_string);
        major = 0;
        minor = 0;
        patch = 0;
    }

    if ((major > UINT8_MAX) || (minor > UINT8_MAX) || (patch > UINT8_MAX))
    {
        MMLOG_ERR("FW version out of range\n");
        return -1;
    }

    fw_version->major = major;
    fw_version->minor = minor;
    fw_version->patch = patch;

    return 0;
}


static bool mmdrv_valid_fw_flags(uint32_t firmware_flags)
{
    if ((firmware_flags & MORSE_FW_FLAGS_SUPPORT_S1G) == 0)
    {
        return false;
    }

    if ((firmware_flags & MORSE_FW_FLAGS_SUPPORT_HW_SCAN) == 0)
    {
        return false;
    }

    if ((firmware_flags & MORSE_FW_FLAGS_REPORTS_TX_BEACON_COMPLETION) == 0)
    {
        return false;
    }

    if ((firmware_flags & MORSE_FW_FLAGS_STA_IFACE_MANAGE_SNS_BASELINE) == 0)
    {
        return false;
    }

    if ((firmware_flags & MORSE_FW_FLAGS_STA_IFACE_MANAGE_SNS_INDIV_ADDR_QOS_DATA) == 0)
    {
        return false;
    }

    if ((firmware_flags & MORSE_FW_FLAGS_STA_IFACE_MANAGE_SNS_QOS_NULL) == 0)
    {
        return false;
    }

    return true;
}

int mmdrv_init(struct mmdrv_chip_info *chip_info, const char *country_code)
{
    uint8_t *mac_addr = (chip_info != NULL) ? chip_info->mac_addr : NULL;
    int result = -1;

    DRV_TRACE_INIT();

    DRV_TRACE("init");

    memset(&driver_data, 0, sizeof(driver_data));

    driver_data.cfg = mmhal_get_chip();
    MMOSAL_ASSERT(driver_data.cfg != NULL);

    driver_data.beacon.vif_id = 0xffff;

    result = morse_trns_start(&driver_data);
    if (result != MORSE_SUCCESS)
    {
        // MMLOG_ERR("Transport init failed\n");
        result = -333; // indicate to upper layer that the hardware is not available
        goto error_transport;
    }

    if (driver_data.cfg->enable_sdio_burst_mode)
    {
        driver_data.cfg->enable_sdio_burst_mode(&driver_data);
    }

    MMLOG_DBG("Transport initialised\n");


    if (chip_info != NULL)
    {
        chip_info->morse_chip_id = driver_data.chip_id;
        chip_info->morse_chip_id_string = driver_data.cfg->get_hw_version(chip_info->morse_chip_id);
    }

    MMOSAL_ASSERT(country_code != NULL);
    driver_data.country[0] = country_code[0];
    driver_data.country[1] = country_code[1];
    driver_data.country[2] = '\0';

    result = morse_firmware_init(&driver_data, mmhal_wlan_read_fw_file, mmhal_wlan_read_bcf_file);
    if (result != 0)
    {
        MMLOG_ERR("Firmware init failed\n");
        goto error_firmware;
    }

    MMLOG_DBG("Firmware downloaded/booted\n");

    result = driver_data.cfg->ops->init(&driver_data);
    if (result)
    {
        MMLOG_ERR("Pageset init failed\n");
        result = -ENOMEM;
        goto error_pageset;
    }

    MMLOG_DBG("Pagesets initialised\n");

    result = morse_firmware_parse_extended_host_table(&driver_data, mac_addr);
    if (result)
    {
        MMLOG_ERR("Failed to parse extended host table\n");
        goto error_hosttable;
    }

    if (!mmdrv_valid_fw_flags(driver_data.firmware_flags))
    {
        MMLOG_ERR("FW does not have the required capabilities 0x%08x\n",
                  driver_data.firmware_flags);
        MMOSAL_ASSERT(false);
    }

    driver_data.lock = mmosal_mutex_create("driverd");
    if (driver_data.lock == NULL)
    {
        MMLOG_ERR("Mutex creation failed\n");
        result = -ENOMEM;
        goto error_mutex;
    }

    result = driver_task_start(&driver_data);
    if (result != 0)
    {
        MMLOG_ERR("Driver task start failed\n");
        goto error_task;
    }

    result = morse_cmd_init(&driver_data);
    if (result)
    {

        goto error_cmd;
    }


    result = morse_ps_init(&driver_data);
    if (result)
    {
        MMLOG_ERR("Power Save init failed\n");
        goto error_ps;
    }
    MMLOG_DBG("Power Save initialized\n");

    result = driver_health_init(&driver_data);
    if (result)
    {
        MMLOG_ERR("Health init failed\n");
        goto error_health;
    }
    MMLOG_DBG("Health check initialized\n");

    morse_trns_set_irq_enabled(&driver_data, true);

    MMLOG_DBG("SPI IRQ enabled\n");

    driver_data.started = true;

    if (chip_info != NULL)
    {
        result = mmdrv_fetch_fw_version(&driver_data, &chip_info->fw_version);
        if (result)
        {
            goto error_fw_version;
        }
    }

    morse_stale_tx_status_timer_init(&driver_data);

    MMLOG_DBG("mmdrv_init success\n");
    DRV_TRACE("init success");

    return MORSE_SUCCESS;

error_fw_version:
    driver_health_deinit(&driver_data);
error_health:
    morse_ps_deinit(&driver_data);
error_ps:
    morse_cmd_deinit(&driver_data);
error_cmd:
    mmosal_mutex_delete(driver_data.lock);
    driver_data.lock = NULL;
error_mutex:
error_hosttable:
    driver_data.cfg->ops->finish(&driver_data);
error_pageset:
    driver_task_stop(&driver_data);
error_task:
error_firmware:
    morse_trns_stop(&driver_data);
error_transport:
    memset(&driver_data, 0, sizeof(driver_data));
    return result;
}

#ifdef UNIT_TESTS
void mmdrv_init_for_unit_tests(void)
{
    struct mmhal_wlan_pktmem_init_args args = { NULL };
    mmhal_wlan_pktmem_init(&args);
    memset(&driver_data, 0, sizeof(driver_data));
    driver_data.started = true;
}

#endif

void mmdrv_deinit(void)
{
    MMLOG_DBG("\n");

    DRV_TRACE("deinit");


    MMOSAL_ASSERT(driver_data.started);

    driver_data.started = false;

    driver_health_deinit(&driver_data);
    morse_stale_tx_status_timer_finish(&driver_data);

    morse_trns_set_irq_enabled(&driver_data, false);

    driver_task_stop(&driver_data);

    mmosal_mutex_delete(driver_data.lock);
    driver_data.lock = NULL;

    morse_cmd_deinit(&driver_data);

    driver_data.cfg->ops->finish(&driver_data);

    morse_trns_stop(&driver_data);

    morse_ps_deinit(&driver_data);

    memset(&driver_data, 0, sizeof(driver_data));

    DRV_TRACE("deinit done");
}

int mmdrv_get_bcf_metadata(struct mmwlan_bcf_metadata *metadata)
{
    return morse_bcf_get_metadata(metadata);
}

int mmdrv_set_param(uint16_t vif_id, enum morse_param_id param_id, uint32_t value)
{
    struct morse_cmd_req_get_set_generic_param cmd =
        MORSE_COMMAND_INIT(cmd,
                           MORSE_CMD_ID_GET_SET_GENERIC_PARAM,
                           vif_id,
                           .param_id = param_id,
                           .action = MORSE_CMD_PARAM_ACTION_SET,
                           .value = htole32(value));

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_set_duty_cycle(uint32_t duty_cycle,
                         bool duty_cycle_omit_ctrl_resp,
                         enum mmwlan_duty_cycle_mode mode)
{
    struct morse_cmd_req_set_duty_cycle cmd =
        MORSE_COMMAND_INIT(cmd,
                           MORSE_CMD_ID_SET_DUTY_CYCLE,
                           UNKNOWN_VIF_ID,
                           .config.duty_cycle = htole32(duty_cycle),
                           .config.omit_control_responses = duty_cycle_omit_ctrl_resp ? 1 : 0,
                           .set_cfgs = MORSE_CMD_DUTY_CYCLE_SET_CFG_DUTY_CYCLE |
                                       MORSE_CMD_DUTY_CYCLE_SET_CFG_OMIT_CONTROL_RESP |
                                       MORSE_CMD_DUTY_CYCLE_SET_CFG_EXT,
                           .config_ext.mode = (uint8_t)mode);
    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);

    MM_STATIC_ASSERT((uint8_t)MMWLAN_DUTY_CYCLE_MODE_SPREAD == MORSE_CMD_DUTY_CYCLE_MODE_SPREAD,
                     "enums out of sync");
    MM_STATIC_ASSERT((uint8_t)MMWLAN_DUTY_CYCLE_MODE_BURST == MORSE_CMD_DUTY_CYCLE_MODE_BURST,
                     "enums out of sync");
    MM_STATIC_ASSERT(MORSE_CMD_DUTY_CYCLE_MODE_LAST == MORSE_CMD_DUTY_CYCLE_MODE_BURST,
                     "New modes added, update static assert tests");
}

int mmdrv_get_duty_cycle(struct mmwlan_duty_cycle_stats *stats)
{
    MMOSAL_DEV_ASSERT(stats);

    struct MM_PACKED
    {

        struct morse_cmd_header hdr;
    } cmd = MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_GET_DUTY_CYCLE, UNKNOWN_VIF_ID);
    struct morse_cmd_resp_get_duty_cycle resp = { 0 };
    int ret = morse_cmd_tx(&driver_data,
                           (struct morse_cmd_resp *)&resp,
                           (struct morse_cmd_req *)&cmd,
                           sizeof(resp),
                           0);

    if ((resp.config.omit_control_responses > true) ||
        (resp.config.duty_cycle > MMDRV_DUTY_CYCLE_MAX) ||
        (resp.config.duty_cycle < MMDRV_DUTY_CYCLE_MIN) ||
        (resp.config_ext.set.mode > MORSE_CMD_DUTY_CYCLE_MODE_LAST) ||
        ((resp.config_ext.airtime_remaining_us || resp.config_ext.burst_window_duration_us) &&
         (resp.config_ext.set.mode != MORSE_CMD_DUTY_CYCLE_MODE_BURST)))
    {

        return -1;
    }

    stats->duty_cycle = resp.config.duty_cycle;
    stats->mode = (enum mmwlan_duty_cycle_mode)resp.config_ext.set.mode;
    stats->burst_airtime_remaining_us = resp.config_ext.airtime_remaining_us;
    stats->burst_window_duration_us = resp.config_ext.burst_window_duration_us;
    return ret;
}

int mmdrv_set_txpower(int32_t *out_power_dbm, int txpower_dbm)
{
    int ret;
    struct morse_cmd_resp_set_txpower resp;
    struct morse_cmd_req_set_txpower cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_SET_TXPOWER, UNKNOWN_VIF_ID);


    if (txpower_dbm > 30)
    {
        txpower_dbm = 30;
    }

    cmd.power_qdbm = htole32(DBM_TO_QDBM(txpower_dbm));

    ret = morse_cmd_tx(&driver_data,
                       (struct morse_cmd_resp *)&resp,
                       (struct morse_cmd_req *)&cmd,
                       sizeof(resp),
                       UNKNOWN_VIF_ID);
    if (ret == 0)
    {
        *out_power_dbm = QDBM_TO_DBM(le32toh(resp.power_qdbm));
    }

    return ret;
}

int mmdrv_add_if(uint16_t *vif_id, const uint8_t *addr, enum mmdrv_interface_type type)
{
    MM_STATIC_ASSERT((int)MMDRV_INTERFACE_TYPE_STA == (int)MORSE_CMD_INTERFACE_TYPE_STA,
                     "MMDRV_INTERFACE_TYPE_STA/MORSE_CMD_INTERFACE_TYPE_STA enum mismatch");
    MM_STATIC_ASSERT((int)MMDRV_INTERFACE_TYPE_AP == (int)MORSE_CMD_INTERFACE_TYPE_AP,
                     "MMDRV_INTERFACE_TYPE_AP/MORSE_CMD_INTERFACE_TYPE_AP enum mismatch");

    if (!driver_data.started)
    {
        return -ENODEV;
    }

    enum morse_cmd_interface_type if_type = MORSE_CMD_INTERFACE_TYPE_INVALID;
    switch (type)
    {
        case MMDRV_INTERFACE_TYPE_STA:
            if_type = MORSE_CMD_INTERFACE_TYPE_STA;
            break;

        case MMDRV_INTERFACE_TYPE_AP:
            if_type = MORSE_CMD_INTERFACE_TYPE_AP;
            break;

        default:
            return -EINVAL;
    }

    int ret;
    struct morse_cmd_resp_add_interface resp;

    struct morse_cmd_req_add_interface cmd = MORSE_COMMAND_INIT(cmd,
                                                                MORSE_CMD_ID_ADD_INTERFACE,
                                                                UNKNOWN_VIF_ID,
                                                                .interface_type = htole32(if_type));

    memcpy(cmd.addr.octet, addr, sizeof(cmd.addr.octet));

    ret = morse_cmd_tx(&driver_data,
                       (struct morse_cmd_resp *)&resp,
                       (struct morse_cmd_req *)&cmd,
                       sizeof(resp),
                       0);
    if (ret == 0)
    {
        *vif_id = le16toh(resp.hdr.vif_id);
    }

    return ret;
}

int mmdrv_start_beaconing(uint16_t vif_id)
{
    return morse_beacon_start(&driver_data, vif_id);
}

int mmdrv_rm_if(uint16_t vif_id)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    if (vif_id == driver_data.beacon.vif_id)
    {
        morse_beacon_stop(&driver_data);
    }

    struct morse_cmd_req_remove_interface cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_REMOVE_INTERFACE, vif_id);

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_cfg_scan(bool enabled)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    struct morse_cmd_req_scan_config cmd = MORSE_COMMAND_INIT(cmd,
                                                              MORSE_CMD_ID_SCAN_CONFIG,
                                                              UNKNOWN_VIF_ID,
                                                              .enabled = enabled ? 1 : 0);

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_twt_agreement_install_req(const struct mmdrv_twt_data *twt_data)
{
    struct driver_data *driverd = &driver_data;

    if (!driverd->started)
    {
        return -ENODEV;
    }

    if (twt_data->agreement_len > TWT_MAX_AGREEMENT_LEN)
    {
        MMLOG_WRN("Invalid TWT agreement data length\n");
        return -1;
    }

    struct morse_cmd_req_twt_agreement_install cmd =
        MORSE_COMMAND_INIT(cmd,
                           MORSE_CMD_ID_TWT_AGREEMENT_INSTALL,
                           twt_data->interface_id,
                           .flow_id = twt_data->flow_id,
                           .agreement_len = twt_data->agreement_len);

    memcpy(cmd.agreement, twt_data->agreement, twt_data->agreement_len);

    return morse_cmd_tx(driverd, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_twt_agreement_validate_req(const struct mmdrv_twt_data *twt_data)
{
    struct driver_data *driverd = &driver_data;

    if (!driverd->started)
    {
        return -ENODEV;
    }

    if (twt_data->agreement_len > TWT_MAX_AGREEMENT_LEN)
    {
        MMLOG_WRN("Invalid TWT agreement data length\n");
        return -1;
    }

    struct morse_cmd_req_twt_agreement_install cmd =
        MORSE_COMMAND_INIT(cmd,
                           MORSE_CMD_ID_TWT_AGREEMENT_VALIDATE,
                           twt_data->interface_id,
                           .flow_id = twt_data->flow_id,
                           .agreement_len = twt_data->agreement_len);

    memcpy(cmd.agreement, twt_data->agreement, twt_data->agreement_len);

    return morse_cmd_tx(driverd, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_twt_remove_req(const struct mmdrv_twt_data *twt_data)
{
    struct driver_data *driverd = &driver_data;

    if (!driverd->started)
    {
        return -ENODEV;
    }

    struct morse_cmd_req_twt_agreement_remove cmd =
        MORSE_COMMAND_INIT(cmd,
                           MORSE_CMD_ID_TWT_AGREEMENT_REMOVE,
                           twt_data->interface_id,
                           .flow_id = twt_data->flow_id);

    return morse_cmd_tx(driverd, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_set_channel(uint32_t op_chan_freq_hz,
                      uint8_t pri_1mhz_chan_idx,
                      uint8_t op_bw_mhz,
                      uint8_t pri_bw_mhz,
                      bool is_off_channel)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }
    DRV_TRACE("set_channel %u", op_chan_freq_hz);
    int ret;
    struct morse_cmd_resp_set_channel resp;

    struct morse_cmd_req_set_channel cmd =
        MORSE_COMMAND_INIT(cmd,
                           MORSE_CMD_ID_SET_CHANNEL,
                           UNKNOWN_VIF_ID,
                           .op_chan_freq_hz = htole32(op_chan_freq_hz),
                           .op_bw_mhz = op_bw_mhz,
                           .pri_bw_mhz = pri_bw_mhz,
                           .pri_1mhz_chan_idx = pri_1mhz_chan_idx,
                           .dot11_mode = MORSE_CMD_DOT11_PROTO_MODE_AH,
                           .is_off_channel = is_off_channel);

    ret = morse_cmd_tx(&driver_data,
                       (struct morse_cmd_resp *)&resp,
                       (struct morse_cmd_req *)&cmd,
                       sizeof(resp),
                       0);
    if (ret == 0)
    {
        MMLOG_INF("%s channel change f:%lu, o:%u, p:%u, i:%u\n",
                  __func__,
                  cmd.op_chan_freq_hz,
                  cmd.op_bw_mhz,
                  cmd.pri_bw_mhz,
                  cmd.pri_1mhz_chan_idx);
    }

    return ret;
}

int mmdrv_cfg_mpsw(uint32_t airtime_min_us,
                   uint32_t airtime_max_us,
                   uint32_t packet_space_window_length_us)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    struct morse_cmd_resp_mpsw_config resp = { 0 };

    if ((airtime_max_us != 0) &&
        ((airtime_min_us > airtime_max_us) || (airtime_min_us == airtime_max_us)))
    {
        MMLOG_WRN("airtime_min (%lu) must be < airtime max (%lu), or airtime max must be 0.\n",
                  airtime_min_us,
                  airtime_max_us);
        return -EINVAL;
    }

    struct morse_cmd_req_mpsw_config cmd =
        MORSE_COMMAND_INIT(cmd,
                           MORSE_CMD_ID_MPSW_CONFIG,
                           UNKNOWN_VIF_ID,
                           .config.airtime_min_us = airtime_min_us,
                           .config.airtime_max_us = airtime_max_us,
                           .config.packet_space_window_length_us = packet_space_window_length_us);

    if ((airtime_min_us > 0) || (airtime_max_us > 0))
    {
        cmd.set_cfgs |= MORSE_CMD_SET_MPSW_CFG_PKT_SPC_WIN_LEN;
    }
    if (packet_space_window_length_us > 0)
    {
        cmd.set_cfgs |= MORSE_CMD_SET_MPSW_CFG_AIRTIME_BOUNDS;
    }

    if ((cmd.set_cfgs & MORSE_CMD_SET_MPSW_CFG_PKT_SPC_WIN_LEN) ||
        (cmd.set_cfgs & MORSE_CMD_SET_MPSW_CFG_AIRTIME_BOUNDS))
    {
        cmd.set_cfgs |= MORSE_CMD_SET_MPSW_CFG_ENABLED;
        cmd.config.enable = 1;
    }

    return morse_cmd_tx(&driver_data,
                        (struct morse_cmd_resp *)&resp,
                        (struct morse_cmd_req *)&cmd,
                        sizeof(resp),
                        0);
}

int mmdrv_update_beacon_vendor_ie_filter(uint16_t vif_id, const uint8_t *ouis, uint8_t n_ouis)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    if (n_ouis > MMWLAN_BEACON_VENDOR_IE_MAX_OUI_FILTERS)
    {
        return -ENOSPC;
    }

    struct morse_cmd_req_update_oui_filter cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_UPDATE_OUI_FILTER, vif_id, .n_ouis = n_ouis);

    if (ouis != NULL)
    {
        memcpy(cmd.ouis, ouis, (cmd.n_ouis * MMWLAN_OUI_SIZE));
    }

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_cfg_bss(uint16_t vif_id, uint16_t beacon_int, uint16_t dtim_period, uint32_t cssid)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    struct morse_cmd_req_bss_config cmd =
        MORSE_COMMAND_INIT(cmd,
                           MORSE_CMD_ID_BSS_CONFIG,
                           vif_id,
                           .beacon_interval_tu = htole16(beacon_int),
                           .cssid = htole32(cssid),
                           .dtim_period = htole16(dtim_period));

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_update_sta_state(uint16_t vif_id,
                           uint16_t aid,
                           const uint8_t *addr,
                           enum morse_sta_state state)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    DRV_TRACE("set_sta_state %u %02x:%02x:%02x:%02x:%02x:%02x",
              state,
              addr[0],
              addr[1],
              addr[2],
              addr[3],
              addr[4],
              addr[5]);

    struct morse_cmd_resp_set_sta_state resp;
    struct morse_cmd_req_set_sta_state cmd = MORSE_COMMAND_INIT(cmd,
                                                                MORSE_CMD_ID_SET_STA_STATE,
                                                                vif_id,
                                                                .aid = htole16(aid),
                                                                .state = htole16(state));

    memcpy(cmd.sta_addr, addr, sizeof(cmd.sta_addr));

    return morse_cmd_tx(&driver_data,
                        (struct morse_cmd_resp *)&resp,
                        (struct morse_cmd_req *)&cmd,
                        sizeof(resp),
                        0);
}

int mmdrv_install_key(uint16_t vif_id, uint16_t aid, struct mmdrv_key_conf *key_conf)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    DRV_TRACE("install key %u %u %u %u",
              key_conf->key_idx,
              key_conf->is_pairwise,
              aid,
              key_conf->length);

    uint16_t requested_key_idx = key_conf->key_idx;
    struct morse_cmd_resp_install_key resp;

    MMLOG_DBG("%s Installing key for vif (%d):\n"
              "\tkey->idx: %d\n"
              "\tkey->cipher: 0x%08x\n"
              "\tkey->pn: 0x" MM_X64_FMT "\n"
              "\tkey->len: %d\n"
              "\tkey->is_pairwise: %d\n"
              "\taid (optional): %d\n",
              __func__,
              vif_id,
              key_conf->key_idx,
              MORSE_CMD_KEY_CIPHER_AES_CCM,
              MM_X64_VAL(key_conf->tx_pn),
              key_conf->length,
              key_conf->is_pairwise,
              aid);

    struct morse_cmd_req_install_key cmd =
        MORSE_COMMAND_INIT(cmd,
                           MORSE_CMD_ID_INSTALL_KEY,
                           vif_id,
                           .pn = htole64(key_conf->tx_pn),
                           .aid = htole32(aid),
                           .cipher = MORSE_CMD_KEY_CIPHER_AES_CCM);

    switch (key_conf->length)
    {
        case 16:
            cmd.key_length = MORSE_CMD_AES_KEY_LEN_LENGTH_128;
            break;

        case 32:
            cmd.key_length = MORSE_CMD_AES_KEY_LEN_LENGTH_256;
            break;

        default:
            return -EINVAL;
    }
    cmd.key_type = key_conf->is_pairwise ? MORSE_CMD_TEMPORAL_KEY_TYPE_PTK :
                                           MORSE_CMD_TEMPORAL_KEY_TYPE_GTK;

    cmd.key_idx = key_conf->key_idx;
    memcpy(&cmd.key[0], &key_conf->key[0], sizeof(cmd.key));

    int result = morse_cmd_tx(&driver_data,
                              (struct morse_cmd_resp *)&resp,
                              (struct morse_cmd_req *)&cmd,
                              sizeof(resp),
                              0);
    if (result)
    {
        MMLOG_WRN("mmdrv_add_key - morse_cmd_install_key failed %d\n", result);
        return result;
    }

    key_conf->key_idx = resp.key_idx;
    MMLOG_DBG("%s Installed key @ hw index: %d\n", __func__, resp.key_idx);


    MMOSAL_ASSERT(requested_key_idx == key_conf->key_idx);

    return result;
}

int mmdrv_disable_key(uint16_t vif_id, uint16_t aid, uint8_t hw_key_idx, bool is_pairwise)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    if (aid)
    {
        DRV_TRACE("disable key %u %u %u", hw_key_idx, is_pairwise, aid);

        MMLOG_DBG("%s Disabling key for vif (%d):\n"
                  "\tkey->hw_key_idx: %d\n"
                  "\taid (optional): %d\n",
                  __func__,
                  vif_id,
                  hw_key_idx,
                  aid);

        struct morse_cmd_req_disable_key cmd =
            MORSE_COMMAND_INIT(cmd,
                               MORSE_CMD_ID_DISABLE_KEY,
                               vif_id,
                               .aid = htole16(aid),
                               .key_idx = hw_key_idx,
                               .key_type = is_pairwise ? MORSE_CMD_TEMPORAL_KEY_TYPE_PTK :
                                                         MORSE_CMD_TEMPORAL_KEY_TYPE_GTK);

        return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
    }
    return 0;
}

struct mmpkt *mmdrv_alloc_mmpkt_for_tx(uint8_t pkt_class,
                                       uint32_t space_at_start,
                                       uint32_t space_at_end)
{
    struct morse_buff_skb_header *hdr;

    if (!driver_data.started)
    {
        return NULL;
    }

    return mmhal_wlan_alloc_mmpkt_for_tx(
        pkt_class,
        FAST_ROUND_UP(space_at_start + sizeof(*hdr), MORSE_PKT_WORD_ALIGN) + MORSE_YAPS_DELIM_SIZE,
        FAST_ROUND_UP(space_at_end, MORSE_PKT_WORD_ALIGN),
        sizeof(struct mmdrv_tx_metadata));
}

struct mmpkt *mmdrv_alloc_mmpkt_for_defrag(uint32_t min_capacity, uint32_t max_capacity)
{

    struct mmpkt *mmpkt = mmhal_wlan_alloc_mmpkt_for_rx(MMHAL_WLAN_PKT_DATA_TID0, max_capacity, 0);
    if (mmpkt != NULL)
    {
        return mmpkt;
    }


    mmpkt = mmhal_wlan_alloc_mmpkt_for_rx(MMHAL_WLAN_PKT_DATA_TID0, UINT32_MAX, 0);
    if (mmpkt != NULL)
    {
        struct mmpktview *pktview = mmpkt_open(mmpkt);
        uint32_t capacity = mmpkt_available_space_at_end(pktview);
        mmpkt_close(&pktview);
        if (capacity < min_capacity)
        {
            mmpkt_release(mmpkt);
            mmpkt = NULL;
        }
        return mmpkt;
    }


    return mmhal_wlan_alloc_mmpkt_for_rx(MMHAL_WLAN_PKT_DATA_TID0, min_capacity, 0);
}

int mmdrv_set_frag_threshold(uint32_t frag_threshold)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    if (frag_threshold == 0)
    {
        frag_threshold = UINT32_MAX;
    }

    return mmdrv_set_param(UNKNOWN_VIF_ID, MORSE_PARAM_ID_FRAGMENT_THRESHOLD, frag_threshold);
}

int mmdrv_set_dynamic_ps_timeout(uint32_t timeout_ms)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    return morse_ps_set_dynamic_ps_timeout(&driver_data, timeout_ms);
}

int mmdrv_tx_frame(struct mmpkt *mmpkt, bool is_mgmt)
{
    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(mmpkt);

    tx_metadata->status_flags = MMDRV_TX_STATUS_FLAG_NO_ACK;
    tx_metadata->attempts = 0;

    if (!driver_data.started)
    {
        mmdrv_host_process_tx_status(mmpkt);
        return -ENODEV;
    }

    uint16_t aci = dot11_tid_to_ac(tx_metadata->tid);
    struct driver_data *driverd = &(driver_data);
    struct morse_skbq *mq;
    enum morse_skb_channel channel;

    if (is_mgmt)
    {
        mq = driverd->cfg->ops->skbq_mgmt_tc_q(driverd);
        channel = MORSE_SKB_CHAN_MGMT;
    }
    else if (tx_metadata->flags & MMDRV_TX_FLAG_NO_ACK)
    {
        mq = driverd->cfg->ops->skbq_tc_q_from_aci(driverd, aci);
        channel = MORSE_SKB_CHAN_DATA_NOACK;
    }
    else
    {
        mq = driverd->cfg->ops->skbq_tc_q_from_aci(driverd, aci);
        channel = MORSE_SKB_CHAN_DATA;
    }

    DRV_TRACE("tx %x", mmpkt);

    return morse_skbq_mmpkt_tx(mq, mmpkt, channel);
}

int mmdrv_cfg_qos_queue(const struct mmwlan_qos_queue_params *params)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    struct morse_cmd_req_set_qos_params qparams = {

        .uapsd = 0,
        .queue_idx = params->aci,
        .aifs_slot_count = params->aifs,
        .contention_window_min = params->cw_min,
        .contention_window_max = params->cw_max,
        .max_txop_usec = params->txop_max_us,
    };

    DRV_TRACE("cfg_qos %x", params->aci);

    struct morse_cmd_req_set_qos_params cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_SET_QOS_PARAMS, UNKNOWN_VIF_ID);

    cmd.uapsd = qparams.uapsd;
    cmd.queue_idx = qparams.queue_idx;
    cmd.aifs_slot_count = qparams.aifs_slot_count;
    cmd.contention_window_min = htole16(qparams.contention_window_min);
    cmd.contention_window_max = htole16(qparams.contention_window_max);
    cmd.max_txop_usec = htole32(qparams.max_txop_usec);

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_set_wake_enabled(bool enabled)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    if (enabled)
    {
        return morse_ps_disable_async(&driver_data, PS_WAKER_UMAC);
    }
    else
    {
        return morse_ps_enable_async(&driver_data, PS_WAKER_UMAC);
    }
}

int mmdrv_set_chip_power_save_enabled(uint16_t vif_id, bool enabled)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    MMLOG_DBG("Chip Power Mode set to: %d\n", enabled);


    struct morse_cmd_req_config_ps cmd = MORSE_COMMAND_INIT(cmd,
                                                            MORSE_CMD_ID_CONFIG_PS,
                                                            vif_id,
                                                            .enabled = (uint8_t)enabled,
                                                            .dynamic_ps_offload = (uint8_t)enabled);

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, MM_CMD_TIMEOUT_PS);
}

int mmdrv_set_chip_wnm_sleep_enabled(uint16_t vif_id, bool enabled)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    MMLOG_DBG("Chip WNM sleep set to: %d\n", enabled);

    struct morse_cmd_req_set_long_sleep_config cmd =
        MORSE_COMMAND_INIT(cmd,
                           MORSE_CMD_ID_SET_LONG_SLEEP_CONFIG,
                           vif_id,
                           .enabled = (uint8_t)enabled);

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_get_timestamp(uint32_t *pdw_low, uint32_t *pdw_high)
{
    MM_UNUSED(pdw_low);
    MM_UNUSED(pdw_high);

    int result = -1;
    MMLOG_DBG("\n");
    return result;
}


int mmdrv_rssi_to_signal_strength(uint8_t rssi)
{
    MMLOG_DBG("\n");

    int8_t signed_rssi = (int8_t)rssi;

    if (signed_rssi >= -50)
    {
        return 100;
    }
    else if (signed_rssi >= -80)
    {
        return (24 + ((signed_rssi + 80) * 26) / 10);
    }
    else if (signed_rssi >= -90)
    {
        return ((signed_rssi + 90) * 26) / 10;
    }
    else
    {
        return 0;
    }
}

int mmdrv_set_tx_continuous_mode(bool enable)
{
    MM_UNUSED(enable);

    MMLOG_DBG("\n");
    return 0;
}


#define MMDRV_PERIODIC_HEALTH_THRESHOLD_MS            \
    (MMWLAN_DEFAULT_MIN_HEALTH_CHECK_INTERVAL_MS +    \
     ((MMWLAN_DEFAULT_MAX_HEALTH_CHECK_INTERVAL_MS -  \
       MMWLAN_DEFAULT_MIN_HEALTH_CHECK_INTERVAL_MS) / \
      2))


static uint32_t calc_health_check_interval(uint32_t min_interval_ms, uint32_t max_interval_ms)
{
    MMOSAL_DEV_ASSERT(min_interval_ms <= max_interval_ms);

    if (min_interval_ms > MMDRV_PERIODIC_HEALTH_THRESHOLD_MS)
    {
        return min_interval_ms;
    }
    else if (max_interval_ms > MMDRV_PERIODIC_HEALTH_THRESHOLD_MS)
    {
        return MMDRV_PERIODIC_HEALTH_THRESHOLD_MS;
    }
    else
    {
        return max_interval_ms;
    }
}

int mmdrv_set_health_check_interval(uint32_t min_interval_ms, uint32_t max_interval_ms)
{
    if (min_interval_ms > max_interval_ms)
    {
        return -MM_EINVAL;
    }

    if (!driver_data.started)
    {
        return -MM_ENODEV;
    }

    driver_data.health_check.interval_ms =
        calc_health_check_interval(min_interval_ms, max_interval_ms);

    driver_health_request_check(&driver_data);
    return 0;
}

void mmdrv_set_health_check_veto(enum mmdrv_health_check_veto_id veto_id)
{
    MMOSAL_ASSERT(veto_id < 32);
    atomic_fetch_or(&driver_data.health_check.periodic_check_vetoes, 1ul << veto_id);
    driver_health_request_check(&driver_data);
}

void mmdrv_unset_health_check_veto(enum mmdrv_health_check_veto_id veto_id)
{
    MMOSAL_ASSERT(veto_id < 32);
    atomic_fetch_and(&driver_data.health_check.periodic_check_vetoes, ~(1ul << veto_id));
    driver_health_request_check(&driver_data);
}

void mmdrv_hw_restart_completed(void)
{
    mmdrv_host_set_tx_paused(MMDRV_PAUSE_SOURCE_MASK_HW_RESTART, false);
}

int mmdrv_get_stats(uint32_t core_num, uint8_t **buf, uint32_t *len)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    uint16_t cmd_id;

    struct morse_cmd_resp *resp = (struct morse_cmd_resp *)*buf;

    switch (core_num)
    {
        case 0:
            cmd_id = MORSE_CMD_ID_HOST_STATS_LOG;
            break;

        case 1:
            cmd_id = MORSE_CMD_ID_MAC_STATS_LOG;
            break;

        case 2:
            cmd_id = MORSE_CMD_ID_UPHY_STATS_LOG;
            break;

        default:
            return -1;
    }

    struct morse_cmd_req cmd = MORSE_COMMAND_INIT(cmd, cmd_id, UNKNOWN_VIF_ID);

    int ret = morse_cmd_tx(&driver_data, resp, &cmd, *len, 0);
    if (ret == 0)
    {
        *buf = resp->data;
        *len = le16toh(resp->hdr.len);
    }

    return ret;
}

int mmdrv_reset_stats(uint32_t core_num)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    uint16_t cmd_id;

    switch (core_num)
    {
        case 0:
            cmd_id = MORSE_CMD_ID_HOST_STATS_RESET;
            break;

        case 1:
            cmd_id = MORSE_CMD_ID_MAC_STATS_RESET;
            break;

        case 2:
            cmd_id = MORSE_CMD_ID_UPHY_STATS_RESET;
            break;

        default:
            return -1;
    }

    struct morse_cmd_req cmd = MORSE_COMMAND_INIT(cmd, cmd_id, UNKNOWN_VIF_ID);

    return morse_cmd_tx(&driver_data, NULL, &cmd, 0, 0);
}

int mmdrv_enable_arp_response_offload(uint16_t vif_id, uint32_t arp_addr)
{
    struct morse_cmd_req_arp_offload cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_ARP_OFFLOAD, vif_id);

    cmd.ip_table[0] = arp_addr;

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_enable_arp_refresh_offload(uint16_t vif_id,
                                     uint32_t interval_s,
                                     uint32_t dest_ip,
                                     bool send_as_garp)
{
    struct morse_cmd_req_arp_periodic_refresh cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_ARP_PERIODIC_REFRESH, vif_id);
    cmd.config.refresh_period_s = interval_s;
    cmd.config.destination_ip = dest_ip;
    cmd.config.send_as_garp = send_as_garp ? 1 : 0;

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_enable_dhcp_offload(uint16_t vif_id)
{
    struct morse_cmd_req_dhcp_offload cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_DHCP_OFFLOAD, vif_id);
    cmd.opcode = MORSE_CMD_DHCP_OPCODE_ENABLE;

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_do_dhcp_discovery(uint16_t vif_id)
{
    struct morse_cmd_req_dhcp_offload cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_DHCP_OFFLOAD, vif_id);
    cmd.opcode = MORSE_CMD_DHCP_OPCODE_DO_DISCOVERY;

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_set_tcp_keepalive_offload(uint16_t vif_id,
                                    const struct mmwlan_tcp_keepalive_offload_args *args)
{
    struct morse_cmd_req_set_tcp_keepalive cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_SET_TCP_KEEPALIVE, vif_id);
    if (args != NULL)
    {
        cmd.enabled = 1;
        cmd.retry_count = args->retry_count;
        cmd.retry_interval_s = args->retry_interval_s;
        cmd.set_cfgs = args->set_cfgs;
        cmd.src_ip = args->src_ip;
        cmd.dest_ip = args->dest_ip;
        cmd.src_port = args->src_port;
        cmd.dest_port = args->dest_port;
        cmd.period_s = args->period_s;
    }

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_standby_enter(uint16_t vif_id, const uint8_t monitor_bssid[MMWLAN_MAC_ADDR_LEN])
{
    struct morse_cmd_req_standby_mode cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_STANDBY_MODE, vif_id);
    cmd.cmd = MORSE_CMD_STANDBY_MODE_ENTER;
    mac_addr_copy(cmd.enter.monitor_bssid.octet, monitor_bssid);

    int ret = morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
    if (ret == 0)
    {
        driver_data.standby_waiting_for_wakeup = true;
    }

    return ret;
}

int mmdrv_standby_exit(uint16_t vif_id, uint8_t *reason)
{
    struct morse_cmd_resp_standby_mode rsp;
    struct morse_cmd_req_standby_mode cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_STANDBY_MODE, vif_id);
    cmd.cmd = MORSE_CMD_STANDBY_MODE_EXIT;

    int ret = morse_cmd_tx(&driver_data,
                           (struct morse_cmd_resp *)&rsp,
                           (struct morse_cmd_req *)&cmd,
                           sizeof(rsp),
                           0);

    if (ret == 0)
    {
        *reason = rsp.info.reason;
    }

    return ret;
}

int mmdrv_standby_set_status_payload(uint16_t vif_id, const uint8_t *payload, uint32_t payload_len)
{
    if (payload_len > MORSE_CMD_STANDBY_STATUS_FRAME_USER_PAYLOAD_MAX_LEN)
    {
        return -EINVAL;
    }

    struct morse_cmd_req_standby_mode cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_STANDBY_MODE, vif_id);
    cmd.cmd = MORSE_CMD_STANDBY_MODE_SET_STATUS_PAYLOAD;
    memcpy(cmd.set_payload.payload, payload, payload_len);
    cmd.set_payload.len = payload_len;

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_standby_set_wake_filter(uint16_t vif_id,
                                  const uint8_t *filter,
                                  uint32_t filter_len,
                                  uint32_t offset)
{
    if (filter_len > MMWLAN_STANDBY_WAKE_FRAME_USER_FILTER_MAXLEN)
    {
        return -EINVAL;
    }

    struct morse_cmd_req_standby_mode cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_STANDBY_MODE, vif_id);
    cmd.cmd = MORSE_CMD_STANDBY_MODE_SET_WAKE_FILTER;
    memcpy(cmd.set_filter.filter, filter, filter_len);
    cmd.set_filter.len = filter_len;
    cmd.set_filter.offset = offset;

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_standby_set_config(uint16_t vif_id, const struct mmwlan_standby_config *config)
{
    struct morse_cmd_standby_set_config drv_config = { 0 };

    drv_config.deep_sleep_increment_s = config->snooze_increment_s;
    drv_config.deep_sleep_max_s = config->snooze_max_s;
    drv_config.deep_sleep_period_s = config->snooze_period_s;
    drv_config.dst_ip = config->dst_ip;
    drv_config.dst_port = config->dst_port;
    drv_config.notify_period_s = config->notify_period_s;
    drv_config.src_ip = config->src_ip;

    struct morse_cmd_req_standby_mode cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_STANDBY_MODE, vif_id);
    cmd.cmd = MORSE_CMD_STANDBY_MODE_SET_CONFIG_V2;
    memcpy(&cmd.config, &drv_config, sizeof(drv_config));

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_set_whitelist_filter(uint16_t vif_id, const struct mmwlan_config_whitelist *whitelist)
{
    struct morse_cmd_req_set_whitelist cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_SET_WHITELIST, vif_id);
    cmd.flags = whitelist->flags;
    cmd.ip_protocol = htobe16(whitelist->ip_protocol);
    cmd.llc_protocol = htobe16(whitelist->llc_protocol);
    cmd.src_ip = whitelist->src_ip;
    cmd.dest_ip = whitelist->dest_ip;
    cmd.netmask = whitelist->netmask;
    cmd.src_port = htobe16(whitelist->src_port);
    cmd.dest_port = htobe16(whitelist->dest_port);

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_get_capabilities(uint16_t vif_id, struct morse_caps *caps)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    struct morse_cmd_resp_get_capabilities rsp;
    struct morse_cmd_req_get_capabilities cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_GET_CAPABILITIES, vif_id);

    int ret = morse_cmd_tx(&driver_data,
                           (struct morse_cmd_resp *)&rsp,
                           (struct morse_cmd_req *)&cmd,
                           sizeof(rsp),
                           0);
    if (ret != 0)
    {
        return ret;
    }

    caps->ampdu_mss = rsp.capabilities.ampdu_mss;
    caps->morse_mmss_offset = rsp.morse_mmss_offset;
    caps->beamformee_sts_capability = rsp.capabilities.beamformee_sts_capability;
    caps->maximum_ampdu_length_exponent = rsp.capabilities.maximum_ampdu_length_exponent;
    caps->number_sounding_dimensions = rsp.capabilities.number_sounding_dimensions;
    for (int i = 0; i < MORSE_CMD_S1G_CAPABILITY_FLAGS_WIDTH; i++)
    {
        caps->flags[i] = le32toh(rsp.capabilities.flags[i]);
    }

    return ret;
}

int mmdrv_trigger_core_assert(uint32_t core_id)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }
    struct morse_cmd_req_force_assert cmd =
        MORSE_COMMAND_INIT(cmd, MORSE_CMD_ID_FORCE_ASSERT, UNKNOWN_VIF_ID, .hart_id = core_id);

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_set_ndp_probe(uint16_t vif_id, bool enabled)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    struct morse_cmd_req_set_ndp_probe_support cmd =
        MORSE_COMMAND_INIT(cmd,
                           MORSE_CMD_ID_SET_NDP_PROBE_SUPPORT,
                           vif_id,
                           .enabled = enabled ? true : false,

                           .requested_response_is_pv1 = 0,

                           .tx_bw_mhz = -1);

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}

int mmdrv_execute_command(uint8_t *command, uint8_t *response, uint32_t *response_len)
{
    struct morse_cmd_resp *resp = (struct morse_cmd_resp *)response;
    struct morse_cmd_req *cmd = (struct morse_cmd_req *)command;

    uint32_t resp_len = (response_len != NULL) ? *response_len : 0;

    if (!driver_data.started)
    {
        return -ENODEV;
    }
    int ret = morse_cmd_tx(&driver_data, resp, cmd, resp_len, 0);
    if (ret != 0)
    {
        if (resp != NULL && response_len != NULL)
        {
            resp->hdr.message_id = cmd->hdr.message_id;
            resp->hdr.host_id = cmd->hdr.host_id;
            resp->hdr.vif_id = cmd->hdr.vif_id;
            resp->status = htole32(resp->status);
            *response_len = sizeof(*resp);
        }

        return ret;
    }

    if (resp != NULL && response_len != NULL)
    {
        *response_len = le16toh(resp->hdr.len) + sizeof(resp->hdr);
    }
    return 0;
}

int mmdrv_set_seq_num_spaces(uint16_t vif_id,
                             const uint16_t *tx_seq_num_spaces,
                             const uint8_t *addr)
{
    int ret;


    struct morse_cmd_req_sequence_number_spaces req =
        MORSE_COMMAND_INIT(req,
                           MORSE_CMD_ID_SEQUENCE_NUMBER_SPACES,
                           vif_id,
                           .flags = MORSE_CMD_SNS_FLAG_SET |
                                    MORSE_CMD_SNS_FLAG_BASELINE |
                                    MORSE_CMD_SNS_FLAG_INDIV_ADDR_QOS_DATA,
                           .spaces.baseline = tx_seq_num_spaces[MMDRV_SEQ_NUM_BASELINE],
                           .spaces.qos_null = tx_seq_num_spaces[MMDRV_SEQ_NUM_QOS_NULL]);

    MM_STATIC_ASSERT(MMWLAN_MAX_QOS_TID <= MORSE_CMD_SNS_MAX_TIDS,
                     "Driver sequence number spaces exceed the amount supported by the chip\n");
    memcpy(req.spaces.individually_addr_qos_data, tx_seq_num_spaces, MMWLAN_MAX_QOS_TID);
    mac_addr_copy(req.addr, addr);

    ret = morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&req, 0, MM_CMD_TIMEOUT_DEFAULT);

    return ret;
}

int mmdrv_set_listen_interval_sleep(uint16_t vif, uint16_t listen_interval)
{
    int ret;

    struct morse_cmd_req_li_sleep req =
        MORSE_COMMAND_INIT(req, MORSE_CMD_ID_LI_SLEEP, vif, .listen_interval = listen_interval);

    ret = morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&req, 0, MM_CMD_TIMEOUT_DEFAULT);

    return ret;
}

MM_STATIC_ASSERT(MMDRV_DIRECTION_OUTGOING == 0 && MMDRV_DIRECTION_INCOMING == 1,
                 "Traffic flow direction enum must match firmware expectation");

int mmdrv_set_control_response_bw(uint16_t vif_id, enum mmdrv_direction direction, bool cr_1mhz_en)
{
    if (!driver_data.started)
    {
        return -ENODEV;
    }

    struct morse_cmd_req_set_control_response cmd =
        MORSE_COMMAND_INIT(cmd,
                           MORSE_CMD_ID_SET_CONTROL_RESPONSE,
                           vif_id,
                           .control_response_1mhz_en = cr_1mhz_en,
                           .direction = direction);

    return morse_cmd_tx(&driver_data, NULL, (struct morse_cmd_req *)&cmd, 0, 0);
}
