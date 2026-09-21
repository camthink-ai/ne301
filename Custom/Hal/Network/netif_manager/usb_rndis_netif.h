#ifndef __USB_RNDIS_NETIF_H__
#define __USB_RNDIS_NETIF_H__

#ifdef __cplusplus
    extern "C" {
#endif

#include "netif_manager.h"

int usb_rndis_netif_init(void);
int usb_rndis_netif_up(void);
int usb_rndis_netif_down(void);
void usb_rndis_netif_deinit(void);
int usb_rndis_netif_config(netif_config_t *netif_cfg);
int usb_rndis_netif_info(netif_info_t *netif_info);
netif_state_t usb_rndis_netif_state(void);
struct netif *usb_rndis_netif_ptr(void);

int usb_rndis_netif_ctrl(const char *if_name, netif_cmd_t cmd, void *param);

#ifdef __cplusplus
}
#endif

#endif /* __USB_RNDIS_NETIF_H__ */
