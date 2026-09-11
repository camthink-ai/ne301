/***************************************************************************/ /**
* @file sli_si91x_socket_defs.h
* @brief Minimal socket type definitions for dual stack mode without LWIP sockets
*
* @internal
* This is an SDK-internal compatibility shim. It is included from
* sl_si91x_socket_types.h whenever SLI_SI91X_NETWORK_DUAL_STACK is enabled
* and LWIP_SOCKET=0, to provide the BSD socket types that the offload socket
* layer references. Application code MUST NOT include this header directly
* and MUST NOT rely on the SLI_SI91X_* helper macros it exposes.
*******************************************************************************
* # License
* <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
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

#ifndef SLI_SI91X_SOCKET_DEFS_H
#define SLI_SI91X_SOCKET_DEFS_H

/*
 * This header is included from sl_si91x_socket_types.h ONLY when
 *   SLI_SI91X_NETWORK_DUAL_STACK is defined AND LWIP_SOCKET == 0.
 * In that configuration LWIP's lwip/sockets.h is a no-op (its body is gated
 * on `#if LWIP_SOCKET`), so the offload BSD-socket layer must get its
 * sockaddr / sockaddr_in / sockaddr_in6 / fd_set / SOCK_*  from somewhere.
 * That "somewhere" is this file.
 */

#include <stdint.h>
#include <stddef.h>
#include "lwipopts.h"
#include "sl_si91x_protocol_types.h"

/* When LWIP_IPV4=0, lwip/ip4_addr.h does not define IPADDR_ANY (and friends),
 * but lwip/inet.h unconditionally defines INADDR_ANY as IPADDR_ANY. Pre-define
 * the IPADDR_* family here so any later use of INADDR_* through lwip/inet.h
 * resolves cleanly. */
#if !LWIP_IPV4
#ifndef IPADDR_ANY
#define IPADDR_ANY ((uint32_t)0x00000000UL)
#endif
#ifndef IPADDR_BROADCAST
#define IPADDR_BROADCAST ((uint32_t)0xffffffffUL)
#endif
#ifndef IPADDR_NONE
#define IPADDR_NONE ((uint32_t)0xffffffffUL)
#endif
#ifndef IPADDR_LOOPBACK
#define IPADDR_LOOPBACK ((uint32_t)0x7f000001UL)
#endif
#endif /* !LWIP_IPV4 */

#include "lwip/inet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Socket-layer constants                                                    */
/* ------------------------------------------------------------------------- */

#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif
#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif

#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef AF_INET6
#define AF_INET6 10
#endif

#ifndef IPPROTO_IP
#define IPPROTO_IP 0
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif

/* ------------------------------------------------------------------------- */
/* socklen_t / sa_family_t / in_port_t                                       */
/*                                                                           */
/* These are typedefs, so a plain `#ifndef name` guard does not work. Use    */
/* sentinel macros, matching the convention used by lwip/sockets.h.          */
/* ------------------------------------------------------------------------- */

#if !defined(SOCKLEN_T_DEFINED) && !defined(_SOCKLEN_T_DECLARED)
#define SOCKLEN_T_DEFINED
#define _SOCKLEN_T_DECLARED
typedef uint32_t socklen_t;
#endif

#ifndef SA_FAMILY_T_DEFINED
#define SA_FAMILY_T_DEFINED
typedef uint8_t sa_family_t;
#endif

#ifndef IN_PORT_T_DEFINED
#define IN_PORT_T_DEFINED
typedef uint16_t in_port_t;
#endif

/* ------------------------------------------------------------------------- */
/* Socket address structures                                                 */
/*                                                                           */
/* This header is only reached with LWIP_SOCKET=0, so lwip/sockets.h has not */
/* defined these. struct in_addr / struct in6_addr come from lwip/inet.h     */
/* (included above) regardless of LWIP_IPV4 / LWIP_IPV6.                     */
/* ------------------------------------------------------------------------- */

#ifndef SLI_SI91X_SOCKADDR_DEFINED
#define SLI_SI91X_SOCKADDR_DEFINED
struct sockaddr {
  uint8_t sa_len;
  sa_family_t sa_family;
  char sa_data[14];
};

struct sockaddr_in {
  uint8_t sin_len;
  sa_family_t sin_family;
  in_port_t sin_port;
  struct in_addr sin_addr;
  char sin_zero[8];
};

struct sockaddr_in6 {
  uint8_t sin6_len;
  sa_family_t sin6_family;
  in_port_t sin6_port;
  uint32_t sin6_flowinfo;
  struct in6_addr sin6_addr;
  uint32_t sin6_scope_id;
};
#endif /* SLI_SI91X_SOCKADDR_DEFINED */

#ifndef FD_SETSIZE
#define FD_SETSIZE SLI_NUMBER_OF_SOCKETS
#endif

#if FD_SETSIZE < SLI_NUMBER_OF_SOCKETS
#error "FD_SETSIZE is smaller than SLI_NUMBER_OF_SOCKETS; widen FD_SETSIZE in lwipopts.h or via -DFD_SETSIZE=<n>."
#endif

#ifndef SLI_SI91X_FD_SET_DEFINED
#define SLI_SI91X_FD_SET_DEFINED

/* If toolchain headers already provided select() types/macros (common in C++
 * builds via <sys/select.h>), do not redefine fd_set. */
#if !defined(_SYS_SELECT_H) && !defined(_SELECT_H) && !defined(__SYS_SELECT_H__) && !defined(FD_SET) \
  && !defined(FD_ZERO) && !defined(FD_CLR) && !defined(FD_ISSET)

typedef uint32_t sli_si91x_fd_mask_t;
#define SLI_SI91X_NFDBITS          ((int)sizeof(sli_si91x_fd_mask_t) * 8)
#define SLI_SI91X_FD_HOWMANY(x, y) (((x) + ((y)-1)) / (y))

typedef struct fd_set {
  sli_si91x_fd_mask_t sli_fds_bits[SLI_SI91X_FD_HOWMANY(FD_SETSIZE, SLI_SI91X_NFDBITS)];
} fd_set;

#define SLI_SI91X_FDSET_MASK(n) ((sli_si91x_fd_mask_t)1 << ((n) % SLI_SI91X_NFDBITS))

#ifndef FD_CLR
#define FD_CLR(n, p) ((p)->sli_fds_bits[(n) / SLI_SI91X_NFDBITS] &= ~SLI_SI91X_FDSET_MASK(n))
#endif
#ifndef FD_ISSET
#define FD_ISSET(n, p) (((p)->sli_fds_bits[(n) / SLI_SI91X_NFDBITS] & SLI_SI91X_FDSET_MASK(n)) != 0)
#endif
#ifndef FD_SET
#define FD_SET(n, p) ((p)->sli_fds_bits[(n) / SLI_SI91X_NFDBITS] |= SLI_SI91X_FDSET_MASK(n))
#endif
#ifndef FD_ZERO
#define FD_ZERO(p)                                                           \
  do {                                                                       \
    fd_set *sli_fdz_p = (p);                                                 \
    size_t sli_fdz_n  = SLI_SI91X_FD_HOWMANY(FD_SETSIZE, SLI_SI91X_NFDBITS); \
    while (sli_fdz_n > 0)                                                    \
      sli_fdz_p->sli_fds_bits[--sli_fdz_n] = 0;                              \
  } while (0)
#endif

#endif /* local fd_set fallback */
#endif /* SLI_SI91X_FD_SET_DEFINED */

#ifdef __cplusplus
}
#endif

#endif /* SLI_SI91X_SOCKET_DEFS_H */
