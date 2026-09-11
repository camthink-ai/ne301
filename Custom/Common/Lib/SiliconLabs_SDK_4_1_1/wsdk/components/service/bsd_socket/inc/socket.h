/*	$OpenBSD: socket.h,v 1.105 2022/09/03 21:13:48 mbuhl Exp $	*/
/*	$NetBSD: socket.h,v 1.14 1996/02/09 18:25:36 christos Exp $	*/

/*
 * Copyright (c) 1982, 1985, 1986, 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)socket.h	8.4 (Berkeley) 2/21/94
 */

#ifndef COMPONENTS_BSD_SOCKET_INC_SOCKET_H_
#define	COMPONENTS_BSD_SOCKET_INC_SOCKET_H_

/* get the definitions for struct iovec, size_t, ssize_t, and <sys/cdefs.h> */
#include "uio.h"
#include "netinet_in.h"

#ifdef __cplusplus
extern "C" {	
#endif

#ifndef	_SOCKLEN_T_DEFINED_
#define	_SOCKLEN_T_DEFINED_
typedef	__socklen_t	socklen_t;	/* length type for network syscalls */
#endif

#ifndef	_SA_FAMILY_T_DEFINED_
#define	_SA_FAMILY_T_DEFINED_
typedef	__sa_family_t	sa_family_t;	/* sockaddr address family type */
#endif

/*
 * Definitions related to sockets: types, address families, options.
 */

/*
 * Types
 */
/**
 * @addtogroup BSD_SOCKET_TYPES Socket Type
 * @ingroup BSD_SOCKET_FUNCTIONS
 * @{ 
 */
#define	SOCK_STREAM	1		///< Stream socket. Provides sequenced, reliable, two-way, connection-based byte streams.
#define	SOCK_DGRAM	2		///< Datagram socket. Supports datagrams (connectionless, unreliable messages of a fixed maximum length).
#define	SOCK_RAW	3		///< Raw socket. Provides raw network protocol access. This feature is not supported in the current release.
#define	SOCK_RDM	4		///< Reliably-delivered message. Provides a reliable datagram layer that does not guarantee ordering. This feature is not supported in the current release.
#define	SOCK_SEQPACKET	5   ///< Sequenced packet stream. Provides a sequenced, reliable, two-way, connection-based data transmission path for datagrams of fixed maximum length. This feature is not supported in the current release.
#ifdef _KERNEL
#define	SOCK_TYPE_MASK	0x000F		///< Mask that covers all the above socket types. This feature is not supported in the current release.
#endif
/** @} */

/*
 * Option flags per-socket.
 */
/**
 * @addtogroup BSD_SOCKET_OPTION_NAME Socket Option Name
 * @ingroup BSD_SOCKET_FUNCTIONS
 * @{ 
 */
#define	SO_DEBUG	0x0001		///< Enables recording of debugging information. This option is not supported in the current release.
#define	SO_ACCEPTCONN	0x0002		///< Indicates that the socket has had listen(). This option is not supported in the current release.
#define	SO_REUSEADDR	0x0004		///< Allows local address reuse. This option is not supported in the current release.
#define	SO_KEEPALIVE	0x0008		///< Keeps connections alive.
#define	SO_DONTROUTE	0x0010		///< Uses interface addresses only. This option is not supported in the current release.
#define	SO_BROADCAST	0x0020		///< Permits sending of broadcast messages. This option is not supported in the current release.
#define	SO_USELOOPBACK	0x0040		///< Bypasses hardware when possible. This option is not supported in the current release.
#define	SO_LINGER	0x0080		///< Lingers on close if data is present. This option is not supported in the current release.
#define	SO_OOBINLINE	0x0100		///< Leaves received out-of-band data in line. This option is not supported in the current release.
#define	SO_REUSEPORT	0x0200		///< Allows local address and port reuse. This option is not supported in the current release.
#define SO_TIMESTAMP	0x0800		///< Timestamps received datagram traffic. This option is not supported in the current release.
#define SO_BINDANY	0x1000		///< Allows binding to any address. This option is not supported in the current release.
#define SO_ZEROIZE	0x2000		///< Zeroes out all mbufs sent over the socket. This option is not supported in the current release.
#define SO_MAX_RETRANSMISSION_TIMEOUT_VALUE 0x3012 ///< Configures max retransmission timeout value. The option value associated with this option name should be a power of 2 between 1 and 128.
/*
 * Additional options, not kept in so_options.
 */
#define	SO_SNDBUF	0x1001		///< Sets send buffer size.
#define	SO_RCVBUF	0x1002		///< Sets receive buffer size.
#define	SO_SNDLOWAT	0x1003		///< Sets send low-water mark. This option is not supported in the current release.
#define	SO_RCVLOWAT	0x1004		///< Sets receive low-water mark. This option is not supported in the current release.
#define	SO_SNDTIMEO	0x1005		///< Sets send timeout. This option is not supported in the current release.
#define	SO_RCVTIMEO	0x1006		///< Sets receive timeout. 
#define	SO_ERROR	0x1007		///< Gets error status and clears.
#define	SO_TYPE		0x1008		///< Gets socket type.
#define	SO_NETPROC	0x1020		///< Multiplexes; network processing. This option is not supported in the current release.
#define	SO_RTABLE	0x1021		///< Sets routing table to be used. This option is not supported in the current release.
#define	SO_PEERCRED	0x1022		///< Gets connect-time credentials. This option is not supported in the current release.
#define	SO_SPLICE	0x1023		///< Splices data to another socket. This option is not supported in the current release.
#define	SO_DOMAIN	0x1024		///< Gets socket domain.
#define	SO_PROTOCOL	0x1025		///< Gets socket protocol.

// From Linux include/uapi/linux/tcp.h
// See https://www.kernel.org/doc/Documentation/networking/tls.txt for more details.
#define TCP_ULP	0x001f		///< Attach a ULP (Upper Layer Protocol) to a TCP connection.

/*
 * si91x socket options
 */
#define	SL_SO_CERT_INDEX				0x1026	///< Sets certificate index for SSL socket.
#define	SL_SO_HIGH_PERFORMANCE_SOCKET	0x1027	///< Enables high-performance socket.
#define SL_SO_TLS_SNI                  0x1028  ///< Passes SNI extension for SSL socket.
#define SL_SO_TLS_ALPN                 0x1029  ///< Passes ALPN extension for SSL socket.
#define SL_SO_MSS                      0x102A  ///< Sets the Maximum Segment Size (MSS) for a socket.
#define SL_SO_SOCK_VAP_ID              0x102B  ///< Sets the VAP ID for a socket.
#define SL_SO_MAXRETRY                 0x102C  ///< Sets the maximum number of retries for a socket.
#define SL_SO_VERIFY_DOMAIN_NAME       0x102D  ///< Sets expected domain name for TLS certificate verification.
#define SL_SO_PER_SOCKET_CLOSE         0x102E  ///< Enable per-socket graceful close handling.
/** @} */

/*
 * Option values per-socket.
 */
/**
 * @addtogroup BSD_TLS_OPTION_VALUE TLS Option Value
 * @ingroup BSD_SOCKET_FUNCTIONS
 * @{ 
 */
#define TLS		"tls"		///< Option value for default TLS version.
#define TLS_1_0	"tls_1_0"	///< Option value for TLS 1.0.
#define TLS_1_1	"tls_1_1" 	///< Option value for TLS 1.1.
#define TLS_1_2	"tls_1_2"   ///< Option value for TLS 1.2.
#define TLS_1_3 "tls_1_3"	///< Option value for TLS 1.3.
/** @} */

/**
 * @addtogroup BSD_SOCKET_OPTION_LEVEL Socket Option Level
 * @ingroup BSD_SOCKET_FUNCTIONS
 * @{ 
 */
/*
 * Level number for (get/set)sockopt() to apply to socket itself.
 */
#define	SOL_SOCKET	0xffff		///< This is used to denote that the options are applicable at the socket level.

//From Linux include/linux/socket.h
/* Setsockoptions(2) level. Thanks to BSD these must match IPPROTO_xxx */
#define SOL_TCP 0x0006  ///< This is used to denote that the options are applicable at the TCP level.
/** @} */

/*
 * Address families.
 */
/**
 * @addtogroup BSD_SOCKET_FAMILIY Socket Address Family
 * @ingroup BSD_SOCKET_FUNCTIONS
 * @{ 
 */
#define	AF_UNSPEC	0		///< Unspecified address family.
#define	AF_UNIX		1		///< Local to host (UNIX domain) address family. This domain is not supported in the current release.
#define	AF_LOCAL	AF_UNIX		///< POSIX compatibility alias for AF_UNIX. This domain is not supported in the current release.
#define	AF_INET		2		///< Internet IP Protocol address family.
#define	AF_IMPLINK	3		///< ARPANET IMP address family. This domain is not supported in the current release.
#define	AF_PUP		4		///< PUP protocols (for example, BSP) address family. This domain is not supported in the current release.
#define	AF_CHAOS	5		///< MIT CHAOS protocols address family. This domain is not supported in the current release.
#define	AF_NS		6		///< XEROX NS protocols address family. This domain is not supported in the current release.
#define	AF_ISO		7		///< ISO protocols address family. This domain is not supported in the current release.
#define	AF_OSI		AF_ISO  ///< OSI protocol family. This domain is not supported in the current release.
#define	AF_ECMA		8		///< European Computer Manufacturers Association (ECMA) protocols address family. This domain is not supported in the current release.
#define	AF_DATAKIT	9		///< Datakit protocols address family. This domain is not supported in the current release.
#define	AF_CCITT	10		///< CCITT protocols (for example, X.25) address family. This domain is not supported in the current release.
#define	AF_SNA		11		///< IBM SNA address family. This domain is not supported in the current release.
#define AF_DECnet	12		///< DECnet address family. This domain is not supported in the current release.
#define AF_DLI		13		///< DEC Direct data link interface address family. This domain is not supported in the current release.
#define AF_LAT		14		///< LAT address family. This domain is not supported in the current release.
#define	AF_HYLINK	15		///< NSC Hyperchannel address family. This domain is not supported in the current release.
#define	AF_APPLETALK	16		///< AppleTalk address family. This domain is not supported in the current release.
#define	AF_ROUTE	17		///< Internal Routing Protocol address family. This domain is not supported in the current release.
#define	AF_LINK		18		///< Link layer interface address family. This domain is not supported in the current release.
#define	pseudo_AF_XTP	19		///< eXpress Transfer Protocol (no AF). This domain is not supported in the current release.
#define	AF_COIP		20		///< Connection-oriented IP, aka ST II address family. This domain is not supported in the current release.
#define	AF_CNT		21		///< Computer Network Technology address family. This domain is not supported in the current release.
#define pseudo_AF_RTIP	22		///< Identifier for RTIP packets. This domain is not supported in the current release.
#define	AF_IPX		23		///< Novell Internet Protocol address family. This domain is not supported in the current release.
#define	AF_INET6	24		///< IPv6 address family.
#define pseudo_AF_PIP	25		///< Identifier for PIP packets. This domain is not supported in the current release.
#define AF_ISDN		26		///< Integrated Services Digital Network address family. This domain is not supported in the current release.
#define AF_E164		AF_ISDN		///< CCITT E.164 recommendation address family. This domain is not supported in the current release.
#define AF_NATM		27		///< Native ATM access address family. This domain is not supported in the current release.
#define	AF_ENCAP	28      ///< Encapsulation address family. This domain is not supported in the current release.
#define	AF_SIP		29		///< Simple Internet Protocol address family. This domain is not supported in the current release.
#define AF_KEY		30      ///< Key management protocol address family (originally developed for usage with IPsec).
#define pseudo_AF_HDRCMPLT 31		///< Used by BPF to not rewrite headers in interface output routine. This domain is not supported in the current release.
#define	AF_BLUETOOTH	32		///< Bluetooth address family. This domain is not supported in the current release.
#define AF_MPLS         33              ///< MPLS address family. This domain is not supported in the current release.
#define pseudo_AF_PFLOW 34		///< Identifier for pflow packets. This domain is not supported in the current release.
#define pseudo_AF_PIPEX 35		///< Identifier for PIPEX packets. This domain is not supported in the current release.
#define AF_MAX          36      ///< Maximum address family value. This domain is not supported in the current release.
/** @} */

/*
 * Structure used by kernel to store most
 * addresses.
 */
/**
 * @addtogroup BSD_SOCKET_FUNCTIONS
 * @{ 
 */
/**
 * @struct sockaddr
 * @brief 
 *     The structure stores generic socket address.
 * 
 * @details
 *     The @ref sockaddr structure represents addresses in a protocol-independent way. It includes the address family, and the address data. The actual format of the address data is determined by the address family specified in `sa_family`.
 * 
 */
struct sockaddr {
	uint8_t    sa_len;		///< Total length of the structure.
	sa_family_t sa_family;		///< Address family (for example, AF_INET).
	char	    sa_data[14];	///< Socket address data. The actual format is determined by the address family.
};
/** @} */

/**
 * Sockaddr type which can hold any sockaddr type available
 * in the system.
 *
 * Note: __ss_{len,family} is defined in RFC2553.  During RFC2553 discussion
 * the field name went back and forth between ss_len and __ss_len,
 * and RFC2553 specifies it to be __ss_len.  openbsd picked ss_len.
 * For maximum portability, userland programmer would need to
 * (1) make the code never touch ss_len portion (cast it into sockaddr and
 * touch sa_len), or (2) add "-Dss_len=__ss_len" into CFLAGS to unify all
 * occurrences (including header file) to __ss_len.
 */
struct sockaddr_storage {
	uint8_t	ss_len;		///< Total length. 
	sa_family_t	ss_family;	///< Address family.
	unsigned char	__ss_pad1[6];	///< Align to quad. 
	uint64_t	__ss_pad2;	///< Force alignment for stupid compilers. 
	unsigned char	__ss_pad3[240];	///< Pad to a total of 256 bytes. 
};

/* __BEGIN_DECLS */

/**
 * @addtogroup BSD_SOCKET_FUNCTIONS
 * @{ 
 */

/***************************************************************************/ 
/**
 * @brief
 *   Create an endpoint for communication.
 * 
 * @details
 *   The `socket()` function creates an endpoint for communication and returns a file descriptor that refers to that endpoint. 
 *   The file descriptor returned by a successful call would be the lowest-numbered file descriptor not currently open for the process.
 * 
 * @pre Pre-conditions:
 * - The Wi-Fi/Net stack must be initialized and a network profile (STA or AP) must be up.
 * - If socket resources are configured using @ref sl_si91x_config_socket(), that call must have been made before @ref socket().
 *
 * @post Post-conditions:
 * - On success a new socket descriptor is allocated and can be passed to @ref bind(), @ref connect(),
 *   @ref listen(), @ref send(), @ref recv(), etc.
 * - On failure no descriptor is allocated and @c errno is set.
 *
 * @param[in] domain
 *   Specifies a communication domain, selecting the protocol family to be used for communication. 
 *   Must be one of the values from @ref BSD_SOCKET_FAMILIY. Only @ref AF_INET and @ref AF_INET6 are supported.
 * 
 * @param[in] type
 *   Specifies the semantics of communication for the socket. 
 *   Must be one of the values from BSD_SOCKET_TYPES. 
 *   Currently defined types are SOCK_STREAM, SOCK_DGRAM, SOCK_RAW, SOCK_RDM, and SOCK_SEQPACKET.
 *   Only @ref SOCK_STREAM and @ref SOCK_DGRAM are supported.
 * 
 * @param[in] protocol
 *   Specifies a particular protocol to be used with the socket. Normally, only a single protocol exists to support a particular socket type 
 *   within a given protocol family. However, if multiple protocols exist, a specific protocol must be specified. 
 *   The protocol number to use is specific to the communication domain. Must be one of the values from @ref BSD_SOCKET_PROTOCOL.
 *   Use @c IPPROTO_TCP with @ref SOCK_STREAM and @c IPPROTO_UDP with @ref SOCK_DGRAM.
 * 
 * @return 
 *   Returns the socket ID or file descriptor that references the socket upon success.
 *   Returns -1 if an error occurs and sets @c errno.
 *
 * @retval >=0        Valid socket descriptor.
 * @retval -1/EAFNOSUPPORT    @p domain is not supported.
 * @retval -1/EPROTONOSUPPORT @p protocol is not supported for the given @p domain / @p type.
 * @retval -1/EINVAL          Invalid combination of @p domain, @p type, or @p protocol.
 * @retval -1/EMFILE          No free socket descriptor available (socket pool exhausted).
 * @retval -1/ENOMEM          Not enough memory to allocate the socket.
 *
 * @note Thread safety:
 * - Thread-safe: multiple threads may call @ref socket() concurrently to obtain distinct descriptors.
 *
 * @note Side effects:
 * - Allocates an entry in the internal socket pool. On failure, @c errno is updated.
 *
 * @note
 * 	The \p type supports SOCK_STREAM and SOCK_DGRAM only.
 *
 * @see bind(), listen(), connect(), close(), sl_si91x_config_socket()
 *
 * @par Example
 * Create a TCP/IPv4 stream socket and check for errors:
 * @code{.c}
 * int sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
 * if (sock_fd < 0) {
 *   printf("socket() failed, errno = %d\r\n", errno);
 *   return -1;
 * }
 * @endcode
 *
 * Create a UDP/IPv4 datagram socket:
 * @code{.c}
 * int udp_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
 * if (udp_fd < 0) {
 *   printf("UDP socket() failed, errno = %d\r\n", errno);
 *   return -1;
 * }
 * @endcode
 ******************************************************************************/
int socket(int domain, int type, int protocol);

/***************************************************************************/ 
/**
 * @brief
 *   Bind a name to a socket.
 * 
 * @details
 *   When a socket is created with `socket()`, it exists in a namespace (address family) but, has no address assigned to it. 
 *   The `bind()` function assigns the address specified by `addr` to the socket referred to by the file descriptor `socket_id`.  
 *   Traditionally, this operation is called "assigning a name to a socket".
 * 
 *   It is normally necessary to assign a local address using `bind()` before a `SOCK_STREAM` socket may receive connections.
 * 
 * @pre Pre-conditions:
 * - @p socket_id must be a valid descriptor returned by @ref socket().
 * - @ref bind() must be called before @ref listen() on a TCP server socket and before sending/receiving on a UDP socket that needs a fixed local port.
 *
 * @post Post-conditions:
 * - On success the socket is associated with the supplied local address/port.
 * - On failure the socket remains unbound; @c errno is set.
 *
 * @param[in] socket_id
 *   The socket ID or file descriptor for the specified socket.
 * 
 * @param[in] addr 
 *   Pointer to a @ref sockaddr structure containing the address to be assigned to the socket.
 *   Must be non-NULL and @c sa_family must match the family used in @ref socket().
 * 
 * @param[in] addr_len 
 *   The `addr_len` parameter specifies the size, in bytes, of the address structure pointed to by `addr`.
 *   Must be @c sizeof(struct sockaddr_in) for IPv4 or @c sizeof(struct sockaddr_in6) for IPv6.
 * 
 * @return
 *   Returns 0 if successful. Otherwise, returns -1 and sets the global variable `errno` to indicate the error.
 *
 * @retval 0                 Success.
 * @retval -1/EBADF         @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK      @p socket_id refers to a non-socket file descriptor.
 * @retval -1/EINVAL        @p addr / @p addr_len is invalid or the socket is already bound.
 * @retval -1/EADDRINUSE    The requested address/port is already in use.
 * @retval -1/EAFNOSUPPORT  @p addr->sa_family does not match the socket family.
 * @retval -1/EACCES        The requested address is protected and the caller lacks permission.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor. Serialize concurrent access to @p socket_id.
 *
 * @note Side effects:
 * - Reserves the local address/port in the stack; updates @c errno on failure.
 *
 * @see socket(), listen(), connect(), getsockname()
 *
 * @par Example
 * Bind a TCP server socket to any local interface on port 5000:
 * @code{.c}
 * struct sockaddr_in local_addr = { 0 };
 * local_addr.sin_family      = AF_INET;
 * local_addr.sin_port        = 5000;
 * local_addr.sin_addr.s_addr = INADDR_ANY;
 *
 * if (bind(sock_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
 *   printf("bind() failed, errno = %d\r\n", errno);
 *   close(sock_fd);
 *   return -1;
 * }
 * @endcode
 ******************************************************************************/
int bind(int socket_id, const struct sockaddr *addr, socklen_t addr_len);

/***************************************************************************/ 
/**
 * @brief
 *   Listen for connections on a socket.
 * 
 * @details
 *   The `listen()` function marks the socket referred to by `socket_id` as a passive socket, 
 *   that is, as a socket that would be used to accept incoming connection requests using `accept()`.
 *   This function applies only to sockets of type `SOCK_STREAM` or `SOCK_SEQPACKET`.
 * 
 * @pre Pre-conditions:
 * - @p socket_id must be a @ref SOCK_STREAM socket created using @ref socket() and bound using @ref bind().
 * - A willingness to accept incoming connections and a queue limit for incoming connections 
 *   must be specified with @ref listen(), and then the connections can be accepted with @ref accept().
 *
 * @post Post-conditions:
 * - On success the socket transitions into the listening state and can be passed to @ref accept().
 * - On failure the socket state is unchanged; @c errno is set.
 * 
 * @param[in] socket_id
 *   The socket ID or file descriptor for the specified socket.
 * 
 * @param[in] backlog
 *   The backlog argument defines the maximum number of pending connections that can be queued.
 *   Must be > 0 and typically must not exceed the configured maximum (see @ref sl_si91x_config_socket()).
 * 
 * @return
 *   Returns 0 if successful. Otherwise, returns -1 and sets the global variable `errno` to indicate the error.
 *
 * @retval 0                 Success.
 * @retval -1/EBADF         @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK      @p socket_id refers to a non-socket file descriptor.
 * @retval -1/EOPNOTSUPP    The socket type does not support @ref listen() (for example, @ref SOCK_DGRAM).
 * @retval -1/EINVAL        The socket is not bound, @p backlog is invalid, or the socket is already connected.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Allocates a pending-connection queue of size @p backlog in the stack.
 *
 * @see socket(), bind(), accept(), sl_si91x_config_socket()
 *
 * @par Example
 * Mark the TCP server socket as passive with a backlog of 1 connection:
 * @code{.c}
 * if (listen(sock_fd, 1) < 0) {
 *   printf("listen() failed, errno = %d\r\n", errno);
 *   close(sock_fd);
 *   return -1;
 * }
 * @endcode
 ******************************************************************************/
int listen(int socket_id, int backlog);

/***************************************************************************/ 
/**
 * @brief
 *   Accept a connection on a socket.
 * 
 * @details
 *   The `accept()` system call is used with connection-based socket types (for example, `SOCK_STREAM`, `SOCK_SEQPACKET`). 
 *   It extracts the first connection request on the queue of pending connections for the listening socket (`socket_id`), 
 *   creates a new connected socket, and returns a new file descriptor referring to that socket. 
 *   The newly created socket is not in the listening state. The original socket (`socket_id`) is unaffected by this call.
 * 
 *   If no pending connections are present on the queue and the original socket is not marked as non-blocking, 
 *   this function blocks the caller until a connection is present. If the original socket is marked non-blocking and 
 *   no pending connections are present on the queue, @ref accept() returns an error. The accepted socket may not be 
 *   used to accept more connections. The original socket (`socket_id`) remains open.
 *
 * @pre Pre-conditions:
 * - @p socket_id must be a @ref SOCK_STREAM socket created using @ref socket(), bound using @ref bind(),
 *   and placed in the listening state using @ref listen().
 *
 * @post Post-conditions:
 * - On success a new, connected socket descriptor is returned for the peer connection; @p addr / @p addr_len are populated (if non-NULL).
 * - The original listening descriptor remains open and can be used for further @ref accept() calls.
 * 
 * @param[in] socket_id
 *   Socket identification number of the socket to accept. This socket must have been created with @ref socket(), 
 *   bound to an address with @ref bind(), and set to listen for connections with @ref listen().
 * 
 * @param[out] addr 
 *   Pointer to a `sockaddr` structure that will be filled with the address of the connecting entity, as known
 *   to the communications layer. The exact format of the `addr` argument is determined by the domain in which the 
 *   communication is occurring. A null pointer may be specified for `addr` if the address information is not desired; 
 *   in this case, `addr_len` is not used and should also be null.
 * 
 * @param[in, out] addr_len 
 *   Pointer to a `socklen_t` variable. Initially, it should contain the amount of space pointed to by `addr`. 
 *   On return, it will contain the actual length (in bytes) of the address returned. This call is used with 
 *   connection-based socket types, currently with `SOCK_STREAM`.
 *   Must be non-NULL when @p addr is non-NULL.
 * 
 * @return
 *   Returns a non-negative integer that is a descriptor for the accepted socket if successful. 
 *   Returns -1 on error and sets the global variable `errno` to indicate the error.
 *
 * @retval >=0               Descriptor for the accepted connection.
 * @retval -1/EBADF         @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK      @p socket_id refers to a non-socket descriptor.
 * @retval -1/EINVAL        The socket is not listening (no prior @ref listen()) or arguments are invalid.
 * @retval -1/EWOULDBLOCK   The socket is non-blocking and no pending connections are available.
 * @retval -1/EMFILE        No free socket descriptor to allocate for the accepted connection.
 * @retval -1/ECONNABORTED  A connection is aborted before @ref accept() returned.
 *
 * @note Thread safety:
 * - Not thread-safe on the same listening descriptor; serialize concurrent @ref accept() calls.
 *
 * @note Side effects:
 * - Allocates a new socket slot for the accepted connection.
 * 
 * @note 
 *   The @ref accept() system call only supports blocking mode.
 *
 * @see socket(), bind(), listen(), close(), getpeername()
 *
 * @par Example
 * Accept an incoming TCP client connection on a previously listened socket:
 * @code{.c}
 * struct sockaddr_in client_addr = { 0 };
 * socklen_t          client_len  = sizeof(client_addr);
 *
 * int client_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &client_len);
 * if (client_fd < 0) {
 *   printf("accept() failed, errno = %d\r\n", errno);
 *   close(sock_fd);
 *   return -1;
 * }
 * printf("Client connected from port %u\r\n", ntohs(client_addr.sin_port));
 * @endcode
 ******************************************************************************/
int accept(int socket_id, struct sockaddr *addr, socklen_t *addr_len);

/***************************************************************************/ 
/**
 * @brief
 *   Initiate a connection on a socket.
 * 
 * @details
 *   The @ref connect() system call connects the socket referred to by the file descriptor `socket_id` to the address specified by `addr`.  
 *   The `addr_len` argument specifies the size of `addr`. The format of the address in `addr` is determined by 
 *   the address space of the socket `socket_id`; see @ref socket() for further details.
 * 
 *   If `socket_id` is of type `SOCK_DGRAM`, the @ref connect() system call specifies the peer with which the socket is to be associated.
 *   If the socket is of type `SOCK_STREAM`, the @ref connect() system call attempts to make a connection to another socket. The other 
 *   socket is specified by `addr`, which is an address in the communications space of the socket. Each communications space interprets
 *   the `addr` argument in its own way. Generally, stream sockets may successfully @ref connect() only once; datagram sockets can use 
 *   @ref connect() multiple times to change their association.
 *
 * @pre Pre-conditions:
 * - @p socket_id must be a descriptor created using @ref socket().
 * - For TCP (@ref SOCK_STREAM) any TLS / socket options (e.g. @ref TCP_ULP, @ref SL_SO_TLS_SNI) must be set using @ref setsockopt() before calling @ref connect().
 * - The network profile must be up and the destination must be reachable.
 *
 * @post Post-conditions:
 * - On success a TCP socket enters the connected state and can be used with @ref send() / @ref recv().
 * - For UDP the peer is cached and @ref send()/@ref recv() may be used without specifying a destination.
 * - On failure the socket state is unchanged except for @c errno; the descriptor may need to be closed.
 * 
 * @param[in] socket_id
 *   The socket ID or file descriptor for the specified socket.
 * 
 * @param[in] addr 
 *   The addr argument of type @ref sockaddr is the address is that to which datagrams are to be sent.
 *   Must be non-NULL; @c sa_family must match the socket family.
 * 
 * @param[in] addr_len 
 *   The addr_len argument of type @ref socklen_t indicates the amount of space pointed to by address, in bytes.
 *   Must be @c sizeof(struct sockaddr_in) for IPv4 or @c sizeof(struct sockaddr_in6) for IPv6.
 * 
 * @return
 *   Returns 0 if successful. Otherwise, returns -1 and sets the global variable `errno` to indicate the error.
 *
 * @retval 0                  Success.
 * @retval -1/EBADF          @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK       @p socket_id refers to a non-socket descriptor.
 * @retval -1/EINVAL         Invalid @p addr or @p addr_len.
 * @retval -1/EFAULT         @p addr is NULL or points to invalid memory.
 * @retval -1/EAFNOSUPPORT   Address family does not match the socket family.
 * @retval -1/ECONNREFUSED   Remote peer refused the connection.
 * @retval -1/ETIMEDOUT      Connection attempt timed out.
 * @retval -1/ENETUNREACH    No route to the remote peer.
 * @retval -1/EALREADY       A previous connection attempt is still in progress.
 * @retval -1/EISCONN        Socket is already connected.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Allocates internal TCP/TLS state. For TLS, initiates the handshake synchronously.
 * - Updates @c errno on failure.
 * 
 * @note
 *   Connecting to an invalid address, such as a null address, will result in an `EFAULT` error.
 *
 * @see socket(), bind(), setsockopt(), send(), recv(), close()
 *
 * @par Example
 * Connect a TCP client socket to a remote server at 192.168.0.100:5001:
 * @code{.c}
 * struct sockaddr_in server_addr = { 0 };
 * server_addr.sin_family         = AF_INET;
 * server_addr.sin_port           = 5001;
 * sl_net_inet_addr("192.168.0.100", &server_addr.sin_addr.s_addr);
 *
 * if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
 *   printf("connect() failed, errno = %d\r\n", errno);
 *   close(sock_fd);
 *   return -1;
 * }
 * @endcode
 ******************************************************************************/
int connect(int socket_id, const struct sockaddr *addr, socklen_t addr_len);

/***************************************************************************/ 
/**
 * @brief
 *   Receive a message from a socket.
 *  
 * @details
 *   The @ref recv() function is normally used only on a connected socket. It receives messages from a socket and returns the length of the message on successful completion. 
 *   If a message is too long to fit in the supplied buffer, excess bytes may be discarded depending on the type of socket the message is received from.
 *   By default, @ref recv() is a blocking API. To use it in a non-blocking manner, you can set the socket to non-blocking mode using `setsockopt()`.
 *
 * @pre Pre-conditions:
 * - For TCP the socket must be connected using @ref connect() or obtained from @ref accept().
 * - For UDP the socket must be bound using @ref bind() or connected using @ref connect() to a peer.
 *
 * @post Post-conditions:
 * - On success @p buf contains the received bytes and the return value is the number of bytes read.
 * - A return value of 0 indicates that the peer has performed an orderly shutdown (TCP).
 * 
 * @param[in] socket_id
 *   The socket ID or file descriptor for the specified socket.
 * 
 * @param[out] buf
 *   Pointer to the buffer that receives the data. Must be non-NULL.
 * 
 * @param[in] buf_len
 *   Length of the buffer pointed to by the `buf` parameter. Must be > 0.
 * 
 * @param[in] flags
 *   Controls the reception of the data. Currently, no flags are supported and this parameter should be set to 0.
 * 
 * @return
 *   Number of bytes received on success (0 on orderly peer close for TCP); -1 on error with @c errno set.
 *
 * @retval >0              Number of bytes read into @p buf.
 * @retval 0               Orderly shutdown by the peer (TCP).
 * @retval -1/EBADF       @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK    @p socket_id refers to a non-socket descriptor.
 * @retval -1/ENOTCONN    Socket is not connected.
 * @retval -1/EINVAL      @p buf is NULL or @p buf_len is 0.
 * @retval -1/EWOULDBLOCK Non-blocking socket has no data available.
 * @retval -1/ETIMEDOUT   @ref SO_RCVTIMEO elapsed before data was received.
 * @retval -1/ECONNRESET  Peer reset the connection.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Blocks the calling thread unless @ref SO_RCVTIMEO is set or the socket is non-blocking.
 * - Updates @c errno on failure.
 * 
 * @note 
 *   The @ref recv() system call does not support any flags.
 *
 * @see recvfrom(), send(), connect(), accept(), setsockopt()
 *
 * @par Example
 * Receive up to 1460 bytes from a connected TCP socket in a loop, processing
 * the data on each successful read:
 * @code{.c}
 * uint8_t rx_buffer[1460];
 * ssize_t bytes_read = 0;
 *
 * while ((bytes_read = recv(client_fd, rx_buffer, sizeof(rx_buffer), 0)) > 0) {
 *   ...
 * }
 *
 * if (bytes_read < 0) {
 *   printf("recv() failed, errno = %d\r\n", errno);
 * } else {
 *   printf("Peer closed the connection\r\n");
 * }
 * close(client_fd);
 * @endcode
 ******************************************************************************/
ssize_t recv(int socket_id, void *buf, size_t buf_len, int flags);

/***************************************************************************/ 
/**
 * @brief
 *   Receive a message from a socket.
 * 
 * @details
 *   The @ref recvfrom() system call is used to receive messages from a socket and can be used to receive data on a socket 
 *   whether or not it is connection-oriented. If `from_addr` is not a null pointer and the socket is not connection-oriented, 
 *   the source address of the message is filled in.
 *
 * @pre Pre-conditions:
 * - For UDP the socket must be bound using @ref bind() (at least implicitly).
 * - For TCP the socket must be connected or obtained from @ref accept().
 *
 * @post Post-conditions:
 * - On success @p buf contains the datagram/segment bytes and, when @p from_addr is non-NULL, the source address is populated.
 * 
 * @param[in] socket_id
 *   The socket ID or file descriptor for the specified socket.
 * 
 * @param[out] buf
 *   Pointer to the buffer that receives the data. Must be non-NULL.
 * 
 * @param[in] buf_len
 *   The length of the buffer pointed to by the `buf` parameter, in bytes. Must be > 0.
 * 
 * @param[in] flags
 *   Controls the reception of the data. Currently, no flags are supported and this parameter should be set to 0.
 * 
 * @param[out] from_addr 
 *   Pointer to a socket address structure of type @ref sockaddr that will be filled with the source address of the received message. 
 *   If the source address is not required, this parameter can be NULL.
 * 
 * @param[in, out] from_addr_len 
 *   A value-result argument of type @ref socklen_t. Initially, it should contain the size of the buffer associated with `from_addr`. 
 *   On return, it will be modified to indicate the actual size of the address stored there. If `from_addr` is NULL, this parameter is ignored.
 *   Must be non-NULL when @p from_addr is non-NULL.
 * 
 * @return
 *   Returns the length of the message on successful completion. Returns -1 on error and sets the global variable `errno` to indicate the error.
 *
 * @retval >=0             Number of bytes received (0 is valid for zero-length UDP datagrams).
 * @retval -1/EBADF       @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK    @p socket_id refers to a non-socket descriptor.
 * @retval -1/EINVAL      @p buf is NULL or @p buf_len is 0.
 * @retval -1/EWOULDBLOCK Non-blocking socket has no data available.
 * @retval -1/ETIMEDOUT   @ref SO_RCVTIMEO elapsed before data was received.
 * @retval -1/ECONNRESET  Peer reset the connection.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Blocks the calling thread unless @ref SO_RCVTIMEO is set or the socket is non-blocking.
 * - Truncated datagrams (UDP) cause silent discard of the excess bytes.
 * - Updates @c errno on failure.
 * 
 * @note 
 *   The @ref recvfrom() system call does not support any flags.
 *
 * @see recv(), sendto(), bind(), setsockopt()
 *
 * @par Example
 * Receive a UDP datagram and retrieve the sender's address:
 * @code{.c}
 * uint8_t            buffer[1472];
 * struct sockaddr_in peer_addr = { 0 };
 * socklen_t          peer_len  = sizeof(peer_addr);
 *
 * ssize_t read_bytes = recvfrom(udp_fd,
 *                      buffer,
 *                      sizeof(buffer),
 *                      0,
 *                      (struct sockaddr *)&peer_addr,
 *                      &peer_len);
 * if (read_bytes < 0) {
 *   printf("recvfrom() failed, errno = %d\r\n", errno);
 * } else {
 *   printf("Received %d bytes from port %u\r\n", (int)n, ntohs(peer_addr.sin_port));
 * }
 * @endcode
 ******************************************************************************/
ssize_t recvfrom(int socket_id, void *buf, size_t buf_len, int flags, struct sockaddr *from_addr, socklen_t *from_addr_len);

/***************************************************************************/ 
/**
 * @brief
 *   Send a message on a socket.
 * 
 * @details
 *   The @ref send() function is used to transmit a message on a socket. This function can only be used when the socket is in a connected state. 
 *   If the socket is connection-oriented, the protocol must support implied connect or the socket must be explicitly connected before use. 
 *   No indication of failure to deliver is implicit in a @ref send(). Locally detected errors are indicated by a return value of -1. 
 *   If no message space is available at the socket to hold the message to be transmitted, then @ref send() normally blocks, unless the socket 
 *   is placed in non-blocking I/O mode.
 *
 * @pre Pre-conditions:
 * - @p socket_id must be connected (TCP) or have cached a peer through @ref connect() (UDP).
 *
 * @post Post-conditions:
 * - On success the data is passed to the stack for transmission. Return value indicates bytes accepted.
 * - On failure no bytes are transmitted and @c errno is set.
 * 
 * @param[in] socket_id
 *   The socket ID or file descriptor for the specified socket.
 * 
 * @param[in] buf
 *   Pointer to the buffer containing the message to transmit. Must be non-NULL.
 * 
 * @param[in] buf_len
 *   The length of the message in bytes. Must be > 0 and must not exceed 1460 bytes for plain TCP/UDP
 *   or 1370 bytes for TLS.
 * 
 * @param[in] flags
 *   Controls the transmission of the data. Currently, no flags are supported and this parameter should be set to 0.
 * 
 * @return
 *   Returns the number of bytes sent on success. Returns -1 on error and sets the global variable `errno` to indicate the error.
 *
 * @retval >=0             Number of bytes accepted by the stack for transmission.
 * @retval -1/EBADF       @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK    @p socket_id refers to a non-socket descriptor.
 * @retval -1/ENOTCONN    Socket is not connected.
 * @retval -1/EINVAL      @p buf is NULL or @p buf_len is 0/exceeds the MSS limit.
 * @retval -1/EMSGSIZE    Message is too large to be sent atomically.
 * @retval -1/EPIPE       Peer has closed the connection (TCP).
 * @retval -1/ECONNRESET  Peer reset the connection.
 * @retval -1/ENOBUFS     Not enough buffer space in the stack; try again later.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Enqueues the payload in the firmware send queue; transmission happens asynchronously.
 * - Updates @c errno on failure.
 * 
 * @note 
 *   - The @ref send() system call currently supports only blocking mode.
 *   - The @ref send() system call does not guarantee that the packets are transmitted to the remote node; they are enqueued in the local queue.
 *   - The @ref send() system call does not support any flags.
 *   - The @ref send() system call can send a maximum of 1460 bytes in the case of plain TCP/UDP. For TLS, the maximum buffer length is 1370 bytes.
 *
 * @see sendto(), recv(), connect(), setsockopt()
 *
 * @par Example
 * Send a buffer on a connected TCP socket:
 * @code{.c}
 * const uint8_t tx_buffer[] = "Hello, server!";
 * ssize_t sent = send(sock_fd, tx_buffer, sizeof(tx_buffer) - 1, 0);
 * if (sent < 0) {
 *   printf("send() failed, errno = %d\r\n", errno);
 * }
 * @endcode
 ******************************************************************************/
ssize_t send(int socket_id, const void *buf, size_t buf_len, int flags);

/***************************************************************************/ 
/**
 * @brief
 *   Send a message on a socket.
 * 
 * @details
 *   The @ref sendto() function is used to transmit a message on a socket. This function can be used at any time if the socket is in connectionless mode. 
 *   If the socket is in connection mode, the protocol must support implied connect or the socket must be in a connected state before use. 
 *   The address of the target is specified by `to_addr` with `to_addr_len` indicating its size. If the socket is in a connected state, 
 *   the target address passed to @ref sendto() is ignored. If the message is too long to pass atomically through the protocol, 
 *   the error `EMSGSIZE` is returned, and the message is not transmitted.
 *
 * @pre Pre-conditions:
 * - For UDP the socket must have been created using @ref socket() (optionally bound using @ref bind()).
 * - For a connected socket, @p to_addr is ignored.
 *
 * @post Post-conditions:
 * - On success the data is passed to the stack for transmission.
 * - On failure no bytes are transmitted and @c errno is set.
 * 
 * @param[in] socket_id
 *   The socket ID or file descriptor for the specified socket.
 * 
 * @param[in] buf
 *   Pointer to the buffer containing the message to transmit. Must be non-NULL.
 * 
 * @param[in] buf_len
 *   The length of the message in bytes. Must be > 0 and must not exceed 1460 bytes for plain TCP/UDP
 *   or 1370 bytes for TLS.
 * 
 * @param[in] flags
 *   Controls the transmission of the data. Due to firmware limitations, @ref sendto() does not support any flags and this parameter should be set to 0.
 * 
 * @param[in] to_addr 
 *   Pointer to a `sockaddr` structure containing the address of the target.
 *   Must be non-NULL for connectionless sockets unless the socket is already connected.
 * 
 * @param[in] to_addr_len 
 *   Size of the address structure pointed to by `to_addr`, in bytes.
 *   Must be @c sizeof(struct sockaddr_in) for IPv4 or @c sizeof(struct sockaddr_in6) for IPv6.
 * 
 * @return
 *   The number of octets sent. If an error occurred, a value of -1 is returned.
 *
 * @retval >=0              Number of bytes accepted by the stack for transmission.
 * @retval -1/EBADF        @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK     @p socket_id refers to a non-socket descriptor.
 * @retval -1/EINVAL       Invalid argument (@p buf NULL, @p buf_len out of range, or @p to_addr invalid).
 * @retval -1/EAFNOSUPPORT @p to_addr family does not match the socket family.
 * @retval -1/EMSGSIZE     Message is too large to be sent atomically.
 * @retval -1/ENOBUFS      Not enough buffer space in the stack; try again later.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Enqueues the datagram in the firmware send queue; transmission happens asynchronously.
 * - Updates @c errno on failure.
 * 
 * @note 
 *   - The @ref sendto() system call can only send a maximum of 1460 bytes in the case of plain TCP/UDP. For TLS, the maximum buffer length is 1370 bytes.
 *   - The @ref sendto() system call does not support any flags.
 *
 * @see send(), recvfrom(), bind(), connect()
 *
 * @par Example
 * Send a UDP datagram to a specific remote address/port:
 * @code{.c}
 * const uint8_t      payload[]    = "Hello UDP";
 * struct sockaddr_in target_addr  = { 0 };
 * target_addr.sin_family          = AF_INET;
 * target_addr.sin_port            = 5002;
 * sl_net_inet_addr("192.168.0.100", &target_addr.sin_addr.s_addr);
 *
 * ssize_t sent = sendto(udp_fd,
 *                       payload,
 *                       sizeof(payload) - 1,
 *                       0,
 *                       (const struct sockaddr *)&target_addr,
 *                       sizeof(target_addr));
 * if (sent < 0) {
 *   printf("sendto() failed, errno = %d\r\n", errno);
 * }
 * @endcode
 ******************************************************************************/
ssize_t sendto(int socket_id, const void *buf, size_t buf_len, int flags, const struct sockaddr *to_addr, socklen_t to_addr_len);

/***************************************************************************/ 
/**
 * @brief
 *   Set options on a socket.
 * 
 * @details
 *   The @ref setsockopt() system call manipulates the options associated with a socket. Options may exist at multiple protocol levels; 
 *   they are always present at the uppermost "socket" level. When manipulating socket options, the level at which the option resides 
 *   and the name of the option must be specified. To manipulate options at the socket level, `option_level` is specified as `SOL_SOCKET`. 
 *   To manipulate options at any other level, the protocol number of the appropriate protocol controlling the option is supplied. 
 *   For example, to indicate that an option is to be interpreted by the TCP protocol, `option_level` should be set to the protocol number of TCP. 
 *   The `option_value` and `option_length` arguments are used to access option values for @ref setsockopt().
 *
 * @pre Pre-conditions:
 * - @p socket_id must be a valid descriptor returned by @ref socket().
 * - TLS-related options (@ref TCP_ULP, @ref SL_SO_CERT_INDEX, @ref SL_SO_TLS_SNI, @ref SL_SO_TLS_ALPN,
 *   @ref SL_SO_VERIFY_DOMAIN_NAME) must be set before @ref connect().
 *
 * @post Post-conditions:
 * - On success the option is applied to the socket. Subsequent calls on the socket observe the new behavior.
 * - On failure the option is unchanged and @c errno is set.
 * 
 * @param[in] socket_id
 *   The socket ID or file descriptor for the specified socket.
 * 
 * @param[in] option_level
 *   The level at which the option is being set. One of the values from @ref BSD_SOCKET_OPTION_LEVEL.
 * 
 * @param[in] option_name
 *   The option to be configured. Accepts values from @ref BSD_SOCKET_OPTION_NAME. 
 *   Currently, following options are supported:
 *   - @ref SO_RCVTIMEO
 *   - @ref SO_KEEPALIVE
 *   - @ref SO_MAX_RETRANSMISSION_TIMEOUT_VALUE
 *   - @ref TCP_ULP
 *   - IP_TOS
 *   - @ref SL_SO_CERT_INDEX
 *   - @ref SL_SO_HIGH_PERFORMANCE_SOCKET
 *   - @ref SL_SO_TLS_SNI
 *   - @ref SL_SO_TLS_ALPN
 *   - @ref SL_SO_VERIFY_DOMAIN_NAME
 *  
 * @param[in] option_value
 *   A pointer to the buffer containing the value for the option. Most socket-level options utilize an `int` argument for `option_value`. 
 *   For boolean options, set option_value to a non-zero value to enable the option or to zero to disable it. For non-boolean options, option_value should contain the specific data required for the option.
 *   The value of the parameter.
 *   | option_name                                       | option_value                         |  description                                                                                                               |
 *   |---------------------------------------------------|--------------------------------------|----------------------------------------------------------------------------------------------------------------------------|
 *   | @ref SO_RCVTIMEO                                  | sl_si91x_time_value                  | Socket Receive timeout. sl_si91x_time_value structure is used to represent time in two parts: seconds and microseconds.    |
 *   | @ref SO_KEEPALIVE                                 | uint16_t                             | Set TCP keepalive in seconds                                                                                               |
 *   | @ref SO_MAX_RETRANSMISSION_TIMEOUT_VALUE          | uint8_t                              | Maximum retransmission timeout value that should be in the power of 2 for TCP in seconds                                   |
 *   | @ref TCP_ULP                                      | uint8_t                              | Supported one of the values from @ref BSD_TLS_OPTION_VALUE                                                       |
 *   | IP_TOS                                            | uint16_t                             | Supported one of the values from @ref BSD_SOCKET_TOS_DEFINES (Values 0-7 are deprecated)                                              |
 *   | @ref SL_SO_CERT_INDEX                             | uint8_t                              | Supported values for certificate index range from 0 - 3                                                                    |
 *   | @ref SL_SO_HIGH_PERFORMANCE_SOCKET                | BIT(7)                               | Set high performance socket                                                                                                |
 *   | @ref SL_SO_TLS_SNI                                | sl_si91x_socket_type_length_value_t  | Server name indication for the socket                                                                                      |
 *   | @ref SL_SO_TLS_ALPN                               | sl_si91x_socket_type_length_value_t  | Application layer protocol negotiation for the socket                                                                      |
 *   | @ref SL_SO_VERIFY_DOMAIN_NAME                     | uint8_t *                            | Expected domain name for TLS certificate verification; firmware uses this for server certificate CN/SAN check.             |
 * 
 * @param[in] option_length
 *   The length of the option data, in bytes, pointed to by `option_value`.
 *   Must match the type expected by @p option_name (see the table above).
 * 
 * @return
 *   Returns 0 on successful completion. Returns -1 on error and sets the global variable `errno` to indicate the error.
 *
 * @retval 0                 Success.
 * @retval -1/EBADF         @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK      @p socket_id refers to a non-socket descriptor.
 * @retval -1/EINVAL        @p option_value is NULL, @p option_length mismatch, or the option is not supported for the socket state.
 * @retval -1/ENOPROTOOPT   @p option_name is not recognized at @p option_level.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Changes internal state of the socket (timeouts, TLS parameters, keepalive, etc.).
 * - TLS options alter the next @ref connect() handshake.
 * 
 * @note 
 *   The options `SL_SO_CERT_INDEX`, `SL_SO_HIGH_PERFORMANCE_SOCKET`, `SL_SO_TLS_SNI`, `SL_SO_TLS_ALPN`, and `SL_SO_VERIFY_DOMAIN_NAME` are Silicon Labs specific options.
 * 	 This function is used before the socket is connected.
 *
 * @see getsockopt(), connect(), socket()
 *
 * @par Example
 * 1. Set a 5-second receive timeout on a socket:
 * @code{.c}
 * struct sl_si91x_time_value rx_timeout = { .tv_sec = 5, .tv_usec = 0 };
 * if (setsockopt(sock_fd,
 *                SOL_SOCKET,
 *                SO_RCVTIMEO,
 *                &rx_timeout,
 *                sizeof(rx_timeout)) < 0) {
 *   printf("setsockopt(SO_RCVTIMEO) failed, errno = %d\r\n", errno);
 * }
 * @endcode
 *
 * 2. Enable TCP keepalive (30 seconds):
 * @code{.c}
 * uint16_t keepalive_sec = 30;
 * if (setsockopt(sock_fd,
 *                SOL_SOCKET,
 *                SO_KEEPALIVE,
 *                &keepalive_sec,
 *                sizeof(keepalive_sec)) < 0) {
 *   printf("setsockopt(SO_KEEPALIVE) failed, errno = %d\r\n", errno);
 * }
 * @endcode
 *
 * 3. Enable TLS 1.2 on the socket prior to connect():
 * @code{.c}
 * if (setsockopt(sock_fd,
 *                SOL_TCP,
 *                TCP_ULP,
 *                TLS_1_2,
 *                sizeof(TLS_1_2)) < 0) {
 *   printf("setsockopt(TCP_ULP) failed, errno = %d\r\n", errno);
 * }
 * @endcode
 *
 * 4. Select the TLS certificate stored at index 1:
 * @code{.c}
 * uint8_t cert_index = 1;
 * if (setsockopt(sock_fd,
 *                SOL_SOCKET,
 *                SL_SO_CERT_INDEX,
 *                &cert_index,
 *                sizeof(cert_index)) < 0) {
 *   printf("setsockopt(SL_SO_CERT_INDEX) failed, errno = %d\r\n", errno);
 * }
 * @endcode
 ******************************************************************************/
int setsockopt(int socket_id, int option_level, int option_name, const void *option_value, socklen_t option_length);

/***************************************************************************/ 
/**
 * @brief
 *   Get options on a socket.
 * 
 * @details
 *   The @ref getsockopt() system call retrieves the options associated with a socket. Options may exist at multiple protocol levels; 
 *   they are always present at the uppermost "socket" level. When retrieving socket options, the level at which the option resides 
 *   and the name of the option must be specified. To retrieve options at the socket level, `option_level` is specified as `SOL_SOCKET`. 
 *   To retrieve options at any other level, the protocol number of the appropriate protocol controlling the option is supplied. 
 *   For example, to indicate that an option is to be interpreted by the TCP protocol, `option_level` should be set to the protocol number of TCP. 
 *   The `option_value` and `option_length` arguments identify a buffer in which the value for the requested options are to be returned.
 *
 * @pre Pre-conditions:
 * - @p socket_id must be a valid descriptor returned by @ref socket().
 *
 * @post Post-conditions:
 * - On success @p option_value is populated and @p option_length is updated with the number of bytes written.
 * 
 * @param[in] socket_id
 *   The socket ID or file descriptor for the specified socket.
 * 
 * @param[in] option_level
 *   The level at which the option is defined. One of the values from @ref BSD_SOCKET_OPTION_LEVEL.
 * 
 * @param[in] option_name
 *   The name of the option to retrieve. One of the values from @ref BSD_SOCKET_OPTION_NAME. The `option_name` argument and any specified options 
 *   are passed uninterpreted to the appropriate protocol module for interpretation.
 * 
 * @param[out] option_value
 *   Pointer to a buffer where the retrieved option value will be stored. Must be non-NULL.
 * 
 * @param[in, out] option_length
 *   A value-result argument of type @ref socklen_t. Initially, it should contain the size of the buffer pointed to by `option_value`. 
 *   On return, it will be modified to indicate the actual size of the value returned. If no option value is to be supplied or returned, 
 *   `option_value` may be NULL.
 *   Must be non-NULL; initial value must be >= the size required by @p option_name.
 * 
 * @return
 *   Returns 0 on successful completion. Returns -1 on error and sets the global variable `errno` to indicate the error.
 *
 * @retval 0                 Success.
 * @retval -1/EBADF         @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK      @p socket_id refers to a non-socket descriptor.
 * @retval -1/EINVAL        @p option_value / @p option_length is NULL or insufficient.
 * @retval -1/ENOPROTOOPT   @p option_name is not recognized at @p option_level.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor; serialize concurrent access.
 *
 * @note Side effects:
 * - Updates @c errno on failure. Does not modify socket behavior.
 * 
 * @note 
 *   The following options are currently supported:
 *   - `SO_RCVTIMEO`: Receive timeout.
 *   - `SO_KEEPALIVE`: Keep connections alive.
 *   - `TCP_ULP`: TCP upper layer protocol.
 *   - `SO_MAX_RETRANSMISSION_TIMEOUT_VALUE`: Maximum retransmission timeout value.
 *   - `IP_TOS`: Type of service.
 *
 * @see setsockopt(), socket()
 *
 * @par Example
 * Read back the current receive timeout of a socket:
 * @code{.c}
 * struct sl_si91x_time_value rx_timeout = { 0 };
 * socklen_t                  opt_len    = sizeof(rx_timeout);
 *
 * if (getsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &rx_timeout, &opt_len) < 0) {
 *   printf("getsockopt(SO_RCVTIMEO) failed, errno = %d\r\n", errno);
 * } else {
 *   printf("Current RX timeout: %ld sec, %ld usec\r\n",
 *          (long)rx_timeout.tv_sec,
 *          (long)rx_timeout.tv_usec);
 * }
 * @endcode
 ******************************************************************************/
int getsockopt(int socket_id, int option_level, int option_name, void *option_value, socklen_t *option_length);

/***************************************************************************/ 
/**
 * @brief
 *   Get the name of the connected peer socket.
 * 
 * @details
 *   The @ref getpeername() system call returns the address of the peer connected to the socket specified by `socket_id`. 
 *   The address is returned in the buffer pointed to by `name`, and the actual size of the address is returned in the variable 
 *   pointed to by `name_len`. If the buffer provided is too small, the address is truncated.
 *
 * @pre Pre-conditions:
 * - @p socket_id must be a connected socket (e.g. returned by @ref accept() or post-@ref connect()).
 *
 * @post Post-conditions:
 * - On success @p name is populated with the peer's address and @p name_len is updated with the actual size.
 * 
 * @param[in] socket_id
 *   The socket ID or file descriptor for the specified socket.
 * 
 * @param[out] name 
 *   Pointer to a `sockaddr` structure that will be filled with the address of the peer socket. Must be non-NULL.
 * 
 * @param[in, out] name_len
 *   A value-result argument of type @ref socklen_t. Initially, it should contain the size of the buffer pointed to by `name`. 
 *   On return, it will be modified to indicate the actual size of the address returned (in bytes). Must be non-NULL.
 * 
 * @return
 *   Returns 0 on success. Returns -1 on error and sets the global variable `errno` to indicate the error.
 *
 * @retval 0                 Success.
 * @retval -1/EBADF         @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK      @p socket_id refers to a non-socket descriptor.
 * @retval -1/ENOTCONN      Socket is not connected.
 * @retval -1/EINVAL        @p name or @p name_len is NULL / buffer too small.
 *
 * @note Thread safety:
 * - Safe to call concurrently with I/O on the same descriptor; read-only access to socket state.
 *
 * @note Side effects:
 * - Updates @c errno on failure. Does not modify socket behavior.
 *
 * @see getsockname(), accept(), connect()
 *
 * @par Example
 * Fetch the remote peer's address for a connected TCP socket:
 * @code{.c}
 * struct sockaddr_in peer_addr = { 0 };
 * socklen_t          peer_len  = sizeof(peer_addr);
 *
 * if (getpeername(client_fd, (struct sockaddr *)&peer_addr, &peer_len) < 0) {
 *   printf("getpeername() failed, errno = %d\r\n", errno);
 * } else {
 *   printf("Peer port: %u\r\n", ntohs(peer_addr.sin_port));
 * }
 * @endcode
 ******************************************************************************/
int getpeername(int socket_id, struct sockaddr *name, socklen_t *name_len);

/***************************************************************************/ 
/**
 * @brief
 *   Get the current address assigned to a socket.
 * 
 * @details
 *   The @ref getsockname() system call returns the current address to which the socket `socket_id` is bound. 
 *   The address is returned in the buffer pointed to by `name`, and the actual size of the address is returned in the variable 
 *   pointed to by `name_len`. If the buffer provided is too small, the address is truncated.
 *
 * @pre Pre-conditions:
 * - @p socket_id must be a descriptor returned by @ref socket(). The socket should be bound (explicitly using @ref bind() or implicitly).
 *
 * @post Post-conditions:
 * - On success @p name is populated with the local address/port and @p name_len is updated with the actual size.
 * 
 * @param[in] socket_id
 *   The socket ID or file descriptor for the specified socket.
 * 
 * @param[out] name 
 *   Pointer to a `sockaddr` structure that will be filled with the address of the socket. Must be non-NULL.
 * 
 * @param[in, out] name_len
 *   A value-result argument of type @ref socklen_t. Initially, it should contain the size of the buffer pointed to by `name`. 
 *   On return, it will be modified to indicate the actual size of the address returned (in bytes). Must be non-NULL.
 * 
 * @return
 *   Returns 0 on success. Returns -1 on error and sets the global variable `errno` to indicate the error.
 *
 * @retval 0                 Success.
 * @retval -1/EBADF         @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK      @p socket_id refers to a non-socket descriptor.
 * @retval -1/EINVAL        @p name or @p name_len is NULL / buffer too small.
 *
 * @note Thread safety:
 * - Safe to call concurrently with I/O on the same descriptor; read-only access to socket state.
 *
 * @note Side effects:
 * - Updates @c errno on failure. Does not modify socket behavior.
 *
 * @see getpeername(), bind(), socket()
 *
 * @par Example
 * Retrieve the local address/port bound to a socket (useful when port was 0):
 * @code{.c}
 * struct sockaddr_in local_addr = { 0 };
 * socklen_t          local_len  = sizeof(local_addr);
 *
 * if (getsockname(sock_fd, (struct sockaddr *)&local_addr, &local_len) < 0) {
 *   printf("getsockname() failed, errno = %d\r\n", errno);
 * } else {
 *   printf("Bound to local port %u\r\n", ntohs(local_addr.sin_port));
 * }
 * @endcode
 ******************************************************************************/
int getsockname(int socket_id, struct sockaddr *name, socklen_t *name_len);

/***************************************************************************/ 
/**
 * @brief 
 *   Close a socket.
 * 
 * @details
 *   The @ref close() function closes a socket identified by `socket_id`, so that it no longer refers to any file and can be reused. 
 *   This function releases the resources associated with the socket and makes the socket descriptor available for reuse.
 *
 * @pre Pre-conditions:
 * - @p socket_id must be a valid descriptor returned by @ref socket() or @ref accept().
 *
 * @post Post-conditions:
 * - The descriptor is released back to the pool and must not be used for further operations.
 * - Any pending I/O on the descriptor is aborted; queued unsent data may be discarded.
 * 
 * @param[in] socket_id
 *   The socket ID or file descriptor for the specified socket.
 * 
 * @return
 *   Returns 0 on success. Returns -1 on error and sets the global variable `errno` to indicate the error.
 *
 * @retval 0                 Success.
 * @retval -1/EBADF         @p socket_id is not a valid descriptor.
 * @retval -1/ENOTSOCK      @p socket_id refers to a non-socket descriptor.
 * @retval -1/EIO           I/O error while finalising the close sequence.
 *
 * @note Thread safety:
 * - Not thread-safe on the same descriptor. The caller must ensure that no other thread is operating on @p socket_id when @ref close() is invoked.
 *
 * @note Side effects:
 * - Releases stack resources (TCP state, TLS context, internal buffers).
 * - Closing a server socket or the first client socket may close all associated sockets (see note).
 * 
 * @note
 *   - Calling @ref close() on a server socket or the first client socket will close all associated sockets.
 *   - If @ref close() is called on a non-existing socket, the error `EBADF` (Bad file descriptor) will be set in `errno`.
 *
 * @see socket(), accept()
 *
 * @par Example
 * Always close a socket in both success and error paths to avoid leaks:
 * @code{.c}
 * if (close(sock_fd) < 0) {
 *   printf("close() failed, errno = %d\r\n", errno);
 * }
 * sock_fd = -1;
 * @endcode
 ******************************************************************************/
int close(int socket_id);

/** @} */
/* __END_DECLS */

#ifdef __cplusplus
}	
#endif

#endif /* !COMPONENTS_BSD_SOCKET_INC_SOCKET_H_ */
