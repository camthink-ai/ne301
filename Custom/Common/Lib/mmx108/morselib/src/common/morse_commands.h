/*
 * Copyright 2025-2026 Morse Micro
 */



#pragma once

#include "common/common.h"

#define MORSE_CMD_SEMVER_MAJOR 56
#define MORSE_CMD_SEMVER_MINOR 18
#define MORSE_CMD_SEMVER_PATCH 0

enum morse_cmd_id
{

    MORSE_CMD_ID_SET_CHANNEL = 0x0001,
    MORSE_CMD_ID_GET_VERSION = 0x0002,
    MORSE_CMD_ID_SET_TXPOWER = 0x0003,
    MORSE_CMD_ID_ADD_INTERFACE = 0x0004,
    MORSE_CMD_ID_REMOVE_INTERFACE = 0x0005,
    MORSE_CMD_ID_BSS_CONFIG = 0x0006,
    MORSE_CMD_ID_INSTALL_KEY = 0x000A,
    MORSE_CMD_ID_DISABLE_KEY = 0x000B,
    MORSE_CMD_ID_SCAN_CONFIG = 0x0010,
    MORSE_CMD_ID_SET_QOS_PARAMS = 0x0011,
    MORSE_CMD_ID_GET_CHANNEL_FULL = 0x0013,
    MORSE_CMD_ID_SET_STA_STATE = 0x0014,
    MORSE_CMD_ID_SET_BSS_COLOR = 0x0015,
    MORSE_CMD_ID_CONFIG_PS = 0x0016,
    MORSE_CMD_ID_HEALTH_CHECK = 0x0019,
    MORSE_CMD_ID_GET_CHANNEL_DTIM = 0x001C,
    MORSE_CMD_ID_GET_CHANNEL = 0x001D,
    MORSE_CMD_ID_ARP_OFFLOAD = 0x0020,
    MORSE_CMD_ID_SET_LONG_SLEEP_CONFIG = 0x0021,
    MORSE_CMD_ID_SET_DUTY_CYCLE = 0x0022,
    MORSE_CMD_ID_GET_DUTY_CYCLE = 0x0023,
    MORSE_CMD_ID_GET_MAX_TXPOWER = 0x0024,
    MORSE_CMD_ID_GET_CAPABILITIES = 0x0025,
    MORSE_CMD_ID_TWT_AGREEMENT_INSTALL = 0x0026,
    MORSE_CMD_ID_TWT_AGREEMENT_REMOVE = 0x0027,
    MORSE_CMD_ID_MPSW_CONFIG = 0x0030,
    MORSE_CMD_ID_STANDBY_MODE = 0x0031,
    MORSE_CMD_ID_DHCP_OFFLOAD = 0x0032,
    MORSE_CMD_ID_UPDATE_OUI_FILTER = 0x0034,
    MORSE_CMD_ID_TWT_AGREEMENT_VALIDATE = 0x0036,
    MORSE_CMD_ID_HW_SCAN = 0x0044,
    MORSE_CMD_ID_SET_WHITELIST = 0x0045,
    MORSE_CMD_ID_ARP_PERIODIC_REFRESH = 0x0046,
    MORSE_CMD_ID_SET_TCP_KEEPALIVE = 0x0047,
    MORSE_CMD_ID_LI_SLEEP = 0x0049,
    MORSE_CMD_ID_SEQUENCE_NUMBER_SPACES = 0x004B,
    MORSE_CMD_ID_SET_CQM_RSSI = 0x004F,


    MORSE_CMD_ID_HOST_STATS_LOG = 0x2007,
    MORSE_CMD_ID_HOST_STATS_RESET = 0x2008,
    MORSE_CMD_ID_MAC_STATS_LOG = 0x200C,
    MORSE_CMD_ID_MAC_STATS_RESET = 0x200D,
    MORSE_CMD_ID_UPHY_STATS_LOG = 0x200E,
    MORSE_CMD_ID_UPHY_STATS_RESET = 0x200F,


    MORSE_CMD_ID_IOT_CONFIGURE_INTEROP = 0xB000,
    MORSE_CMD_ID_IOT_SEND_ADDBA = 0xB001,
    MORSE_CMD_ID_IOT_STA_REASSOC = 0xB002,
    MORSE_CMD_ID_IOT_DUMP_STATS = 0xBF00,
    MORSE_CMD_ID_IOT_READ_STATS = 0xBF01,


    MORSE_CMD_ID_SET_CONTROL_RESPONSE = 0x1009,


    MORSE_CMD_ID_EVT_BEACON_LOSS = 0x4002,
    MORSE_CMD_ID_EVT_UMAC_TRAFFIC_CONTROL = 0x4004,
    MORSE_CMD_ID_EVT_DHCP_LEASE_UPDATE = 0x4005,
    MORSE_CMD_ID_EVT_HW_SCAN_DONE = 0x4011,
    MORSE_CMD_ID_EVT_CONNECTION_LOSS = 0x4013,
    MORSE_CMD_ID_EVT_CQM_RSSI_NOTIFY = 0x4015,


    MORSE_CMD_ID_SET_RESPONSE_INDICATION = 0x8007,
    MORSE_CMD_ID_SET_TRANSMISSION_RATE = 0x8009,
    MORSE_CMD_ID_SET_NDP_PROBE_SUPPORT = 0x800C,
    MORSE_CMD_ID_FORCE_ASSERT = 0x800E,


    MORSE_CMD_ID_GET_SET_GENERIC_PARAM = 0x003E,


    MORSE_CMD_ID_TURBO_MODE = 0x0018,
    MORSE_CMD_ID_SET_FRAG_THRESHOLD = 0x0037,
};

#define MORSE_CMD_TYPE_REQ     BIT(0)
#define MORSE_CMD_TYPE_RESP    BIT(1)
#define MORSE_CMD_TYPE_EVT     BIT(2)

#define MORSE_CMD_SSID_MAX_LEN 32
#define MORSE_CMD_MAC_ADDR_LEN 6


struct MM_PACKED morse_cmd_mac_addr
{
    uint8_t octet[MORSE_CMD_MAC_ADDR_LEN];
};


enum morse_cmd_ocs_subcmd
{
    MORSE_CMD_OCS_SUBCMD_CONFIG = 1,
    MORSE_CMD_OCS_SUBCMD_STATUS = 2,
};


struct MM_PACKED morse_cmd_header
{

    uint16_t flags;

    uint16_t message_id;

    uint16_t len;

    uint16_t host_id;

    uint16_t vif_id;

    uint16_t pad;
};


struct MM_PACKED morse_cmd_req
{
    struct morse_cmd_header hdr;
    uint8_t data[];
};


struct MM_PACKED morse_cmd_resp
{
    struct morse_cmd_header hdr;
    uint32_t status;
    uint8_t data[];
};


#define MORSE_CMD_CHANNEL_BW_NOT_SET 0xFF


#define MORSE_CMD_CHANNEL_IDX_NOT_SET 0xFF


#define MORSE_CMD_CHANNEL_FREQ_NOT_SET 0xFFFFFFFF


enum morse_cmd_dot11_proto_mode
{

    MORSE_CMD_DOT11_PROTO_MODE_AH = 0,

    MORSE_CMD_DOT11_PROTO_MODE_B = 1,

    MORSE_CMD_DOT11_PROTO_MODE_BG = 2,

    MORSE_CMD_DOT11_PROTO_MODE_GN = 3,

    MORSE_CMD_DOT11_PROTO_MODE_BGN = 4,

    MORSE_CMD_DOT11_PROTO_MODE_INVALID = 5,
};


struct MM_PACKED morse_cmd_req_set_channel
{
    struct morse_cmd_header hdr;

    uint32_t op_chan_freq_hz;

    uint8_t op_bw_mhz;

    uint8_t pri_bw_mhz;

    uint8_t pri_1mhz_chan_idx;

    uint8_t dot11_mode;

    uint8_t __deprecated_reg_tx_power_set;

    uint8_t is_off_channel;
};


struct MM_PACKED morse_cmd_resp_set_channel
{
    struct morse_cmd_header hdr;
    uint32_t status;

    int32_t power_qdbm;
};


struct MM_PACKED morse_cmd_req_get_channel
{
    struct morse_cmd_header hdr;
};


struct MM_PACKED morse_cmd_resp_get_channel
{
    struct morse_cmd_header hdr;
    uint32_t status;

    uint32_t op_chan_freq_hz;

    uint8_t op_chan_bw_mhz;

    uint8_t pri_chan_bw_mhz;

    uint8_t pri_1mhz_chan_idx;
};


#define MORSE_CMD_MAX_VERSION_LEN 128


struct MM_PACKED morse_cmd_req_get_version
{
    struct morse_cmd_header hdr;
};


struct MM_PACKED morse_cmd_resp_get_version
{
    struct morse_cmd_header hdr;
    uint32_t status;

    int32_t length;

    uint8_t version[];
};


struct MM_PACKED morse_cmd_req_set_txpower
{
    struct morse_cmd_header hdr;

    int32_t power_qdbm;
};


struct MM_PACKED morse_cmd_resp_set_txpower
{
    struct morse_cmd_header hdr;
    uint32_t status;

    int32_t power_qdbm;
};


struct MM_PACKED morse_cmd_req_get_max_txpower
{
    struct morse_cmd_header hdr;
};


struct MM_PACKED morse_cmd_resp_get_max_txpower
{
    struct morse_cmd_header hdr;
    uint32_t status;

    int32_t power_qdbm;
};


enum morse_cmd_interface_type
{

    MORSE_CMD_INTERFACE_TYPE_INVALID = 0,

    MORSE_CMD_INTERFACE_TYPE_STA = 1,

    MORSE_CMD_INTERFACE_TYPE_AP = 2,

    MORSE_CMD_INTERFACE_TYPE_MON = 3,

    MORSE_CMD_INTERFACE_TYPE_ADHOC = 4,

    MORSE_CMD_INTERFACE_TYPE_MESH = 5,

    MORSE_CMD_INTERFACE_TYPE_LAST = MORSE_CMD_INTERFACE_TYPE_MESH,
};


struct MM_PACKED morse_cmd_req_add_interface
{
    struct morse_cmd_header hdr;

    struct morse_cmd_mac_addr addr;

    uint32_t interface_type;
};


struct MM_PACKED morse_cmd_resp_add_interface
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_remove_interface
{
    struct morse_cmd_header hdr;
};


struct MM_PACKED morse_cmd_resp_remove_interface
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_bss_config
{
    struct morse_cmd_header hdr;

    uint16_t beacon_interval_tu;

    uint16_t dtim_period;

    uint8_t __padding[2];

    uint32_t cssid;
};


struct MM_PACKED morse_cmd_resp_bss_config
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_scan_config
{
    struct morse_cmd_header hdr;

    uint8_t enabled;

    uint8_t is_survey;
};


struct MM_PACKED morse_cmd_resp_scan_config
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_set_qos_params
{
    struct morse_cmd_header hdr;

    uint8_t uapsd;

    uint8_t queue_idx;

    uint8_t aifs_slot_count;

    uint16_t contention_window_min;

    uint16_t contention_window_max;

    uint32_t max_txop_usec;
};


struct MM_PACKED morse_cmd_resp_set_qos_params
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


#define MORSE_CMD_STA_FLAG_S1G_PV1 BIT(0)


struct MM_PACKED morse_cmd_req_set_sta_state
{
    struct morse_cmd_header hdr;

    uint8_t sta_addr[MORSE_CMD_MAC_ADDR_LEN];

    uint16_t aid;

    uint16_t state;

    uint8_t uapsd_queues;

    uint32_t flags;
};


struct MM_PACKED morse_cmd_resp_set_sta_state
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_set_bss_color
{
    struct morse_cmd_header hdr;

    uint8_t bss_color;
};


struct MM_PACKED morse_cmd_resp_set_bss_color
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_config_ps
{
    struct morse_cmd_header hdr;

    uint8_t enabled;
    uint8_t dynamic_ps_offload;
};


struct MM_PACKED morse_cmd_resp_config_ps
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_health_check
{
    struct morse_cmd_header hdr;
};


struct MM_PACKED morse_cmd_resp_health_check
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


#define MORSE_CMD_ARP_OFFLOAD_MAX_IP_ADDRESSES 4


struct MM_PACKED morse_cmd_req_arp_offload
{
    struct morse_cmd_header hdr;
    uint32_t ip_table[MORSE_CMD_ARP_OFFLOAD_MAX_IP_ADDRESSES];
};


struct MM_PACKED morse_cmd_resp_arp_offload
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_set_long_sleep_config
{
    struct morse_cmd_header hdr;

    uint8_t enabled;
};


struct MM_PACKED morse_cmd_resp_set_long_sleep_config
{
    struct morse_cmd_header hdr;
    uint32_t status;
};

#define MORSE_CMD_DUTY_CYCLE_SET_CFG_DUTY_CYCLE        BIT(0)
#define MORSE_CMD_DUTY_CYCLE_SET_CFG_OMIT_CONTROL_RESP BIT(1)
#define MORSE_CMD_DUTY_CYCLE_SET_CFG_EXT               BIT(2)
#define MORSE_CMD_DUTY_CYCLE_SET_CFG_BURST_RECORD_UNIT BIT(3)


enum morse_cmd_duty_cycle_mode
{
    MORSE_CMD_DUTY_CYCLE_MODE_SPREAD = 0,
    MORSE_CMD_DUTY_CYCLE_MODE_BURST = 1,
    MORSE_CMD_DUTY_CYCLE_MODE_LAST = MORSE_CMD_DUTY_CYCLE_MODE_BURST,
};


struct MM_PACKED morse_cmd_duty_cycle_configuration
{

    uint8_t omit_control_responses;

    uint32_t duty_cycle;
};


struct MM_PACKED morse_cmd_duty_cycle_set_configuration_ext
{

    uint32_t burst_record_unit_us;

    uint8_t mode;
};


struct MM_PACKED morse_cmd_duty_cycle_configuration_ext
{

    uint32_t airtime_remaining_us;

    uint32_t burst_window_duration_us;

    struct morse_cmd_duty_cycle_set_configuration_ext set;
};


struct MM_PACKED morse_cmd_req_set_duty_cycle
{
    struct morse_cmd_header hdr;
    struct morse_cmd_duty_cycle_configuration config;
    uint8_t set_cfgs;
    struct morse_cmd_duty_cycle_set_configuration_ext config_ext;
};


struct MM_PACKED morse_cmd_resp_get_duty_cycle
{
    struct morse_cmd_header hdr;
    uint32_t status;
    struct morse_cmd_duty_cycle_configuration config;
    struct morse_cmd_duty_cycle_configuration_ext config_ext;
};

#define MORSE_CMD_SET_S1G_CAP_FLAGS          BIT(0)
#define MORSE_CMD_SET_S1G_CAP_AMPDU_MSS      BIT(1)
#define MORSE_CMD_SET_S1G_CAP_BEAM_STS       BIT(2)
#define MORSE_CMD_SET_S1G_CAP_NUM_SOUND_DIMS BIT(3)
#define MORSE_CMD_SET_S1G_CAP_MAX_AMPDU_LEXP BIT(4)
#define MORSE_CMD_SET_MORSE_CAP_MMSS_OFFSET  BIT(5)
#define MORSE_CMD_S1G_CAPABILITY_FLAGS_WIDTH 4


struct MM_PACKED morse_cmd_mm_capabilities
{

    uint32_t flags[MORSE_CMD_S1G_CAPABILITY_FLAGS_WIDTH];

    uint8_t ampdu_mss;

    uint8_t beamformee_sts_capability;

    uint8_t number_sounding_dimensions;

    uint8_t maximum_ampdu_length_exponent;
};


struct MM_PACKED morse_cmd_req_get_capabilities
{
    struct morse_cmd_header hdr;
};


struct MM_PACKED morse_cmd_resp_get_capabilities
{
    struct morse_cmd_header hdr;
    uint32_t status;
    struct morse_cmd_mm_capabilities capabilities;

    uint8_t morse_mmss_offset;
};

#define MORSE_CMD_DOT11_TWT_AGREEMENT_MAX_LEN 20


struct MM_PACKED morse_cmd_req_twt_agreement_install
{
    struct morse_cmd_header hdr;

    uint8_t flow_id;

    uint8_t agreement_len;

    uint8_t agreement[MORSE_CMD_DOT11_TWT_AGREEMENT_MAX_LEN];
};


struct MM_PACKED morse_cmd_resp_twt_agreement_install
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_twt_agreement_validate
{
    struct morse_cmd_header hdr;

    uint8_t flow_id;

    uint8_t agreement_len;

    uint8_t agreement[MORSE_CMD_DOT11_TWT_AGREEMENT_MAX_LEN];
};


struct MM_PACKED morse_cmd_resp_twt_agreement_validate
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_twt_agreement_remove
{
    struct morse_cmd_header hdr;

    uint8_t flow_id;
};

#define MORSE_CMD_SET_MPSW_CFG_AIRTIME_BOUNDS  BIT(0)
#define MORSE_CMD_SET_MPSW_CFG_PKT_SPC_WIN_LEN BIT(1)
#define MORSE_CMD_SET_MPSW_CFG_ENABLED         BIT(2)


struct MM_PACKED morse_cmd_mpsw_configuration
{

    uint32_t airtime_max_us;

    uint32_t airtime_min_us;

    uint32_t packet_space_window_length_us;

    uint8_t enable;
};


struct MM_PACKED morse_cmd_req_mpsw_config
{
    struct morse_cmd_header hdr;
    struct morse_cmd_mpsw_configuration config;
    uint8_t set_cfgs;
};


struct MM_PACKED morse_cmd_resp_mpsw_config
{
    struct morse_cmd_header hdr;
    uint32_t status;
    struct morse_cmd_mpsw_configuration config;
};


#define MORSE_CMD_MAX_KEY_LEN 32


enum morse_cmd_key_cipher
{
    MORSE_CMD_KEY_CIPHER_INVALID = 0,
    MORSE_CMD_KEY_CIPHER_AES_CCM = 1,
    MORSE_CMD_KEY_CIPHER_AES_GCM = 2,
    MORSE_CMD_KEY_CIPHER_AES_CMAC = 3,
    MORSE_CMD_KEY_CIPHER_AES_GMAC = 4,
    MORSE_CMD_KEY_CIPHER_LAST = MORSE_CMD_KEY_CIPHER_AES_GMAC,
};


enum morse_cmd_aes_key_len
{
    MORSE_CMD_AES_KEY_LEN_INVALID = 0,
    MORSE_CMD_AES_KEY_LEN_LENGTH_128 = 1,
    MORSE_CMD_AES_KEY_LEN_LENGTH_256 = 2,
    MORSE_CMD_AES_KEY_LEN_LENGTH_LAST = MORSE_CMD_AES_KEY_LEN_LENGTH_256,
};


enum morse_cmd_temporal_key_type
{
    MORSE_CMD_TEMPORAL_KEY_TYPE_INVALID = 0,
    MORSE_CMD_TEMPORAL_KEY_TYPE_GTK = 1,
    MORSE_CMD_TEMPORAL_KEY_TYPE_PTK = 2,
    MORSE_CMD_TEMPORAL_KEY_TYPE_IGTK = 3,
    MORSE_CMD_TEMPORAL_KEY_TYPE_LAST = MORSE_CMD_TEMPORAL_KEY_TYPE_IGTK,
};


struct MM_PACKED morse_cmd_req_install_key
{
    struct morse_cmd_header hdr;

    uint64_t pn;

    uint32_t aid;

    uint8_t key_idx;

    uint8_t cipher;

    uint8_t key_length;

    uint8_t key_type;

    uint8_t __padding[2];

    uint8_t key[MORSE_CMD_MAX_KEY_LEN];
};


struct MM_PACKED morse_cmd_resp_install_key
{
    struct morse_cmd_header hdr;
    uint32_t status;

    uint8_t key_idx;
};


struct MM_PACKED morse_cmd_req_disable_key
{
    struct morse_cmd_header hdr;

    uint32_t key_type;

    uint32_t aid;

    uint8_t key_idx;
};


struct MM_PACKED morse_cmd_resp_disable_key
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


#define MORSE_CMD_STANDBY_STATUS_FRAME_USER_PAYLOAD_MAX_LEN 64


#define MORSE_CMD_STANDBY_WAKE_FRAME_USER_FILTER_MAX_LEN 64


enum morse_cmd_standby_mode
{

    MORSE_CMD_STANDBY_MODE_EXIT = 0,

    MORSE_CMD_STANDBY_MODE_ENTER = 1,

    MORSE_CMD_STANDBY_MODE_SET_CONFIG_V1 = 2,

    MORSE_CMD_STANDBY_MODE_SET_STATUS_PAYLOAD = 3,

    MORSE_CMD_STANDBY_MODE_SET_WAKE_FILTER = 4,

    MORSE_CMD_STANDBY_MODE_SET_CONFIG_V2 = 5,

    MORSE_CMD_STANDBY_MODE_SET_CONFIG_V3 = 6,
};


enum morse_cmd_standby_mode_exit_reason
{

    MORSE_CMD_STANDBY_MODE_EXIT_REASON_NONE = 0,

    MORSE_CMD_STANDBY_MODE_EXIT_REASON_WAKEUP_FRAME = 1,

    MORSE_CMD_STANDBY_MODE_EXIT_REASON_ASSOCIATE = 2,

    MORSE_CMD_STANDBY_MODE_EXIT_REASON_EXT_INPUT = 3,

    MORSE_CMD_STANDBY_MODE_EXIT_REASON_WHITELIST_PKT = 4,

    MORSE_CMD_STANDBY_MODE_EXIT_REASON_TCP_CONNECTION_LOST = 5,

    MORSE_CMD_STANDBY_MODE_EXIT_REASON_HW_SCAN_NOT_ENABLED = 6,

    MORSE_CMD_STANDBY_MODE_EXIT_REASON_HW_SCAN_FAILED_TO_START = 7,

    MORSE_CMD_STANDBY_MODE_EXIT_REASON_MAX =
        MORSE_CMD_STANDBY_MODE_EXIT_REASON_HW_SCAN_FAILED_TO_START,
};


enum morse_cmd_ieee80211_sta_state
{
    MORSE_CMD_IEEE80211_STA_STATE_NOTEXIST = 0,
    MORSE_CMD_IEEE80211_STA_STATE_NONE = 1,
    MORSE_CMD_IEEE80211_STA_STATE_AUTHENTICATED = 2,
    MORSE_CMD_IEEE80211_STA_STATE_ASSOCIATED = 3,
    MORSE_CMD_IEEE80211_STA_STATE_AUTHORIZED = 4,
    MORSE_CMD_IEEE80211_STA_STATE_AUTHORIZED_ASLEEP = 5,
};


struct MM_PACKED morse_cmd_standby_set_config
{

    uint32_t notify_period_s;

    uint32_t bss_inactivity_before_deep_sleep_s;

    uint32_t deep_sleep_period_s;

    uint32_t src_ip;

    uint32_t dst_ip;

    uint16_t dst_port;

    uint8_t pad[2];

    uint32_t deep_sleep_increment_s;

    uint32_t deep_sleep_max_s;

    uint32_t deep_sleep_scan_iterations;
};


struct MM_PACKED morse_cmd_standby_set_status_payload
{

    uint32_t len;

    uint8_t payload[MORSE_CMD_STANDBY_STATUS_FRAME_USER_PAYLOAD_MAX_LEN];
};


struct MM_PACKED morse_cmd_standby_enter
{

    struct morse_cmd_mac_addr monitor_bssid;

    uint8_t is_umac_controlled;
};


struct MM_PACKED morse_cmd_standby_set_wake_filter
{

    uint32_t len;

    uint32_t offset;

    uint8_t filter[MORSE_CMD_STANDBY_WAKE_FRAME_USER_FILTER_MAX_LEN];
};


struct MM_PACKED morse_cmd_standby_mode_exit
{

    uint8_t reason;

    uint8_t sta_state;
};


struct MM_PACKED morse_cmd_req_standby_mode
{
    struct morse_cmd_header hdr;

    uint32_t cmd;

    union
    {
        uint8_t opaque[0];

        struct morse_cmd_standby_set_config config;

        struct morse_cmd_standby_set_status_payload set_payload;

        struct morse_cmd_standby_enter enter;

        struct morse_cmd_standby_set_wake_filter set_filter;
    };
};


struct MM_PACKED morse_cmd_resp_standby_mode
{
    struct morse_cmd_header hdr;
    uint32_t status;

    union
    {
        uint8_t opaque[0];
        struct morse_cmd_standby_mode_exit info;
    };
};


enum morse_cmd_dhcp_opcode
{

    MORSE_CMD_DHCP_OPCODE_ENABLE = 0,

    MORSE_CMD_DHCP_OPCODE_DO_DISCOVERY = 1,

    MORSE_CMD_DHCP_OPCODE_GET_LEASE = 2,

    MORSE_CMD_DHCP_OPCODE_CLEAR_LEASE = 3,

    MORSE_CMD_DHCP_OPCODE_RENEW_LEASE = 4,

    MORSE_CMD_DHCP_OPCODE_REBIND_LEASE = 5,

    MORSE_CMD_DHCP_OPCODE_SEND_LEASE_UPDATE = 6,
};


enum morse_cmd_dhcp_retcode
{

    MORSE_CMD_DHCP_RETCODE_SUCCESS = 0,

    MORSE_CMD_DHCP_RETCODE_NOT_ENABLED = 1,

    MORSE_CMD_DHCP_RETCODE_ALREADY_ENABLED = 2,

    MORSE_CMD_DHCP_RETCODE_NO_LEASE = 3,

    MORSE_CMD_DHCP_RETCODE_HAVE_LEASE = 4,

    MORSE_CMD_DHCP_RETCODE_BUSY = 5,

    MORSE_CMD_DHCP_RETCODE_BAD_VIF = 6,
};


struct MM_PACKED morse_cmd_req_dhcp_offload
{
    struct morse_cmd_header hdr;

    uint32_t opcode;
};


struct MM_PACKED morse_cmd_resp_dhcp_offload
{
    struct morse_cmd_header hdr;
    uint32_t status;

    uint32_t retcode;

    uint32_t my_ip;

    uint32_t netmask;

    uint32_t router;

    uint32_t dns;
};

#define MORSE_CMD_MAX_OUI_FILTERS           5
#define MORSE_CMD_OUI_SIZE                  3
#define MORSE_CMD_MAX_OUI_FILTER_ARRAY_SIZE 15


struct MM_PACKED morse_cmd_req_update_oui_filter
{
    struct morse_cmd_header hdr;
    uint8_t n_ouis;
    uint8_t ouis[MORSE_CMD_MAX_OUI_FILTERS][MORSE_CMD_OUI_SIZE];
};

#define MORSE_CMD_HW_SCAN_FLAGS_START                BIT(0)
#define MORSE_CMD_HW_SCAN_FLAGS_ABORT                BIT(1)
#define MORSE_CMD_HW_SCAN_FLAGS_SURVEY               BIT(2)
#define MORSE_CMD_HW_SCAN_FLAGS_STORE                BIT(3)
#define MORSE_CMD_HW_SCAN_FLAGS_1MHZ_PROBES          BIT(4)
#define MORSE_CMD_HW_SCAN_FLAGS_SCHED_START          BIT(5)
#define MORSE_CMD_HW_SCAN_FLAGS_SCHED_STOP           BIT(6)
#define MORSE_CMD_HW_SCAN_FLAGS_PROBE_ON_DOZE_BEACON BIT(7)


enum morse_cmd_hw_scan_tlv_tag
{
    MORSE_CMD_HW_SCAN_TLV_TAG_PAD = 0,
    MORSE_CMD_HW_SCAN_TLV_TAG_PROBE_REQ = 1,
    MORSE_CMD_HW_SCAN_TLV_TAG_CHAN_LIST = 2,
    MORSE_CMD_HW_SCAN_TLV_TAG_POWER_LIST = 3,
    MORSE_CMD_HW_SCAN_TLV_TAG_DWELL_ON_HOME = 4,
    MORSE_CMD_HW_SCAN_TLV_TAG_SCHED = 5,
    MORSE_CMD_HW_SCAN_TLV_TAG_FILTER = 6,
    MORSE_CMD_HW_SCAN_TLV_TAG_SCHED_PARAMS = 7,
};


struct MM_PACKED morse_cmd_hw_scan_tlv
{

    uint16_t tag;

    uint16_t len;

    uint8_t value[];
};


struct MM_PACKED morse_cmd_req_hw_scan
{
    struct morse_cmd_header hdr;

    uint32_t flags;

    uint32_t dwell_time_ms;

    uint8_t variable[];
};

#define MORSE_CMD_WHITELIST_FLAGS_CLEAR BIT(0)


struct MM_PACKED morse_cmd_req_set_whitelist
{
    struct morse_cmd_header hdr;

    uint8_t flags;

    uint8_t ip_protocol;

    uint16_t llc_protocol;

    uint32_t src_ip;

    uint32_t dest_ip;

    uint32_t netmask;

    uint16_t src_port;

    uint16_t dest_port;
};


struct MM_PACKED morse_cmd_arp_periodic_params
{
    uint32_t refresh_period_s;
    uint32_t destination_ip;
    uint8_t send_as_garp;
};


struct MM_PACKED morse_cmd_req_arp_periodic_refresh
{
    struct morse_cmd_header hdr;
    struct morse_cmd_arp_periodic_params config;
};

#define MORSE_CMD_TCP_KEEPALIVE_SET_CFG_PERIOD         BIT(0)
#define MORSE_CMD_TCP_KEEPALIVE_SET_CFG_RETRY_COUNT    BIT(1)
#define MORSE_CMD_TCP_KEEPALIVE_SET_CFG_RETRY_INTERVAL BIT(2)
#define MORSE_CMD_TCP_KEEPALIVE_SET_CFG_SRC_IP_ADDR    BIT(3)
#define MORSE_CMD_TCP_KEEPALIVE_SET_CFG_DEST_IP_ADDR   BIT(4)
#define MORSE_CMD_TCP_KEEPALIVE_SET_CFG_SRC_PORT       BIT(5)
#define MORSE_CMD_TCP_KEEPALIVE_SET_CFG_DEST_PORT      BIT(6)


struct MM_PACKED morse_cmd_req_set_tcp_keepalive
{
    struct morse_cmd_header hdr;
    uint8_t enabled;
    uint8_t retry_count;
    uint8_t retry_interval_s;
    uint8_t set_cfgs;
    uint32_t src_ip;
    uint32_t dest_ip;
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t period_s;
};


struct MM_PACKED morse_cmd_req_li_sleep
{
    struct morse_cmd_header hdr;

    uint32_t listen_interval;
};


#define MORSE_CMD_SNS_MAX_TIDS 16


enum morse_cmd_sns_flag
{

    MORSE_CMD_SNS_FLAG_SET = BIT(0),

    MORSE_CMD_SNS_FLAG_BASELINE = BIT(1),

    MORSE_CMD_SNS_FLAG_INDIV_ADDR_QOS_DATA = BIT(2),

    MORSE_CMD_SNS_FLAG_QOS_NULL = BIT(3),
};


struct MM_PACKED morse_cmd_sequence_number_spaces
{

    uint16_t baseline;

    uint16_t individually_addr_qos_data[MORSE_CMD_SNS_MAX_TIDS];

    uint16_t qos_null;
};


struct MM_PACKED morse_cmd_req_sequence_number_spaces
{
    struct morse_cmd_header hdr;

    uint32_t flags;

    uint8_t addr[MORSE_CMD_MAC_ADDR_LEN];

    struct morse_cmd_sequence_number_spaces spaces;
};


struct MM_PACKED morse_cmd_resp_sequence_number_spaces
{
    struct morse_cmd_header hdr;
    uint32_t status;

    uint32_t flags;

    struct morse_cmd_sequence_number_spaces spaces;
};


struct MM_PACKED morse_cmd_req_set_cqm_rssi
{
    struct morse_cmd_header hdr;

    int32_t threshold;

    uint32_t hysteresis;
};


struct MM_PACKED morse_cmd_req_iot_configure_interop
{
    struct morse_cmd_header hdr;
    uint8_t disable_op_class_checking;
    uint8_t enable_channel_width_workaround;
};


struct MM_PACKED morse_cmd_req_iot_send_addba
{
    struct morse_cmd_header hdr;
    struct morse_cmd_mac_addr mac_addr;
    uint8_t tid;
};


struct MM_PACKED morse_cmd_resp_iot_read_stats
{
    struct morse_cmd_header hdr;
    uint32_t status;

    uint8_t stats[];
};


struct MM_PACKED morse_cmd_req_set_control_response
{
    struct morse_cmd_header hdr;

    uint8_t direction;

    uint8_t control_response_1mhz_en;
};


struct MM_PACKED morse_cmd_resp_set_control_response
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_evt_beacon_loss
{
    struct morse_cmd_header hdr;
    uint32_t num_bcns;
};

#define MORSE_CMD_UMAC_TRAFFIC_CONTROL_SOURCE_TWT        BIT(0)
#define MORSE_CMD_UMAC_TRAFFIC_CONTROL_SOURCE_DUTY_CYCLE BIT(1)


struct MM_PACKED morse_cmd_evt_umac_traffic_control
{
    struct morse_cmd_header hdr;

    uint8_t pause_data_traffic;

    uint32_t sources;
};


struct MM_PACKED morse_cmd_evt_dhcp_lease_update
{
    struct morse_cmd_header hdr;

    uint32_t my_ip;

    uint32_t netmask;

    uint32_t router;

    uint32_t dns;
};


struct MM_PACKED morse_cmd_evt_hw_scan_done
{
    struct morse_cmd_header hdr;

    uint8_t aborted;
};


enum morse_cmd_connection_loss_reason
{
    MORSE_CMD_CONNECTION_LOSS_REASON_TSF_RESET = 0,
};


struct MM_PACKED morse_cmd_evt_connection_loss
{
    struct morse_cmd_header hdr;

    uint32_t reason;
};


enum morse_cmd_cqm_rssi_threshold_event
{
    MORSE_CMD_CQM_RSSI_THRESHOLD_EVENT_LOW = 0,
    MORSE_CMD_CQM_RSSI_THRESHOLD_EVENT_HIGH = 1,
};


struct MM_PACKED morse_cmd_evt_cqm_rssi_notify
{
    struct morse_cmd_header hdr;

    int16_t rssi;

    uint16_t event;
};


struct MM_PACKED morse_cmd_req_set_response_indication
{
    struct morse_cmd_header hdr;
    int8_t response_indication;
};


struct MM_PACKED morse_cmd_resp_set_response_indication
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_set_transmission_rate
{
    struct morse_cmd_header hdr;
    int32_t mcs_index;
    int32_t bandwidth_mhz;
    int32_t tx_80211ah_format;
    int8_t use_traveling_pilots;
    int8_t use_sgi;
    uint8_t enabled;
    int8_t nss_idx;
    int8_t use_ldpc;
    int8_t use_stbc;
};


struct MM_PACKED morse_cmd_resp_set_transmission_rate
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_set_ndp_probe_support
{
    struct morse_cmd_header hdr;
    uint8_t enabled;
    uint8_t requested_response_is_pv1;
    int8_t tx_bw_mhz;
};


struct MM_PACKED morse_cmd_resp_set_ndp_probe_support
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


enum morse_cmd_hart_id
{
    MORSE_CMD_HART_ID_HOST = 0,
    MORSE_CMD_HART_ID_MAC = 1,
    MORSE_CMD_HART_ID_UPHY = 2,
    MORSE_CMD_HART_ID_LPHY = 3,
};


struct MM_PACKED morse_cmd_req_force_assert
{
    struct morse_cmd_header hdr;

    uint32_t hart_id;
};

#define MORSE_CMD_HOST_BLOCK_TX_FRAMES BIT(0)
#define MORSE_CMD_HOST_BLOCK_TX_CMD    BIT(1)


enum morse_cmd_param_action
{
    MORSE_CMD_PARAM_ACTION_SET = 0,
    MORSE_CMD_PARAM_ACTION_GET = 1,
    MORSE_CMD_PARAM_ACTION_LAST = 2,
};


enum morse_cmd_slow_clock_mode
{

    MORSE_CMD_SLOW_CLOCK_MODE_AUTO = 0,

    MORSE_CMD_SLOW_CLOCK_MODE_INTERNAL = 1,
};


enum morse_cmd_param_id
{
    MORSE_CMD_PARAM_ID_MAX_TRAFFIC_DELIVERY_WAIT_US = 0,
    MORSE_CMD_PARAM_ID_EXTRA_ACK_TIMEOUT_ADJUST_US = 1,
    MORSE_CMD_PARAM_ID_TX_STATUS_FLUSH_WATERMARK = 2,
    MORSE_CMD_PARAM_ID_TX_STATUS_FLUSH_MIN_AMPDU_SIZE = 3,
    MORSE_CMD_PARAM_ID_POWERSAVE_TYPE = 4,
    MORSE_CMD_PARAM_ID_SNOOZE_DURATION_ADJUST_US = 5,
    MORSE_CMD_PARAM_ID_TX_BLOCK = 6,
    MORSE_CMD_PARAM_ID_FORCED_SNOOZE_PERIOD_US = 7,
    MORSE_CMD_PARAM_ID_WAKE_ACTION_GPIO = 8,
    MORSE_CMD_PARAM_ID_WAKE_ACTION_GPIO_PULSE_MS = 9,
    MORSE_CMD_PARAM_ID_CONNECTION_MONITOR_GPIO = 10,
    MORSE_CMD_PARAM_ID_INPUT_TRIGGER_GPIO = 11,
    MORSE_CMD_PARAM_ID_INPUT_TRIGGER_MODE = 12,
    MORSE_CMD_PARAM_ID_COUNTRY = 13,
    MORSE_CMD_PARAM_ID_RTS_THRESHOLD = 14,
    MORSE_CMD_PARAM_ID_HOST_TX_BLOCK = 15,
    MORSE_CMD_PARAM_ID_MEM_RETENTION_CODE = 16,
    MORSE_CMD_PARAM_ID_NON_TIM_MODE = 17,
    MORSE_CMD_PARAM_ID_DYNAMIC_PS_TIMEOUT_MS = 18,
    MORSE_CMD_PARAM_ID_HOME_CHANNEL_DWELL_MS = 19,
    MORSE_CMD_PARAM_ID_SLOW_CLOCK_MODE = 20,
    MORSE_CMD_PARAM_ID_FRAGMENT_THRESHOLD = 21,
    MORSE_CMD_PARAM_ID_BEACON_LOSS_COUNT = 22,
    MORSE_CMD_PARAM_ID_AP_POWER_SAVE = 23,
    MORSE_CMD_PARAM_ID_BEACON_OFFLOAD = 24,
    MORSE_CMD_PARAM_ID_PROBE_RESP_OFFLOAD = 25,
    MORSE_CMD_PARAM_ID_BSS_MAX_AWAY_DURATION = 26,
    MORSE_CMD_PARAM_ID_DEFAULT_ACTIVE_SCAN_DWELL_MS = 27,
    MORSE_CMD_PARAM_ID_CTS_TO_SELF = 28,
    MORSE_CMD_PARAM_ID_CHANNELIZATION = 29,
    MORSE_CMD_PARAM_ID_CRYPTO_IN_HOST = 30,
    MORSE_CMD_PARAM_ID_LAST = 31,
};


struct MM_PACKED morse_cmd_req_get_set_generic_param
{
    struct morse_cmd_header hdr;

    uint32_t param_id;

    uint32_t action;

    uint32_t flags;

    uint32_t value;
};


struct MM_PACKED morse_cmd_resp_get_set_generic_param
{
    struct morse_cmd_header hdr;
    uint32_t status;

    uint32_t flags;

    uint32_t value;
};


struct MM_PACKED morse_cmd_req_turbo_mode
{
    struct morse_cmd_header hdr;

    uint32_t aid;

    uint8_t enabled;
};


struct MM_PACKED morse_cmd_resp_turbo_mode
{
    struct morse_cmd_header hdr;
    uint32_t status;
};


struct MM_PACKED morse_cmd_req_set_frag_threshold
{
    struct morse_cmd_header hdr;

    uint32_t frag_threshold;
};


struct MM_PACKED morse_cmd_resp_set_frag_threshold
{
    struct morse_cmd_header hdr;
    uint32_t status;

    uint32_t frag_threshold;
};
