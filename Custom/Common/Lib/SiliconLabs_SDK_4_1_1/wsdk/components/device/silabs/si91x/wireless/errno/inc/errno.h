/***************************************************************************/ /**
 * @file errno.h
 * @brief errno standard header
 *******************************************************************************
 * # License
 * <b>Copyright 2022 Silicon Laboratories Inc. www.silabs.com</b>
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
#ifndef SL_SI91X_ERRNO_H
#define SL_SI91X_ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif

// #include_next pulls in the next standard library's <errno.h> in the search path, providing the full libc's errno symbol set before the SDK overrides the errno symbols.
#include_next <errno.h>

// Overrides the standard library's errno symbol with the one provided by the SDK.
#undef errno

/*******************************************************************************
 * @addtogroup errno Errno
 * @details Errno contains the definition for errno and the defines for
 * the errno values.
 * @{
 ******************************************************************************/

/***************************************************************************/ /**
 * Macro for errno usage.
 * Can be used to retrieve errno value and to assign errno value since function
 * returns address of variable used to store errno value.
 ******************************************************************************/
#define errno (*sl_si91x_errno())

/***************************************************************************/ /**
 * Errno function used with macro errno.
 *
 * @return  Address of global variable used to store errno value
 ******************************************************************************/
int *sl_si91x_errno(void);

// -----------------------------------------------------------------------------
// Errno Define Values

#undef EPERM
#define EPERM 1 /* Operation not permitted */
#undef ENOENT
#define ENOENT 2 /* No such file or directory */
#undef ESRCH
#define ESRCH 3 /* No such process */
#undef EINTR
#define EINTR 4 /* Interrupted system call */
#undef EIO
#define EIO 5 /* Input/output error */
#undef ENXIO
#define ENXIO 6 /* Device not configured */
#undef E2BIG
#define E2BIG 7 /* Argument list too long */
#undef ENOEXEC
#define ENOEXEC 8 /* Exec format error */
#undef EBADF
#define EBADF 9 /* Bad file descriptor */
#undef ECHILD
#define ECHILD 10 /* No child processes */
#undef EDEADLK
#define EDEADLK 11 /* Resource deadlock avoided */
#undef ENOMEM
/* 11 was EAGAIN */
#define ENOMEM 12 /* Cannot allocate memory */
#undef EACCES
#define EACCES 13 /* Permission denied */
#undef EFAULT
#define EFAULT 14 /* Bad address */
#if __BSD_VISIBLE
#undef ENOTBLK
#define ENOTBLK 15 /* Block device required */
#endif             /* __BSD_VISIBLE */
#undef EBUSY
#define EBUSY 16 /* Device busy */
#undef EEXIST
#define EEXIST 17 /* File exists */
#undef EXDEV
#define EXDEV 18 /* Cross-device link */
#undef ENODEV
#define ENODEV 19 /* Operation not supported by device */
#undef ENOTDIR
#define ENOTDIR 20 /* Not a directory */
#undef EISDIR
#define EISDIR 21 /* Is a directory */
#undef EINVAL
#define EINVAL 22 /* Invalid argument */
#undef ENFILE
#define ENFILE 23 /* Too many open files in system */
#undef EMFILE
#define EMFILE 24 /* Too many open files */
#undef ENOTTY
#define ENOTTY 25 /* Inappropriate ioctl for device */
#undef ETXTBSY
#define ETXTBSY 26 /* Text file busy */
#undef EFBIG
#define EFBIG 27 /* File too large */
#undef ENOSPC
#define ENOSPC 28 /* No space left on device */
#undef ESPIPE
#define ESPIPE 29 /* Illegal seek */
#undef EROFS
#define EROFS 30 /* Read-only file system */
#undef EMLINK
#define EMLINK 31 /* Too many links */
#undef EPIPE
#define EPIPE 32 /* Broken pipe */

/* math software */
#undef EDOM
#define EDOM 33 /* Numerical argument out of domain */
#undef ERANGE
#define ERANGE 34 /* Result too large */

/* non-blocking and interrupt i/o */
#undef EAGAIN
#define EAGAIN 35 /* Resource temporarily unavailable */
#undef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN /* Operation would block */
#undef EINPROGRESS
#define EINPROGRESS 36 /* Operation now in progress */
#undef EALREADY
#define EALREADY 37 /* Operation already in progress */

/* ipc/network software -- argument errors */
#undef ENOTSOCK
#define ENOTSOCK 38 /* Socket operation on non-socket */
#undef EDESTADDRREQ
#define EDESTADDRREQ 39 /* Destination address required */
#undef EMSGSIZE
#define EMSGSIZE 40 /* Message too long */
#undef EPROTOTYPE
#define EPROTOTYPE 41 /* Protocol wrong type for socket */
#undef ENOPROTOOPT
#define ENOPROTOOPT 42 /* Protocol not available */
#undef EPROTONOSUPPORT
#define EPROTONOSUPPORT 43 /* Protocol not supported */
#if __BSD_VISIBLE
#undef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT 44 /* Socket type not supported */
#endif                     /* __BSD_VISIBLE */
#undef EOPNOTSUPP
#define EOPNOTSUPP 45 /* Operation not supported */
#if __BSD_VISIBLE
#undef EPFNOSUPPORT
#define EPFNOSUPPORT 46 /* Protocol family not supported */
#endif                  /* __BSD_VISIBLE */
#undef EAFNOSUPPORT
#define EAFNOSUPPORT 47 /* Address family not supported by protocol family */
#undef EADDRINUSE
#define EADDRINUSE 48 /* Address already in use */
#undef EADDRNOTAVAIL
#define EADDRNOTAVAIL 49 /* Can't assign requested address */

/* ipc/network software -- operational errors */
#undef ENETDOWN
#define ENETDOWN 50 /* Network is down */
#undef ENETUNREACH
#define ENETUNREACH 51 /* Network is unreachable */
#undef ENETRESET
#define ENETRESET 52 /* Network dropped connection on reset */
#undef ECONNABORTED
#define ECONNABORTED 53 /* Software caused connection abort */
#undef ECONNRESET
#define ECONNRESET 54 /* Connection reset by peer */
#undef ENOBUFS
#define ENOBUFS 55 /* No buffer space available */
#undef EISCONN
#define EISCONN 56 /* Socket is already connected */
#undef ENOTCONN
#define ENOTCONN 57 /* Socket is not connected */
#if __BSD_VISIBLE
#undef ESHUTDOWN
#define ESHUTDOWN 58 /* Can't send after socket shutdown */
#undef ETOOMANYREFS
#define ETOOMANYREFS 59 /* Too many references: can't splice */
#endif                  /* __BSD_VISIBLE */
#undef ETIMEDOUT
#define ETIMEDOUT 60 /* Operation timed out */
#undef ECONNREFUSED
#define ECONNREFUSED 61 /* Connection refused */

#undef ELOOP
#define ELOOP 62 /* Too many levels of symbolic links */
#undef ENAMETOOLONG
#define ENAMETOOLONG 63 /* File name too long */

/* should be rearranged */
#if __BSD_VISIBLE
#undef EHOSTDOWN
#define EHOSTDOWN 64 /* Host is down */
#endif               /* __BSD_VISIBLE */
#undef EHOSTUNREACH
#define EHOSTUNREACH 65 /* No route to host */
#undef ENOTEMPTY
#define ENOTEMPTY 66 /* Directory not empty */

/* quotas & mush */
#if __BSD_VISIBLE
#undef EPROCLIM
#define EPROCLIM 67 /* Too many processes */
#undef EUSERS
#define EUSERS 68 /* Too many users */
#endif            /* __BSD_VISIBLE */
#undef EDQUOT
#define EDQUOT 69 /* Disk quota exceeded */

/* Network File System */
#undef ESTALE
#define ESTALE 70 /* Stale NFS file handle */
#if __BSD_VISIBLE
#undef EREMOTE
#define EREMOTE 71 /* Too many levels of remote in path */
#undef EBADRPC
#define EBADRPC 72 /* RPC struct is bad */
#undef ERPCMISMATCH
#define ERPCMISMATCH 73 /* RPC version wrong */
#undef EPROGUNAVAIL
#define EPROGUNAVAIL 74 /* RPC program not available */
#undef EPROGMISMATCH
#define EPROGMISMATCH 75 /* Program version wrong */
#undef EPROCUNAVAIL
#define EPROCUNAVAIL 76 /* Bad procedure for program */
#endif                  /* __BSD_VISIBLE */

#undef ENOLCK
#define ENOLCK 77 /* No locks available */
#undef ENOSYS
#define ENOSYS 78 /* Function not implemented */

#if __BSD_VISIBLE
#undef EFTYPE
#define EFTYPE 79 /* Inappropriate file type or format */
#undef EAUTH
#define EAUTH 80 /* Authentication error */
#undef ENEEDAUTH
#define ENEEDAUTH 81 /* Need authenticator */
#undef EIPSEC
#define EIPSEC 82 /* IPsec processing failure */
#undef ENOATTR
#define ENOATTR 83 /* Attribute not found */
#endif             /* __BSD_VISIBLE */
#undef EILSEQ
#define EILSEQ 84 /* Illegal byte sequence */
#if __BSD_VISIBLE
#undef ENOMEDIUM
#define ENOMEDIUM 85 /* No medium found */
#undef EMEDIUMTYPE
#define EMEDIUMTYPE 86 /* Wrong medium type */
#endif                 /* __BSD_VISIBLE */
#undef EOVERFLOW
#define EOVERFLOW 87 /* Value too large to be stored in data type */
#undef ECANCELED
#define ECANCELED 88 /* Operation canceled */
#undef EIDRM
#define EIDRM 89 /* Identifier removed */
#undef ENOMSG
#define ENOMSG 90 /* No message of desired type */
#undef ENOTSUP
#define ENOTSUP 91 /* Not supported */
#undef EBADMSG
#define EBADMSG 92 /* Bad message */
#undef ENOTRECOVERABLE
#define ENOTRECOVERABLE 93 /* State not recoverable */
#undef EOWNERDEAD
#define EOWNERDEAD 94 /* Previous owner died */
#undef EPROTO
#define EPROTO 95 /* Protocol error */
#if __BSD_VISIBLE
#undef ELAST
#define ELAST 95 /* Must be equal largest errno */
#endif           /* __BSD_VISIBLE */

#ifdef _KERNEL
/* pseudo-errors returned inside kernel to modify return to process */
#undef ERESTART
#define ERESTART -1 /* restart syscall */
#undef EJUSTRETURN
#define EJUSTRETURN -2 /* don't modify regs, just return */
#endif                 /* !_KERNEL */

/** @} (end addtogroup errno) */
#ifdef __cplusplus
}
#endif

#endif // SL_SI91X_ERRNO_H
