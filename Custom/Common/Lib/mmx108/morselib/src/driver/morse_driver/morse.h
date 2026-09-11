/*
 * Copyright 2017-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include "hw.h"
#include "mmwlan.h"
#include "mmosal.h"
#include "mmpkt.h"
#include "mmdrv.h"

#define MORSE_DRIVER_SEMVER_MAJOR 56
#define MORSE_DRIVER_SEMVER_MINOR 0
#define MORSE_DRIVER_SEMVER_PATCH 0

#define MORSE_SEMVER_GET_MAJOR(x) ((x >> 22) & 0x3FF)
#define MORSE_SEMVER_GET_MINOR(x) ((x >> 10) & 0xFFF)
#define MORSE_SEMVER_GET_PATCH(x) (x & 0x3FF)


#define MORSE_DEVICE_ID(chip_id, chip_rev, chip_type) \
    ((chip_id) | ((chip_rev) << 8) | ((chip_type) << 12))


#define MORSE_PKT_WORD_ALIGN 4


#define MORSE_YAPS_DELIM_SIZE 4

struct morse_ps
{

    volatile uint32_t wakers;

    bool initialized;

    bool suspended;

    uint32_t bus_ps_timeout;

    uint32_t dynamic_ps_timout_ms;

    struct mmosal_mutex *lock;

    struct mmosal_semb *wake;

    volatile atomic_bool pending_wake;
};

struct morse_stale_tx_status
{
    struct mmosal_timer *timer;
    bool enabled;
};


enum driver_task_event
{
    DRV_EVT_NONE,

    DRV_EVT_RX_PEND,
    DRV_EVT_PAGE_RETURN_PEND,
    DRV_EVT_TX_COMMAND_PEND,
    DRV_EVT_TX_BEACON_PEND,
    DRV_EVT_TX_MGMT_PEND,
    DRV_EVT_TX_DATA_PEND,
    DRV_EVT_TX_PACKET_FREED_UP_PEND,
    DRV_EVT_TRAFFIC_PAUSE_PEND,
    DRV_EVT_TRAFFIC_RESUME_PEND,

    DRV_EVT_PS_ASYNC_WAKEUP_PEND,
    DRV_EVT_PS_DELAYED_EVAL_PEND,
    DRV_EVT_PS_BUS_ACTIVITY_PEND,

    DRV_EVT_STALE_TX_STATUS_PEND,

    DRV_EVT_SHUTDOWN,

    DRV_EVT_BEACON_REQ_PEND,
};


#define DRV_EVT_MASK_PAGESET                   \
    (1ul << DRV_EVT_RX_PEND) |                 \
        (1ul << DRV_EVT_PAGE_RETURN_PEND) |    \
        (1ul << DRV_EVT_TX_COMMAND_PEND) |     \
        (1ul << DRV_EVT_TX_BEACON_PEND) |      \
        (1ul << DRV_EVT_TX_MGMT_PEND) |        \
        (1ul << DRV_EVT_TX_DATA_PEND) |        \
        (1ul << DRV_EVT_TRAFFIC_PAUSE_PEND) |  \
        (1ul << DRV_EVT_TRAFFIC_RESUME_PEND) | \
        (1ul << DRV_EVT_TX_PACKET_FREED_UP_PEND)

struct driver_scheduled_evt
{
    enum driver_task_event evt;
    uint32_t timeout_at_ms;
};


enum morse_state_flags
{

    MORSE_STATE_FLAG_DATA_TX_STOPPED,
};

#define MAX_SCHEDULED_EVTS (4)

struct driver_data
{
    uint32_t chip_id;

    char country[MMWLAN_COUNTRY_CODE_LEN];


    uint32_t firmware_flags;
    struct morse_caps capabilities;

    volatile bool started;


    uint32_t state_flags;


    volatile bool standby_waiting_for_wakeup;


    uint32_t bcf_address;


    struct mmosal_mutex *lock;

    int pageset_consec_failure_cnt;


    struct morse_chip_if_state *chip_if;


    struct morse_stale_tx_status stale_status;


    struct morse_ps ps;

    const struct mmhal_chip *cfg;

    uint32_t host_table_ptr;


    struct
    {

        struct mmosal_mutex *lock;

        struct mmosal_mutex *wait;

        uint16_t seq;

        struct mmosal_semb *semb;


        struct mmpktview *rspview;


        uint16_t pending_cmd_id;


        uint16_t pending_cmd_host_id;
    } cmd;

    struct
    {
        struct mmosal_task *task;
        struct mmosal_semb *pending_semb;
        volatile uint32_t last_checked;
        volatile uint32_t interval_ms;
        volatile bool task_enabled;
        volatile bool task_running;

        volatile atomic_uint_fast32_t periodic_check_vetoes;

        volatile bool check_demanded;
    } health_check;

    struct
    {
        struct mmosal_task *task;
        volatile atomic_uint_least32_t pending_evts;
        struct mmosal_semb *pending_semb;
        volatile bool task_running;
        struct driver_scheduled_evt scheduled_evts[MAX_SCHEDULED_EVTS];
    } driver_task;

    struct
    {
        uint16_t vif_id;
        bool enabled;
        uint32_t count;
        int (*beacon_work_fn)(struct driver_data *driverd);
    } beacon;
};


enum morse_page_aci
{
    MORSE_ACI_BE = 0,
    MORSE_ACI_BK = 1,
    MORSE_ACI_VI = 2,
    MORSE_ACI_VO = 3,
};


enum qos_tid_up_index
{
    MORSE_QOS_TID_UP_BK = 1,
    MORSE_QOS_TID_UP_xx = 2,
    MORSE_QOS_TID_UP_BE = 0,
    MORSE_QOS_TID_UP_EE = 3,
    MORSE_QOS_TID_UP_CL = 4,
    MORSE_QOS_TID_UP_VI = 5,
    MORSE_QOS_TID_UP_VO = 6,
    MORSE_QOS_TID_UP_NC = 7,

    MORSE_QOS_TID_UP_LOWEST = MORSE_QOS_TID_UP_BK,
    MORSE_QOS_TID_UP_HIGHEST = MORSE_QOS_TID_UP_NC
};


static inline enum morse_page_aci dot11_tid_to_ac(uint8_t tid)
{
    MMOSAL_DEV_ASSERT(tid <= MMWLAN_MAX_QOS_TID);
    switch (tid)
    {
        case MORSE_QOS_TID_UP_BK:
        case MORSE_QOS_TID_UP_xx:
            return MORSE_ACI_BK;

        case MORSE_QOS_TID_UP_CL:
        case MORSE_QOS_TID_UP_VI:
            return MORSE_ACI_VI;

        case MORSE_QOS_TID_UP_VO:
        case MORSE_QOS_TID_UP_NC:
            return MORSE_ACI_VO;

        case MORSE_QOS_TID_UP_BE:
        case MORSE_QOS_TID_UP_EE:
        default:
            return MORSE_ACI_BE;
    }
}
