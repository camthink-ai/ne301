/*
 * DHCP server for the Si91x WiFi AP (lwIP UDP companion).
 *
 * Deployment model (ne301): this is a *whitelist* DHCP server. Clients are
 * pre-registered by the AP event handler (dhcps_add_client_by_mac on station
 * join, dhcps_del_client_by_mac on leave). The DHCP flow only looks clients up
 * in the table — it never assigns an address to an unknown MAC.
 *
 * Leases are *sticky*: on AP leave the MAC->IP binding is kept cached so a
 * reconnecting station gets its previous address back. The table is bounded by
 * DHCPS_MAX_CLIENTS; when full, the oldest cached (disconnected) entry is
 * evicted. Active stations are never evicted.
 *
 * REQUEST handling is state-aware: a request for an address this server can't
 * confirm (held by another station, or outside the pool) is answered with a
 * NAK — never a blind ACK of a *different* address, which leaves the client
 * parked on its stale lease while the server believes otherwise. A request
 * for a free address the client previously held is honored, so IPs stay
 * stable across table rebuilds.
 *
 * The lease table is touched from two threads (lwIP tcpip thread vs. the WiFi
 * event context), so every access is guarded by s_lease_mutex.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmsis_os2.h"

#include "lwip/inet.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/mem.h"
#include "lwip/ip_addr.h"

#include "dhcpserver.h"
#include "dhcp_priv.h"

/* ---- protocol constants ------------------------------------------------ */
static const u32_t magic_cookie  = 0x63538263;
static const u8_t  start_ip4     = 100;   /* first host octet in the pool    */
static const u8_t  end_ip4       = 250;   /* last  host octet in the pool    */
static u8_t        now_ip4       = start_ip4; /* rotating allocation cursor  */

#define BOOTP_BROADCAST         0x8000

#define DHCP_REQUEST            1
#define DHCP_REPLY              2
#define DHCP_HTYPE_ETHERNET     1
#define DHCP_HLEN_ETHERNET      6

#define DHCPS_SERVER_PORT       67
#define DHCPS_CLIENT_PORT       68

/* DHCP message types (option 53 value) */
#define DHCPDISCOVER            1
#define DHCPOFFER               2
#define DHCPREQUEST             3
#define DHCPDECLINE             4
#define DHCPACK                 5
#define DHCPNAK                 6
#define DHCPRELEASE             7

/* DHCP options we emit/inspect */
#define DHCP_OPTION_PAD                 0
#define DHCP_OPTION_SUBNET_MASK         1
#define DHCP_OPTION_ROUTER              3
#define DHCP_OPTION_DNS_SERVER          6
#define DHCP_OPTION_REQ_IPADDR          50
#define DHCP_OPTION_LEASE_TIME          51
#define DHCP_OPTION_MSG_TYPE            53
#define DHCP_OPTION_SERVER_ID           54
#define DHCP_OPTION_INTERFACE_MTU       26
#define DHCP_OPTION_PERFORM_ROUTER_DISCOVERY 31
#define DHCP_OPTION_BROADCAST_ADDRESS   28
#define DHCP_OPTION_REQ_LIST            55
#define DHCP_OPTION_END                 255

/* Fixed BOOTP header is 236 bytes; magic cookie occupies options[0..3], so
 * real DHCP options start at byte offset 240. Replies below this can't be a
 * valid DHCP message; RFC 2131 also requires BOOTREPLY >= 300 bytes. */
#define DHCPS_MSG_FIXED_LEN             236
#define DHCPS_MIN_REQUEST_LEN           (DHCPS_MSG_FIXED_LEN + 4)   /* 240 */
#define DHCPS_REPLY_MIN_LEN             300

/* server state machine (returned by parse_options) */
#define DHCPS_STATE_OFFER               1
#define DHCPS_STATE_DECLINE             2
#define DHCPS_STATE_ACK                 3
#define DHCPS_STATE_NAK                 4
#define DHCPS_STATE_IDLE                5
#define DHCPS_STATE_RELEASE             6

/* ---- logging ----------------------------------------------------------- */
/* Kept local on purpose: pulling Log/debug.h into this low-level network file
 * would drag in YModem/json_config/main.h and shadow lwip/mem.h's namesake. */
#define DHCPS_DEBUG  1
#if DHCPS_DEBUG
  #define DHCPS_LOG(fmt, ...)   printf("[DHCPS] " fmt, ##__VA_ARGS__)
#else
  #define DHCPS_LOG(fmt, ...)   ((void)0)
#endif
/* always-on, low-frequency events worth seeing in the field */
#define DHCPS_INFO(fmt, ...)    printf("[DHCPS] " fmt, ##__VA_ARGS__)

/* MAC pretty-printing — cast each octet to unsigned so %x doesn't trip -Wformat
 * on the uint8_t -> int default promotion. */
#define MAC_STR_FMT   "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_STR_ARG(m) (unsigned)(m)[0], (unsigned)(m)[1], (unsigned)(m)[2], \
                       (unsigned)(m)[3], (unsigned)(m)[4], (unsigned)(m)[5]

typedef struct {
    ip4_addr_t Broadcast_Address;
    ip4_addr_t Subnet_Mask;
    ip4_addr_t DNS_Server_Address;
    ip4_addr_t Client_Address;        /* network prefix (first 3 octets) for the pool */
    ip4_addr_t DHCP_Server_Address;
    ip4_addr_t Gateway_Address;
} DHCP_Address;

/* ---- module state ------------------------------------------------------ */
static struct netif   *dhcps_netif = NULL;
static struct udp_pcb *dhcps_pcb;
static DHCP_Address    dhcp_address;

static dhcps_lease_t  dhcps_poll;
static dhcps_time_t   dhcps_lease_time = DHCPS_LEASE_TIME_DEF;   /* minutes */
static dhcps_offer_t  dhcps_offer = 0xFF;
static dhcps_offer_t  dhcps_dns   = 0xFF;
static dhcps_cb_t     dhcps_cb;

static dhcps_client_t dhcps_client[DHCPS_MAX_CLIENTS];

/* Guards dhcps_client[] / now_ip4 / s_seq — shared across the tcpip thread
 * (handle_dhcp) and the WiFi event context (add/del_client_by_mac).
 * ponytail: plain osMutexNew(NULL), no priority inheritance — the critical
 * sections contain no blocking calls, so inversion window is sub-microsecond.
 * Switch to osMutexPrioInherit only if a longer section is ever introduced. */
static osMutexId_t    s_lease_mutex = NULL;
static u32_t          s_seq         = 0;   /* monotonic LRU stamp            */

static void lease_lock(void)   { if (s_lease_mutex) (void)osMutexAcquire(s_lease_mutex, osWaitForever); }
static void lease_unlock(void) { if (s_lease_mutex) (void)osMutexRelease(s_lease_mutex); }

/* ---- lease table helpers (callers must hold s_lease_mutex) ------------- */
static dhcps_client_t *dhcps_get_client_by_mac(const uint8_t *mac)
{
    int i;
    for (i = 0; i < DHCPS_MAX_CLIENTS; i++) {
        if (dhcps_client[i].is_used &&
            memcmp(dhcps_client[i].Client_Mac, mac, 6) == 0) {
            return &dhcps_client[i];
        }
    }
    return NULL;
}

static dhcps_client_t *dhcps_get_client_by_ip(const ip4_addr_t *ip)
{
    int i;
    for (i = 0; i < DHCPS_MAX_CLIENTS; i++) {
        if (dhcps_client[i].is_used &&
            ip4_addr_cmp(&dhcps_client[i].Client_Address, ip)) {
            return &dhcps_client[i];
        }
    }
    return NULL;
}

/* Pick the next free host octet in [start_ip4..end_ip4]. Returns 0 on success
 * and writes the full IP (AP prefix + host octet) to *out. The full range is
 * scanned (the old code gave up after only DHCPS_MAX_CLIENTS+1 tries). */
static int lease_alloc_ip(ip4_addr_t *out)
{
    ip4_addr_t tip;
    uint8_t b1 = ip4_addr1(&dhcp_address.Client_Address);
    uint8_t b2 = ip4_addr2(&dhcp_address.Client_Address);
    uint8_t b3 = ip4_addr3(&dhcp_address.Client_Address);
    uint8_t srv = ip4_addr4(&dhcp_address.DHCP_Server_Address);
    int range = (int)end_ip4 - (int)start_ip4 + 1;
    int i;

    for (i = 0; i < range; i++) {
        uint8_t h = now_ip4++;
        if (now_ip4 > end_ip4) now_ip4 = start_ip4;
        if (h == srv) continue;                       /* never hand out the AP's own IP */
        IP4_ADDR(&tip, b1, b2, b3, h);
        if (dhcps_get_client_by_ip(&tip) == NULL) {   /* skip IPs held by active+cached */
            out->addr = tip.addr;
            return 0;
        }
    }
    return -1;   /* pool exhausted */
}

/* Is this a host address this server may hand out (AP prefix, pool range,
 * not the AP itself)? Must stay consistent with lease_alloc_ip's notion of
 * the pool: first 3 octets of Client_Address + host octet in [start..end]. */
static bool ip_in_pool(const ip4_addr_t *ip)
{
    uint8_t host = ip4_addr4(ip);
    return ip4_addr1(ip) == ip4_addr1(&dhcp_address.Client_Address) &&
           ip4_addr2(ip) == ip4_addr2(&dhcp_address.Client_Address) &&
           ip4_addr3(ip) == ip4_addr3(&dhcp_address.Client_Address) &&
           host >= start_ip4 && host <= end_ip4 &&
           host != ip4_addr4(&dhcp_address.DHCP_Server_Address);
}

int dhcps_add_client_by_mac(uint8_t *mac)
{
    dhcps_client_t *slot = NULL, *victim = NULL;
    ip4_addr_t ip;
    int i;

    if (mac == NULL) return -1;

    lease_lock();

    /* Already known? Reactivate in place — sticky: same IP is reused. */
    slot = dhcps_get_client_by_mac(mac);
    if (slot != NULL) {
        slot->is_active = 1;
        slot->seq       = ++s_seq;
        lease_unlock();
        return 0;
    }

    /* New MAC: find a free slot, else evict the oldest cached entry. */
    for (i = 0; i < DHCPS_MAX_CLIENTS; i++) {
        if (!dhcps_client[i].is_used) { slot = &dhcps_client[i]; break; }
        if (!dhcps_client[i].is_active &&
            (victim == NULL || dhcps_client[i].seq < victim->seq)) {
            victim = &dhcps_client[i];
        }
    }
    if (slot == NULL) slot = victim;          /* reclaim a cached slot */
    if (slot == NULL) {                        /* every slot is active */
        lease_unlock();
        DHCPS_INFO("table full, reject mac " MAC_STR_FMT "\r\n", MAC_STR_ARG(mac));
        return -1;
    }
    if (lease_alloc_ip(&ip) != 0) {
        lease_unlock();
        DHCPS_INFO("ip pool exhausted, reject mac " MAC_STR_FMT "\r\n", MAC_STR_ARG(mac));
        return -1;
    }

    memset(slot, 0, sizeof(*slot));
    memcpy(slot->Client_Mac, mac, 6);
    slot->Client_Address = ip;
    slot->is_used   = 1;
    slot->is_active = 1;
    slot->seq       = ++s_seq;

    lease_unlock();
    return 0;
}

int dhcps_del_client_by_mac(uint8_t *mac)
{
    dhcps_client_t *c;

    if (mac == NULL) return -1;

    lease_lock();
    c = dhcps_get_client_by_mac(mac);
    /* Don't wipe the binding — keep it cached (is_used stays 1) so a quick
     * reconnect reuses the same IP. Only the association flag is cleared. */
    if (c != NULL) c->is_active = 0;
    lease_unlock();
    return 0;
}

bool dhcp_search_ip_on_mac(uint8_t *mac, ip4_addr_t *ip)
{
    dhcps_client_t *c;
    bool found = false;

    if (mac == NULL || ip == NULL) return false;

    lease_lock();
    c = dhcps_get_client_by_mac(mac);
    if (c != NULL) {
        ip->addr = c->Client_Address.addr;
        found = true;
    }
    lease_unlock();
    return found;
}

/* Snapshot the used entries of the lease table into caller storage (for the
 * AP client-list debug command). Snapshotting keeps printf out of the
 * critical section. Returns the number of entries written. */
int dhcps_get_clients(dhcps_client_t *out, int max)
{
    int i, n = 0;

    if (out == NULL || max <= 0) return 0;

    lease_lock();
    for (i = 0; i < DHCPS_MAX_CLIENTS && n < max; i++) {
        if (dhcps_client[i].is_used) out[n++] = dhcps_client[i];
    }
    lease_unlock();
    return n;
}

/* ---- option get/set API (kept for compatibility) ---------------------- */
void *dhcps_option_info(uint8_t op_id, u32_t opt_len)
{
    void *option_arg = NULL;

    switch (op_id) {
        case IP_ADDRESS_LEASE_TIME:
            if (opt_len == sizeof(dhcps_time_t))  option_arg = &dhcps_lease_time;
            break;
        case REQUESTED_IP_ADDRESS:
            if (opt_len == sizeof(dhcps_lease_t)) option_arg = &dhcps_poll;
            break;
        case ROUTER_SOLICITATION_ADDRESS:
            if (opt_len == sizeof(dhcps_offer_t)) option_arg = &dhcps_offer;
            break;
        case DOMAIN_NAME_SERVER:
            if (opt_len == sizeof(dhcps_offer_t)) option_arg = &dhcps_dns;
            break;
        default:
            break;
    }
    return option_arg;
}

void dhcps_set_option_info(uint8_t op_id, void *opt_info, u32_t opt_len)
{
    if (opt_info == NULL) return;

    switch (op_id) {
        case IP_ADDRESS_LEASE_TIME:
            if (opt_len == sizeof(dhcps_time_t))
                dhcps_lease_time = *(dhcps_time_t *)opt_info;
            break;
        case REQUESTED_IP_ADDRESS:
            if (opt_len == sizeof(dhcps_lease_t))
                dhcps_poll = *(dhcps_lease_t *)opt_info;
            break;
        case ROUTER_SOLICITATION_ADDRESS:
            if (opt_len == sizeof(dhcps_offer_t))
                dhcps_offer = *(dhcps_offer_t *)opt_info;
            break;
        case DOMAIN_NAME_SERVER:
            if (opt_len == sizeof(dhcps_offer_t))
                dhcps_dns = *(dhcps_offer_t *)opt_info;
            break;
        default:
            break;
    }
}

/* ---- DHCP option encoders --------------------------------------------- */
static uint8_t *add_msg_type(uint8_t *optptr, uint8_t type)
{
    *optptr++ = DHCP_OPTION_MSG_TYPE;
    *optptr++ = 1;
    *optptr++ = type;
    return optptr;
}

static uint8_t *add_server_id(uint8_t *optptr)
{
    *optptr++ = DHCP_OPTION_SERVER_ID;
    *optptr++ = 4;
    *optptr++ = ip4_addr1(&dhcp_address.DHCP_Server_Address);
    *optptr++ = ip4_addr2(&dhcp_address.DHCP_Server_Address);
    *optptr++ = ip4_addr3(&dhcp_address.DHCP_Server_Address);
    *optptr++ = ip4_addr4(&dhcp_address.DHCP_Server_Address);
    return optptr;
}

static uint8_t *add_offer_options(uint8_t *optptr)
{
    u32_t lease = dhcps_lease_time * DHCPS_LEASE_UNIT;   /* seconds */

    *optptr++ = DHCP_OPTION_SUBNET_MASK;
    *optptr++ = 4;
    *optptr++ = ip4_addr1(&dhcp_address.Subnet_Mask);
    *optptr++ = ip4_addr2(&dhcp_address.Subnet_Mask);
    *optptr++ = ip4_addr3(&dhcp_address.Subnet_Mask);
    *optptr++ = ip4_addr4(&dhcp_address.Subnet_Mask);

    *optptr++ = DHCP_OPTION_LEASE_TIME;
    *optptr++ = 4;
    *optptr++ = (uint8_t)((lease >> 24) & 0xFF);
    *optptr++ = (uint8_t)((lease >> 16) & 0xFF);
    *optptr++ = (uint8_t)((lease >> 8)  & 0xFF);
    *optptr++ = (uint8_t)((lease >> 0)  & 0xFF);

    optptr = add_server_id(optptr);

    if (dhcps_router_enabled(dhcps_offer) &&
        !ip4_addr_isany_val(dhcp_address.Gateway_Address)) {
        *optptr++ = DHCP_OPTION_ROUTER;
        *optptr++ = 4;
        *optptr++ = ip4_addr1(&dhcp_address.Gateway_Address);
        *optptr++ = ip4_addr2(&dhcp_address.Gateway_Address);
        *optptr++ = ip4_addr3(&dhcp_address.Gateway_Address);
        *optptr++ = ip4_addr4(&dhcp_address.Gateway_Address);
    }

    *optptr++ = DHCP_OPTION_DNS_SERVER;
    *optptr++ = 4;
    if (dhcps_dns_enabled(dhcps_dns) && !ip4_addr_isany_val(dhcp_address.DNS_Server_Address)) {
        *optptr++ = ip4_addr1(&dhcp_address.DNS_Server_Address);
        *optptr++ = ip4_addr2(&dhcp_address.DNS_Server_Address);
        *optptr++ = ip4_addr3(&dhcp_address.DNS_Server_Address);
        *optptr++ = ip4_addr4(&dhcp_address.DNS_Server_Address);
    } else {
        *optptr++ = ip4_addr1(&dhcp_address.DHCP_Server_Address);
        *optptr++ = ip4_addr2(&dhcp_address.DHCP_Server_Address);
        *optptr++ = ip4_addr3(&dhcp_address.DHCP_Server_Address);
        *optptr++ = ip4_addr4(&dhcp_address.DHCP_Server_Address);
    }

    /* Wire option set preserved verbatim from the original to avoid client
     * compatibility regressions (some stations depend on these). */
    *optptr++ = DHCP_OPTION_INTERFACE_MTU;
    *optptr++ = 2;
    *optptr++ = 0x05;
    *optptr++ = 0xdc;

    *optptr++ = DHCP_OPTION_PERFORM_ROUTER_DISCOVERY;
    *optptr++ = 1;
    *optptr++ = 0x00;

    *optptr++ = 43;            /* vendor-specific */
    *optptr++ = 6;
    *optptr++ = 0x01;
    *optptr++ = 4;
    *optptr++ = 0x00;
    *optptr++ = 0x00;
    *optptr++ = 0x00;
    *optptr++ = 0x02;

    return optptr;
}

static uint8_t *add_end(uint8_t *optptr)
{
    *optptr++ = DHCP_OPTION_END;
    return optptr;
}

/* Build the BOOTREPLY header in place over the request buffer. The client's
 * xid and chaddr are preserved (we only overwrite the fields below), which is
 * exactly what DHCP requires. yiaddr comes from the caller's table lookup. */
static void create_reply(struct dhcps_msg *m, const ip4_addr_t *yiaddr)
{
    m->op    = DHCP_REPLY;
    m->htype = DHCP_HTYPE_ETHERNET;
    m->hlen  = DHCP_HLEN_ETHERNET;
    m->hops  = 0;
    m->secs  = 0;
    m->flags = 0;
    /* xid, chaddr: kept from the request */

    memcpy(m->yiaddr, &yiaddr->addr, sizeof(m->yiaddr));
    memset(m->ciaddr, 0, sizeof(m->ciaddr));
    memcpy(m->siaddr, &dhcp_address.DHCP_Server_Address.addr, sizeof(m->siaddr));
    memset(m->giaddr, 0, sizeof(m->giaddr));
    memset(m->sname,  0, sizeof(m->sname));
    memset(m->file,   0, sizeof(m->file));
    memset(m->options, 0, sizeof(m->options));
    memcpy(m->options, &magic_cookie, sizeof(magic_cookie));
}

/* Zero-pad to the RFC 300-byte floor, copy via pbuf_take and broadcast
 * to :68. */
static void send_packet(struct dhcps_msg *m, uint8_t *end)
{
    size_t   opt_len, send_len;
    struct pbuf *p;
    ip_addr_t dst = IPADDR4_INIT(0x0);

    opt_len  = (size_t)((uint8_t *)end - (uint8_t *)m);
    send_len = (opt_len < DHCPS_REPLY_MIN_LEN) ? DHCPS_REPLY_MIN_LEN : opt_len;

    p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)send_len, PBUF_RAM);
    if (p == NULL) {
        DHCPS_LOG("send_packet: pbuf_alloc failed\r\n");
        return;
    }
    /* m is sizeof(struct dhcps_msg) (>= send_len) and zero-padded beyond end,
     * so the tail becomes valid DHCP PAD bytes. */
    pbuf_take(p, m, (u16_t)send_len);

    ip4_addr_set(ip_2_ip4(&dst), &dhcp_address.Broadcast_Address);
    udp_sendto(dhcps_pcb, p, &dst, DHCPS_CLIENT_PORT);
    pbuf_free(p);
}

/* OFFER/ACK sender: full option set on top of the BOOTREPLY header. */
static void send_reply(struct dhcps_msg *m, uint8_t type, const ip4_addr_t *yiaddr)
{
    uint8_t *end;

    create_reply(m, yiaddr);
    end = add_msg_type(&m->options[4], type);
    end = add_offer_options(end);
    end = add_end(end);
    send_packet(m, end);
}

/* NAK carries only msg-type + server-id and a zero yiaddr (RFC 2131 §4.3.5).
 * Its job is to knock the client out of INIT-REBOOT/RENEWING back to INIT so
 * it re-DISCOVERs instead of sitting on an address we can't confirm. */
static void send_nak(struct dhcps_msg *m)
{
    ip4_addr_t zero = {0};
    uint8_t  *end;

    create_reply(m, &zero);
    end = add_msg_type(&m->options[4], DHCPNAK);
    end = add_server_id(end);
    end = add_end(end);
    send_packet(m, end);
}

/* ---- request parsing --------------------------------------------------- */
/* Client hints extracted while walking the option TLV. */
typedef struct {
    uint8_t    msg_type;       /* option 53                                    */
    bool       has_server_id;  /* option 54 present (SELECTING-style REQUEST)  */
    ip4_addr_t server_id;
    bool       has_req_ip;     /* option 50 present (requested address)        */
    ip4_addr_t req_ip;
} dhcp_opts_t;

/* Walk the options TLV. Handles PAD (single byte, no length) and bounds-checks
 * every length read so a truncated/malformed option can't run off the buffer.
 * Fills *out with the options this server cares about (53/50/54) and returns
 * the server state derived from the DHCP message type (option 53). */
static uint8_t parse_options(uint8_t *opt, int16_t len, dhcp_opts_t *out)
{
    uint8_t *end = opt + len;

    while (opt < end) {
        uint8_t code = *opt;
        if (code == DHCP_OPTION_END) break;
        if (code == DHCP_OPTION_PAD) { opt++; continue; }
        if (opt + 1 >= end) break;              /* no length byte present */
        {
            uint8_t olen = opt[1];
            if (opt + 2 + olen > end) break;    /* value runs past the buffer */
            if (code == DHCP_OPTION_MSG_TYPE && olen >= 1) {
                out->msg_type = opt[2];
            } else if (code == DHCP_OPTION_REQ_IPADDR && olen >= 4) {
                memcpy(&out->req_ip.addr, &opt[2], 4);
                out->has_req_ip = true;
            } else if (code == DHCP_OPTION_SERVER_ID && olen >= 4) {
                memcpy(&out->server_id.addr, &opt[2], 4);
                out->has_server_id = true;
            }
            opt += 2 + olen;                    /* skip code+len+value */
        }
    }

    switch (out->msg_type) {
        case DHCPDISCOVER: return DHCPS_STATE_OFFER;
        case DHCPREQUEST:  return DHCPS_STATE_ACK;
        case DHCPDECLINE:  return DHCPS_STATE_DECLINE;
        case DHCPRELEASE:  return DHCPS_STATE_RELEASE;
        default:           return DHCPS_STATE_IDLE;
    }
}

static uint8_t parse_msg(struct dhcps_msg *msg, uint16_t len, dhcp_opts_t *opts)
{
    memset(opts, 0, sizeof(*opts));
    if (len < DHCPS_MIN_REQUEST_LEN) return 0;             /* need header + cookie */
    if (memcmp(msg->options, &magic_cookie, sizeof(magic_cookie)) != 0) return 0;
    return parse_options(&msg->options[4], (int16_t)(len - DHCPS_MIN_REQUEST_LEN), opts);
}

/* ---- UDP receive callback (runs in the lwIP tcpip thread) -------------- */
static void handle_dhcp(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        const ip_addr_t *addr, uint16_t port)
{
    struct dhcps_msg *msg;
    dhcp_opts_t opts;
    ip4_addr_t want = {0}, reply_ip = {0};
    dhcps_client_t *c;
    bool known, have_want, nak;
    u16_t copy_len, len;
    uint8_t state;

    (void)arg; (void)pcb; (void)addr; (void)port;

    if (p == NULL) return;

    msg = (struct dhcps_msg *)mem_malloc(sizeof(struct dhcps_msg));
    if (msg == NULL) {
        pbuf_free(p);
        return;
    }
    memset(msg, 0, sizeof(*msg));

    copy_len = (p->tot_len < sizeof(struct dhcps_msg)) ? p->tot_len
                                                       : (u16_t)sizeof(struct dhcps_msg);
    len = pbuf_copy_partial(p, msg, copy_len, 0);
    pbuf_free(p);

    if (len < DHCPS_MIN_REQUEST_LEN) {
        mem_free(msg);
        return;
    }

    /* Whitelist check under the lock, then release before any UDP I/O.
     * Never hold s_lease_mutex across network calls. */
    lease_lock();
    known = (dhcps_get_client_by_mac(msg->chaddr) != NULL);
    lease_unlock();

    if (!known) {
        // DHCPS_LOG("drop: unknown mac " MAC_STR_FMT "\r\n", MAC_STR_ARG(msg->chaddr));
        mem_free(msg);
        return;
    }

    state = (uint8_t)parse_msg(msg, len, &opts);
    switch (state) {
        case DHCPS_STATE_OFFER:
            /* DISCOVER: honor an option-50 hint when that address is free, so
             * stations keep their previous address across table rebuilds (the
             * join-order allocator alone reshuffles them). */
            lease_lock();
            c = dhcps_get_client_by_mac(msg->chaddr);
            if (c != NULL && opts.has_req_ip && ip_in_pool(&opts.req_ip) &&
                dhcps_get_client_by_ip(&opts.req_ip) == NULL) {
                c->Client_Address = opts.req_ip;
                DHCPS_INFO("lease " MAC_STR_FMT " keeps %u.%u.%u.%u (option 50)\r\n",
                           MAC_STR_ARG(msg->chaddr),
                           (unsigned)ip4_addr1(&opts.req_ip),
                           (unsigned)ip4_addr2(&opts.req_ip),
                           (unsigned)ip4_addr3(&opts.req_ip),
                           (unsigned)ip4_addr4(&opts.req_ip));
            }
            if (c != NULL) reply_ip = c->Client_Address;
            lease_unlock();
            if (c != NULL) send_reply(msg, DHCPOFFER, &reply_ip);
            break;

        case DHCPS_STATE_ACK:
            /* DHCPREQUEST. SELECTING-style requests name a server (option 54);
             * if it isn't us, the request is none of our business. */
            if (opts.has_server_id &&
                !ip4_addr_cmp(&opts.server_id, &dhcp_address.DHCP_Server_Address)) {
                break;
            }

            /* The address the client claims: option 50 in SELECTING and
             * INIT-REBOOT, ciaddr in RENEWING/REBINDING. */
            have_want = false;
            if (opts.has_req_ip) {
                want = opts.req_ip;
                have_want = true;
            } else if (msg->ciaddr[0] | msg->ciaddr[1] | msg->ciaddr[2] | msg->ciaddr[3]) {
                memcpy(&want.addr, msg->ciaddr, 4);
                have_want = true;
            }

            lease_lock();
            c   = dhcps_get_client_by_mac(msg->chaddr);
            nak = false;
            if (c != NULL) {
                if (have_want && !ip4_addr_cmp(&want, &c->Client_Address)) {
                    if (ip_in_pool(&want) &&
                        dhcps_get_client_by_ip(&want) == NULL) {
                        /* Nobody holds the requested address (typically the
                         * client's pre-rebuild lease): grant it. */
                        c->Client_Address = want;
                    } else {
                        /* Held by another station or not ours to give — the
                         * client's notion is wrong. NAK it back to INIT for a
                         * clean DISCOVER; blind-ACKing a different address is
                         * what left stations parked on stale leases. */
                        nak = true;
                    }
                }
                reply_ip = c->Client_Address;
            }
            lease_unlock();

            if (c == NULL) break;
            if (nak) {
                send_nak(msg);
                DHCPS_INFO("NAK %u.%u.%u.%u -> " MAC_STR_FMT " (bound to %u.%u.%u.%u)\r\n",
                           (unsigned)ip4_addr1(&want), (unsigned)ip4_addr2(&want),
                           (unsigned)ip4_addr3(&want), (unsigned)ip4_addr4(&want),
                           MAC_STR_ARG(msg->chaddr),
                           (unsigned)ip4_addr1(&reply_ip), (unsigned)ip4_addr2(&reply_ip),
                           (unsigned)ip4_addr3(&reply_ip), (unsigned)ip4_addr4(&reply_ip));
            } else {
                send_reply(msg, DHCPACK, &reply_ip);
                DHCPS_INFO("ACK %u.%u.%u.%u -> " MAC_STR_FMT "\r\n",
                           (unsigned)msg->yiaddr[0], (unsigned)msg->yiaddr[1],
                           (unsigned)msg->yiaddr[2], (unsigned)msg->yiaddr[3],
                           MAC_STR_ARG(msg->chaddr));
                if (dhcps_cb) dhcps_cb(msg->yiaddr);
            }
            break;

        case DHCPS_STATE_DECLINE:
            /* Client probed the ACKed address and found it in use (RFC 2131
             * §4.4.1): rebind this MAC to a fresh address so the next
             * DISCOVER doesn't loop on the conflicted one. */
            lease_lock();
            c = dhcps_get_client_by_mac(msg->chaddr);
            if (c != NULL && opts.has_req_ip &&
                ip4_addr_cmp(&opts.req_ip, &c->Client_Address)) {
                ip4_addr_t fresh;
                if (lease_alloc_ip(&fresh) == 0) {
                    c->Client_Address = fresh;
                    DHCPS_INFO("decline %u.%u.%u.%u, rebind " MAC_STR_FMT "\r\n",
                               (unsigned)ip4_addr1(&opts.req_ip),
                               (unsigned)ip4_addr2(&opts.req_ip),
                               (unsigned)ip4_addr3(&opts.req_ip),
                               (unsigned)ip4_addr4(&opts.req_ip),
                               MAC_STR_ARG(msg->chaddr));
                }
            }
            lease_unlock();
            break;

        case DHCPS_STATE_RELEASE:
            dhcps_del_client_by_mac(msg->chaddr);
            break;
        default:
            break;
    }

    mem_free(msg);
}

/* ---- public start/stop ------------------------------------------------- */
void dhcps_set_new_lease_cb(dhcps_cb_t cb)
{
    dhcps_cb = cb;
}

void dhcps_start(struct netif *netif)
{
    if (netif == NULL) return;

    dhcps_netif = netif;

    if (s_lease_mutex == NULL) {
        s_lease_mutex = osMutexNew(NULL);
    }

    if (dhcps_pcb != NULL) {
        udp_remove(dhcps_pcb);
        dhcps_pcb = NULL;
    }

    dhcps_pcb = udp_new();
    if (dhcps_pcb == NULL) {
        DHCPS_INFO("start: udp_new failed\r\n");
        return;
    }

    /* Addresses are derived from the AP netif (ip/gw/netmask set by the
     * caller just before dhcps_start). Client_Address holds the pool prefix. */
    dhcp_address.DHCP_Server_Address.addr = netif->ip_addr.addr;
    dhcp_address.Gateway_Address.addr     = netif->gw.addr;
    dhcp_address.Subnet_Mask              = netif->netmask;
    IP4_ADDR(&dhcp_address.Broadcast_Address, 255, 255, 255, 255);
    IP4_ADDR(&dhcp_address.DNS_Server_Address, 8, 8, 8, 8);
    IP4_ADDR(&dhcp_address.Client_Address,
             ip4_addr1(&netif->ip_addr), ip4_addr2(&netif->ip_addr),
             ip4_addr3(&netif->ip_addr), now_ip4);

    dhcps_pcb->netif_idx = netif->num + 1;
    ip_set_option(dhcps_pcb, SOF_BROADCAST);
    udp_bind(dhcps_pcb, &netif->ip_addr, DHCPS_SERVER_PORT);
    udp_recv(dhcps_pcb, handle_dhcp, NULL);

    DHCPS_INFO("started on %u.%u.%u.%u/%u.%u.%u.%u, pool .%u-.%u\r\n",
               (unsigned)ip4_addr1(&netif->ip_addr), (unsigned)ip4_addr2(&netif->ip_addr),
               (unsigned)ip4_addr3(&netif->ip_addr), (unsigned)ip4_addr4(&netif->ip_addr),
               (unsigned)ip4_addr1(&dhcp_address.Subnet_Mask), (unsigned)ip4_addr2(&dhcp_address.Subnet_Mask),
               (unsigned)ip4_addr3(&dhcp_address.Subnet_Mask), (unsigned)ip4_addr4(&dhcp_address.Subnet_Mask),
               (unsigned)start_ip4, (unsigned)end_ip4);
}

void dhcps_stop(struct netif *netif)
{
    (void)netif;

    if (dhcps_pcb != NULL) {
        udp_disconnect(dhcps_pcb);
        udp_remove(dhcps_pcb);
        dhcps_pcb = NULL;
    }

    lease_lock();
    memset(dhcps_client, 0, sizeof(dhcps_client));
    now_ip4 = start_ip4;
    s_seq   = 0;
    lease_unlock();

    DHCPS_INFO("stopped, lease table cleared\r\n");
}

void dhcps_dns_setserver(const ip_addr_t *dnsserver)
{
    if (dnsserver != NULL) {
        dhcp_address.DNS_Server_Address = *(ip_2_ip4(dnsserver));
    } else {
        dhcp_address.DNS_Server_Address = *(ip_2_ip4(IP_ADDR_ANY));
    }
}

ip4_addr_t dhcps_dns_getserver(void)
{
    return dhcp_address.DNS_Server_Address;
}
