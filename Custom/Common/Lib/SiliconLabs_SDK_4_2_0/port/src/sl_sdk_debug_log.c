/**
 * @file sl_sdk_debug_log.c
 * @brief WiseConnect host-side SDK debug-log redirect into the project logger.
 *
 * The 4.1.1 SDK routes its internal tracing through SL_DEBUG_LOG_V2(), which
 * on this external-host build calls sl_debug_log() (declared in
 * sl_constants.h, __WEAK default in sl_utility.c that vprintf's to the UART).
 * This TU provides a strong override that feeds each line into generic_log as
 * module "SLSDK". The modules are registered console-only (full DEBUG
 * verbosity on the UART log port) and kept out of the aicam.log file output
 * via module routing — file writes dropped lines under this trace volume.
 *
 * SL_DEBUG_LOG_V2 prefixes its fmt with "[DEBUG] "/"[WARN] "/... — that tag is
 * stripped here and mapped to the matching LogLevel, so the generic_log line
 * header carries the level exactly once (no "[SLSDK] [DEBUG] [DEBUG] ...").
 * Raw sl_debug_log() calls without a tag (e.g. the packet-dump fragments in
 * sl_rsi_utility.c) fall back to LOG_DEBUG.
 *
 * Enable with -DSL_SDK_DEBUG_LOG_ENABLE plus
 * -DSLI_HOST_CURRENT_DBGLOG_LEVEL=SLI_HOST_DBGLOG_LVL_DEBUG (the compile-time
 * gate in sl_constants.h that decides which SL_DEBUG_LOG_V2 levels survive;
 * its default is WARN-only). Both are wired in silabs_sdk.mk.
 *
 * NOTE: log_message() takes an osMutex, so SDK code running in ISR context
 * must not log. The WiseConnect sources only emit SL_DEBUG_LOG_V2 from
 * thread-level driver/command-engine code, never from the SPI IRQ handlers.
 */

#ifdef SL_SDK_DEBUG_LOG_ENABLE

#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include "generic_log.h"
#include "Log/debug.h"

/* Map the leading "[LEVEL] " tag SL_DEBUG_LOG_V2 bakes into fmt to a
 * LogLevel; advance *text past the tag. Returns LOG_DEBUG when untagged. */
static LogLevel sdk_trace_level(const char **text)
{
    static const struct {
        const char *tag;
        LogLevel level;
    } levels[] = {
        { "[DEBUG] ", LOG_DEBUG },
        { "[INFO] ",  LOG_INFO },
        { "[WARN] ",  LOG_WARNING },
        { "[ERROR] ", LOG_ERROR },
    };

    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        size_t len = strlen(levels[i].tag);
        if (strncmp(*text, levels[i].tag, len) == 0) {
            *text += len;
            return levels[i].level;
        }
    }
    return LOG_DEBUG;
}

void sl_debug_log(const char *format, ...)
{
    static char buf[256];
    va_list args;

    va_start(args, format);
    int len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (len <= 0) {
        return;
    }
    /* Strip the trailing \r\n the SDK strings carry — log_message adds its own. */
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }

    const char *text = buf;
    LogLevel level = sdk_trace_level(&text);

    log_message(level, DEBUG_MODULE_SDK, "%s", text);
}

#endif /* SL_SDK_DEBUG_LOG_ENABLE */
