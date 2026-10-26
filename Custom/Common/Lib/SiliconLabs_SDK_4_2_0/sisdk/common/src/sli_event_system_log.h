/***************************************************************************//**
 * @file
 * @brief Event System tracing macros.
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

#ifndef SLI_EVENT_SYSTEM_LOG_H
#define SLI_EVENT_SYSTEM_LOG_H

#include "sl_log_component.h"
#include "sl_event_system_config.h"

/// Compile-time trace level for the Event System. The global compile-time
/// level may restrict this further. Initialization happens before the logger
/// backend is ready, so compact backends buffer its init trace until stage 2;
/// the formatted backend discards it.
#ifndef SL_EVENT_SYSTEM_LOG_LEVEL_COMPILE_TIME
#define SL_EVENT_SYSTEM_LOG_LEVEL_COMPILE_TIME  SL_LOG_CONFIG_LEVEL_NONE
#endif

#if (SL_EVENT_SYSTEM_LOG_LEVEL_COMPILE_TIME != SL_LOG_CONFIG_LEVEL_NONE)
/// Runtime level, initialized to the compile-time level and adjustable from a debugger.
extern volatile sl_log_level_t sl_event_system_log_level;
#endif

#define SLI_EVENT_SYSTEM_LOG_TAG  "EVENT_SYSTEM: "

#if ((SL_EVENT_SYSTEM_LOG_LEVEL_COMPILE_TIME <= SL_LOG_CONFIG_LEVEL_DEBUG) \
  && (SL_LOG_CONFIG_LEVEL_COMPILE_TIME <= SL_LOG_CONFIG_LEVEL_DEBUG))
#define SLI_EVENT_SYSTEM_LOG_DEBUG(format, ...) \
  SLI_LOG_COMPONENT_PRINT_RUNTIME(DEBUG, sl_event_system_log_level, \
                                  SLI_EVENT_SYSTEM_LOG_TAG, format, ## __VA_ARGS__)
#else
#define SLI_EVENT_SYSTEM_LOG_DEBUG(format, ...) \
  SLI_LOG_COMPONENT_PRINT_NONE(format, ## __VA_ARGS__)
#endif

#if ((SL_EVENT_SYSTEM_LOG_LEVEL_COMPILE_TIME <= SL_LOG_CONFIG_LEVEL_INFO) \
  && (SL_LOG_CONFIG_LEVEL_COMPILE_TIME <= SL_LOG_CONFIG_LEVEL_INFO))
#define SLI_EVENT_SYSTEM_LOG_INFO(format, ...) \
  SLI_LOG_COMPONENT_PRINT_RUNTIME(INFO, sl_event_system_log_level, \
                                  SLI_EVENT_SYSTEM_LOG_TAG, format, ## __VA_ARGS__)
#else
#define SLI_EVENT_SYSTEM_LOG_INFO(format, ...) \
  SLI_LOG_COMPONENT_PRINT_NONE(format, ## __VA_ARGS__)
#endif

#if ((SL_EVENT_SYSTEM_LOG_LEVEL_COMPILE_TIME <= SL_LOG_CONFIG_LEVEL_WARN) \
  && (SL_LOG_CONFIG_LEVEL_COMPILE_TIME <= SL_LOG_CONFIG_LEVEL_WARN))
#define SLI_EVENT_SYSTEM_LOG_WARN(format, ...) \
  SLI_LOG_COMPONENT_PRINT_RUNTIME(WARN, sl_event_system_log_level, \
                                  SLI_EVENT_SYSTEM_LOG_TAG, format, ## __VA_ARGS__)
#else
#define SLI_EVENT_SYSTEM_LOG_WARN(format, ...) \
  SLI_LOG_COMPONENT_PRINT_NONE(format, ## __VA_ARGS__)
#endif

#if ((SL_EVENT_SYSTEM_LOG_LEVEL_COMPILE_TIME <= SL_LOG_CONFIG_LEVEL_ERROR) \
  && (SL_LOG_CONFIG_LEVEL_COMPILE_TIME <= SL_LOG_CONFIG_LEVEL_ERROR))
#define SLI_EVENT_SYSTEM_LOG_ERROR(format, ...) \
  SLI_LOG_COMPONENT_PRINT_RUNTIME(ERROR, sl_event_system_log_level, \
                                  SLI_EVENT_SYSTEM_LOG_TAG, format, ## __VA_ARGS__)
#else
#define SLI_EVENT_SYSTEM_LOG_ERROR(format, ...) \
  SLI_LOG_COMPONENT_PRINT_NONE(format, ## __VA_ARGS__)
#endif

#endif /* SLI_EVENT_SYSTEM_LOG_H */
