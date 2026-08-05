/*
 * Copyright 2024 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "mmhal_wlan.h"
#include "mmosal.h"
#include "mmpkt.h"
#include "mmpkt_list.h"
#include "mmutils.h"

#ifndef MMPKTMEM_TX_POOL_N_BLOCKS
#error MMPKTMEM_TX_POOL_N_BLOCKS not defined
#endif

#ifndef MMPKTMEM_RX_POOL_N_BLOCKS
#error MMPKTMEM_RX_POOL_N_BLOCKS not defined
#endif

/* Packet pool for data/management frames configuration. */
#define MMPKTMEM_TX_DATA_POOL_UNPAUSE_THRESHOLD (2)
#define MMPKTMEM_TX_DATA_POOL_PAUSE_THRESHOLD   (1)

#define MMPKTMEM_TX_COMMAND_POOL_BLOCK_SIZE     (256)
#define MMPKTMEM_TX_COMMAND_POOL_N_BLOCKS       (2)
#define MMPKTMEM_RX_COMMAND_POOL_BLOCK_SIZE     (MMHAL_WLAN_MMPKT_RX_MAX_SIZE)
#define MMPKTMEM_RX_COMMAND_POOL_N_BLOCKS       (2)

#define MMPKTMEM_TX_POOL_BLOCK_SIZE             (MMHAL_WLAN_MMPKT_TX_MAX_SIZE)
#define MMPKTMEM_RX_POOL_BLOCK_SIZE             (MMHAL_WLAN_MMPKT_RX_MAX_SIZE)

#ifndef MMPKT_LOG
#define MMPKT_LOG(...) printf(__VA_ARGS__)
#endif

struct pktmem_data
{
    /** Boolean tracking whether the data path is currently paused. */
    volatile bool tx_data_pool_tx_paused;

    /** TX command pool free (unallocated) packet list. */
    struct mmpkt_list tx_command_pool_free_list;
    /** Statically allocated memory for the TX command pool. */
    uint8_t
        tx_command_pool[MMPKTMEM_TX_COMMAND_POOL_BLOCK_SIZE * MMPKTMEM_TX_COMMAND_POOL_N_BLOCKS];

    /** TX data pool free (unallocated) packet list. */
    struct mmpkt_list tx_data_pool_free_list;
    /** Statically allocated memory for the TX data pool. */
    uint8_t tx_data_pool[MMPKTMEM_TX_POOL_BLOCK_SIZE * MMPKTMEM_TX_POOL_N_BLOCKS];

    /** RX command pool free (unallocated) packet list. */
    struct mmpkt_list rx_command_pool_free_list;
    /** Statically allocated memory for the RX command pool. */
    uint8_t rx_command_pool[MMPKTMEM_RX_POOL_BLOCK_SIZE * MMPKTMEM_RX_COMMAND_POOL_N_BLOCKS];

    /** TX data pool free (unallocated) packet list. */
    struct mmpkt_list rx_data_pool_free_list;
    /** Statically allocated memory for the TX data pool. */
    uint8_t rx_data_pool[MMPKTMEM_RX_POOL_BLOCK_SIZE * MMPKTMEM_RX_POOL_N_BLOCKS];

    /** Flow control callback function pointer. */
    mmhal_wlan_pktmem_tx_flow_control_cb_t tx_flow_control_cb;
};

static struct pktmem_data pktmem;

void mmhal_wlan_pktmem_init(struct mmhal_wlan_pktmem_init_args *args)
{
    unsigned ii;

    memset(&pktmem, 0, sizeof(pktmem));

    pktmem.tx_flow_control_cb = args->tx_flow_control_cb;

    /* Initialize the free (unallocated) packet list of the transmit command pool. */
    for (ii = 0; ii < MMPKTMEM_TX_COMMAND_POOL_N_BLOCKS; ii++)
    {
        size_t offset = MMPKTMEM_TX_COMMAND_POOL_BLOCK_SIZE * ii;
        mmpkt_list_append(&pktmem.tx_command_pool_free_list,
                          (struct mmpkt *)(pktmem.tx_command_pool + offset));
    }

    /* Initialize the free (unallocated) packet list of the transmit data pool. */
    for (ii = 0; ii < MMPKTMEM_TX_POOL_N_BLOCKS; ii++)
    {
        size_t offset = MMPKTMEM_TX_POOL_BLOCK_SIZE * ii;
        mmpkt_list_append(&pktmem.tx_data_pool_free_list,
                          (struct mmpkt *)(pktmem.tx_data_pool + offset));
    }

    /* Initialize the free (unallocated) packet list of the receive command pool. */
    for (ii = 0; ii < MMPKTMEM_RX_COMMAND_POOL_N_BLOCKS; ii++)
    {
        size_t offset = MMPKTMEM_RX_COMMAND_POOL_BLOCK_SIZE * ii;
        mmpkt_list_append(&pktmem.rx_command_pool_free_list,
                          (struct mmpkt *)(pktmem.rx_command_pool + offset));
    }

    /* Initialize the free (unallocated) packet list of the receive pool. */
    for (ii = 0; ii < MMPKTMEM_RX_POOL_N_BLOCKS; ii++)
    {
        size_t offset = MMPKTMEM_RX_POOL_BLOCK_SIZE * ii;
        mmpkt_list_append(&pktmem.rx_data_pool_free_list,
                          (struct mmpkt *)(pktmem.rx_data_pool + offset));
    }
}

void mmhal_wlan_pktmem_deinit(void)
{
    size_t ii;

    /* If there is still memory allocated, allow some time for other threads to clean up. */
    for (ii = 0; ii < 100; ii++)
    {
        if (pktmem.tx_command_pool_free_list.len != MMPKTMEM_TX_COMMAND_POOL_N_BLOCKS ||
            pktmem.tx_data_pool_free_list.len != MMPKTMEM_TX_POOL_N_BLOCKS ||
            pktmem.rx_command_pool_free_list.len != MMPKTMEM_RX_COMMAND_POOL_N_BLOCKS ||
            pktmem.rx_data_pool_free_list.len != MMPKTMEM_RX_POOL_N_BLOCKS)
        {
            break;
        }
        mmosal_task_sleep(10);
    }

    if (pktmem.tx_command_pool_free_list.len != MMPKTMEM_TX_COMMAND_POOL_N_BLOCKS)
    {
        MMPKT_LOG("Potential memory leak: %d %s pool allocations at deinit\n",
                  MMPKTMEM_TX_COMMAND_POOL_N_BLOCKS - (int)pktmem.tx_command_pool_free_list.len,
                  "tx cmd");
    }

    if (pktmem.tx_data_pool_free_list.len != MMPKTMEM_TX_POOL_N_BLOCKS)
    {
        MMPKT_LOG("Potential memory leak: %d %s pool allocations at deinit\n",
                  MMPKTMEM_TX_POOL_N_BLOCKS - (int)pktmem.tx_data_pool_free_list.len,
                  "tx data");
    }

    if (pktmem.rx_command_pool_free_list.len != MMPKTMEM_RX_COMMAND_POOL_N_BLOCKS)
    {
        MMPKT_LOG("Potential memory leak: %d %s pool allocations at deinit\n",
                  MMPKTMEM_RX_COMMAND_POOL_N_BLOCKS - (int)pktmem.rx_command_pool_free_list.len,
                  "rx");
    }

    if (pktmem.rx_data_pool_free_list.len != MMPKTMEM_RX_POOL_N_BLOCKS)
    {
        MMPKT_LOG("Potential memory leak: %d %s pool allocations at deinit\n",
                  MMPKTMEM_RX_POOL_N_BLOCKS - (int)pktmem.rx_data_pool_free_list.len,
                  "rx");
    }
}

/*
 * --------------------------------------------------------------------------------------
 *     Allocation and free functions
 * --------------------------------------------------------------------------------------
 */

static void tx_command_free(void *mmpkt)
{
    struct mmpkt *pkt = (struct mmpkt *)mmpkt;
    MMOSAL_TASK_ENTER_CRITICAL();
    mmpkt_list_append(&pktmem.tx_command_pool_free_list, pkt);
    MMOSAL_TASK_EXIT_CRITICAL();
}

static const struct mmpkt_ops tx_command_pool_ops = {
    .free_mmpkt = tx_command_free,
};

static bool _tx_data_free(struct mmpkt *pkt)
{
    mmpkt_list_append(&pktmem.tx_data_pool_free_list, pkt);
    if (pktmem.tx_data_pool_tx_paused)
    {
        if (pktmem.tx_data_pool_free_list.len >= MMPKTMEM_TX_DATA_POOL_UNPAUSE_THRESHOLD)
        {
            pktmem.tx_data_pool_tx_paused = false;
            return true;
        }
    }

    return false;
}

static void tx_data_free(void *mmpkt)
{
    struct mmpkt *pkt = (struct mmpkt *)mmpkt;
    bool invoke_fc_callback;
    MMOSAL_TASK_ENTER_CRITICAL();
    invoke_fc_callback = _tx_data_free(pkt);
    MMOSAL_TASK_EXIT_CRITICAL();

    if (invoke_fc_callback && pktmem.tx_flow_control_cb)
    {
        pktmem.tx_flow_control_cb();
    }
}

static const struct mmpkt_ops tx_data_pool_ops = {
    .free_mmpkt = tx_data_free,
};

static struct mmpkt *alloc_pkt_from_list(struct mmpkt_list *list,
                                         uint32_t pktbufsize,
                                         const struct mmpkt_ops *ops,
                                         uint32_t space_at_start,
                                         uint32_t space_at_end,
                                         uint32_t metadata_length)
{
    struct mmpkt *mmpkt_buf;
    struct mmpkt *mmpkt;

    MMOSAL_TASK_ENTER_CRITICAL();
    mmpkt_buf = mmpkt_list_dequeue(list);
    MMOSAL_TASK_EXIT_CRITICAL();

    if (mmpkt_buf == NULL)
    {
        return NULL;
    }

    mmpkt = mmpkt_init_buf((uint8_t *)mmpkt_buf,
                           pktbufsize,
                           space_at_start,
                           space_at_end,
                           metadata_length,
                           ops);
    if (mmpkt == NULL)
    {
        mmpkt_list_append(list, mmpkt_buf);
    }

    return mmpkt;
}

static struct mmpkt *tx_command_pool_alloc(uint32_t space_at_start,
                                           uint32_t space_at_end,
                                           uint32_t metadata_length)
{
    return alloc_pkt_from_list(&pktmem.tx_command_pool_free_list,
                               MMPKTMEM_TX_COMMAND_POOL_BLOCK_SIZE,
                               &tx_command_pool_ops,
                               space_at_start,
                               space_at_end,
                               metadata_length);
}

static struct mmpkt *tx_data_pool_alloc(uint32_t space_at_start,
                                        uint32_t space_at_end,
                                        uint32_t metadata_length)
{
    return alloc_pkt_from_list(&pktmem.tx_data_pool_free_list,
                               MMPKTMEM_TX_POOL_BLOCK_SIZE,
                               &tx_data_pool_ops,
                               space_at_start,
                               space_at_end,
                               metadata_length);
}

static bool update_tx_flow_control_state(void)
{
    if (!pktmem.tx_data_pool_tx_paused)
    {
        if (pktmem.tx_data_pool_free_list.len <= MMPKTMEM_TX_DATA_POOL_PAUSE_THRESHOLD)
        {
            pktmem.tx_data_pool_tx_paused = true;
            return true;
        }
    }

    return false;
}

struct mmpkt *mmhal_wlan_alloc_mmpkt_for_tx(uint8_t pkt_class,
                                            uint32_t space_at_start,
                                            uint32_t space_at_end,
                                            uint32_t metadata_length)
{
    bool invoke_fc_callback;
    struct mmpkt *mmpkt;

    /* For command packets, try allocating from the command pool first. If that fails then
     * we proceed to allocate from the data pool. */
    if (pkt_class == MMHAL_WLAN_PKT_COMMAND)
    {
        mmpkt = tx_command_pool_alloc(space_at_start, space_at_end, metadata_length);
        if (mmpkt != NULL)
        {
            return mmpkt;
        }
    }

    mmpkt = tx_data_pool_alloc(space_at_start, space_at_end, metadata_length);

    MMOSAL_TASK_ENTER_CRITICAL();
    invoke_fc_callback = update_tx_flow_control_state();
    MMOSAL_TASK_EXIT_CRITICAL();

    if (invoke_fc_callback && pktmem.tx_flow_control_cb)
    {
        pktmem.tx_flow_control_cb();
    }

    return mmpkt;
}

enum mmwlan_tx_flow_control_state mmhal_wlan_pktmem_tx_flow_control_state(void)
{
    return pktmem.tx_data_pool_tx_paused ? MMWLAN_TX_PAUSED : MMWLAN_TX_READY;
}

static void rx_command_free(void *mmpkt)
{
    struct mmpkt *pkt = (struct mmpkt *)mmpkt;
    MMOSAL_TASK_ENTER_CRITICAL();
    mmpkt_list_append(&pktmem.rx_command_pool_free_list, pkt);
    MMOSAL_TASK_EXIT_CRITICAL();
}

static const struct mmpkt_ops rx_command_pool_ops = {
    .free_mmpkt = rx_command_free,
};

static void rx_data_free(void *mmpkt)
{
    struct mmpkt *pkt = (struct mmpkt *)mmpkt;
    MMOSAL_TASK_ENTER_CRITICAL();
    mmpkt_list_append(&pktmem.rx_data_pool_free_list, pkt);
    MMOSAL_TASK_EXIT_CRITICAL();
}

static const struct mmpkt_ops rx_data_pool_ops = {
    .free_mmpkt = rx_data_free,
};

static struct mmpkt *rx_command_pool_alloc(uint32_t space_at_start,
                                           uint32_t space_at_end,
                                           uint32_t metadata_length)
{
    return alloc_pkt_from_list(&pktmem.rx_command_pool_free_list,
                               MMPKTMEM_RX_COMMAND_POOL_BLOCK_SIZE,
                               &rx_command_pool_ops,
                               space_at_start,
                               space_at_end,
                               metadata_length);
}

static struct mmpkt *rx_data_pool_alloc(uint32_t space_at_start,
                                        uint32_t space_at_end,
                                        uint32_t metadata_length)
{
    return alloc_pkt_from_list(&pktmem.rx_data_pool_free_list,
                               MMPKTMEM_RX_POOL_BLOCK_SIZE,
                               &rx_data_pool_ops,
                               space_at_start,
                               space_at_end,
                               metadata_length);
}

struct mmpkt *mmhal_wlan_alloc_mmpkt_for_rx(uint8_t pkt_class,
                                            uint32_t capacity,
                                            uint32_t metadata_length)
{
    /* For command packets, try allocating from the command pool first. If that fails then
     * we proceed to allocate from the data pool. */
    if (pkt_class == MMHAL_WLAN_PKT_COMMAND)
    {
        struct mmpkt *pkt = rx_command_pool_alloc(0, capacity, metadata_length);
        if (pkt != NULL)
        {
            return pkt;
        }
    }

    return rx_data_pool_alloc(0, capacity, metadata_length);
}
