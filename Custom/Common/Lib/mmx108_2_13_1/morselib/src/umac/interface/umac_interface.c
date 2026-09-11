/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "common/mac_address.h"
#include "mmlog.h"
#include "common/morse_commands.h"
#include "common/morse_command_utils.h"
#include "dot11/dot11_utils.h"

#include "mmdrv.h"
#include "umac_interface.h"
#include "umac_interface_data.h"
#include "umac/ps/umac_ps.h"
#include "umac/config/umac_config.h"
#include "umac/rc/umac_rc.h"
#include "umac/regdb/umac_regdb.h"
#include "umac/twt/umac_twt.h"
#include "umac/core/umac_core.h"
#include "umac/supplicant_shim/umac_supp_shim.h"
#include "umac/health_check/umac_health_check.h"
#include "mmhal_wlan.h"


#define MM_TX_STATUS_BUFFER_FLUSH_WATERMARK (10)


#define DEFAULT_MORSE_IBSS_ACK_TIMEOUT_ADJUST_US (1000)

static inline const char *umac_interface_type_to_str(enum umac_interface_type type)
{
    switch (type)
    {
        case UMAC_INTERFACE_NONE:
            return "None";

        case UMAC_INTERFACE_SCAN:
            return "Scan";

        case UMAC_INTERFACE_STA:
            return "STA";

        case UMAC_INTERFACE_AP:
            return "AP";

        default:
            return "??";
    }
}

void umac_interface_init(struct umac_data *umacd)
{
    struct umac_interface_vif_data *data;
    data = umac_data_get_interface_vif(umacd, MMWLAN_VIF_STA);
    data->vif_id = MMDRV_VIF_ID_INVALID;
    data = umac_data_get_interface_vif(umacd, MMWLAN_VIF_AP);
    data->vif_id = MMDRV_VIF_ID_INVALID;
}

static void umac_interface_populate_device_mac_addr(struct umac_interface_data *data,
                                                    struct mmdrv_chip_info *chip_info)
{
    MMOSAL_DEV_ASSERT(mm_mac_addr_is_zero(data->mac_addr));

    if (!mm_mac_addr_is_zero(chip_info->mac_addr))
    {
        mac_addr_copy(data->mac_addr, chip_info->mac_addr);
        MMLOG_INF("Using MAC addr from %s\n", "chip");
    }


    mmhal_read_mac_addr(data->mac_addr);
    if (!mm_mac_addr_is_zero(data->mac_addr))
    {
        MMLOG_INF("Using MAC addr from %s\n", "HAL/prev");
        return;
    }

    MMLOG_INF("Using MAC addr from %s\n", "rng");
    data->mac_addr[0] = 0x02;
    data->mac_addr[1] = 0x01;
    uint32_t rnd = mmhal_random_u32(0, UINT32_MAX);
    data->mac_addr[2] = rnd >> 24;
    data->mac_addr[3] = rnd >> 16;
    data->mac_addr[4] = rnd >> 8;
    data->mac_addr[5] = rnd >> 0;
}


static void umac_interface_configure_control_response_out_1mhz(
    struct umac_data *umacd,
    struct umac_interface_vif_data *vif_data)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);
    bool enabled = umac_config_is_ctrl_resp_out_1mhz_enabled(umacd) &&
                   MORSE_CAP_SUPPORTED(&data->capabilities, 1MHZ_CONTROL_RESPONSE_PREAMBLE);
    mmdrv_set_control_response_bw(vif_data->vif_id, MMDRV_DIRECTION_OUTGOING, enabled);
}

bool umac_interface_get_control_response_bw_1mhz_out_enabled(struct umac_data *umacd)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);
    return umac_config_is_ctrl_resp_out_1mhz_enabled(umacd) &&
           MORSE_CAP_SUPPORTED(&data->capabilities, 1MHZ_CONTROL_RESPONSE_PREAMBLE);
}


#define VIF_STA_INTERFACE_TYPES_MASK \
    (UMAC_INTERFACE_SCAN | UMAC_INTERFACE_STA | UMAC_INTERFACE_NONE)

static void umac_interface_init_vif(struct umac_data *umacd,
                                    struct umac_interface_vif_data *vif_data)
{

    umac_ps_update_mode(umacd);


    mmdrv_set_param(vif_data->vif_id,
                    MORSE_PARAM_ID_TX_STATUS_FLUSH_WATERMARK,
                    MM_TX_STATUS_BUFFER_FLUSH_WATERMARK);

    umac_health_check_start(umacd);
    mmdrv_set_dynamic_ps_timeout(umac_config_get_dynamic_ps_timeout(umacd));

    if (vif_data->active_interface_types & UMAC_INTERFACE_SCAN)
    {
        mmdrv_set_ndp_probe(vif_data->vif_id, umac_config_is_ndp_probe_supported(umacd));
    }
    if (vif_data->active_interface_types & UMAC_INTERFACE_STA)
    {
        mmdrv_set_listen_interval_sleep(vif_data->vif_id, umac_config_get_listen_interval(umacd));

        uint8_t beacon_loss_count = umac_config_get_beacon_loss_count(umacd);
        if (beacon_loss_count != UINT8_MAX)
        {
            mmdrv_set_param(vif_data->vif_id, MORSE_PARAM_ID_BEACON_LOSS_COUNT, beacon_loss_count);
        }
        umac_twt_init_vif(umacd, &vif_data->vif_id);
        umac_interface_configure_control_response_out_1mhz(umacd, vif_data);
    }
}

enum mmwlan_status umac_interface_add(struct umac_data *umacd,
                                      enum umac_interface_type type,
                                      const struct umac_datapath_ops *datapath_ops,
                                      const uint8_t *mac_addr)
{
    enum mmwlan_status status = MMWLAN_ERROR;
    struct umac_interface_data *data = umac_data_get_interface(umacd);
    struct umac_interface_vif_data *data_sta = umac_data_get_interface_vif(umacd, MMWLAN_VIF_STA);
    struct umac_interface_vif_data *data_ap = umac_data_get_interface_vif(umacd, MMWLAN_VIF_AP);
    struct umac_interface_vif_data *vif_data = NULL;
    enum mmdrv_interface_type drv_if_type = MMDRV_INTERFACE_TYPE_STA;
    MMOSAL_DEV_ASSERT(data_sta != NULL && data_ap != NULL);


    if (data_ap->active_interface_types == 0 && data_sta->active_interface_types == 0)
    {
        MMLOG_DBG("Booting device\n");

        const char *country_code = umac_regdb_get_country_code(umacd);
        if (strncmp(country_code, "??", 2) == 0)
        {
            MMLOG_ERR("Channel list not set\n");
            status = MMWLAN_CHANNEL_LIST_NOT_SET;
            goto error;
        }

        struct mmdrv_chip_info chip_info = { 0 };
        status = mmdrv_init(&chip_info, country_code);
        if (status != MMWLAN_SUCCESS)
        {
            MMLOG_WRN("Driver init failed (%u)\n", status);
            goto error;
        }

        data->fw_version.major = chip_info.fw_version.major;
        data->fw_version.minor = chip_info.fw_version.minor;
        data->fw_version.patch = chip_info.fw_version.patch;
        data->morse_chip_id = chip_info.morse_chip_id;
        data->morse_chip_id_string = chip_info.morse_chip_id_string;


        if (mm_mac_addr_is_zero(data->mac_addr))
        {
            umac_interface_populate_device_mac_addr(data, &chip_info);
            MMLOG_DBG("Device MAC address: " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(data->mac_addr));
        }

        status = mmdrv_get_capabilities(MMDRV_VIF_ID_INVALID, &data->capabilities);
        MMOSAL_ASSERT(status == MMWLAN_SUCCESS);
    }

    if (type & VIF_STA_INTERFACE_TYPES_MASK)
    {
        vif_data = data_sta;
        drv_if_type = MMDRV_INTERFACE_TYPE_STA;

        if (vif_data->active_interface_types & type)
        {
            MMLOG_DBG("STA interface of type %s already exists (existing 0x%x)\n",
                      umac_interface_type_to_str(type),
                      vif_data->active_interface_types);
        }
    }
    else if (type == UMAC_INTERFACE_AP)
    {
        vif_data = data_ap;
        drv_if_type = MMDRV_INTERFACE_TYPE_AP;

        if (vif_data->vif_id != MMDRV_VIF_ID_INVALID)
        {
            MMLOG_WRN("AP VIF already exists (ID=%u)\n", vif_data->vif_id);
            status = MMWLAN_UNAVAILABLE;
            goto error;
        }
    }
    else
    {
        MMOSAL_DEV_ASSERT(false);
        status = MMWLAN_INVALID_ARGUMENT;
        goto error;
    }

    MMOSAL_DEV_ASSERT(vif_data != NULL);

    if (datapath_ops)
    {
        MMOSAL_DEV_ASSERT(vif_data->datapath_ops == datapath_ops || vif_data->datapath_ops == NULL);
        vif_data->datapath_ops = datapath_ops;
    }

    if (vif_data->vif_id == MMDRV_VIF_ID_INVALID)
    {
        if (mac_addr == NULL)
        {
            mac_addr = data->mac_addr;
        }
        mac_addr_copy(vif_data->mac_addr, mac_addr);

        status = mmdrv_add_if(&vif_data->vif_id, vif_data->mac_addr, drv_if_type);
        MMOSAL_ASSERT(status == MMWLAN_SUCCESS);
    }
    else
    {

        if (mac_addr != NULL && !mm_mac_addr_is_equal(vif_data->mac_addr, mac_addr))
        {
            MMLOG_WRN("Trying to change MAC address of STA interface from " MM_MAC_ADDR_FMT
                      " to " MM_MAC_ADDR_FMT "\n",
                      MM_MAC_ADDR_VAL(vif_data->mac_addr),
                      MM_MAC_ADDR_VAL(mac_addr));
            MMOSAL_DEV_ASSERT(false);
        }
    }

    vif_data->active_interface_types |= type;


    umac_interface_init_vif(umacd, vif_data);

    MMLOG_INF("%s interface added (active=%x), vif_id=%u, mac_addr=" MM_MAC_ADDR_FMT "\n",
              umac_interface_type_to_str(type),
              vif_data->active_interface_types,
              vif_data->vif_id,
              MM_MAC_ADDR_VAL(vif_data->mac_addr));

    return MMWLAN_SUCCESS;
error:
    MMLOG_WRN("Failed to add %s interface\n", umac_interface_type_to_str(type));
    return status;
}

static void umac_interface_execute_inactive_cb(struct umac_interface_data *data)
{
    if (data->inactive_callback != NULL)
    {
        data->inactive_callback(data->inactive_cb_arg);
    }


    data->inactive_callback = NULL;
}

void umac_interface_remove(struct umac_data *umacd, enum umac_interface_type type)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);
    struct umac_interface_vif_data *vif_data_sta =
        umac_data_get_interface_vif(umacd, MMWLAN_VIF_STA);
    struct umac_interface_vif_data *vif_data_ap = umac_data_get_interface_vif(umacd, MMWLAN_VIF_AP);
    struct umac_interface_vif_data *vif_data = NULL;

    if (type & VIF_STA_INTERFACE_TYPES_MASK)
    {
        vif_data = vif_data_sta;
    }
    else if (type == UMAC_INTERFACE_AP)
    {
        vif_data = vif_data_ap;
    }

    MMOSAL_ASSERT(vif_data != NULL);

    uint16_t active_interface_types = vif_data_sta->active_interface_types |
                                      vif_data_ap->active_interface_types;

    if (vif_data->active_interface_types == 0)
    {
        MMLOG_DBG("%s interface is already inactive\n", umac_interface_type_to_str(type));
        if (active_interface_types == 0)
        {
            umac_interface_execute_inactive_cb(data);
        }
        return;
    }

    if (type & UMAC_INTERFACE_STA)
    {

        umac_twt_deinit_vif(umacd, &vif_data->vif_id);
    }

    vif_data->active_interface_types &= ~((uint16_t)type);

    MMLOG_DBG("Interface remove %s, active=%u\n",
              umac_interface_type_to_str(type),
              vif_data->active_interface_types);

    if (vif_data->active_interface_types == 0)
    {
        MMLOG_DBG("Shutting down VIF %d\n", vif_data->vif_id);

        enum mmwlan_status status = mmdrv_rm_if(vif_data->vif_id);
        if (status != MMWLAN_SUCCESS)
        {
            if (umac_shutdown_is_in_progress(umacd))
            {

                MMLOG_WRN("Failed to remove %s interface (%d); ignoring.\n",
                          umac_interface_type_to_str(type),
                          status);
            }
            else
            {
                MMLOG_ERR("Failed to remove interface (%u)\n", status);
                MMOSAL_ASSERT(status == MMWLAN_SUCCESS);
            }
        }

        vif_data->vif_id = MMDRV_VIF_ID_INVALID;
    }

    if ((vif_data_sta->active_interface_types | vif_data_ap->active_interface_types) == 0)
    {
        umac_health_check_stop(umacd);

        mmdrv_deinit();

        umac_interface_execute_inactive_cb(data);


        uint8_t backup_mac_addr[DOT11_MAC_ADDR_LEN];
        memcpy(backup_mac_addr, data->mac_addr, sizeof(backup_mac_addr));
        const char *chip_id_string = data->morse_chip_id_string;
        uint32_t chip_id;
        memcpy(&chip_id, &data->morse_chip_id, sizeof(chip_id));
        struct mmdrv_fw_version fw_version;
        memcpy(&fw_version, &data->fw_version, sizeof(fw_version));
        memset(data, 0, sizeof(*data));
        memcpy(data->mac_addr, backup_mac_addr, sizeof(data->mac_addr));
        memcpy(&data->fw_version, &fw_version, sizeof(fw_version));
        data->morse_chip_id_string = chip_id_string;
        memcpy(&data->morse_chip_id, &chip_id, sizeof(chip_id));

        umac_ps_reset(umacd);
    }
}

bool umac_interface_is_active(struct umac_data *umacd)
{
    struct umac_interface_vif_data *vif_data = umac_data_get_interface_vif(umacd, MMWLAN_VIF_STA);
    if (vif_data->active_interface_types != 0)
    {
        return true;
    }

    vif_data = umac_data_get_interface_vif(umacd, MMWLAN_VIF_AP);
    if (vif_data->active_interface_types != 0)
    {
        return true;
    }

    return false;
}

uint16_t umac_interface_get_vif_id(struct umac_data *umacd, uint16_t type_mask)
{
    if (type_mask & VIF_STA_INTERFACE_TYPES_MASK)
    {
        struct umac_interface_vif_data *vif_data =
            umac_data_get_interface_vif(umacd, MMWLAN_VIF_STA);
        return vif_data->vif_id;
    }
    else if (type_mask & UMAC_INTERFACE_AP)
    {
        struct umac_interface_vif_data *vif_data =
            umac_data_get_interface_vif(umacd, MMWLAN_VIF_AP);
        return vif_data->vif_id;
    }
    else
    {
        return MMDRV_VIF_ID_INVALID;
    }
}

enum mmwlan_vif umac_interface_get_vif_by_id(struct umac_data *umacd, uint16_t vif_id)
{
    if (vif_id == MMDRV_VIF_ID_INVALID)
    {
        return MMWLAN_VIF_UNSPECIFIED;
    }
    struct umac_interface_vif_data *vif_data_sta =
        umac_data_get_interface_vif(umacd, MMWLAN_VIF_STA);
    if (vif_data_sta != NULL && vif_data_sta->vif_id == vif_id)
    {
        return MMWLAN_VIF_STA;
    }

    struct umac_interface_vif_data *vif_data_ap = umac_data_get_interface_vif(umacd, MMWLAN_VIF_AP);
    if (vif_data_ap != NULL && vif_data_ap->vif_id == vif_id)
    {
        return MMWLAN_VIF_AP;
    }

    return MMWLAN_VIF_UNSPECIFIED;
}

static struct umac_interface_vif_data *umac_interface_get_vif_data_by_vif_id(
    struct umac_data *umacd,
    uint16_t vif_id)
{
    if (vif_id == MMDRV_VIF_ID_INVALID)
    {
        return NULL;
    }
    enum mmwlan_vif vif = umac_interface_get_vif_by_id(umacd, vif_id);
    struct umac_interface_vif_data *data = umac_data_get_interface_vif(umacd, vif);
    if (data == NULL)
    {
        MMLOG_WRN("Failed to get data for vif_id %u, vif %u\n", vif_id, vif);
    }
    return data;
}

enum mmwlan_status umac_interface_reinstall_vif(struct umac_data *umacd, enum mmwlan_vif vif)
{
    struct umac_interface_vif_data *vif_data = umac_data_get_interface_vif(umacd, vif);

    if (vif_data->active_interface_types == 0)
    {
        MMLOG_WRN("Unable to reinstall VIF %u: not active\n", vif);
        return MMWLAN_UNAVAILABLE;
    }

    enum mmdrv_interface_type drv_if_type = (vif == MMWLAN_VIF_AP) ? MMDRV_INTERFACE_TYPE_AP :
                                                                     MMDRV_INTERFACE_TYPE_STA;
    enum mmwlan_status status = mmdrv_add_if(&vif_data->vif_id, vif_data->mac_addr, drv_if_type);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    umac_interface_init_vif(umacd, vif_data);

    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_interface_get_fw_version(struct umac_data *umacd,
                                                 struct mmdrv_fw_version *version)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);

    MMOSAL_DEV_ASSERT(version != NULL);

    memcpy(version, &data->fw_version, sizeof(data->fw_version));
    return MMWLAN_SUCCESS;
}

uint32_t umac_interface_get_chip_id(struct umac_data *umacd)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);

    if (data->morse_chip_id != 0)
    {
        return data->morse_chip_id;
    }
    MMLOG_WRN("Failed: invalid chip id\n");
    return 0;
}

const char *umac_interface_get_chip_id_string(struct umac_data *umacd)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);

    if (data->morse_chip_id_string != NULL)
    {
        return data->morse_chip_id_string;
    }
    MMLOG_WRN("Failed: invalid chip id\n");
    return NULL;
}

enum mmwlan_status umac_interface_get_mac_addr(struct umac_sta_data *stad, uint8_t *mac_addr)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_interface_vif_data *data =
        umac_interface_get_vif_data_by_vif_id(umacd, umac_sta_data_get_vif_id(stad));
    if (data == NULL)
    {
        memset(mac_addr, 0, MMWLAN_MAC_ADDR_LEN);
        MMLOG_INF("Failed: no interface\n");
        return MMWLAN_UNAVAILABLE;
    }

    mac_addr_copy(mac_addr, data->mac_addr);
    return MMWLAN_SUCCESS;
}

const uint8_t *umac_interface_peek_mac_addr(struct umac_sta_data *stad)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_interface_vif_data *data =
        umac_interface_get_vif_data_by_vif_id(umacd, umac_sta_data_get_vif_id(stad));
    if (data != NULL)
    {
        return data->mac_addr;
    }
    else
    {
        return NULL;
    }
}

bool umac_interface_addr_matches_mac_addr(struct umac_sta_data *stad, const uint8_t *addr)
{
    const uint8_t *stad_addr = umac_interface_peek_mac_addr(stad);
    if (stad_addr == NULL)
    {
        return false;
    }
    return mm_mac_addr_is_equal(addr, stad_addr);
}

enum mmwlan_status umac_interface_set_scan(struct umac_data *umacd, bool enabled)
{
    struct umac_interface_vif_data *data = umac_data_get_interface_vif(umacd, MMWLAN_VIF_STA);

    if (data->active_interface_types == 0)
    {
        MMLOG_INF("Failed: no interface\n");
        return MMWLAN_ERROR;
    }

    MMLOG_INF("Setting scan mode: enabled=%s\n", (enabled ? "true" : "false"));

    enum mmwlan_status status = mmdrv_cfg_scan(enabled);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to set scan mode (%u)\n", status);
        return status;
    }

    return MMWLAN_SUCCESS;
}

int umac_interface_calc_pri_1mhz_idx(struct umac_data *umacd,
                                     const struct ie_s1g_operation *s1g_operation,
                                     const struct mmwlan_s1g_channel *operating_chan)
{
    const struct mmwlan_s1g_channel *primary_chan =
        umac_regdb_get_channel(umacd, s1g_operation->primary_channel_number);

    if (primary_chan == NULL)
    {
        return -1;
    }

    const int32_t freq_delta_hz = primary_chan->centre_freq_hz - operating_chan->centre_freq_hz;
    const int32_t bw_margin_hz =
        (operating_chan->bw_mhz - primary_chan->bw_mhz) * (MHZ_TO_HZ(1) / 2);

    int pri_1mhz_idx = (freq_delta_hz + bw_margin_hz) / MHZ_TO_HZ(1);

    if (primary_chan->bw_mhz > 1)
    {
        pri_1mhz_idx += s1g_operation->primary_1mhz_channel_loc;
    }

    if (pri_1mhz_idx < 0 || pri_1mhz_idx >= operating_chan->bw_mhz)
    {
        return -1;
    }

    return pri_1mhz_idx;
}

const struct mmwlan_s1g_channel *umac_interface_calc_pri_channel(
    struct umac_data *umacd,
    const struct mmwlan_s1g_channel *operating_chan,
    uint8_t pri_1mhz_chan_idx,
    uint8_t pri_bw_mhz)
{
    MMOSAL_DEV_ASSERT(pri_1mhz_chan_idx < operating_chan->bw_mhz);
    MMOSAL_DEV_ASSERT((pri_bw_mhz == 1) || (pri_bw_mhz == 2));


    uint8_t primary_1mhz_offset = 0;
    if (pri_bw_mhz == 2)
    {
        primary_1mhz_offset = pri_1mhz_chan_idx % 2;
    }

    const int32_t bw_margin_hz = (operating_chan->bw_mhz - pri_bw_mhz) * (MHZ_TO_HZ(1) / 2);
    const int32_t freq_delta_hz =
        (pri_1mhz_chan_idx - primary_1mhz_offset) * MHZ_TO_HZ(1) - bw_margin_hz;
    const int32_t primary_centre_freq_hz = operating_chan->centre_freq_hz + freq_delta_hz;

    return umac_regdb_get_channel_from_freq_and_bw(umacd,
                                                   (uint32_t)primary_centre_freq_hz,
                                                   pri_bw_mhz);
}

static enum mmwlan_status umac_interface_set_channel_internal(
    struct umac_data *umacd,
    const struct ie_s1g_operation *s1g_operation,
    const struct mmwlan_s1g_channel *s1g_channel_info,
    bool is_off_channel)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);
    enum mmwlan_status status = MMWLAN_SUCCESS;

    if (!umac_interface_is_active(umacd))
    {
        MMLOG_INF("Failed: no interface\n");
        return MMWLAN_ERROR;
    }

    MMLOG_INF(
        "Setting channel %u: op freq=%lu Hz, pri ch=%u, pri 1M loc=%d, bw=%u MHz, pri bw=%u MHz\n",
        s1g_operation->operating_channel_index,
        s1g_channel_info->centre_freq_hz,
        s1g_operation->primary_channel_number,
        s1g_operation->primary_1mhz_channel_loc,
        s1g_channel_info->bw_mhz,
        s1g_operation->primary_channel_width_mhz);


    if (s1g_operation->primary_channel_width_mhz > 2 ||
        s1g_operation->primary_channel_width_mhz > s1g_operation->operation_channel_width_mhz)
    {
        MMLOG_ERR("Invalid primary bandwidth %u\n", s1g_operation->primary_channel_width_mhz);
        return MMWLAN_CHANNEL_INVALID;
    }

    if (s1g_operation->operation_channel_width_mhz > umac_interface_max_supported_bw(umacd))
    {
        MMLOG_ERR("%u MHz not supported\n", s1g_operation->operation_channel_width_mhz);
        return MMWLAN_CHANNEL_INVALID;
    }

    if (s1g_operation->operation_channel_width_mhz != s1g_channel_info->bw_mhz)
    {
        MMLOG_ERR("Invalid operating bw %u, expect %u (chan#=%u)\n",
                  s1g_operation->operation_channel_width_mhz,
                  s1g_channel_info->bw_mhz,
                  s1g_channel_info->s1g_chan_num);
        return MMWLAN_CHANNEL_INVALID;
    }

    if (s1g_operation->primary_1mhz_channel_loc > 1 ||
        s1g_operation->primary_1mhz_channel_loc == s1g_channel_info->bw_mhz)
    {
        MMLOG_ERR("Invalid primary 1 MHz channel %u\n", s1g_operation->primary_1mhz_channel_loc);
        return MMWLAN_CHANNEL_INVALID;
    }

    int prim_1mhz_chan_idx =
        umac_interface_calc_pri_1mhz_idx(umacd, s1g_operation, s1g_channel_info);
    if (prim_1mhz_chan_idx < 0)
    {
        MMLOG_ERR("Invalid primary channel config in S1G operation\n");
        return MMWLAN_CHANNEL_INVALID;
    }

    if (ie_s1g_operation_is_equal(&data->current_s1g_operation, s1g_operation))
    {
        MMLOG_INF("Channel is the same, skipping set channel\n");
    }
    else
    {
        status = mmdrv_set_channel(s1g_channel_info->centre_freq_hz,
                                   prim_1mhz_chan_idx,
                                   s1g_channel_info->bw_mhz,
                                   s1g_operation->primary_channel_width_mhz,
                                   is_off_channel);
        if (status != MMWLAN_SUCCESS)
        {
            MMLOG_WRN("Failed to set channel %u: %u\n",
                      s1g_operation->operating_channel_index,
                      status);
            return status;
        }
        data->current_s1g_operation = *s1g_operation;
    }

    int32_t actual_txpower = 0;
    int new_txpower = s1g_channel_info->max_tx_eirp_dbm;
    uint16_t tx_power_override = umac_config_get_max_tx_power(umacd);
    if (tx_power_override > 0 && tx_power_override < new_txpower)
    {
        new_txpower = tx_power_override;
    }

    status = mmdrv_set_txpower(&actual_txpower, new_txpower);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to set power level %d\n", new_txpower);
        return status;
    }

    enum mmwlan_duty_cycle_mode duty_cycle_mode = umac_config_get_duty_cycle_mode(umacd);
    status = mmdrv_set_duty_cycle(s1g_channel_info->duty_cycle_sta,
                                  s1g_channel_info->duty_cycle_omit_ctrl_resp,
                                  duty_cycle_mode);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to set duty cycle %d\n", s1g_channel_info->duty_cycle_sta);
        return status;
    }

    status = mmdrv_cfg_mpsw(s1g_channel_info->airtime_min_us,
                            s1g_channel_info->airtime_max_us,
                            s1g_channel_info->pkt_spacing_us);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to configure mpsw.\n");
        return status;
    }

    MMLOG_INF("Requested tx power %d, actual %ld; duty cycle %u.%02u %%, "
              "minimum airtime %lu us, maximum airtime %lu us, packet space window %lu us.\n",
              new_txpower,
              actual_txpower,
              s1g_channel_info->duty_cycle_sta / 100,
              s1g_channel_info->duty_cycle_sta % 100,
              s1g_channel_info->airtime_min_us,
              s1g_channel_info->airtime_max_us,
              s1g_channel_info->pkt_spacing_us);

    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_interface_set_channel(struct umac_data *umacd,
                                              const struct ie_s1g_operation *s1g_operation)
{
    const struct mmwlan_s1g_channel *operating_chan =
        umac_regdb_get_channel(umacd, s1g_operation->operating_channel_index);
    if (operating_chan == NULL ||
        !umac_regdb_op_class_match(umacd, s1g_operation->operating_class, operating_chan))
    {
        MMLOG_ERR("No matching channel (reg_dom=%s, op_class=%u, chan#=%u)\n",
                  umac_regdb_get_country_code(umacd),
                  s1g_operation->operating_class,
                  s1g_operation->operating_channel_index);
        return MMWLAN_CHANNEL_INVALID;
    }

    return umac_interface_set_channel_internal(umacd, s1g_operation, operating_chan, false);
}

enum mmwlan_status umac_interface_set_channel_from_regdb(struct umac_data *umacd,
                                                         const struct mmwlan_s1g_channel *channel,
                                                         bool is_off_channel)
{
    struct ie_s1g_operation s1g_operation = {
        .primary_channel_width_mhz = channel->bw_mhz > 1 ? 2 : 1,
        .operation_channel_width_mhz = channel->bw_mhz,
        .primary_1mhz_channel_loc = 0,
        .recommend_no_mcs10 = false,
        .operating_class = channel->global_operating_class,
        .primary_channel_number = channel->s1g_chan_num,
        .operating_channel_index = channel->s1g_chan_num,
    };

    return umac_interface_set_channel_internal(umacd, &s1g_operation, channel, is_off_channel);
}

const struct ie_s1g_operation *umac_interface_get_current_s1g_operation_info(
    struct umac_data *umacd)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);
    return &data->current_s1g_operation;
}

enum mmwlan_status umac_interface_reconfigure_channel(struct umac_data *umacd)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);


    struct ie_s1g_operation s1g_operation = data->current_s1g_operation;


    memset(&data->current_s1g_operation, 0, sizeof(data->current_s1g_operation));

    if (s1g_operation.operating_channel_index != 0)
    {
        return umac_interface_set_channel(umacd, &s1g_operation);
    }
    else
    {
        return MMWLAN_SUCCESS;
    }
}

const struct morse_caps *umac_interface_get_capabilities(struct umac_data *umacd)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);

    return &data->capabilities;
}

uint8_t umac_interface_max_supported_bw(struct umac_data *umacd)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);
    const struct morse_caps *capabilities = &data->capabilities;

    if (morse_caps_supported(capabilities, MORSE_CAPS_16MHZ))
    {
        return 16;
    }
    if (morse_caps_supported(capabilities, MORSE_CAPS_8MHZ))
    {
        return 8;
    }
    if (morse_caps_supported(capabilities, MORSE_CAPS_4MHZ))
    {
        return 4;
    }
    if (morse_caps_supported(capabilities, MORSE_CAPS_2MHZ))
    {
        return 2;
    }
    return 1;
}

enum mmwlan_status umac_interface_set_ndp_probe_support(struct umac_data *umacd, bool enabled)
{
    uint16_t vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_SCAN);
    if (vif_id == MMDRV_VIF_ID_INVALID)
    {
        MMLOG_INF("Failed: no STA/SCAN interface\n");
        return MMWLAN_UNAVAILABLE;
    }

    MMLOG_INF("Setting ndp probe support: enabled=%s\n", (enabled ? "true" : "false"));

    enum mmwlan_status status = mmdrv_set_ndp_probe(vif_id, enabled);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to set ndp probe support (%u)\n", status);
        return status;
    }

    return MMWLAN_SUCCESS;
}

void umac_interface_register_inactive_cb(struct umac_data *umacd,
                                         umac_interface_inactive_cb_t callback,
                                         void *arg)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);
    data->inactive_callback = callback;
    data->inactive_cb_arg = arg;
}

const struct umac_datapath_ops *umac_interface_get_datapath_ops(struct umac_data *umacd,
                                                                enum mmwlan_vif vif)
{
    struct umac_interface_vif_data *data = umac_data_get_interface_vif(umacd, vif);
    if (data != NULL && data->active_interface_types != 0)
    {
        MMOSAL_DEV_ASSERT(data->vif_id != MMDRV_VIF_ID_INVALID);
        return data->datapath_ops;
    }
    else
    {
        if (data == NULL && vif != MMWLAN_VIF_UNSPECIFIED)
        {
            MMLOG_WRN("Invalid VIF %u\n", vif);
        }
        return NULL;
    }
}

const struct umac_datapath_ops *umac_interface_get_datapath_ops_by_vif_id(struct umac_data *umacd,
                                                                          uint16_t vif_id)
{
    struct umac_interface_vif_data *data = umac_interface_get_vif_data_by_vif_id(umacd, vif_id);
    if (data != NULL)
    {
        return data->datapath_ops;
    }
    else
    {
        return NULL;
    }
}

enum mmwlan_status umac_interface_register_vif_state_cb(struct umac_data *umacd,
                                                        enum mmwlan_vif vif,
                                                        mmwlan_vif_state_cb_t callback,
                                                        void *arg)
{
    if (vif == MMWLAN_VIF_UNSPECIFIED)
    {
        umac_interface_register_vif_state_cb(umacd, MMWLAN_VIF_STA, callback, arg);
        umac_interface_register_vif_state_cb(umacd, MMWLAN_VIF_AP, callback, arg);
        return MMWLAN_SUCCESS;
    }

    struct umac_interface_vif_data *data = umac_data_get_interface_vif(umacd, vif);
    if (data == NULL)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    data->vif_state_cb = callback;
    data->vif_state_cb_arg = arg;

    return MMWLAN_SUCCESS;
}

bool umac_interface_invoke_vif_state_cb(struct umac_data *umacd,
                                        const struct mmwlan_vif_state *state)
{
    struct umac_interface_vif_data *data = umac_data_get_interface_vif(umacd, state->vif);
    if (data == NULL || data->vif_state_cb == NULL)
    {
        return false;
    }

    data->vif_state_cb(state, data->vif_state_cb_arg);
    return true;
}

enum mmwlan_status umac_interface_get_device_mac_addr(struct umac_data *umacd, uint8_t *mac_addr)
{
    struct umac_interface_data *data = umac_data_get_interface(umacd);
    mac_addr_copy(mac_addr, data->mac_addr);

    if (mm_mac_addr_is_zero(data->mac_addr))
    {
        MMLOG_INF("Failed: no MAC addr\n");
        return MMWLAN_UNAVAILABLE;
    }

    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_interface_get_vif_mac_addr(struct umac_data *umacd,
                                                   enum mmwlan_vif vif,
                                                   uint8_t *mac_addr)
{
    struct umac_interface_vif_data *data = umac_data_get_interface_vif(umacd, vif);
    if (data != NULL && data->active_interface_types != 0)
    {
        mac_addr_copy(mac_addr, data->mac_addr);
        return MMWLAN_SUCCESS;
    }

    MMLOG_DBG("VIF not active\n");
    return MMWLAN_UNAVAILABLE;
}

enum mmwlan_status umac_interface_borrow_vif_mac_addr(struct umac_data *umacd,
                                                      enum mmwlan_vif vif,
                                                      const uint8_t **mac_addr)
{
    struct umac_interface_vif_data *data = umac_data_get_interface_vif(umacd, vif);
    if (data != NULL && data->active_interface_types != 0)
    {
        *mac_addr = data->mac_addr;
        return MMWLAN_SUCCESS;
    }

    MMLOG_DBG("VIF not active\n");
    return MMWLAN_UNAVAILABLE;
}

enum mmwlan_status umac_interface_register_rx_pkt_ext_cb(struct umac_data *umacd,
                                                         enum mmwlan_vif vif,
                                                         mmwlan_rx_pkt_ext_cb_t callback,
                                                         void *arg)
{
    if (vif == MMWLAN_VIF_UNSPECIFIED)
    {
        umac_interface_register_rx_pkt_ext_cb(umacd, MMWLAN_VIF_STA, callback, arg);
        umac_interface_register_rx_pkt_ext_cb(umacd, MMWLAN_VIF_AP, callback, arg);
        return MMWLAN_SUCCESS;
    }

    struct umac_interface_vif_data *data = umac_data_get_interface_vif(umacd, vif);
    if (data == NULL)
    {
        return MMWLAN_INVALID_ARGUMENT;
    }

    data->rx_pkt_ext_cb = callback;
    data->rx_pkt_ext_cb_arg = arg;

    return MMWLAN_SUCCESS;
}

mmwlan_rx_pkt_ext_cb_t umac_interface_get_rx_pkt_ext_cb(struct umac_data *umacd,
                                                        enum mmwlan_vif vif,
                                                        void **arg)
{
    MMOSAL_DEV_ASSERT(arg != NULL);

    struct umac_interface_vif_data *data = umac_data_get_interface_vif(umacd, vif);
    if (data == NULL)
    {
        *arg = NULL;
        return NULL;
    }

    *arg = data->rx_pkt_ext_cb_arg;
    return data->rx_pkt_ext_cb;
}
