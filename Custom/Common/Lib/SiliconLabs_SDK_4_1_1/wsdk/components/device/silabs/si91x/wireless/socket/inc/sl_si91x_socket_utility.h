/********************************************************************************
 * @file  sl_si91x_socket_utility.h
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/
#ifndef _SL_SI91X_SOCKET_UTILITY_H_
#define _SL_SI91X_SOCKET_UTILITY_H_

#pragma once

#include "sl_si91x_types.h"
#include "sl_si91x_socket_types.h"
#include "sl_si91x_protocol_types.h"
#include "sl_si91x_socket_constants.h"
#include "sl_ip_types.h"
#include "sl_wifi_types.h"
#ifdef SLI_SI91X_NETWORK_DUAL_STACK
#include "lwip/errno.h"
#else
#include "errno.h"
#endif
#include <stdbool.h>

#define SLI_SET_ERROR_AND_RETURN(error)     \
  do {                                      \
    if (PRINT_ERROR_LOGS) {                 \
      PRINT_ERROR_STATUS(ERROR_TAG, error); \
    }                                       \
    errno = error;                          \
    return -1;                              \
  } while (0)

#define SLI_SET_ERRNO_AND_RETURN_IF_TRUE(condition, errno_value) \
  do {                                                           \
    if (condition) {                                             \
      if (PRINT_ERROR_LOGS) {                                    \
        PRINT_ERROR_STATUS(ERROR_TAG, errno_value);              \
      }                                                          \
      errno = errno_value;                                       \
      return -1;                                                 \
    }                                                            \
  } while (0)

#define SLI_SOCKET_VERIFY_STATUS_AND_RETURN(status, expected_status, errno_value) \
  do {                                                                            \
    if (status != expected_status) {                                              \
      if (PRINT_ERROR_LOGS) {                                                     \
        PRINT_ERROR_STATUS(ERROR_TAG, status);                                    \
      }                                                                           \
      errno = errno_value;                                                        \
      return -1;                                                                  \
    }                                                                             \
  } while (0)

#ifndef __ZEPHYR__
#define SLI_SI91X_NULL_SAFE_FD_ZERO(fd_set) \
  do {                                      \
    if (NULL != fd_set) {                   \
      FD_ZERO(fd_set);                      \
    }                                       \
  } while (0)
#else
#define SLI_SI91X_NULL_SAFE_FD_ZERO(fd_set) \
  do {                                      \
    if (NULL != fd_set) {                   \
      SL_SI91X_FD_ZERO(fd_set);             \
    }                                       \
  } while (0)
#endif

// Macro to check if malloc failed
#define SLI_VERIFY_MALLOC_AND_RETURN(ptr) \
  do {                                    \
    if (ptr == NULL) {                    \
      return SL_STATUS_ALLOCATION_FAILED; \
    }                                     \
  } while (0)

#define SLI_GET_SAFE_MEMCPY_LENGTH(destination_size, source_size) \
  source_size > destination_size ? destination_size : source_size

#ifndef IS_POWER_OF_TWO
#define IS_POWER_OF_TWO(x) (x && (!(x & (x - 1))))
#endif

#ifndef ROUND_UP
#define ROUND_UP(x, y) ((x) % (y) ? (x) + (y) - ((x) % (y)) : (x))
#endif /* ifndef ROUND_UP */

extern sli_si91x_socket_t *sli_si91x_sockets[SLI_NUMBER_OF_SOCKETS];

sl_status_t sli_si91x_socket_init(uint8_t max_select_count);

sl_status_t sli_si91x_socket_deinit(void);

sl_status_t sli_si91x_vap_shutdown(uint8_t vap_id, sli_si91x_bsd_disconnect_reason_t disconnect_reason);

/**
 * @brief True if @p socket is on @p filter_vap_id for @p opermode and, when @p dest_ip_address is non-NULL,
 *        its remote address matches that IP (same selection rules as Wi-Fi command-engine socket queue flush).
 */
bool sli_si91x_socket_matches_vap_and_remote_ip(sl_wifi_operation_mode_t opermode,
                                                const sli_si91x_socket_t *socket,
                                                uint8_t filter_vap_id,
                                                const sl_ip_address_t *dest_ip_address);

/**
 * @brief Set host BSD sockets to DISCONNECTED for the same Wi-Fi responses that trigger
 *        \c sli_handle_packet_flush_logic socket queue flush (matched by VAP and optional remote IP).
 *        Intended for the network / event-engine dispatch path.
 */
void sli_si91x_sync_bsd_socket_states_for_flush_scenarios(const sl_wifi_system_packet_t *packet);

/**
 * @addtogroup SOCKET_CONFIGURATION_FUNCTION
 * @{ 
 */
/**
 * @brief SiWx91x Socket Configuration
 *
 * The structure defines the configuration parameters for SiWx91x sockets.
 * It includes settings for the total number of sockets, TCP and UDP socket
 * configurations, and performance-related parameters.
 */
typedef struct {
  uint8_t total_sockets; ///< Total number of sockets (combination of TOTAL_TCP_SOCKETS + TOTAL_UDP_SOCKETS).

  uint8_t
    total_tcp_sockets; ///< Total number of TCP sockets (combination of TCP_TX_ONLY_SOCKETS + TCP_RX_ONLY_SOCKETS).

  uint8_t
    total_udp_sockets; ///< Total number of UDP sockets (combination of UDP_TX_ONLY_SOCKETS + UDP_RX_ONLY_SOCKETS).

  uint8_t
    tcp_tx_only_sockets; ///< The number of TCP sockets is primarily intended for transmission (TX). While these sockets can support both transmission (TX) and reception (RX).

  uint8_t
    tcp_rx_only_sockets; ///< The number of TCP sockets is primarily intended for Reception (RX). While these sockets can support both transmission (TX) and reception (RX).

  uint8_t
    udp_tx_only_sockets; ///< The number of UDP sockets is primarily intended for transmission (TX). While these sockets can support both transmission (TX) and reception (RX).

  uint8_t
    udp_rx_only_sockets; ///< The number of UDP sockets is primarily intended for Reception (RX). While these sockets can support both transmission (TX) and reception (RX).

  uint8_t
    tcp_rx_high_performance_sockets; ///< Total number of high-performance TCP RX sockets, specifically configured to handle high-throughput data reception.

  uint8_t
    tcp_rx_window_size_cap; ///< The TCP receive window size is capped and scales linearly based on the product of the TCP Maximum Segment Size (MSS) and the TCP_RX_WINDOW_SIZE_CAP

  uint8_t
    tcp_rx_window_div_factor; ///< The TCP receive window division factor increases the frequency of ACKs for asynchronous sockets.
} sl_si91x_socket_config_t;

/** @} */

/**
 * @addtogroup SOCKET_CONFIGURATION_FUNCTION
 * @{
 */

/**
 * @brief SiWx91x-specific socket type length value structure.
 *
 * | Type                                    | Value                                                 | Length                                          |
 * |-----------------------------------------|-------------------------------------------------------|-------------------------------------------------|
 * | @ref SL_SI91X_TLS_EXTENSION_SNI_TYPE    | The server name or hostname, provided as a string     | Length of the server name or hostname string    |
 * | @ref SL_SI91X_TLS_EXTENSION_ALPN_TYPE   | The application protocol name, provided as a string   | Length of the application protocol name string  |
 *
 * @note For `SNI`, provide the server name or hostname as a string (e.g., `"example.com"`).
 * @note For `ALPN`, provide the application protocol string (e.g., `"http/1.1"` or `"mqtt"`).
 */
typedef struct {
  uint16_t type;   ///< Specifies the TLS extension type.
  uint16_t length; ///< Length of the value[] field.
  uint8_t value[]; ///< Data corresponding to the specified extension type.
} sl_si91x_socket_type_length_value_t;

/** @} */

/**
 * @addtogroup SOCKET_CONFIGURATION_FUNCTION
 * @{ 
 */

/**
 * @brief
 *   Configures SiWx91x specific socket settings.
 *
 * @details
 *   This function sets up the socket configuration specific to the SiWx91x.
 *   It must be called before invoking any socket-creation API.
 *   The configuration includes setting parameters such as socket type,
 *   protocol, and other options specific to the SiWx91x series.
 *
 *   **Applies to all socket families.** The configuration values (TCP/UDP/TLS slot counts,
 *   MSS, RX/TX buffer sizes, TLS extensions) are applied to the underlying SiWx91x network
 *   stack and therefore affect **all three socket families** — BSD sockets (@ref socket),
 *   IoT sockets (@ref iotSocketCreate), and native SiWx91x sockets (@ref sl_si91x_socket /
 *   @ref sl_si91x_socket_async) — because they share the same firmware resources.
 *
 * @pre Pre-conditions:
 * - The Wi-Fi/Net stack must be initialized.
 * - This API must be called before creating any socket using @ref socket, @ref iotSocketCreate,
 *   @ref sl_si91x_socket, or @ref sl_si91x_socket_async.
 *
 * @post Post-conditions:
 * - On success the supplied configuration is applied to the SiWx91x socket pool and all
 *   subsequent socket-creation calls (regardless of family) observe the new limits.
 * - On failure the firmware configuration is unchanged.
 *
 * @param[in] socket_config 
 *   Socket configuration of type @ref sl_si91x_socket_config_t.
 *   Field values must satisfy the constraints documented in @ref sl_si91x_socket_config_t
 *   (for example, `total_sockets` must equal the sum of TCP and UDP sockets and must not
 *   exceed the stack-level @c SI91X_MAX_SOCKETS).
 *
 * @return
 *   sl_status_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.
 *
 * @retval SL_STATUS_OK                         Configuration applied successfully.
 * @retval SL_STATUS_NOT_INITIALIZED            Wi-Fi/Net stack is not initialized.
 * @retval SL_STATUS_INVALID_PARAMETER          One of the fields in @p socket_config is invalid (e.g., the calculated totals do not match or exceed the maximum value).
 * @retval SL_STATUS_SI91X_INVALID_CONFIG       Firmware rejected the configuration (for example, MSS/buffer sizes out of range).
 * @retval SL_STATUS_BUSY                       A previous socket-configuration request is still in progress.
 *
 * @note Thread safety:
 * - Not thread-safe. Must be invoked by a single thread at initialization time, before any socket is created.
 *
 * @note Side effects:
 * - Allocates/releases firmware buffers used by the SiWx91x socket pool.
 * - Affects every subsequent socket-creation call across BSD, IoT, and SiWx91x families.
 *
 * @see socket(), iotSocketCreate(), sl_si91x_socket(), sl_si91x_socket_async()
 *
 * @par Example
 * Configure the SiWx91x socket pool before creating application sockets:
 * @code{.c}
 * sl_si91x_socket_config_t socket_cfg = {
 *   .total_sockets                   = 3,
 *   .total_tcp_sockets               = 2,
 *   .total_udp_sockets               = 1,
 *   .tcp_tx_only_sockets             = 1,
 *   .tcp_rx_only_sockets             = 1,
 *   .udp_tx_only_sockets             = 1,
 *   .udp_rx_only_sockets             = 0,
 *   .tcp_rx_high_performance_sockets = 1,
 *   .tcp_rx_window_size_cap          = 10,
 *   .tcp_rx_window_div_factor        = 0,
 * };
 *
 * sl_status_t status = sl_si91x_config_socket(socket_cfg);
 * if (status != SL_STATUS_OK) {
 *   printf("sl_si91x_config_socket() failed, 0x%lx\r\n", (unsigned long)status);
 *   return status;
 * }
 * @endcode
 *
 ******************************************************************************/
sl_status_t sl_si91x_config_socket(sl_si91x_socket_config_t socket_config);

/** @} */

/**
 * A internal function to free the socket.
 * @param socket socket FD which needs to be reset.
 */
void sli_si91x_free_socket(int socket);

/**
 * An internal function to get the sl_si91x_socket structure based on the socket FD provided.
 * @param socket Pointer to store the address of the found sl_si91x_socket structure.
 * @param index Pointer to store the socket FD index number.
 * @return
 * sl_status_t indicating the result of the operation.
 *         - SL_STATUS_OK if a free socket is found.
 *         - SL_STATUS_NOT_FOUND if no free socket is available.
 */
sl_status_t sli_get_free_socket(sli_si91x_socket_t **socket, int *index);

void sli_si91x_send_tx_packet_status_handler(uint16_t packet_type, sl_status_t status, void *context);

/**
 * A internal function to get free socket.
 * @param socket_id 
 * Socket ID.
 */
sli_si91x_socket_t *sli_get_si91x_socket(int32_t socket_id);

/**
 * @brief Copies a TLS extension TLV into the socket extension buffer.
 *
 * @param[in] socket_tls_extensions Pointer to TLS extensions in the socket structure.
 * @param[in] tls_extension Pointer to the TLS extension TLV provided by the application.
 * @param[in] option_length Length of the TLS extension buffer passed by the application.
 *
 * @return SL_STATUS_OK on success.
 * @return SL_STATUS_NULL_POINTER if @p socket_tls_extensions or @p tls_extension is NULL.
 * @return SL_STATUS_INVALID_PARAMETER if the TLV layout, type, or length is invalid.
 * @return SL_STATUS_SI91X_MEMORY_ERROR if the socket extension buffer is full.
 */
sl_status_t sli_si91x_add_tls_extension(sli_si91x_tls_extensions_t *socket_tls_extensions,
                                        const sl_si91x_socket_type_length_value_t *tls_extension,
                                        socklen_t option_length);

/**
 * @brief Adds a TLS extension TLV and maps the result to BSD socket errno conventions.
 *
 * @param[in] socket_tls_extensions Pointer to TLS extensions in the socket structure.
 * @param[in] tls_extension Pointer to the TLS extension TLV provided by the application.
 * @param[in] option_length Length of the TLS extension buffer passed by the application.
 *
 * @return SLI_SI91X_NO_ERROR on success.
 * @return -1 on failure with @c errno set to @c ENOMEM or @c EINVAL.
 */
int sli_si91x_configure_tls_extension(sli_si91x_tls_extensions_t *socket_tls_extensions,
                                      const sl_si91x_socket_type_length_value_t *tls_extension,
                                      socklen_t option_length);

sl_status_t sli_create_and_send_socket_request(int socketIdIndex, int type, const int *backlog);

int sli_si91x_socket(int family, int type, int protocol, sl_si91x_socket_receive_data_callback_t callback);

int sli_si91x_shutdown(int socket, int how);

int sli_si91x_connect(int socket, const struct sockaddr *addr, socklen_t addr_len);

int sli_si91x_bind(int socket, const struct sockaddr *addr, socklen_t addr_len);

int sli_si91x_accept(int socket,
                     struct sockaddr *addr,
                     socklen_t *addr_len,
                     sl_si91x_socket_accept_callback_t callback);
#ifndef __ZEPHYR__
int sli_si91x_select(int nfds,
                     fd_set *readfds,
                     fd_set *writefds,
                     fd_set *exceptfds,
                     const struct timeval *timeout,
                     sl_si91x_socket_select_callback_t callback);
#else
int sli_si91x_select(int nfds,
                     sl_si91x_fdset_t *readfds,
                     sl_si91x_fdset_t *writefds,
                     sl_si91x_fdset_t *exceptfds,
                     const struct timeval *timeout,
                     sl_si91x_socket_select_callback_t callback);
#endif

void sli_handle_accept_response(sli_si91x_socket_t *si91x_client_socket,
                                const sli_si91x_rsp_ltcp_est_t *accept_response);

#ifndef __ZEPHYR__
int sli_handle_select_response(const sli_si91x_socket_select_rsp_t *response,
                               fd_set *readfds,
                               fd_set *writefds,
                               fd_set *exception_fd);
#else
int sli_handle_select_response(const sli_si91x_socket_select_rsp_t *response,
                               sl_si91x_fdset_t *readfds,
                               sl_si91x_fdset_t *writefds,
                               sl_si91x_fdset_t *exception_fd);
#endif

void sli_si91x_set_accept_callback(sli_si91x_socket_t *server_socket,
                                   sl_si91x_socket_accept_callback_t callback,
                                   int32_t client_socket_id);

void sli_si91x_set_remote_socket_termination_callback(sl_si91x_socket_remote_termination_callback_t callback);

int sli_si91x_get_socket_id(sl_wifi_system_packet_t *packet);

/**
 * A internal function to find a socket with the matching ID and not in the exlcuded_state
 * @param socket_id Socket ID
 * @param excluded_state The socket state that the socket must not be
 * @param role Socket role
 */
sli_si91x_socket_t *sli_si91x_get_socket_from_id(int socket_id,
                                                 sli_si91x_bsd_socket_state_t excluded_state,
                                                 int16_t role);

sl_status_t sli_si91x_send_socket_data(sli_si91x_socket_t *si91x_socket,
                                       const sli_si91x_socket_send_request_t *request,
                                       const void *data);
int32_t sli_get_socket_command_from_host_packet(sl_wifi_buffer_t *buffer);

void sli_si91x_set_socket_event(uint32_t event_mask);

sl_status_t sli_si91x_flush_select_request_table(uint16_t error_code);

sl_status_t sli_si91x_udp_connect_if_unconnected(sli_si91x_socket_t *si91x_socket,
                                                 const struct sockaddr *to_addr,
                                                 socklen_t to_addr_len,
                                                 int socket_id);

/**
 * @addtogroup SOCKET_CONFIGURATION_FUNCTION
 * @{
 */

/**
 * @brief
 *   Sets the list of standard TLS ciphers (TLSv1.0 / TLSv1.1 / TLSv1.2) to be
 *   used when creating secure sockets on the SiWx91x.
 *
 * @details
 *   This function stores the cipher-suite bitmap in an internal global
 *   (`sl_si91x_socket_selected_ciphers`). When a secure socket is subsequently
 *   created, the SiWx91x socket layer copies this bitmap into the socket-create
 *   command sent to the firmware (see `ssl_ciphers_bitmap` in
 *   `sli_create_and_send_socket_request()`), and the firmware uses it during
 *   the TLS handshake.
 *
 * ### Applicability by socket family
 *
 * - **BSD sockets** (@ref socket) — Applies whenever TLS is enabled on the
 *   socket (e.g., via @ref setsockopt with the SiWx91x TLS option before
 *   @ref connect). This is the primary use case.
 * - **SiWx91x (proprietary) sockets** (@ref sl_si91x_socket,
 *   @ref sl_si91x_socket_async) — Applies whenever TLS is enabled on the
 *   socket.
 * - **IoT sockets** (@ref iotSocketCreate) — The IoT Socket API surface itself
 *   does **not** expose TLS enablement (`iotSocketCreate` accepts only
 *   `IOT_SOCKET_IPPROTO_TCP` / `IOT_SOCKET_IPPROTO_UDP`, and
 *   `iotSocketSetOpt` has no TLS options). The cipher configuration therefore
 *   has no effect on a pure IoT-Socket-API usage.
 *
 * @pre Pre-conditions:
 * - The Wi-Fi / network stack must be initialized.
 * - Must be called **before** creating the secure socket(s) that need to use
 *   the selected ciphers. Existing secure sockets are not updated retroactively.
 *
 * @post Post-conditions:
 * - The supplied cipher bitmap is stored and applied to every secure socket
 *   created afterwards until the cipher list is changed again.
 *
 * @param[in] cipher_list
 *   A bitmap of the selected ciphers from @ref SI91X_SOCKET_CIPHERS.
 *   Multiple ciphers can be OR-ed together.
 *
 * @note Thread safety: Not thread-safe — the cipher list is held in an
 *       unsynchronized process-global. Call once from an initialization
 *       thread before any secure socket is created.
 *
 * @note For TLSv1.3 ciphers use @ref sl_si91x_set_extended_socket_cipherlist.
 *
 * @see sl_si91x_set_extended_socket_cipherlist, sl_si91x_config_socket,
 *      socket, sl_si91x_socket, sl_si91x_socket_async
 *
 * @par Example
 * Select a set of TLSv1.2 ciphers for every subsequent secure socket
 * (BSD or SiWx91x family):
 * @code{.c}
 * sl_si91x_set_socket_cipherlist(BIT(SL_SI91X_TLS_RSA_WITH_AES_256_CBC_SHA256)
 *                                | BIT(SL_SI91X_TLS_RSA_WITH_AES_128_CBC_SHA256));
 * @endcode
 */
void sl_si91x_set_socket_cipherlist(uint32_t cipher_list);

/**
 * @brief
 *   Sets the list of extended TLS ciphers (including TLSv1.3) to be used when
 *   creating secure sockets on the SiWx91x.
 *
 * @details
 *   This function stores the extended cipher-suite bitmap in an internal
 *   global (`sl_si91x_socket_selected_extended_ciphers`). When a secure socket
 *   is subsequently created, the SiWx91x socket layer copies this bitmap into
 *   the socket-create command sent to the firmware (see
 *   `ssl_ext_ciphers_bitmap` in `sli_create_and_send_socket_request()`, under
 *   `SLI_SI917`), and the firmware uses it during the TLS handshake. Extended
 *   ciphers include the TLSv1.3 suites and other suites not covered by
 *   @ref sl_si91x_set_socket_cipherlist.
 *
 * ### Applicability by socket family
 *
 * - **BSD sockets** (@ref socket) — Applies whenever TLS is enabled on the
 *   socket. Primary use case.
 * - **SiWx91x (proprietary) sockets** (@ref sl_si91x_socket,
 *   @ref sl_si91x_socket_async) — Applies whenever TLS is enabled on the
 *   socket.
 * - **IoT sockets** (@ref iotSocketCreate) — No effect when using the IoT
 *   Socket API in isolation, because that API does not expose TLS enablement.
 *
 * @pre Pre-conditions:
 * - The Wi-Fi / network stack must be initialized.
 * - Must be called **before** creating the secure socket(s) that need to use
 *   the selected ciphers. Existing secure sockets are not updated retroactively.
 *
 * @post Post-conditions:
 * - The supplied extended cipher bitmap is stored and applied to every secure
 *   socket created afterwards until the extended cipher list is changed again.
 *
 * @param[in] extended_cipher_list
 *   A bitmap of the selected extended ciphers from @ref SI91X_EXTENDED_CIPHERS.
 *   Multiple ciphers can be OR-ed together.
 *
 * @note Thread safety: Not thread-safe — the extended cipher list is held in
 *       an unsynchronized process-global. Call once from an initialization
 *       thread before any secure socket is created.
 *
 * @see sl_si91x_set_socket_cipherlist, sl_si91x_config_socket,
 *      socket, sl_si91x_socket, sl_si91x_socket_async
 *
 * @par Example
 * Enable the TLSv1.3 AES-128-GCM-SHA256 suite for every subsequent secure
 * socket (BSD or SiWx91x family):
 * @code{.c}
 * sl_si91x_set_extended_socket_cipherlist(BIT(SL_SI91X_TLS13_AES_128_GCM_SHA256));
 * @endcode
 */
void sl_si91x_set_extended_socket_cipherlist(uint32_t extended_cipher_list);

/** @} */

sli_si91x_socket_t *get_socket_from_packet(sl_wifi_system_packet_t *socket_packet);

#ifdef __ZEPHYR__
static inline void SL_SI91X_FD_CLR(unsigned int n, sl_si91x_fdset_t *p)
{
  p->__fds_bits &= ~(1U << n);
}

static inline void SL_SI91X_FD_SET(unsigned int n, sl_si91x_fdset_t *p)
{
  p->__fds_bits |= 1U << n;
}

static inline bool SL_SI91X_FD_ISSET(unsigned int n, const sl_si91x_fdset_t *p)
{
  return p->__fds_bits & (1U << n);
}

static inline void SL_SI91X_FD_ZERO(sl_si91x_fdset_t *p)
{
  p->__fds_bits = 0;
}
#endif

sl_status_t sli_si91x_socket_pre_tx_handler(sli_command_engine_t *instance, uint16_t packet_type, void *data);

/**
 * @brief Get the destination IP address from ap_disconnect_resp structure
 * 
 * This function extracts the destination IP address from a sli_si91x_ap_disconnect_resp_t structure.
 * It supports IPv4, IPv6 link-local, and IPv6 global addresses. The function checks the flag
 * bits to determine which addresses are available and returns the first available address
 * in priority order: IPv4 > IPv6 Global > IPv6 Link-Local
 * 
 * @param ap_disconnect_resp Pointer to the ap_disconnect_resp structure
 * @param dest_ip_address Pointer to sl_ip_address_t structure to store the destination IP address
 * @return sl_status_t Status of the operation
 *         - SL_STATUS_OK if the IP address was successfully retrieved
 *         - SL_STATUS_NULL_POINTER if ap_disconnect_resp or dest_ip_address is NULL
 *         - SL_STATUS_NOT_FOUND if no valid IP address is available
 */
sl_status_t sli_si91x_get_dest_ip_address_from_ap_client_disconnect_resp(
  const sli_si91x_ap_disconnect_resp_t *ap_disconnect_resp,
  sl_ip_address_t *dest_ip_address);

#endif // _SL_SI91X_SOCKET_UTILITY_H_
