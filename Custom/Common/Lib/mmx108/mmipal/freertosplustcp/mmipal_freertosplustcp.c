/*
 * Copyright 2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * FreeRTOS-plus-TCP support is deprecated. It will be removed in a future release.
 */

#include "mmhal_core.h"
#include "mmipal.h"
#include "mmwlan.h"
#include "mmosal.h"
#include "mmpkt.h"
#include "mmutils.h"

#include "cmsis_gcc.h"

#include "FreeRTOS_DHCP.h"
#include "FreeRTOS_DHCPv6.h"
#include "FreeRTOS_DNS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_IPv4.h"
#include "FreeRTOS_IPv6.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IPv4_Sockets.h"
#include "FreeRTOS_IPv6_Sockets.h"
#include "FreeRTOS_ND.h"

#if (ipconfigUSE_IPv6 && (ipconfigCOMPATIBLE_WITH_SINGLE != 0))
#error ipconfigCOMPATIBLE_WITH_SINGLE must be set to 0
#endif

/** IPv6 prefix (or network) address for link-local address. Expands to fe80:0000:0000:0000. */
#define IPV6_PREFIX "fe80::"
/** IPv6 link-local prefix (or network) address length in bits. */
#define IPV6_PREFIX_LEN_IN_BITS (64)

/** Number of IP addresses associated with an interface. */
#define NUMBER_OF_ENDPOINTS (MMIPAL_MAX_IPV6_ADDRESSES + 1)

static struct mmipal_data
{
    /** The active VIF type if the interface is up. */
    enum mmwlan_vif vif;
    /**
     * This stores the link state for the WLAN connection. I.e., are we associated or not.
     *
     * @note This isn't technically the PHY state in the case of WLAN, but is the equivalent of
     *       Ethernet PHY link state.
     */
    enum mmwlan_link_state phy_link_state;
    /** This stores the IPv4 link state for the IP stack. I.e., do we have an IP address or not. */
    enum mmipal_link_state ip_link_state;
    /** The link status callback function that has been registered. */
    mmipal_link_status_cb_fn_t link_status_callback;
    /** The extended link status callback function that has been registered. */
    mmipal_ext_link_status_cb_fn_t ext_link_status_callback;
    /** Argument for the extended link status callback function that has been registered. */
    void *ext_link_status_callback_arg;
    /** Flag requesting ARP response offload feature */
    bool offload_arp_response;
    /** ARP refresh offload interval in seconds */
    uint32_t offload_arp_refresh_s;
    /** Initial dhcp offload call has been completed */
    bool dhcp_offload_init_complete;
#if ipconfigUSE_IPv4
    enum mmipal_addr_mode ip4_mode;
#endif
#if ipconfigUSE_IPv6
    enum mmipal_ip6_addr_mode ip6_mode;
#endif
#if defined(ipconfigIPv4_BACKWARD_COMPATIBLE) && (ipconfigIPv4_BACKWARD_COMPATIBLE == 0)
    NetworkInterface_t xInterfaces[1];
    NetworkEndPoint_t xEndPoints[NUMBER_OF_ENDPOINTS];
#endif
} mmipal_data = {};

/** Getter function to retrieve the global mmipal data structure.*/
static inline struct mmipal_data *mmipal_get_data(void)
{
    return &mmipal_data;
}

static void invoke_link_status_callback(struct xNetworkEndPoint *ep);

#if ipconfigUSE_IPv4
/**
 * DHCP Lease update callback, invoked when we get a new DHCP lease.
 *
 * @param lease_info The new DHCP lease.
 */
static void mmipal_dhcp_lease_updated(const struct mmwlan_dhcp_lease_info *lease_info, void *arg)
{
    struct mmipal_data *data = mmipal_get_data();
    MM_UNUSED(arg);

    data->dhcp_offload_init_complete = true;

    FreeRTOS_SetEndPointConfiguration(&lease_info->ip4_addr,
                                      &lease_info->mask4_addr,
                                      &lease_info->gw4_addr,
                                      &lease_info->dns4_addr,
                                      data->xInterfaces[0].pxEndPoint);

    /* Copy the current values to the default values. When the link comes up, we trigger a network
     * down event. This will copy the default IP settings to current settings, so we need to update
     * the defaults here. */
    memcpy(&(data->xInterfaces[0].pxEndPoint->ipv4_defaults),
           &(data->xInterfaces[0].pxEndPoint->ipv4_settings),
           sizeof(data->xInterfaces[0].pxEndPoint->ipv4_defaults));

    /* We trigger a network down event  trigger reinitialization. */
    invoke_link_status_callback(data->xInterfaces[0].pxEndPoint);
}

#endif

static void invoke_link_status_callback(struct xNetworkEndPoint *ep)
{
    struct mmipal_data *data = mmipal_get_data();
    struct mmipal_link_status link_status;
    const char *ret;

    link_status.link_state = ep->bits.bEndPointUp ? MMIPAL_LINK_UP : MMIPAL_LINK_DOWN;

    /* Currently we do not support Link status callbacks for the IPv6 endpoint */
    if (ep->bits.bIPv6)
    {
        return;
    }

#if ipconfigUSE_IPv4
    if (!data->dhcp_offload_init_complete && (data->ip4_mode == MMIPAL_DHCP_OFFLOAD))
    {
        /* Do not proceed if DHCP offload is not yet complete */
        return;
    }

    /* If FreeRTOS_inet_ntop4() fails it returns NULL. It should not fail
     * unless the buffer we have provided is too small (<16 bytes). However,
     * MMIPAL_IPADDR_STR_MAXLEN should be large enough to contain any IPv4/6
     * address as a string. */
    ret = FreeRTOS_inet_ntop4(&ep->ipv4_settings.ulIPAddress,
                              link_status.ip_addr,
                              MMIPAL_IPADDR_STR_MAXLEN);
    MMOSAL_ASSERT(ret != NULL);
    ret = FreeRTOS_inet_ntop4(&ep->ipv4_settings.ulNetMask,
                              link_status.netmask,
                              MMIPAL_IPADDR_STR_MAXLEN);
    MMOSAL_ASSERT(ret != NULL);
    ret = FreeRTOS_inet_ntop4(&ep->ipv4_settings.ulGatewayAddress,
                              link_status.gateway,
                              MMIPAL_IPADDR_STR_MAXLEN);
    MMOSAL_ASSERT(ret != NULL);
#else
    MM_UNUSED(ret);
#endif

    /* This check eliminates multiple link down notifications */
    if ((link_status.link_state == MMIPAL_LINK_UP) || (data->ip_link_state == MMIPAL_LINK_UP))
    {
#if ipconfigUSE_IPv4
        if (link_status.link_state == MMIPAL_LINK_UP)
        {
            /* Check if ARP response offload feature is enabled */
            if (data->offload_arp_response)
            {
                mmwlan_enable_arp_response_offload(ep->ipv4_settings.ulIPAddress);
            }

            /* Check if ARP refresh offload feature is enabled */
            if (data->offload_arp_refresh_s > 0)
            {
                mmwlan_enable_arp_refresh_offload(data->offload_arp_refresh_s,
                                                  ep->ipv4_settings.ulGatewayAddress,
                                                  true);
            }
        }
#endif

        if (data->link_status_callback)
        {
            data->link_status_callback(&link_status);
        }
        if (data->ext_link_status_callback)
        {
            data->ext_link_status_callback(&link_status, data->ext_link_status_callback_arg);
        }
    }
    data->ip_link_state = link_status.link_state;
}

static void mmipal_mmwlan_vif_state_change_handler(const struct mmwlan_vif_state *state, void *arg)
{
    struct mmipal_data *data = mmipal_get_data();
    NetworkInterface_t *pxInterface = (NetworkInterface_t *)arg;
    enum mmwlan_link_state old_link_state = data->phy_link_state;

    switch (state->link_state)
    {
        case MMWLAN_LINK_DOWN:
            if (state->vif == data->vif)
            {
                data->dhcp_offload_init_complete = false;
                data->phy_link_state = state->link_state;
            }
            else
            {
                printf("VIF down for other VIF\n");
            }
            break;

        case MMWLAN_LINK_UP:
            if (data->phy_link_state == MMWLAN_LINK_DOWN)
            {
                data->vif = state->vif;
                data->phy_link_state = state->link_state;
#if ipconfigUSE_IPv4
                if (data->ip4_mode == MMIPAL_DHCP_OFFLOAD && !data->dhcp_offload_init_complete)
                {
                    /* Initialize DHCP offload on link up */
                    if (mmwlan_enable_dhcp_offload(mmipal_dhcp_lease_updated, NULL) !=
                        MMWLAN_SUCCESS)
                    {
                        printf("Failed to enable DHCP offload!\n");
                    }
                }
#endif
            }
            break;
    }

    if (data->phy_link_state != old_link_state)
    {
        /* We trigger a network down event even if the link is coming up, since the link
         * down event will trigger reinitialization. */
        FreeRTOS_NetworkDown(pxInterface);
    }
}

void vApplicationIPNetworkEventHook_Multi(eIPCallbackEvent_t eNetworkEvent,
                                          struct xNetworkEndPoint *pxEndPoint)
{
    struct mmipal_data *data = mmipal_get_data();
    switch (eNetworkEvent)
    {
        case eNetworkUp:
            if (pxEndPoint->bits.bIPv6)
            {
                FreeRTOS_debug_printf(("Recieved eNetworkUp event for IPv6 endpoint.\n"));
            }
            else
            {
                FreeRTOS_debug_printf(("Recieved eNetworkUp event for IPv4 endpoint.\n"));
            }
            break;

        case eNetworkDown:
            if (pxEndPoint->bits.bIPv6)
            {
                FreeRTOS_debug_printf(("Recieved eNetworkDown event for IPv6 endpoint.\n"));
            }
            else
            {
                FreeRTOS_debug_printf(("Recieved eNetworkDown event for IPv4 endpoint.\n"));
            }
            break;
    }

    if (data->link_status_callback || data->ext_link_status_callback)
    {
        invoke_link_status_callback(pxEndPoint);
    }
}

static void mmipal_mmwlan_rx_handler(struct mmpkt *mmpkt,
                                     const struct mmwlan_rx_metadata *metadata,
                                     void *arg)
{
    struct mmipal_data *data = mmipal_get_data();
    NetworkInterface_t *pxInterface = (NetworkInterface_t *)arg;

    if (metadata->vif != data->vif)
    {
        FreeRTOS_debug_printf(("RX event for other interface.\n"));
        mmpkt_release(mmpkt);
        return;
    }

    NetworkBufferDescriptor_t *pxBufferDescriptor;
    IPStackEvent_t xRxEvent;
    struct mmpktview *pktview = mmpkt_open(mmpkt);
    size_t xBytesReceived = mmpkt_get_data_length(pktview);

    if (xBytesReceived == 0)
    {
        goto exit;
    }

    pxBufferDescriptor = pxGetNetworkBufferWithDescriptor(xBytesReceived, 0);
    if (pxBufferDescriptor == NULL)
    {
        iptraceETHERNET_RX_EVENT_LOST();
        goto exit;
    }

    memcpy(pxBufferDescriptor->pucEthernetBuffer, mmpkt_get_data_start(pktview), xBytesReceived);
    pxBufferDescriptor->xDataLength = xBytesReceived;
    pxBufferDescriptor->pxInterface = pxInterface;

    if (eConsiderFrameForProcessing(pxBufferDescriptor->pucEthernetBuffer) != eProcessBuffer)
    {
        vReleaseNetworkBufferAndDescriptor(pxBufferDescriptor);
        goto exit;
    }

    pxBufferDescriptor->pxEndPoint =
        FreeRTOS_MatchingEndpoint(pxInterface, pxBufferDescriptor->pucEthernetBuffer);
    if (pxBufferDescriptor->pxEndPoint == NULL)
    {
        vReleaseNetworkBufferAndDescriptor(pxBufferDescriptor);
        goto exit;
    }

    xRxEvent.eEventType = eNetworkRxEvent;
    xRxEvent.pvData = (void *)pxBufferDescriptor;

    if (xSendEventStructToIPTask(&xRxEvent, 0) == pdFALSE)
    {
        vReleaseNetworkBufferAndDescriptor(pxBufferDescriptor);
        iptraceETHERNET_RX_EVENT_LOST();
    }
    else
    {
        iptraceNETWORK_INTERFACE_RECEIVE();
    }

exit:
    mmpkt_close(&pktview);
    mmpkt_release(mmpkt);
}

static BaseType_t mmipal_mmwlan_init(NetworkInterface_t *pxInterface)
{
    struct mmipal_data *data = mmipal_get_data();
    enum mmwlan_status status;
    uint8_t mac_addr[6] = { 0 };
    NetworkEndPoint_t *pxEndPoint;

    status = mmwlan_register_vif_state_cb(MMWLAN_VIF_UNSPECIFIED,
                                          mmipal_mmwlan_vif_state_change_handler,
                                          pxInterface);
    MMOSAL_ASSERT(status == MMWLAN_SUCCESS);

    status = mmwlan_register_rx_pkt_ext_cb(MMWLAN_VIF_UNSPECIFIED,
                                           mmipal_mmwlan_rx_handler,
                                           pxInterface);
    MMOSAL_ASSERT(status == MMWLAN_SUCCESS);

    if (data->phy_link_state == MMWLAN_LINK_DOWN)
    {
        data->vif = MMWLAN_VIF_UNSPECIFIED;
        return pdFAIL;
    }

    MMOSAL_ASSERT(data->vif != MMWLAN_VIF_UNSPECIFIED);

    status = mmwlan_get_vif_mac_addr(data->vif, mac_addr);
    MMOSAL_ASSERT(status == MMWLAN_SUCCESS);

    pxEndPoint = FreeRTOS_FirstEndPoint(pxInterface);
    MMOSAL_ASSERT(pxEndPoint != NULL);

    if (memcmp((uint8_t *)pxEndPoint->xMACAddress.ucBytes, mac_addr, 6))
    {
        FreeRTOS_debug_printf(("MAC addr mismatch\n"));
    }

    return pdPASS;
}

static BaseType_t mmipal_mmwlan_output(NetworkInterface_t *pxInterface,
                                       NetworkBufferDescriptor_t *const pxBuffer,
                                       BaseType_t bReleaseAfterSend)
{
    MM_UNUSED(pxInterface);
    struct mmipal_data *data = mmipal_get_data();

    enum mmwlan_status status = mmwlan_tx_wait_until_ready(MMWLAN_TX_DEFAULT_TIMEOUT_MS);
    if (status != MMWLAN_SUCCESS)
    {
        return status;
    }

    struct mmpkt *pkt = mmwlan_alloc_mmpkt_for_tx(pxBuffer->xDataLength, MMWLAN_TX_DEFAULT_QOS_TID);
    if (pkt == NULL)
    {
        return MMWLAN_NO_MEM;
    }

    struct mmpktview *pktview = mmpkt_open(pkt);
    mmpkt_append_data(pktview, pxBuffer->pucEthernetBuffer, pxBuffer->xDataLength);
    mmpkt_close(&pktview);

    if (bReleaseAfterSend != pdFALSE)
    {
        vReleaseNetworkBufferAndDescriptor(pxBuffer);
    }

    struct mmwlan_tx_metadata metadata = MMWLAN_TX_METADATA_INIT;
    metadata.vif = data->vif;
    mmwlan_tx_pkt(pkt, &metadata);

    return pdPASS;
}

static BaseType_t mmipal_mmwlan_get_phy_link_status(NetworkInterface_t *pxInterface)
{
    struct mmipal_data *data = mmipal_get_data();
    MM_UNUSED(pxInterface);

    if (data->phy_link_state == MMWLAN_LINK_UP)
    {
        return pdTRUE;
    }
    else
    {
        return pdFALSE;
    }
}

static NetworkInterface_t *mmipal_FillInterfaceDescriptor(NetworkInterface_t *pxInterface)
{
    static char name[] = "mmwlan0";

    memset(pxInterface, 0, sizeof(*pxInterface));
    pxInterface->pcName = name;
    pxInterface->pvArgument = NULL;
    pxInterface->pfInitialise = mmipal_mmwlan_init;
    pxInterface->pfOutput = mmipal_mmwlan_output;
    pxInterface->pfGetPhyLinkStatus = mmipal_mmwlan_get_phy_link_status;

    FreeRTOS_AddNetworkInterface(pxInterface);

    return pxInterface;
}

BaseType_t xApplicationGetRandomNumber(uint32_t *pulNumber)
{
    *pulNumber = mmhal_random_u32(0, UINT32_MAX);
    return pdTRUE;
}

uint32_t ulApplicationGetNextSequenceNumber(uint32_t ulSourceAddress,
                                            uint16_t usSourcePort,
                                            uint32_t ulDestinationAddress,
                                            uint16_t usDestinationPort)
{
    MM_UNUSED(ulSourceAddress);
    MM_UNUSED(usSourcePort);
    MM_UNUSED(ulDestinationAddress);
    MM_UNUSED(usDestinationPort);
    return mmhal_random_u32(0, UINT32_MAX);
}

/* Time in seconds since 1970-1-1. */
uint32_t ulApplicationTimeHook(void)
{
    /* We need to return correct time in seconds. */
    return (1692757677ul + (mmosal_get_time_ms() / 1000));
}

#if ipconfigUSE_IPv4
enum mmipal_status mmipal_get_ip_config(struct mmipal_ip_config *config)
{
    struct mmipal_data *data = mmipal_get_data();
    uint32_t ip_addr = 0;
    uint32_t netmask = 0;
    uint32_t gateway_addr = 0;

    config->mode = data->ip4_mode;

    FreeRTOS_GetEndPointConfiguration(&ip_addr,
                                      &netmask,
                                      &gateway_addr,
                                      NULL,
                                      data->xInterfaces[0].pxEndPoint);

    if (FreeRTOS_inet_ntop4(&ip_addr, config->ip_addr, MMIPAL_IPADDR_STR_MAXLEN) == NULL)
    {
        FreeRTOS_debug_printf(("ip_addr failed\n"));
    }

    if (FreeRTOS_inet_ntop4(&netmask, config->netmask, MMIPAL_IPADDR_STR_MAXLEN) == NULL)
    {
        FreeRTOS_debug_printf(("netmask failed\n"));
    }

    if (FreeRTOS_inet_ntop4(&gateway_addr, config->gateway_addr, MMIPAL_IPADDR_STR_MAXLEN) == NULL)
    {
        FreeRTOS_debug_printf(("gateway failed\n"));
    }

    return MMIPAL_SUCCESS;
}

enum mmipal_status mmipal_set_ip_config(const struct mmipal_ip_config *config)
{
    struct mmipal_data *data = mmipal_get_data();
    uint32_t ip_addr = 0;
    uint32_t netmask = 0;
    uint32_t gateway_addr = 0;
    uint32_t dns_server_addr = 0;
    BaseType_t ret = pdFAIL;

    if (config->mode != MMIPAL_DHCP_OFFLOAD && data->ip4_mode == MMIPAL_DHCP_OFFLOAD)
    {
        printf("Once enabled DHCP offload mode cannot be disabled\n");
        return MMIPAL_NOT_SUPPORTED;
    }

    switch (config->mode)
    {
        case MMIPAL_DISABLED:
            printf("%s mode not supported\n", "DISABLED");
            return MMIPAL_INVALID_ARGUMENT;

        case MMIPAL_AUTOIP:
            printf("%s mode not supported\n", "AutoIP");
            return MMIPAL_INVALID_ARGUMENT;

        case MMIPAL_DHCP_OFFLOAD:
            /* Currently we only support enabling DHCP offload when initialising */
            printf("%s mode not supported\n", "DHCP_OFFLOAD");
            return MMIPAL_INVALID_ARGUMENT;

        case MMIPAL_STATIC:
            ret = FreeRTOS_inet_pton4(config->ip_addr, (void *)&ip_addr);
            if (ret == pdFAIL)
            {
                return MMIPAL_INVALID_ARGUMENT;
            }
            ret = FreeRTOS_inet_pton4(config->netmask, (void *)&netmask);
            if (ret == pdFAIL)
            {
                return MMIPAL_INVALID_ARGUMENT;
            }
            ret = FreeRTOS_inet_pton4(config->gateway_addr, (void *)&gateway_addr);
            if (ret == pdFAIL)
            {
                return MMIPAL_INVALID_ARGUMENT;
            }

            data->xEndPoints[0].bits.bWantDHCP = pdFALSE;
            break;

        case MMIPAL_DHCP:
            data->xEndPoints[0].bits.bWantDHCP = pdTRUE;
            break;
    }

    data->ip4_mode = config->mode;

    FreeRTOS_GetEndPointConfiguration(NULL,
                                      NULL,
                                      NULL,
                                      &dns_server_addr,
                                      data->xInterfaces[0].pxEndPoint);

    FreeRTOS_SetEndPointConfiguration(&ip_addr,
                                      &netmask,
                                      &gateway_addr,
                                      &dns_server_addr,
                                      data->xInterfaces[0].pxEndPoint);

    /* Copy the current values to the default values. When the link comes up, we trigger a
     * network down event. This will copy the default IP settings to current settings, so we
     * need to update the defaults here. */
    memcpy(&(data->xInterfaces[0].pxEndPoint->ipv4_defaults),
           &(data->xInterfaces[0].pxEndPoint->ipv4_settings),
           sizeof(data->xInterfaces[0].pxEndPoint->ipv4_defaults));

    /* We trigger a network down event even if the link is coming up, since the link down
     * event will trigger reinitialization. */
    FreeRTOS_NetworkDown(&(data->xInterfaces[0]));

    return MMIPAL_SUCCESS;
}

enum mmipal_status mmipal_get_ip_broadcast_addr(mmipal_ip_addr_t broadcast_addr)
{
    struct mmipal_data *data = mmipal_get_data();
    uint32_t ip_addr = 0;
    uint32_t netmask = 0;
    uint32_t gateway_addr = 0;
    uint32_t broadcast;

    FreeRTOS_GetEndPointConfiguration(&ip_addr,
                                      &netmask,
                                      &gateway_addr,
                                      NULL,
                                      data->xInterfaces[0].pxEndPoint);
    broadcast = (ip_addr & netmask) | (0xffffffff & ~netmask);

    if (FreeRTOS_inet_ntop4(&broadcast, broadcast_addr, MMIPAL_IPADDR_STR_MAXLEN) == NULL)
    {
        return MMIPAL_NO_MEM;
    }

    return MMIPAL_SUCCESS;
}

#else
enum mmipal_status mmipal_get_ip_config(struct mmipal_ip_config *config)
{
    if (config == NULL)
    {
        return MMIPAL_INVALID_ARGUMENT;
    }
    memset(config, 0, sizeof(*config));
    config->mode = MMIPAL_DISABLED;
    return MMIPAL_SUCCESS;
}

enum mmipal_status mmipal_set_ip_config(const struct mmipal_ip_config *config)
{
    if (config == NULL)
    {
        return MMIPAL_INVALID_ARGUMENT;
    }
    if (config->mode != MMIPAL_DISABLED)
    {
        printf("ipconfigUSE_IPv4 is not enabled\n");
        return MMIPAL_NOT_SUPPORTED;
    }
    return MMIPAL_SUCCESS;
}

enum mmipal_status mmipal_get_ip_broadcast_addr(mmipal_ip_addr_t broadcast_addr)
{
    MM_UNUSED(broadcast_addr);
    printf("ipconfigUSE_IPv4 is not enabled\n");
    return MMIPAL_NOT_SUPPORTED;
}

#endif

#if ipconfigUSE_IPv6
enum mmipal_status mmipal_get_ip6_config(struct mmipal_ip6_config *config)
{
    struct mmipal_data *data = mmipal_get_data();
    unsigned ii = 0;
    NetworkEndPoint_t *pxEndPoint = NULL;

    pxEndPoint = FreeRTOS_FirstEndPoint_IPv6(&data->xInterfaces[0]);
    if (pxEndPoint == NULL)
    {
        printf("IPv6 endpoint not found\n");
        return MMIPAL_NOT_SUPPORTED;
    }

    if (config == NULL)
    {
        return MMIPAL_INVALID_ARGUMENT;
    }

    memset(config, 0, sizeof(*config));
    config->ip6_mode = data->ip6_mode;

    while (pxEndPoint != NULL)
    {
        if (pxEndPoint->bits.bIPv6)
        {
            if (FreeRTOS_inet_ntop6(&pxEndPoint->ipv6_settings.xIPAddress.ucBytes,
                                    (char *)&config->ip6_addr[ii],
                                    MMIPAL_IPADDR_STR_MAXLEN) != NULL)
            {
                ii++;
            }
        }
        pxEndPoint = FreeRTOS_NextEndPoint(&data->xInterfaces[0], pxEndPoint);
    }

    return MMIPAL_SUCCESS;
}

enum mmipal_status mmipal_set_ip6_config(const struct mmipal_ip6_config *config)
{
    struct mmipal_data *data = mmipal_get_data();
    BaseType_t ret = pdFAIL;
    IPv6_Address_t ipv6_addr = { 0 };
    IPv6_Address_t ipv6_prefix = { 0 };
    uint8_t ucMACAddress[6];
    unsigned ii;

    if (config->ip6_mode == MMIPAL_IP6_STATIC)
    {
        ret = FreeRTOS_inet_pton6((const char *)&config->ip6_addr[0], ipv6_addr.ucBytes);
        if ((ret == pdFAIL) || (xIPv6_GetIPType(&ipv6_addr) != eIPv6_LinkLocal))
        {
            printf("First address must be linklocal address (address start with fe80)\n");
            return MMIPAL_INVALID_ARGUMENT;
        }

        data->xEndPoints[1].pxNext = NULL;
        data->xEndPoints[1].bits.bIPv6 = pdTRUE;
        data->xEndPoints[1].bits.bWantDHCP = pdFALSE;
        memcpy(data->xEndPoints[1].ipv6_settings.xIPAddress.ucBytes,
               ipv6_addr.ucBytes,
               sizeof(data->xEndPoints[1].ipv6_settings.xIPAddress.ucBytes));
        /* Copy the current values to the default values. When the link comes up, we trigger a
         * network down event. This will copy the default IP settings to current settings, so we
         * need to update the defaults here. */
        memcpy(&(data->xEndPoints[1].ipv6_defaults),
               &(data->xEndPoints[1].ipv6_settings),
               sizeof(data->xEndPoints[1].ipv6_defaults));

        mmwlan_get_mac_addr(ucMACAddress);
        for (ii = 1; ii < MMIPAL_MAX_IPV6_ADDRESSES; ii++)
        {
            ret = FreeRTOS_inet_pton6((const char *)&config->ip6_addr[ii], ipv6_addr.ucBytes);
            if ((ret == pdFAIL) || (xIPv6_GetIPType(&ipv6_addr) == eIPv6_Unknown))
            {
                break;
            }

            FreeRTOS_FillEndPoint_IPv6(&data->xInterfaces[0],
                                       &data->xEndPoints[(ii + 1)],
                                       &ipv6_addr,
                                       NULL,
                                       0,
                                       NULL,
                                       NULL,
                                       ucMACAddress);
            memcpy(data->xEndPoints[(ii + 1)].ipv6_settings.xIPAddress.ucBytes,
                   ipv6_addr.ucBytes,
                   sizeof(data->xEndPoints[(ii + 1)].ipv6_settings.xIPAddress.ucBytes));
        }
    }
    else
    {
        FreeRTOS_inet_pton6(IPV6_PREFIX, ipv6_prefix.ucBytes);
        ret = FreeRTOS_CreateIPv6Address(&ipv6_addr, &ipv6_prefix, IPV6_PREFIX_LEN_IN_BITS, pdTRUE);
        if (ret == pdFAIL)
        {
            printf("Failed to create IPv6 Address\n");
        }
        data->xEndPoints[1].pxNext = NULL;
        data->xEndPoints[1].bits.bIPv6 = pdTRUE;
        data->xEndPoints[1].bits.bWantDHCP = pdFALSE;
        if (config->ip6_mode == MMIPAL_IP6_DHCP6_STATELESS)
        {
            if (ipconfigUSE_DHCPv6 == 0)
            {
                printf("DHCP6_STATELESS not compiled in\n");
                return MMIPAL_NOT_SUPPORTED;
            }
            data->xEndPoints[1].bits.bWantDHCP = pdTRUE;
        }
        memcpy(data->xEndPoints[1].ipv6_settings.xPrefix.ucBytes,
               ipv6_prefix.ucBytes,
               sizeof(data->xEndPoints[1].ipv6_settings.xPrefix.ucBytes));
        memcpy(data->xEndPoints[1].ipv6_settings.xIPAddress.ucBytes,
               ipv6_addr.ucBytes,
               sizeof(data->xEndPoints[1].ipv6_settings.xIPAddress.ucBytes));
        /* Copy the current values to the default values. When the link comes up, we trigger a
         * network down event. This will copy the default IP settings to current settings, so we
         * need to update the defaults here. */
        memcpy(&(data->xEndPoints[1].ipv6_defaults),
               &(data->xEndPoints[1].ipv6_settings),
               sizeof(data->xEndPoints[1].ipv6_defaults));
    }

    data->ip6_mode = config->ip6_mode;
    return MMIPAL_SUCCESS;
}

#else
enum mmipal_status mmipal_get_ip6_config(struct mmipal_ip6_config *config)
{
    if (config == NULL)
    {
        return MMIPAL_INVALID_ARGUMENT;
    }
    memset(config, 0, sizeof(*config));
    config->ip6_mode = MMIPAL_IP6_DISABLED;
    return MMIPAL_SUCCESS;
}

enum mmipal_status mmipal_set_ip6_config(const struct mmipal_ip6_config *config)
{
    if (config == NULL)
    {
        return MMIPAL_INVALID_ARGUMENT;
    }
    if (config->ip6_mode != MMIPAL_IP6_DISABLED)
    {
        printf("ipconfigUSE_IPv6 is not enabled\n");
        return MMIPAL_NOT_SUPPORTED;
    }
    return MMIPAL_SUCCESS;
}

#endif

void mmipal_set_link_status_callback(mmipal_link_status_cb_fn_t fn)
{
#if ipconfigUSE_IPv4
    struct mmipal_data *data = mmipal_get_data();
    data->link_status_callback = fn;
#else
    MM_UNUSED(fn);
#endif
}

void mmipal_set_ext_link_status_callback(mmipal_ext_link_status_cb_fn_t fn, void *arg)
{
#if ipconfigUSE_IPv4
    struct mmipal_data *data = mmipal_get_data();
    data->ext_link_status_callback = fn;
    data->ext_link_status_callback_arg = arg;
#else
    MM_UNUSED(fn);
    MM_UNUSED(arg);
#endif
}

enum mmipal_status mmipal_add_static_arp_entry(const struct mmipal_arp_config *config)
{
    MM_UNUSED(config);

    printf("Static ARP is not supported\n");

    return MMIPAL_NOT_SUPPORTED;
}

enum mmipal_status mmipal_remove_static_arp_entry(const struct mmipal_arp_config *config)
{
    MM_UNUSED(config);

    printf("Static ARP is not supported\n");

    return MMIPAL_NOT_SUPPORTED;
}

enum mmipal_status mmipal_init(const struct mmipal_init_args *args)
{
    struct mmipal_data *data = mmipal_get_data();
    enum mmwlan_status result = MMWLAN_ERROR;
    BaseType_t ret = pdFAIL;
    uint8_t ucMACAddress[6];
#if ipconfigUSE_IPv4
    uint32_t ip_addr = 0;
    uint32_t netmask = 0;
    uint32_t gateway_addr = 0;
#endif
#if ipconfigUSE_IPv6
    IPv6_Address_t ipv6_addr = { 0 };
    IPv6_Address_t ipv6_prefix = { 0 };
    uint32_t ipv6_prefix_len = 0;
    data->ip6_mode = args->ip6_mode;
#endif
#if ipconfigUSE_IPv4
    data->ip4_mode = args->mode;
#endif
    data->link_status_callback = NULL;

    data->offload_arp_response = args->offload_arp_response;
    data->offload_arp_refresh_s = args->offload_arp_refresh_s;

    /* Validate arguments */
#if ipconfigUSE_IPv4
    data->xEndPoints[0].bits.bWantDHCP = pdFALSE;

    switch (args->mode)
    {
        case MMIPAL_DISABLED:
            printf("%s mode not supported\n", "DISABLED");
            return MMIPAL_NOT_SUPPORTED;

        case MMIPAL_DHCP_OFFLOAD:
        case MMIPAL_STATIC:
            ret = FreeRTOS_inet_pton4(args->ip_addr, (void *)&ip_addr);
            if (ret == pdFAIL)
            {
                return MMIPAL_INVALID_ARGUMENT;
            }

            ret = FreeRTOS_inet_pton4(args->netmask, (void *)&netmask);
            if (ret == pdFAIL)
            {
                return MMIPAL_INVALID_ARGUMENT;
            }
            ret = FreeRTOS_inet_pton4(args->gateway_addr, (void *)&gateway_addr);
            if (ret == pdFAIL)
            {
                return MMIPAL_INVALID_ARGUMENT;
            }

            if (ip_addr == FREERTOS_INADDR_ANY)
            {
                printf("IP address not specified\n");
                return MMIPAL_INVALID_ARGUMENT;
            }
            break;

        case MMIPAL_DHCP:
            if (ipconfigUSE_DHCP == 0)
            {
                printf("DHCP not compiled in\n");
                return MMIPAL_NOT_SUPPORTED;
            }
            break;

        case MMIPAL_AUTOIP:
            printf("%s mode not supported\n", "AutoIP");
            return MMIPAL_NOT_SUPPORTED;
    }
#endif

#if ipconfigUSE_IPv6
    data->xEndPoints[1].bits.bWantDHCP = pdFALSE;

    FreeRTOS_inet_pton6(IPV6_PREFIX, ipv6_prefix.ucBytes);
    ipv6_prefix_len = IPV6_PREFIX_LEN_IN_BITS;
    ret = FreeRTOS_CreateIPv6Address(&ipv6_addr, &ipv6_prefix, ipv6_prefix_len, pdTRUE);
    if (ret == pdFAIL)
    {
        printf("Failed to create IPv6 Address\n");
    }

    switch (args->ip6_mode)
    {
        case MMIPAL_IP6_DISABLED:
            break;

        case MMIPAL_IP6_STATIC:
            ret = FreeRTOS_inet_pton6(args->ip6_addr, ipv6_addr.ucBytes);
            if (ret == pdFAIL)
            {
                return MMIPAL_INVALID_ARGUMENT;
            }

            if (xIPv6_GetIPType(&ipv6_addr) != eIPv6_LinkLocal)
            {
                printf("First IPv6 address must be linklocal address (address start with fe80)\n");
                return MMIPAL_INVALID_ARGUMENT;
            }

            memset(&ipv6_prefix, 0, sizeof(ipv6_prefix));
            ipv6_prefix_len = 0;
            break;

        case MMIPAL_IP6_AUTOCONFIG:
            if (ipconfigDHCP_FALL_BACK_AUTO_IP == 0)
            {
                printf("AUTOCONFIG not compiled in\n");
                return MMIPAL_NOT_SUPPORTED;
            }
            break;

        case MMIPAL_IP6_DHCP6_STATELESS:
            if (ipconfigUSE_DHCPv6 == 0)
            {
                printf("DHCP6_STATELESS not compiled in\n");
                return MMIPAL_NOT_SUPPORTED;
            }
            break;
    }
#endif

    mmipal_FillInterfaceDescriptor(&data->xInterfaces[0]);

    struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
    result = mmwlan_boot(&boot_args);
    if (result != MMWLAN_SUCCESS)
    {
        printf("Failed to boot MMWLAN (result=%d)", result);
        MMOSAL_ASSERT(false);
    }

    result = mmwlan_get_mac_addr(ucMACAddress);
    if (result != MMWLAN_SUCCESS)
    {
        printf("Failed to get MAC address (result=%d)", result);
        MMOSAL_ASSERT(false);
    }

#if ipconfigUSE_IPv4
    FreeRTOS_FillEndPoint(&data->xInterfaces[0],
                          &data->xEndPoints[0],
                          (const uint8_t *)&ip_addr,
                          (const uint8_t *)&netmask,
                          (const uint8_t *)&gateway_addr,
                          (const uint8_t *)&gateway_addr,
                          ucMACAddress);
    if (args->mode == MMIPAL_DHCP)
    {
        data->xEndPoints[0].bits.bWantDHCP = pdTRUE;
    }
#endif
#if ipconfigUSE_IPv6
    FreeRTOS_FillEndPoint_IPv6(&data->xInterfaces[0],
                               &data->xEndPoints[1],
                               &ipv6_addr,
                               &ipv6_prefix,
                               ipv6_prefix_len,
                               NULL,
                               NULL,
                               ucMACAddress);
    if (args->ip6_mode == MMIPAL_IP6_DHCP6_STATELESS)
    {
        data->xEndPoints[1].bits.bWantDHCP = pdTRUE;
    }
#endif

    ret = FreeRTOS_IPInit_Multi();
    if (ret == pdFALSE)
    {
        return MMIPAL_NOT_SUPPORTED;
    }

    printf("Morse FreeRTOS+ TCP interface initialised. MAC address "
           "%02x:%02x:%02x:%02x:%02x:%02x\n",
           ucMACAddress[0],
           ucMACAddress[1],
           ucMACAddress[2],
           ucMACAddress[3],
           ucMACAddress[4],
           ucMACAddress[5]);

    return MMIPAL_SUCCESS;
}

void mmipal_get_link_packet_counts(uint32_t *tx_packets, uint32_t *rx_packets)
{
    MM_UNUSED(tx_packets);
    MM_UNUSED(rx_packets);
}

void mmipal_set_tx_qos_tid(uint8_t tid)
{
    MM_UNUSED(tid);
}

enum mmipal_link_state mmipal_get_link_state(void)
{
    struct mmipal_data *data = mmipal_get_data();
    return data->xInterfaces[0].pxEndPoint->bits.bEndPointUp ? MMIPAL_LINK_UP : MMIPAL_LINK_DOWN;
}

enum mmipal_status mmipal_get_local_addr(mmipal_ip_addr_t local_addr,
                                         const mmipal_ip_addr_t dest_addr)
{
    uint32_t ulIP_local_addr = 0;

#if ipconfigUSE_IPv6
    struct mmipal_data *data = mmipal_get_data();
    IPv6_Address_t ipv6_dest_addr;
    BaseType_t ret;
    NetworkEndPoint_t *pxEndPoint;
    ret = FreeRTOS_inet_pton6(dest_addr, ipv6_dest_addr.ucBytes);
    if (ret == pdPASS)
    {
        pxEndPoint = FreeRTOS_FirstEndPoint_IPv6(&data->xInterfaces[0]);
        if (pxEndPoint == NULL)
        {
            return MMIPAL_NOT_SUPPORTED;
        }

        if (FreeRTOS_inet_ntop6(&pxEndPoint->ipv6_settings.xIPAddress.ucBytes,
                                local_addr,
                                MMIPAL_IPADDR_STR_MAXLEN) == NULL)
        {
            return MMIPAL_NOT_SUPPORTED;
        }
        else
        {
            return MMIPAL_SUCCESS;
        }
    }
#else
    MM_UNUSED(dest_addr);
#endif

#if ipconfigUSE_IPv4
    ulIP_local_addr = FreeRTOS_GetIPAddress();
    if (ulIP_local_addr == 0)
    {
        return MMIPAL_NOT_SUPPORTED;
    }

    if (FreeRTOS_inet_ntop4(&ulIP_local_addr, local_addr, MMIPAL_IPADDR_STR_MAXLEN) == NULL)
    {
        return MMIPAL_NO_MEM;
    }
    else
    {
        return MMIPAL_SUCCESS;
    }
#else
    MM_UNUSED(local_addr);
    MM_UNUSED(ulIP_local_addr);
    return MMIPAL_NOT_SUPPORTED;
#endif
}

enum mmipal_status mmipal_set_dns_server(uint8_t index, const mmipal_ip_addr_t addr)
{
#if ipconfigUSE_IPv4
    struct mmipal_data *data = mmipal_get_data();
    BaseType_t ret;
    uint32_t ip_addr = 0;
    uint32_t netmask = 0;
    uint32_t gateway_addr = 0;
    uint32_t dns_server_addr = 0;

    if (index > 0)
    {
        return MMIPAL_INVALID_ARGUMENT;
    }

    ret = FreeRTOS_inet_pton4(addr, (void *)&dns_server_addr);
    if (ret != pdPASS)
    {
        return MMIPAL_INVALID_ARGUMENT;
    }

    FreeRTOS_GetEndPointConfiguration(&ip_addr,
                                      &netmask,
                                      &gateway_addr,
                                      NULL,
                                      data->xInterfaces[0].pxEndPoint);

    FreeRTOS_SetEndPointConfiguration(&ip_addr,
                                      &netmask,
                                      &gateway_addr,
                                      &dns_server_addr,
                                      data->xInterfaces[0].pxEndPoint);

    return MMIPAL_SUCCESS;
#else
    MM_UNUSED(index);
    MM_UNUSED(addr);
    return MMIPAL_NOT_SUPPORTED;
#endif
}

enum mmipal_status mmipal_get_dns_server(uint8_t index, mmipal_ip_addr_t addr)
{
#if ipconfigUSE_IPv4
    struct mmipal_data *data = mmipal_get_data();
    uint32_t dns_server_addr = 0;

    if (index > 0)
    {
        return MMIPAL_INVALID_ARGUMENT;
    }

    FreeRTOS_GetEndPointConfiguration(NULL,
                                      NULL,
                                      NULL,
                                      &dns_server_addr,
                                      data->xInterfaces[0].pxEndPoint);

    if (FreeRTOS_inet_ntop4(&dns_server_addr, addr, MMIPAL_IPADDR_STR_MAXLEN) == NULL)
    {
        return MMIPAL_NO_MEM;
    }
    else
    {
        return MMIPAL_SUCCESS;
    }
#else
    MM_UNUSED(index);
    MM_UNUSED(addr);
    return MMIPAL_NOT_SUPPORTED;
#endif
}

/* Weak implementation of the ping reply handler hook. This is overridden by mmping if it
 * is linked into the application. */
MM_WEAK void vApplicationPingReplyHook(ePingReplyStatus_t eStatus, uint16_t usIdentifier)
{
    MM_UNUSED(eStatus);
    MM_UNUSED(usIdentifier);
}
