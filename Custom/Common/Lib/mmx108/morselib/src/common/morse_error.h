/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include <limits.h>
#include <stdint.h>



#define MORSE_MISC_ERROR_SECTION -(0x8000)
#define MORSE_HOST_ERROR_SECTION -(0x10000)
#define MORSE_MAC_ERROR_SECTION  -(0x18000)
#define MORSE_PHY_ERROR_SECTION  -(0x20000)


typedef enum
{

    MORSE_SUCCESS = 0,
    MORSE_EPERM = -1,
    MORSE_ENOENT = -2,
    MORSE_ESRCH = -3,
    MORSE_EINTR = -4,
    MORSE_EIO = -5,
    MORSE_ENXIO = -6,
    MORSE_E2BIG = -7,
    MORSE_ENOEXEC = -8,
    MORSE_EBADF = -9,
    MORSE_ECHILD = -10,
    MORSE_EAGAIN = -11,
    MORSE_ENOMEM = -12,
    MORSE_EACCES = -13,
    MORSE_EFAULT = -14,
    MORSE_ENOTBLK = -15,
    MORSE_EBUSY = -16,
    MORSE_EEXIST = -17,
    MORSE_EXDEV = -18,
    MORSE_ENODEV = -19,
    MORSE_ENOTDIR = -20,
    MORSE_EISDIR = -21,
    MORSE_EINVAL = -22,
    MORSE_ENFILE = -23,
    MORSE_EMFILE = -24,
    MORSE_ENOTTY = -25,
    MORSE_ETXTBSY = -26,
    MORSE_EFBIG = -27,
    MORSE_ENOSPC = -28,
    MORSE_ESPIPE = -29,
    MORSE_EROFS = -30,
    MORSE_EMLINK = -31,
    MORSE_EPIPE = -32,
    MORSE_EDOM = -33,
    MORSE_ERANGE = -34,
    MORSE_EDEADLK = -35,
    MORSE_ENAMETOOLONG = -36,
    MORSE_ENOLCK = -37,
    MORSE_ENOSYS = -38,
    MORSE_ENOTEMPTY = -39,
    MORSE_ELOOP = -40,
    MORSE_EWOULDBLOCK = -41,
    MORSE_ENOMSG = -42,
    MORSE_EIDRM = -43,
    MORSE_ECHRNG = -44,
    MORSE_EL2NSYNC = -45,
    MORSE_EL3HLT = -46,
    MORSE_EL3RST = -47,
    MORSE_ELNRNG = -48,
    MORSE_EUNATCH = -49,
    MORSE_ENOCSI = -50,
    MORSE_EL2HLT = -51,
    MORSE_EBADE = -52,
    MORSE_EBADR = -53,
    MORSE_EXFULL = -54,
    MORSE_ENOANO = -55,
    MORSE_EBADRQC = -56,
    MORSE_EBADSLT = -57,

    MORSE_EDEADLOCK = -58,

    MORSE_EBFONT = -59,
    MORSE_ENOSTR = -60,
    MORSE_ENODATA = -61,
    MORSE_ETIME = -62,
    MORSE_ENOSR = -63,
    MORSE_ENONET = -64,
    MORSE_ENOPKG = -65,
    MORSE_EREMOTE = -66,
    MORSE_ENOLINK = -67,
    MORSE_EADV = -68,
    MORSE_ESRMNT = -69,
    MORSE_ECOMM = -70,
    MORSE_EPROTO = -71,
    MORSE_EMULTIHOP = -72,
    MORSE_EDOTDOT = -73,
    MORSE_EBADMSG = -74,
    MORSE_EOVERFLOW = -75,
    MORSE_ENOTUNIQ = -76,
    MORSE_EBADFD = -77,
    MORSE_EREMCHG = -78,
    MORSE_ELIBACC = -79,
    MORSE_ELIBBAD = -80,
    MORSE_ELIBSCN = -81,
    MORSE_ELIBMAX = -82,
    MORSE_ELIBEXEC = -83,
    MORSE_EILSEQ = -84,
    MORSE_ERESTART = -85,
    MORSE_ESTRPIPE = -86,
    MORSE_EUSERS = -87,
    MORSE_ENOTSOCK = -88,
    MORSE_EDESTADDRREQ = -89,
    MORSE_EMSGSIZE = -90,
    MORSE_EPROTOTYPE = -91,
    MORSE_ENOPROTOOPT = -92,
    MORSE_EPROTONOSUPPORT = -93,
    MORSE_ESOCKTNOSUPPORT = -94,
    MORSE_EOPNOTSUPP = -95,
    MORSE_EPFNOSUPPORT = -96,
    MORSE_EAFNOSUPPORT = -97,
    MORSE_EADDRINUSE = -98,
    MORSE_EADDRNOTAVAIL = -99,
    MORSE_ENETDOWN = -100,
    MORSE_ENETUNREACH = -101,
    MORSE_ENETRESET = -102,
    MORSE_ECONNABORTED = -103,
    MORSE_ECONNRESET = -104,
    MORSE_ENOBUFS = -105,
    MORSE_EISCONN = -106,
    MORSE_ENOTCONN = -107,
    MORSE_ESHUTDOWN = -108,
    MORSE_ETOOMANYREFS = -109,
    MORSE_ETIMEDOUT = -110,
    MORSE_ECONNREFUSED = -111,
    MORSE_EHOSTDOWN = -112,
    MORSE_EHOSTUNREACH = -113,
    MORSE_EALREADY = -114,
    MORSE_EINPROGRESS = -115,
    MORSE_ESTALE = -116,
    MORSE_EUCLEAN = -117,
    MORSE_ENOTNAM = -118,
    MORSE_ENAVAIL = -119,
    MORSE_EISNAM = -120,
    MORSE_EREMOTEIO = -121,
    MORSE_EDQUOT = -122,

    MORSE_ENOMEDIUM = -123,
    MORSE_EMEDIUMTYPE = -124,



    MORSE_FAILED = MORSE_MISC_ERROR_SECTION,
    MORSE_NULL_POINTER,
    MORSE_INVALID_ARGUMENT,
    MORSE_INVALID_CRC,
    MORSE_INVALID_MAGIC_NUMBER,
    MORSE_UNSUPPORTED_VERSION,
    MORSE_UNSUPPORTED_MAC_CIPHER,


    MORSE_UNSUPPORTED_MCS = MORSE_PHY_ERROR_SECTION,
    MORSE_UNSUPPORTED_MCS_FOR_CHANNEL,

    MORSE_PSDU_OVERSIZE,

    MORSE_PSDU_INIT_ERROR,
    MORSE_UNSUPPORTED_FFT_SIZE,
    MORSE_UNSUPPORTED_MODULATION,
    MORSE_LTF_NO_MATCH,

    MORSE_UNSUPPORTED_CHANNEL_BANDWIDTH,
    MORSE_1MHZ_PRIMARY_CHAN_INDEX_ERROR,
    MORSE_SERVICE_FIELD_ERROR,

    MORSE_PROCESSING_ONGOING,

    MORSE_FINAL_ERROR = INT32_MAX
} morse_error_t;


#define MORSE_ERROR_CHECK(_status_)    \
    do {                               \
        if (_status_ != MORSE_SUCCESS) \
        {                              \
            goto error_handler;        \
        }                              \
    } while (0)


#define MORSE_ERROR_RAISE(_status_, _error_code_) \
    do {                                          \
        _status_ = _error_code_;                  \
        goto error_handler;                       \
    } while (0)


#define MORSE_NULL_CHECK(_status_, _ptr_)                    \
    do {                                                     \
        if (_ptr_ == NULL)                                   \
        {                                                    \
            MORSE_ERROR_RAISE(_status_, MORSE_NULL_POINTER); \
        }                                                    \
    } while (0)
