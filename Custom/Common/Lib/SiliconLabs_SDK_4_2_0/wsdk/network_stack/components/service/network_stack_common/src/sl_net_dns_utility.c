#include <string.h>
#include "sl_net_dns_utility.h"
#include "sl_constants.h"
#include "sli_constants.h"
#include "sli_utility.h"
#include "sl_string.h"
#include "sli_net_ip_config.h"
#include "sl_additional_status.h"

sl_status_t sli_convert_si91x_dns_response(sl_ip_address_t *ip_address,
                                           const sli_si91x_dns_response_t *si91x_dns_response)
{
  SL_VERIFY_POINTER_OR_RETURN(ip_address, SL_STATUS_WIFI_NULL_PTR_ARG);
  SL_VERIFY_POINTER_OR_RETURN(si91x_dns_response, SL_STATUS_WIFI_NULL_PTR_ARG);

  // Check if DNS response has IP addresses
  if ((si91x_dns_response->ip_count[0] | (si91x_dns_response->ip_count[1] << 8)) <= 0) {
    return SL_STATUS_OK;
  }

  // Determine IP address size (IPv4 or IPv6) and copy the address bytes
  uint8_t ip_address_size = (si91x_dns_response->ip_version[0] | si91x_dns_response->ip_version[1] << 8)
                                == SL_IPV4_ADDRESS_LENGTH
                              ? SL_IPV4_ADDRESS_LENGTH
                              : SL_IPV6_ADDRESS_LENGTH;
  uint8_t *sl_ip_address;
  const uint8_t *si91x_ip_address;

  ip_address->type = ip_address_size == SL_IPV4_ADDRESS_LENGTH ? SL_IPV4 : SL_IPV6;

  si91x_ip_address = ip_address_size == SL_IPV4_ADDRESS_LENGTH ? si91x_dns_response->ip_address[0].ipv4_address
                                                               : si91x_dns_response->ip_address[0].ipv6_address;
  sl_ip_address    = ip_address_size == SL_IPV4_ADDRESS_LENGTH ? ip_address->ip.v4.bytes : ip_address->ip.v6.bytes;

  memcpy(sl_ip_address, si91x_ip_address, ip_address_size);

  return SL_STATUS_OK;
}

sl_status_t sl_net_dns_resolve_hostname_v2(const char *host_name,
                                           const uint8_t initial_timeout_sec,
                                           const uint8_t retry_count,
                                           const sl_net_dns_resolution_ip_type_t dns_resolution_ip,
                                           sl_ip_address_t *sl_ip_address)
{

  // Check for NULL pointers
  SL_WIFI_ARGS_CHECK_NULL_POINTER(sl_ip_address);
  SL_WIFI_ARGS_CHECK_NULL_POINTER(host_name);

#if defined(SLI_SI91X_NETWORK_DUAL_STACK) || defined(SLI_SI91X_OFFLOAD_NETWORK_STACK)
  if (sli_is_bypass_mode_enabled()) {
    return SL_STATUS_WIFI_UNSUPPORTED;
  }

  sl_ip_address_type_t client_ip_type = sli_get_stored_ip_config(SLI_SI91X_CLIENT)->type;

  // If client interface has not been brought up (ip_type == 0), allow the request
  // to proceed - the firmware will handle it or return an appropriate error.
  // Reject DNS type if the client interface is up but does not support that IP version
  if (client_ip_type != 0
      && ((dns_resolution_ip == SL_NET_DNS_TYPE_IPV4 && (client_ip_type & SL_IPV4) == 0)
          || (dns_resolution_ip == SL_NET_DNS_TYPE_IPV6 && (client_ip_type & SL_IPV6) == 0))) {
    return SL_STATUS_INVALID_CONFIGURATION;
  }
#endif
  size_t len = sl_strnlen(host_name, SLI_SI91X_DNS_REQUEST_MAX_URL_LEN + 1);
  if (len > SLI_SI91X_DNS_REQUEST_MAX_URL_LEN) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sl_status_t status                                 = SL_STATUS_FAIL;
  sl_wifi_system_packet_t *packet                    = NULL;
  sl_wifi_buffer_t *buffer                           = NULL;
  const sli_si91x_dns_response_t *dns_response       = NULL;
  sli_si91x_dns_query_request_t dns_query_request_v2 = { 0 };
  uint8_t dns_timeout                                = 0;

  // Determine the wait period based on the timeout value
  sli_wifi_wait_period_t wait_period =
    initial_timeout_sec == 0 ? SLI_WIFI_RETURN_IMMEDIATELY : (SLI_WIFI_WAIT_FOR_EVER | SLI_WIFI_WAIT_FOR_RESPONSE_BIT);

  if (wait_period != SLI_WIFI_RETURN_IMMEDIATELY) {
    dns_timeout = initial_timeout_sec;

    if (dns_timeout < SLI_NET_MIN_DNS_INITIAL_TIMEOUT) {
      return SL_STATUS_INVALID_PARAMETER;
    }

    if (dns_timeout > SLI_NET_MAX_DNS_INITIAL_TIMEOUT) {
      dns_timeout = SLI_NET_MAX_DNS_INITIAL_TIMEOUT; // Set the maximum timeout to 10 seconds
    }
  }

  // Determine the IP version to be used (IPv4 or IPv6)
  dns_query_request_v2.ip_version[0]       = (dns_resolution_ip == SL_NET_DNS_TYPE_IPV4) ? 4 : 6;
  dns_query_request_v2.initial_timeout_sec = dns_timeout;
  dns_query_request_v2.retry_count         = retry_count;
  memcpy(dns_query_request_v2.url_name, host_name, len);

  status = sli_wifi_send_command(SLI_WIFI_REQ_DNS_QUERY,
                                 SLI_SI91X_NETWORK_CMD,
                                 &dns_query_request_v2,
                                 sizeof(dns_query_request_v2),
                                 wait_period,
                                 NULL,
                                 (void **)&buffer);

  // Check if the command failed and free the buffer if it was allocated
  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer(buffer);
  }
  VERIFY_STATUS_AND_RETURN(status);

  // Extract the DNS response from the SI91X packet buffer
  packet       = sli_wifi_host_get_buffer_data(buffer, 0, NULL);
  dns_response = (sli_si91x_dns_response_t *)packet->data;

  // Convert the SI91X DNS response to the sl_ip_address format
  sli_convert_si91x_dns_response(sl_ip_address, dns_response);
  sli_buffer_manager_free_buffer(buffer);
  return SL_STATUS_OK;
}
