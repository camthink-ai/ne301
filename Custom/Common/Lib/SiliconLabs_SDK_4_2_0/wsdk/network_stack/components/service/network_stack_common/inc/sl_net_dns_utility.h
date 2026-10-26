#pragma once
#include "sl_ip_types.h"
#include "sl_status.h"
#include "sli_types.h"

/**
 * @brief
 *   Resolve the given host name to an IP address.
 * 
 * @details
 *   This function resolves a host name to its corresponding IP address. It requires
 *   the DNS client feature to be enabled in the TCP/IP feature bitmap before calling.
 * 
 * 
 * @pre Pre-conditions:
 * - The [sl_net_up](../wiseconnect-api-reference-guide-nwk-mgmt/net-interface-functions#sl-net-up) API must be called before this API.
 * - If [sl_net_up](../wiseconnect-api-reference-guide-nwk-mgmt/net-interface-functions#sl-net-up) is not used, then [sl_si91x_configure_ip_address](../wiseconnect-api-reference-guide-si91x-driver/si91-x-network-functions#sl-si91x-configure-ip-address) should be called prior to this API.
 * - The [SL_SI91X_TCP_IP_FEAT_DNS_CLIENT](../wiseconnect-api-reference-guide-si91x-driver/si91-x-tcp-ip-feature-bitmap#sl-si91-x-tcp-ip-feat-dns-client) bit should be enabled in the TCP/IP feature bitmap.
 * 
 * @param[in] host_name
 *   Host name that needs to be resolved.
 * @param[in] initial_timeout_sec
 *   Timeout in seconds. Can be configured in the range 5 to 10 seconds. If the value is zero, the response will be sent through @ref sl_net_event_handler_t.
 * @param[in] retry_count
 *   Number of retries after the first attempt (total attempts = 1 + retry_count).
 * @param[in] dns_resolution_ip
 *   DNS resolution by IP of type @ref sl_net_dns_resolution_ip_type_t.
 * @param[out] sl_ip_address
 *   IP address object to store resolved IP address of type [sl_ip_address_t](../wiseconnect-api-reference-guide-nwk-mgmt/sl-ip-address-t).
 *
 * @note
 *   Retry timeouts use exponential backoff: the first attempt uses initial_timeout_sec (T in
 *   seconds), then 2T, 4T, 8T, ... for each subsequent retry. Per the DNS specification, any
 *   single attempt timeout exceeding 45 s is capped at 45 s. The overall per-server DNS
 *   resolution timeout is capped at 70 s.
 * @note
 * If the initial_timeout_sec value is set to zero, the API will behave asynchronously (non-blocking) and return immediately with SL_STATUS_IN_PROGRESS. A value greater than zero will make the API behave synchronously (blocking).
 * @note
 *   The timeout applies per DNS server, not across all configured DNS servers. If both a primary
 *   and a secondary DNS server are configured (see @ref sl_net_set_dns_server), the NWP will
 *   independently exhaust the full retry schedule on each server before returning a failure.
 *   As a result, the worst-case overall resolution time scales linearly with the number of
 *   configured DNS servers (i.e., approximately per_server_total_timeout * number_of_dns_servers).
 *   - Example (single DNS server configured): with initial_timeout_sec = 10 s and retry_count = 2,
 *     the overall timeout is 70 s (10 + 20 + 40).
 *   - Example (both primary and secondary DNS servers configured): with the same parameters
 *     (initial_timeout_sec = 10 s, retry_count = 2), the NWP first exhausts the full 70 s
 *     schedule on the primary server and then, on failure, repeats it on the secondary server,
 *     resulting in a worst-case overall timeout of 140 s (70 s * 2). In general, with both
 *     DNS servers configured, the effective total remains in the range 30 to 140 seconds
 *     (minimum: initial_timeout_sec = 5 s and retry_count = 1, giving (5 + 10) * 2 = 30 s;
 *     maximum: initial_timeout_sec = 10 s and retry_count = 2, giving (10 + 20 + 40) * 2 = 140 s).
 * @note
 *   If retry_count is set to 0, the NWP internally overrides it to 2 (i.e., three total
 *   attempts: the initial attempt plus two retries with exponential backoff).
 * @return
 *   sl_status_t. See [Status Codes](https://docs.silabs.com/gecko-platform/latest/platform-common/status) and [WiSeConnect Status Codes](../wiseconnect-api-reference-guide-err-codes/wiseconnect-status-codes) for details.
 */
sl_status_t sl_net_dns_resolve_hostname_v2(const char *host_name,
                                           const uint8_t initial_timeout_sec,
                                           const uint8_t retry_count,
                                           const sl_net_dns_resolution_ip_type_t dns_resolution_ip,
                                           sl_ip_address_t *sl_ip_address);

#define SLI_NET_MIN_DNS_INITIAL_TIMEOUT 5  ///< minimum DNS initial timeout in seconds.
#define SLI_NET_MAX_DNS_INITIAL_TIMEOUT 10 ///< maximum DNS initial timeout in seconds.

/** \addtogroup SL_NET_TYPES Types
  * @{ */

/**
  * @brief Structure to hold DNS server addresses for configuration.
  * 
  * @details
  * This structure contains pointers to the primary and secondary DNS server addresses.
  * It is used as a parameter in the sl_net_set_dns_server function to set the DNS server IP addresses.
  */
typedef struct {
  sl_ip_address_t *primary_server_address;   ///< Primary DNS server address
  sl_ip_address_t *secondary_server_address; ///< Secondary DNS server address
} sl_net_dns_address_t;

/** @} */

sl_status_t sli_convert_si91x_dns_response(sl_ip_address_t *ip_address,
                                           const sli_si91x_dns_response_t *si91x_dns_response);
