#ifndef __MS_MODEM_H__
#define __MS_MODEM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "ms_modem_at.h"

#define MODEM_RX_TASK_STACK_SIZE        (4096)                      // Task stack
#define MODEM_RX_TASK_PRIORITY          (osPriorityRealtime5)       // Task priority
#define MODEM_TX_TASK_STACK_SIZE        (4096)                      // Task stack
#define MODEM_TX_TASK_PRIORITY          (osPriorityRealtime4)       // Task priority
#define MODEM_POWER_OFF_DELAY_MS        (200)                       // Module power-off stabilization delay
#define MODEM_POWER_ON_DELAY_MS         (800)                      // Module power-on stabilization delay
#define MODEM_GPIO_READY_TIMEOUT_MS     (3000)                      // Module power-on GPIO ready timeout
#define MODEM_UART_SEND_MAX_TIME_MS     (1000)                      // Maximum wait time for sending data 1s
#define MODEM_UART_BAUDRATE             (921600U)                   // UART baud rate
#define MODEM_IS_ENABLE_NETWORK_READY   (1)                         // Enable network ready check
#define MODEM_IS_CHECK_NETWORK_TYPE     (0)                         // Enable network type check

/// @brief MODEM state
typedef enum 
{
    MODEM_STATE_UNINIT = 0,
    MODEM_STATE_INIT,
    MODEM_STATE_PPP,
    MODEM_STATE_MAX,
} modem_state_t;

/// @brief MODEM OPERATOR
typedef enum {
    MODEM_OPERATOR_AUTO = 0,
    MODEM_OPERATOR_CHINA_MOBILE,
    MODEM_OPERATOR_CHINA_UNICOM,
    MODEM_OPERATOR_CHINA_TELECOM,
    MODEM_OPERATOR_AMERICAN_VERIZON,
    MODEM_OPERATOR_UNKNOWN,
} modem_operator_t;

#pragma pack(push, 1)
/// @brief MODEM device status information
typedef struct {
    int csq_value;                       // Signal strength value (0~31, 99: no signal)
    int ber_value;                       // Bit error rate value
    int csq_level;                       // Signal strength level (0~5)
    int rssi;                            // Received Signal Strength Indicator
    char model_name[64];                 // Device model name
    char imei[32];                       // Device IMEI
    char imsi[32];                       // SIM card IMSI
    char iccid[32];                      // SIM card ICCID
    char sim_status[32];                 // SIM card status
    char operator[32];                   // Current network operator name
    char version[32];                    // Firmware version
    char plmn_id[8];                     // Current network PLMN ID
    char cell_id[32];                    // Current cell ID
    char lac[32];                        // Location area code
    char network_type[16];               // Network type
    char registration_status[64];        // Network registration status
} modem_info_t;
/// @brief MODEM configuration parameters
typedef struct {
    char apn[32];                       // APN (Access Point Name)
    char user[64];                      // APN username
    char passwd[64];                    // APN password
    uint8_t apn_context_id;             // APN context ID (read only) (0: Auto, others: specified)
    uint8_t authentication;             // APN authentication
    uint8_t is_enable_roam;             // Enable roaming
    uint8_t isp_selected;               // ISP selected (0: Auto, 1: China Mobile, 2: China Unicom, 3: China Telecom, 4: American Verizon)
    char plmn[8];                       // Manual PLMN code (MCC+MNC, 5-6 digits); empty = auto COPS=0
    char pin[32];                       // SIM PIN
    char puk[32];                       // SIM PUK
    uint8_t ppp_context_id;             // PPP context ID (0: Auto, others: specified)
    char ppp_pre_at_cmds[8][64];       // PPP pre-AT command list (8 commands, 64 characters each)
} modem_config_t;
#pragma pack(pop)

typedef int (*modem_net_ppp_callback_t)(uint8_t *p_data, uint16_t len);

int modem_device_init(void);
int modem_device_deinit(void);
int modem_device_get_info(modem_info_t *info, uint8_t is_update_all);
int modem_device_set_config(const modem_config_t *config);
int modem_device_get_config(modem_config_t *config);
int modem_device_restart_network(void);
#if MODEM_IS_ENABLE_NETWORK_READY
int modem_device_wait_network_ready(uint32_t timeout_ms);
#endif
int modem_device_wait_sim_ready(uint32_t timeout_ms);
int modem_device_into_ppp(modem_net_ppp_callback_t recv_callback);
int modem_device_exit_ppp(uint8_t is_focre);
int modem_net_ppp_send(uint8_t *p_data, uint16_t len, uint32_t timeout);
int modem_device_check_and_enable_ecm(void);
modem_state_t modem_device_get_state(void);
modem_operator_t modem_device_get_operator(void);

void modem_device_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __MS_MODEM_H__ */
