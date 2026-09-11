/*
 * Copyright 2021-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "mmwlan.h"
#include "umac_supp_shim_private.h"
#include "umac/ap/umac_ap.h"
#include "umac/interface/umac_interface.h"


#define AUTOSCAN_CONFIG_MAXLEN (32)


struct mmwlan_supp_config_entry
{

    const char *name;

    bool (*reader)(struct wpa_config *config, bool ro);
};


static enum mfp_options translate_pmf_option(enum mmwlan_pmf_mode pmf_mode)
{
    switch (pmf_mode)
    {
        case MMWLAN_PMF_DISABLED:
            return NO_MGMT_FRAME_PROTECTION;

        case MMWLAN_PMF_REQUIRED:
            return MGMT_FRAME_PROTECTION_REQUIRED;

        default:
            MMOSAL_ASSERT(false);
    }


    MMOSAL_ASSERT(false);
    return MGMT_FRAME_PROTECTION_REQUIRED;
}


static bool config_populate_network_security(struct wpa_ssid *ssid,
                                             enum mmwlan_security_type security_type,
                                             const char *passphrase,
                                             uint16_t passphrase_len)
{
    switch (security_type)
    {
        case MMWLAN_OPEN:

            ssid->key_mgmt = WPA_KEY_MGMT_NONE;
            break;

        case MMWLAN_OWE:
            ssid->pairwise_cipher = WPA_CIPHER_CCMP;
            ssid->group_cipher = WPA_CIPHER_CCMP;
            ssid->key_mgmt = WPA_KEY_MGMT_OWE;
            ssid->proto = WPA_PROTO_RSN;


            ssid->owe_only = 1;
            break;

        case MMWLAN_SAE:
            ssid->pairwise_cipher = WPA_CIPHER_CCMP;
            ssid->group_cipher = WPA_CIPHER_CCMP;
            ssid->key_mgmt = WPA_KEY_MGMT_SAE;
            ssid->proto = WPA_PROTO_RSN;
            if (passphrase_len == 0)
            {
                passphrase_len = strlen(passphrase);
            }
            ssid->sae_password = (char *)os_malloc(passphrase_len + 1);
            if (ssid->sae_password == NULL)
            {
                MMLOG_ERR("Failed to allocate sae_password buffer for config\n");
                return false;
            }
            memcpy(ssid->sae_password, passphrase, passphrase_len);
            ssid->sae_password[passphrase_len] = '\0';
            break;
    }

    return true;
}


static bool config_add_network(struct wpa_config *config, bool ro, struct umac_data *umacd)
{
    const struct mmwlan_sta_args *args = umac_connection_get_sta_args(umacd);
    const struct mmwlan_twt_config_args *twt_config = umac_twt_get_config(umacd);
    struct wpa_ssid *ssid = wpa_config_add_network(config);
    if (ssid == NULL)
    {
        MMLOG_ERR("Failed to add network to supplicant\n");
        return false;
    }

    wpa_config_set_network_defaults(ssid);

    ssid->ro = ro;

    ssid->ssid = (u8 *)os_malloc(args->ssid_len);
    if (ssid->ssid == NULL)
    {
        MMLOG_ERR("Failed to allocate SSID buffer for config\n");
        goto cleanup;
    }
    memcpy(config->ssid->ssid, args->ssid, args->ssid_len);
    config->ssid->ssid_len = args->ssid_len;

    if (!mm_mac_addr_is_zero(args->bssid))
    {
        memcpy(config->ssid->bssid, args->bssid, MMWLAN_MAC_ADDR_LEN);
        config->ssid->bssid_set = 1;
    }

    ssid->raw_sta_priority = args->raw_sta_priority;
    ssid->cac = args->cac_mode == MMWLAN_CAC_ENABLED ? 1 : 0;

    if (args->bgscan_short_interval_s && args->bgscan_long_interval_s)
    {
        char bgscan_str[30];
        snprintf(bgscan_str,
                 sizeof(bgscan_str),
                 "simple:%d:%d:%d",
                 args->bgscan_short_interval_s,
                 args->bgscan_signal_threshold_dbm,
                 args->bgscan_long_interval_s);

        ssid->bgscan = (char *)os_malloc((strlen(bgscan_str) + 1));
        if (ssid->bgscan == NULL)
        {
            MMLOG_ERR("Failed to allocate bgscan buffer for config\n");
            goto cleanup;
        }
        memcpy(ssid->bgscan, bgscan_str, (strlen(bgscan_str) + 1));
    }

    if (twt_config->twt_mode == MMWLAN_TWT_REQUESTER)
    {
        ssid->twt_conf.enable = true;
        ssid->twt_conf.wake_interval_us = twt_config->twt_wake_interval_us;
        ssid->twt_conf.wake_duration_us = twt_config->twt_min_wake_duration_us;
        ssid->twt_conf.setup_command = twt_config->twt_setup_command;
    }

    if (!config_populate_network_security(ssid,
                                          args->security_type,
                                          args->passphrase,
                                          args->passphrase_len))
    {
        goto cleanup;
    }

    return true;

cleanup:
    wpa_config_free_ssid(ssid);
    return false;
}


static bool config_populate_autoscan(struct wpa_config *config, const struct mmwlan_sta_args *args)
{
    int ret;
    uint16_t scan_interval_base_s;
    uint16_t scan_interval_limit_s;

    if (config->autoscan == NULL)
    {
        config->autoscan = (char *)os_malloc(AUTOSCAN_CONFIG_MAXLEN);
        if (config->autoscan == NULL)
        {
            return false;
        }
    }

    scan_interval_base_s = args->scan_interval_base_s;
    if (scan_interval_base_s == 0)
    {
        scan_interval_base_s = MMWLAN_DEFAULT_SCAN_INTERVAL_BASE_S;
    }

    scan_interval_limit_s = args->scan_interval_limit_s;
    if (scan_interval_limit_s == 0)
    {
        scan_interval_limit_s = MMWLAN_DEFAULT_SCAN_INTERVAL_LIMIT_S;
    }

    ret = snprintf(config->autoscan,
                   AUTOSCAN_CONFIG_MAXLEN,
                   "exponential:%d:%d",
                   scan_interval_base_s,
                   scan_interval_limit_s);

    return (ret >= 0) ? true : false;
}


static bool config_populate_sae_groups(struct wpa_config *config,
                                       const int *sae_owe_ec_groups,
                                       unsigned n_sae_owe_ec_groups)
{
    if (sae_owe_ec_groups[0] == 0)
    {
        return true;
    }

    if (config->sae_groups != NULL)
    {
        os_free(config->sae_groups);
    }

    size_t alloc_size = n_sae_owe_ec_groups * sizeof(sae_owe_ec_groups[0]);
    config->sae_groups = (int *)os_malloc(alloc_size);
    if (config->sae_groups == NULL)
    {
        MMLOG_ERR("Failed to allocate sae_groups buffer for config\n");
        return false;
    }
    memcpy(config->sae_groups, sae_owe_ec_groups, alloc_size);

    return true;
}


static bool wpa_config_read_sta(struct wpa_config *config, bool ro)
{
    MMOSAL_DEV_ASSERT(config != NULL);
    bool ok = false;
    struct umac_data *umacd = umac_data_get_umacd();

    const struct mmwlan_sta_args *args = umac_connection_get_sta_args(umacd);
    if (args == NULL)
    {
        return false;
    }


    config->pmf = translate_pmf_option(args->pmf_mode);


    config->sae_pwe = SAE_PWE_HASH_TO_ELEMENT;

    ok = config_populate_sae_groups(config,
                                    args->sae_owe_ec_groups,
                                    MM_ARRAY_COUNT(args->sae_owe_ec_groups));
    if (!ok)
    {
        return false;
    }

    ok = config_populate_autoscan(config, args);
    if (!ok)
    {
        return false;
    }

    ok = config_add_network(config, ro, umacd);
    if (!ok)
    {
        return false;
    }

    return true;
}

const struct mmwlan_supp_config_entry mmwlan_wpa_config_sta = {
    UMAC_SUPP_STA_CONFIG_NAME,
    wpa_config_read_sta,
};


static bool wpa_config_read_dpp(struct wpa_config *config, bool ro)
{
    MM_UNUSED(config);
    MM_UNUSED(ro);
    return true;
}

const struct mmwlan_supp_config_entry mmwlan_wpa_config_dpp = {
    UMAC_SUPP_DPP_CONFIG_NAME,
    wpa_config_read_dpp,
};


static bool wpa_config_read_ap(struct wpa_config *config, bool ro)
{
    struct umac_data *umacd = umac_data_get_umacd();
    const struct mmwlan_ap_args *args = umac_ap_get_args(umacd);

    struct wpa_ssid *ssid = wpa_config_add_network(config);
    if (ssid == NULL)
    {
        MMLOG_ERR("Failed to add network to supplicant\n");
        return false;
    }

    wpa_config_set_network_defaults(ssid);

    ssid->ro = ro;

    ssid->ssid = (u8 *)os_malloc(args->ssid_len);
    if (ssid->ssid == NULL)
    {
        MMLOG_ERR("Failed to allocate SSID buffer for config\n");
        return false;
    }
    memcpy(config->ssid->ssid, args->ssid, args->ssid_len);
    config->ssid->ssid_len = args->ssid_len;

    if (!mm_mac_addr_is_zero(args->bssid))
    {
        memcpy(config->ssid->bssid, args->bssid, MMWLAN_MAC_ADDR_LEN);
        config->ssid->bssid_set = 1;
    }

    const struct mmwlan_s1g_channel_list *chan_list = umac_config_get_channel_list(umacd);
    memcpy(config->country, chan_list->country_code, 2);

    const struct mmwlan_s1g_channel *chan = umac_ap_get_specified_s1g_channel(umacd);
    if (chan == NULL)
    {
        MMLOG_WRN("Invalid channel\n");
        MMOSAL_DEV_ASSERT(false);
        return false;
    }

    config->s1g_op_class = chan->s1g_operating_class;
    config->ssid->frequency_khz = chan->centre_freq_hz / 1000;
    config->ssid->s1g_prim_chwidth = args->pri_bw_mhz;
    config->ssid->primary_1mhz_channel_loc = args->pri_1mhz_chan_idx % 2;

    const struct mmwlan_s1g_channel *pri_chan =
        umac_interface_calc_pri_channel(umacd, chan, args->pri_1mhz_chan_idx, args->pri_bw_mhz);
    if (pri_chan == NULL)
    {
        return false;
    }

    config->ssid->s1g_prim_channel = pri_chan->s1g_chan_num;

    config->beacon_int = args->beacon_interval_tus;
    config->dtim_period = args->dtim_period;
    MMLOG_INF("Configured AP arguments:\n");
    MMLOG_INF("    Op Class (global): %u\n", config->op_class);
    MMLOG_INF("    Beacon interval:   %u TUs\n", config->beacon_int);
    MMLOG_INF("    DTIM period:       %u\n", config->dtim_period);
    MMLOG_INF("    Channel freq:      %u kHz\n", config->ssid->frequency_khz);
    MMLOG_INF("    Pri channel:       %u\n", config->ssid->s1g_prim_channel);
    MMLOG_INF("    Pri ch width:      %u MHz\n", config->ssid->s1g_prim_chwidth);
    MMLOG_INF("    Pri 1 MHz loc:     %u MHz\n", config->ssid->primary_1mhz_channel_loc);

    config->ssid->mode = WPAS_MODE_AP;

    bool ok = config_populate_network_security(config->ssid,
                                               args->security_type,
                                               args->passphrase,
                                               args->passphrase_len);
    if (!ok)
    {
        return false;
    }


    config->pmf = translate_pmf_option(args->pmf_mode);


    config->sae_pwe = SAE_PWE_HASH_TO_ELEMENT;

    ok = config_populate_sae_groups(config,
                                    args->sae_owe_ec_groups,
                                    MM_ARRAY_COUNT(args->sae_owe_ec_groups));
    if (!ok)
    {
        return false;
    }
    return true;
}

const struct mmwlan_supp_config_entry mmwlan_wpa_config_ap = {
    UMAC_SUPP_AP_CONFIG_NAME,
    wpa_config_read_ap,
};



extern const struct mmwlan_supp_config_entry *const mmwlan_wpa_configs[];


static bool find_and_read_config(const char *name, struct wpa_config *config, bool ro)
{
    for (const struct mmwlan_supp_config_entry *const *iter = mmwlan_wpa_configs; *iter != NULL;
         iter++)
    {
        const struct mmwlan_supp_config_entry *entry = *iter;

        MMOSAL_DEV_ASSERT(entry->name != NULL);
        MMOSAL_DEV_ASSERT(entry->reader != NULL);

        if (os_strcmp(name, entry->name) == 0)
        {
            return entry->reader(config, ro);
        }
    }

    MMOSAL_DEV_ASSERT(false);
    return false;
}


struct wpa_config *wpa_config_read(const char *name, struct wpa_config *cfgp, bool ro)
{
    MMLOG_INF("Reading WPA Supplicant config: %s\n", name);

    struct wpa_config *config = cfgp;
    if (config == NULL)
    {
        config = wpa_config_alloc_empty(NULL, NULL);
    }
    if (config == NULL)
    {
        MMLOG_ERR("Failed to allocate config file structure\n");
        return NULL;
    }

    if (!find_and_read_config(name, config, ro))
    {
        goto cleanup;
    }

    return config;

cleanup:
    if (config != cfgp)
    {
        wpa_config_free(config);
    }
    return NULL;
}

int wpa_config_write(const char *name, struct wpa_config *config)
{
    (void)name;
    (void)config;
    return 0;
}
