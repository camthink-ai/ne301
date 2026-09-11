/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_rc.h"
#include "umac_rc_data.h"
#include "mmlog.h"
#include "mmrc.h"
#include "umac/config/umac_config.h"
#include "umac/connection/umac_connection.h"
#include "umac/core/umac_core.h"
#include "umac/ba/umac_ba.h"
#include "umac/umac.h"
#include "umac/interface/umac_interface.h"
#include "umac/stats/umac_stats.h"
#include "mmosal.h"
#include "mmpkt.h"
#include "mmdrv.h"

#define SUPPORTED_SPATIAL_STREAMS MMRC_MASK(MMRC_SPATIAL_STREAM_1)
#define SUPPORTED_STA_FLAGS       MMRC_MASK(MMRC_FLAGS_CTS_RTS);
#define SUPPORTED_MAX_RATES       4

#ifdef ENABLE_RC_TRACE
#include "mmtrace.h"
static mmtrace_channel rc_channel_handle;
#define RC_TRACE_INIT()     rc_channel_handle = mmtrace_register_channel("rc")
#define RC_TRACE(_fmt, ...) mmtrace_printf(rc_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define RC_TRACE_INIT() \
    do {                \
    } while (0)
#define RC_TRACE(_fmt, ...) \
    do {                    \
    } while (0)
#endif


static void umac_rc_update(struct umac_sta_data *stad)
{
    struct umac_rc_sta_data *sta_data = umac_sta_data_get_rc(stad);

    if (sta_data->reference_table != NULL)
    {
        mmrc_update(sta_data->reference_table);
    }

    sta_data->next_update_time_ms = mmosal_get_time_ms() + MMRC_UPDATE_FREQUENCY_MS;
    MMLOG_DBG("Next update scheduled for %lums.\n", sta_data->next_update_time_ms);
}


static struct mmrc_table *umac_rc_sta_create(struct mmrc_sta_capabilities *caps)
{
    size_t alloc_size = mmrc_memory_required_for_caps(caps);
    struct mmrc_table *table = (struct mmrc_table *)mmosal_malloc(alloc_size);
    MMOSAL_ASSERT(table != NULL);
    memset(table, 0, alloc_size);
    return table;
}


static void umac_rc_sta_destroy(struct mmrc_table *tb)
{
    mmosal_free(tb);
}

void umac_rc_init(void)
{
    RC_TRACE_INIT();
    mmrc_init();
}

void umac_rc_start(struct umac_sta_data *stad, uint8_t sgi_flags, uint8_t max_mcs)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_rc_sta_data *sta_data = umac_sta_data_get_rc(stad);

    sta_data->local_capabilities.bandwidth = MMRC_MASK(MMRC_BW_1MHZ);

    const uint8_t max_bw = umac_interface_max_supported_bw(umacd);

    switch (max_bw)
    {
        case 16:
            sta_data->local_capabilities.bandwidth |= MMRC_MASK(MMRC_BW_16MHZ);
            MM_FALLTHROUGH;

        case 8:
            sta_data->local_capabilities.bandwidth |= MMRC_MASK(MMRC_BW_8MHZ);
            MM_FALLTHROUGH;

        case 4:
            sta_data->local_capabilities.bandwidth |= MMRC_MASK(MMRC_BW_4MHZ);
            MM_FALLTHROUGH;

        case 2:
            sta_data->local_capabilities.bandwidth |= MMRC_MASK(MMRC_BW_2MHZ);
            MM_FALLTHROUGH;

        case 1:
            break;

        default:
            MMOSAL_ASSERT(false);
    }


    sta_data->local_capabilities.guard = MMRC_MASK(MMRC_GUARD_LONG);
    sta_data->local_capabilities.sgi_per_bw = 0;

    if (umac_config_rc_is_sgi_enabled(umacd))
    {
        sta_data->local_capabilities.guard |= MMRC_MASK(MMRC_GUARD_SHORT);
        switch (max_bw)
        {
            case 16:
                sta_data->local_capabilities.sgi_per_bw |= SGI_PER_BW(MMRC_BW_16MHZ);
                MM_FALLTHROUGH;

            case 8:
                sta_data->local_capabilities.sgi_per_bw |= SGI_PER_BW(MMRC_BW_8MHZ);
                MM_FALLTHROUGH;

            case 4:
                sta_data->local_capabilities.sgi_per_bw |= SGI_PER_BW(MMRC_BW_4MHZ);
                MM_FALLTHROUGH;

            case 2:
                sta_data->local_capabilities.sgi_per_bw |= SGI_PER_BW(MMRC_BW_2MHZ);
                MM_FALLTHROUGH;

            case 1:
                sta_data->local_capabilities.sgi_per_bw |= SGI_PER_BW(MMRC_BW_1MHZ);
                break;

            default:
                MMOSAL_ASSERT(false);
        }
    }

    sta_data->local_capabilities.rates = MMRC_MASK(MMRC_MCS0) |
                                         MMRC_MASK(MMRC_MCS1) |
                                         MMRC_MASK(MMRC_MCS2) |
                                         MMRC_MASK(MMRC_MCS3) |
                                         MMRC_MASK(MMRC_MCS4) |
                                         MMRC_MASK(MMRC_MCS5) |
                                         MMRC_MASK(MMRC_MCS6) |
                                         MMRC_MASK(MMRC_MCS7);
    if (MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), MCS8))
    {
        sta_data->local_capabilities.rates |= MMRC_MASK(MMRC_MCS8);
    }
    if (MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), MCS9))
    {
        sta_data->local_capabilities.rates |= MMRC_MASK(MMRC_MCS9);
    }

    sta_data->local_capabilities.spatial_streams = SUPPORTED_SPATIAL_STREAMS;
    sta_data->local_capabilities.sta_flags = SUPPORTED_STA_FLAGS;

    MMLOG_INF("Starting rate control. SGI enabled bitmap %02x, max MCS %u\n", sgi_flags, max_mcs);

    const struct ie_s1g_operation *s1g_operation =
        umac_interface_get_current_s1g_operation_info(umacd);
    MMOSAL_ASSERT(s1g_operation != NULL);

    struct mmrc_sta_capabilities capabilities = sta_data->local_capabilities;

    uint16_t supported_bws = s1g_operation->operation_channel_width_mhz;
    if (umac_config_rc_are_subbands_enabled(umacd))
    {

        supported_bws |= (supported_bws - 1);
    }


    capabilities.bandwidth &= supported_bws;

    capabilities.rates &= MMRC_MASK(max_mcs) | (MMRC_MASK(max_mcs) - 1);
    capabilities.sgi_per_bw &= sgi_flags;
    if (capabilities.sgi_per_bw == 0)
    {
        capabilities.guard &= ~(MMRC_MASK(MMRC_GUARD_SHORT));
    }

    if (capabilities.bandwidth == 0 || capabilities.rates == 0)
    {
        MMLOG_WRN("Unable to negotiate parameters with AP\n");
        return;
    }

    capabilities.max_rates = SUPPORTED_MAX_RATES;

    capabilities.max_retries = MMRC_MAX_CHAIN_ATTEMPTS;

    if (sta_data->reference_table != NULL)
    {
        MMLOG_INF("RC already started, restarting\n");

        umac_rc_sta_destroy(sta_data->reference_table);
        sta_data->reference_table = NULL;
    }

    MMLOG_INF("Rate control negotiated capabilities:\n");
    MMLOG_INF("    Bandwidths 0x%04hx\n", capabilities.bandwidth);
    MMLOG_INF("    S. streams 0x%04hx\n", capabilities.spatial_streams);
    MMLOG_INF("    Rates      0x%04hx\n", capabilities.rates);
    MMLOG_INF("    Guard      0x%04hx\n", capabilities.guard);
    MMLOG_INF("    SGI per bw 0x%04hx\n", capabilities.sgi_per_bw);
    MMLOG_INF("    STA flags  0x%04hx\n", capabilities.sta_flags);

    RC_TRACE("RC start");
    RC_TRACE("BW    %hx", capabilities.bandwidth);
    RC_TRACE("Rates %hx", capabilities.rates);
    RC_TRACE("Guard %hx", capabilities.guard);
    RC_TRACE("Flags %hx", capabilities.sta_flags);

    sta_data->reference_table = umac_rc_sta_create(&capabilities);
    sta_data->active_capabilities = capabilities;


    mmrc_sta_init(sta_data->reference_table,
                  &sta_data->active_capabilities,
                  umac_stats_get_rssi(umacd));


    sta_data->next_update_time_ms = mmosal_get_time_ms() + MMRC_UPDATE_FREQUENCY_MS;
}

void umac_rc_stop(struct umac_sta_data *stad)
{
    struct umac_rc_sta_data *sta_data = umac_sta_data_get_rc(stad);

    umac_rc_sta_destroy(sta_data->reference_table);
    sta_data->reference_table = NULL;
}

void umac_rc_deinit(struct umac_sta_data *stad)
{
    struct umac_rc_sta_data *sta_data = umac_sta_data_get_rc(stad);

    umac_rc_sta_destroy(sta_data->reference_table);
    sta_data->reference_table = NULL;
}

static void apply_rate_table_overrides(struct umac_data *umacd, struct mmrc_rate_table *table)
{
    unsigned ii;
    const struct umac_config_rc_override *override = umac_config_rc_get_override(umacd);

    RC_TRACE("Generated rate table %x", (uint32_t)table);
    for (ii = 0; ii < MMRC_MAX_CHAIN_LENGTH; ii++)
    {
        uint32_t rate_info;

        switch (override->tx_rate)
        {
            case MMWLAN_MCS_NONE:

                break;

            case MMWLAN_MCS_0:
                table->rates[ii].rate = MMRC_MCS0;
                break;

            case MMWLAN_MCS_1:
                table->rates[ii].rate = MMRC_MCS1;
                break;

            case MMWLAN_MCS_2:
                table->rates[ii].rate = MMRC_MCS2;
                break;

            case MMWLAN_MCS_3:
                table->rates[ii].rate = MMRC_MCS3;
                break;

            case MMWLAN_MCS_4:
                table->rates[ii].rate = MMRC_MCS4;
                break;

            case MMWLAN_MCS_5:
                table->rates[ii].rate = MMRC_MCS5;
                break;

            case MMWLAN_MCS_6:
                table->rates[ii].rate = MMRC_MCS6;
                break;

            case MMWLAN_MCS_7:
                table->rates[ii].rate = MMRC_MCS7;
                break;

            case MMWLAN_MCS_8:
                table->rates[ii].rate = MMRC_MCS8;
                break;

            case MMWLAN_MCS_9:
                table->rates[ii].rate = MMRC_MCS9;
                break;
        }

        if (override->guard_interval > MMWLAN_GI_NONE)
        {
            table->rates[ii].guard =
                (override->guard_interval == MMWLAN_GI_SHORT) ? MMRC_GUARD_SHORT : MMRC_GUARD_LONG;
        }

        switch (override->bandwidth)
        {
            case MMWLAN_BW_NONE:

                break;

            case MMWLAN_BW_1MHZ:
                table->rates[ii].bw = MMRC_BW_1MHZ;
                break;

            case MMWLAN_BW_2MHZ:
                table->rates[ii].bw = MMRC_BW_2MHZ;
                break;

            case MMWLAN_BW_4MHZ:
                table->rates[ii].bw = MMRC_BW_4MHZ;
                break;

            case MMWLAN_BW_8MHZ:
                table->rates[ii].bw = MMRC_BW_8MHZ;
                break;
        }

        rate_info = ((table->rates[ii].rate & 0xff) << 24) |
                    (table->rates[ii].attempts << 16) |
                    (table->rates[ii].guard << 12) |
                    (table->rates[ii].ss << 8) |
                    (table->rates[ii].flags & 0xff);
        RC_TRACE("  %x", rate_info);
        MM_UNUSED(rate_info);
    }
}

static void apply_mcs10_mode(struct umac_data *umacd, struct mmrc_rate_table *table)
{
    unsigned ii;
    const enum mmwlan_mcs10_mode mcs10_mode = umac_config_get_mcs10_mode(umacd);
    switch (mcs10_mode)
    {
        case MMWLAN_MCS10_MODE_FORCED:
            if ((table->rates[0].rate == MMRC_MCS0) && (table->rates[0].bw == MMRC_BW_1MHZ))
            {
                table->rates[0].rate = MMRC_MCS10;
            }
            MM_FALLTHROUGH;

        case MMWLAN_MCS10_MODE_AUTO:
            for (ii = 1; ii < MMRC_MAX_CHAIN_LENGTH; ii++)
            {
                if ((table->rates[ii].rate == MMRC_MCS0) && (table->rates[ii].bw == MMRC_BW_1MHZ))
                {
                    table->rates[ii].rate = MMRC_MCS10;
                }
            }
            break;

        case MMWLAN_MCS10_MODE_DISABLED:
            break;
    }
}

#if MMLOG_LEVEL >= MMLOG_LEVEL_VRB
static void umac_rc_dump_rate_table(struct mmrc_rate_table *table)
{
    size_t ii;

    MMLOG_VRB("Rate table\n");

    for (ii = 0; ii < MM_ARRAY_COUNT(table->rates); ii++)
    {
        struct mmrc_rate *rate = &table->rates[ii];
        if (rate->rate == MMRC_MCS_UNUSED)
        {
            break;
        }

        MMLOG_VRB("  %u | %2d x | MCS%d | %u MHz | %cGI |%s\n",
                  ii,
                  rate->attempts,
                  rate->rate,
                  (1u << (unsigned)rate->bw),
                  rate->guard == MMRC_GUARD_SHORT ? 'S' : 'L',
                  rate->flags & MMRC_MASK(MMRC_FLAGS_CTS_RTS) ? " RTS" : "");
    }
}

#else
static void umac_rc_dump_rate_table(struct mmrc_rate_table *table)
{
    MM_UNUSED(table);
}

#endif

void umac_rc_init_rate_table_mgmt(struct umac_data *umacd,
                                  struct mmrc_rate_table *table,
                                  bool rts_required)
{
    const struct ie_s1g_operation *s1g_operation =
        umac_interface_get_current_s1g_operation_info(umacd);
    MMOSAL_ASSERT(s1g_operation != NULL);

    table->rates[0].attempts = 5;
    table->rates[0].rate = MMRC_MCS0;
    table->rates[0].bw = (s1g_operation->primary_channel_width_mhz == 1) ? MMRC_BW_1MHZ :
                                                                           MMRC_BW_2MHZ;
    table->rates[0].guard = MMRC_GUARD_LONG;
    table->rates[0].ss = MMRC_SPATIAL_STREAM_1;
    table->rates[0].flags = rts_required ? MMRC_MASK(MMRC_FLAGS_CTS_RTS) : 0;
    table->rates[1].rate = MMRC_MCS_UNUSED;
    table->rates[2].rate = MMRC_MCS_UNUSED;
    table->rates[3].rate = MMRC_MCS_UNUSED;
    apply_rate_table_overrides(umacd, table);
    apply_mcs10_mode(umacd, table);
    umac_rc_dump_rate_table(table);
}

void umac_rc_init_rate_table_data(struct umac_sta_data *stad,
                                  struct mmrc_rate_table *table,
                                  bool rts_required,
                                  uint32_t frame_size)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_rc_sta_data *sta_data = umac_sta_data_get_rc(stad);


    if (sta_data->reference_table == NULL)
    {
        MMLOG_WRN("MMRC not started, using management frame parameters\n");
        umac_rc_init_rate_table_mgmt(umacd, table, rts_required);
        return;
    }


    mmrc_get_rates(sta_data->reference_table, table, frame_size);


    if (rts_required)
    {
        int ii;

        for (ii = 0; ii < MMRC_MAX_CHAIN_LENGTH; ii++)
        {
            table->rates[ii].flags |= MMRC_MASK(MMRC_FLAGS_CTS_RTS);
        }
    }

    apply_rate_table_overrides(umacd, table);
    apply_mcs10_mode(umacd, table);
    umac_rc_dump_rate_table(table);
}

void umac_rc_feedback(struct umac_sta_data *stad, struct mmdrv_tx_metadata *tx_metadata)
{
    struct mmrc_rate_table *rate_table = &tx_metadata->rc_data;
    uint8_t attempts_count = tx_metadata->attempts;
    bool frame_acked = (tx_metadata->status_flags & MMDRV_TX_STATUS_FLAG_NO_ACK) == 0;
    bool was_aggregated = (tx_metadata->status_flags & MMDRV_TX_STATUS_WAS_AGGREGATED) != 0;
    struct umac_rc_sta_data *sta_data = umac_sta_data_get_rc(stad);


    if (rate_table->rates[0].attempts == 0)
    {
        return;
    }

    MMLOG_VRB("Feedback (%p): %u attempts, ack %sreceived\n",
              rate_table,
              attempts_count,
              frame_acked ? " " : "not ");
    RC_TRACE("FB %x %x", (uint32_t)rate_table, (ack_received ? 0x8000 : 0) | attempts_count);

    if (attempts_count == 0)
    {

        return;
    }

    if (sta_data->reference_table != NULL)
    {

        mmrc_feedback(sta_data->reference_table, rate_table, attempts_count, was_aggregated);
    }


    if (mmosal_time_has_passed(sta_data->next_update_time_ms))
    {
        umac_rc_update(stad);
    }
}

struct mmwlan_rc_stats *umac_rc_get_rc_stats(struct umac_sta_data *stad)
{
    struct umac_rc_sta_data *sta_data = umac_sta_data_get_rc(stad);

    if (sta_data->reference_table == NULL)
    {
        return NULL;
    }

    struct mmwlan_rc_stats *stats = (struct mmwlan_rc_stats *)mmosal_malloc(sizeof(*stats));
    if (stats == NULL)
    {
        MMLOG_WRN("Failed to alloc buffer for RC stats\n");
        goto cleanup;
    }

    memset(stats, 0, sizeof(*stats));

    stats->n_entries = rows_from_sta_caps(&(sta_data->reference_table->caps));

    stats->rate_info = (uint32_t *)mmosal_malloc(stats->n_entries * sizeof(uint32_t));
    stats->total_sent = (uint32_t *)mmosal_malloc(stats->n_entries * sizeof(uint32_t));
    stats->total_success = (uint32_t *)mmosal_malloc(stats->n_entries * sizeof(uint32_t));

    if (!(stats->rate_info && stats->total_sent && stats->total_success))
    {
        MMLOG_WRN("Failed to alloc buffer for RC stats\n");
        goto cleanup;
    }

    uint8_t i;
    for (i = 0; i < stats->n_entries; i++)
    {
        struct mmrc_rate rate_info = get_rate_row(sta_data->reference_table, i);
        stats->rate_info[i] = (rate_info.bw << MMWLAN_RC_STATS_RATE_INFO_BW_OFFSET) |
                              (rate_info.rate << MMWLAN_RC_STATS_RATE_INFO_RATE_OFFSET) |
                              (rate_info.guard << MMWLAN_RC_STATS_RATE_INFO_GUARD_OFFSET);
        stats->total_sent[i] = sta_data->reference_table->table[i].total_sent;
        stats->total_success[i] = sta_data->reference_table->table[i].total_success;
    }

    return stats;

cleanup:
    umac_rc_free_rc_stats(stats);
    return NULL;
}

void umac_rc_free_rc_stats(struct mmwlan_rc_stats *stats)
{
    if (stats != NULL)
    {
        mmosal_free(stats->rate_info);
        mmosal_free(stats->total_sent);
        mmosal_free(stats->total_success);
        mmosal_free(stats);
    }
}
