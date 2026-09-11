/***************************************************************************/ /**
 * @file  sl_constants.h
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

#include "sl_additional_status.h"
#include "sl_status.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#if (defined(SLI_SI91X_MCU_INTERFACE) || defined(SLI_SI91X_NCP_INTERFACE))
#include "sl_log_helper.h"
#endif

/** @brief
 *    Macro for marking deprecated functions
 *
 *  @details
 *    SL_DEPRECATED_API_WISECONNECT_<RELEASE> is used to mark functions that are
 *    deprecated and should not be used from a given version of WiSeConnect SDK.
 *    The accompanying SL_SUPPRESS_DEPRECATION_WARNINGS_WISECONNECT_<RELEASE>
 *    define can be set to suppress warnings generated when using
 *    deprecated APIs.
 */
#ifdef SL_SUPPRESS_DEPRECATION_WARNINGS_WISECONNECT_3_5
#define SL_DEPRECATED_API_WISECONNECT_3_5
#else
#define SL_DEPRECATED_API_WISECONNECT_3_5 __attribute__((deprecated))
#endif

#ifdef SL_SUPPRESS_DEPRECATION_WARNINGS_WISECONNECT_4_0
#define SL_DEPRECATED_API_WISECONNECT_4_0
#else
#define SL_DEPRECATED_API_WISECONNECT_4_0 __attribute__((deprecated))
#endif

#ifdef SL_SUPPRESS_DEPRECATION_WARNINGS_WISECONNECT_4_1
#define SL_DEPRECATED_API_WISECONNECT_4_1
#else
#define SL_DEPRECATED_API_WISECONNECT_4_1 __attribute__((deprecated))
#endif

#define SLI_STATUS_ENUM(prefix, name, value) prefix##_##name = (prefix##_ENUM_OFFSET + value)
#define SLI_STATUS_SHARED_ENUM(prefix, name) prefix##_##name = (SL_##name)

// Project-wide assert hook (strong definition in Custom/Hal sl_net_netif.c,
// also the newlib assert() destination): prints file:line and reboots; halts
// on bkpt only when a debugger session is live. A raw bkpt executed without a
// debugger escalates to HardFault (lockup in IRQ context) and silently hangs
// shipping units (lab-port fix, SDK_LOCAL_MODIFICATIONS.md §3.8).
extern void __assert_func(const char *file, int line, const char *func, const char *failedexpr);

#define BREAKPOINT() __assert_func(__FILE__, __LINE__, __func__, "BREAKPOINT")

/// IPv4 address length in bytes.
#define SL_IPV4_ADDRESS_LENGTH 4

/// IPv6 address length in bytes.
#define SL_IPV6_ADDRESS_LENGTH 16

/// Number of 32-bit words in an IPv6 address.
#define SL_IPV6_ADDRESS_U32_COUNT 4

/// Max length of an IPv6 address string in characters, including the terminating null.
#define SL_IPV6_ADDRESS_STRING_MAX_LENGTH 46

/// MAC address length
#define SL_MAC_ADDRESS_LENGTH 6

/// Length of an EUI-64 address in bytes.
#define SL_EUI64_ADDRESS_LENGTH 8

#ifndef UNUSED_VARIABLE
#define UNUSED_VARIABLE(x) (void)(x)
#endif // UNUSED_VARIABLE
#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif // UNUSED_PARAMETER

#define SLI_ARRAY_COUNT(x) (sizeof(x) / sizeof *(x))

#ifndef FUZZING
#define SL_ASSERT(condition, ...)                                       \
  do {                                                                  \
    if (!(condition)) {                                                 \
      __assert_func(__FILE__, __LINE__, __func__, #condition);          \
    }                                                                   \
  } while (0)
#else
#define SL_ASSERT(condition, ...) \
  do {                            \
    if (!(condition)) {           \
    }                             \
  } while (0)
#endif

#define SL_WAIT_FOREVER    0xFFFFFFFF
#define SL_INVALID_POINTER ((void *)0xEFFFFFFF) // This can point to any location that will trigger an exception

// Defines for log tags
#define ERROR_TAG   "ERROR"
#define WARNING_TAG "WARNING"
#define DEBUG_TAG   "DEBUG"
#define INFO_TAG    "INFO"

// Defines for error logging
#define PRINT_ERROR_LOGS 0

#define PRINT_STATUS(tag, status) printf("\r\n%s %s:%d: 0x%lx \r\n", tag, __FILE__, __LINE__, (unsigned long)status);

#define SL_CHECK_STATUS(x)    \
  do {                        \
    if (x != SL_SUCCESS) {    \
      goto status_check_fail; \
    }                         \
  } while (0)

#define SL_CHECK_STATUS_AND_LOG(x, log) \
  do {                                  \
    if (x != SL_SUCCESS) {              \
      SL_LOG(log);                      \
      goto status_check_fail;           \
    }                                   \
  } while (0)

// Macros to help work with pointers / allocations
#define SL_VERIFY_POINTER_OR_EXIT(pointer) \
  do {                                     \
    if ((pointer) == NULL) {               \
      goto exit;                           \
    }                                      \
  } while (0)

#define SL_VERIFY_POINTER_OR_GOTO(pointer, label) \
  do {                                            \
    if ((pointer) == NULL) {                      \
      goto label;                                 \
    }                                             \
  } while (0)

#define SL_VERIFY_POINTER_OR_RETURN(pointer, status) \
  do {                                               \
    if ((pointer) == NULL) {                         \
      if (PRINT_ERROR_LOGS) {                        \
        PRINT_STATUS(ERROR_TAG, status)              \
      }                                              \
      return (status);                               \
    }                                                \
  } while (0)

#define SL_VERIFY_SUCCESS_OR_CONTINUE(x) \
  do {                                   \
    if (x != SL_STATUS_OK) {             \
      continue;                          \
    }                                    \
  } while (0)

#define SL_VERIFY_SUCCESS_OR_EXIT(x) \
  do {                               \
    if (x != SL_STATUS_OK) {         \
      goto exit;                     \
    }                                \
  } while (0)

#define SL_VERIFY_SUCCESS_OR_RETURN(x) \
  do {                                 \
    if ((x) != SL_STATUS_OK) {         \
      return (x);                      \
    }                                  \
  } while (0)

#define SL_CLEANUP_MALLOC(pointer) \
  do {                             \
    if ((pointer) != NULL) {       \
      SLI_FREE(pointer);              \
      pointer = NULL;              \
    }                              \
  } while (0)

#define SL_VERIFY_PARAMETER(condition) \
  do {                                 \
    if (!(condition)) {                \
      return SL_BAD_ARG;               \
    }                                  \
  } while (0)

#define VERIFY_STATUS_AND_RETURN(status)                             \
  do {                                                               \
    if (status != SL_STATUS_OK) {                                    \
      if (PRINT_ERROR_LOGS && ((status) != SL_STATUS_IN_PROGRESS)) { \
        PRINT_STATUS(ERROR_TAG, status)                              \
      }                                                              \
      return status;                                                 \
    }                                                                \
  } while (0)

#define SLI_VERIFY_STATUS_AND_GOTO(status, goto_label)               \
  do {                                                               \
    if (status != SL_STATUS_OK) {                                    \
      if (PRINT_ERROR_LOGS && ((status) != SL_STATUS_IN_PROGRESS)) { \
        PRINT_STATUS(ERROR_TAG, status)                              \
      }                                                              \
      goto goto_label;                                               \
    }                                                                \
  } while (0)

#define PRINT_ERROR_STATUS(tag, status)            \
  printf("\r\n%s %s:%d: 0x%x : Thread_id<%s>\r\n", \
         tag,                                      \
         __FILE__,                                 \
         __LINE__,                                 \
         (unsigned int)status,                     \
         osThreadGetName(osThreadGetId()))

#ifdef PRINT_DEBUG_LOG
extern void sl_debug_log(const char *format, ...);
#define SL_DEBUG_LOG(format, ...)                                                         \
  do {                                                                                    \
    sl_debug_log("%s:%s:%d:" format "\r\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__); \
  } while (0)
#else
extern void sl_redirect_log(const char *format, ...);
#define SL_DEBUG_LOG(format, ...)                                                            \
  do {                                                                                       \
    sl_redirect_log("%s:%s:%d:" format "\r\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__); \
  } while (0)
#endif

#define SL_COMPILE_TIME_ASSERT(condition, comment) typedef char assertion_failed__##comment[2 * !!(condition)-1];

// Expands to SL_PRINT_STRING_<level>(fmt, ...). Use level INFO, ERROR, DEBUG, or WARN (must match macro suffix).
#if (defined(SLI_SI91X_MCU_INTERFACE) || defined(SLI_SI91X_NCP_INTERFACE))
#define SL_DEBUG_LOG_V2(level, fmt, ...)         \
  do {                                           \
    SL_PRINT_STRING_##level(fmt, ##__VA_ARGS__); \
  } while (0)
#else

// This section of code is applicable for Non-Silabs host boards only as an alternative to the SL_PRINT_STRING_* macros.
extern void sl_debug_log(const char *format, ...);

#ifndef SLI_HOST_DBGLOG_LVL_DEBUG
#define SLI_HOST_DBGLOG_LVL_DEBUG 0
#endif

#ifndef SLI_HOST_DBGLOG_LVL_INFO
#define SLI_HOST_DBGLOG_LVL_INFO 1
#endif

#ifndef SLI_HOST_DBGLOG_LVL_WARN
#define SLI_HOST_DBGLOG_LVL_WARN 2
#endif

#ifndef SLI_HOST_DBGLOG_LVL_ERROR
#define SLI_HOST_DBGLOG_LVL_ERROR 3
#endif

#ifndef SLI_HOST_CURRENT_DBGLOG_LEVEL
#define SLI_HOST_CURRENT_DBGLOG_LEVEL SLI_HOST_DBGLOG_LVL_WARN
#endif

#define SL_DEBUG_LOG_V2(level, fmt, ...)                                \
  do {                                                                  \
    if (SLI_HOST_DBGLOG_LVL_##level >= SLI_HOST_CURRENT_DBGLOG_LEVEL) { \
      sl_debug_log("[" #level "] " fmt, ##__VA_ARGS__);                 \
    }                                                                   \
  } while (0)

#endif

typedef uint32_t sl_duration_t;

typedef void (*sli_event_handler_t)(void);
