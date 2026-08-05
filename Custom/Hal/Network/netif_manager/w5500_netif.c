#include <stdio.h>
#include <string.h>
#include "common_utils.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/netifapi.h"
#include "Log/debug.h"
#include "dhcpserver.h"
#include "mem.h"
#include "w5500.h"
#include "w5500_netif.h"
#include "chip_id_mac.h"

#define W5500_EVENT_INTERRUPT               (1 << 0)
#define W5500_EVENT_INT_SEND_OK             (1 << 1)
#define W5500_EVENT_INT_TIMEOUT             (1 << 2)
#define W5500_EVENT_INT_RECV                (1 << 3)
#define W5500_EVENT_ISR_TASK_EXIT_REQ       (1 << 4) 
#define W5500_EVENT_ISR_TASK_EXIT_ACK       (1 << 5)
#define W5500_EVENT_COMM_TASK_EXIT_REQ      (1 << 6) 
#define W5500_EVENT_COMM_TASK_EXIT_ACK      (1 << 7)

/// @brief Ethernet network interface
static struct netif eth_netif = {
    .name = {NETIF_NAME_ETH_WAN[0], NETIF_NAME_ETH_WAN[1]},
};
/// @brief Ethernet configuration
static netif_config_t eth_config = {
    .ip_mode = NETIF_ETH_WAN_DEFAULT_IP_MODE,
    .ip_addr = NETIF_ETH_WAN_DEFAULT_IP,
    .netmask = NETIF_ETH_WAN_DEFAULT_MASK,
    .gw = NETIF_ETH_WAN_DEFAULT_GW,
};
/// @brief Ethernet event flags
static osEventFlagsId_t w5500_events = NULL;
/// @brief Ethernet mutex
static osMutexId_t w5500_mutex = NULL;
/// @brief Ethernet thread IDs
static osThreadId_t w5500_isr_thread_ID = NULL, w5500_comm_thread_ID = NULL;
/// @brief Ethernet interrupt event handling thread stack
static uint8_t w5500_isr_event_stack[4096] ALIGN_32 IN_PSRAM = {0};
/// @brief Ethernet interrupt event handling thread attributes
static const osThreadAttr_t w5500_isr_event_attr = {
    .name       = "w5500_isr_event",
    .priority   = osPriorityRealtime5,
    .stack_mem  = w5500_isr_event_stack,
    .stack_size = sizeof(w5500_isr_event_stack),
    .cb_mem     = 0,
    .cb_size    = 0,
    .attr_bits  = 0u,
    .tz_module  = 0u,
};
/// @brief Ethernet data processing thread stack
static uint8_t w5500_data_comm_stack[4096] ALIGN_32 IN_PSRAM = {0};
/// @brief Ethernet data processing thread attributes
static const osThreadAttr_t w5500_data_comm_attr = {
    .name       = "w5500_data_comm",
    .priority   = osPriorityRealtime4,
    .stack_mem  = w5500_data_comm_stack,
    .stack_size = sizeof(w5500_data_comm_stack),
    .cb_mem     = 0,
    .cb_size    = 0,
    .attr_bits  = 0u,
    .tz_module  = 0u,
};

#define W5500_BUF_SIZE          (16 * 1024)
static uint8_t w5500_rbuf[W5500_BUF_SIZE] ALIGN_32 IN_PSRAM = {0};
static uint16_t w5500_rbuf_len = 0;
static uint8_t w5500_is_need_reinit = 0; ///< 0: No need to reinitialize W5500, 1: Need to reinitialize W5500

/// @brief Network interface low-level data input processing function
/// @param netif Network interface
/// @param b Data pointer
/// @param len Length
static void w5500_low_level_input(struct netif *netif, uint8_t *b, uint16_t len)
{
    struct pbuf *p = NULL, *q = NULL;
    uint32_t bufferoffset = 0;
    int ret = ERR_OK;

    if (len <= 0) return;
    if (len < NETIF_LWIP_FRAME_ALIGNMENT) len = NETIF_LWIP_FRAME_ALIGNMENT;

    // W5500_LOGD("LWIP RX len : %d", len);
    /* We allocate a pbuf chain of pbufs from the Lwip buffer pool
    * and copy the data to the pbuf chain
    */
    if ((p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL)) != ((struct pbuf *)0)) {
        for (q = p, bufferoffset = 0; q != NULL; q = q->next) {
            memcpy((uint8_t *)q->payload, (uint8_t *)b + bufferoffset, q->len);
            bufferoffset += q->len;
        }
        ret = netif->input(p, netif);
        if (ret != ERR_OK) {
            pbuf_free(p);
            osDelay(5);
            // printf(NETIF_NAME_STR_FMT ": Input failed(ret = %d)!", NETIF_NAME_PARAMETER(netif), ret);
        }
    } else {
        printf(NETIF_NAME_STR_FMT ": Failed to allocate pbuf!", NETIF_NAME_PARAMETER(netif));
    }
}

void w5500_send_macraw_data(uint8_t *data, uint16_t len)
{
    int all_slen = 0, slen = 0;

    // SCB_CleanDCache_by_Addr((void *)data, len);
    do {
        osEventFlagsWait(w5500_events, W5500_EVENT_INT_SEND_OK, osFlagsWaitAny, NETIF_ETH_WAN_MACRAW_SEND_TIMEOUT);
        slen = W5500_Macraw_Sock_Send(data + all_slen, len - all_slen, 1);
        if (slen <= 0) break;
        all_slen += slen;
    } while (all_slen < len);
}

/// @brief Network interface low-level data output function
/// @param netif Network interface
/// @param p Data buffer
/// @return Error code
static err_t w5500_low_level_output(struct netif *netif, struct pbuf *p)
{
    // err_t ret = 0;
    struct pbuf *q = NULL;
    uint8_t *out_buf = NULL, *out_ptr = NULL;

    // W5500_LOGD("LWIP TX len : %d / %d", p->len, p->tot_len);
    if (p->len != p->tot_len) {
        out_buf = hal_mem_alloc_fast(p->tot_len);
        if (out_buf != NULL) {
            out_ptr = out_buf;
            for (q = p; q != NULL; q = q->next) {
                memcpy(out_ptr, q->payload, q->len);
                out_ptr += q->len;
            }
            w5500_send_macraw_data(out_buf, p->tot_len);
            hal_mem_free(out_buf);
            return ERR_OK;
        }
    }
    for (q = p; q != NULL; q = q->next) {
        // W5500_LOGD("LWIP TX len : %d", q->len);
        w5500_send_macraw_data((uint8_t *)q->payload, q->len);
    }
    return ERR_OK;
}

/// @brief Network interface low-level initialization function
/// @param netif Network interface to initialize
static err_t w5500_ethernetif_init(struct netif *netif)
{
    int ret = ERR_OK;
    uint8_t w5500_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (netif == NULL) return ERR_ARG;

    // set netif MAC hardware address length
    netif->hwaddr_len = ETH_HWADDR_LEN;
    // Request MAC address
    ret = W5500_Get_MAC(w5500_mac);
    if (ret != W5500_OK) return ERR_IF;

    // Config diy mac
    if (NETIF_MAC_IS_UNICAST(netif->hwaddr) && memcmp(netif->hwaddr, w5500_mac, netif->hwaddr_len)) {
        memcpy(w5500_mac, netif->hwaddr, netif->hwaddr_len);
        ret = W5500_Set_MAC(w5500_mac);
        if (ret != W5500_OK) return ERR_IF;
    } else memcpy(netif->hwaddr, w5500_mac, netif->hwaddr_len);
    W5500_LOGD(NETIF_NAME_STR_FMT ": MAC Address: " NETIF_MAC_STR_FMT "", NETIF_NAME_PARAMETER(netif), NETIF_MAC_PARAMETER(netif->hwaddr));

#if LWIP_NETIF_HOSTNAME
    // TODO: set netif hostname
    netif->hostname = eth_config.host_name;
#endif

    //! Assign handler/function for the interface
#if LWIP_IPV4 && LWIP_ARP
    netif->output = etharp_output;
#endif /* #if LWIP_IPV4 && LWIP_ARP */
    netif->linkoutput = w5500_low_level_output;

    // set netif maximum transfer unit
    netif->mtu = NETIF_MAX_TRANSFER_UNIT;

    // Accept broadcast address and ARP traffic
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;

    return ERR_OK;
}

/// @brief Get MAC address generated from chip ID
/// @param mac Generated MAC address
static void w5500_net_get_chip_mac(uint8_t *mac)
{
    netif_chip_id_get_mac(mac, NETIF_CHIP_MAC_W5500);
    W5500_LOGD("MCU chip MAC: %02X:%02X:%02X:%02X:%02X:%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

extern void netif_manager_change_default_if(void);
void w5500_isr_thread(void *arg)
{
    int ret = 0;
    uint8_t s0_ir = 0x00, phy_cgf = 0x00, ver_reg = 0x00;
    uint32_t event = 0;
    netif_state_t w5500_state = NETIF_STATE_DEINIT;

    while (1) {
        event = osEventFlagsWait(w5500_events, W5500_EVENT_INTERRUPT | W5500_EVENT_ISR_TASK_EXIT_REQ, osFlagsWaitAny, NETIF_ETH_WAN_WAIT_IR_TIMEOUT);
        if (event & osFlagsError) event = 0;

        s0_ir = 0x00;
        ret = W5500_Sock_Get_IR(0, &s0_ir);
        if (ret != W5500_OK) goto isr_thread_exit_check;
        if (s0_ir == 0x00 || s0_ir == 0xFF) {
            if (w5500_mutex != NULL && osMutexAcquire(w5500_mutex, 0) == osOK) {
                w5500_state = w5500_netif_state();
                osMutexRelease(w5500_mutex);
                if (w5500_state != NETIF_STATE_UP) goto isr_thread_exit_check;
                phy_cgf = 0x00;
                ver_reg = 0x00;
                ret = W5500_Read_Datas(VERSIONR, &ver_reg, 1, W5500_SPI_LESS_10B_TIMEOUT);
                if (ret < 0) goto isr_thread_exit_check;
                if (ver_reg == NETIF_ETH_WAN_W5500_FW_VERSION) {
                    ret = W5500_Read_Datas(PHYCFGR, &phy_cgf, 1, W5500_SPI_LESS_10B_TIMEOUT);
                    if (ret < 0) goto isr_thread_exit_check;
                    // W5500_LOGD("W5500 PHYCFGR = 0x%02X", phy_cgf);
                }
                if ((ver_reg != NETIF_ETH_WAN_W5500_FW_VERSION) || ((phy_cgf & 0x80) == 0x00) || ((phy_cgf & 0x01) == 0x00)) {
                    W5500_LOGE("W5500 is disconnected or reset!");
                    osMutexAcquire(w5500_mutex, osWaitForever);
                    w5500_netif_down();
                    w5500_is_need_reinit = 1;
                    osMutexRelease(w5500_mutex);
                    netif_manager_change_default_if();
                    goto isr_thread_exit_check;
                }
            }
        } else {
            if (s0_ir & Sn_IR_SEND_OK) {
                ret = W5500_Sock_Set_IR(0, Sn_IR_SEND_OK);
                if (ret != W5500_OK) goto isr_thread_exit_check;
                osEventFlagsSet(w5500_events, W5500_EVENT_INT_SEND_OK);
            }
            if (s0_ir & Sn_IR_TIMEOUT) {
                ret = W5500_Sock_Set_IR(0, Sn_IR_TIMEOUT);
                if (ret != W5500_OK) goto isr_thread_exit_check;
                osEventFlagsSet(w5500_events, W5500_EVENT_INT_TIMEOUT);
            }
            if (s0_ir & Sn_IR_RECV) {
                ret = W5500_Sock_Set_IR(0, Sn_IR_RECV);
                if (ret != W5500_OK) goto isr_thread_exit_check;
                osEventFlagsSet(w5500_events, W5500_EVENT_INT_RECV);
            }
        }
        if (W5500_GPIO_INTn_READ() == 0) {
            osEventFlagsSet(w5500_events, W5500_EVENT_INTERRUPT);
        }
    isr_thread_exit_check:
        if (event & W5500_EVENT_ISR_TASK_EXIT_REQ) {
            osEventFlagsSet(w5500_events, W5500_EVENT_ISR_TASK_EXIT_ACK);
            osThreadExit();
        }
    }
}

void w5500_comm_thread(void *arg)
{
    int rlen = 0;
    uint16_t pkt_len = 0;
    uint32_t event = 0;

    while (1) {
        event = osEventFlagsWait(w5500_events, W5500_EVENT_INT_RECV | W5500_EVENT_COMM_TASK_EXIT_REQ, osFlagsWaitAny, osWaitForever);
        if (event & osFlagsError) continue;

        if (event & W5500_EVENT_INT_RECV) {
            rlen = W5500_Macraw_Sock_Recv(w5500_rbuf + w5500_rbuf_len, W5500_BUF_SIZE - w5500_rbuf_len, 1);
            // SCB_InvalidateDCache_by_Addr((void *)w5500_rbuf, W5500_BUF_SIZE);
            if (rlen > 0) {
                w5500_rbuf_len += rlen;
                while (w5500_rbuf_len > 2) {
                    pkt_len = w5500_rbuf[0] << 8 | w5500_rbuf[1];
                    if (pkt_len < 14 || pkt_len > 1600) {
                        w5500_rbuf_len = 0;
                        W5500_LOGE("LWIP RX len error : %d / %d", pkt_len, w5500_rbuf_len);
                        break;
                    }
                    if (pkt_len > w5500_rbuf_len) break;
                    w5500_rbuf_len -= pkt_len;
                    w5500_low_level_input(&eth_netif, &w5500_rbuf[2], pkt_len - 2);
                    memcpy(w5500_rbuf, (w5500_rbuf + pkt_len), w5500_rbuf_len);
                }
            }
        }
        if (event & W5500_EVENT_COMM_TASK_EXIT_REQ) {
            osEventFlagsSet(w5500_events, W5500_EVENT_COMM_TASK_EXIT_ACK);
            osThreadExit();
        }
    }
}

void w5500_interrupt_callback(void)
{
    osEventFlagsSet(w5500_events, W5500_EVENT_INTERRUPT);
}

int w5500_netif_init(void)
{
    w5500_config_t w5500_cfg = { 0 };
    struct netif *eth = NULL;
    int ret = 0;

    eth = netif_get_by_index(eth_netif.num + 1);
    if (eth != NULL && eth == &eth_netif) return W5500_ERR_INVALID_STATE;

    /* Initialize config */
    if (NETIF_MAC_IS_ZERO(eth_config.diy_mac)) {
        w5500_net_get_chip_mac(eth_config.diy_mac);   
    }
    memcpy(w5500_cfg.mac, eth_config.diy_mac, sizeof(eth_config.diy_mac));
    memcpy(w5500_cfg.sub, eth_config.netmask, sizeof(eth_config.netmask));
    memcpy(w5500_cfg.gw, eth_config.gw, sizeof(eth_config.gw));
    memcpy(w5500_cfg.ip, eth_config.ip_addr, sizeof(eth_config.ip_addr));
    w5500_cfg.rtr = w5500_default_config.rtr;
    w5500_cfg.rcr = w5500_default_config.rcr;
    w5500_cfg.tx_size[0] = 16;
    w5500_cfg.rx_size[0] = 16;

    // Init W5500
    ret = W5500_Init(&w5500_cfg);
    if (ret != W5500_OK) return ret;

    // Create Task
    w5500_events = osEventFlagsNew(NULL);
    if (w5500_events == NULL) {
        ret = W5500_ERR_MEM;
        goto w5500_netif_init_failed;
    }
    w5500_isr_thread_ID = osThreadNew(w5500_isr_thread, NULL, &w5500_isr_event_attr);
    if (w5500_isr_thread_ID == NULL) {
        ret = W5500_ERR_MEM;
        goto w5500_netif_init_failed;
    }
    w5500_comm_thread_ID = osThreadNew(w5500_comm_thread, NULL, &w5500_data_comm_attr);
    if (w5500_comm_thread_ID == NULL) {
        ret = W5500_ERR_MEM;
        goto w5500_netif_init_failed;
    }

    // Enable Interrupt
    W5500_Enable_Interrupt(w5500_interrupt_callback);
    
    // Add Network Interface (run on tcpip thread)
    err_t nif_err = netifapi_netif_add(&eth_netif, NULL, NULL, NULL, NULL, &w5500_ethernetif_init, &tcpip_input);
    if (nif_err != ERR_OK) {
        ret = W5500_ERR_FAILED;
        goto w5500_netif_init_failed;
    }

    w5500_is_need_reinit = 0;
    return W5500_OK;

w5500_netif_init_failed:
    if (w5500_isr_thread_ID != NULL) {
        osEventFlagsSet(w5500_events, W5500_EVENT_ISR_TASK_EXIT_REQ);
        osEventFlagsWait(w5500_events, W5500_EVENT_ISR_TASK_EXIT_ACK, osFlagsWaitAny, osWaitForever);
        osThreadTerminate(w5500_isr_thread_ID);
        w5500_isr_thread_ID = NULL;
    }

    if (w5500_comm_thread_ID != NULL) {
        osEventFlagsSet(w5500_events, W5500_EVENT_COMM_TASK_EXIT_REQ);
        osEventFlagsWait(w5500_events, W5500_EVENT_COMM_TASK_EXIT_ACK, osFlagsWaitAny, osWaitForever);
        osThreadTerminate(w5500_comm_thread_ID);
        w5500_comm_thread_ID = NULL;
    }

    // Disable Interrupt
    W5500_Disable_Interrupt();

    W5500_DeInit();

    if (w5500_events != NULL) {
        osEventFlagsDelete(w5500_events);
        w5500_events = NULL;
    }

    return ret;
}

int w5500_netif_up(void)
{
    struct netif *eth = NULL;
    ip_addr_t ipaddr  = { 0 };
    ip_addr_t gateway = { 0 };
    ip_addr_t netmask = { 0 };
    int ret = 0;
    uint8_t phy_cgf = 0x00, ver_reg = 0x00, try_count = 0;
    uint32_t timeout_ms = 0;

    eth = netif_get_by_index(eth_netif.num + 1);
    if (eth == NULL || eth != &eth_netif) return W5500_ERR_INVALID_STATE;
    if (netif_is_link_up(eth)) return W5500_OK;

    ret = W5500_Read_Datas(VERSIONR, &ver_reg, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (ret < 0) return ret;
    if (ver_reg != NETIF_ETH_WAN_W5500_FW_VERSION) {
        ret = W5500_ERR_UNKNOW;
        W5500_LOGE("W5500 firmware version error!");
        return ret;
    }

    if (w5500_is_need_reinit) {
        w5500_netif_deinit();
        ret = w5500_netif_init();
        if (ret != W5500_OK) return ret;
    }

    do {
        ret = W5500_Read_Datas(PHYCFGR, &phy_cgf, 1, W5500_SPI_LESS_10B_TIMEOUT);
        if (ret < 0) goto w5500_netif_up_end;
        if (((phy_cgf & 0x01) == 0x00)) osDelay(100);
    } while (((phy_cgf & 0x01) == 0x00) && (try_count++ < 30));
    if (((phy_cgf & 0x01) == 0x00)) {
        ret = W5500_ERR_UNCONNECTED;
        W5500_LOGE("W5500 is disconnected!");
        goto w5500_netif_up_end;
    }

    // Config IP
    ret = W5500_Cfg_Net(eth_config.ip_addr, eth_config.gw, eth_config.netmask);
    if (ret != W5500_OK) goto w5500_netif_up_end;
    // OPEN Socket
    ret = W5500_Macraw_Sock_Open(1, 1, 0, 0);
    if (ret != W5500_OK) goto w5500_netif_up_end;
    IP4_ADDR(&ipaddr, eth_config.ip_addr[0], eth_config.ip_addr[1], eth_config.ip_addr[2], eth_config.ip_addr[3]);
    IP4_ADDR(&gateway, eth_config.gw[0], eth_config.gw[1], eth_config.gw[2], eth_config.gw[3]);
    IP4_ADDR(&netmask, eth_config.netmask[0], eth_config.netmask[1], eth_config.netmask[2], eth_config.netmask[3]);

    ret = netifapi_netif_set_addr(&eth_netif, &ipaddr, &netmask, &gateway);
    if (ret != ERR_OK) {
        ret = W5500_ERR_FAILED;
        goto w5500_netif_up_end;
    }

    ret = netifapi_netif_set_up(&eth_netif);
    if (ret != ERR_OK) {
        ret = W5500_ERR_FAILED;
        goto w5500_netif_up_end;
    }
    ret = netifapi_netif_set_link_up(&eth_netif);
    if (ret != ERR_OK) {
        ret = W5500_ERR_FAILED;
        goto w5500_netif_up_end;
    }

    if (eth_config.ip_mode == NETIF_IP_MODE_DHCP) {    // DHCP client Mode
        ip_addr_set_zero_ip4(&(eth_netif.ip_addr));
        ip_addr_set_zero_ip4(&(eth_netif.netmask));
        ip_addr_set_zero_ip4(&(eth_netif.gw));

        ret = netifapi_dhcp_start(&eth_netif);
        if (ret == ERR_OK) {
            do {
                if (dhcp_supplied_address(&eth_netif)) {
                    W5500_LOGD(NETIF_NAME_STR_FMT " dhcp ip: %s", NETIF_NAME_PARAMETER(&eth_netif), ip4addr_ntoa((const ip4_addr_t *)&eth_netif.ip_addr));
                    break;
                }
                if (timeout_ms < NETIF_ETH_WAN_DEFAULT_DHCP_TIMEOUT) {
                    osDelay(100);
                    timeout_ms += 100;
                } else ret = W5500_ERR_TIMEOUT;
            } while (ret == ERR_OK);
        } else ret = W5500_ERR_FAILED;

        if (ret == ERR_OK) {
            // Config IP
            eth_config.ip_addr[0] = ip4_addr1(&eth_netif.ip_addr);
            eth_config.ip_addr[1] = ip4_addr2(&eth_netif.ip_addr);
            eth_config.ip_addr[2] = ip4_addr3(&eth_netif.ip_addr);
            eth_config.ip_addr[3] = ip4_addr4(&eth_netif.ip_addr);
            eth_config.gw[0] = ip4_addr1(&eth_netif.gw);
            eth_config.gw[1] = ip4_addr2(&eth_netif.gw);
            eth_config.gw[2] = ip4_addr3(&eth_netif.gw);
            eth_config.gw[3] = ip4_addr4(&eth_netif.gw);
            eth_config.netmask[0] = ip4_addr1(&eth_netif.netmask);
            eth_config.netmask[1] = ip4_addr2(&eth_netif.netmask);
            eth_config.netmask[2] = ip4_addr3(&eth_netif.netmask);
            eth_config.netmask[3] = ip4_addr4(&eth_netif.netmask);
            ret = W5500_Cfg_Net(eth_config.ip_addr, eth_config.gw, eth_config.netmask);
        }
    }

w5500_netif_up_end:
        if (ret != ERR_OK) {
        if (eth_config.ip_mode == NETIF_IP_MODE_DHCP) (void)netifapi_dhcp_stop(&eth_netif);
        netifapi_netif_set_link_down(&eth_netif);
        netifapi_netif_set_down(&eth_netif);
    }
    return ret;
}

int w5500_netif_down(void)
{
    struct netif *eth = NULL;
    eth = netif_get_by_index(eth_netif.num + 1);
    if (eth == NULL || eth != &eth_netif) return W5500_ERR_INVALID_STATE;
    if (!netif_is_link_up(eth)) return W5500_OK;

    // Close Socket
    W5500_Macraw_Sock_Close();

    (void)netifapi_dhcp_stop(&eth_netif);
    netifapi_netif_set_link_down(&eth_netif);
    netifapi_netif_set_down(&eth_netif);
    return W5500_OK;
}

void w5500_netif_deinit(void)
{
    struct netif *eth = NULL;
    eth = netif_get_by_index(eth_netif.num + 1);
    if (eth == NULL || eth != &eth_netif) return;

    if (netif_is_link_up(eth)) w5500_netif_down();

    osEventFlagsSet(w5500_events, W5500_EVENT_ISR_TASK_EXIT_REQ);
    osEventFlagsWait(w5500_events, W5500_EVENT_ISR_TASK_EXIT_ACK, osFlagsWaitAny, 3000);
    osThreadTerminate(w5500_isr_thread_ID);
    w5500_isr_thread_ID = NULL;

    osEventFlagsSet(w5500_events, W5500_EVENT_COMM_TASK_EXIT_REQ);
    osEventFlagsWait(w5500_events, W5500_EVENT_COMM_TASK_EXIT_ACK, osFlagsWaitAny, 3000);
    osThreadTerminate(w5500_comm_thread_ID);
    w5500_comm_thread_ID = NULL;
    
    // osMutexAcquire(w5500_sbuf_mutex, osWaitForever);
    
    (void)netifapi_netif_remove(eth);

    // Disable Interrupt
    W5500_Disable_Interrupt();

    W5500_DeInit();

    osEventFlagsDelete(w5500_events);
    w5500_events = NULL;

}

int w5500_netif_config(netif_config_t *netif_cfg)
{   
    uint8_t w5500_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    int ret = 0;
    if (netif_cfg == NULL) return W5500_ERR_INVALID_ARG;
    if (netif_is_link_up(&eth_netif) || netif_is_up(&eth_netif)) return W5500_ERR_INVALID_STATE;

    if (NETIF_MAC_IS_UNICAST(netif_cfg->diy_mac)) {
        if (netif_get_by_index(eth_netif.num + 1) == &eth_netif) {
            ret = W5500_Get_MAC(w5500_mac);
            if (ret != W5500_OK) {
                LOG_DRV_ERROR(NETIF_NAME_STR_FMT ": Get MAC address failed(ret = 0x%lX)!", NETIF_NAME_PARAMETER((&eth_netif)), ret);
                return ret;
            }
            if (memcmp(netif_cfg->diy_mac, w5500_mac, sizeof(w5500_mac))) {
                memcpy(w5500_mac, netif_cfg->diy_mac, sizeof(netif_cfg->diy_mac));
                ret = W5500_Set_MAC(w5500_mac);
                if (ret != W5500_OK) {
                    LOG_DRV_ERROR(NETIF_NAME_STR_FMT ": Set MAC address failed(ret = 0x%lX)!", NETIF_NAME_PARAMETER((&eth_netif)), ret);
                    return ret;
                }
            }
        }
        memcpy(eth_netif.hwaddr, netif_cfg->diy_mac, sizeof(netif_cfg->diy_mac));
        W5500_LOGD(NETIF_NAME_STR_FMT ": MAC Address: " NETIF_MAC_STR_FMT "", NETIF_NAME_PARAMETER((&eth_netif)), NETIF_MAC_PARAMETER(netif_cfg->diy_mac));
    }

    if (netif_cfg->host_name != NULL) {
    #if LWIP_NETIF_HOSTNAME
        eth_netif.hostname = netif_cfg->host_name;
    #endif
    }

    memcpy(&eth_config, netif_cfg, sizeof(netif_config_t));
    return ret;
}

int w5500_netif_info(netif_info_t *netif_info)
{
    struct netif *eth = NULL;
    uint8_t version = 0x00;
    int ret = 0;
    if (netif_info == NULL) return W5500_ERR_INVALID_ARG;

    netif_info->host_name = eth_config.host_name;
    netif_info->if_name = NETIF_NAME_ETH_WAN;
    eth = netif_get_by_index(eth_netif.num + 1);
    if (eth == NULL || eth != &eth_netif) netif_info->state = NETIF_STATE_DEINIT;
    else if (!netif_is_link_up(&eth_netif) || !netif_is_up(&eth_netif)) netif_info->state = NETIF_STATE_DOWN;
    else netif_info->state = NETIF_STATE_UP;
    netif_info->type = NETIF_TYPE_ETH;
    netif_info->rssi = 0;
    netif_info->ip_mode = eth_config.ip_mode;
    memcpy(netif_info->if_mac, eth_netif.hwaddr, sizeof(netif_info->if_mac));
    memcpy(netif_info->ip_addr, &eth_netif.ip_addr, sizeof(netif_info->ip_addr));
    memcpy(netif_info->gw, &eth_netif.gw, sizeof(netif_info->gw));
    memcpy(netif_info->netmask, &eth_netif.netmask, sizeof(netif_info->netmask));
    memset(netif_info->fw_version, 0, sizeof(netif_info->fw_version));
    if (netif_info->state != NETIF_STATE_DEINIT) {
        ret = W5500_Read_Datas(VERSIONR, &version, 1, W5500_SPI_LESS_10B_TIMEOUT);
        if (ret != 1) ret = W5500_ERR_FAILED;
        else {
            snprintf(netif_info->fw_version, sizeof(netif_info->fw_version), "0x%02X", version);
            ret = W5500_OK;
        }
    }

    return ret;
}

netif_state_t w5500_netif_state(void)
{
    struct netif *eth = NULL;

    eth = netif_get_by_index(eth_netif.num + 1);
    if (eth == NULL || eth != &eth_netif) return NETIF_STATE_DEINIT;
    else if (!netif_is_link_up(&eth_netif) || !netif_is_up(&eth_netif)) return NETIF_STATE_DOWN;
    else return NETIF_STATE_UP;
}

struct netif *w5500_netif_ptr(void)
{
    return &eth_netif;
}

int w5500_netif_reset_test(void)
{
    W5500_GPIO_RST_LOW();
    W5500_DELAY_MS(1);
    W5500_GPIO_RST_HIGH();
    W5500_DELAY_MS(1);
    return W5500_OK;
}

int w5500_netif_ctrl(const char *if_name, netif_cmd_t cmd, void *param)
{
    int ret = W5500_ERR_FAILED;
    netif_state_t if_state = NETIF_STATE_DEINIT;
    if (w5500_mutex == NULL) w5500_mutex = osMutexNew(NULL);
    if (w5500_mutex == NULL) return W5500_ERR_MEM;

    osMutexAcquire(w5500_mutex, osWaitForever);
    switch (cmd) {
        case NETIF_CMD_CFG:
            ret = w5500_netif_config((netif_config_t *)param);
            break;
        case NETIF_CMD_INIT:
            ret = w5500_netif_init();
            break;
        case NETIF_CMD_UP:
            ret = w5500_netif_up();
            break;
        case NETIF_CMD_INFO:
            ret = w5500_netif_info((netif_info_t *)param);
            break;
        case NETIF_CMD_DOWN:
            ret = w5500_netif_down();
            break;
        case NETIF_CMD_UNINIT:
            w5500_netif_deinit();
            ret = W5500_OK;
            break;
        case NETIF_CMD_STATE:
            if (param == NULL) ret = W5500_ERR_INVALID_ARG;
            else {
                *((netif_state_t *)param) = w5500_netif_state();
                ret = W5500_OK;
            }
            break;
        case NETIF_CMD_CFG_EX:
            if_state = w5500_netif_state();
            if (if_state == NETIF_STATE_UP) {
                ret = w5500_netif_down();
                if (ret) break;
            }
            ret = w5500_netif_config((netif_config_t *)param);
            if (ret) break;
            if (if_state == NETIF_STATE_UP) ret = w5500_netif_up();
            break;
        default:
            break;
    }
    osMutexRelease(w5500_mutex);
    return ret;
}
