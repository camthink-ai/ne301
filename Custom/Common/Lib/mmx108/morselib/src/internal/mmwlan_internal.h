/*
 * Copyright 2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup MMWLAN_INTERNAL Unreleased MMWLAN API
 *
 * Extra interfaces in addition to the public MMWLAN API. These are not released to customers,
 * they are for use inside Morse Micro in fullmac and standalone firmware.
 *
 * @{
 */

#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "mmwlan.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Declaration of mmpkt struct type. See mmpkt.h for definition. */
struct mmpkt;

/** Shutdown callback prototype. */
typedef void (*mmwlan_shutdown_cb_t)(void *arg);

/**
 * Enable station mode, without waiting for event completion.
 *
 * Same as @ref mmwlan_sta_enable(), but returns before the umac event loop has
 * handled the connection request.
 *
 * @note Unlike @ref mmwlan_sta_enable(), @p sta_args must have static lifetime since the umac event
 *       loop will dereference it after this function returns.
 * @note A copy of @p args->extra_assoc_ies buffer will be made if @p args->extra_assoc_ies_len is
 *       non-zero. Caller is responsible for freeing the buffer in @p args after this function is
 * called.
 *
 * @param args              STA arguments (e.g., SSID, etc.). See @ref mmwlan_sta_args.
 * @param sta_status_cb     Optional callback to be invoked on STA state changes. May be @c NULL.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_sta_enable_nowait(const struct mmwlan_sta_args *args,
                                            mmwlan_sta_status_cb_t sta_status_cb);

/**
 * Disable station mode, without waiting for event completion.
 *
 * Same as @ref mmwlan_sta_disable(), but returns before the umac event loop has
 * handled the disconnection request.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_sta_disable_nowait(void);

/**
 * Perform a clean shutdown of the UMAC, without waiting for event completion.
 *
 * Same as @ref mmwlan_shutdown(), but returns before the UMAC event loop has handled the shutdown
 * request.
 *
 * @param shutdown_cb  Optional callback to be invoked when the UMAC is shut down. May be @c NULL.
 * @param cb_arg       Opaque argument to be passed to the callback.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_shutdown_nowait(mmwlan_shutdown_cb_t shutdown_cb, void *cb_arg);

/**
 * Stop and remove mmwlan event loop task.
 *
 * This should be called in an event invoked by @ref mmwlan_shutdown_cb_t but not within the
 * callback since the callback is called in event loop task.
 */
void mmwlan_stop_core_if_no_interface(void);

/**
 * Set the STA MAC address.
 *
 * This must be called after @ref mmwlan_boot() but before @ref mmwlan_sta_enable().
 *
 * @param mac_addr  Array of length @ref DOT11_MAC_ADDR_LEN containing the MAC address to use.
 *
 * @returns @c MMWLAN_SUCCESS on success, or an error code.
 */
enum mmwlan_status mmwlan_sta_set_mac_addr(const uint8_t *mac_addr);

/** Enumeration of STA autoconnect modes. */
enum mmwlan_sta_autoconnect_mode
{
    /** Automatically attempt to reconnect when connection is lost. */
    MMWLAN_STA_AUTOCONNECT_ENABLED,
    /** Do not automatically reconnect when connection is lost. */
    MMWLAN_STA_AUTOCONNECT_DISABLED,
};

/**
 * Set the reconnection behavior when the STA loses connection.
 *
 * @param mode Desired autoconnect mode.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_sta_autoconnect(enum mmwlan_sta_autoconnect_mode mode);

/**
 * Roam onto the given BSSID. Only valid while already connected.
 *
 * @param bssid Array of length @ref DOT11_MAC_ADDR_LEN containing the BSSID to roam onto.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_roam(const uint8_t *bssid);

/** Enumeration of key types supported by @ref mmwlan_key_info. */
enum mmwlan_key_type
{
    MMWLAN_KEY_TYPE_PAIRWISE, /**< Pairwise key */
    MMWLAN_KEY_TYPE_GROUP, /**< Group key */
    MMWLAN_KEY_TYPE_IGTK, /**< Integrity group key for protected management frames */
};

/** Maximum length of a key in @ref mmwlan_key_info. */
#define MMWLAN_MAX_KEYLEN (32)
/** Maximum number of keys that can be returned from @ref mmwlan_ate_get_key_info(). */
#define MMWLAN_MAX_KEYS (6)

/** Data structure representing a WLAN security key. */
struct mmwlan_key_info
{
    /** Index of the key */
    uint8_t key_id;
    /** Type of key */
    enum mmwlan_key_type key_type;
    /** Array to store the actual key */
    uint8_t key_data[MMWLAN_MAX_KEYLEN];
    /** Length of the key data */
    size_t key_len;
};

/**
 * Read WLAN security keys from the device. This is generally used for decrypting sniffer
 * traces. This should not be used in production as it is a security risk.
 *
 * @param[out] key_info     Array of mmwlan_key_info structs to receive the key information.
 * @param key_info_count    Pointer to an integer that may be initialized to the number of items
 *                          in the @p key_info array when the function is invoked. On successful
 *                          return this will be updated to the number of actual keys that were
 *                          written to @p key_info.
 *
 * @returns @c MMWLAN_SUCCESS on success, or an error code.
 */
enum mmwlan_status mmwlan_ate_get_key_info(struct mmwlan_key_info *key_info,
                                           uint32_t *key_info_count);

#if defined(ENABLE_EXTERNAL_EVENT_LOOP) && ENABLE_EXTERNAL_EVENT_LOOP
/**
 * Dispatch any pending UMAC/Supplicant events and/or timers.
 *
 * @param[out] next_event_time  Will be set to the time (in ms) until the next event (or
 *                              @c UINT32_MAX if no further events currently scheduled).
 *
 * @returns @c MMWLAN_SUCCESS on success, or an error code.
 */
enum mmwlan_status mmwlan_dispatch_events(uint32_t *next_event_time);
#endif

/** An indication of whether the UMAC can sleep or not. */
enum mmwlan_sleep_state
{
    /** The UMAC is busy and cannot sleep. */
    MMWLAN_SLEEP_STATE_BUSY,
    /** The UMAC is idle and ready to sleep until the next wake-up. */
    MMWLAN_SLEEP_STATE_IDLE,
};

/**
 * Prototype for sleep state change callback.
 *
 * @param sleep_state      Current sleep state.
 * @param next_timeout_ms  Time in milliseconds until the UMAC needs to wake up, if it is currently
 *                         ready to sleep.
 * @param arg              Opaque argument that was given when the callback was registered.
 */
typedef void (
    *mmwlan_sleep_cb_t)(enum mmwlan_sleep_state sleep_state, uint32_t next_timeout_ms, void *arg);

/**
 * Register a sleep state change callback.
 *
 * The callback will be invoked whenever the umac becomes busy or is ready to sleep.
 *
 * @warning The sleep state change callback may be invoked from any task or interrupt handler.
 *          It must not call any MMWLAN API functions or OS functions or any other blocking
 *          functions.
 *
 * @param callback  The callback to register.
 * @param arg       Opaque argument to be passed to the callback.
 *
 * @returns @c MMWLAN_SUCCESS on success, or an error code.
 */
enum mmwlan_status mmwlan_register_sleep_cb(mmwlan_sleep_cb_t callback, void *arg);

/**
 * Transmit the given management frame with 802.11 mac header. Given management frame is not meant
 * to be allocated by the likes of @ref mmwlan_alloc_mmpkt_for_tx().
 *
 * @param txbuf mmpkt containing the payload to be transmitted. This will be consumed by
 *              this function.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_tx_mgmt_frame(struct mmpkt *txbuf);

/**
 * Enumeration of flags for frame filter.
 */
enum mmwlan_frame_filter_flag
{
    MMWLAN_FRAME_NO_MATCH = 0, /**< No matching frame filter flag */
    /* Management frames */
    MMWLAN_FRAME_ASSOC_REQ = 1 << 0, /**< Association request frame */
    MMWLAN_FRAME_ASSOC_RSP = 1 << 1, /**< Association response frame */
    MMWLAN_FRAME_REASSOC_REQ = 1 << 2, /**< Re-association request frame */
    MMWLAN_FRAME_REASSOC_RSP = 1 << 3, /**< Re-association response frame */
    MMWLAN_FRAME_PROBE_REQ = 1 << 4, /**< Probe request frame */
    MMWLAN_FRAME_PROBE_RSP = 1 << 5, /**< Probe response frame */
    MMWLAN_FRAME_TIMING_ADV = 1 << 6, /**< Timing advertisement frame */
    MMWLAN_FRAME_BEACON = 1 << 8, /**< Beacon frame */
    MMWLAN_FRAME_ATIM = 1 << 9, /**< A.T.I.M. frame */
    MMWLAN_FRAME_DISASSOC = 1 << 10, /**< Disassociation frame */
    MMWLAN_FRAME_AUTH = 1 << 11, /**< Authentication frame */
    MMWLAN_FRAME_DEAUTH = 1 << 12, /**< De-authentication frame */
    MMWLAN_FRAME_ACTION = 1 << 13, /**< Action frame */
    MMWLAN_FRAME_ACTION_NO_ACK = 1 << 14, /**< Action No Ack frame */
};

/**
 * Information of the received frame that matches the RX frame filter (@c
 * umac_connection_data.rx_frame_filter)
 */
struct mmwlan_rx_frame_info
{
    /** Frame filter flag of the received frame. */
    enum mmwlan_frame_filter_flag frame_filter_flag;
    /** Pointer to the received frame data. */
    const uint8_t *buf;
    /** Length of the frame data (@c buf). */
    uint32_t buf_len;
    /** Frequency at which the frame was received (in 100 kHz). */
    uint16_t freq_100khz;
    /** RSSI of the received frame. */
    int16_t rssi_dbm;
    /** Bandwidth in MHz, where the frame was received. */
    uint8_t bw_mhz;
};

/**
 * Frame receive callback function prototype.
 *
 * Prototype for a frame receive callback that can be registered via @c
 * umac_datapath_data.rx_frame_cb to be invoked on reception of certain WLAN frames (configured by
 * @c umac_datapath_data.rx_frame_filter). The callback is invoked for a given non-data frame after
 * validation and prior to handling by the WLAN Upper MAC.
 *
 * The callback function should not alter the received buffer. The received buffer can be used to
 * retrieve various packet contents such as IEs or be sent up to the host as a full packet. When the
 * buffer is being sent up to the host as a full packet, the packet should be a copy of the given
 * buffer, not the original buffer itself.
 *
 * @warning This is for advanced use only, and will not be required in most typical use cases.
 * Amount of work done here should be kept to an absolute minimum and mmwlan API must not be invoked
 * from the callback.
 */
typedef void (*mmwlan_rx_frame_cb_t)(const struct mmwlan_rx_frame_info *rx_info, void *arg);

/**
 * Register rx frame callback and its frame sub-type filter.
 *
 * @param filter Frame sub-type bitmap to match the received frame with.
 * @param callback Callback to call when the received frame matches the filter.
 * @param arg Extra argument that is reserved to be used in the callback.
 *
 * @note The callback function cannot be registered for data frames.
 *
 * @return MMWLAN_SUCCESS
 */
enum mmwlan_status mmwlan_register_rx_frame_cb(uint32_t filter,
                                               mmwlan_rx_frame_cb_t callback,
                                               void *arg);

/**
 * Retrieve UMAC statistics in serialized (TLV) form.
 *
 * @param buf   Buffer to serialize into.
 * @param len   Pointer to a size_t that is initialized to the length of the buffer. The value
 *              will be updated by this function to the actual length of data written into
 *              the buffer.
 *
 * @returns @c MMWLAN_SUCCESS on success, @c MMWLAN_NO_MEM if the buffer was too small.
 */
enum mmwlan_status mmwlan_get_serialized_umac_stats(uint8_t *buf, size_t *len);

#ifdef __cplusplus
}
#endif

/** @} */
