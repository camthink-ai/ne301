/*
 * Copyright 2021-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */

#include <errno.h>
#include <string.h>

#include "mmutils.h"

#include "pager_if_hw.h"

#include "driver/driver.h"
#include "driver/morse_driver/chip_if.h"
#include "driver/transport/morse_transport.h"


#define ENABLE_PAGER_HW_IRQ 1


#define MORSE_PAGER_BITS_BLOCK_LEN  (1)
#define MORSE_PAGER_BITS_BITMAP_LEN (32 - MORSE_PAGER_BITS_BLOCK_LEN)

#define MORSE_PAGER_NUM_BLOCKS      (1ul << MORSE_PAGER_BITS_BLOCK_LEN)

struct morse_pager_cache
{

    uint32_t bitmap[MORSE_PAGER_NUM_BLOCKS];
};

struct morse_pager_hw_aux_data
{

    uint32_t put_addr;
    uint32_t pop_addr;

    struct morse_pager_cache cache;
};


static struct morse_pager_hw_aux_data morse_pager_hw_aux_data[MAX_PAGERS];

int morse_pager_hw_read_table(struct driver_data *driverd, struct morse_pager_hw_table *tbl_ptr)
{
    int ret;
    const uint32_t pager_count_addr = driverd->host_table_ptr +
                                      offsetof(struct host_table, chip_if) +
                                      offsetof(struct morse_chip_if_host_table, pager_count);

    tbl_ptr->addr = driverd->host_table_ptr +
                    offsetof(struct host_table, chip_if) +
                    offsetof(struct morse_chip_if_host_table, pager_table);

    ret = morse_trns_read_le32(driverd, pager_count_addr, &tbl_ptr->count);

    if (ret != 0 && ((tbl_ptr->count == 0) || (tbl_ptr->addr == 0)))
    {
        ret = -EIO;
    }
    return ret;
}

static int morse_pager_hw_get_index_from_page(struct morse_pager *pager,
                                              struct morse_page *page,
                                              uint8_t *index)
{
    struct morse_pager_pkt_memory *pkt_memory = &pager->driverd->chip_if->pkt_memory;

    if (page->addr < pkt_memory->base_addr ||
        page->addr > pkt_memory->base_addr + pkt_memory->page_len * pkt_memory->num)
    {
        return -EINVAL;
    }

    *index =
        (page->addr - pkt_memory->page_len_reserved - pkt_memory->base_addr) / pkt_memory->page_len;

    return 0;
}

static int morse_pager_hw_get_page_from_index(struct morse_pager *pager,
                                              uint8_t index,
                                              struct morse_page *page)
{
    struct morse_pager_pkt_memory *pkt_memory = &pager->driverd->chip_if->pkt_memory;

    if (index >= pkt_memory->num)
    {
        return -EINVAL;
    }

    page->addr =
        pkt_memory->base_addr + (pkt_memory->page_len * index) + pkt_memory->page_len_reserved;
    page->size_bytes = pkt_memory->page_len - pkt_memory->page_len_reserved;

    MMLOG_VRB("PG FROM IDX %u -> %08lx\n", index, page->addr);

    return 0;
}

static void morse_pager_hw_cache_pages(struct morse_pager *pager, struct morse_page *page)
{
    struct morse_pager_hw_aux_data *aux_data = (struct morse_pager_hw_aux_data *)pager->aux_data;
    uint32_t block = page->addr >> MORSE_PAGER_BITS_BITMAP_LEN;

    aux_data->cache.bitmap[block] = page->addr & ~BIT(MORSE_PAGER_BITS_BITMAP_LEN);
}

static int morse_pager_hw_get_page_from_cache(struct morse_pager *pager, struct morse_page *page)
{
    struct morse_pager_hw_aux_data *aux_data = (struct morse_pager_hw_aux_data *)pager->aux_data;
    size_t block;
    int index;

    for (block = 0; block < ARRAY_SIZE(aux_data->cache.bitmap); ++block)
    {
        if (aux_data->cache.bitmap[block])
        {
            break;
        }
    }

    if (block >= ARRAY_SIZE(aux_data->cache.bitmap))
    {
        return -ENOENT;
    }
    index = ffs(aux_data->cache.bitmap[block]);
    MMLOG_VRB("CACHE idx %d\n", index);
    if (!index)
    {
        return -ENOENT;
    }

    index -= 1;

    aux_data->cache.bitmap[block] &= ~BIT(index);

    return morse_pager_hw_get_page_from_index(pager,
                                              (block * MORSE_PAGER_BITS_BITMAP_LEN) + index,
                                              page);
}

static int _morse_pager_hw_pop(struct morse_pager *pager, struct morse_page *page)
{
    struct morse_pager_hw_aux_data *aux_data = (struct morse_pager_hw_aux_data *)pager->aux_data;
    int ret = 0;
    uint32_t pop_val;

    ret = morse_trns_read_le32(pager->driverd, aux_data->pop_addr, &pop_val);
    pop_val = le32toh(pop_val);

    MMLOG_VRB("POPVAL: %08lx\n", pop_val);

    if (!ret)
    {

        if (pop_val == 0)
        {
            return -EAGAIN;
        }

        page->addr = pop_val;
        page->size_bytes = pager->page_size_bytes;
    }

    return ret;
}

static int morse_pager_hw_pop(struct morse_pager *pager, struct morse_page *page)
{
    int ret;

    if (!(pager->flags & MORSE_PAGER_FLAGS_FREE) ||
        !pager->parent->driverd->chip_if->pkt_memory.num)
    {
        return _morse_pager_hw_pop(pager, page);
    }


    ret = morse_pager_hw_get_page_from_cache(pager, page);
    if (!ret)
    {
        return ret;
    }

    ret = _morse_pager_hw_pop(pager, page);
    if (ret)
    {
        return ret;
    }

    morse_pager_hw_cache_pages(pager, page);

    ret = morse_pager_hw_get_page_from_cache(pager, page);

    return ret;
}

static int _morse_pager_hw_put(const struct morse_pager *pager, struct morse_page *page)
{
    struct morse_pager_hw_aux_data *aux_data = (struct morse_pager_hw_aux_data *)pager->aux_data;
    int ret;

    ret = morse_trns_write_le32(pager->driverd, aux_data->put_addr, htole32(page->addr));

    if (!ret)
    {
        page->addr = 0;
        page->size_bytes = 0;
    }
    return ret;
}

static int morse_pager_hw_put(struct morse_pager *pager, struct morse_page *page)
{
    struct morse_pager_hw_aux_data *aux_data = (struct morse_pager_hw_aux_data *)pager->aux_data;
    int ret;
    uint8_t index;
    uint32_t block;

    if (!(pager->flags & MORSE_PAGER_FLAGS_FREE) ||
        !pager->parent->driverd->chip_if->pkt_memory.num)
    {
        return _morse_pager_hw_put(pager, page);
    }


    ret = morse_pager_hw_get_index_from_page(pager, page, &index);
    if (ret)
    {
        return ret;
    }

    block = index / MORSE_PAGER_BITS_BITMAP_LEN;
    aux_data->cache.bitmap[block] |= BIT(index - (block * MORSE_PAGER_BITS_BITMAP_LEN));

    return 0;
}

static void _morse_pager_hw_notify_pager(const struct morse_pager *pager)
{
    struct morse_pager_hw_aux_data *aux_data = (struct morse_pager_hw_aux_data *)pager->aux_data;
    uint32_t block;
    struct morse_page page;

    page.size_bytes = 0;

    for (block = 0; block < ARRAY_SIZE(aux_data->cache.bitmap); ++block)
    {
        if (!aux_data->cache.bitmap[block])
        {
            continue;
        }

        page.addr = (block << MORSE_PAGER_BITS_BITMAP_LEN) | aux_data->cache.bitmap[block];

        _morse_pager_hw_put((const struct morse_pager *)pager, &page);
        aux_data->cache.bitmap[block] = 0;
    }
}

static int morse_pager_hw_notify_pager(const struct morse_pager *pager)
{

    if ((pager->flags & (MORSE_PAGER_FLAGS_DIR_TO_HOST | MORSE_PAGER_FLAGS_FREE)) &&
        pager->parent->driverd->chip_if->pkt_memory.num)
    {
        _morse_pager_hw_notify_pager(pager);
    }

#if ENABLE_PAGER_HW_IRQ

    return 0;
#else

    return morse_trns_write_le32(pager->driverd,
                                 MORSE_PAGER_TRGR_SET(pager->driverd),
                                 MORSE_PAGER_IRQ_MASK(pager->id));
#endif
}

static int morse_pager_hw_page_write(struct morse_pager *pager,
                                     struct morse_page *page,
                                     int offset,
                                     const uint8_t *buf,
                                     uint32_t num_bytes)
{
    int ret;

    if (offset < 0)
    {
        return -EINVAL;
    }

    if (num_bytes > page->size_bytes)
    {
        return -EMSGSIZE;
    }

    if (page->addr == 0)
    {
        return -EFAULT;
    }

    ret = morse_trns_write_multi_byte(pager->driverd, page->addr + offset, buf, num_bytes);
    return ret;
}

static int morse_pager_hw_page_read(struct morse_pager *pager,
                                    struct morse_page *page,
                                    int offset,
                                    uint8_t *buf,
                                    uint32_t num_bytes)
{
    int ret;

    if (offset < 0)
    {
        return -EINVAL;
    }

    if (num_bytes > page->size_bytes)
    {
        return -EMSGSIZE;
    }

    if (page->addr == 0)
    {
        return -EFAULT;
    }

    ret = morse_trns_read_multi_byte(pager->driverd, page->addr + offset, buf, num_bytes);
    return ret;
}

const struct morse_pager_ops morse_pager_hw_ops = { .put = morse_pager_hw_put,
                                                    .pop = morse_pager_hw_pop,
                                                    .write_page = morse_pager_hw_page_write,
                                                    .read_page = morse_pager_hw_page_read,
                                                    .notify = morse_pager_hw_notify_pager };

static int morse_pager_hw_init(struct morse_pager *pager,
                               unsigned pager_num,
                               uint32_t put_addr,
                               uint32_t pop_addr)
{
    struct morse_pager_hw_aux_data *aux_data = &morse_pager_hw_aux_data[pager_num];

    pager->ops = &morse_pager_hw_ops;
    pager->aux_data = aux_data;

    memset(aux_data, 0, sizeof(*aux_data));
    aux_data->put_addr = put_addr;
    aux_data->pop_addr = pop_addr;

    return 0;
}

static void morse_pager_hw_finish(struct morse_pager *pager)
{
    pager->aux_data = NULL;
    pager->ops = NULL;
}

struct morse_chip_if_state chip_if_state;

int morse_pager_hw_pagesets_init(struct driver_data *driverd)
{
    int ret = 0;
    uint32_t i, j;
    struct morse_pager *pager;
    struct morse_pager_hw_table tbl_ptr;
    struct morse_pager_hw_entry pager_entry;
    struct morse_pager *rx_data = NULL;
    struct morse_pager *rx_return = NULL;
    struct morse_pager *tx_data = NULL;
    struct morse_pager *tx_return = NULL;

    morse_trns_claim(driverd);

    ret = morse_pager_hw_read_table(driverd, &tbl_ptr);
    if (ret)
    {
        MMLOG_ERR("morse_pager_hw_read_table failed %d\n", ret);
        goto exit;
    }

    memset(&chip_if_state, 0, sizeof(struct morse_chip_if_state));
    driverd->chip_if = &chip_if_state;

    memset(driverd->chip_if->pagers, 0, sizeof(driverd->chip_if->pagers));

    driverd->chip_if->pager_count = tbl_ptr.count;
    MMLOG_INF("morse pagers detected %lu\n", tbl_ptr.count);
    MMOSAL_ASSERT(tbl_ptr.count <= MAX_PAGERS);


    for (pager = driverd->chip_if->pagers, i = 0; i < tbl_ptr.count; pager++, i++)
    {

        const uint32_t addr = tbl_ptr.addr + i * sizeof(struct morse_pager_hw_entry);

        ret = morse_trns_read_multi_byte(driverd,
                                         addr,
                                         (uint8_t *)&pager_entry,
                                         sizeof(struct morse_pager_hw_entry));
        if (ret)
        {
            MMLOG_ERR("%s failed to read table %d\n", __func__, ret);
            goto err_exit;
        }

        ret = morse_pager_hw_init(pager,
                                  i,
                                  le32toh(pager_entry.push_addr),
                                  le32toh(pager_entry.pop_addr));
        if (ret)
        {
            MMLOG_ERR("morse_pager_hw_init failed %d\n", ret);
            goto err_exit;
        }

        ret =
            morse_pager_init(driverd, pager, le32toh(pager_entry.page_size), pager_entry.flags, i);
        if (ret)
        {
            MMLOG_ERR("morse_pager_init failed %d\n", ret);

            morse_pager_hw_finish(pager);

            i--;
            goto err_exit;
        }
    }


    for (pager = driverd->chip_if->pagers, i = 0; i < tbl_ptr.count; pager++, i++)
    {
        if ((pager->flags & MORSE_PAGER_FLAGS_DIR_TO_HOST) &&
            (pager->flags & MORSE_PAGER_FLAGS_POPULATED))
        {
            rx_data = pager;
        }
        else if ((pager->flags & MORSE_PAGER_FLAGS_DIR_TO_HOST) &&
                 (pager->flags & MORSE_PAGER_FLAGS_FREE))
        {
            rx_return = pager;
        }
        else if ((pager->flags & MORSE_PAGER_FLAGS_DIR_TO_CHIP) &&
                 (pager->flags & MORSE_PAGER_FLAGS_POPULATED))
        {
            tx_data = pager;
        }
        else if ((pager->flags & MORSE_PAGER_FLAGS_DIR_TO_CHIP) &&
                 (pager->flags & MORSE_PAGER_FLAGS_FREE))
        {
            tx_return = pager;

            driver_task_notify_event(driverd, DRV_EVT_PAGE_RETURN_PEND);
        }
        else
        {
            MMLOG_ERR("Invalid pager flags [0x%x]\n", pager->flags);
        }
    }
    if ((rx_data == NULL) || (rx_return == NULL) || (tx_data == NULL) || (tx_return == NULL))
    {
        MMLOG_ERR("Not all required pagers found\n");
        ret = -EFAULT;
        goto err_exit;
    }

    memset(driverd->chip_if->pagesets, 0, sizeof(driverd->chip_if->pagesets));
    driverd->chip_if->pageset_count = 2;
    MMOSAL_ASSERT(driverd->chip_if->pageset_count <= MAX_PAGESETS);

    ret = morse_pageset_init(
        driverd,
        &driverd->chip_if->pagesets[0],
        (MORSE_CHIP_IF_FLAGS_DIR_TO_CHIP | MORSE_CHIP_IF_FLAGS_COMMAND | MORSE_CHIP_IF_FLAGS_DATA),
        tx_data,
        tx_return);

    if (ret)
    {
        goto err_exit;
    }

    ret = morse_pageset_init(
        driverd,
        &driverd->chip_if->pagesets[1],
        (MORSE_CHIP_IF_FLAGS_DIR_TO_HOST | MORSE_CHIP_IF_FLAGS_COMMAND | MORSE_CHIP_IF_FLAGS_DATA),
        rx_data,
        rx_return);

    if (ret)
    {
        morse_pageset_finish(&driverd->chip_if->pagesets[0]);
        goto err_exit;
    }


    driverd->chip_if->to_chip_pageset = &driverd->chip_if->pagesets[0];
    driverd->chip_if->from_chip_pageset = &driverd->chip_if->pagesets[1];
    memset(driverd->chip_if->bypass.tx_status.to_process,
           0,
           sizeof(driverd->chip_if->bypass.tx_status.to_process));
    memset(driverd->chip_if->bypass.cmd_resp.to_process,
           0,
           sizeof(driverd->chip_if->bypass.cmd_resp.to_process));


    morse_trns_release(driverd);


    morse_pager_irq_enable(tx_return, true);
    morse_pager_irq_enable(rx_data, true);
    morse_pager_tx_status_irq_enable(driverd, true);
    morse_pager_cmd_resp_irq_enable(driverd, true);
    morse_hw_irq_enable(driverd, MORSE_INT_HW_STOP_NOTIFICATION_NUM, true);

    return ret;

err_exit:

    for (pager = driverd->chip_if->pagers, j = 0; j < i; pager++, j++)
    {
        morse_pager_finish(pager);
        morse_pager_hw_finish(pager);
    }
    driverd->chip_if = NULL;

exit:
    morse_trns_release(driverd);
    return ret;
}

void morse_pager_hw_pagesets_flush_tx_data(struct driver_data *driverd)
{
    int count;
    struct morse_pageset *pageset;

    for (pageset = driverd->chip_if->pagesets, count = 0; count < driverd->chip_if->pageset_count;
         pageset++, count++)
    {
        if ((pageset->flags & MORSE_CHIP_IF_FLAGS_DIR_TO_CHIP) &&
            (pageset->flags & (MORSE_CHIP_IF_FLAGS_DATA | MORSE_CHIP_IF_FLAGS_BEACON)))
        {
            morse_pageset_flush_tx_data(pageset);
        }
    }
}

void morse_pager_hw_pagesets_finish(struct driver_data *driverd)
{
    int count;
    struct morse_pager *pager;
    struct morse_pageset *pageset;

    for (pageset = driverd->chip_if->pagesets, count = 0; count < driverd->chip_if->pageset_count;
         pageset++, count++)
    {
        morse_pageset_finish(pageset);
    }

    morse_pager_tx_status_irq_enable(driverd, false);
    morse_pager_cmd_resp_irq_enable(driverd, false);
    for (pager = driverd->chip_if->pagers, count = 0; count < driverd->chip_if->pager_count;
         pager++, count++)
    {
        morse_pager_irq_enable(pager, false);
        morse_pager_finish(pager);
        morse_pager_hw_finish(pager);
    }
    driverd->chip_if->pager_count = 0;
    driverd->chip_if->from_chip_pageset = NULL;
    driverd->chip_if->to_chip_pageset = NULL;
    driverd->chip_if = NULL;
}
