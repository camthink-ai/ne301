/**
 * @file web_recovery.h
 * @brief Built-in web recovery interface
 * @details When the web asset firmware fails to load (e.g. after an OTA moved
 *          the web partition address and the user did not flash a matching web
 *          firmware), the web service falls back to a tiny built-in HTML page.
 *          That page lets the user upload a web firmware package to recover the
 *          full web UI without reflashing the whole device.
 */

#ifndef WEB_RECOVERY_H
#define WEB_RECOVERY_H

#include "aicam_types.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enter web recovery mode.
 *        Called when the normal web assets could not be loaded. While active,
 *        the static request handler serves the built-in recovery page instead
 *        of the (missing) normal UI.
 */
void web_recovery_activate(void);

/**
 * @brief Whether web recovery mode is currently active.
 * @return AICAM_TRUE if the built-in recovery page is being served
 */
aicam_bool_t web_recovery_is_active(void);

/**
 * @brief Get the embedded recovery HTML page.
 * @return Pointer to a NUL-terminated HTML string stored in flash/rodata
 */
const char* web_recovery_get_html(void);

/**
 * @brief Get the size in bytes of the embedded recovery HTML page (excluding
 *        the NUL terminator).
 */
size_t web_recovery_get_html_size(void);

/**
 * @brief Reload the web assets from flash.
 *        Called after a web firmware upgrade completes successfully. On
 *        success the freshly written assets are loaded into RAM and recovery
 *        mode is cleared, so the normal UI becomes available immediately
 *        without a reboot. On failure recovery mode is left unchanged.
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t web_recovery_reload_assets(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_RECOVERY_H */
