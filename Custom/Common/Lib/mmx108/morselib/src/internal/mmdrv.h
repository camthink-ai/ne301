/*
 * Copyright 2021-2025 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup MMDRV Morse Micro Driver API
 *
 * The following API is to be implemented by the transceiver driver.
 *
 * @warning This is not considered to be a stable API and may change between minor revisions.
 *
 * @{
 */

#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "mmosal.h"
#include "mmpkt.h"
#include "mmwlan.h"

#include "mmrc.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * ---------------------------------------------------------------------------------------------
 *      Host -> Driver API
 * ---------------------------------------------------------------------------------------------
 */

/** STA state */
enum morse_sta_state
{
    MORSE_STA_NOTEXIST = 0,
    MORSE_STA_NONE = 1,
    MORSE_STA_AUTHENTICATED = 2,
    MORSE_STA_ASSOCIATED = 3,
    MORSE_STA_AUTHORIZED = 4,
    MORSE_STA_MAX = UINT16_MAX
};

/**
 * enum @c morse_param_id - Device parameters that can be set/get
 */
enum morse_param_id
{
    MORSE_PARAM_ID_MAX_TRAFFIC_DELIVERY_WAIT_US = 0,
    MORSE_PARAM_ID_EXTRA_ACK_TIMEOUT_ADJUST_US = 1,
    MORSE_PARAM_ID_TX_STATUS_FLUSH_WATERMARK = 2,
    MORSE_PARAM_ID_TX_STATUS_FLUSH_MIN_AMPDU_SIZE = 3,
    MORSE_PARAM_ID_POWERSAVE_TYPE = 4,
    MORSE_PARAM_ID_SNOOZE_DURATION_ADJUST_US = 5,
    MORSE_PARAM_ID_TX_BLOCK = 6,
    MORSE_PARAM_ID_FORCED_SNOOZE_PERIOD_US = 7,
    MORSE_PARAM_ID_WAKE_ACTION_GPIO = 8,
    MORSE_PARAM_ID_WAKE_ACTION_GPIO_PULSE_MS = 9,
    MORSE_PARAM_ID_CONNECTION_MONITOR_GPIO = 10,
    MORSE_PARAM_ID_INPUT_TRIGGER_GPIO = 11,
    MORSE_PARAM_ID_INPUT_TRIGGER_MODE = 12,
    MORSE_PARAM_ID_COUNTRY = 13,
    MORSE_PARAM_ID_RTS_THRESHOLD = 14,
    MORSE_PARAM_ID_HOST_TX_BLOCK = 15,
    MORSE_PARAM_ID_MEM_RETENTION_CODE = 16,
    MORSE_PARAM_ID_NON_TIM_MODE = 17,
    MORSE_PARAM_ID_DYNAMIC_PS_TIMEOUT_MS = 18,
    MORSE_PARAM_ID_HOME_CHANNEL_DWELL_MS = 19,
    MORSE_PARAM_ID_SLOW_CLOCK_MODE = 20,
    MORSE_PARAM_ID_FRAGMENT_THRESHOLD = 21,
    MORSE_PARAM_ID_BEACON_LOSS_COUNT = 22,

    MORSE_PARAM_ID_LAST,
    MORSE_PARAM_ID_MAX = UINT32_MAX,
};

/** Enumeration of flags used to specify direction which traffic flow. */
enum mmdrv_direction
{
    MMDRV_DIRECTION_OUTGOING = 0,
    MMDRV_DIRECTION_INCOMING = 1
};

/** Enumeration of veto_id ranges for use with @ref mmhal_set_deep_sleep_veto() and
 *  @ref mmhal_clear_deep_sleep_veto(). */
enum mmdrv_health_check_veto_id
{
    MMDRV_HEALTH_CHECK_VETO_ID_STANDBY,
    MMDRV_HEALTH_CHECK_VETO_ID_WNM_SLEEP
};

/** Maximum length in bytes of the key data. */
#define MORSE_KEY_MAXLEN (32)

/** The max length of the TWT agreement sent to the FW */
#define TWT_MAX_AGREEMENT_LEN (20)

/** Key configuration */
struct mmdrv_key_conf
{
    /** Is a Pairwise Key */
    bool is_pairwise;
    /** TX packet number */
    uint64_t tx_pn;
    /** Length of key in Bytes. Allowed values are 16 and 32. */
    uint16_t length;
    /** Key data */
    uint8_t key[MORSE_KEY_MAXLEN];
    /** Key index to use. */
    uint8_t key_idx;
};

/** TWT configuration */
struct mmdrv_twt_data
{
    /** Interface ID */
    uint16_t interface_id;
    /** TWT flow ID */
    uint8_t flow_id;
    /**
     * TWT agreement length.
     * @note This should not be more than @ref TWT_MAX_AGREEMENT_LEN.
     */
    uint8_t agreement_len;
    /**
     * TWT agreement data.
     * @code
     *
     *  +---------+---------+-----------+-------------+--------------+----------+---------+--------+
     *  | Control | Request | Target    | TWT Group   | Min TWT Wake | Mantissa | Channel | NDP    |
     *  |         | Type    | Wake Time | Assignment  | Duration     |          |         | Paging |
     *  +---------+---------+-----------+-------------+--------------+----------+---------+--------+
     *  |----1----|----2----|--8 or 0---|-9 or 3 or 0-|------1-------|-----2----|----1----|-0 or 4-|
     *
     * @endcode
     *
     * @note Either Target Wake Time or TWT Group Assignment field will be present, not both.
     */
    const uint8_t *agreement;
};

/** Struct to store the semantic version contents. */
struct mmdrv_fw_version
{
    /** Major version digit */
    uint8_t major;
    /** Minor version digit */
    uint8_t minor;
    /** Patch version digit */
    uint8_t patch;
};

/** Structure of information read from the chip. */
struct mmdrv_chip_info
{
    /** Array to store the MAC address read from the chip. */
    uint8_t mac_addr[MMWLAN_MAC_ADDR_LEN];
    /** Struct store the FW version read from the chip. Parsed from the release tag, which should be
     * in the format 'rel_<major>_<minor>_<patch>'. If the tag is not in this format then
     * corresponding version field will be 0.*/
    struct mmdrv_fw_version fw_version;
    /** Morse transceiver chip ID. */
    uint32_t morse_chip_id;
    /** Morse transceiver chip ID user-friendly string. */
    const char *morse_chip_id_string;
};

/** Perform "one off" initialization of the driver. Invoked from @ref mmwlan_init(). */
void mmdrv_pre_init(void);

/** Perform "one off" deinitialization of the driver. Invoked from @ref mmwlan_deinit(). */
void mmdrv_post_deinit(void);

/**
 * Start the driver. Invoked when powering on the chip.
 *
 * @param[out] chip_info    Data structure to be filled out with chip information.
 * @param country_code      Country code of the currently configured channel list.
 *                          This must be a string containing the two character country
 *                          code followed by a null terminator.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_init(struct mmdrv_chip_info *chip_info, const char *country_code);

#ifdef UNIT_TESTS
/**
 * Basic start function for use in unit tests. This should put the driver into
 * a state where @ref mmdrv_alloc_mmpkt_for_tx() will return valid values.
 */
void mmdrv_init_for_unit_tests(void);
#endif

/**
 * Stops the driver. Invoked when powering off the chip.
 */
void mmdrv_deinit(void);

/**
 * Read metadata from the board configuration file (BCF).
 *
 * @param metadata  Pointer to a metadata data structure to be filled out on success.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_get_bcf_metadata(struct mmwlan_bcf_metadata *metadata);

#define MMDRV_DUTY_CYCLE_MIN (1lu) /**< Minimum allowable duty cycle value. */
#define MMDRV_DUTY_CYCLE_MAX (10000lu) /**< Maximum allowable duty cycle value. */

/**
 * Set the transmit duty cycle.
 *
 * @param duty_cycle    The duty cycle to set (in 100th of percent).
 *                      Valid range @ref MMDRV_DUTY_CYCLE_MIN - @ref MMDRV_DUTY_CYCLE_MAX.
 * @param duty_cycle_omit_ctrl_resp Boolean indicating whether to omit control response
 *                                  frames from duty cycle.
 * @param mode          Duty cycle mode, see @ref mmwlan_duty_cycle_mode.
 *
 * @return @c 0 if successful, else appropriate error code.
 */
int mmdrv_set_duty_cycle(uint32_t duty_cycle,
                         bool duty_cycle_omit_ctrl_resp,
                         enum mmwlan_duty_cycle_mode mode);

/**
 * Retrieve the transmit duty cycle configuration and statistics.
 *
 * @param stats Pointer to a duty cycle statistics structure.
 *
 * @return @c 0 if successful, else appropriate error code.
 */
int mmdrv_get_duty_cycle(struct mmwlan_duty_cycle_stats *stats);

/**
 * Get the MAC address of the transceiver.
 *
 * @param[out] mac_addr The transceiver MAC address.
 */
void mmdrv_read_mac_addr(uint8_t *mac_addr);

/**
 * Set the transmit power level to use.
 *
 * @param[out] out_power_dbm    Actual power level after this command.
 * @param txpower_dbm           The transmit power level to set (dBm).
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_set_txpower(int32_t *out_power_dbm, int txpower_dbm);

/**
 * Different interface types
 */
enum mmdrv_interface_type
{
    /** A station interface */
    MMDRV_INTERFACE_TYPE_STA = 1,
    /** An access point interface */
    MMDRV_INTERFACE_TYPE_AP = 2,
};

/**
 * Add an interface.
 *
 * @note Only a single interface is supported with the current API.
 *
 * @param[out] vif_id  If not NULL and the interface was added successfully,
 *                     this will be updated with value of the newly created
 *                     VIF ID.
 * @param addr         MAC address to use for the interface.
 * @param type         The interface type.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_add_if(uint16_t *vif_id, const uint8_t *addr, enum mmdrv_interface_type type);

/**
 * Remove the given interface.
 *
 * @param vif_id      VIF ID of the given interface.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_rm_if(uint16_t vif_id);

/**
 * Start beaconing (for AP VIFs).
 *
 * @param vif_id      VIF ID of the given interface.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_start_beaconing(uint16_t vif_id);

/**
 * Configure scan mode.
 *
 * @param enabled       Whether scan mode should be enabled.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_cfg_scan(bool enabled);

/**
 * Install TWT agreement request.
 *
 * @param twt_data      Pointer to the TWT agreement data.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_twt_agreement_install_req(const struct mmdrv_twt_data *twt_data);

/**
 * Validate TWT agreement request.
 *
 * @param twt_data      Pointer to the TWT agreement data.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_twt_agreement_validate_req(const struct mmdrv_twt_data *twt_data);

/**
 * Remove TWT agreement request.
 *
 * @param twt_data      Pointer to the TWT agreement data.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_twt_remove_req(const struct mmdrv_twt_data *twt_data);

/**
 * Set the channel.
 *
 * @param op_chan_freq_hz       Operating channel frequency in Hz.
 * @param pri_1mhz_chan_idx     Primary 1 MHz channel index.
 * @param op_bw_mhz             Operating bandwidth in MHz.
 * @param pri_bw_mhz            Primary 1 MHz bandwidth in MHz.
 * @param is_off_channel        Indicates that the channel is not the home channel.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_set_channel(uint32_t op_chan_freq_hz,
                      uint8_t pri_1mhz_chan_idx,
                      uint8_t op_bw_mhz,
                      uint8_t pri_bw_mhz,
                      bool is_off_channel);

/**
 * Configure the BSS for the MM-Chip.
 *
 * @param vif_id        The VIF_ID this pertains to..
 * @param beacon_int    The beacon interval in time units (TU)
 * @param dtim_period   The Delivery Traffic Indication Map (DTIM) period in beacons.
 *                      Only required if the interface is an AP else set to @c 0
 * @param cssid         Compressed ssid. Only required if the interface is an AP else set to @c 0
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_cfg_bss(uint16_t vif_id, uint16_t beacon_int, uint16_t dtim_period, uint32_t cssid);

/**
 * Update the STA state. This is used for both STA and AP interface types.
 *
 * @param vif_id    The VIF_ID this pertains to.
 * @param aid       The STA AID.
 * @param addr      This contains the BSSID;
 * @param state     The new STA state.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_update_sta_state(uint16_t vif_id,
                           uint16_t aid,
                           const uint8_t *addr,
                           enum morse_sta_state state);

/**
 * Install an encryption key.
 *
 * @param vif_id       The vif_id this pertains to.
 * @param aid          The STA AID.
 * @param key_conf     Configuration of the Key to install.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_install_key(uint16_t vif_id, uint16_t aid, struct mmdrv_key_conf *key_conf);

/**
 * Disable the given encryption key.
 *
 * @param vif_id       The vif_id this pertains to.
 * @param aid          The STA AID.
 * @param hw_key_idx   ID of the Key to be deleted.
 * @param is_pairwise  Whether this pertains to a pairwise key.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_disable_key(uint16_t vif_id, uint16_t aid, uint8_t hw_key_idx, bool is_pairwise);

/**
 * Configure a QoS queue using the given queue parameters.
 *
 * @param params    The queue parameters.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_cfg_qos_queue(const struct mmwlan_qos_queue_params *params);

/**
 * Tells the driver interface that we want the MM-Chip to stay awake or not.
 *
 * @param enabled   If this is set to @c true the driver will keep the MM-Chip awake
 *                  if set to @c false we are telling the driver the MM-Chip can go to
 *                  sleep.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_set_wake_enabled(bool enabled);

/**
 * Sends a command to the MM-Chip to tell it to enable its power save logic.
 *
 * @param vif_id    VIF ID of the interface this applies to.
 * @param enabled   If this is set to @c true the MM-Chip logic is enabled.
 *                  If @c false the MM-Chip power save logic is disabled.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_set_chip_power_save_enabled(uint16_t vif_id, bool enabled);

/**
 * Specify the upper and lower bound for the periodic health check interval. To guarantee a specific
 * interval set both @c min_interval_ms and @c max_interval_ms to the same value.
 *
 * @note To disable periodic health checks entirely set both values to zero (0).
 *
 * @param min_interval_ms Minimum value that the interval can be.
 * @param max_interval_ms Maximum value that the interval can be.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_set_health_check_interval(uint32_t min_interval_ms, uint32_t max_interval_ms);

/**
 * Sets a veto to disable periodic health checks. One or more vetoes can be used at any given time
 * to suspend periodic health checks. Only when all of the vetoes have been cleared will periodic
 * health checks be resumed.
 *
 * @param veto_id   The veto identifier to enable.
 */
void mmdrv_set_health_check_veto(enum mmdrv_health_check_veto_id veto_id);

/**
 * Removes a veto from the periodic health check suspension. Periodic health checks will only resume
 * when all vetoes are disabled.
 *
 * @param veto_id The veto identifier to disable.
 */
void mmdrv_unset_health_check_veto(enum mmdrv_health_check_veto_id veto_id);

/**
 * Invoked by the UMAC in response to @ref mmdrv_host_hw_restart_required() to indicate that
 * the hardware restart process has completed.
 */
void mmdrv_hw_restart_completed(void);

/** Enumeration of flags used by @ref mmdrv_tx_metadata.flags */
enum mmdrv_tx_metadata_flags
{
    /** Allow aggregation of this packet in an A-MPDU. */
    MMDRV_TX_FLAG_AMPDU_ENABLED = 0x01,
    /** Require immediate TX status reporting from the firmware. */
    MMDRV_TX_FLAG_IMMEDIATE_REPORT = 0x02,
    /** Encrypt this packet using the key at the specified index. */
    MMDRV_TX_FLAG_HW_ENC = 0x04,
    /** Use traveling pilots. */
    MMDRV_TX_FLAG_TP_ENABLED = 0x08,
    /** No ACK expected for the given packet. */
    MMDRV_TX_FLAG_NO_ACK = 0x10,
    /** Control response preamble at 1MHz bandwidth. */
    MMDRV_TX_FLAG_CR_1MHZ_PRE_ENABLED = 0x20,
};

/** Enumeration of flags used by @ref mmdrv_tx_metadata.status_flags */
enum mmdrv_tx_metadata_status_flags
{
    /** Indicates that no ACK was received. */
    MMDRV_TX_STATUS_FLAG_NO_ACK = 0x10,
    /** Indicates whether the frame was returned as the destination/sender is entering
     *  power save. */
    MMDRV_TX_STATUS_FLAG_PS_FILTERED = 0x20,
    /* Indicates the frame was returned as the remaining transmit air time is insufficient. */
    MMDRV_TX_STATUS_DUTY_CYCLE_CANT_SEND = 0x40,
    /* Indicates the frame was sent as part of an AMPDU for at least one of the attempts. */
    MMDRV_TX_STATUS_WAS_AGGREGATED = 0x80
};

/** Mask of the TX status flags that indicate some form of failure.  */
#define MMDRV_TX_STATUS_FLAGS_FAIL_MASK \
    (MMDRV_TX_STATUS_FLAG_NO_ACK |      \
     MMDRV_TX_STATUS_FLAG_PS_FILTERED | \
     MMDRV_TX_STATUS_DUTY_CYCLE_CANT_SEND)

/** Metadata structure for TX mmpkts. */
struct mmdrv_tx_metadata
{
    /* -- Driver internal fields -- */

    /** Timeout timestamp field for driver use. */
    uint32_t timeout_abs_ms;

    /* -- TX config (UMAC to driver fields) -- */

    /** The rate control table to use for this packet. Filled in by UMAC prior to passing the
     *  mmpkt to @ref mmdrv_tx_frame(). */
    struct mmrc_rate_table rc_data;

    /** The VIF ID to use for this packet. Filled in by UMAC prior to passing the mmpkt to
     *  @ref mmdrv_tx_frame(). */
    uint8_t vif_id;
    /** The index of the encryption key to use. Filled in by UMAC prior to passing the mmpkt to
     *  @ref mmdrv_tx_frame(). */
    uint8_t key_idx;
    /** The TID to use for this packet. Filled in by UMAC prior to passing the mmpkt to
     *  @ref mmdrv_tx_frame(). */
    uint8_t tid;
    /** Size of MPDU reordering buffer (in MSDUs) reported by the other end. Filled in by UMAC
     *  prior to passing the mmpkt to @ref mmdrv_tx_frame(). */
    uint8_t tid_max_reorder_buf_size;
    /** Flags applicable to this packet. Filled in by UMAC prior to passing the mmpkt to
     *  @ref mmdrv_tx_frame(). See @ref mmdrv_tx_metadata_flags. */
    uint8_t flags;
    /** The AMPDU minimum spacing to apply */
    uint8_t ampdu_mss;
    /** The MMSS offset to apply */
    uint8_t mmss_offset;

    /* -- TX status (driver to UMAC fields) -- */

    /** Status flags filled in by driver prior passing the mmpkt back to the UMAC via.
     *  @ref mmdrv_host_process_tx_status(). See @ref mmdrv_tx_metadata_status_flags. */
    uint8_t status_flags;
    /** Number of attempts that were made to transmit this packet. Filled in by driver
     *  prior passing the mmpkt back to the UMAC via. @ref mmdrv_host_process_tx_status(). */
    uint8_t attempts;

    /* -- Additional driver internal field -- */

    /** Additional bytes added to mmpkt for alignment when transmitting to the chip. This is for
     * driver use only and will be removed after sending to chip. */
    uint8_t tail_padding;

    /* -- Additional UMAC internal fields -- */

    /** AID of the relevant STA record. */
    uint16_t aid;

    /** Encryption to send the frame with. Is of value `enum umac_datapath_frame_encryption` */
    uint8_t enc;
};

/**
 * Retrieve the metadata from an TX frame (also applies for frames passed to
 * @ref mmdrv_host_process_tx_status).
 *
 * @warning This must only be for TX frames.
 *
 * @param txbuf The mmpkt to get the attached metadata from.
 *
 * @returns a reference to the TX metadata in @p txbuf.
 */
static inline struct mmdrv_tx_metadata *mmdrv_get_tx_metadata(struct mmpkt *txbuf)
{
    struct mmdrv_tx_metadata *metadata = mmpkt_get_metadata(txbuf).tx;
    MMOSAL_ASSERT(metadata != NULL);
    return metadata;
}

/**
 * Enumeration of packet classes used by the @ref mmdrv_alloc_mmpkt_for_tx() @c pkt_class
 * parameter. These definitions must match the corresponding values in @c mmhal_wlan_pkt_class.
 */
enum mmdrv_pkt_class
{
    MMDRV_PKT_CLASS_DATA_TID0,
    MMDRV_PKT_CLASS_DATA_TID1,
    MMDRV_PKT_CLASS_DATA_TID2,
    MMDRV_PKT_CLASS_DATA_TID3,
    MMDRV_PKT_CLASS_DATA_TID4,
    MMDRV_PKT_CLASS_DATA_TID5,
    MMDRV_PKT_CLASS_DATA_TID6,
    MMDRV_PKT_CLASS_DATA_TID7,
    MMDRV_PKT_CLASS_MGMT,
};

/**
 * Allocates an mmpkt for transmission via @ref mmdrv_tx_frame().
 *
 * Note that the metadata of the allocated mmpkt will reference a @ref mmdrv_tx_metadata
 * struct.
 *
 * When the pool of mmpkt buffers available for TX is exhausted, the driver should pause
 * the TX path using @ref mmdrv_host_set_tx_paused(). Similarly, when the buffers become available
 * again (and assuming the TX path is not otherwise blocked) the driver should unpause the
 * TX path.
 *
 * @param pkt_class         The class of packet to be allocated. See @ref mmdrv_pkt_class.
 * @param space_at_start    Amount of space to allocate at start of mmpkt (for prepend).
 * @param space_at_end      Amount of space to allocate at end of mmpkt (for append).
 *
 * @returns the SKB on success or @c NULL on allocation failure.
 */
struct mmpkt *mmdrv_alloc_mmpkt_for_tx(uint8_t pkt_class,
                                       uint32_t space_at_start,
                                       uint32_t space_at_end);

/**
 * Allocates an mmpkt for fragment chain buffer used for defragmentation.
 *
 * This should return the largest available packet from the RX packet memory pool where the
 * capacity first within the specified range.
 *
 * @param min_capacity      The minimum capacity required. Return @c NULL if there is no packet
 *                          available with at least this capacity.
 * @param max_capacity      The maximum capacity required. The returned mmpkt may have a larger
 *                          capacity but extra capacity will not be used.
 *
 * @returns the mmpkt buffer on success or @c NULL on allocation failure.
 */
struct mmpkt *mmdrv_alloc_mmpkt_for_defrag(uint32_t min_capacity, uint32_t max_capacity);

/**
 * Set the Fragmentation threshold.
 *
 * Maximum length of the frame, beyond which packets must be fragmented into two or more frames.
 *
 * @note Even if the fragmentation threshold is set to 0 (disabled), fragmentation may still occur
 *       if a given packet is too large to be transmitted at the selected rate.
 *
 * @param frag_threshold    The fragmentation threshold (in octets) to set,
 *                          or zero to disable the fragmentation threshold.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_set_frag_threshold(uint32_t frag_threshold);

/**
 * Sets the time after network activity before the STA will notify the AP that it will go to sleep
 * using a QoS Null frame and when the driver will release its veto on chip sleep.
 *
 * @param timeout_ms Timeout after network activity before signaling sleep.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_set_dynamic_ps_timeout(uint32_t timeout_ms);

/**
 * Transmit the given SKB.
 *
 * For each invocation of this function, the driver must have a corresponding call to
 * @ref mmdrv_host_process_tx_status() when transmission completes or is abandoned.
 *
 * @param mmpkt     The SKB to transmit. This function will take ownership of
 *                  the SKB and will either destroy it or pass it back via the
 *                  TX Status handler.
 * @param is_mgmt   Whether the frame to be transmitted is a management frame.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_tx_frame(struct mmpkt *mmpkt, bool is_mgmt);

/**
 * Sends a command to the MM-Chip to tell it to enable WNM sleep.
 *
 * @param vif_id    VIF to enable this feature on.
 * @param enabled   If this is set to @c true the WNM sleep is enabled.
 *                  If @c false the WNM sleep is disabled.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_set_chip_wnm_sleep_enabled(uint16_t vif_id, bool enabled);

/**
 * Retrieve statistics from the chip.
 *
 * @param core_num  The core to retrieve stats for (0-2).
 * @param buf       Pointer to a buffer to put the stats into. The pointer will be updated
 *                  to point to the start of the stats within the buffer.
 * @param len       Pointer to a @c uint32_t that contains the length of @p buf. This will be
 *                  updated to the actual length of the stats.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_get_stats(uint32_t core_num, uint8_t **buf, uint32_t *len);

/**
 * Reset statistics held by the chip.
 *
 * @param core_num  The core whose stats are to be reset (0-2).
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_reset_stats(uint32_t core_num);

/**
 * Trigger an assertion on the specified core of the chip.
 *
 * @param core_id  The id of the core to be asserted (0-3).
 *
 * @returns 0 or @c -ETIMEDOUT on success since cmd will time out after chip assertion,
 *          or an appropriate error code.
 */
int mmdrv_trigger_core_assert(uint32_t core_id);

/** Maximum value for the morse capabilities flags.  */
#define MORSE_CAPS_MAX_FW_VAL (128)

/**
 * Capabilities of the morse device.
 *
 * A subset of flags are signalled from the hw as reported
 * by the fw table. These flags appear between:
 * MORSE_CAPS_FW_START <-> MORSE_CAPS_FW_END
 */
enum morse_caps_flags
{
    MORSE_CAPS_FW_START = 0,
    MORSE_CAPS_2MHZ = MORSE_CAPS_FW_START,
    MORSE_CAPS_4MHZ,
    MORSE_CAPS_8MHZ,
    MORSE_CAPS_16MHZ,
    MORSE_CAPS_SGI,
    MORSE_CAPS_S1G_LONG,
    MORSE_CAPS_TRAVELING_PILOT_ONE_STREAM,
    MORSE_CAPS_TRAVELING_PILOT_TWO_STREAM,
    MORSE_CAPS_MU_BEAMFORMEE,
    MORSE_CAPS_MU_BEAMFORMER,
    MORSE_CAPS_RD_RESPONDER,
    MORSE_CAPS_STA_TYPE_SENSOR,
    MORSE_CAPS_STA_TYPE_NON_SENSOR,
    MORSE_CAPS_GROUP_AID,
    MORSE_CAPS_NON_TIM,
    MORSE_CAPS_TIM_ADE,
    MORSE_CAPS_BAT,
    MORSE_CAPS_DYNAMIC_AID,
    MORSE_CAPS_UPLINK_SYNC,
    MORSE_CAPS_FLOW_CONTROL,
    MORSE_CAPS_AMPDU,
    MORSE_CAPS_AMSDU,
    MORSE_CAPS_1MHZ_CONTROL_RESPONSE_PREAMBLE,
    MORSE_CAPS_PAGE_SLICING,
    MORSE_CAPS_RAW,
    MORSE_CAPS_MCS8,
    MORSE_CAPS_MCS9,
    MORSE_CAPS_ASYMMETRIC_BA_SUPPORT,
    MORSE_CAPS_DAC,
    MORSE_CAPS_CAC,
    MORSE_CAPS_TXOP_SHARING_IMPLICIT_ACK,
    MORSE_CAPS_NDP_PSPOLL,
    MORSE_CAPS_FRAGMENT_BA,
    MORSE_CAPS_OBSS_MITIGATION,
    MORSE_CAPS_TMP_PS_MODE_SWITCH,
    MORSE_CAPS_SECTOR_TRAINING,
    MORSE_CAPS_UNSOLICIT_DYNAMIC_AID,
    MORSE_CAPS_NDP_BEAMFORMING_REPORT,
    MORSE_CAPS_MCS_NEGOTIATION,
    MORSE_CAPS_DUPLICATE_1MHZ,
    MORSE_CAPS_TACK_AS_PSPOLL,
    MORSE_CAPS_PV1,
    MORSE_CAPS_TWT_RESPONDER,
    MORSE_CAPS_TWT_REQUESTER,
    MORSE_CAPS_BDT,
    MORSE_CAPS_TWT_GROUPING,
    MORSE_CAPS_LINK_ADAPTATION_WO_NDP_CMAC,
    MORSE_CAPS_LONG_MPDU,
    MORSE_CAPS_TXOP_SECTORIZATION,
    MORSE_CAPS_GROUP_SECTORIZATION,
    MORSE_CAPS_HTC_VHT,
    MORSE_CAPS_HTC_VHT_MFB,
    MORSE_CAPS_HTC_VHT_MRQ,
    MORSE_CAPS_2SS,
    MORSE_CAPS_3SS,
    MORSE_CAPS_4SS,
    MORSE_CAPS_SU_BEAMFORMEE,
    MORSE_CAPS_SU_BEAMFORMER,
    MORSE_CAPS_RX_STBC,
    MORSE_CAPS_TX_STBC,
    MORSE_CAPS_RX_LDPC,
    MORSE_CAPS_HW_FRAGMENT,
    MORSE_CAPS_FW_END = MORSE_CAPS_MAX_FW_VAL,

    /* Capabilities not filled by FW need to be inserted after
     * MORSE_CAPS_FW_END. These capabilities are allowed to move
     * around within the enum (for example if the CAPS_FW subset expands).
     *
     * As such, their internal integer representation should not be used
     * directly when sending information on air.
     */

    MORSE_CAPS_LAST = MORSE_CAPS_FW_END,
};

/**
 * @code
 * CAPABILITIES_FLAGS_WIDTH = ceil(MORSE_CAPS_LAST / 32)
 * @endcode
 */
#define MORSE_CAPS_FLAGS_WIDTH (4)

/** FW/driver capabilities data structure. */
struct morse_caps
{
    /** Bitmap for capabilities, see @ref morse_caps_flags */
    uint32_t flags[MORSE_CAPS_FLAGS_WIDTH];

    /**
     * The minimum A-MPDU start spacing required by firmware.
     *
     * * Set to 0 for no restriction.
     * * Set to 1 for 1/4 us.
     * * Set to 2 for 1/2 us.
     * * Set to 3 for 1 us.
     * * Set to 4 for 2 us.
     * * Set to 5 for 4 us.
     * * Set to 6 for 8 us.
     * * Set to 7 for 16 us.
     */
    uint8_t ampdu_mss;

    /** The beamformee Space-Time Streams capability value. */
    uint8_t beamformee_sts_capability;

    /** Number of sounding dimensions */
    uint8_t number_sounding_dimensions;

    /** The maximum A-MPDU length. This is the exponent value such that
     * (2^(13 + exponent) - 1) is the length.  */
    uint8_t maximum_ampdu_length_exponent;

    /**
     * Offset to apply to the specification's mmss table to signal further
     * minimum mpdu start spacing.
     */
    uint8_t morse_mmss_offset;
};

/** Reduce verbosity when referencing the firmware flags */
#define MORSE_CAP_SUPPORTED(_caps, _capability) \
    morse_caps_supported(_caps, MORSE_CAPS_##_capability)

/**
 * Check if a capability flag is set, indicating that it is supported.
 *
 * @param caps  The capabilities supported by the chip.
 * @param flag  The capability flag to check
 *
 * @return true if the capability is supported, false if otherwise
 */
static inline bool morse_caps_supported(const struct morse_caps *caps, enum morse_caps_flags flag)
{
    /* (flag/32) gives the word number, (flag%32) gives the bit number */
    unsigned word_num = (flag >> 5);
    unsigned bit_num = (flag & 31);
    const uint32_t mask = 1ul << bit_num;

    MMOSAL_ASSERT(word_num < sizeof(caps->flags) / sizeof(caps->flags[0]));

    return (caps->flags[word_num] & mask) != 0;
}

/**
 * Enables ARP response offload to the chip.
 *
 * When enabled the Morse chip will handle all ARP requests without interrupting or
 * waking up the host processor.
 *
 * @param vif_id    VIF to enable this feature on.
 * @param arp_addr  The IPv4 address to respond with for ARP requests to this interface.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_enable_arp_response_offload(uint16_t vif_id, uint32_t arp_addr);

/**
 * Enables ARP refresh offload to the chip.
 *
 * When enabled the Morse chip will periodically send ARP requests to the AP to refresh
 * its ARP table. This keeps this stations ARP entry from expiring.
 *
 * @param vif_id       VIF to enable this feature on.
 * @param interval_s   The interval in seconds to refresh the ARP entries on the AP.
 * @param dest_ip      The IPv4 address to send the ARP packet to (usually the gateway/router).
 * @param send_as_garp If true, send as a gratuitous ARP packet.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_enable_arp_refresh_offload(uint16_t vif_id,
                                     uint32_t interval_s,
                                     uint32_t dest_ip,
                                     bool send_as_garp);

/**
 * Enables DHCP offload to the chip.
 *
 * When enabled the Morse chip will send DHCP requests and handle responses on behalf of
 * the host processor.
 *
 * @param vif_id      VIF to enable this feature on.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_enable_dhcp_offload(uint16_t vif_id);

/**
 * Enables DHCP discovery.
 *
 * When called the Morse chip will send DHCP requests to perform discovery.
 *
 * @param vif_id      VIF to enable this feature on.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_do_dhcp_discovery(uint16_t vif_id);

/**
 * Sets the TCP keep-alive offload feature.
 *
 * When enabled the Morse chip will periodically send TCP keep-alive packets to the
 * destination IP and port. This keeps the TCP connection from dropping.
 *
 * @param vif_id    VIF to enable this feature on.
 * @param args      TCP keep-alive arguments if enabling, or NULL if disabling.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_set_tcp_keepalive_offload(uint16_t vif_id,
                                    const struct mmwlan_tcp_keepalive_offload_args *args);

/**
 * Enters standby mode.
 *
 * Tells the Morse chip that the host is going to sleep. When in Standby mode the Morse chip
 * will not wake up the host processor unless an event causing us to exit standby mode occurs.
 *
 * @param vif_id        VIF to enable this feature on.
 * @param monitor_bssid The BSSID of the AP to monitor for wake up events from standby mode.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_standby_enter(uint16_t vif_id, const uint8_t monitor_bssid[MMWLAN_MAC_ADDR_LEN]);

/**
 * Exits standby mode.
 *
 * Tells the Morse chip to exit standby mode and resume normal operation.
 *
 * @param vif_id VIF to enable this feature on.
 * @param reason Returns the reason we exited standby mode.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_standby_exit(uint16_t vif_id, uint8_t *reason);

/**
 * Sets the user payload in the standby status packet.
 *
 * @param vif_id      VIF to enable this feature on.
 * @param payload     A pointer to the user data to append to the standby status packets.
 * @param payload_len The length of the payload in bytes.
 *                    See @ref MMWLAN_STANDBY_STATUS_FRAME_USER_PAYLOAD_MAXLEN
 *                    for maximum payload length.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_standby_set_status_payload(uint16_t vif_id, const uint8_t *payload, uint32_t payload_len);

/**
 * Sets the filter to wake from standby mode.
 *
 * If the filter data matches the data in the wake packet at the specified offset,
 * then the Morse chip will exit standby mode.
 *
 * @param vif_id     VIF to enable this feature on.
 * @param filter     A pointer to data to match with for filtering.
 * @param filter_len The length of the filter data in bytes.
 *                   See @ref MMWLAN_STANDBY_WAKE_FRAME_USER_FILTER_MAXLEN
 *                   for maximum filter length.
 * @param offset     The offset within the packet to search for the filter match.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_standby_set_wake_filter(uint16_t vif_id,
                                  const uint8_t *filter,
                                  uint32_t filter_len,
                                  uint32_t offset);

/**
 * Setup the configuration for standby mode.
 *
 * @param vif_id VIF to enable this feature on.
 * @param config Config parameters for standby mode. See @ref mmwlan_standby_config.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_standby_set_config(uint16_t vif_id, const struct mmwlan_standby_config *config);

/**
 * Sets the whitelist filter for standby mode.
 *
 * The whitelist filter specifies which incoming packets should wake the host from standby mode.
 *
 * @param vif_id    VIF to enable this feature on.
 * @param whitelist The whitelist filter to apply, see @ref mmwlan_config_whitelist.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_set_whitelist_filter(uint16_t vif_id, const struct mmwlan_config_whitelist *whitelist);

/**
 * Reads the capabilities into the given struct.
 *
 * @param vif_id VIF to read capabilities for.
 * @param caps   Data structure to receive capabilities.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_get_capabilities(uint16_t vif_id, struct morse_caps *caps);

/**
 * Configure minimum packet spacing window.
 *
 * @param airtime_min_us    The minimum packet airtime duration to trigger spacing.
 * @param airtime_max_us    The maximum allowable packet airtime duration. Set to 0 if no airtime
 * upper bound.
 * @param packet_space_window_length_us  The length of time to close the tx window between packets.
 *
 * @return int 0 on success or an appropriate error code.
 */
int mmdrv_cfg_mpsw(uint32_t airtime_min_us,
                   uint32_t airtime_max_us,
                   uint32_t packet_space_window_length_us);

/**
 * Updates the OUIs list in the chip to filter on when receiving beacons. This means any beacons
 * containing a vendor ie with an OUI in this list will cause the chip to send the beacon to the
 * host. Setting this a second time will override the previous filter list.
 *
 * @param vif_id    The VIF ID of the virtual interface that this applies.
 * @param ouis      Reference to list of OUIs to filter on. Each oui should be @ref MMWLAN_OUI_SIZE
 *                  in length. Set to @c NULL to clear filters.
 * @param n_ouis    Number of OUIs in the list. This can be @ref
 *                  MMWLAN_BEACON_VENDOR_IE_MAX_OUI_FILTERS max.
 *
 * @return int 0 on success or an appropriate error code.
 */
int mmdrv_update_beacon_vendor_ie_filter(uint16_t vif_id, const uint8_t *ouis, uint8_t n_ouis);

/**
 * Sets a specified parameter to the specified value.
 *
 * @param vif_id     VIF to set parameters for
 * @param param_id   Parameter to set
 * @param value      Value to set it to
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_set_param(uint16_t vif_id, enum morse_param_id param_id, uint32_t value);

/**
 * Execute a test/debug command. The format of command and response is opaque to this API.
 *
 * @note The caller must have already verified that the command does not overflow the buffer.
 *
 * @param[in]     command       Buffer containing the command to be executed.
 * @param[out]    response      Buffer to received the response to the command.
 * @param[in,out] response_len  Pointer to a @c uint32_t that is initialized to the length of the
 *                              response buffer. On success, the value will be updated to the
 *                              length of data that was put into the response buffer.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_execute_command(uint8_t *command, uint8_t *response, uint32_t *response_len);

/**
 * Sets whether NDP probe requests should be enabled for the given virtual interface.
 *
 * @param vif_id    The VIF ID of the virtual interface that this applies.
 * @param enabled   Boolean indicating whether NDP probe requests should be enabled.
 *
 * @returns         If successful returns 0 otherwise an integer that corresponds with an error type
 */
int mmdrv_set_ndp_probe(uint16_t vif_id, bool enabled);

/**
 * Index enum used for the sequence number spaces array. See P802.11me D1.1, Section 10.3.2.14.1,
 * Table 10-5/6 for more details.
 */
enum mmdrv_sequnce_num_spaces
{
    /** Start of @c SNS1, frames not covered by any of the other sequence number spaces. Sequence
     * numbers before this point are @c SNS2, individually addressed QoS data. */
    MMDRV_SEQ_NUM_BASELINE = MMWLAN_MAX_QOS_TID + 1,
    /** Start of @c SNS5, frames not covered by any of the other sequence number spaces. */
    MMDRV_SEQ_NUM_QOS_NULL,
    /** Total number of sequence numbers. */
    MMDRV_SEQ_NUM_SPACES,
};

/**
 * Install the given sequence number spaces into the chip.
 *
 * @param vif_id            The VIF ID of the virtual interface that this applies.
 * @param tx_seq_num_spaces Reference to the transmit sequence numbers to install. Must be
 *                          @c MMDRV_SEQ_NUM_SPACES in length.
 * @param addr              MAC address used for the individually addressed spaces. For a station
 *                          this is the BSSID.
 *
 * @returns If successful returns 0 otherwise an integer that corresponds with an error type.
 */
int mmdrv_set_seq_num_spaces(uint16_t vif_id,
                             const uint16_t *tx_seq_num_spaces,
                             const uint8_t *addr);

/*
 * ---------------------------------------------------------------------------------------------
 *      Driver -> Host API
 * ---------------------------------------------------------------------------------------------
 */

/** Enumeration of flag values used by @ref mmdrv_rx_metadata.flags. */
enum mmdrv_rx_metadata_flags
{
    /** Indicates that the packet was received encrypted and was decrypted by the chip. */
    MMDRV_RX_FLAG_DECRYPTED = 0x01,
};

/** Metadata structure for RX mmpkts. */
struct mmdrv_rx_metadata
{
    /** RSSI of the received packet. */
    int16_t rssi;
    /** Frequency at which the packet was received (in 100 kHz). */
    uint16_t freq_100khz;
    /** Bandwidth at which this packet was received (in MHz). */
    uint8_t bw_mhz;
    /** Flags relating to the received packet. See @ref mmdrv_rx_metadata_flags. */
    uint8_t flags;
    /** Background noise at the time the packet was received. */
    int8_t noise_dbm;
    /**
     * VIF ID of the VIF this packet was received on. @c UINT8_MAX indicates invalid.
     *
     * @warning Note that this is an 8 bit value as opposed to the normally used 16 bit size.
     */
    uint8_t vif_id;
    /** Timestamp at which the MPDU was read from the chip. */
    uint32_t read_timestamp_ms;
};

/**
 * Retrieve the metadata from an RX frame.
 *
 * @warning This must only be for RX frames.
 *
 * @param rxbuf The mmpkt containing the received frame to get the attached metadata from.
 *
 * @returns a reference to the RX metadata in @p rxbuf.
 */
static inline struct mmdrv_rx_metadata *mmdrv_get_rx_metadata(struct mmpkt *rxbuf)
{
    struct mmdrv_rx_metadata *metadata = mmpkt_get_metadata(rxbuf).rx;
    MMOSAL_ASSERT(metadata != NULL);
    return metadata;
}

/**
 * Process received frames
 *
 * Metadata will point to an instance of @ref mmdrv_rx_metadata.
 *
 * @param[in] rxbuf    The received packet. Note that this IS consumed by this function.
 * @param channel      The channel that this frame was received on.
 *
 * @warning This function is not reentrant and must always be invoked from the same thread.
 */
void mmdrv_host_process_rx_frame(struct mmpkt *rxbuf, uint16_t channel);

/**
 * Process TX status
 *
 *  @param mmpkt    mmpkt containing tx status information. This mmpkt IS consumed by
 *                  this function.
 */
void mmdrv_host_process_tx_status(struct mmpkt *mmpkt);

/**
 * Enumeration of the different places that might wish to pause the datapath from the driver.
 * This is a bit field, so values must be powers of 2.
 *
 * * Values @c 0x0000-0x007f are reserved for this enum and its potential future expansion.
 * * Values @c 0x0080-0x00ff are available for the driver to use as it wishes.
 * * Values @c 0x0100-0xffff are reserved for the UMAC.
 */
enum mmdrv_pause_source
{
    /** Memory pools exhausted. */
    MMDRV_PAUSE_SOURCE_MASK_PKTMEM = 0x01,
    /** Traffic control signaling from the transceiver. */
    MMDRV_PAUSE_SOURCE_MASK_TRAFFIC_CTRL = 0x02,
    /** Hardware restart in progress. */
    MMDRV_PAUSE_SOURCE_MASK_HW_RESTART = 0x04,
};

/**
 * Pause or unpause transmission.
 *
 * @warning When using this function, a given pause source must always be paused and unpaused
 *          from the same thread. If different threads can pause/unpause a given source then use
 *          @ref mmdrv_host_update_tx_paused().
 *
 * @param sources_mask  Identifies which pause sources should be updated.
 * @param paused        Boolean to determine if the tx path should be paused (true) or unpaused
 *                      (false).
 *
 * @see mmdrv_pause_source
 */
void mmdrv_host_set_tx_paused(uint16_t sources_mask, bool paused);

/**
 * Callback to check pause state, invoked from @ref mmdrv_host_update_tx_paused().
 *
 * @warning This will be invoked from within a critical section.
 *
 * @returns @c true if paused else @c false.
 */
typedef bool (*mmdrv_host_update_tx_paused_cb_t)(void);

/**
 * Updated tx paused state based on the result of a given callback.
 *
 * @note The callback will be called from within a critical section.
 *
 * @param sources_mask  Identifies which pause sources should be updated.
 * @param cb            The callback to invoke to determine whether the given source(s) should
 *                      be paused.
 *
 * @see mmdrv_pause_source
 */
void mmdrv_host_update_tx_paused(uint16_t sources_mask, mmdrv_host_update_tx_paused_cb_t cb);

/**
 * Used by the driver to notify the UMAC that a serious failure has occurred and that the
 * driver needs to be restarted.
 */
void mmdrv_host_hw_restart_required(void);

/**
 * Invoked by driver when informed of beacon loss by the chip.
 *
 * @param num_bcns      The number of beacons that were lost.
 */
void mmdrv_host_beacon_loss(uint32_t num_bcns);

/**
 * Enumeration of the different reasons that may cause a connection loss event.
 */
enum mmdrv_connection_loss_reason
{
    /** Time sync with access point was reset indicating access point restarted. */
    MMDRV_CONNECTION_LOSS_REASON_TSF_RESET = 0,
};

/**
 * Invoked by driver when informed of connection loss by the chip.
 *
 * @param reason        The reason for connection loss.
 *                      See @ref mmdrv_connection_loss_reason.
 */
void mmdrv_host_connection_loss(uint32_t reason);

/**
 * Function invoked by the driver when a DHCP lease update is received.
 *
 * @param ip    IP address assigned in the lease.
 * @param mask  Netmask assigned in the lease.
 * @param gw    Gateway assigned in the lease.
 * @param dns   Primary DNS assigned in the lease.
 */
void mmdrv_host_dhcp_lease_update(uint32_t ip, uint32_t mask, uint32_t gw, uint32_t dns);

/**
 * Function invoked by the driver when a HW scan operation has completed.
 *
 * @param state State of the scan upon completion.
 */
void mmdrv_host_hw_scan_complete(enum mmwlan_scan_state state);

/**
 * Increment counter of number of RX pages dropped due to allocation failures.
 */
void mmdrv_host_stats_increment_datapath_driver_rx_alloc_failures(void);

/**
 * Increment counter of number of RX pages dropped due to read failures. This includes issues
 * where pages are dropped due to being malformed.
 */
void mmdrv_host_stats_increment_datapath_driver_rx_read_failures(void);

/**
 * Notifies the firmware of the listen interval which it should sleep for. Listen interval is
 * defined in upper mac, but the firmware is not aware of the value until this function has been
 * called.
 *
 * @param vif             VIF ID of the given interface.
 * @param listen_interval Interval value in beacon or short beacon (if enabled) units. e.g. 5 will
 *                        set the listen interval to the interval between 5 beacons.
 *
 * @returns @c MMWLAN_SUCCESS on success, else an appropriate error code.
 */
int mmdrv_set_listen_interval_sleep(uint16_t vif, uint16_t listen_interval);

/**
 * Retrieve a beacon for transmission.
 *
 * @returns An mmpkt containing the beacon to be transmitted (or @c NULL on failure).
 */
struct mmpkt *mmdrv_host_get_beacon(void);

/**
 * Set the bandwidth of control response preambles.
 *
 * @param vif        The virtual interface to change.
 * @param direction  The direction of the control response frames being sent/received.
 * @param cr_1mhz_en Whether control responses are in 1MHz or not. True to enable 1MHz bandwidth,
 *                   false to disable this override.
 *
 * @returns 0 on success or an appropriate error code.
 */
int mmdrv_set_control_response_bw(uint16_t vif, enum mmdrv_direction direction, bool cr_1mhz_en);

#ifdef __cplusplus
}
#endif

/** @} */
