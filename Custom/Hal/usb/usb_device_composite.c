/**
 * @file    usb_device_composite.c
 * @brief   CherryUSB composite device on USB1_OTG_HS (J21 Type-C):
 *          - RNDIS network adapter (lwip netif + DHCP server for the PC)
 *          - CDC-ACM virtual COM redirected to the debug log/console
 *
 * Role note (vs the host side): here the NE301 is the USB *device*. The PC
 * is the USB host: it sees one RNDIS NIC (shares the NE301's network via
 * lwip IP forwarding — lwipopts has IP_FORWARD=1) plus one virtual COM port
 * carrying the same log/console as the USART2 debug UART.
 *
 * Start/stop from the debug CLI: `usb device <init|deinit|status>`.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "cmsis_os2.h"
#include "stm32n6xx_hal.h"
#include "usb_cherry.h"
#include "usbd_core.h"
#include "usbd_rndis.h"
#include "usbd_cdc_acm.h"
#include "lwip/tcpip.h"
#include "lwip/netifapi.h"
#include "lwip/etharp.h"
#include "lwip/udp.h"
#include "lwip/timeouts.h"
#include "chip_id_mac.h"
#include "Log/debug.h"

/*!< RNDIS function endpoints */
#define RNDIS_IN_EP  0x81
#define RNDIS_OUT_EP 0x02
#define RNDIS_INT_EP 0x83

/*!< CDC-ACM (console) function endpoints */
#define CDC_IN_EP  0x85
#define CDC_OUT_EP 0x04
#define CDC_INT_EP 0x86

#define USBD_VID           0x0483
#define USBD_PID           0x5740
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

/*!< config descriptor size */
#define USB_CONFIG_SIZE (9 + CDC_RNDIS_DESCRIPTOR_LEN + CDC_ACM_DESCRIPTOR_LEN)

#ifdef CONFIG_USB_HS
#define CDC_MAX_MPS 512
#else
#define CDC_MAX_MPS 64
#endif

/* PC-facing subnet served by the RNDIS function (device = .1, DHCP pool). */

/* ==================== mini DHCP server (PC side) ====================
 *
 * Point-to-point link semantics: exactly one PC client. The shared
 * dhcpserver.c is a whitelist design for the AP path (station MACs are
 * registered at WiFi association; see handle_dhcp) and silently drops
 * unknown RNDIS MACs, so the composite device carries its own minimal
 * RFC2131 server: DISCOVER->OFFER, REQUEST->ACK/NAK, always broadcast.
 * ========================================================================= */

#define UDHCP_SERVER_PORT      67
#define UDHCP_CLIENT_PORT      68
#define UDHCP_CLIENT_IP_3RD    20       /* 192.168.20.x */
#define UDHCP_CLIENT_IP_4TH    100      /* single client: always .100 */
#define UDHCP_LEASE_TIME_S     86400

/* defined below (netif section) */
static struct netif rndis_dev_netif;

#define DHCP_MSG_FIXED_LEN     236
#define DHCP_MAGIC_COOKIE      0x63538263ul

#define DHCPDISCOVER           1
#define DHCPOFFER              2
#define DHCPREQUEST            3
#define DHCPDECLINE            4
#define DHCPACK                5
#define DHCPNAK                6
#define DHCPRELEASE            7
#define DHCPINFORM             8

static struct udp_pcb *g_udhcp_pcb;
static osSemaphoreId_t g_rndis_tx_sem;
static osSemaphoreId_t g_cdc_console_sem; /* worker wait object (console + rndis rx) */
/* fixed header of a bootp/dhcp message (236 bytes before the cookie) */
struct udhcp_msg {
    uint8_t op, htype, hlen, hops;
    uint8_t xid[4];
    uint16_t secs, flags;
    uint8_t ciaddr[4], yiaddr[4], siaddr[4], giaddr[4];
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
};

/* Parse one DHCP option; returns length and sets *value on hit. */
static const uint8_t *udhcp_find_opt(const uint8_t *opts, uint16_t max, uint8_t opt_id, uint8_t *out_len)
{
    uint16_t i = 0;
    while (i + 1 < max) {
        uint8_t code = opts[i];
        if (code == 0) { i++; continue; }        /* pad */
        if (code == 255) break;                  /* end */
        uint8_t len = opts[i + 1];
        if (i + 2 + len > max) break;
        if (code == opt_id) {
            *out_len = len;
            return &opts[i + 2];
        }
        i = (uint16_t)(i + 2 + len);
    }
    return NULL;
}

static void udhcp_send_reply(struct udhcp_msg *req, uint8_t msg_type,
                             const uint8_t yiaddr[4], bool include_addr)
{
    /* reply on tcpip thread (udp recv context): reuse the request xid/chaddr */
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, DHCP_MSG_FIXED_LEN + 64, PBUF_RAM);
    if (p == NULL) return;

    uint8_t *out = (uint8_t *)p->payload;
    memset(out, 0, p->len);
    struct udhcp_msg *rsp = (struct udhcp_msg *)out;
    rsp->op = 2;                     /* BOOTREPLY */
    rsp->htype = req->htype;
    rsp->hlen = req->hlen;
    memcpy(rsp->xid, req->xid, 4);
    rsp->flags = req->flags;
    if (include_addr) memcpy(rsp->yiaddr, yiaddr, 4);
    /* siaddr: next-server — mirror the AP dhcpserver (some clients check it) */
    rsp->siaddr[0] = 192; rsp->siaddr[1] = 168;
    rsp->siaddr[2] = UDHCP_CLIENT_IP_3RD; rsp->siaddr[3] = 1;
    memcpy(rsp->chaddr, req->chaddr, 16);

    uint16_t o = DHCP_MSG_FIXED_LEN;
    out[o++] = 99; out[o++] = 130; out[o++] = 83; out[o++] = 99; /* cookie */
    /* 53: message type */
    out[o++] = 53; out[o++] = 1; out[o++] = msg_type;
    /* 54: server identifier */
    out[o++] = 54; out[o++] = 4;
    out[o++] = 192; out[o++] = 168; out[o++] = UDHCP_CLIENT_IP_3RD; out[o++] = 1;
    if (include_addr) {
        /* 51: lease time */
        out[o++] = 51; out[o++] = 4;
        out[o++] = (uint8_t)(UDHCP_LEASE_TIME_S >> 24); out[o++] = (uint8_t)(UDHCP_LEASE_TIME_S >> 16);
        out[o++] = (uint8_t)(UDHCP_LEASE_TIME_S >> 8);  out[o++] = (uint8_t)(UDHCP_LEASE_TIME_S);
        /* 1: subnet mask */
        out[o++] = 1; out[o++] = 4; out[o++] = 255; out[o++] = 255; out[o++] = 255; out[o++] = 0;
        /* 3: router */
        out[o++] = 3; out[o++] = 4;
        out[o++] = 192; out[o++] = 168; out[o++] = UDHCP_CLIENT_IP_3RD; out[o++] = 1;
        /* 6: dns (the device itself) */
        out[o++] = 6; out[o++] = 4;
        out[o++] = 192; out[o++] = 168; out[o++] = UDHCP_CLIENT_IP_3RD; out[o++] = 1;
    }
    out[o++] = 255; /* end */

    /* RFC 2131: a BOOTP message is at least 300 bytes. Windows silently
     * drops shorter replies — zero-pad (option 0 = PAD) up to the floor,
     * exactly like the AP-side dhcpserver does (see its send_packet). */
    uint16_t send_len = (o < 300) ? 300 : o;
    p->len = p->tot_len = send_len;

    ip_addr_t dst;
    IP4_ADDR(&dst, 255, 255, 255, 255);
    err_t send_err = udp_sendto(g_udhcp_pcb, p, &dst, UDHCP_CLIENT_PORT);
    if (send_err != ERR_OK) {
        printf("[USB-DEV] dhcp tx type=%u err=%d\r\n", msg_type, (int)send_err);
    }
    pbuf_free(p);
}

static void udhcp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                       const ip_addr_t *addr, uint16_t port)
{
    (void)arg; (void)pcb; (void)addr; (void)port;

    static const uint8_t client_ip[4] = {192, 168, UDHCP_CLIENT_IP_3RD, UDHCP_CLIENT_IP_4TH};

    if (p == NULL || p->len < DHCP_MSG_FIXED_LEN + 4) {
        if (p) pbuf_free(p);
        return;
    }

    struct udhcp_msg *req = (struct udhcp_msg *)p->payload;
    uint16_t opts_len = (uint16_t)(p->len - DHCP_MSG_FIXED_LEN - 4);
    const uint8_t *opts = p->payload + DHCP_MSG_FIXED_LEN + 4;

    uint8_t olen = 0;
    const uint8_t *v53 = udhcp_find_opt(opts, opts_len, 53, &olen);
    uint8_t msg_type = (v53 && olen >= 1) ? v53[0] : 0;
    const uint8_t *v50 = udhcp_find_opt(opts, opts_len, 50, &olen);
    const uint8_t *v54 = udhcp_find_opt(opts, opts_len, 54, &olen);

    switch (msg_type) {
        case DHCPDISCOVER:
            udhcp_send_reply(req, DHCPOFFER, client_ip, true);
            break;
        case DHCPREQUEST: {
            /* SELECTING names a server (54): must be us. RENEWING has no 54. */
            bool server_match = true;
            if (v54 && olen == 4) {
                server_match = (v54[0] == 192 && v54[1] == 168 &&
                                v54[2] == UDHCP_CLIENT_IP_3RD && v54[3] == 1);
            }
            if (!server_match) {
                break; /* another server's client */
            }
            /* requested address (option 50) or ciaddr must be ours */
            bool addr_ok = false;
            if (v50 && olen == 4) {
                addr_ok = (memcmp(v50, client_ip, 4) == 0);
            } else if (req->ciaddr[0] | req->ciaddr[1] | req->ciaddr[2] | req->ciaddr[3]) {
                addr_ok = (memcmp(req->ciaddr, client_ip, 4) == 0);
            }
            if (addr_ok) {
                udhcp_send_reply(req, DHCPACK, client_ip, true);
            } else {
                udhcp_send_reply(req, DHCPNAK, NULL, false);
            }
            break;
        }
        case DHCPINFORM:
            udhcp_send_reply(req, DHCPACK, NULL, false);
            break;
        default:
            break; /* RELEASE / DECLINE: nothing to free (single slot) */
    }

    pbuf_free(p);
}

static void udhcp_start(void)
{
    if (g_udhcp_pcb != NULL) return;

    g_udhcp_pcb = udp_new();
    if (g_udhcp_pcb == NULL) {
        printf("[USB-DEV] mini-dhcps: udp_new failed\r\n");
        return;
    }
    ip_set_option(g_udhcp_pcb, SOF_BROADCAST);
    /* Bind to THIS netif's address (not ANY): avoids clashing with another
     * port-67 server (the AP's dhcpserver binds its own netif IP), and for
     * 255.255.255.255 datagrams lwip prefers the pcb whose local_ip matches
     * the receiving netif + pcb->netif_idx, so both servers can coexist. */
    g_udhcp_pcb->netif_idx = (u8_t)(rndis_dev_netif.num + 1);
    if (udp_bind(g_udhcp_pcb, &rndis_dev_netif.ip_addr, UDHCP_SERVER_PORT) != ERR_OK) {
        printf("[USB-DEV] mini-dhcps: bind failed\r\n");
        udp_remove(g_udhcp_pcb);
        g_udhcp_pcb = NULL;
        return;
    }
    udp_recv(g_udhcp_pcb, udhcp_recv, NULL);
    printf("[USB-DEV] mini-dhcps: serving 192.168.%u.%u\r\n",
           UDHCP_CLIENT_IP_3RD, UDHCP_CLIENT_IP_4TH);
}

static void udhcp_stop(void)
{
    if (g_udhcp_pcb != NULL) {
        udp_disconnect(g_udhcp_pcb);
        udp_remove(g_udhcp_pcb);
        g_udhcp_pcb = NULL;
    }
}

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

/* Composite configuration: RNDIS (IAD, interfaces 0-1) + CDC-ACM (2-3) */
static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_RNDIS_DESCRIPTOR_INIT(0x00, RNDIS_INT_EP, RNDIS_OUT_EP, RNDIS_IN_EP, CDC_MAX_MPS, 0x02),
    CDC_ACM_DESCRIPTOR_INIT(0x02, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x04)
};

static const uint8_t device_quality_descriptor[] = {
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 }, /* Langid,    index 0 */
    "NE301",                      /* Manufacturer, 1 */
    "NE301 USB Device",           /* Product,       2 (also RNDIS ifaces) */
    "NE301CDC0001",               /* Serial Number, 3 */
    "NE301 CDC Console",          /* CDC-ACM ifaces, 4 (str_idx in descriptor!) */
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return config_descriptor;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_quality_descriptor;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

static const struct usb_descriptor composite_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback
};

/* ==================== RNDIS lwip netif (PC side) ==================== */

static struct netif rndis_dev_netif = { .name = {'u', 'd'} };
static volatile uint8_t g_dev_inited;
static volatile uint8_t g_dev_configured;
/* Host stopped driving the bus (unplug-as-suspend, or host sleep): suppress
 * new transfers until RESUME/RESET. */
static volatile uint8_t g_dev_suspended;
static volatile uint8_t g_rndis_tx_busy;

/* Diagnostics for `usb device status` (IRQ-safe increments) */
static volatile uint32_t g_stat_rndis_rx_frames;
static volatile uint32_t g_stat_rndis_rx_bytes;
static volatile uint32_t g_stat_rndis_tx_frames;
static volatile uint32_t g_stat_rndis_tx_drop;
static volatile uint32_t g_stat_rx_ipv4, g_stat_rx_arp, g_stat_rx_ipv6, g_stat_rx_other;
static volatile uint32_t g_stat_rx_input_err;
static volatile uint32_t g_stat_in_done;
static volatile uint32_t g_stat_in_last_len;

/*
 * TX is queue-decoupled: linkoutput only enqueues a pbuf reference and
 * returns, so the tcpip thread never blocks on USB completion and lwip can
 * pipeline segments. A dedicated thread serializes the actual IN transfers
 * (the class has a single TX buffer) and owns the busy/claim dance.
 */
#define RNDIS_DEV_TX_QUEUE_DEPTH 64

static osMessageQueueId_t g_rndis_tx_queue;
static osThreadId_t g_rndis_tx_thread;
static volatile uint8_t g_rndis_tx_run;

static const osThreadAttr_t rndis_dev_tx_attr = {
    .name = "usb_dev_tx",
    /* Keep BELOW the watchdog feeder (wdgTask ~32): at Realtime this
     * thread starved the IWDG feeder under sustained 20 Mbps traffic and
     * reset the board. AboveNormal held 16 Mbps safely. */
    .priority = (osPriority_t) osPriorityAboveNormal,
    .stack_size = 2048,
};

static void rndis_dev_tx_worker(void *argument)
{
    (void)argument;
    struct pbuf *p;

    while (g_rndis_tx_run) {
        if (osMessageQueueGet(g_rndis_tx_queue, &p, NULL, osWaitForever) != osOK) continue;
        if (p == NULL) {
            pbuf_free(p);
            continue;
        }
        if (!g_dev_configured || g_dev_suspended) {
            g_stat_rndis_tx_drop++;
            pbuf_free(p);
            continue;
        }

        /* Drain any stale completion token, then submit and wait for THIS
         * transfer's completion. (A plain "wait if busy" pattern misfires:
         * with a max-count-1 semaphore, a leftover token makes the wait
         * pass immediately while busy is still set -> spurious drops ->
         * TCP collapse.) */
        (void)osSemaphoreAcquire(g_rndis_tx_sem, 0);
        g_rndis_tx_busy = 1;
        if (usbd_rndis_eth_tx(p) == 0) {
            g_stat_rndis_tx_frames++;
            if (osSemaphoreAcquire(g_rndis_tx_sem, 100) != osOK && g_rndis_tx_busy) {
                /* transfer still pending after timeout: leave busy set, the
                 * XFRC hook clears it; next iteration drains the token */
                g_stat_rndis_tx_drop++;
            }
        } else {
            g_rndis_tx_busy = 0;
            g_stat_rndis_tx_drop++;
        }
        pbuf_free(p); /* the class copies into its own TX buffer */
    }
}

static err_t rndis_dev_linkoutput(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    if (!g_dev_configured) return ERR_IF;
    if (g_rndis_tx_queue == NULL) return ERR_IF;

    pbuf_ref(p);
    if (osMessageQueuePut(g_rndis_tx_queue, &p, 0, 0) != osOK) {
        pbuf_free(p);
        g_stat_rndis_tx_drop++;
        return ERR_BUF; /* queue full: lwip retransmits */
    }
    return ERR_OK;
}

static err_t rndis_dev_netif_init(struct netif *netif)
{
    netif->hwaddr_len = ETH_HWADDR_LEN;
    netif_chip_id_get_mac(netif->hwaddr, NETIF_CHIP_MAC_USB_RNDIS);
    /* The host adopts the MAC we report via OID as ITS station address
     * (RNDIS behavior). Windows NDIS then drops any frame whose Ethernet
     * source equals its own MAC (loop detection) — so the netif (device
     * side) MUST transmit from a different MAC: flip the last bit.
     * (Same trick as upstream cdc_rndis_template: "device mac can't same
     * as host".) */
    netif->hwaddr[5] ^= 0x01;
#if LWIP_IPV4 && LWIP_ARP
    netif->output = etharp_output;
#endif
    netif->linkoutput = rndis_dev_linkoutput;
    netif->mtu = 1500;
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    return ERR_OK;
}

/* Class hooks: a frame arrived from the PC / previous TX completed.
 * The RX hook must wake the WORKER'S wait object (g_cdc_console_sem) —
 * nothing waits on g_rndis_rx_sem, and missing this wake capped the whole
 * link at one frame per 10 ms poll (~0.4 Mbps). */
void usbd_rndis_data_recv_done(uint32_t len)
{
    (void)len;
    if (g_cdc_console_sem != NULL) {
        osSemaphoreRelease(g_cdc_console_sem);
    }
}

void usbd_rndis_data_send_done(uint32_t len)
{
    g_stat_in_done++;
    g_stat_in_last_len = len;
    g_rndis_tx_busy = 0;
    if (g_rndis_tx_sem != NULL) {
        osSemaphoreRelease(g_rndis_tx_sem);
    }
}

/* Feeds PC frames into lwip; called from the usb device worker thread.
 * Drains everything buffered by the class (single rx slot at a time: the
 * class re-arms its OUT read inside eth_rx, so loop until empty). */
static void rndis_dev_input_poll(void)
{
    while (g_dev_configured) {
        struct pbuf *p = usbd_rndis_eth_rx();
        if (p == NULL) break;
        g_stat_rndis_rx_frames++;
        g_stat_rndis_rx_bytes += p->tot_len;

        if (p->len >= 14) {
            uint16_t ethtype = (uint16_t)((((uint8_t *)p->payload)[12] << 8) |
                                          ((uint8_t *)p->payload)[13]);
            if (ethtype == 0x0800) {
                g_stat_rx_ipv4++;
            } else if (ethtype == 0x0806) {
                g_stat_rx_arp++;
            } else if (ethtype == 0x86dd) {
                g_stat_rx_ipv6++;
            } else {
                g_stat_rx_other++;
            }
        } else {
            g_stat_rx_other++;
        }

        if (rndis_dev_netif.input != NULL) {
            if (rndis_dev_netif.input(p, &rndis_dev_netif) != ERR_OK) {
                g_stat_rx_input_err++;
                pbuf_free(p);
            }
        } else {
            g_stat_rx_input_err++;
            pbuf_free(p);
        }
    }
}

static int rndis_dev_netif_start(void)
{
    ip_addr_t ipaddr, netmask, gw;

    g_rndis_tx_sem = osSemaphoreNew(1, 0, NULL);
    if (g_rndis_tx_sem == NULL) return -1;

    g_rndis_tx_queue = osMessageQueueNew(RNDIS_DEV_TX_QUEUE_DEPTH, sizeof(struct pbuf *), NULL);
    if (g_rndis_tx_queue == NULL) {
        osSemaphoreDelete(g_rndis_tx_sem);
        g_rndis_tx_sem = NULL;
        return -1;
    }
    g_rndis_tx_run = 1;
    g_rndis_tx_thread = osThreadNew(rndis_dev_tx_worker, NULL, &rndis_dev_tx_attr);
    if (g_rndis_tx_thread == NULL) {
        g_rndis_tx_run = 0;
        osMessageQueueDelete(g_rndis_tx_queue);
        g_rndis_tx_queue = NULL;
        osSemaphoreDelete(g_rndis_tx_sem);
        g_rndis_tx_sem = NULL;
        return -1;
    }

    if (netif_get_by_index(rndis_dev_netif.num + 1) == &rndis_dev_netif) return 0;

    IP4_ADDR(&ipaddr, 192, 168, UDHCP_CLIENT_IP_3RD, 1);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, UDHCP_CLIENT_IP_3RD, 1);
    err_t err = netifapi_netif_add(&rndis_dev_netif, &ipaddr, &netmask, &gw, NULL,
                                   &rndis_dev_netif_init, &tcpip_input);
    if (err != ERR_OK) {
        return -1;
    }
    netifapi_netif_set_up(&rndis_dev_netif);
    netifapi_netif_set_link_up(&rndis_dev_netif);

    /* Hand out 192.168.20.100 to the PC via the mini DHCP server above. */
    udhcp_start();
    return 0;
}

static void rndis_dev_netif_stop(void)
{
    struct pbuf *p;

    if (g_rndis_tx_thread != NULL) {
        g_rndis_tx_run = 0;
        (void)osThreadTerminate(g_rndis_tx_thread);
        g_rndis_tx_thread = NULL;
    }
    if (g_rndis_tx_queue != NULL) {
        while (osMessageQueueGet(g_rndis_tx_queue, &p, NULL, 0) == osOK) {
            if (p != NULL) pbuf_free(p);
        }
        osMessageQueueDelete(g_rndis_tx_queue);
        g_rndis_tx_queue = NULL;
    }
    if (g_rndis_tx_sem != NULL) {
        osSemaphoreDelete(g_rndis_tx_sem);
        g_rndis_tx_sem = NULL;
    }

    if (netif_get_by_index(rndis_dev_netif.num + 1) != &rndis_dev_netif) return;
    udhcp_stop();
    netifapi_netif_set_link_down(&rndis_dev_netif);
    netifapi_netif_set_down(&rndis_dev_netif);
    (void)netifapi_netif_remove(&rndis_dev_netif);
}

/* ==================== CDC-ACM console redirect ===================== */

#define CDC_CONSOLE_RING_SIZE 8192 /* power of two (mask arithmetic) */
#define CDC_CONSOLE_TX_CHUNK  512  /* one bulk-IN endpoint packet */

static struct {
    volatile uint32_t head;
    volatile uint32_t tail;
    uint8_t buf[CDC_CONSOLE_RING_SIZE];
} g_cdc_tx_ring, g_cdc_rx_ring;

static volatile uint8_t g_cdc_dtr;
/* Host reader detected: DTR asserted OR the host has sent us at least one
 * byte (tools/scripts that open the port without DTR). Console output is
 * gated on this instead of DTR alone. Cleared on bus reset/disconnect. */
static volatile uint8_t g_cdc_host_active;
static volatile uint8_t g_cdc_ep_tx_busy;
static osThreadId_t g_usb_dev_thread;
static volatile uint8_t g_usb_dev_thread_run;

static uint32_t ring_count(volatile uint32_t head, volatile uint32_t tail)
{
    return (head - tail) & (CDC_CONSOLE_RING_SIZE - 1);
}

static void ring_push(uint8_t *buf, volatile uint32_t *head, uint32_t tail, uint8_t c)
{
    uint32_t next = (*head + 1) & (CDC_CONSOLE_RING_SIZE - 1);
    if (next == tail) return; /* full: drop (log output is lossy by design) */
    buf[*head] = c;
    *head = next;
}

static uint8_t ring_pop(uint8_t *buf, volatile uint32_t *head, volatile uint32_t *tail)
{
    uint8_t c = buf[*tail];
    *tail = (*tail + 1) & (CDC_CONSOLE_RING_SIZE - 1);
    return c;
}

/*
 * Console tee — strong override of the weak hook in Gcc/Src/console.c.
 * Called from _write (printf/stdout: logs + command output) and from the
 * debug system's direct-UART sinks (per-keystroke echo). Any context
 * (incl. IRQ): only touches the tx ring under a short IRQ mask — printf
 * is legal from multiple threads and even USB IRQ handlers here.
 */
void usb_console_output_hook(const char *msg, int len)
{
    /* Gate on CONFIGURED only: many terminals/scripts open the COM port
     * without asserting DTR — output must still flow. If nothing reads the
     * endpoint, the first chunk parks in g_cdc_ep_tx_busy until a reader
     * appears (ring absorbs the backlog, overflow drops). */
    if (!g_dev_configured || g_dev_suspended) return;
    if (!g_cdc_dtr && !g_cdc_host_active) return;
    if (msg == NULL || len <= 0) return;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    for (int i = 0; i < len; i++) {
        ring_push(g_cdc_tx_ring.buf, &g_cdc_tx_ring.head, g_cdc_tx_ring.tail, (uint8_t)msg[i]);
    }
    __set_PRIMASK(primask);

    if (g_cdc_console_sem != NULL) {
        osSemaphoreRelease(g_cdc_console_sem);
    }
}

/* USB bulk OUT callback (IRQ context): queue PC keystrokes for the CLI. */
static void cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    /* read_buffer belongs to this function alone; reuse via static below. */
    static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc_read_buffer[512];
    if (nbytes > 0) {
        g_cdc_host_active = 1; /* reader on the host side: enable console tx */
    }
    for (uint32_t i = 0; i < nbytes && i < sizeof(cdc_read_buffer); i++) {
        ring_push(g_cdc_rx_ring.buf, &g_cdc_rx_ring.head, g_cdc_rx_ring.tail, cdc_read_buffer[i]);
    }
    if (g_cdc_console_sem != NULL) {
        osSemaphoreRelease(g_cdc_console_sem);
    }
    usbd_ep_start_read(busid, CDC_OUT_EP, cdc_read_buffer, sizeof(cdc_read_buffer));
}

static void cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;
    g_cdc_ep_tx_busy = 0;
    if (g_cdc_console_sem != NULL) {
        osSemaphoreRelease(g_cdc_console_sem);
    }
}

static struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = cdc_acm_bulk_out
};

static struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = cdc_acm_bulk_in
};

/* DTR means "a terminal is attached on the PC side" (informational; console
 * tx is gated on DTR OR any received host data — see g_cdc_host_active). */
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    (void)intf;
    g_cdc_dtr = dtr ? 1 : 0;
    printf("[USB-DEV] console port %s\r\n", dtr ? "opened" : "closed");
}

/*
 * Worker thread: drains the RNDIS rx hook into lwip and shuttles the CDC
 * console rings (device->PC log bytes, PC->device keystrokes). Never calls
 * blocking USB APIs from IRQ context.
 */
static const osThreadAttr_t usb_dev_worker_attr = {
    .name = "usb_dev",
    .priority = (osPriority_t) osPriorityAboveNormal,
    .stack_size = 2048,
};

static void usb_dev_worker(void *argument)
{
    (void)argument;
    static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t cdc_write_buffer[CDC_CONSOLE_TX_CHUNK];

    while (g_usb_dev_thread_run) {
        /* Fast path for RNDIS rx; everything else rides the same timeout. */
        (void)osSemaphoreAcquire(g_cdc_console_sem, 10);

        if (g_dev_configured) {
            rndis_dev_input_poll();
        }

        /* log bytes -> PC */
        if (g_dev_configured && !g_dev_suspended && !g_cdc_ep_tx_busy) {
            uint32_t n = ring_count(g_cdc_tx_ring.head, g_cdc_tx_ring.tail);
            if (n > 0) {
                if (n > CDC_CONSOLE_TX_CHUNK) n = CDC_CONSOLE_TX_CHUNK;
                for (uint32_t i = 0; i < n; i++) {
                    cdc_write_buffer[i] = ring_pop(g_cdc_tx_ring.buf, &g_cdc_tx_ring.head, &g_cdc_tx_ring.tail);
                }
                g_cdc_ep_tx_busy = 1;
                int ret = usbd_ep_start_write(0, CDC_IN_EP, cdc_write_buffer, n);
                if (ret != 0) {
                    g_cdc_ep_tx_busy = 0;
                }
            }
        }

        /* PC keystrokes -> debug CLI (debug_cmdline_input is the official
         * injection point and is ISR/context safe). */
        while (ring_count(g_cdc_rx_ring.head, g_cdc_rx_ring.tail) > 0) {
            uint8_t c = ring_pop(g_cdc_rx_ring.buf, &g_cdc_rx_ring.head, &g_cdc_rx_ring.tail);
            debug_cmdline_input((char)c);
        }
    }
}

/* ==================== device lifecycle ============================== */

static struct usbd_interface intf_rndis0;
static struct usbd_interface intf_rndis1;
static struct usbd_interface intf_acm0;
static struct usbd_interface intf_acm1;

static const char *dev_event_name(uint8_t event)
{
    switch (event) {
        case USBD_EVENT_RESET: return "reset";
        case USBD_EVENT_CONNECTED: return "connected";
        case USBD_EVENT_DISCONNECTED: return "disconnected";
        case USBD_EVENT_CONFIGURED: return "configured";
        case USBD_EVENT_SUSPEND: return "suspend";
        case USBD_EVENT_RESUME: return "resume";
        default: return "other";
    }
}

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    printf("[USB-DEV] bus%u: %s\r\n", busid, dev_event_name(event));
    switch (event) {
        case USBD_EVENT_CONFIGURED:
            g_dev_configured = 1;
            g_dev_suspended = 0;
            /* RNDIS arms its own first read via its notify handler. Arm the
             * CDC console OUT endpoint here (no auto-arm for CDC-ACM). */
            cdc_acm_bulk_out(busid, CDC_OUT_EP, 0);
            break;
        case USBD_EVENT_SUSPEND:
            /* Host stopped driving the bus: cable pulled (device IP sees
             * unplug as suspend) or host sleep. Stop NEW transfers; in-flight
             * ones complete (or die silently) — tx gates stay bounded. */
            g_dev_suspended = 1;
            break;
        case USBD_EVENT_RESUME:
            g_dev_suspended = 0;
            break;
        case USBD_EVENT_RESET:
        case USBD_EVENT_DISCONNECTED:
            g_dev_configured = 0;
            g_dev_suspended = 0;
            g_cdc_ep_tx_busy = 0;
            g_rndis_tx_busy = 0;
            g_cdc_host_active = 0; /* re-learn the reader after re-enum */
            break;
        default:
            break;
    }
}

int usb_cherry_device_init(void)
{
    if (g_dev_inited) {
        return 0;
    }

    if (usb_cherry_osal_ensure() != 0) {
        return -1;
    }

    if (rndis_dev_netif_start() != 0) {
        printf("[USB-DEV] rndis netif start failed\r\n");
        return -1;
    }

    uint8_t mac[6];
    netif_chip_id_get_mac(mac, NETIF_CHIP_MAC_USB_RNDIS);

    g_cdc_console_sem = osSemaphoreNew(1, 0, NULL);
    if (g_cdc_console_sem == NULL) {
        rndis_dev_netif_stop();
        return -1;
    }

    g_usb_dev_thread_run = 1;
    g_usb_dev_thread = osThreadNew(usb_dev_worker, NULL, &usb_dev_worker_attr);
    if (g_usb_dev_thread == NULL) {
        g_usb_dev_thread_run = 0;
        osSemaphoreDelete(g_cdc_console_sem);
        g_cdc_console_sem = NULL;
        rndis_dev_netif_stop();
        return -1;
    }

    /* Console tee is installed at the UART emission points
     * (usb_console_output_hook in console.c / debug.c) — see above. */

    usbd_desc_register(0, &composite_descriptor);

    /* RNDIS function (interfaces 0-1) — init_intf also registers its 3 endpoints. */
    usbd_add_interface(0, usbd_rndis_init_intf(&intf_rndis0, RNDIS_OUT_EP, RNDIS_IN_EP, RNDIS_INT_EP, mac));
    usbd_add_interface(0, usbd_rndis_init_intf(&intf_rndis1, RNDIS_OUT_EP, RNDIS_IN_EP, RNDIS_INT_EP, mac));

    /* CDC-ACM console function (interfaces 2-3) + its data endpoints. */
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intf_acm0));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intf_acm1));
    usbd_add_endpoint(0, &cdc_out_ep);
    usbd_add_endpoint(0, &cdc_in_ep);

    int ret = usbd_initialize(0, (uintptr_t)USB1_OTG_HS_BASE, usbd_event_handler);
    if (ret != 0) {
        printf("[USB-DEV] device init failed: %d\r\n", ret);
        usb_cherry_device_deinit();
        return ret;
    }
    g_dev_inited = 1;
    return 0;
}

int usb_cherry_device_deinit(void)
{
    if (!g_dev_inited && !g_dev_configured && g_usb_dev_thread == NULL) {
        return 0;
    }

    usbd_deinitialize(0);
    g_dev_configured = 0;
    g_dev_inited = 0;

    if (g_usb_dev_thread != NULL) {
        g_usb_dev_thread_run = 0;
        (void)osThreadTerminate(g_usb_dev_thread);
        g_usb_dev_thread = NULL;
    }
    if (g_cdc_console_sem != NULL) {
        osSemaphoreDelete(g_cdc_console_sem);
        g_cdc_console_sem = NULL;
    }

    rndis_dev_netif_stop();
    return 0;
}

int usb_cherry_device_is_inited(void)
{
    return g_dev_inited;
}

void usb_cherry_device_diag(char *buf, int buflen)
{
    snprintf(buf, buflen,
             "cfg:%d rx:%lu(ip4:%lu arp:%lu v6:%lu) tx:%lu drop:%lu indone:%lu inlen:%lu ip:%s",
             g_dev_configured,
             (unsigned long)g_stat_rndis_rx_frames,
             (unsigned long)g_stat_rx_ipv4,
             (unsigned long)g_stat_rx_arp,
             (unsigned long)g_stat_rx_ipv6,
             (unsigned long)g_stat_rndis_tx_frames,
             (unsigned long)g_stat_rndis_tx_drop,
             (unsigned long)g_stat_in_done,
             (unsigned long)g_stat_in_last_len,
             ip4addr_ntoa((const ip4_addr_t *)&rndis_dev_netif.ip_addr));
}

/* ==================== netif_manager glue ("ud") ===================== */

#include "netif_manager.h"

static netif_state_t usb_dev_netif_state(void)
{
    if (netif_get_by_index(rndis_dev_netif.num + 1) != &rndis_dev_netif) return NETIF_STATE_DEINIT;
    if (!netif_is_link_up(&rndis_dev_netif)) return NETIF_STATE_DOWN;
    return NETIF_STATE_UP;
}

static int usb_dev_netif_info(netif_info_t *info)
{
    if (info == NULL) return AICAM_ERROR_INVALID_PARAM;
    info->host_name = NULL;
    info->if_name = NETIF_NAME_USB_DEV;
    info->type = NETIF_TYPE_ETH;
    info->state = usb_dev_netif_state();
    info->ip_mode = NETIF_IP_MODE_STATIC;
    snprintf(info->fw_version, sizeof(info->fw_version), "cherryusb-%s", "1.6.1");
    memcpy(info->if_mac, rndis_dev_netif.hwaddr, sizeof(info->if_mac));
    memcpy(info->ip_addr, &rndis_dev_netif.ip_addr, sizeof(info->ip_addr));
    memcpy(info->gw, &rndis_dev_netif.gw, sizeof(info->gw));
    memcpy(info->netmask, &rndis_dev_netif.netmask, sizeof(info->netmask));
    return AICAM_OK;
}

int usb_dev_netif_ctrl(const char *if_name, int cmd, void *param)
{
    int ret = AICAM_ERROR;

    if (if_name == NULL || strcmp(if_name, NETIF_NAME_USB_DEV) != 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    switch ((netif_cmd_t)cmd) {
        case NETIF_CMD_INIT:
            ret = usb_cherry_device_init();
            break;
        case NETIF_CMD_UP:
            /* The netif comes up together with the composite device. */
            ret = (usb_dev_netif_state() == NETIF_STATE_UP) ? AICAM_OK : AICAM_ERROR;
            break;
        case NETIF_CMD_DOWN:
        case NETIF_CMD_UNINIT:
            usb_cherry_device_deinit();
            ret = AICAM_OK;
            break;
        case NETIF_CMD_INFO:
            ret = usb_dev_netif_info((netif_info_t *)param);
            break;
        case NETIF_CMD_STATE:
            if (param == NULL) ret = AICAM_ERROR_INVALID_PARAM;
            else {
                *(netif_state_t *)param = usb_dev_netif_state();
                ret = AICAM_OK;
            }
            break;
        default:
            ret = AICAM_ERROR_NOT_SUPPORTED;
            break;
    }
    return ret;
}
