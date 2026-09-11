/***************************************************************************/ /**
 * @file  sl_si91x_socket.h
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

#pragma once
#include "sl_si91x_socket_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @addtogroup SI91X_SOCKET_FUNCTIONS
 * @{ 
 */
/**
 * @brief Creates a new socket.
 *
 * @details
 * The function creates a new socket and returns a file descriptor for the respective socket.
 * The socket is used for communication within the specified protocol family,
 * type, and protocol. The created socket is used for various network operations
 * such as, connecting to a remote host, sending and receiving data, and so on.
 *
 * @pre Pre-conditions:
 * - The Wi-Fi/Net stack must be initialized and a network profile must be up.
 * - If socket resources are configured via @ref sl_si91x_config_socket(), that call must be made before @ref sl_si91x_socket().
 *
 * @post Post-conditions:
 * - On success a new socket descriptor is allocated and can be passed to @ref sl_si91x_bind(), @ref sl_si91x_connect(),
 *   @ref sl_si91x_listen(), @ref sl_si91x_send(), @ref sl_si91x_recv(), etc.
 * - On failure no descriptor is allocated and @c errno is set.
 *
 * @param[in] family 
 *   Specifies the communication domain for the socket. This selects the protocol family to be used. 
 *   Accepts values from @ref BSD_SOCKET_FAMILIY. Currently, only @ref AF_INET and @ref AF_INET6 are supported.
 *
 * @param[in] type 
 *   Specifies the type of the socket, which determines the semantics of communication. 
 *   Accepts values from @ref BSD_SOCKET_TYPES. Currently, only @ref SOCK_STREAM and @ref SOCK_DGRAM are supported.
 *
 * @param[in] protocol 
 *   Specifies a particular protocol to be used with the socket. 
 *   Accepts values from @ref BSD_SOCKET_PROTOCOL. Currently, only @ref IPPROTO_TCP, @ref IPPROTO_UDP, and @ref IPPROTO_IP are supported.
 *
 * @return 
 *   Returns the socket ID or file descriptor for the newly created socket on success, or -1 on failure with @c errno set.
 *
 * @retval >=0                     Valid socket descriptor.
 * @retval -1/EAFNOSUPPORT        @p family is not supported.
 * @retval -1/EPROTONOSUPPORT     @p protocol is not supported for the given @p family / @p type.
 * @retval -1/EINVAL              Invalid combination of @p family, @p type, or @p protocol.
 * @retval -1/EMFILE              No free socket descriptor available.
 * @retval -1/ENOMEM              Not enough memory to allocate the socket.
 *
 * @note Thread safety:
 * - Thread-safe: multiple threads may call @ref sl_si91x_socket() concurrently to obtain distinct descriptors.
 *
 * @note Side effects:
 * - Allocates an entry in the internal SiWx91x socket pool. Updates @c errno on failure.
 *
 * @see sl_si91x_bind(), sl_si91x_connect(), sl_si91x_listen(), sl_si91x_socket_async(), sl_si91x_config_socket()
 *
 * @par Example
 * Create a synchronous SiWx91x TCP socket:
 * @code{.c}
 * int sock = sl_si91x_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
 * if (sock < 0) {
 *   printf("sl_si91x_socket() failed, errno = %d\r\n", errno);
 *   return -1;
 * }
 * @endcode
 */
int sl_si91x_socket(int family, int type, int protocol);

/**
 * @brief Creates an asynchronous socket and registers the provided callback.
 * 
 * @details
 *    This function creates a new asynchronous socket and registers a callback function
 *    that is called whenever data is received on the socket. The socket can be used
 *    for communication within the specified protocol family, type, and protocol.
 *
 * @pre Pre-conditions:
 * - The Wi-Fi/Net stack must be initialized and a network profile must be up.
 * - @p callback must be non-NULL and remain valid for the lifetime of the socket.
 *
 * @post Post-conditions:
 * - On success the socket is created and @p callback is registered to be invoked from the stack context whenever data is received.
 *
 * @param[in] family Specifies the communication domain for the socket. This selects the protocol family to be used. 
 *                   Accepts values from @ref BSD_SOCKET_FAMILIY. Currently, only @ref AF_INET and @ref AF_INET6 are supported.
 *
 * @param[in] type Specifies the type of the socket, which determines the semantics of communication. 
 *                 Accepts values from @ref BSD_SOCKET_TYPES. Currently, only @ref SOCK_STREAM and @ref SOCK_DGRAM are supported.
 *
 * @param[in] protocol Specifies a particular protocol to be used with the socket. 
 *                     Accepts values from @ref BSD_SOCKET_PROTOCOL. Currently, only @ref IPPROTO_TCP, @ref IPPROTO_UDP, and @ref IPPROTO_IP are supported.
 *
 *  @param[in] callback A function pointer of type @ref sl_si91x_socket_receive_data_callback_t. This function is called when the socket receives data.
 *                       Must be non-NULL.
 *
 * @return Returns the socket ID or file descriptor for the newly created socket on success, or -1 on failure with @c errno set.
 *
 * @retval >=0                     Valid asynchronous socket descriptor.
 * @retval -1/EINVAL              @p callback is NULL or arguments are invalid.
 * @retval -1/EAFNOSUPPORT        @p family is not supported.
 * @retval -1/EPROTONOSUPPORT     @p protocol is not supported.
 * @retval -1/EMFILE              No free socket descriptor available.
 * @retval -1/ENOMEM              Not enough memory to allocate the socket.
 *
 * @note Thread safety:
 * - Thread-safe for creation. The registered @p callback is invoked from an internal stack thread; user logic inside the callback must be thread-safe with the rest of the application.
 *
 * @note Side effects:
 * - Allocates an entry in the internal socket pool and registers the RX callback.
 *
 * @see sl_si91x_socket(), sl_si91x_accept_async(), sl_si91x_send_async(), sl_si91x_set_remote_termination_callback()
 *
 * @par Example
 * Define an asynchronous RX handler that the stack invokes when data arrives:
 * @code{.c}
 * static void on_data_rx(uint32_t sock_id,
 *                        uint8_t *buffer,
 *                        uint32_t length,
 *                        const sl_si91x_socket_metadata_t *metadata)
 * {
 *   UNUSED_PARAMETER(buffer);
 *   UNUSED_PARAMETER(metadata);
 *   printf("Socket %u received %u bytes\r\n", sock_id, length);
 * }
 * @endcode
 *
 * Create an asynchronous TCP socket and register the RX callback:
 * @code{.c}
 * int sock = sl_si91x_socket_async(AF_INET, SOCK_STREAM, IPPROTO_TCP, on_data_rx);
 * if (sock < 0) {
 *   printf("sl_si91x_socket_async() failed, errno = %d\r\n", errno);
 *   return -1;
 * }
 * @endcode
 */
int sl_si91x_socket_async(int family, int type, int protocol, sl_si91x_socket_receive_data_callback_t callback);

/**
 * @brief Sets a specified socket option on the identified socket asynchronously.
 *
 * @details
 * This function sets a specified option for a given socket asynchronously. The options
 * can be set at various levels and include parameters such as receive timeout, maximum
 * retries, maximum segment size, TCP keepalive, SSL options, and so on.
 *
 * @param[in] socket 
 *   The socket ID or file descriptor for the specified socket.
 *
 * @param[in] level 
 *   The option level. Accepts values from @ref BSD_SOCKET_OPTION_LEVEL.
 *
 * @param[in] option_name 
 *   The option to be configured. Accepts values from @ref SI91X_SOCKET_OPTION_NAME. 
 *   Currently, following options are supported:
 *   - @ref SL_SI91X_SO_RCVTIME
 *   - @ref SL_SI91X_SO_MAXRETRY
 *   - @ref SL_SI91X_SO_MSS
 *   - @ref SL_SI91X_SO_TCP_KEEPALIVE
 *   - @ref SL_SI91X_SO_HIGH_PERFORMANCE_SOCKET
 *   - @ref SL_SI91X_SO_SSL_ENABLE
 *   - @ref SL_SI91X_SO_SSL_V_1_0_ENABLE 
 *   - @ref SL_SI91X_SO_SSL_V_1_1_ENABLE 
 *   - @ref SL_SI91X_SO_SSL_V_1_2_ENABLE
 *   - @ref SL_SI91X_SO_SOCK_VAP_ID
 *   - @ref SL_SI91X_SO_SSL_V_1_3_ENABLE
 *   - @ref SL_SI91X_SO_CERT_INDEX
 *   - @ref SL_SI91X_SO_TLS_SNI
 *   - @ref SL_SI91X_SO_TLS_ALPN
 *   - @ref SL_SI91X_SO_MAX_RETRANSMISSION_TIMEOUT_VALUE
 *   - @ref SL_SI91X_SO_PER_SOCKET_CLOSE
 *
 * @param[in] option_value 
 *   The value of the parameter.
 *   | option_name                                       | option_value                              |  description                                                                                                               |
 *   |---------------------------------------------------|-------------------------------------------|----------------------------------------------------------------------------------------------------------------------------|
 *   | @ref SL_SI91X_SO_RCVTIME                          | sl_si91x_time_value                       | Socket Receive timeout. sl_si91x_time_value structure is used to represent time in two parts: seconds and microseconds.    |
 *   | @ref SL_SI91X_SO_MAXRETRY                         | uint16_t                                  | Maximum number of TCP retries                                                                                              |
 *   | @ref SL_SI91X_SO_MSS                              | uint16_t                                  | Maximum Segment Size (MSS) for the TCP connection                                                                          |
 *   | @ref SL_SI91X_SO_TCP_KEEPALIVE                    | uint16_t                                  | Set TCP keepalive in seconds                                                                                               |
 *   | @ref SL_SI91X_SO_HIGH_PERFORMANCE_SOCKET          | BIT(7)                                    | Set high performance socket                                                                                                |
 *   | @ref SL_SI91X_SO_SSL_ENABLE                       | SL_SI91X_ENABLE_TLS                       | Enable TLS/SSL                                                                                                             |
 *   | @ref SL_SI91X_SO_SSL_V_1_0_ENABLE                 | SL_SI91X_ENABLE_TLS \| SL_SI91X_TLS_V_1_0 | Enable TLS v1.0                                                                                                            |
 *   | @ref SL_SI91X_SO_SSL_V_1_1_ENABLE                 | SL_SI91X_ENABLE_TLS \| SL_SI91X_TLS_V_1_1 | Enable TLS v1.1                                                                                                            |
 *   | @ref SL_SI91X_SO_SSL_V_1_2_ENABLE                 | SL_SI91X_ENABLE_TLS \| SL_SI91X_TLS_V_1_2 | Enable TLS v1.2                                                                                                            |
 *   | @ref SL_SI91X_SO_SSL_V_1_3_ENABLE                 | SL_SI91X_ENABLE_TLS \| SL_SI91X_TLS_V_1_3 | Enable TLS v1.3                                                                                                            |
 *   | @ref SL_SI91X_SO_SOCK_VAP_ID                      | uint8_t                                   | Specifies the interface on which the socket will operate                                                                   |
 *   | @ref SL_SI91X_SO_CERT_INDEX                       | uint8_t                                   | Certificate index                                                                                                          |
 *   | @ref SL_SI91X_SO_TLS_SNI                          | sl_si91x_socket_type_length_value_t       | Server Name Indication (SNI)                                                                                               |
 *   | @ref SL_SI91X_SO_TLS_ALPN                         | sl_si91x_socket_type_length_value_t       | Application-Layer Protocol Negotiation (ALPN)                                                                              |
 *   | @ref SL_SI91X_SO_MAX_RETRANSMISSION_TIMEOUT_VALUE | uint8_t                                   | Maximum retransmission timeout value for TCP                                                                               |
 *   | @ref SL_SI91X_SO_PER_SOCKET_CLOSE                 | BIT(0)                                    | Enable per-socket graceful close handling                                                                                  |
 *
 * @param[in] option_len 
 *   The length of the parameter of type @ref socklen_t. Must match the type expected by @p option_name.
 *
 * @pre Pre-conditions:
 * - @p socket must be a valid descriptor returned by @ref sl_si91x_socket() or @ref sl_si91x_socket_async().
 * - Must be called before @ref sl_si91x_connect() / @ref sl_si91x_listen() (TCP) or before @ref sl_si91x_sendto() / @ref sl_si91x_recvfrom() / @ref sl_si91x_connect() (UDP).
 *
 * @post Post-conditions:
 * - On success the option is applied to the socket.
 *
 * @return 
 *   Returns 0 on success, or -1 on failure with @c errno set.
 *
 * @retval 0                      Success.
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/EINVAL             @p option_value is NULL, @p option_len mismatch, or the socket is in the wrong state to apply this option.
 * @retval -1/ENOPROTOOPT        @p option_name is not supported at @p level.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Changes internal SiWx91x socket state; TLS-related options change the next handshake.
 *
 * @see sl_si91x_socket(), sl_si91x_connect(), sl_si91x_listen()
 * 
 * @note
 * This function is used only for the SiWx91x socket API.
 * The options set in this function will not be effective if called after `sl_si91x_connect()` or `sl_si91x_listen()` for TCP, or after `sl_si91x_sendto()`, `sl_si91x_recvfrom()`, or `sl_si91x_connect()` for UDP.
 * The value of the option SL_SI91X_SO_MAX_RETRANSMISSION_TIMEOUT_VALUE should be a power of 2 between 1 and 128.
 *
 * @par Example
 * 1. Mark the socket as a high-performance RX socket (must be done before
 *    connect/listen):
 * @code{.c}
 * uint32_t high_performance = BIT(7);
 * if (sl_si91x_setsockopt(sock,
 *                         SOL_SOCKET,
 *                         SL_SI91X_SO_HIGH_PERFORMANCE_SOCKET,
 *                         &high_performance,
 *                         sizeof(high_performance)) < 0) {
 *   printf("setsockopt(SL_SI91X_SO_HIGH_PERFORMANCE_SOCKET) failed, errno = %d\r\n", errno);
 * }
 * @endcode
 *
 * 2. Enable TLS 1.2 on the socket:
 * @code{.c}
 * uint8_t ssl_version = SL_SI91X_ENABLE_TLS | SL_SI91X_TLS_V_1_2;
 * if(sl_si91x_setsockopt(sock, SOL_SOCKET, SL_SI91X_SO_SSL_V_1_2_ENABLE,
 *                           &ssl_version, sizeof(ssl_version)) < 0) {
 *  printf("setsockopt(SL_SI91X_SO_SSL_V_1_2_ENABLE) failed, errno = %d\r\n", errno);
 * }
 * @endcode
 *
 * 3. Select the TLS certificate index (0-3) to use:
 * @code{.c}
 * uint8_t cert_index = 0;
 * if(sl_si91x_setsockopt(sock, SOL_SOCKET, SL_SI91X_SO_CERT_INDEX,
 *                           &cert_index, sizeof(cert_index)) < 0) {
 *  printf("setsockopt(SL_SI91X_SO_CERT_INDEX) failed, errno = %d\r\n", errno);
 * }
 * @endcode
 */
int sl_si91x_setsockopt(int32_t socket, int level, int option_name, const void *option_value, socklen_t option_len);

/**
 * @brief Assigns a local protocol address to a socket.
 *
 * @details
 * The function binds a socket to a specific local address and port number.
 * It is typically used on the server side to specify the port on which the server
 * will listen for incoming connections.
 *
 * @pre Pre-conditions:
 * - @p socket must be a valid descriptor returned by @ref sl_si91x_socket() / @ref sl_si91x_socket_async().
 *
 * @post Post-conditions:
 * - On success the socket is associated with the supplied local address/port.
 *
 * @param[in] socket 
 *   The socket ID or file descriptor for the specified socket.
 *
 * @param[in] addr 
 *   Pointer to a `struct sockaddr` contains the address to which the socket is bound.
 *   This address specifies the local IP address and port number. Must be non-NULL.
 *
 * @param[in] addr_len 
 *   The length of the socket address, in bytes, of type `socklen_t`.
 *   Must be @c sizeof(struct sockaddr_in) for IPv4 or @c sizeof(struct sockaddr_in6) for IPv6.
 *
 * @return 
 *   Returns 0 on success, or -1 on failure with @c errno set.
 *
 * @retval 0                      Success.
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/EINVAL             @p addr / @p addr_len is invalid or the socket is already bound.
 * @retval -1/EADDRINUSE         The requested address/port is already in use.
 * @retval -1/EAFNOSUPPORT       @p addr->sa_family does not match the socket family.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Reserves the local address/port in the stack.
 *
 * @see sl_si91x_socket(), sl_si91x_listen(), sl_si91x_connect()
 *
 * @par Example
 * Bind the SiWx91x server socket to any local interface on port 5000:
 * @code{.c}
 * struct sockaddr_in server_addr = { 0 };
 * server_addr.sin_family         = AF_INET;
 * server_addr.sin_port           = 5000;
 * server_addr.sin_addr.s_addr    = INADDR_ANY;
 *
 * if (sl_si91x_bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
 *   printf("sl_si91x_bind() failed, errno = %d\r\n", errno);
 *   close(sock);
 *   return -1;
 * }
 * @endcode
 */
int sl_si91x_bind(int socket, const struct sockaddr *addr, socklen_t addr_len);

/**
 * @brief Enables a socket to listen for remote connection requests in passive mode.
 *
 * @details
 * The function configures a socket to listen for incoming connection requests.
 * It is typically used on the server side after the socket has been bound to a local
 * address using the `sl_si91x_bind` function. The socket enters passive mode,
 * where it waits for remote clients to connect.
 *
 * @pre Pre-conditions:
 * - @p socket must be a TCP socket bound via @ref sl_si91x_bind().
 *
 * @post Post-conditions:
 * - On success the socket transitions into the listening state.
 *
 * @param[in] socket 
 *   The socket ID or file descriptor for the specified socket.
 *
 * @param[in] max_number_of_clients 
 *   The maximum number of pending connections which the socket can queue. Must be > 0 and must not exceed the configured maximum
 *   (see @ref sl_si91x_config_socket()).
 *
 * @return 
 *   Returns 0 on success, or -1 on failure with @c errno set.
 *
 * @retval 0                      Success.
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/EOPNOTSUPP         The socket type does not support listening (e.g. UDP).
 * @retval -1/EINVAL             Socket is not bound or @p max_number_of_clients is invalid.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Allocates a pending-connection queue of size @p max_number_of_clients in the stack.
 *
 * @see sl_si91x_socket(), sl_si91x_bind(), sl_si91x_accept(), sl_si91x_accept_async(), sl_si91x_config_socket()
 *
 * @par Example
 * Place the SiWx91x server socket into passive/listen mode (1 pending client):
 * @code{.c}
 * if (sl_si91x_listen(sock, 1) < 0) {
 *   printf("sl_si91x_listen() failed, errno = %d\r\n", errno);
 *   close(sock);
 *   return -1;
 * }
 * @endcode
 */
int sl_si91x_listen(int socket, int max_number_of_clients);

/**
 * @brief Accepts a connection request from a remote peer. 
 *
 * @details 
 * The function blocks until a client attempts to connect to the server socket. After receiving a connection request, it proceeds.
 *
 * @pre Pre-conditions:
 * - @p socket must be a listening TCP socket placed in passive mode via @ref sl_si91x_listen().
 *
 * @post Post-conditions:
 * - On success a new connected socket descriptor is returned. @p addr is populated with the peer's address when non-NULL.
 * - The original listening descriptor remains open and usable for further @ref sl_si91x_accept() calls.
 *
 * @param[in] socket The socket ID or file descriptor for the specified socket.
 * @param[in] addr The address of type @ref sockaddr to which datagrams are to be sent. Its value can be NULL.
 * @param[in] addr_len The length of the socket address of type @ref socklen_t in bytes.
 * @return 
 *   Returns a non-negative socket descriptor for the accepted connection on success, or -1 on failure with @c errno set.
 *
 * @retval >=0                    Descriptor for the accepted connection.
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/EINVAL             Socket is not listening or arguments are invalid.
 * @retval -1/EMFILE             No free descriptor to allocate for the accepted connection.
 * @retval -1/ECONNABORTED       A connection has been aborted before @ref sl_si91x_accept() returned.
 *
 * @note Thread safety:
 * - Not thread-safe on the same listening descriptor; serialize concurrent @ref sl_si91x_accept() calls.
 *
 * @note Side effects:
 * - Blocks the calling thread until a client connects. Allocates a new socket slot for the accepted connection.
 *
 * @see sl_si91x_socket(), sl_si91x_bind(), sl_si91x_listen(), sl_si91x_accept_async()
 *
 * @par Example
 * Synchronously accept an incoming TCP client on a listening server socket:
 * @code{.c}
 * struct sockaddr_in client_addr = { 0 };
 *
 * int client_sock = sl_si91x_accept(sock,
 *                                   (struct sockaddr *)&client_addr,
 *                                   sizeof(client_addr));
 * if (client_sock < 0) {
 *   printf("sl_si91x_accept() failed, errno = %d\r\n", errno);
 *   close(sock);
 *   return -1;
 * }
 * @endcode
 */
int sl_si91x_accept(int socket, const struct sockaddr *addr, socklen_t addr_len);

/**
 * @brief 
 *  Accepts a connection request from the remote peer and registers a callback.
 * 
 * @details
 *  The function sets up the server socket to listen for incoming connections,
 *  and immediately returns without blocking the main program's execution. 
 *
 * @pre Pre-conditions:
 * - @p socket must be a listening TCP socket placed in passive mode via @ref sl_si91x_listen().
 * - @p callback must be non-NULL.
 *
 * @post Post-conditions:
 * - On success, the registration is complete and @p callback is invoked from the stack context whenever a new client connects.
 * - The original descriptor remains open.
 * 
 * @param[in] socket 
 *  The socket ID or file descriptor for the specified socket.
 * @param[in] callback 
 *  A function pointer of type @ref sl_si91x_socket_accept_callback_t that is called when a new client is connected to the server.
 *  Must be non-NULL.
 * @return 
 *   Returns 0 on success, or -1 on failure with @c errno set.
 *
 * @retval 0                      Registration complete; accepted clients are delivered via @p callback.
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/EINVAL             @p callback is NULL or the socket is not in listen mode.
 *
 * @note Thread safety:
 * - Thread-safe for registration. The registered @p callback is invoked from an internal stack thread; user logic inside the callback must be thread-safe with the rest of the application.
 *
 * @note Side effects:
 * - Registers the async-accept handler in the SiWx91x stack.
 *
 * @see sl_si91x_listen(), sl_si91x_accept(), sl_si91x_socket_async(), sl_si91x_set_remote_termination_callback()
 *
 * @par Example
 * Define a callback that the stack invokes when a new client connects:
 * @code{.c}
 * static void on_client_connected(int32_t sock_id,
 *                                 struct sockaddr *addr,
 *                                 uint8_t ip_version)
 * {
 *   (void)addr;
 *   (void)ip_version;
 *   printf("New client on socket %ld\r\n", (long)sock_id);
 * }
 * @endcode
 *
 * Non-blocking accept: returns immediately and the callback is invoked later:
 * @code{.c}
 * if (sl_si91x_accept_async(sock, on_client_connected) < 0) {
 *   printf("sl_si91x_accept_async() failed, errno = %d\r\n", errno);
 *   close(sock);
 *   return -1;
 * }
 * @endcode
 */
int sl_si91x_accept_async(int socket, sl_si91x_socket_accept_callback_t callback);

/**
 * @brief 
 * Initiates a connection to a remote socket specified by the addr parameter.
 * 
 * @details
 * The function initiates a connection to a remote socket specified by the `addr` parameter.
 * It is typically used on the client side to establish a connection to a server.
 *
 * @pre Pre-conditions:
 * - @p socket must be a descriptor returned by @ref sl_si91x_socket() / @ref sl_si91x_socket_async().
 * - For TLS: @ref sl_si91x_setsockopt() must have been called for the relevant SSL options before @ref sl_si91x_connect().
 *
 * @post Post-conditions:
 * - On success a TCP socket enters the connected state. For UDP, the peer is cached for subsequent send/recv.
 * 
 * @param[in] socket 
 *  The socket ID or file descriptor for the specified socket.
 * @param[in] addr 
 *  Address of type @ref sockaddr to which datagrams are to be sent. Must be non-NULL.
 * @param[in] addr_len
 *  Length of the socket address of type @ref socklen_t in bytes.
 *  Must be @c sizeof(struct sockaddr_in) for IPv4 or @c sizeof(struct sockaddr_in6) for IPv6.
 * @return 
 *   Returns 0 on success, or -1 on failure with @c errno set.
 *
 * @retval 0                      Success.
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/EINVAL             Invalid @p addr or @p addr_len.
 * @retval -1/EAFNOSUPPORT       Address family does not match the socket family.
 * @retval -1/ECONNREFUSED       Remote peer refused the connection.
 * @retval -1/ETIMEDOUT          Connection attempt timed out.
 * @retval -1/ENETUNREACH        No route to the remote peer.
 * @retval -1/EISCONN            Socket is already connected.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Allocates internal TCP/TLS state. For TLS, the handshake runs synchronously.
 *
 * @see sl_si91x_socket(), sl_si91x_bind(), sl_si91x_setsockopt(), sl_si91x_send(), sl_si91x_recv()
 *
 * @par Example
 * Connect a SiWx91x client socket to a remote TCP server:
 * @code{.c}
 * struct sockaddr_in server_addr = { 0 };
 * server_addr.sin_family         = AF_INET;
 * server_addr.sin_port           = 5001;
 * sl_net_inet_addr("192.168.0.100", &server_addr.sin_addr.s_addr);
 *
 * if (sl_si91x_connect(sock,
 *                      (const struct sockaddr *)&server_addr,
 *                      sizeof(server_addr)) < 0) {
 *   printf("sl_si91x_connect() failed, errno = %d\r\n", errno);
 *   close(sock);
 *   return -1;
 * }
 * @endcode
 */
int sl_si91x_connect(int socket, const struct sockaddr *addr, socklen_t addr_len);

/**
 * @brief 
 * Sends the data to the remote peer on the given socket. 
 * 
 * @details
 * This should be used only when the socket is in a connected state.
 *
 * @pre Pre-conditions:
 * - @p socket must be connected (TCP) or have a cached peer (UDP via @ref sl_si91x_connect()).
 *
 * @post Post-conditions:
 * - On success the bytes have been handed to the stack for transmission.
 * 
 * @param[in] socket 
 * The socket ID or file descriptor for the specified socket.
 * @param[in] buffer 
 * Pointer to the buffer containing data to send to the remote peer. Must be non-NULL.
 * @param[in] buffer_length 
 *  Length of the buffer pointed to by the buffer parameter. Must be > 0 and must not exceed the per-protocol limits below.
 * @param[in] flags 
 *  Controls the transmission of the data. Must be 0 (not currently supported).
 * @return 
 *   Returns the number of bytes sent on success, or -1 on failure with @c errno set.
 *
 * @retval >=0                    Number of bytes accepted by the stack for transmission.
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/ENOTCONN           Socket is not connected.
 * @retval -1/EINVAL             @p buffer is NULL or @p buffer_length out of range.
 * @retval -1/EMSGSIZE           Buffer exceeds the MSS limit for the protocol.
 * @retval -1/EPIPE              Peer has closed the connection.
 * @retval -1/ECONNRESET         Peer resets the connection.
 * @retval -1/ENOBUFS            Not enough buffer space in the stack; try again later.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Enqueues the payload in the firmware send queue.
 *
 * @see sl_si91x_send_async(), sl_si91x_sendto(), sl_si91x_send_large_data(), sl_si91x_recv()
 *
 * @note The flags parameter is not currently supported.
 * @note For TCP, the maximum buffer length should not exceed the MSS.
 * @note The following table lists the maximum buffer length which could be sent over each supported protocol.
 *  
 *  Protocol | Maximum data chunk (bytes)
 *  ---------|----------------------
 *  UDP      | 1472 bytes
 *  TCP      | 1460 bytes
 *  TLS      | 1370 bytes
 *
 * @par Example
 * Send a buffer on a connected SiWx91x TCP socket (respecting the 1460 B MSS):
 * @code{.c}
 * const uint8_t payload[1460] = { 0 };
 * int           sent          = sl_si91x_send(sock, payload, sizeof(payload), 0);
 * if (sent < 0) {
 *   printf("sl_si91x_send() failed, errno = %d\r\n", errno);
 * }
 * @endcode
 */
int sl_si91x_send(int socket, const uint8_t *buffer, size_t buffer_length, int32_t flags);

/**
 * @brief 
 * Transmits one or more messages to a socket asynchronously. 
 * 
 * @details
 * This should be used only when the socket is in a connected state.
 *
 * @pre Pre-conditions:
 * - @p socket must be connected (TCP) or have a cached peer (UDP).
 * - @p callback, if non-NULL, must remain valid until the TX-complete notification is delivered.
 *
 * @post Post-conditions:
 * - On success, the request is queued. @p callback is invoked from the stack context once the transmission completes.
 * 
 * @param[in] socket 
 * The socket ID or file descriptor for the specified socket.
 * @param[in] buffer 
 * Pointer to the buffer containing data to send to the remote peer. Must remain valid until @p callback fires. Must be non-NULL.
 * @param[in] buffer_length 
 *  Length of the buffer pointed to by the buffer parameter. Must be > 0 and must not exceed the per-protocol limits below.
 * @param[in] flags 
 *  Controls the transmission of the data. Must be 0 (not currently supported).
 * @param[in] callback 
 *  A function pointer of type @ref sl_si91x_socket_data_transfer_complete_handler_t that is called after complete data transfer.
 *  May be NULL if no notification is required.
 * @return 
 *   Returns 0 on successful submission, or -1 on failure with @c errno set.
 *
 * @retval 0                      Request accepted; @p callback will fire when the TX completes.
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/ENOTCONN           Socket is not connected.
 * @retval -1/EINVAL             @p buffer is NULL or @p buffer_length out of range.
 * @retval -1/EMSGSIZE           Buffer exceeds the MSS limit for the protocol.
 * @retval -1/ENOBUFS            Not enough buffer space in the stack; try again later.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 * - The registered @p callback is invoked from an internal stack thread.
 *
 * @note Side effects:
 * - Enqueues the payload in the firmware send queue.
 *
 * @see sl_si91x_send(), sl_si91x_sendto_async(), sl_si91x_socket_async()
 *
 * @note The flags parameter is not currently supported.
 * @note For TCP, the maximum buffer length should not exceed the MSS.
 * @note The following table lists the maximum buffer length which could be sent over each supported protocol.
 *  
 *  Protocol | Maximum data chunk (bytes)
 *  ---------|----------------------
 *  UDP      | 1472 bytes
 *  TCP      | 1460 bytes
 *  TLS      | 1370 bytes
 */
/**
 * @par Example
 * Define a callback that the stack invokes once the TX completes, then issue
 * a non-blocking send:
 * @code{.c}
 * static void on_tx_complete(int32_t sock_id, uint16_t bytes_sent)
 * {
 *   printf("socket %ld: %u bytes transmitted\r\n", (long)sock_id, bytes_sent);
 * }
 *
 * const uint8_t payload[] = "hello";
 * if (sl_si91x_send_async(sock, payload, sizeof(payload) - 1, 0, on_tx_complete) < 0) {
 *   printf("sl_si91x_send_async() failed, errno = %d\r\n", errno);
 * }
 * @endcode
 */
int sl_si91x_send_async(int socket,
                        const uint8_t *buffer,
                        size_t buffer_length,
                        int32_t flags,
                        sl_si91x_socket_data_transfer_complete_handler_t callback);

/**
 * @brief 
 * Transmits one or more messages to another socket. 
 * 
 * @details
 * The function is called from an unconnected socket, typically like a UDP socket.
 *
 * @pre Pre-conditions:
 * - @p socket must be a descriptor returned by @ref sl_si91x_socket() (typically UDP).
 *
 * @post Post-conditions:
 * - On success the datagram has been handed to the stack for transmission.
 * 
 * @param[in] socket 
 * The socket ID or file descriptor for the specified socket.
 * @param[in] buffer 
 *  Pointer to data buffer contains data to send to remote peer. Must be non-NULL.
 * @param[in] buffer_length 
 *  Length of the buffer pointed to by the buffer parameter. Must be > 0 and must not exceed the per-protocol limits below.
 * @param[in] flags 
 *  Controls the transmission of the data. Must be 0 (not currently supported).
 * @param[in] addr 
 *  Address of type @ref sockaddr to which datagrams are to be sent. Must be non-NULL.
 * @param[in] addr_len
 *  Length of the socket address of type @ref socklen_t in bytes.
 *  Must be @c sizeof(struct sockaddr_in) for IPv4 or @c sizeof(struct sockaddr_in6) for IPv6.
 * @return 
 *   Returns the number of bytes sent on success, or -1 on failure with @c errno set.
 *
 * @retval >=0                    Number of bytes accepted by the stack for transmission.
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/EINVAL             Invalid @p buffer, @p buffer_length, or @p addr.
 * @retval -1/EAFNOSUPPORT       @p addr family does not match the socket family.
 * @retval -1/EMSGSIZE           Buffer exceeds the MSS limit for the protocol.
 * @retval -1/ENOBUFS            Not enough buffer space in the stack; try again later.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Enqueues the datagram in the firmware send queue.
 *
 * @see sl_si91x_send(), sl_si91x_sendto_async(), sl_si91x_recvfrom()
 *
 * @note The flags parameter is not currently supported.
 * @note For TCP, the maximum buffer length should not exceed the MSS.
 * @note The following table lists the maximum buffer length which could be sent over each supported protocol.
 *  
 *  Protocol | Maximum data chunk (bytes)
 *  ---------|----------------------
 *  UDP      | 1472 bytes
 *  TCP      | 1460 bytes
 *  TLS      | 1370 bytes
 */
/**
 * @par Example
 * Send a UDP datagram to a specific remote address using the SiWx91x API:
 * @code{.c}
 * struct sockaddr_in dest_addr  = { 0 };
 * dest_addr.sin_family          = AF_INET;
 * dest_addr.sin_port            = 5002;
 * sl_net_inet_addr("192.168.0.100", &dest_addr.sin_addr.s_addr);
 *
 * const uint8_t payload[] = "ping";
 * int sent = sl_si91x_sendto(udp_sock,
 *                            payload,
 *                            sizeof(payload) - 1,
 *                            0,
 *                            (const struct sockaddr *)&dest_addr,
 *                            sizeof(dest_addr));
 * if (sent < 0) {
 *   printf("sl_si91x_sendto() failed, errno = %d\r\n", errno);
 * }
 * @endcode
 */
int sl_si91x_sendto(int socket,
                    const uint8_t *buffer,
                    size_t buffer_length,
                    int32_t flags,
                    const struct sockaddr *addr,
                    socklen_t addr_len);

/**
 * @brief 
 * Transmits one or more messages to another socket asynchronously, and receives acknowledgement through the registered callback.
 * 
 * @details
 * The function can also be called from an unconnected socket, typically like a UDP socket.
 *
 * @pre Pre-conditions:
 * - @p socket must be a descriptor returned by @ref sl_si91x_socket_async() (typically UDP).
 * - @p callback, if non-NULL, must remain valid until the TX-complete notification is delivered.
 *
 * @post Post-conditions:
 * - On success, the request is queued. @p callback is invoked from the stack context once the transmission completes.
 * 
 * @param[in] socket 
 * The socket ID or file descriptor for the specified socket.
 * @param[in] buffer 
 *  Pointer to data buffer contains data to send to remote peer. Must remain valid until @p callback fires. Must be non-NULL.
 * @param[in] buffer_length 
 *  Length of the buffer pointed to by the buffer parameter. Must be > 0 and must not exceed the per-protocol limits below.
 * @param[in] flags 
 *  Controls the transmission of the data. Must be 0 (not currently supported).
 * @param[in] to_addr 
 *  Address of type @ref sockaddr to which datagrams are to be sent. Must be non-NULL.
 * @param[in] to_addr_len
 *  Length of the socket address of type @ref socklen_t in bytes.
 *  Must be @c sizeof(struct sockaddr_in) for IPv4 or @c sizeof(struct sockaddr_in6) for IPv6.
 * @param[in] callback 
 *  A function pointer of type @ref sl_si91x_socket_data_transfer_complete_handler_t that is called after complete data transfer.
 *  May be NULL.
 * @return 
 *   Returns 0 on successful submission, or -1 on failure with @c errno set.
 *
 * @retval 0                      Request accepted; @p callback will fire when the TX completes.
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/EINVAL             Invalid @p buffer, @p buffer_length, or @p to_addr.
 * @retval -1/EAFNOSUPPORT       @p to_addr family does not match the socket family.
 * @retval -1/EMSGSIZE           Buffer exceeds the MSS limit for the protocol.
 * @retval -1/ENOBUFS            Not enough buffer space in the stack; try again later.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 * - The registered @p callback is invoked from an internal stack thread.
 *
 * @note Side effects:
 * - Enqueues the datagram in the firmware send queue.
 *
 * @see sl_si91x_sendto(), sl_si91x_send_async(), sl_si91x_socket_async()
 *
 * @note The flags parameter is not currently supported.
 * @note For TCP, the maximum buffer length should not exceed the MSS.
 * @note The following table lists the maximum buffer length which could be sent over each supported protocol.
 *  
 *  Protocol | Maximum data chunk (bytes)
 *  ---------|----------------------
 *  UDP      | 1472 bytes
 *  TCP      | 1460 bytes
 *  TLS      | 1370 bytes
 */
int sl_si91x_sendto_async(int socket,
                          const uint8_t *buffer,
                          size_t buffer_length,
                          int32_t flags,
                          const struct sockaddr *to_addr,
                          socklen_t to_addr_len,
                          sl_si91x_socket_data_transfer_complete_handler_t callback);

/**
 * @brief Sends data that is larger than the Maximum Segment Size (MSS).
 *
 * @details
 * This function sends data that exceeds the MSS size to a remote peer. It handles
 * the segmentation of the data into smaller chunks that fit within the MSS limit.
 * This API can even be used when the buffer length is less than the MSS.
 *
 * @pre Pre-conditions:
 * - @p socket must be a connected TCP socket.
 *
 * @post Post-conditions:
 * - On success, the requested number of bytes are segmented and queued by the stack for transmission.
 *
 * @param[in] socket 
 *   The socket ID or file descriptor for the specified socket.
 *
 * @param[in] buffer 
 *   Pointer to the data buffer contains the data to be sent to the remote peer. Must be non-NULL.
 *
 * @param[in] buffer_length 
 *   The length of the buffer pointed to by the buffer parameter. Must be > 0.
 *
 * @param[in] flags 
 *   Controls the transmission of the data. Note that the flags parameter is not currently supported.
 *
 * @return 
 *   Returns the number of bytes sent on success, or -1 on failure with @c errno set.
 *
 * @retval >=0                    Total number of bytes sent (may be less than @p buffer_length if the stack returns early).
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/ENOTCONN           Socket is not connected.
 * @retval -1/EINVAL             @p buffer is NULL or @p buffer_length is 0.
 * @retval -1/EPIPE              Peer has closed the connection.
 * @retval -1/ECONNRESET         Peer resets the connection.
 * @retval -1/ENOBUFS            Not enough buffer space in the stack.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Internally segments @p buffer into MSS-sized chunks and enqueues them for transmission.
 *
 * @see sl_si91x_send(), sl_si91x_send_async()
 *
 * @par Example
 * Send a payload larger than the MSS; the API handles internal segmentation:
 * @code{.c}
 * uint8_t big_buffer[8192];
 * memset(big_buffer, 'A', sizeof(big_buffer));
 *
 * int sent = sl_si91x_send_large_data(sock, big_buffer, sizeof(big_buffer), 0);
 * if (sent < 0) {
 *   printf("sl_si91x_send_large_data() failed, errno = %d\r\n", errno);
 * } else {
 *   printf("Transmitted %d bytes of %u\r\n", sent, (unsigned)sizeof(big_buffer));
 * }
 * @endcode
 */
int sl_si91x_send_large_data(int socket, const uint8_t *buffer, size_t buffer_length, int32_t flags);

/**
 * @brief Receives data from a connected socket.
 *
 * @details
 * This function receives data from a connected socket and stores it in the specified buffer.
 * It is typically used on the client or server side to read incoming data from a remote peer.
 *
 * If the incoming TCP payload exceeds the provided buffer, only the first bufferLength bytes are returned.
 * Any remaining bytes will be delivered by subsequent calls to `sl_si91x_recv()`.
 * The max buffer length for each supported protocol is as follows:
 *   - IPv4: 1460 bytes
 *   - IPv6: 1440 bytes
 *
 * @pre Pre-conditions:
 * - @p socket must be a connected descriptor (TCP) or an accepted socket.
 *
 * @post Post-conditions:
 * - On success @p buffer contains the received bytes and the return value is the number of bytes read.
 * - A return value of 0 indicates orderly shutdown by the peer (TCP).
 *
 * @param[in] socket
 *   The socket ID or file descriptor for the specified socket.
 *
 * @param[out] buffer 
 *   Pointer to the buffer holds the data received from the remote peer. Must be non-NULL.
 *
 * @param[in] bufferLength 
 *   The length of the buffer pointed to by the buffer parameter. Must be > 0.
 *
 * @param[in] flags 
 *   Controls the reception of the data. Note that the flags parameter are not currently supported.
 *
 * @return 
 *   Returns the number of bytes received on success, or -1 on failure with @c errno set.
 *
 * @retval >0                     Number of bytes read into @p buffer.
 * @retval 0                      Orderly shutdown by the peer (TCP).
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/ENOTCONN           Socket is not connected.
 * @retval -1/EINVAL             @p buffer is NULL or @p bufferLength is 0.
 * @retval -1/EWOULDBLOCK        Non-blocking socket has no data available.
 * @retval -1/ETIMEDOUT          @ref SL_SI91X_SO_RCVTIME elapsed before data was received.
 * @retval -1/ECONNRESET         Peer resets the connection.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Blocks the calling thread unless @ref SL_SI91X_SO_RCVTIME is set.
 *
 * @see sl_si91x_recvfrom(), sl_si91x_send(), sl_si91x_setsockopt()
 *
 * @par Example
 * Read up to 1460 bytes on a connected SiWx91x TCP socket and process
 * the received data when the call succeeds:
 * @code{.c}
 * uint8_t rx_buffer[1460];
 * int     received_bytes = sl_si91x_recv(sock, rx_buffer, sizeof(rx_buffer), 0);
 * if (received_bytes < 0) {
 *   printf("sl_si91x_recv() failed, errno = %d\r\n", errno);
 * } else if (received_bytes == 0) {
 *   printf("Peer closed the connection\r\n");
 * } else {
 *   ...
 * }
 * @endcode
 */
int sl_si91x_recv(int socket, uint8_t *buffer, size_t bufferLength, int32_t flags);

/**
 * @brief Receives data from an unconnected socket, typically a UDP socket.
 *
 * @details
 * This function receives data from an unconnected socket and stores it in the specified buffer.
 * It is typically used to receive data from a remote peer without establishing a connection.
 *
 * If the payload of an incoming UDP datagram exceeds the size of the user-provided buffer, the sl_si91x_recvfrom() function returns only the first buffersize bytes.
 * The remaining portion of the same datagram is delivered in subsequent calls, effectively splitting the datagram into chunks.
 * To ensure the entire datagram is received in a single call, the buffer should be sized as follows:
 *   - IPv4: 1472 bytes
 *   - IPv6: 1452 bytes
 *
 * @pre Pre-conditions:
 * - @p socket must be bound via @ref sl_si91x_bind() (for UDP) or connected (for TCP).
 *
 * @post Post-conditions:
 * - On success @p buffer contains the received bytes and, when @p fromAddr is non-NULL, the source address is populated.
 *
 * @param[in] socket 
 *   The socket ID or file descriptor for the specified socket.
 *
 * @param[out] buffer 
 *   Pointer to the buffer that will hold the data received from the remote peer. Must be non-NULL.
 *
 * @param[in] buffersize 
 *   The size of the buffer pointed to by the buffer parameter. Must be > 0.
 *
 * @param[in] flags 
 *   Controls the reception of the data. Note that the flags parameter is not currently supported.
 *
 * @param[out] fromAddr 
 *   Pointer to a @ref sockaddr that will hold the address of the remote peer from which the current packet was received.
 *   May be NULL if the source address is not required.
 *
 * @param[in, out] fromAddrLen 
 *   Pointer to a @ref socklen_t that contains the length of the remote peer address (fromAddr). 
 *   On return, it will contain the actual length of the address. Must be non-NULL when @p fromAddr is non-NULL.
 *
 * @return 
 *   Returns the number of bytes received on success, or -1 on failure with @c errno set.
 *
 * @retval >=0                    Number of bytes received.
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/EINVAL             @p buffer is NULL, @p buffersize is 0, or @p fromAddrLen invalid.
 * @retval -1/EWOULDBLOCK        Non-blocking socket has no data available.
 * @retval -1/ETIMEDOUT          @ref SL_SI91X_SO_RCVTIME elapsed before data was received.
 * @retval -1/ECONNRESET         Peer resets the connection.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Blocks the calling thread unless @ref SL_SI91X_SO_RCVTIME is set.
 * - Truncated UDP datagrams are delivered in pieces across successive calls (no silent loss).
 *
 * @see sl_si91x_recv(), sl_si91x_sendto(), sl_si91x_setsockopt()
 */
/**
 * @par Example
 * Receive a UDP datagram on a SiWx91x socket and log the sender:
 * @code{.c}
 * uint8_t            buffer[1472];
 * struct sockaddr_in src_addr = { 0 };
 * socklen_t          src_len  = sizeof(src_addr);
 *
 * int received_bytes = sl_si91x_recvfrom(udp_sock,
 *                           buffer,
 *                           sizeof(buffer),
 *                           0,
 *                           (struct sockaddr *)&src_addr,
 *                           &src_len);
 * if (received_bytes > 0) {
 *   printf("Got %d bytes from port %u\r\n", n, ntohs(src_addr.sin_port));
 * } else if (received_bytes < 0) {
 *   printf("sl_si91x_recvfrom() failed, errno = %d\r\n", errno);
 * }
 * @endcode
 */
int sl_si91x_recvfrom(int socket,
                      uint8_t *buffer,
                      size_t buffersize,
                      int32_t flags,
                      struct sockaddr *fromAddr,
                      socklen_t *fromAddrLen);

/**
 * @brief Disables send or receive operations on a socket.
 *
 * @details
 * This function disables further send or receive operations on a specified socket.
 * It can either close a specific socket or all sockets associated with a given port number.
 *
 * @pre Pre-conditions:
 * - @p socket must be a valid descriptor.
 *
 * @post Post-conditions:
 * - On success the indicated socket(s) are closed and their descriptors are released.
 * - Any pending I/O on the descriptor(s) is aborted; queued unsent data may be discarded.
 *
 * @param[in] socket
 *   The socket ID or file descriptor for the specified socket that is to be closed.
 *
 * @param[in] how
 *   Determines the scope of the shutdown operation: (@ref SI91X_SOCKET_SHUTDOWN_OPTION)
 *   TCP/TLS:
*      - @c 0: Close the specified client socket.
*      - @c 1: Close the server socket and the associated client sockets on the specified source port number.
 *   UDP:
*      - @c 0: Close the specified UDP socket.
*      - @c 1: Not supported for UDP sockets; returns @c -1 with @c errno set to @c EINVAL.
 * @return 
 *   Returns 0 on success, or -1 on failure with @c errno set.
 *
 * @retval 0                      Success.
 * @retval -1/EBADF              @p socket is not a valid descriptor.
 * @retval -1/ENOTSOCK           @p socket is not a socket descriptor.
 * @retval -1/EINVAL             @p how is not a valid value from @ref SI91X_SOCKET_SHUTDOWN_OPTION.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor. The caller must ensure no other thread is using the descriptor.
 *
 * @note Side effects:
 * - Releases stack resources (TCP state, TLS context, internal buffers) for the targeted socket(s).
 *
 * @see sl_si91x_socket(), sl_si91x_socket_async()
 *
 * @note
 *   If the socket is a server socket, the `how` parameter is ignored, and the socket is always closed based on the port number.
 *
 * @par Example
 * Close only the specified socket (how = 0):
 * @code{.c}
 * if (sl_si91x_shutdown(sock, 0) < 0) {
 *   printf("sl_si91x_shutdown() failed, errno = %d\r\n", errno);
 * }
 * @endcode
 *
 * Close every socket that shares the same source port (how = 1):
 * @code{.c}
 * (void)sl_si91x_shutdown(sock, 1);
 * @endcode
 */
int sl_si91x_shutdown(int socket, int how);

/**
 * @brief 
 * The sl_si91x_select() function is used to monitor multiple file descriptors for readiness to
 * perform I/O operations.  The file descriptors in the sets are monitored to
 * see if they are ready for reading, ready for writing, or have an error
 * condition pending.  
 * @details 
 * sl_si91x_select() allows a program to monitor multiple file descriptors, 
 * waiting until one or more of the file descriptors become "ready" 
 * for some class of I/O operation (e.g., input possible).  A file 
 * descriptor is considered ready if it is possible to perform a 
 * corresponding I/O operation without blocking. 
 *
 * @pre Pre-conditions:
 * - All descriptors placed in the fd sets must be valid sockets created via @ref sl_si91x_socket() / @ref sl_si91x_socket_async().
 * - The firmware select capacity must be configured via
 *   [SL_SI91X_EXT_TCP_IP_TOTAL_SELECTS](../wiseconnect-api-reference-guide-si91x-driver/si91-x-extended-tcp-ip-feature-bitmap#sl-si91-x-ext-tcp-ip-total-selects).
 *
 * @post Post-conditions:
 * - In synchronous mode (@p callback is NULL): the input fd sets are updated in place to indicate ready descriptors.
 * - In asynchronous mode (@p callback is non-NULL): @p callback is invoked from the stack context when one or more descriptors become ready.
 * 
 * @param[in] nfds 
 *  The first nfds descriptors are checked in each set; that is, the descriptors from 0 through nfds-1.
 *  Must be in the range @c (0, @c FD_SETSIZE].
 * @param[in,out] readfds 
 *  A pointer to a fd_set object that specifies the descriptors to check for files that are ready for reading. May be NULL.
 * @param[in,out] writefds 
 *  A pointer to a fd_set object that specifies the descriptors to check for files that are ready for writing. May be NULL.
 * @param[in,out] exceptfds 
 *  A pointer to a fd_set object that will be watched for exceptions. May be NULL; not currently supported.
 * @param[in] timeout 
 *  If timeout is provided, the device shall wait for timeout duration for the file descriptors to become ready.
 *  If timeout is NULL, the device shall wait indefinitely for the file descriptors to become ready.
 * @param[in] callback 
 *  A function pointer of type @ref sl_si91x_socket_select_callback_t that will be called when an asynchronous response  is received for a select request.
 *  May be NULL for synchronous behavior.
 * @return 
 *  If callback is provided, the function will immediately return zero for success, and -1 for failure.
 *  If callback is NULL, returns:
 *  - total number of file descriptors set on success.
 *  - 0 when no file descriptors are ready within the specified timeout.
 *  - -1 on failure.
 *
 * @retval >=0                    Number of ready descriptors (synchronous mode) or 0 on successful async submission.
 * @retval 0                      Timeout expired before any descriptor became ready (synchronous mode).
 * @retval -1/EBADF              One of the supplied descriptors is invalid.
 * @retval -1/EINVAL             @p nfds is out of range, @p timeout has invalid values, or too many sockets passed (see limits below).
 * @retval -1/EPERM              Select capacity is not configured in firmware.
 *
 * @note Thread safety:
 * - Not thread-safe on the same fd sets. A single thread should own the select loop for a given set of sockets.
 * - The @p callback is invoked from an internal stack thread.
 *
 * @note Side effects:
 * - In synchronous mode, modifies the supplied fd sets in place.
 * - Consumes one firmware select-slot for the duration of the call.
 *
 * @see sl_si91x_socket(), sl_si91x_recv(), sl_si91x_send()
 * 
 * @note 
 * The select function modifies the sets passed to it, so if the function
 * is to be called again, the sets must be reinitialized.
 * The exceptfds parameter is not currently supported.
 * @note 
 * If the number of select requests is not configured, the sl_si91x_select() API will fail and return -1, with the errno being set to EPERM (Operation not permitted).
 * @note 
 * The number of select operations the device can handle can be configured using the [SL_SI91X_EXT_TCP_IP_TOTAL_SELECTS](../wiseconnect-api-reference-guide-si91x-driver/si91-x-extended-tcp-ip-feature-bitmap#sl-si91-x-ext-tcp-ip-total-selects).
 * @note
 * The sl_si91x_select() function has the following limitations:
 *   - Supports a maximum of 10 sockets per call.
 *   - Supports read and write fd_sets only. It does not monitor the exceptfds set.
 *   - Operates only with connected sockets. Listening sockets are not supported.
 *   - The select API supports configuring multiple file descriptors in a single call. However, it returns only one read descriptor when an event is pending, and returns none if no events are pending.
 *
 * @par Example
 * Synchronous select: wait up to 2 seconds for data on two sockets, then
 * service whichever sockets are flagged as ready:
 * @code{.c}
 * fd_set          read_fds;
 * struct timeval  tv = { .tv_sec = 2, .tv_usec = 0 };
 *
 * FD_ZERO(&read_fds);
 * FD_SET(sock_a, &read_fds);
 * FD_SET(sock_b, &read_fds);
 *
 * int max_fd = (sock_a > sock_b ? sock_a : sock_b) + 1;
 * int ready  = sl_si91x_select(max_fd, &read_fds, NULL, NULL, &tv, NULL);
 * if (ready < 0) {
 *   printf("sl_si91x_select() failed, errno = %d\r\n", errno);
 * } else if (ready == 0) {
 *   printf("sl_si91x_select() timed out\r\n");
 * } else {
 *   if (FD_ISSET(sock_a, &read_fds)) {
 *     ...
 *   }
 * }
 * @endcode
 */
#ifndef __ZEPHYR__
int sl_si91x_select(int nfds,
                    fd_set *readfds,
                    fd_set *writefds,
                    fd_set *exceptfds,
                    const struct timeval *timeout,
                    sl_si91x_socket_select_callback_t callback);
#else
int sl_si91x_select(int nfds,
                    sl_si91x_fdset_t *readfds,
                    sl_si91x_fdset_t *writefds,
                    sl_si91x_fdset_t *exceptfds,
                    const struct timeval *timeout,
                    sl_si91x_socket_select_callback_t callback);
#endif

/**
 * @brief Registers a callback for remote socket termination events.
 *
 * @details
 * This function registers a callback function is called when a remote socket is terminated.
 * The callback function should be of type @ref sl_si91x_socket_remote_termination_callback_t.
 *
 * @pre Pre-conditions:
 * - The SiWx91x Wi-Fi/Net stack must be initialized.
 *
 * @post Post-conditions:
 * - After registration the callback is invoked from the stack context whenever a peer terminates a SiWx91x socket.
 * - Only one handler is stored; subsequent calls overwrite the previous registration.
 *
 * @param[in] callback
 *   A valid function pointer of type @ref sl_si91x_socket_remote_termination_callback_t that is called when the remote socket is terminated.
 *   May be NULL to unregister the current handler.
 *
 * @note Thread safety:
 * - The stored handler is overwritten atomically. The callback itself runs on an internal stack thread, so user logic must be reentrant/thread-safe relative to the rest of the application.
 *
 * @note Side effects:
 * - Replaces any previously-registered remote-termination callback.
 *
 * @see sl_si91x_socket_async(), sl_si91x_accept_async()
 *
 * @par Example
 * Define a handler that the stack invokes when the remote peer closes its
 * socket, and register it:
 * @code{.c}
 * static void on_remote_termination(int socket_id, uint16_t port_number, uint32_t bytes_sent)
 * {
 *   printf("Remote closed socket %d (port %u), %lu bytes sent\r\n",
 *          socket_id, port_number, (unsigned long)bytes_sent);
 * }
 *
 * sl_si91x_set_remote_termination_callback(on_remote_termination);
 * @endcode
 */
void sl_si91x_set_remote_termination_callback(sl_si91x_socket_remote_termination_callback_t callback);
/** @} */
#ifdef __cplusplus
}
#endif
