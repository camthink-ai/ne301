/********************************************************************************
 * @file  sl_si91x_socket_types.h
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

#include <stdint.h>
#include "sl_constants.h"
#include "cmsis_os2.h" // CMSIS RTOS2
#ifdef SLI_SI91X_NETWORK_DUAL_STACK
#include "lwipopts.h"
#if LWIP_SOCKET
#include "lwip/sockets.h"
#else
#include "sli_si91x_socket_defs.h"
#endif
#else
#ifndef __ZEPHYR__
#include "socket.h"
#include "select.h"
#else
#include <sys/socket.h>
#endif
#endif
#include "sli_constants.h"
#include "sli_command_engine.h"
#include "sl_types.h"

// Websocket max url length
#define SLI_WEBS_MAX_URL_LENGTH 51

// Websocket max host length
#define SLI_WEBS_MAX_HOST_LENGTH 51
// Websocket max subprotocol length
#define SLI_WEBS_MAX_SUBPROTOCOL_LENGTH 51

// Websocket max origin length
#define SLI_WEBS_MAX_ORIGIN_LENGTH 51

// Websocket max domain name length
#define SLI_WEBS_MAX_DOMAIN_NAME_LENGTH 51

#define SLI_SI91X_MAX_DOMAIN_NAME_LENGTH 256

/* NUMBER_OF_BSD_SOCKETS must be < 32 (sizeof(unsigned) * 8) */
#ifdef __ZEPHYR__
typedef struct sl_si91x_fdset_s {
  unsigned int __fds_bits;
} sl_si91x_fdset_t;
#endif

/* Non-blocking type flag for socket(); BSD socket.h defines this, but dual-stack
 * (LwIP / sli_si91x_socket_defs.h) paths may not. */
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 0x4000
#endif

/**
 * @addtogroup SI91X_SOCKET_FUNCTIONS
 * @{ 
 */
/**
 * @brief Structure for socket metadata associated with read event.
 *
 * @details
 * The structure holds the metadata information for the socket's read event.
 * It includes the following details such as: IP version, socket ID, length of data received,
 * offset within the buffer, destination port, and the IP address of the sender device. 
 * The IP address can be either IPv4 or IPv6, determined by the
 * `ip_version` field.
 */
typedef struct {
  uint16_t
    ip_version; ///< Two bytes for the IP version of the IP address, four bytes for the IPv4, and six bytes for the IPv6.

  uint16_t socket_id; ///< The socket number associated with the read event is two bytes.

  uint32_t length; ///< Four bytes. Length of received data.

  uint16_t offset; ///< Two bytes. Offset data from the start of the buffer.

  uint16_t dest_port; ///< Two bytes. Port number of the device which sends data to the destination.

  union {
    uint8_t ipv4_address
      [SL_IPV4_ADDRESS_LENGTH]; ///< Four bytes. IPv4 address of the device which sends data. Used if ip_version is four.
    uint8_t ipv6_address
      [SL_IPV6_ADDRESS_LENGTH]; ///< 16 bytes. IPv6 address of the device which sends data. Used if ip_version is six.
  } dest_ip_addr;               ///< Union for IPv4 or IPv6 address, depending on ip_version.
} sl_si91x_socket_metadata_t;

/**
 * @typedef sl_si91x_socket_receive_data_callback_t
 * @brief Callback function reads asynchronous data from the socket.
 *
 * @details
 * The callback function reads asynchronous data from the socket when the sl_si91x_socket_async
 * API is registered and called. The callback provides the following details:
 * socket ID, pointer to the buffer which contains receiver data, size of the buffer, 
 * and metadata of the receiver packet (such as IP address, and port number).
 * 
 *
 * @param socket
 *   Socket ID.
 *
 * @param buffer
 *   Pointer to the buffer which stores the receiver data.
 *
 * @param length
 *   Buffer size.
 *
 * @param firmware_socket_response
 * Pointer to sl_si91x_socket_metadata_t structure contains receiver packet metadata information. 
 * The metadata information consists of IP address (either, Ipv4 or IPV6), and port number.
 *
 * @return
 *   N/A
 */
typedef void (*sl_si91x_socket_receive_data_callback_t)(uint32_t socket,
                                                        uint8_t *buffer,
                                                        uint32_t length,
                                                        const sl_si91x_socket_metadata_t *firmware_socket_response);

/**
 * @typedef sl_si91x_socket_accept_callback_t
 * @brief Callback functions for new asynchronous accepted connection.
 *
 * @details
 * The callback provides paramenters for new accepted connection when the sl_si91x_accept_async API is registered and called. 
 * The callback provides the following details: socket ID of the accepted connection, address of remoter peer, 
 * and IP version of connection.
 *
 * @param socket
 *   Socket ID of the accepted connection.
 *
 * @param addr
 *   Pointer to `struct sockaddr` contains remote peer address.
 *
 * @param ip_version
 *   IP version of the connection (for example, four bytes for IPv4, and six bytes for IPv6).
 *
 * @return
 *   N/A
 */
typedef void (*sl_si91x_socket_accept_callback_t)(int32_t socket, struct sockaddr *addr, uint8_t ip_version);

/**
 * @typedef sl_si91x_socket_data_transfer_complete_handler_t
 * @brief Callback function indicates data transfer status.
 *
 * @details
 * The callback indicates the data transfer completion status when either of the sl_si91x_send_async or sl_si91x_sendto_async API is registered and called.
 * The callback provides the socket ID, and the number of bytes that are successfully transfer.
 *
 * @param socket
 *   Socket ID.
 *
 * @param length
 *   Number of bytes transferred.
 *
 * @return
 *   N/A
 */
typedef void (*sl_si91x_socket_data_transfer_complete_handler_t)(int32_t socket, uint16_t length);

/**
 * @typedef sl_si91x_socket_select_callback_t
 * @brief Callback function indicates asynchronous select request result.
 *
 * @details
 * The callback indicates asynchronous response reaches the select request when the sl_si91x_select API is registered and called. 
 * The callback provides the following details: file descriptor sets for read, write, and exception conditions, and status of the selected request.
 *
 * @param fd_read
 *   File descriptor pointer sets for read operations.
 *
 * @param fd_write
 *   File descriptor pointer sets for write operations.
 *
 * @param fd_except
 *   File descriptor pointer sets for exception condition.
 *
 * @param status
 *   Select request status.
 *
 * @return
 *   N/A
 */
#ifndef __ZEPHYR__
typedef void (*sl_si91x_socket_select_callback_t)(fd_set *fd_read, fd_set *fd_write, fd_set *fd_except, int32_t status);
#else
typedef void (*sl_si91x_socket_select_callback_t)(sl_si91x_fdset_t *fd_read,
                                                  sl_si91x_fdset_t *fd_write,
                                                  sl_si91x_fdset_t *fd_except,
                                                  int32_t status);
#endif

/**
 * @typedef sl_si91x_socket_remote_termination_callback_t
 * @brief Callback function indicates termination of the remote socket.
 *
 * @details
 * The callback function notifies on the termination of the remote socket when the sl_si91x_set_remote_termination_callback API is registered and called.
 * The callback provides the following details: host socket index (BSD socket descriptor), remote peer port number, and number of bytes sent before termination of the remote socket.
 *
 * Typical causes reported by firmware:
 * - Remote peer closed the connection with a TCP FIN exchange (or RST).
 * - Remote peer disconnected abruptly / became unreachable and no response until
 *   keep-alive timeout expires.
 * - TCP send/retransmission retries were exhausted.
 *
 * @note `SL_SI91X_EXT_TCP_IP_WAIT_FOR_SOCKET_CLOSE`
 * - If this bit is **not** set in `ext_tcp_ip_feature_bit_map`, firmware auto-closes the
 *   TCP socket on remote termination (host need not issue an explicit close for cleanup
 *   in the auto-close path).
 * - If this bit **is** set (recommended for TCP), the socket is not fully closed until the
 *   host calls `close()` / `sl_si91x_shutdown()` after this callback.
 *
 * Use the same socket index returned by `sl_si91x_socket()` / `sl_si91x_socket_async()` / `socket()` to correlate create, transfer, and remote-termination events.
 *
 * @param[in] socket
 *   Host socket index (BSD socket descriptor) returned by socket creation APIs.
 *
 * @param[in] port
 *   Remote peer port number.
 *
 * @param[in] bytes_sent
 *   Number of bytes sent before termination.
 *
 * @return
 *  The callback does not return a value.
 */
typedef void (*sl_si91x_socket_remote_termination_callback_t)(int socket, uint16_t port, uint32_t bytes_sent);

/** @} */

/// Internal  si91x BSD socket status
typedef enum {
  S_RESET = 0,   // State of unallocated socket.
  INITIALIZED, // Socket attains this state when socket() has been executed successfully.
  BOUND,       // Socket attains this state when bind() has been executed successfully.
  LISTEN,      // (TCP ONLY STATE) Socket attains this state when listen() has been executed successfully.
  UDP_UNCONNECTED_READY, // (UDP ONLY STATE) Socket attains this state when sendto() or recvfrom() has been executed successfully prior connect.
  CONNECTING,   // Non-blocking TCP client connect in progress (after sync RX1 / EINPROGRESS).
  CONNECTED,    // Socket attains this state when connect() has been executed successfully.
  DISCONNECTED, // Socket attains this state when underlying connection is lost.
} sli_si91x_bsd_socket_state_t;

/// Internal reasons for SI91x BSD socket disconnection, applicable only when the socket state is DISCONNECTED.
typedef enum {
  SLI_SI91X_BSD_DISCONNECT_REASON_INTERFACE_DOWN, // Indicates the connection was lost due to the network interface going down.
  SLI_SI91X_BSD_DISCONNECT_REASON_REMOTE_CLOSED // Indicates the connection was terminated by the remote endpoint.
} sli_si91x_bsd_disconnect_reason_t;

#define SLI_SI91X_MAX_SIZE_OF_EXTENSION_DATA 256

#ifdef SLI_SI91X_NETWORK_DUAL_STACK
#if LWIP_SOCKET && !LWIP_IPV4
struct sockaddr_in {
  u8_t sin_len;            /* length of this structure    */
  sa_family_t sin_family;  /* AF_INET                     */
  in_port_t sin_port;      /* Transport layer port #      */
  struct in_addr sin_addr; /* IPv4 address                */
  char sin_zero[8];        /* padding                     */
};
#endif
#if LWIP_SOCKET && !LWIP_IPV6
struct sockaddr_in6 {
  u8_t sin6_len;             /* length of this structure    */
  sa_family_t sin6_family;   /* AF_INET6                    */
  in_port_t sin6_port;       /* Transport layer port #      */
  u32_t sin6_flowinfo;       /* IPv6 flow information       */
  struct in6_addr sin6_addr; /* IPv6 address                */
  u32_t sin6_scope_id;       /* Set of interfaces for scope */
};
#endif
#endif

#pragma pack()
/// Internal  si91x TLS extensions
typedef struct {
  uint8_t buffer[SLI_SI91X_MAX_SIZE_OF_EXTENSION_DATA]; ///< Buffer
  uint16_t total_extensions;                            ///< Total extensions
  uint16_t current_size_of_extensions;                  ///< Current size of extensions
} sli_si91x_tls_extensions_t;

/// Structure to hold WebSocket host, resource and subprotocol information
typedef struct {
  uint8_t host_length;        ///< Length of WebSocket host name
  uint8_t resource_length;    ///< Length of WebSocket resource name
  uint8_t subprotocol_length; ///< Length of WebSocket subprotocol name
  uint8_t origin_length;      ///< Length of WebSocket origin
  uint8_t websocket_data[];   ///< WebSocket host name, resource name, subprotocol name and origin
} sli_si91x_websocket_info_t;

#pragma pack()

/// Internal si91x socket handle
typedef struct {
  int32_t id;                          ///< Socket ID
  int32_t type;                        ///< Socket type
  int32_t index;                       ///< Socket index
  int role;                            ///< Socket role
  int32_t protocol;                    ///< Protocol
  uint16_t tcp_keepalive_initial_time; ///< TCP keepalive initial time
  uint8_t max_tcp_retries;             ///< MAX TCP retries
  uint16_t read_timeout;               ///< Read timeout
  uint8_t certificate_index;           ///< Certificate Index
  uint8_t vap_id;                      ///< Virtual AP ID
  uint16_t mss;                        ///< Maximum segment size (MSS) value
  struct sockaddr_in6 local_address;   ///< Using sockaddr_in6 to hold either IPV4 or IPV6.
  struct sockaddr_in6 remote_address;  ///< Using sockaddr_in6 to hold either IPV4 or IPV6.
  sli_si91x_bsd_socket_state_t state;  ///< BSD socket state (used for internal tracking)
  sli_si91x_bsd_disconnect_reason_t
    disconnect_reason; ///< BSD socket disconnection reasons, applicable only when the socket state is DISCONNECTED.
  sli_si91x_tls_extensions_t tls_extensions; ///< TLS Extension
  bool is_waiting_on_ack;                    ///< Boolean flag to check if socket is waiting for an ack.
#if defined(SLI_SI917)
  uint32_t ssl_bitmap;                       ///< SSL bitmap
  uint32_t max_retransmission_timeout_value; ///< Max retransmission timeout value
  uint32_t tos;                              ///< TOS
#else
  uint8_t ssl_bitmap;  ///< SSL Bitmap
#endif
  uint8_t opcode;                                                          ///< Opcode used in websocket
  sli_si91x_websocket_info_t *websocket_info;                              ///< Pointer to WebSocket info
  sl_si91x_socket_receive_data_callback_t recv_data_callback;              ///< Receive data callback
  sl_si91x_socket_data_transfer_complete_handler_t data_transfer_callback; ///< Data transfer callback
  sl_si91x_socket_accept_callback_t user_accept_callback;                  ///< Async Accept callback
  osEventFlagsId_t socket_events;                                          ///< Event Flags for sockets
  int32_t client_id;                                                       ///< Client Socket Id for accept
  uint8_t socket_bitmap;                                                   ///< Socket Bitmap
  uint8_t data_buffer_count; ///< Number of queued data buffers allocated by this socket
  uint8_t data_buffer_limit; ///< Maximum number of queued data buffers permitted for this socket
  sli_queue_t rx_queue;      ///< Receive queue for incoming commands
  sli_command_engine_packet_type_configuration_t socket_packet_type_configuration; ///< Pointer to socket packet
  bool is_receive_cmd_pending; ///< Flag to indicate if a receive command is pending
  uint8_t *domain_name;        ///< Expected domain name for TLS certificate verification
  uint8_t socket_ext_bitmap;   ///< Extended socket bitmap
  int32_t pending_error;       ///< Per-socket SO_ERROR; cleared on getsockopt(SO_ERROR)
} sli_si91x_socket_t;

/// Socket create command request structure
#pragma pack(1)
typedef struct {
  uint16_t ip_version; ///< ip version4 or 6
  uint16_t
    socket_type; ///< 0=TCP Client, 1=UDP Client, 2=TCP Server. Can be ORed with SOCK_NONBLOCK for non-blocking TCP client.
  uint16_t local_port;  ///< Our local module port number
  uint16_t remote_port; ///< Port number of what we are connecting to
  union {
    uint8_t ipv4_address[SL_IPV4_ADDRESS_LENGTH]; ///<  remote IPv4 Address
    uint8_t ipv6_address[SL_IPV6_ADDRESS_LENGTH]; ///<  remote IPv6 Address
  } dest_ip_addr;                                 ///< Destination IP address
  uint16_t max_count;                             ///< maximum no of LTCP sockets on same port
#if defined(SLI_SI917)
  uint16_t tos; ///< type of service

#else
  uint32_t tos;        ///< type of service

#endif

#if defined(SLI_SI917)
  uint32_t ssl_bitmap; ///< ssl version select bit map

#else
  uint8_t ssl_bitmap;  ///< ssl version select bit map
  uint8_t ssl_ciphers; ///< ssl ciphers bitmap
#endif
  uint8_t webs_resource_name[SLI_WEBS_MAX_URL_LENGTH]; ///< web socket resource name
  uint8_t webs_host_name[SLI_WEBS_MAX_HOST_LENGTH];    ///< web socket host name
  uint8_t max_tcp_retries_count;                       ///< TCP retries
  uint8_t socket_bitmap;                               ///< Socket bitmap
  uint8_t rx_window_size;                              ///< RX window size
  uint16_t tcp_keepalive_initial_time;                 ///< TCP keepalive initial timeout
  uint8_t vap_id;                                      ///< VAPID
  uint8_t socket_cert_inx;                             ///< socket cert inx
  uint32_t ssl_ciphers_bitmap;                         ///< ssl ciphers bitmap
#if defined(SLI_SI917)
  uint32_t ssl_ext_ciphers_bitmap;          ///< ssl extended ciphers bitmap
  uint8_t max_retransmission_timeout_value; ///< max retransmission timeout value
#endif
  uint8_t tcp_retry_transmit_timer;                                 ///< tcp retry transmission timer
  uint16_t tcp_mss;                                                 ///< TCP MSS
  uint16_t no_of_tls_extensions;                                    ///< number of TLS extensions
  uint16_t total_extension_length;                                  ///< total extension length
  uint8_t tls_extension_data[SLI_SI91X_MAX_SIZE_OF_EXTENSION_DATA]; ///< TLS extension data
#if defined(SLI_SI917)
  uint16_t recv_buff_len; ///< receive buffer length
#endif
  uint32_t reserved;                                              ///< reserved
  uint8_t webs_subprotocol_name[SLI_WEBS_MAX_SUBPROTOCOL_LENGTH]; ///< web socket subprotocol name
  uint8_t domain_name[SLI_SI91X_MAX_DOMAIN_NAME_LENGTH];          ///< Domain name for SNI or Host header
  uint8_t socket_ext_bitmap;                                      ///< Extended socket bitmap
  uint8_t webs_origin[SLI_WEBS_MAX_ORIGIN_LENGTH];                ///< web socket origin
} sli_si91x_socket_create_request_t;
#pragma pack()

/// Socket create command response structure
typedef struct {
  uint8_t ip_version[SLI_SI91X_2BYTE_FIELD_SIZE];  ///< ip version 4 or 6
  uint8_t socket_type[SLI_SI91X_2BYTE_FIELD_SIZE]; ///< 2 bytes, type of socket created
  uint8_t socket_id[SLI_SI91X_2BYTE_FIELD_SIZE];   ///< 2 bytes socket descriptor, like a file handle, usually 0x00
  uint8_t module_port[SLI_SI91X_2BYTE_FIELD_SIZE]; ///< 2 bytes, Port number of our local socket
  uint8_t dst_port[SLI_SI91X_2BYTE_FIELD_SIZE];    ///< Destination port number
  union {
    uint8_t ipv4_addr[SL_IPV4_ADDRESS_LENGTH]; ///< 4 bytes, Our (module) IPv4 Address
    uint8_t ipv6_addr[SL_IPV6_ADDRESS_LENGTH]; ///< 16 bytes, Our (module) IPv6 Address
  } module_ip_addr;                            ///< Module IP address
  union {
    uint8_t ipv4_addr[SL_IPV4_ADDRESS_LENGTH];     ///< 4 bytes, Our (module) IPv4 Address
    uint8_t ipv6_addr[SL_IPV6_ADDRESS_LENGTH];     ///< 16 bytes, Our (module) IPv6 Address
  } dest_ip_addr;                                  ///< Destination IP address
  uint8_t mss[SLI_SI91X_2BYTE_FIELD_SIZE];         ///< 2 bytes, Remote peer MSS size
  uint8_t window_size[SLI_SI91X_4BYTE_FIELD_SIZE]; ///< 4 bytes, Remote peer Window size
} sli_si91x_socket_create_response_t;
#pragma pack()

/// socket accept request structure
#pragma pack(1)
typedef struct {
  uint8_t socket_id;    ///< Socket ID
  uint16_t source_port; ///< Local port number
} sli_si91x_socket_accept_request_t;
#pragma pack()

#pragma pack(1)
/// Socket close command request structure
typedef struct {
  uint16_t socket_id;   ///< 2 bytes, socket that was closed
  uint16_t port_number; ///< 4 bytes, port number
} sli_si91x_socket_close_request_t;
#pragma pack()

/// LTCP socket establish request structure
#pragma pack(1)
typedef struct {
  /// IP version
  uint16_t ip_version;

  /// 2 bytes, socket handle
  uint16_t socket_id;

  /// 2 bytes, remote port number
  uint16_t dest_port;

  union {

    ///  remote IPv4 Address
    uint8_t ipv4_address[SL_IPV4_ADDRESS_LENGTH];

    ///  remote IPv6 Address
    uint8_t ipv6_address[SL_IPV6_ADDRESS_LENGTH];
  } dest_ip_addr; ///< Destination IP address

  /// 2 bytes, remote peer MSS size
  uint16_t mss;

  /// 4 bytes, remote peer Window size
  uint32_t window_size;

  /// source port number
  uint16_t src_port_num;
} sli_si91x_rsp_ltcp_est_t;
#pragma pack()

/// The select socket array manager.  */
typedef struct {
  uint32_t fd_array[(SLI_NUMBER_OF_SOCKETS + 31) / 32]; ///< Bit map of SOCKET Descriptors.
  int32_t fd_count;                                     ///< How many are SET
} sli_si91x_fd_set_t;

/// Si91x specifc socket select request structure
typedef struct {
  uint8_t num_fd;                     ///< Number of file descriptors
  uint8_t select_id;                  ///< Select ID
  sli_si91x_fd_set_t read_fds;        ///< Read file descriptors
  sli_si91x_fd_set_t write_fds;       ///< Write file descriptors
  sl_si91x_time_value select_timeout; ///< Select timeout
  uint8_t no_timeout;                 ///< No timeout
} sli_si91x_socket_select_req_t;

/// Si91x specific socket select response structure
typedef struct {
  uint8_t select_id;                ///< Select ID
  sli_si91x_fd_set_t read_fds;      ///< Read file descriptors
  sli_si91x_fd_set_t write_fds;     ///< Write file descriptors
  uint32_t socket_terminate_bitmap; ///< Socket terminate bitmap
} sli_si91x_socket_select_rsp_t;

/// Structure for TCP ACK indication
typedef struct {
  /// Socket ID
  uint8_t socket_id;

  /// Length
  uint8_t length[SLI_SI91X_2BYTE_FIELD_SIZE];

} sli_si91x_rsp_tcp_ack_t;

#pragma pack(1)
/// Si91x specifc send data on socket request structure
typedef struct {
  uint16_t ip_version;  ///< ip version 4 or 6
  uint16_t socket_id;   ///< socket descriptor of the already opened socket connection
  uint32_t length;      ///< length of the data to be sent
  uint16_t data_offset; ///< Data Offset
  uint16_t dest_port;   ///< destination port
  union {
    uint8_t ipv4_address[SL_IPV4_ADDRESS_LENGTH]; ///< 4 bytes, IPv4 Address of the remote device
    uint8_t ipv6_address[SL_IPV6_ADDRESS_LENGTH]; ///< 16 bytes, IPv6 Address of the remote device
  } dest_ip_addr;                                 ///< IP address of the remote device
  uint8_t send_buffer[];                          ///< data buffer to send
} sli_si91x_socket_send_request_t;
#pragma pack()

/// Config command request structure
typedef struct {
  /// config type
  uint16_t config_type;

  /// value to set
  uint16_t value;
} sli_si91x_config_request_t;

/// read bytes coming on socket request structure
typedef struct {
  /// socket id
  uint8_t socket_id;

  /// requested bytes
  uint8_t requested_bytes[SLI_SI91X_4BYTE_FIELD_SIZE];

  /// Timeout for read
  uint8_t read_timeout[SLI_SI91X_2BYTE_FIELD_SIZE];
} sli_si91x_req_socket_read_t;
