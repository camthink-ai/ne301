#ifndef __DHCPS_H__
#define __DHCPS_H__


#include <stdbool.h>
#include "lwip/ip_addr.h"

typedef struct dhcps_state{
        s16_t state;
} dhcps_state;

/**
 * @brief DHCP server message structure.
 *
 * This structure represents a DHCP message as defined by the DHCP protocol.
 * It contains all the fields required for DHCP communication between client and server.
 *
 * Members:
 *   op      - Message opcode/type (e.g., BOOTREQUEST, BOOTREPLY).
 *   htype   - Hardware address type (e.g., Ethernet).
 *   hlen    - Hardware address length.
 *   hops    - Number of relay agent hops from client.
 *   xid     - Transaction ID, a random number chosen by the client.
 *   secs    - Seconds elapsed since client began address acquisition or renewal process.
 *   flags   - Flags (e.g., broadcast flag).
 *   ciaddr  - Client IP address (if already in use).
 *   yiaddr  - 'Your' (client) IP address.
 *   siaddr  - IP address of next server to use in bootstrap.
 *   giaddr  - Relay agent IP address.
 *   chaddr  - Client hardware address.
 *   sname   - Optional server host name, null terminated string.
 *   file    - Boot file name, null terminated string; "generic" name or null in DHCPDISCOVER, fully qualified directory-path name in DHCPOFFER.
 *   options - Optional parameters field (DHCP options).
 */
typedef struct dhcps_msg {
        u8_t op, htype, hlen, hops;
        u8_t xid[4];
        u16_t secs, flags;
        u8_t ciaddr[4];
        u8_t yiaddr[4];
        u8_t siaddr[4];
        u8_t giaddr[4];
        u8_t chaddr[16];
        u8_t sname[64];
        u8_t file[128];
        u8_t options[312];
}dhcps_msg;

typedef struct {
        u8_t is_active;      /* 1 = currently associated with the AP (may DHCP)   */
        u8_t is_used;        /* 1 = slot occupied (active OR cached for stickiness) */
        u32_t seq;           /* monotonic stamp: larger = more recently active (LRU) */
        u8_t Client_Mac[6];
        ip4_addr_t Client_Address;
} dhcps_client_t;

/*   Defined in esp_misc.h */
typedef struct {
	bool enable;
	ip4_addr_t start_ip;
	ip4_addr_t end_ip;
} dhcps_lease_t;

enum dhcps_offer_option {
        OFFER_START = 0x00,
        OFFER_ROUTER = 0x01,
        OFFER_DNS = 0x02,
        OFFER_END
};

// #define DHCPS_DEBUG                     1
#define DHCPS_COARSE_TIMER_SECS         1
#define DHCPS_MAX_LEASE                 0x64
#define DHCPS_LEASE_TIME_DEF            (120)
#define DHCPS_LEASE_UNIT                60
#define DHCPS_MAX_CLIENTS               10

typedef u32_t dhcps_time_t;
typedef u8_t dhcps_offer_t;

typedef struct {
        dhcps_offer_t dhcps_offer;
        dhcps_offer_t dhcps_dns;
        dhcps_time_t  dhcps_time;
        dhcps_lease_t dhcps_poll;
} dhcps_options_t;

typedef void (*dhcps_cb_t)(u8_t client_ip[4]);

static inline bool dhcps_router_enabled (dhcps_offer_t offer) 
{
    return (offer & OFFER_ROUTER) != 0;
}

static inline bool dhcps_dns_enabled (dhcps_offer_t offer) 
{
    return (offer & OFFER_DNS) != 0;
}

void dhcps_start(struct netif *netif);
void dhcps_stop(struct netif *netif);
int dhcps_add_client_by_mac(uint8_t *mac);
int dhcps_del_client_by_mac(uint8_t *mac);
void *dhcps_option_info(u8_t op_id, u32_t opt_len);
void dhcps_set_option_info(u8_t op_id, void *opt_info, u32_t opt_len);
bool dhcp_search_ip_on_mac(u8_t *mac, ip4_addr_t *ip);
int dhcps_get_clients(dhcps_client_t *out, int max);
void dhcps_dns_setserver(const ip_addr_t *dnsserver);
ip4_addr_t dhcps_dns_getserver(void);
void dhcps_set_new_lease_cb(dhcps_cb_t cb);

#endif


