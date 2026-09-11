/*
 * Copyright 2021-2025 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */



#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "mmosal.h"
#include "mmpkt.h"
#include "mmutils.h"
#include "mmwlan.h"

#include "mmrc.h"

#ifdef __cplusplus
extern "C"
{
#endif




enum morse_sta_state
{
    MORSE_STA_NOTEXIST = 0,
    MORSE_STA_NONE = 1,
    MORSE_STA_AUTHENTICATED = 2,
    MORSE_STA_ASSOCIATED = 3,
    MORSE_STA_AUTHORIZED = 4,
    MORSE_STA_MAX = UINT16_MAX
};


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


enum mmdrv_direction
{
    MMDRV_DIRECTION_OUTGOING = 0,
    MMDRV_DIRECTION_INCOMING = 1
};


#define MORSE_KEY_MAXLEN (32)


#define TWT_MAX_AGREEMENT_LEN (20)


#define MMDRV_VIF_ID_INVALID (0xffff)


struct mmdrv_key_conf
{

    bool is_pairwise;

    uint64_t tx_pn;

    uint16_t length;

    uint8_t key[MORSE_KEY_MAXLEN];

    uint8_t key_idx;
};


struct mmdrv_twt_data
{

    uint16_t interface_id;

    uint8_t flow_id;

    uint8_t agreement_len;

    const uint8_t *agreement;
};


struct mmdrv_fw_version
{

    uint8_t major;

    uint8_t minor;

    uint8_t patch;
};


struct mmdrv_chip_info
{

    uint8_t mac_addr[MMWLAN_MAC_ADDR_LEN];

    struct mmdrv_fw_version fw_version;

    uint32_t morse_chip_id;

    const char *morse_chip_id_string;
};


void mmdrv_pre_init(void);


void mmdrv_post_deinit(void);


enum mmwlan_status mmdrv_init(struct mmdrv_chip_info *chip_info, const char *country_code);

#ifdef UNIT_TESTS

void mmdrv_init_for_unit_tests(void);
#endif


void mmdrv_deinit(void);


enum mmwlan_status mmdrv_get_bcf_metadata(struct mmwlan_bcf_metadata *metadata);

#define MMDRV_DUTY_CYCLE_MIN (1lu)
#define MMDRV_DUTY_CYCLE_MAX (10000lu)


enum mmwlan_status mmdrv_set_duty_cycle(uint32_t duty_cycle,
                                        bool duty_cycle_omit_ctrl_resp,
                                        enum mmwlan_duty_cycle_mode mode);


enum mmwlan_status mmdrv_get_duty_cycle(struct mmwlan_duty_cycle_stats *stats);


void mmdrv_read_mac_addr(uint8_t *mac_addr);


enum mmwlan_status mmdrv_set_txpower(int32_t *out_power_dbm, int txpower_dbm);


enum mmdrv_interface_type
{

    MMDRV_INTERFACE_TYPE_STA = 1,

    MMDRV_INTERFACE_TYPE_AP = 2,
};


enum mmwlan_status mmdrv_add_if(uint16_t *vif_id,
                                const uint8_t *addr,
                                enum mmdrv_interface_type type);


enum mmwlan_status mmdrv_rm_if(uint16_t vif_id);


enum mmwlan_status mmdrv_start_beaconing(uint16_t vif_id);


enum mmwlan_status mmdrv_stop_beaconing(void);


enum mmwlan_status mmdrv_cfg_scan(bool enabled);


enum mmwlan_status
mmdrv_twt_agreement_install_req(const struct mmdrv_twt_data *twt_data);


enum mmwlan_status
mmdrv_twt_agreement_validate_req(const struct mmdrv_twt_data *twt_data);


enum mmwlan_status mmdrv_twt_remove_req(const struct mmdrv_twt_data *twt_data);


enum mmwlan_status mmdrv_set_channel(uint32_t op_chan_freq_hz,
                                     uint8_t pri_1mhz_chan_idx,
                                     uint8_t op_bw_mhz,
                                     uint8_t pri_bw_mhz,
                                     bool is_off_channel);


enum mmwlan_status mmdrv_cfg_bss(uint16_t vif_id,
                                 uint16_t beacon_int,
                                 uint16_t dtim_period,
                                 uint32_t cssid);


enum mmwlan_status mmdrv_update_sta_state(uint16_t vif_id,
                                          uint16_t aid,
                                          const uint8_t *addr,
                                          enum morse_sta_state state);


enum mmwlan_status mmdrv_install_key(uint16_t vif_id,
                                     uint16_t aid,
                                     struct mmdrv_key_conf *key_conf);


enum mmwlan_status mmdrv_disable_key(uint16_t vif_id,
                                     uint16_t aid,
                                     uint8_t hw_key_idx,
                                     bool is_pairwise);


enum mmwlan_status mmdrv_cfg_qos_queue(const struct mmwlan_qos_queue_params *params);


enum mmwlan_status mmdrv_set_wake_enabled(bool enabled);


enum mmwlan_status mmdrv_set_chip_power_save_enabled(uint16_t vif_id, bool enabled);


enum mmwlan_status mmdrv_health_check(void);


void mmdrv_hw_restart_completed(void);


enum mmdrv_tx_metadata_flags
{

    MMDRV_TX_FLAG_AMPDU_ENABLED = 0x01,

    MMDRV_TX_FLAG_IMMEDIATE_REPORT = 0x02,

    MMDRV_TX_FLAG_HW_ENC = 0x04,

    MMDRV_TX_FLAG_TP_ENABLED = 0x08,

    MMDRV_TX_FLAG_NO_ACK = 0x10,

    MMDRV_TX_FLAG_CR_1MHZ_PRE_ENABLED = 0x20,
};


enum mmdrv_tx_metadata_status_flags
{

    MMDRV_TX_STATUS_FLAG_NO_ACK = 0x10,

    MMDRV_TX_STATUS_FLAG_PS_FILTERED = 0x20,

    MMDRV_TX_STATUS_DUTY_CYCLE_CANT_SEND = 0x40,

    MMDRV_TX_STATUS_WAS_AGGREGATED = 0x80
};


#define MMDRV_TX_STATUS_FLAGS_FAIL_MASK \
    (MMDRV_TX_STATUS_FLAG_NO_ACK |      \
     MMDRV_TX_STATUS_FLAG_PS_FILTERED | \
     MMDRV_TX_STATUS_DUTY_CYCLE_CANT_SEND)


struct mmdrv_tx_metadata
{



    uint32_t timeout_abs_ms;




    struct mmrc_rate_table rc_data;


    uint8_t vif_id;

    uint8_t key_idx;

    uint8_t tid;

    uint8_t tid_max_reorder_buf_size;

    uint8_t flags;

    uint8_t ampdu_mss;

    uint8_t mmss_offset;




    uint8_t status_flags;

    uint8_t attempts;




    uint8_t tail_padding;




    uint16_t aid;


    uint8_t enc;


    bool is_mgmt;
};


static inline MM_ALWAYS_INLINE struct mmdrv_tx_metadata *mmdrv_get_tx_metadata(struct mmpkt *txbuf)
{
    struct mmdrv_tx_metadata *metadata = mmpkt_get_metadata(txbuf).tx;
    MMOSAL_ASSERT(metadata != NULL);
    return metadata;
}


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


struct mmpkt *mmdrv_alloc_mmpkt_for_tx(uint8_t pkt_class,
                                       uint32_t space_at_start,
                                       uint32_t space_at_end);


struct mmpkt *mmdrv_alloc_mmpkt_for_defrag(uint32_t min_capacity, uint32_t max_capacity);


enum mmwlan_status mmdrv_set_frag_threshold(uint32_t frag_threshold);


enum mmwlan_status mmdrv_set_dynamic_ps_timeout(uint32_t timeout_ms);


enum mmwlan_status mmdrv_tx_frame(struct mmpkt *mmpkt, bool is_mgmt);


enum mmwlan_status mmdrv_set_chip_wnm_sleep_enabled(uint16_t vif_id, bool enabled);


enum mmwlan_status mmdrv_get_stats(uint32_t core_num, uint8_t **buf, uint32_t *len);


enum mmwlan_status mmdrv_reset_stats(uint32_t core_num);


enum mmwlan_status mmdrv_trigger_core_assert(uint32_t core_id);


#define MORSE_CAPS_MAX_FW_VAL (128)


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



    MORSE_CAPS_LAST = MORSE_CAPS_FW_END,
};


#define MORSE_CAPS_FLAGS_WIDTH (4)


struct morse_caps
{

    uint32_t flags[MORSE_CAPS_FLAGS_WIDTH];


    uint8_t ampdu_mss;


    uint8_t beamformee_sts_capability;


    uint8_t number_sounding_dimensions;


    uint8_t maximum_ampdu_length_exponent;


    uint8_t morse_mmss_offset;
};


#define MORSE_CAP_SUPPORTED(_caps, _capability) \
    morse_caps_supported(_caps, MORSE_CAPS_##_capability)


inline bool morse_caps_supported(const struct morse_caps *caps, enum morse_caps_flags flag)
{

    unsigned word_num = (flag >> 5);
    unsigned bit_num = (flag & 31);
    const uint32_t mask = 1ul << bit_num;

    MMOSAL_ASSERT(word_num < sizeof(caps->flags) / sizeof(caps->flags[0]));

    return (caps->flags[word_num] & mask) != 0;
}


enum mmwlan_status
mmdrv_get_capabilities(uint16_t vif_id, struct morse_caps *caps);


enum mmwlan_status mmdrv_cfg_mpsw(uint32_t airtime_min_us,
                                  uint32_t airtime_max_us,
                                  uint32_t packet_space_window_length_us);


enum mmwlan_status mmdrv_update_beacon_vendor_ie_filter(uint16_t vif_id,
                                                        const uint8_t *ouis,
                                                        uint8_t n_ouis);


enum mmwlan_status mmdrv_set_param(uint16_t vif_id, enum morse_param_id param_id, uint32_t value);


enum mmwlan_status mmdrv_execute_command(uint8_t *command,
                                         uint8_t *response,
                                         uint32_t *response_len);


enum mmwlan_status mmdrv_set_cqm_rssi(uint16_t vif_id, int32_t threshold, uint32_t hysteresis);


enum mmwlan_status mmdrv_set_ndp_probe(uint16_t vif_id, bool enabled);


enum mmdrv_sequnce_num_spaces
{

    MMDRV_SEQ_NUM_BASELINE = MMWLAN_MAX_QOS_TID + 1,

    MMDRV_SEQ_NUM_QOS_NULL,

    MMDRV_SEQ_NUM_SPACES,
};


enum mmwlan_status mmdrv_set_seq_num_spaces(uint16_t vif_id,
                                            const uint16_t *tx_seq_num_spaces,
                                            const uint8_t *addr);


struct mmdrv_hw_scan_params
{

    uint32_t flags;

    uint32_t dwell_time_ms;

    uint8_t *tlvs;

    uint32_t tlvs_len;
};


enum mmwlan_status mmdrv_hw_scan(uint16_t vif_id, struct mmdrv_hw_scan_params *params);


enum mmwlan_status mmdrv_set_listen_interval_sleep(uint16_t vif, uint16_t listen_interval);


enum mmwlan_status mmdrv_set_control_response_bw(uint16_t vif,
                                                 enum mmdrv_direction direction,
                                                 bool cr_1mhz_en);




enum mmdrv_rx_metadata_flags
{

    MMDRV_RX_FLAG_DECRYPTED = 0x01,
};


struct mmdrv_rx_metadata
{

    int16_t rssi;

    uint16_t freq_100khz;

    uint8_t bw_mhz;

    uint8_t flags;

    int8_t noise_dbm;

    uint8_t vif_id;

    uint32_t read_timestamp_ms;
};


static inline struct mmdrv_rx_metadata *mmdrv_get_rx_metadata(struct mmpkt *rxbuf)
{
    struct mmdrv_rx_metadata *metadata = mmpkt_get_metadata(rxbuf).rx;
    MMOSAL_ASSERT(metadata != NULL);
    return metadata;
}


void mmdrv_host_process_rx_frame(struct mmpkt *rxbuf, uint16_t channel);


void mmdrv_host_process_tx_status(struct mmpkt *mmpkt);


enum mmdrv_pause_source
{

    MMDRV_PAUSE_SOURCE_MASK_PKTMEM = 0x01,

    MMDRV_PAUSE_SOURCE_MASK_TRAFFIC_CTRL = 0x02,

    MMDRV_PAUSE_SOURCE_MASK_HW_RESTART = 0x04,
};


void mmdrv_host_set_tx_paused(uint16_t sources_mask, bool paused);


typedef bool (*mmdrv_host_update_tx_paused_cb_t)(void);


void mmdrv_host_update_tx_paused(uint16_t sources_mask, mmdrv_host_update_tx_paused_cb_t cb);


void mmdrv_host_hw_restart_required(void);


void mmdrv_host_health_check_required(void);


void mmdrv_host_beacon_loss(uint32_t num_bcns);


enum mmdrv_connection_loss_reason
{

    MMDRV_CONNECTION_LOSS_REASON_TSF_RESET = 0,
};


void mmdrv_host_connection_loss(uint32_t reason);


enum mmdrv_cqm_event
{

    MMDRV_CQM_EVENT_RSSI_THRESHOLD_LOW = 0,

    MMDRV_CQM_EVENT_RSSI_THRESHOLD_HIGH = 1,
};


void mmdrv_host_cqm_event(enum mmdrv_cqm_event event, int16_t rssi);


void mmdrv_host_hw_scan_complete(enum mmwlan_scan_state state);


void mmdrv_host_stats_increment_datapath_driver_rx_alloc_failures(void);


void mmdrv_host_stats_increment_datapath_driver_rx_read_failures(void);


void mmdrv_host_stats_increment_datapath_driver_tx_skbq_timeout(void);


void mmdrv_host_stats_increment_datapath_driver_tx_pending_status_timeout(void);


struct mmpkt *mmdrv_host_get_beacon(void);

#ifdef __cplusplus
}
#endif


