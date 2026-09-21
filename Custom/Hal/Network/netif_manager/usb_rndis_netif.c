/**
 * @file    usb_rndis_netif.c
 * @brief   lwIP netif "ue" over CherryUSB host RNDIS (4G modem uplink).
 *
 * Replaces the former usb_ecm_netif.c (USBX CDC-ECM). The modem is used in
 * RNDIS mode (AT+QCFG="usbnet" RNDIS variant); the CherryUSB usbh_rndis
 * class auto-loads on enumeration and calls the usbh_rndis_run/stop hooks
 * implemented here for link state, the class rx thread and frame IO:
 *   RX: usbh_rndis_rx_thread (class) -> usbh_rndis_eth_input() -> pbuf -> netif
 *   TX: lwip output -> tx queue -> usbh_rndis_get_eth_txbuf/eth_output()
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "aicam_error.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/tcpip.h"
#include "lwip/netifapi.h"
#include "Log/debug.h"
#include "mem.h"
#include "usb_cherry.h"
#include "usbh_rndis.h"
#include "chip_id_mac.h"
#include "ms_modem.h"
#include "usb_rndis_netif.h"

// Basic RNDIS link events
#define USB_RNDIS_EVENT_UP                  (1 << 0)
#define USB_RNDIS_EVENT_DOWN                (1 << 1)
#define USB_RNDIS_EVENT_ACTIVATE            (1 << 2)

#define USB_RNDIS_TX_QUEUE_DEPTH            8
#define USB_RNDIS_RX_THREAD_STACK           2048

// RNDIS netif instance
static struct netif rndis_netif = {
    .name = {NETIF_NAME_USB_RNDIS[0], NETIF_NAME_USB_RNDIS[1]},
};

// Stored config (defaults from netif_manager.h)
static netif_config_t usb_rndis_netif_cfg = {
    .ip_mode = NETIF_USB_RNDIS_DEFAULT_IP_MODE,
    .ip_addr = NETIF_USB_RNDIS_DEFAULT_IP,
    .netmask = NETIF_USB_RNDIS_DEFAULT_MASK,
    .gw = NETIF_USB_RNDIS_DEFAULT_GW,
};

#if NETIF_USB_RNDIS_IS_CAT1_MODULE
/// @brief 4G network interface status information (filled over the AT UART
///         by ms_modem at init; layouts of modem_info_t/cellular_info_t match)
static cellular_info_t usb_rndis_cellular_info = {0};
#endif

static osEventFlagsId_t usb_rndis_netif_events = NULL;
static osMutexId_t usb_rndis_netif_mutex = NULL;
static volatile uint32_t usb_rndis_netif_link_flags = 0;
static osMessageQueueId_t usb_rndis_tx_queue = NULL;
static osThreadId_t usb_rndis_tx_thread = NULL;
static volatile uint8_t usb_rndis_tx_run = 0;
static volatile struct usbh_rndis *g_rndis_host_class;

typedef struct {
    uint16_t len;
    uint8_t *buf;
} usb_rndis_tx_item_t;

/* ===================== CherryUSB class hooks ======================== */

/*
 * Called by the class driver when the RNDIS interface is up: remember the
 * instance, adopt its MAC (chip-derived MAC when the device reports zeros)
 * and start the class rx thread.
 */
void usbh_rndis_run(struct usbh_rndis *rndis_class)
{
    g_rndis_host_class = rndis_class;

    uint8_t fallback_mac[6];
    netif_chip_id_get_mac(fallback_mac, NETIF_CHIP_MAC_USB_RNDIS);
    if (rndis_class != NULL) {
        bool mac_zero = true;
        for (int i = 0; i < 6; i++) {
            if (rndis_class->mac[i] != 0x00) {
                mac_zero = false;
                break;
            }
        }
        if (mac_zero) {
            memcpy(rndis_class->mac, fallback_mac, 6);
            printf("ue: device reported all-zero MAC, using chip-derived MAC\r\n");
        }
        memcpy(rndis_netif.hwaddr, rndis_class->mac, 6);
    }

    usb_osal_thread_create("rndis_rx", USB_RNDIS_RX_THREAD_STACK,
                           CONFIG_USBHOST_PSC_PRIO, usbh_rndis_rx_thread, NULL);

    if (usb_rndis_netif_events != NULL) {
        osEventFlagsSet(usb_rndis_netif_events, USB_RNDIS_EVENT_ACTIVATE);
    }
}

/* Class disconnect: stop the TX path immediately; the class rx thread
 * self-deletes once it notices the device is gone. */
void usbh_rndis_stop(struct usbh_rndis *rndis_class)
{
    (void)rndis_class;
    g_rndis_host_class = NULL;
    usb_rndis_netif_link_flags |= USB_RNDIS_EVENT_DOWN;
    usb_rndis_netif_link_flags &= ~USB_RNDIS_EVENT_UP;
    if (usb_rndis_netif_events != NULL) {
        osEventFlagsSet(usb_rndis_netif_events, USB_RNDIS_EVENT_DOWN);
    }
}

/* Class rx thread delivers raw ethernet frames (thread context). */
void usbh_rndis_eth_input(uint8_t *buf, uint32_t buflen)
{
    struct pbuf *p;

    if (buflen < NETIF_LWIP_FRAME_ALIGNMENT) buflen = NETIF_LWIP_FRAME_ALIGNMENT;
    if (buflen > NETIF_MAX_TRANSFER_UNIT + 100) return;

    p = pbuf_alloc(PBUF_RAW, buflen, PBUF_POOL);
    if (p == NULL) {
        return;
    }

    uint32_t off = 0;
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        memcpy((uint8_t *)q->payload, buf + off, q->len);
        off += q->len;
    }

    if (rndis_netif.input != NULL) {
        if (rndis_netif.input(p, &rndis_netif) != ERR_OK) {
            pbuf_free(p);
        }
    } else {
        pbuf_free(p);
    }
}

/* ========================= lwIP glue ================================ */

static void usb_rndis_tx_worker(void *argument)
{
    (void)argument;
    usb_rndis_tx_item_t item = {0};

    while (usb_rndis_tx_run) {
        if (osMessageQueueGet(usb_rndis_tx_queue, &item, NULL, osWaitForever) != osOK) continue;
        if (item.buf == NULL || item.len == 0) continue;

        if ((usb_rndis_netif_link_flags & USB_RNDIS_EVENT_UP) == 0 || g_rndis_host_class == NULL) {
            hal_mem_free(item.buf);
            continue;
        }

        uint8_t *txbuf = usbh_rndis_get_eth_txbuf();
        if (txbuf != NULL && item.len <= CONFIG_USBHOST_RNDIS_ETH_MAX_TX_SIZE) {
            memcpy(txbuf, item.buf, item.len);
            /* Blocks until the bulk URB completes (class-internal semaphore). */
            int ret = usbh_rndis_eth_output(item.len);
            if (ret < 0) {
                LOG_DRV_DEBUG("ue tx error: %d", ret);
            }
        }
        hal_mem_free(item.buf);
    }
}

static err_t usb_rndis_netif_low_level_output(struct netif *netif, struct pbuf *p)
{
    if (netif == NULL || p == NULL) return ERR_ARG;

    /* Do not transmit if link is down/deactivated. */
    if ((usb_rndis_netif_link_flags & USB_RNDIS_EVENT_UP) == 0) return ERR_IF;
    if (usb_rndis_tx_queue == NULL) return ERR_IF;

    uint16_t len = (uint16_t)p->tot_len;
    if (len > CONFIG_USBHOST_RNDIS_ETH_MAX_TX_SIZE) return ERR_MEM;

    uint8_t *buf = hal_mem_alloc(len, MEM_LARGE);
    if (buf == NULL) return ERR_MEM;

    uint16_t off = 0;
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        memcpy(buf + off, (uint8_t *)q->payload, q->len);
        off += (uint16_t)q->len;
    }

    usb_rndis_tx_item_t item = {.len = len, .buf = buf};
    if (osMessageQueuePut(usb_rndis_tx_queue, &item, 0, 0) != osOK) {
        hal_mem_free(buf);
        return ERR_MEM;
    }
    return ERR_OK;
}

static err_t usb_rndis_netif_ethernetif_init(struct netif *netif)
{
    if (netif == NULL) return ERR_ARG;

    netif->hwaddr_len = ETH_HWADDR_LEN;
    if ((netif->hwaddr[0] | netif->hwaddr[1] | netif->hwaddr[2] |
         netif->hwaddr[3] | netif->hwaddr[4] | netif->hwaddr[5]) == 0) {
        netif_chip_id_get_mac(netif->hwaddr, NETIF_CHIP_MAC_USB_RNDIS);
    }
#if LWIP_NETIF_HOSTNAME
    netif->hostname = usb_rndis_netif_cfg.host_name;
#endif
#if LWIP_IPV4 && LWIP_ARP
    netif->output = etharp_output;
#endif
    netif->linkoutput = usb_rndis_netif_low_level_output;

    netif->mtu = NETIF_MAX_TRANSFER_UNIT;
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;

    return ERR_OK;
}

/* Run lwIP netif changes on tcpip thread — never block USB threads on netifapi. */
static void usb_rndis_tcpip_link_up_fn(void *ctx)
{
    (void)ctx;
    netif_set_link_up(&rndis_netif);
    if (usb_rndis_netif_events != NULL) {
        osEventFlagsSet(usb_rndis_netif_events, USB_RNDIS_EVENT_UP);
    }
    usb_rndis_netif_link_flags |= USB_RNDIS_EVENT_UP;
    usb_rndis_netif_link_flags &= ~USB_RNDIS_EVENT_DOWN;
}

static void usb_rndis_tcpip_link_down_fn(void *ctx)
{
    (void)ctx;
    (void)netifapi_dhcp_stop(&rndis_netif);
    netif_set_down(&rndis_netif);
    netif_set_link_down(&rndis_netif);
    if (usb_rndis_netif_events != NULL) {
        osEventFlagsSet(usb_rndis_netif_events, USB_RNDIS_EVENT_DOWN);
    }
}

/* Host stack event: a device appeared on USB2 — the RNDIS class itself
 * reports via usbh_rndis_run/stop; link up happens once the class is
 * connected and media state is up (the class rx thread sets connect_status,
 * then keepalive polls keep it honest). We treat class activation + media
 * up as link up. */
static void usb_rndis_host_event_cb(uint8_t busid, uint8_t hub_index, uint8_t hub_port,
                                     uint8_t intf, uint8_t event)
{
    (void)busid; (void)hub_index; (void)hub_port; (void)intf;
    if (event == USBH_EVENT_INTERFACE_STOP) {
        usb_rndis_netif_link_flags |= USB_RNDIS_EVENT_DOWN;
        usb_rndis_netif_link_flags &= ~USB_RNDIS_EVENT_UP;
        if (tcpip_callback(usb_rndis_tcpip_link_down_fn, NULL) != ERR_OK) {
            LOG_DRV_ERROR("ue: tcpip_callback(link down) failed");
        }
    }
}

/* ===================== init / up / down / deinit ===================== */

int usb_rndis_netif_init(void)
{
    int ret = AICAM_OK;
    struct netif *ue = NULL;
    uint32_t event = 0;
#if NETIF_USB_RNDIS_IS_CAT1_MODULE
    uint8_t try_count = 0;

    /* The modem service (AT over UART) is exclusive: refuse if '4g' or a
     * previous 'ue' session still owns it. */
    if (modem_device_get_state() != MODEM_STATE_UNINIT) return AICAM_ERROR_BUSY;
#endif

    ue = netif_get_by_index(rndis_netif.num + 1);
    if (ue != NULL && ue == &rndis_netif) return -1;
    ue = NULL;

#if NETIF_USB_RNDIS_IS_CAT1_MODULE
    /* Bring up the AT channel, read identity/SIM/signal/config, and make
     * sure the modem is in RNDIS usbnet mode (QCFG persists across boots). */
    do {
        if (ret != 0) {
            modem_device_deinit();
            osDelay(NETIF_4G_CAT1_PPP_INTERVAL_MS);
        }
        ret = modem_device_init();
    } while (ret != 0 && ++try_count < NETIF_4G_CAT1_TRY_CNT);
    if (ret != 0) return ret;

    ret = modem_device_get_info((modem_info_t *)&usb_rndis_cellular_info, 1);
    if (ret != 0) {
        LOG_DRV_ERROR("modem get info failed(ret = %d)!", ret);
        modem_device_deinit();
        return ret;
    }

    ret = modem_device_get_config((modem_config_t *)&usb_rndis_netif_cfg.cellular_cfg);
    if (ret != 0) {
        LOG_DRV_ERROR("modem get config failed(ret = %d)!", ret);
        modem_device_deinit();
        return ret;
    }

    ret = modem_device_check_and_enable_rndis();
    if (ret != 0) {
        LOG_DRV_ERROR("modem check and enable rndis failed(ret = %d)!", ret);
        modem_device_deinit();
        return ret;
    }
    ret = AICAM_OK;
#endif

    usb_rndis_netif_events = osEventFlagsNew(NULL);
    if (usb_rndis_netif_events == NULL) {
        ret = AICAM_ERROR_NO_MEMORY;
        goto usb_rndis_netif_init_exit;
    }
    usb_rndis_netif_mutex = osMutexNew(NULL);
    if (usb_rndis_netif_mutex == NULL) {
        ret = AICAM_ERROR_NO_MEMORY;
        goto usb_rndis_netif_init_exit;
    }

    usb_rndis_tx_queue = osMessageQueueNew(USB_RNDIS_TX_QUEUE_DEPTH, sizeof(usb_rndis_tx_item_t), NULL);
    if (usb_rndis_tx_queue == NULL) {
        ret = AICAM_ERROR_NO_MEMORY;
        goto usb_rndis_netif_init_exit;
    }
    usb_rndis_tx_run = 1;
    /* named attrs: an attribute-less osThreadNew gets a generated default
     * name (shows up as garbage/"0 N" rows in system tools) */
    static const osThreadAttr_t ue_tx_thread_attr = {
        .name = "ue_tx",
        .priority = (osPriority_t) osPriorityNormal,
        .stack_size = 2048,
    };
    usb_rndis_tx_thread = osThreadNew(usb_rndis_tx_worker, NULL, &ue_tx_thread_attr);
    if (usb_rndis_tx_thread == NULL) {
        usb_rndis_tx_run = 0;
        ret = AICAM_ERROR_NO_MEMORY;
        goto usb_rndis_netif_init_exit;
    }

    // Register lwIP netif (run on tcpip thread)
    err_t nif_err = netifapi_netif_add(&rndis_netif, NULL, NULL, NULL, NULL, &usb_rndis_netif_ethernetif_init, &tcpip_input);
    if (nif_err != ERR_OK) {
        ret = AICAM_ERROR;
        goto usb_rndis_netif_init_exit;
    }

    osEventFlagsClear(usb_rndis_netif_events, USB_RNDIS_EVENT_ACTIVATE);

    /* Starts the CherryUSB host stack on USB2 (idempotent). The modem
     * enumerates and the usbh_rndis class fires usbh_rndis_run(). */
    ret = usb_cherry_host_init(usb_rndis_host_event_cb);
    if (ret != 0) {
        ret = AICAM_ERROR;
        goto usb_rndis_netif_init_exit;
    }

    // Wait for the class to come up (device may already be plugged or
    // appear later; hotplug after init re-fires ACTIVATE).
    event = osEventFlagsWait(usb_rndis_netif_events, USB_RNDIS_EVENT_ACTIVATE, osFlagsWaitAny, NETIF_USB_RNDIS_ACTIVATE_TIMEOUT_MS);
    if (event & osFlagsError) {
        ret = AICAM_ERROR_TIMEOUT;
        goto usb_rndis_netif_init_exit;
    }

    /* Class connected: wait briefly for media up, then raise the netif. */
    for (uint32_t waited = 0; waited < NETIF_USB_RNDIS_UP_TIMEOUT_MS; waited += 128) {
        if (g_rndis_host_class != NULL && usbh_rndis_get_connect_status((struct usbh_rndis *)g_rndis_host_class) == 1) {
            break;
        }
        osDelay(128);
    }
    if (tcpip_callback(usb_rndis_tcpip_link_up_fn, NULL) != ERR_OK) {
        ret = AICAM_ERROR;
        goto usb_rndis_netif_init_exit;
    }

usb_rndis_netif_init_exit:
    if (ret != AICAM_OK) {
        usb_cherry_host_deinit();
        (void)netifapi_netif_remove(&rndis_netif);
#if NETIF_USB_RNDIS_IS_CAT1_MODULE
        modem_device_deinit();
#endif
        if (usb_rndis_netif_events != NULL) {
            osEventFlagsDelete(usb_rndis_netif_events);
            usb_rndis_netif_events = NULL;
        }
        if (usb_rndis_netif_mutex != NULL) {
            osMutexDelete(usb_rndis_netif_mutex);
            usb_rndis_netif_mutex = NULL;
        }
        if (usb_rndis_tx_thread != NULL) {
            (void)osThreadTerminate(usb_rndis_tx_thread);
            usb_rndis_tx_thread = NULL;
        }
        usb_rndis_tx_run = 0;
        if (usb_rndis_tx_queue != NULL) {
            usb_rndis_tx_item_t item = {0};
            while (osMessageQueueGet(usb_rndis_tx_queue, &item, NULL, 0) == osOK) {
                if (item.buf != NULL) hal_mem_free(item.buf);
            }
            osMessageQueueDelete(usb_rndis_tx_queue);
            usb_rndis_tx_queue = NULL;
        }
        g_rndis_host_class = NULL;
    }
    return ret;
}

int usb_rndis_netif_up(void)
{
    struct netif *ue = NULL;
    int ret = 0;
    ip_addr_t ipaddr  = { 0 };
    ip_addr_t gateway = { 0 };
    ip_addr_t netmask = { 0 };
    uint32_t event = 0;

    ue = netif_get_by_index(rndis_netif.num + 1);
    if (ue == NULL || ue != &rndis_netif) return AICAM_ERROR_NOT_SUPPORTED;

    if (!netif_is_link_up(&rndis_netif)) {
        osEventFlagsClear(usb_rndis_netif_events, USB_RNDIS_EVENT_UP);
        event = osEventFlagsWait(usb_rndis_netif_events, USB_RNDIS_EVENT_UP, osFlagsWaitAny, NETIF_USB_RNDIS_UP_TIMEOUT_MS);
        if ((event & osFlagsError) && !netif_is_link_up(&rndis_netif)) return AICAM_ERROR_TIMEOUT;
    }

    /* RNDIS link flap window: if the device re-enumerates during this period,
     * bail out and let the caller retry. */
    osEventFlagsClear(usb_rndis_netif_events, USB_RNDIS_EVENT_UP | USB_RNDIS_EVENT_DOWN);
    osDelay(NETIF_USB_RNDIS_STABLE_TIME_MS);
    if ((osEventFlagsGet(usb_rndis_netif_events) & USB_RNDIS_EVENT_DOWN) != 0) {
        return AICAM_ERROR;
    }

    IP4_ADDR(&ipaddr, usb_rndis_netif_cfg.ip_addr[0], usb_rndis_netif_cfg.ip_addr[1], usb_rndis_netif_cfg.ip_addr[2], usb_rndis_netif_cfg.ip_addr[3]);
    IP4_ADDR(&gateway, usb_rndis_netif_cfg.gw[0], usb_rndis_netif_cfg.gw[1], usb_rndis_netif_cfg.gw[2], usb_rndis_netif_cfg.gw[3]);
    IP4_ADDR(&netmask, usb_rndis_netif_cfg.netmask[0], usb_rndis_netif_cfg.netmask[1], usb_rndis_netif_cfg.netmask[2], usb_rndis_netif_cfg.netmask[3]);
    netifapi_netif_set_addr(&rndis_netif, &ipaddr, &netmask, &gateway);
    ret = netifapi_netif_set_up(&rndis_netif);
    if (ret != ERR_OK) return AICAM_ERROR;

    if (usb_rndis_netif_cfg.ip_mode == NETIF_IP_MODE_DHCP) {
        ip_addr_set_zero_ip4(&(rndis_netif.ip_addr));
        ip_addr_set_zero_ip4(&(rndis_netif.netmask));
        ip_addr_set_zero_ip4(&(rndis_netif.gw));
        ret = netifapi_dhcp_start(&rndis_netif);
        if (ret != ERR_OK){
            netifapi_netif_set_down(&rndis_netif);
            return AICAM_ERROR;
        }
        uint32_t start_tick = HAL_GetTick();
        uint32_t diff_tick;
        do {
            event = osEventFlagsWait(usb_rndis_netif_events, USB_RNDIS_EVENT_DOWN, osFlagsWaitAny, 100);
            if (!(event & osFlagsError)) {
                (void)netifapi_dhcp_stop(&rndis_netif);
                netifapi_netif_set_down(&rndis_netif);
                return AICAM_ERROR;
            }
            if (dhcp_supplied_address(&rndis_netif)) {
                LOG_DRV_INFO(NETIF_NAME_STR_FMT " dhcp ip: %s", NETIF_NAME_PARAMETER(&rndis_netif), ip4addr_ntoa((const ip4_addr_t *)&rndis_netif.ip_addr));
                break;
            }
            uint32_t end_tick = HAL_GetTick();
            diff_tick = (end_tick >= start_tick) ? (end_tick - start_tick) : (0xFFFFFFFFU - start_tick + end_tick);
        } while (diff_tick < NETIF_USB_RNDIS_DHCP_TIMEOUT_MS);
        if (!dhcp_supplied_address(&rndis_netif)) {
            (void)netifapi_dhcp_stop(&rndis_netif);
            netifapi_netif_set_down(&rndis_netif);
            return AICAM_ERROR_TIMEOUT;
        }
        usb_rndis_netif_cfg.ip_addr[0] = ip4_addr1(&rndis_netif.ip_addr);
        usb_rndis_netif_cfg.ip_addr[1] = ip4_addr2(&rndis_netif.ip_addr);
        usb_rndis_netif_cfg.ip_addr[2] = ip4_addr3(&rndis_netif.ip_addr);
        usb_rndis_netif_cfg.ip_addr[3] = ip4_addr4(&rndis_netif.ip_addr);
        usb_rndis_netif_cfg.gw[0] = ip4_addr1(&rndis_netif.gw);
        usb_rndis_netif_cfg.gw[1] = ip4_addr2(&rndis_netif.gw);
        usb_rndis_netif_cfg.gw[2] = ip4_addr3(&rndis_netif.gw);
        usb_rndis_netif_cfg.gw[3] = ip4_addr4(&rndis_netif.gw);
        usb_rndis_netif_cfg.netmask[0] = ip4_addr1(&rndis_netif.netmask);
        usb_rndis_netif_cfg.netmask[1] = ip4_addr2(&rndis_netif.netmask);
        usb_rndis_netif_cfg.netmask[2] = ip4_addr3(&rndis_netif.netmask);
        usb_rndis_netif_cfg.netmask[3] = ip4_addr4(&rndis_netif.netmask);
    }
    return AICAM_OK;
}

int usb_rndis_netif_down(void)
{
    struct netif *ue = NULL;

    ue = netif_get_by_index(rndis_netif.num + 1);
    if (ue == NULL || ue != &rndis_netif) return AICAM_ERROR_NOT_SUPPORTED;

    (void)netifapi_dhcp_stop(&rndis_netif);
    netifapi_netif_set_down(&rndis_netif);
    return AICAM_OK;
}

void usb_rndis_netif_deinit(void)
{
    struct netif *ue = NULL;

    ue = netif_get_by_index(rndis_netif.num + 1);
    if (ue == NULL || ue != &rndis_netif) return;

    (void)netifapi_dhcp_stop(&rndis_netif);
    netifapi_netif_set_down(&rndis_netif);
    netifapi_netif_set_link_down(&rndis_netif);
    (void)netifapi_netif_remove(&rndis_netif);

    usb_cherry_host_deinit();
    g_rndis_host_class = NULL;
#if NETIF_USB_RNDIS_IS_CAT1_MODULE
    modem_device_deinit();
#endif

    if (usb_rndis_tx_thread != NULL) {
        usb_rndis_tx_run = 0;
        (void)osThreadTerminate(usb_rndis_tx_thread);
        usb_rndis_tx_thread = NULL;
    }
    if (usb_rndis_tx_queue != NULL) {
        usb_rndis_tx_item_t item = {0};
        while (osMessageQueueGet(usb_rndis_tx_queue, &item, NULL, 0) == osOK) {
            if (item.buf != NULL) hal_mem_free(item.buf);
        }
        osMessageQueueDelete(usb_rndis_tx_queue);
        usb_rndis_tx_queue = NULL;
    }

    if (usb_rndis_netif_events != NULL) {
        osEventFlagsDelete(usb_rndis_netif_events);
        usb_rndis_netif_events = NULL;
    }
    if (usb_rndis_netif_mutex != NULL) {
        osMutexDelete(usb_rndis_netif_mutex);
        usb_rndis_netif_mutex = NULL;
    }
}

int usb_rndis_netif_config(netif_config_t *netif_cfg)
{
    if (netif_cfg == NULL) return AICAM_ERROR_INVALID_PARAM;
    if (usb_rndis_netif_state() != NETIF_STATE_DOWN) return AICAM_ERROR_BUSY;
    if (netif_cfg->ip_mode == NETIF_IP_MODE_DHCP && netif_cfg->ip_mode != usb_rndis_netif_cfg.ip_mode) {
        usb_rndis_netif_cfg.ip_mode = netif_cfg->ip_mode;
    } else if (netif_cfg->ip_mode != NETIF_IP_MODE_DHCP) {
        usb_rndis_netif_cfg.ip_mode = netif_cfg->ip_mode;
        memcpy(usb_rndis_netif_cfg.ip_addr, netif_cfg->ip_addr, 4);
        memcpy(usb_rndis_netif_cfg.netmask, netif_cfg->netmask, 4);
        memcpy(usb_rndis_netif_cfg.gw, netif_cfg->gw, 4);
    }
    return AICAM_OK;
}

int usb_rndis_netif_info(netif_info_t *netif_info)
{
    if (netif_info == NULL) return AICAM_ERROR_INVALID_PARAM;
#if LWIP_NETIF_HOSTNAME
    netif_info->host_name = rndis_netif.hostname;
#else
    netif_info->host_name = NULL;
#endif
    netif_info->if_name = NETIF_NAME_USB_RNDIS;
    netif_info->type = NETIF_TYPE_4G;
    netif_info->state = usb_rndis_netif_state();
    netif_info->rssi = 0;
    netif_info->ip_mode = usb_rndis_netif_cfg.ip_mode;
    /* the info display prints fw_version unconditionally — always fill it */
#if NETIF_USB_RNDIS_IS_CAT1_MODULE
    snprintf(netif_info->fw_version, sizeof(netif_info->fw_version), "%s",
             usb_rndis_cellular_info.version);
#else
    snprintf(netif_info->fw_version, sizeof(netif_info->fw_version), "-");
#endif
    memcpy(netif_info->if_mac, rndis_netif.hwaddr, sizeof(netif_info->if_mac));
    memcpy(netif_info->ip_addr, &rndis_netif.ip_addr, sizeof(netif_info->ip_addr));
    memcpy(netif_info->gw, &rndis_netif.gw, sizeof(netif_info->gw));
    memcpy(netif_info->netmask, &rndis_netif.netmask, sizeof(netif_info->netmask));
#if NETIF_USB_RNDIS_IS_CAT1_MODULE
    memcpy(&netif_info->cellular_info, &usb_rndis_cellular_info, sizeof(cellular_info_t));
    memcpy(&netif_info->cellular_cfg, &usb_rndis_netif_cfg.cellular_cfg, sizeof(cellular_config_t));
    netif_info->rssi = usb_rndis_cellular_info.rssi;
#endif
    return AICAM_OK;
}

netif_state_t usb_rndis_netif_state(void)
{
    if (netif_get_by_index(rndis_netif.num + 1) != &rndis_netif) return NETIF_STATE_DEINIT;
    if (!netif_is_up(&rndis_netif)) return NETIF_STATE_DOWN;
    if (!netif_is_link_up(&rndis_netif)) return NETIF_STATE_DOWN;
    return NETIF_STATE_UP;
}

struct netif *usb_rndis_netif_ptr(void)
{
    if (netif_get_by_index(rndis_netif.num + 1) != &rndis_netif) return NULL;
    return &rndis_netif;
}

int usb_rndis_netif_ctrl(const char *if_name, netif_cmd_t cmd, void *param)
{
    int ret = AICAM_ERROR;
    netif_state_t if_state = NETIF_STATE_DEINIT;
    if (usb_rndis_netif_mutex == NULL) usb_rndis_netif_mutex = osMutexNew(NULL);
    if (usb_rndis_netif_mutex == NULL) return AICAM_ERROR_NO_MEMORY;

    osMutexAcquire(usb_rndis_netif_mutex, osWaitForever);
    switch (cmd) {
        case NETIF_CMD_CFG:
            ret = usb_rndis_netif_config((netif_config_t *)param);
            break;
        case NETIF_CMD_INIT:
            ret = usb_rndis_netif_init();
            break;
        case NETIF_CMD_UP:
            ret = usb_rndis_netif_up();
            break;
        case NETIF_CMD_INFO:
            ret = usb_rndis_netif_info((netif_info_t *)param);
            break;
        case NETIF_CMD_STATE:
            if (param == NULL) ret = AICAM_ERROR_INVALID_PARAM;
            else {
                *(netif_state_t *)param = usb_rndis_netif_state();
                ret = AICAM_OK;
            }
            break;
        case NETIF_CMD_DOWN:
            ret = usb_rndis_netif_down();
            break;
        case NETIF_CMD_UNINIT:
            usb_rndis_netif_deinit();
            ret = AICAM_OK;
            break;
        case NETIF_CMD_CFG_EX:
            if_state = usb_rndis_netif_state();
            if (if_state == NETIF_STATE_UP) {
                ret = usb_rndis_netif_down();
                if (ret) break;
            }
            ret = usb_rndis_netif_config((netif_config_t *)param);
            if (ret) break;
            if (if_state == NETIF_STATE_UP) ret = usb_rndis_netif_up();
            break;
        default:
            ret = AICAM_ERROR_INVALID_PARAM;
            break;
    }
    osMutexRelease(usb_rndis_netif_mutex);
    return ret;
}
