#include <stdlib.h>
#include <string.h>
#include "lwip/inet.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/mem.h"
#include "lwip/ip_addr.h"


#include "dhcpserver.h"
#include "dhcp_priv.h"


static const u32_t magic_cookie  = 0x63538263;
static const u8_t start_ip4 = 100, end_ip = 250;
static u8_t now_ip4 = start_ip4;

#define BOOTP_BROADCAST 0x8000

#define DHCP_REQUEST        1
#define DHCP_REPLY          2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET  6
#define DHCP_MSG_LEN      236

#define DHCPS_SERVER_PORT  67
#define DHCPS_CLIENT_PORT  68

#define DHCPDISCOVER  1
#define DHCPOFFER     2
#define DHCPREQUEST   3
#define DHCPDECLINE   4
#define DHCPACK       5
#define DHCPNAK       6
#define DHCPRELEASE   7

#define DHCP_OPTION_SUBNET_MASK   1
#define DHCP_OPTION_ROUTER        3
#define DHCP_OPTION_DNS_SERVER    6
#define DHCP_OPTION_REQ_IPADDR   50
#define DHCP_OPTION_LEASE_TIME   51
#define DHCP_OPTION_MSG_TYPE     53
#define DHCP_OPTION_SERVER_ID    54
#define DHCP_OPTION_INTERFACE_MTU 26
#define DHCP_OPTION_PERFORM_ROUTER_DISCOVERY 31
#define DHCP_OPTION_BROADCAST_ADDRESS 28
#define DHCP_OPTION_REQ_LIST     55
#define DHCP_OPTION_END         255

// #define DHCPS_DEBUG  1
#define DHCPS_LOG    printf

//#define DHCPS_MAX_CLIENTS CONFIG_LWIP_DHCPS_DHCPS_MAX_CLIENTS

#define DHCPS_STATE_OFFER 1
#define DHCPS_STATE_DECLINE 2
#define DHCPS_STATE_ACK 3
#define DHCPS_STATE_NAK 4
#define DHCPS_STATE_IDLE 5
#define DHCPS_STATE_RELEASE 6

typedef struct{
	 ip4_addr_t Broadcast_Address;
	 ip4_addr_t DNS_Server_Address;
	 ip4_addr_t Client_Address; 
	 ip4_addr_t DHCP_Server_Address;
	 ip4_addr_t Gateway_Address;
}DHCP_Address;

////////////////////////////////////////////////////////////////////////////////////


static struct netif   *dhcps_netif = NULL;
static struct udp_pcb *dhcps_pcb;
static DHCP_Address dhcp_address;

//static uint8_t xid[4];


//static list_node *plist = NULL;
//static bool renew = false;

static dhcps_lease_t dhcps_poll;
static dhcps_time_t dhcps_lease_time = DHCPS_LEASE_TIME_DEF;  //minute
static dhcps_offer_t dhcps_offer = 0xFF;
static dhcps_offer_t dhcps_dns = 0xFF;
static dhcps_cb_t dhcps_cb;

dhcps_client_t dhcps_client[DHCPS_MAX_CLIENTS] = {0};

static dhcps_client_t *dhcps_get_client_by_mac(uint8_t *mac)
{
    int i;
    for (i = 0; i < DHCPS_MAX_CLIENTS; i++) {
        if (memcmp(dhcps_client[i].Client_Mac, mac, 6) == 0) {
            return &dhcps_client[i];
        }
    }
    return NULL;
}

static dhcps_client_t *dhcps_get_client_by_ip(ip4_addr_t *ip)
{
    int i;
    for (i = 0; i < DHCPS_MAX_CLIENTS; i++) {
        if (ip4_addr_cmp(&dhcps_client[i].Client_Address, ip)) {
            return &dhcps_client[i];
        }
    }
    return NULL;
}

int dhcps_del_client_by_mac(uint8_t *mac)
{
    dhcps_client_t *client = NULL;

    client = dhcps_get_client_by_mac(mac);
    if (client != NULL) {
        client->is_active = 0;
        client->is_used   = 0;
        client->Client_Address.addr = 0;
        memset(client->Client_Mac, 0, sizeof(client->Client_Mac));
    }
    return 0;
}

static void dhcps_get_free_ip(ip4_addr_t *ip)
{
    dhcps_client_t *client = NULL;
    ip4_addr_t tip = {0};
    int i;
    for (i = 0; i < (DHCPS_MAX_CLIENTS + 1); i++) {
        IP4_ADDR(&tip, ip4_addr1(&dhcp_address.Client_Address), ip4_addr2(&dhcp_address.Client_Address), ip4_addr3(&dhcp_address.Client_Address), now_ip4);
        now_ip4++;
        if (now_ip4 > end_ip) now_ip4 = start_ip4;
        client = dhcps_get_client_by_ip(&tip);
        if (client == NULL) {
            ip->addr = tip.addr;
            return;
        }
    }
}

int dhcps_add_client_by_mac(uint8_t *mac)
{
    int i = 0;
    dhcps_client_t *client = NULL;

    client = dhcps_get_client_by_mac(mac);
    if (client != NULL) {
      client->is_used = 1;
      client->is_active = 1;
      return 0;
    } else {
        for (i = 0; i < DHCPS_MAX_CLIENTS; i++) {
            if (dhcps_client[i].is_used == 0) {
                dhcps_client[i].Client_Address.addr = 0;
                memcpy(dhcps_client[i].Client_Mac, mac, 6);
                dhcps_get_free_ip(&dhcps_client[i].Client_Address);
                dhcps_client[i].is_used = 1;
                dhcps_client[i].is_active = 1;
                return 0;
            }
        }

        for (i = 0; i < DHCPS_MAX_CLIENTS; i++) {
            if (dhcps_client[i].is_active == 0) {
                dhcps_client[i].Client_Address.addr = 0;
                memcpy(dhcps_client[i].Client_Mac, mac, 6);
                dhcps_get_free_ip(&dhcps_client[i].Client_Address);
                dhcps_client[i].is_active = 1;
                return 0;
            }
        }
    }
    return -1;
}

/******************************************************************************
 * FunctionName : dhcps_option_info
 * Description  : get the DHCP message option info
 * Parameters   : op_id -- DHCP message option id
 *                opt_len -- DHCP message option length
 * Returns      : DHCP message option addr
*******************************************************************************/
void *dhcps_option_info(uint8_t op_id, u32_t opt_len)
{
    void *option_arg = NULL;

    switch (op_id) {
        case IP_ADDRESS_LEASE_TIME:
            if (opt_len == sizeof(dhcps_time_t)) {
                option_arg = &dhcps_lease_time;
            }

            break;

        case REQUESTED_IP_ADDRESS:
            if (opt_len == sizeof(dhcps_lease_t)) {
                option_arg = &dhcps_poll;
            }

            break;

        case ROUTER_SOLICITATION_ADDRESS:
            if (opt_len == sizeof(dhcps_offer_t)) {
                option_arg = &dhcps_offer;
            }

            break;

        case DOMAIN_NAME_SERVER:
            if (opt_len == sizeof(dhcps_offer_t)) {
                option_arg = &dhcps_dns;
            }

            break;

        default:
            break;
    }

    return option_arg;
}

/******************************************************************************
 * FunctionName : dhcps_set_option_info
 * Description  : set the DHCP message option info
 * Parameters   : op_id -- DHCP message option id
 *                opt_info -- DHCP message option info
 *                opt_len -- DHCP message option length
 * Returns      : none
*******************************************************************************/
void dhcps_set_option_info(uint8_t op_id, void *opt_info, u32_t opt_len)
{
    if (opt_info == NULL) {
        return;
    }
    switch (op_id) {
        case IP_ADDRESS_LEASE_TIME:
            if (opt_len == sizeof(dhcps_time_t)) {
                dhcps_lease_time = *(dhcps_time_t *)opt_info;
            }

            break;

        case REQUESTED_IP_ADDRESS:
            if (opt_len == sizeof(dhcps_lease_t)) {
                dhcps_poll = *(dhcps_lease_t *)opt_info;
            }

            break;

        case ROUTER_SOLICITATION_ADDRESS:
            if (opt_len == sizeof(dhcps_offer_t)) {
                dhcps_offer = *(dhcps_offer_t *)opt_info;
            }

            break;

        case DOMAIN_NAME_SERVER:
            if (opt_len == sizeof(dhcps_offer_t)) {
                dhcps_dns = *(dhcps_offer_t *)opt_info;
            }
            break;

        default:
            break;
    }
    return;
}



/******************************************************************************
 * FunctionName : add_msg_type
 * Description  : add TYPE option of DHCP message
 * Parameters   : optptr -- the addr of DHCP message option
 * Returns      : the addr of DHCP message option
*******************************************************************************/
static uint8_t *add_msg_type(uint8_t *optptr, uint8_t type)
{
    *optptr++ = DHCP_OPTION_MSG_TYPE;
    *optptr++ = 1;
    *optptr++ = type;
    return optptr;
}

/******************************************************************************
 * FunctionName : add_offer_options
 * Description  : add OFFER option of DHCP message
 * Parameters   : optptr -- the addr of DHCP message option
 * Returns      : the addr of DHCP message option
*******************************************************************************/
static uint8_t *add_offer_options(uint8_t *optptr)
{
#ifdef USE_CLASS_B_NET
    *optptr++ = DHCP_OPTION_SUBNET_MASK;
    *optptr++ = 4;  //length
    *optptr++ = 255;
    *optptr++ = 240;
    *optptr++ = 0;
    *optptr++ = 0;
#else
    *optptr++ = DHCP_OPTION_SUBNET_MASK;
    *optptr++ = 4;
    *optptr++ = 255;
    *optptr++ = 255;
    *optptr++ = 255;
    *optptr++ = 0;
#endif

    *optptr++ = DHCP_OPTION_LEASE_TIME;
    *optptr++ = 4;
    *optptr++ = ((dhcps_lease_time * DHCPS_LEASE_UNIT) >> 24) & 0xFF;
    *optptr++ = ((dhcps_lease_time * DHCPS_LEASE_UNIT) >> 16) & 0xFF;
    *optptr++ = ((dhcps_lease_time * DHCPS_LEASE_UNIT) >> 8) & 0xFF;
    *optptr++ = ((dhcps_lease_time * DHCPS_LEASE_UNIT) >> 0) & 0xFF;

    *optptr++ = DHCP_OPTION_SERVER_ID;
    *optptr++ = 4;
    *optptr++ = ip4_addr1(&dhcp_address.DHCP_Server_Address);
    *optptr++ = ip4_addr2(&dhcp_address.DHCP_Server_Address);
    *optptr++ = ip4_addr3(&dhcp_address.DHCP_Server_Address);
    *optptr++ = ip4_addr4(&dhcp_address.DHCP_Server_Address);

    if (dhcps_router_enabled(dhcps_offer)) {
        if (!ip4_addr_isany_val(dhcp_address.Gateway_Address)) {
            *optptr++ = DHCP_OPTION_ROUTER;
            *optptr++ = 4;
            *optptr++ = ip4_addr1(&dhcp_address.Gateway_Address);
            *optptr++ = ip4_addr2(&dhcp_address.Gateway_Address);
            *optptr++ = ip4_addr3(&dhcp_address.Gateway_Address);
            *optptr++ = ip4_addr4(&dhcp_address.Gateway_Address);
        }
    }
		*optptr++ = DHCP_OPTION_DNS_SERVER;
    *optptr++ = 4;
    if (dhcps_dns_enabled(dhcps_dns)) {
        *optptr++ = ip4_addr1(&dhcp_address.DNS_Server_Address );
        *optptr++ = ip4_addr2(&dhcp_address.DNS_Server_Address );
        *optptr++ = ip4_addr3(&dhcp_address.DNS_Server_Address );
        *optptr++ = ip4_addr4(&dhcp_address.DNS_Server_Address );
    }else {
        *optptr++ = ip4_addr1(&dhcp_address.DHCP_Server_Address );
        *optptr++ = ip4_addr2(&dhcp_address.DHCP_Server_Address );
        *optptr++ = ip4_addr3(&dhcp_address.DHCP_Server_Address );
        *optptr++ = ip4_addr4(&dhcp_address.DHCP_Server_Address );
    }


    *optptr++ = DHCP_OPTION_INTERFACE_MTU;
    *optptr++ = 2;
    *optptr++ = 0x05;
    *optptr++ = 0xdc;

    *optptr++ = DHCP_OPTION_PERFORM_ROUTER_DISCOVERY;
    *optptr++ = 1;
    *optptr++ = 0x00;

    *optptr++ = 43;
    *optptr++ = 6;
    *optptr++ = 0x01;
    *optptr++ = 4;
    *optptr++ = 0x00;
    *optptr++ = 0x00;
    *optptr++ = 0x00;
    *optptr++ = 0x02;

    return optptr;
}

/******************************************************************************
 * FunctionName : add_end
 * Description  : add end option of DHCP message
 * Parameters   : optptr -- the addr of DHCP message option
 * Returns      : the addr of DHCP message option
*******************************************************************************/
static uint8_t *add_end(uint8_t *optptr)
{
    *optptr++ = DHCP_OPTION_END;
    return optptr;
}

/******************************************************************************
 * FunctionName : create_msg
 * Description  : create response message
 * Parameters   : m -- DHCP message info
 * Returns      : none
*******************************************************************************/
static void create_msg(struct dhcps_msg *m)
{
    dhcps_client_t *client = NULL;

    m->op = DHCP_REPLY;

    m->htype = DHCP_HTYPE_ETHERNET;

    m->hlen = 6;

    m->hops = 0;

   	//memcpy((char *) m->xid, (char *) xid, sizeof(m->xid));
    m->secs = 0;
//    m->flags = htons(BOOTP_BROADCAST);
		m->flags = 0;

    client = dhcps_get_client_by_mac(m->chaddr);
    if (client == NULL) {
      printf("create_msg, client not found\n");
      return;
    }
    memcpy((char *) m->yiaddr, (char *) &client->Client_Address.addr, sizeof(m->yiaddr));

    memset((char *) m->ciaddr, 0, sizeof(m->ciaddr));

    memcpy((char *) m->siaddr, (char *)&dhcp_address.DHCP_Server_Address.addr, sizeof(m->siaddr));

    memset((char *) m->giaddr, 0, sizeof(m->giaddr));

    memset((char *) m->sname, 0, sizeof(m->sname));

    memset((char *) m->file, 0, sizeof(m->file));

    memset((char *) m->options, 0, sizeof(m->options));

    u32_t magic_cookie_temp = magic_cookie;

    memcpy((char *) m->options, &magic_cookie_temp, sizeof(magic_cookie_temp));
}

struct pbuf * dhcps_pbuf_alloc(uint16_t len)
{
    return pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
}

/******************************************************************************
 * FunctionName : send_offer
 * Description  : DHCP message OFFER Response
 * Parameters   : m -- DHCP message info
 * Returns      : none
*******************************************************************************/
static void send_offer(struct dhcps_msg *msg, uint16_t len)
{
    uint8_t *end;
    struct pbuf *p, *q;
    uint8_t *data;
    uint16_t cnt = 0;
//		uint8_t CopyFinished=0;
    uint16_t i;
//		uint32_t  endptr , dataptr = NULL;
		#if DHCPS_DEBUG
    err_t SendOffer_err_t;
		#endif
    create_msg(msg);
    end = add_msg_type(&msg->options[4], DHCPOFFER);
    end = add_offer_options(end);
    end = add_end(end);

    p = dhcps_pbuf_alloc((uint32_t)end - (uint32_t)msg);
		#if DHCPS_DEBUG
    DHCPS_LOG("udhcp: send_offer>>p->ref = %d\n", p->ref);
		#endif

    if (p != NULL) {
				#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: send_offer>>pbuf_alloc succeed\n");
        DHCPS_LOG("dhcps: send_offer>>p->tot_len = %d\n", p->tot_len);
        DHCPS_LOG("dhcps: send_offer>>p->len = %d\n", p->len);
				#endif
        q = p;
        while (q != NULL ) {
            data = (uint8_t *)q->payload;
            for (i = 0; i < q->len; i++) {
              data[i] = ((uint8_t *) msg)[cnt++];
            }
            q = q->next;
						if (q == NULL){
							break;
						}
        }
    } else {
				#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: send_offer>>pbuf_alloc failed\n");
				#endif
        return;
    }

    ip_addr_t ip_temp = IPADDR4_INIT(0x0);
    ip4_addr_set(ip_2_ip4(&ip_temp), &dhcp_address.Broadcast_Address);
    struct udp_pcb *pcb_dhcps = dhcps_pcb;
		// pcb_dhcps->netif_idx = dhcps_netif->num;
		#if DHCPS_DEBUG
    SendOffer_err_t = udp_sendto(pcb_dhcps, p, &ip_temp, DHCPS_CLIENT_PORT);
    DHCPS_LOG("dhcps: send_offer>>udp_sendto result %d\n", SendOffer_err_t);
		#else
    udp_sendto(pcb_dhcps, p, &ip_temp, DHCPS_CLIENT_PORT);
		#endif

    if (p->ref != 0) {
		#if DHCPS_DEBUG
    DHCPS_LOG("udhcp: send_offer>>free pbuf\n");
		#endif
        pbuf_free(p);
    }
}

/******************************************************************************
 * FunctionName : send_nak
 * Description  : DHCP message NACK Response
 * Parameters   : m -- DHCP message info
 * Returns      : none
*******************************************************************************/
static void send_nak(struct dhcps_msg *m, uint16_t len)
{
    uint8_t *end;
    struct pbuf *p, *q;
    uint8_t *data;
    uint16_t cnt = 0;
    uint16_t i;
		#if DHCPS_DEBUG
    err_t SendNak_err_t;
		#endif
    create_msg(m);

    end = add_msg_type(&m->options[4], DHCPNAK);
    end = add_end(end);

    p = dhcps_pbuf_alloc(len);
		#if DHCPS_DEBUG
    DHCPS_LOG("udhcp: send_nak>>p->ref = %d\n", p->ref);
		#endif

    if (p != NULL) {
		#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: send_nak>>pbuf_alloc succeed\n");
        DHCPS_LOG("dhcps: send_nak>>p->tot_len = %d\n", p->tot_len);
        DHCPS_LOG("dhcps: send_nak>>p->len = %d\n", p->len);
		#endif
        q = p;
        while (q != NULL) {
            data = (uint8_t *)q->payload;
            for (i = 0; i < q->len; i++) {
                data[i] = ((uint8_t *) m)[cnt++];
            }
            q = q->next;
        }
    } else {
			#if DHCPS_DEBUG
       DHCPS_LOG("dhcps: send_nak>>pbuf_alloc failed\n");
			#endif
        return;
    }

    ip_addr_t ip_temp = IPADDR4_INIT(0x0);
    ip4_addr_set(ip_2_ip4(&ip_temp), &dhcp_address.Broadcast_Address);
    struct udp_pcb *pcb_dhcps = dhcps_pcb;
		// pcb_dhcps->netif_idx = 1;
		#if DHCPS_DEBUG
    SendNak_err_t = udp_sendto(pcb_dhcps, p, &ip_temp, DHCPS_CLIENT_PORT);
    DHCPS_LOG("dhcps: send_nak>>udp_sendto result %d\n", SendNak_err_t);
		#else
    udp_sendto(pcb_dhcps, p, &ip_temp, DHCPS_CLIENT_PORT);
		#endif

    if (p->ref != 0) {
		#if DHCPS_DEBUG
    DHCPS_LOG("udhcp: send_nak>>free pbuf\n");
		#endif
        pbuf_free(p);
    }
}

/******************************************************************************
 * FunctionName : send_ack
 * Description  : DHCP message ACK Response
 * Parameters   : m -- DHCP message info
 * Returns      : none
*******************************************************************************/
static void send_ack(struct dhcps_msg *m, uint16_t len)
{
    uint8_t *end;
    struct pbuf *p, *q;
    uint8_t *data;
    uint16_t cnt = 0;
    uint16_t i;
    err_t SendAck_err_t;
    create_msg(m);

    (void)(SendAck_err_t);
    end = add_msg_type(&m->options[4], DHCPACK);
    end = add_offer_options(end);
    end = add_end(end);

    p = dhcps_pbuf_alloc((uint32_t)end - (uint32_t)m);
		#if DHCPS_DEBUG
    DHCPS_LOG("udhcp: send_ack>>p->ref = %d\n", p->ref);
		#endif

    if (p != NULL) {
				#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: send_ack>>pbuf_alloc succeed\n");
        DHCPS_LOG("dhcps: send_ack>>p->tot_len = %d\n", p->tot_len);
        DHCPS_LOG("dhcps: send_ack>>p->len = %d\n", p->len);
				#endif
        q = p;
        while (q != NULL) {
            data = (uint8_t *)q->payload;
            for (i = 0; i < q->len; i++) {
                data[i] = ((uint8_t *) m)[cnt++];
            }
            q = q->next;
						if (q == NULL){
							break;
						}
        }
    } else {
				#if DHCPS_DEBUG
				DHCPS_LOG("dhcps: send_ack>>pbuf_alloc failed\n");
				#endif
        return;
    }

    ip_addr_t ip_temp = IPADDR4_INIT(0x0);
    ip4_addr_set(ip_2_ip4(&ip_temp), &dhcp_address.Broadcast_Address);
    struct udp_pcb *pcb_dhcps = dhcps_pcb;
		// pcb_dhcps->netif_idx = 1;
    SendAck_err_t = udp_sendto(pcb_dhcps, p, &ip_temp, DHCPS_CLIENT_PORT);
		#if DHCPS_DEBUG
    DHCPS_LOG("dhcps: send_ack>>udp_sendto result %d\n", SendAck_err_t);
		#endif

    if (SendAck_err_t == ERR_OK) {
        printf("DHCPS ACK IP: %d.%d.%d.%d\r\n", m->yiaddr[0], m->yiaddr[1], m->yiaddr[2], m->yiaddr[3]);
        // dhcps_cb(m->yiaddr);
    }

    if (p->ref != 0) {
		#if DHCPS_DEBUG
        DHCPS_LOG("udhcp: send_ack>>free pbuf\n");
		#endif
        pbuf_free(p);
    }
		else{
		#if DHCPS_DEBUG
    DHCPS_LOG("udhcp: send_ack>>p->ref == 0\n");
		#endif			
		}
}

/******************************************************************************
 * FunctionName : parse_options
 * Description  : parse DHCP message options
 * Parameters   : optptr -- DHCP message option info
 *                len -- DHCP message option length
 * Returns      : none
*******************************************************************************/
static uint8_t parse_options(uint8_t *OptionsPtr, int16_t len)
{
    bool is_dhcp_parse_end = false;
    struct dhcps_state s;


    uint8_t *OptionsEndPtr = OptionsPtr + len;
    uint16_t type = 0;

    s.state = DHCPS_STATE_IDLE;

    while (OptionsPtr < OptionsEndPtr) {
				#if DHCPS_DEBUG
        DHCPS_LOG("dhcps: (int16_t)*optptr = %d\n", (int16_t)*OptionsPtr);
				#endif
        switch ((int16_t) *OptionsPtr) {
            case DHCP_OPTION_MSG_TYPE:	//53
                type = *(OptionsPtr + 2);
            break;
						case DHCP_OPTION_REQ_IPADDR://50
									s.state = DHCPS_STATE_ACK;
            break;
            case DHCP_OPTION_END: {
                is_dhcp_parse_end = true;
            }
            break;
        }
        if (is_dhcp_parse_end) {
            break;
        }
				/* Increament Ptr by MsgType Field + Len Fielf + Len Value ;*/
        OptionsPtr += OptionsPtr[1] + 2;
    }

    switch (type) {
        case DHCPDISCOVER://1
						#if DHCPS_DEBUG
            DHCPS_LOG("dhcps: DHCPD_STATE_DHCPDISCOVER -- SEND_OFFER\n");
						#endif
            s.state = DHCPS_STATE_OFFER;
        break;
        case DHCPREQUEST://3
						#if DHCPS_DEBUG
						DHCPS_LOG("dhcps: DHCPD_STATE_DHCPREQUEST -- SEND_ACK\n");
						#endif
            s.state = DHCPS_STATE_ACK;
       
        break;
        case DHCPDECLINE://4
            s.state = DHCPS_STATE_IDLE;
						#if DHCPS_DEBUG
            DHCPS_LOG("dhcps: DHCPD_STATE_IDLE\n");
						#endif
        break;
        case DHCPRELEASE://7
            s.state = DHCPS_STATE_RELEASE;
						#if DHCPS_DEBUG
            DHCPS_LOG("dhcps: DHCPD_STATE_IDLE\n");
						#endif
        break;
    }
				#if DHCPS_DEBUG
				DHCPS_LOG("dhcps: return s.state = %d\n", s.state);
			#endif
    return s.state;
}

/******************************************************************************
 * FunctionName : parse_msg
 * Description  : parse DHCP message from netif
 * Parameters   : m -- DHCP message info
 *                len -- DHCP message length
 * Returns      : DHCP message type
*******************************************************************************/
static int16_t parse_msg(struct dhcps_msg *msg, uint16_t len)
{
			/* Check the Magic Cookie*/
    if(memcmp((char *)msg->options, &magic_cookie, sizeof(magic_cookie)) == 0) {
				#if DHCPS_DEBUG
				DHCPS_LOG("dhcps: len = %d\n", len);
				#endif
				/* Parse option and Get Message Type*/
        int16_t ret = parse_options(&msg->options[4], len - 240);

        return ret;
    }

    return 0;
}

/******************************************************************************
 * FunctionName : handle_dhcp
 * Description  : If an incoming DHCP message is in response to us, then trigger the state machine
 * Parameters   : arg -- arg user supplied argument (udp_pcb.recv_arg)
 * 				  pcb -- the udp_pcb which received data
 * 			      p -- the packet buffer that was received
 * 				  addr -- the remote IP address from which the packet was received
 * 				  port -- the remote port from which the packet was received
 * Returns      : none
*******************************************************************************/
static void handle_dhcp(void *arg,
                        struct udp_pcb *pcb,
                        struct pbuf *p,
                        const ip_addr_t *addr,
                        uint16_t port)
{
    dhcps_client_t *client = NULL;
    struct dhcps_msg *pmsg_dhcps = NULL;
    int16_t tlen, malloc_len;
    uint16_t i;
    uint16_t dhcps_msg_cnt = 0;
    uint8_t *p_dhcps_msg = NULL;
    uint8_t *data;
		uint32_t PayloadLen = 0;
		struct pbuf  *q;

		#if DHCPS_DEBUG
    DHCPS_LOG("dhcps: handle_dhcp-> receive a packet\n");
		#endif
    if (p == NULL) {
				#if DHCPS_DEBUG
				DHCPS_LOG("dhcps: handle_dhcp-> p = NULL\n");
				#endif			
        return;
    }
		/* Allocate Buffer For DHCP Message   */
    malloc_len = sizeof(struct dhcps_msg);
    if (malloc_len < p->tot_len) {
        malloc_len = p->tot_len;
    }
    pmsg_dhcps = (struct dhcps_msg *)mem_malloc(malloc_len);
    if (NULL == pmsg_dhcps) {
				#if DHCPS_DEBUG
				DHCPS_LOG("dhcps: handle_dhcp-> pmsg_dhcps = NULL\n");
				#endif	
        pbuf_free(p);
        return;
    }
    memset(pmsg_dhcps , 0x00 , malloc_len);
		
		/* Copying Buffer to message structure*/
    p_dhcps_msg = (uint8_t *)pmsg_dhcps;
    tlen = p->tot_len;
    data = p->payload;
		PayloadLen = p->len;
		
		#if DHCPS_DEBUG
		DHCPS_LOG("dhcps: handle_dhcp-> p->tot_len = %d\n", tlen);
		DHCPS_LOG("dhcps: handle_dhcp-> p->len = %d\n", p->len);
		#endif
		q = p;
		while(1)
			{
				for (i = 0; i < PayloadLen; i++) 
				{
					p_dhcps_msg[dhcps_msg_cnt++] = data[i];
				}
        if(q->next != NULL){
          PayloadLen = q->next->len;
          data = q->next->payload;
					q = q->next;
				}
				else{
					break;
				}
			}

		#if DHCPS_DEBUG
    DHCPS_LOG("dhcps: handle_dhcp-> pbuf_free(p)\n");
		#endif
    pbuf_free(p);
		
    client = dhcps_get_client_by_mac(pmsg_dhcps->chaddr);
    if (client == NULL) {
        printf("dhcps: handle_dhcp-> client == NULL\n");
        mem_free(pmsg_dhcps);
        pmsg_dhcps = NULL;
        return;
    }

		#if DHCPS_DEBUG
		DHCPS_LOG("dhcps: handle_dhcp-> parse_msg(p)\n");
		#endif
    int16_t msg_type = parse_msg(pmsg_dhcps, tlen );
    switch (msg_type) { 
        case DHCPS_STATE_OFFER://1
						#if DHCPS_DEBUG
            DHCPS_LOG("dhcps: handle_dhcp-> DHCPD_STATE_OFFER\n");
						#endif
            send_offer(pmsg_dhcps, malloc_len);
        break;

        case DHCPS_STATE_ACK://3
						#if DHCPS_DEBUG
            DHCPS_LOG("dhcps: handle_dhcp-> DHCPD_STATE_ACK\n");
						#endif
            send_ack(pmsg_dhcps, malloc_len);
            // IP4_ADDR(&dhcp_address.Client_Address, ip4_addr1(&dhcp_address.Client_Address), ip4_addr2(&dhcp_address.Client_Address), ip4_addr3(&dhcp_address.Client_Address), now_ip4);
            // now_ip4++;
            // if (now_ip4 > end_ip) now_ip4 = start_ip4;
        break;

        case DHCPS_STATE_NAK://4
						#if DHCPS_DEBUG
            DHCPS_LOG("dhcps: handle_dhcp-> DHCPD_STATE_NAK\n");
						#endif
            send_nak(pmsg_dhcps, malloc_len);
        break;

        case DHCPS_STATE_RELEASE://6
            dhcps_del_client_by_mac(pmsg_dhcps->chaddr);
        break;

        default :
            #if DHCPS_DEBUG
            DHCPS_LOG("dhcps: handle_dhcp-> %d\n", msg_type);
            #endif
            break;
    }
		#if DHCPS_DEBUG
    DHCPS_LOG("dhcps: handle_dhcp-> mem_free(pmsg_dhcps)\n");
		#endif
		mem_free(pmsg_dhcps);
    pmsg_dhcps = NULL;
}

/******************************************************************************
 * FunctionName : dhcps_set_new_lease_cb
 * Description  : set callback for dhcp server when it assign an IP 
 *                to the connected dhcp client
 * Parameters   : cb -- callback for dhcp server
 * Returns      : none
*******************************************************************************/
void dhcps_set_new_lease_cb(dhcps_cb_t cb)
{
    dhcps_cb = cb;
}

/******************************************************************************
 * FunctionName : dhcps_start
 * Description  : start dhcp server function
 * Parameters   : netif -- The current netif addr
 *              : info  -- The current ip info
 * Returns      : none
*******************************************************************************/
void dhcps_start(struct netif *netif)
{
    dhcps_netif = netif;

    if (dhcps_pcb != NULL) {
        udp_remove(dhcps_pcb);
    }

    dhcps_pcb = udp_new();
    struct udp_pcb *pcb_dhcps = dhcps_pcb;

    if (pcb_dhcps == NULL ) {
        #if DHCPS_DEBUG
        DHCPS_LOG("dhcps_start(): could not obtain pcb\n");
        #endif
        return;
    }

    dhcps_pcb = pcb_dhcps;
    dhcps_pcb->netif_idx = netif->num + 1;
    IP4_ADDR(&dhcp_address.Broadcast_Address, 255, 255, 255, 255);
    IP4_ADDR(&dhcp_address.DNS_Server_Address, 8, 8, 8, 8);
		
		memcpy(&dhcp_address.DHCP_Server_Address.addr, &netif->ip_addr,4);
		memcpy(&dhcp_address.Gateway_Address.addr, &netif->gw.addr, 4);
		IP4_ADDR(&dhcp_address.Client_Address, ip4_addr1(&netif->ip_addr), ip4_addr2(&netif->ip_addr), ip4_addr3(&netif->ip_addr), now_ip4);
    // now_ip4++;
    // if (now_ip4 > end_ip) now_ip4 = start_ip4;
    
    ip_set_option(pcb_dhcps, SOF_BROADCAST);
		udp_bind(pcb_dhcps, &netif->ip_addr, DHCPS_SERVER_PORT);
    udp_recv(pcb_dhcps, handle_dhcp, NULL);
		#if DHCPS_DEBUG
    DHCPS_LOG("dhcps:dhcps_start->udp_recv function Set a receive callback handle_dhcp for UDP_PCB pcb_dhcps\n");
		#endif

}

/******************************************************************************
 * FunctionName : dhcps_stop
 * Description  : stop dhcp server function
 * Parameters   : netif -- The current netif addr
 * Returns      : none
*******************************************************************************/
void dhcps_stop(struct netif *netif)
{
    struct netif *apnetif = netif;

    if (apnetif == NULL) {
        #if DHCPS_DEBUG
        DHCPS_LOG("dhcps_stop: apnetif == NULL\n");
        #endif
        return;
    }

    if (dhcps_pcb != NULL) {
        udp_disconnect(dhcps_pcb);
        udp_remove(dhcps_pcb);
        dhcps_pcb = NULL;
    }

    now_ip4 = 100;
    memset(dhcps_client, 0, sizeof(dhcps_client));
}


/******************************************************************************
 * FunctionName : dhcps_dns_setserver
 * Description  : set DNS server address for dhcpserver
 * Parameters   : dnsserver -- The DNS server address
 * Returns      : none
*******************************************************************************/
void
dhcps_dns_setserver(const ip_addr_t *dnsserver)
{
    if (dnsserver != NULL) {
        dhcp_address.DNS_Server_Address =  *(ip_2_ip4(dnsserver));
    } else {
        dhcp_address.DNS_Server_Address  = *(ip_2_ip4(IP_ADDR_ANY));
    } 
}
/******************************************************************************
 * FunctionName : dhcps_dns_getserver
 * Description  : get DNS server address for dhcpserver
 * Parameters   : none
 * Returns      : ip4_addr_t
*******************************************************************************/
ip4_addr_t 
dhcps_dns_getserver(void)
{
    return dhcp_address.DNS_Server_Address;
}
