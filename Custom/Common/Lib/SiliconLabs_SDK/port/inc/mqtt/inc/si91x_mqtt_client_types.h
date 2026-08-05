/***************************************************************************/ /**
 * @file  si91x_mqtt_client_types.h
 * @brief Port override: NWP firmware-compatible MQTT command structs (legacy layout)
 *        + WiseConnect 4.0 RX message structs for sl_mqtt_client.c reassembly.
 *
 * Included before wsdk/.../mqtt/inc via silabs_sdk.mk. Do not edit SDK tree.
 ******************************************************************************/
#pragma once
#include "stdint.h"
#include "sl_common.h"

#define SI91X_MQTT_CLIENT_TOPIC_MAXIMUM_LENGTH       202
#define SI91X_MQTT_CLIENT_WILL_TOPIC_MAXIMUM_LENGTH  202
#define SLI_SI91X_MQTT_CLIENT_MESSAGE_MAXIMUM_LENGTH 100

#define SLI_SI91X_MQTT_CLIENT_ID_MAXIMUM_LENGTH   62
#define SI91X_MQTT_CLIENT_USERNAME_MAXIMUM_LENGTH 122
#define SI91X_MQTT_CLIENT_PASSWORD_MAXIMUM_LENGTH 62

#define SLI_SI91X_MQTT_CLIENT_INIT_COMMAND        1
#define SLI_SI91X_MQTT_CLIENT_CONNECT_COMMAND     2
#define SLI_SI91X_MQTT_CLIENT_SUBSCRIBE_COMMAND   3
#define SLI_SI91X_MQTT_CLIENT_PUBLISH_COMMAND     4
#define SLI_SI91X_MQTT_CLIENT_UNSUBSCRIBE_COMMAND 5
#define SLI_SI91X_MQTT_CLIENT_DISCONNECT_COMMAND  8
#define SLI_SI91X_MQTT_CLIENT_DEINIT_COMMAND      9

#define SLI_SI91X_MQTT_CLIENT_TOPIC_DELIMITER        "/"
#define SLI_SI91X_MQTT_CLIENT_SINGLE_LEVEL_WILD_CARD "+"
#define SLI_SI91X_MQTT_CLIENT_MULTI_LEVEL_WILD_CARD  "#"

typedef struct {
  uint32_t ip_version;
  union {
    uint8_t ipv4_address[4];
    uint8_t ipv6_address[16];
  } server_ip_address;
} sli_si91x_mqtt_client_ip_address_t;

typedef struct SL_ATTRIBUTE_PACKED {
  uint32_t command_type;
  sli_si91x_mqtt_client_ip_address_t server_ip;
  uint32_t server_port;
  uint8_t client_id_len;
  int8_t client_id[SLI_SI91X_MQTT_CLIENT_ID_MAXIMUM_LENGTH];
  uint16_t keep_alive_interval;
  uint8_t username_len;
  uint8_t user_name[SI91X_MQTT_CLIENT_USERNAME_MAXIMUM_LENGTH];
  uint8_t password_len;
  uint8_t password[SI91X_MQTT_CLIENT_PASSWORD_MAXIMUM_LENGTH];
  uint8_t clean;
  uint8_t encrypt;
  uint32_t client_port;
#if defined(SLI_SI917) || defined(SLI_SI915)
  uint8_t tcp_max_retransmission_cap_for_emb_mqtt;
#endif
  uint16_t keep_alive_retries;
} si91x_mqtt_client_init_request_t;

typedef struct SL_ATTRIBUTE_PACKED {
  uint32_t command_type;
  uint8_t is_username_present;
  uint8_t is_password_present;
  uint8_t will_flag;
  uint8_t will_retain;
  uint8_t will_qos;
  uint8_t will_topic_len;
  uint8_t will_topic[SI91X_MQTT_CLIENT_WILL_TOPIC_MAXIMUM_LENGTH];
  uint8_t will_message_len;
  uint8_t will_msg[SLI_SI91X_MQTT_CLIENT_MESSAGE_MAXIMUM_LENGTH];
} sli_si91x_mqtt_client_connect_request_t;

typedef struct SL_ATTRIBUTE_PACKED {
  uint32_t command_type;
  uint8_t topic_len;
  int8_t topic[SI91X_MQTT_CLIENT_TOPIC_MAXIMUM_LENGTH];
  int8_t qos;
} sli_si91x_mqtt_client_subscribe_t;

typedef struct SL_ATTRIBUTE_PACKED {
  uint32_t command_type;
  uint8_t topic_len;
  uint8_t topic[SI91X_MQTT_CLIENT_TOPIC_MAXIMUM_LENGTH];
  int8_t qos;
  uint8_t retained;
  uint8_t dup;
  uint16_t msg_len;
  int8_t *msg;
} sli_si91x_mqtt_client_publish_request_t;

typedef struct SL_ATTRIBUTE_PACKED {
  uint32_t command_type;
  uint8_t topic_len;
  uint8_t topic[SI91X_MQTT_CLIENT_TOPIC_MAXIMUM_LENGTH];
} sli_si91x_mqtt_client_unsubscribe_request_t;

typedef struct {
  uint32_t command_type;
} sli_si91x_mqtt_client_command_request_t;

/* RX layouts from WiseConnect 4.0 SDK (unchanged) */
typedef struct SL_ATTRIBUTE_PACKED {
  uint16_t mqtt_flags;
  uint16_t current_chunk_length;
  uint16_t topic_length;
  uint32_t total_length;
  uint8_t data[];
} sli_si91x_mqtt_client_received_message_t;

typedef struct SL_ATTRIBUTE_PACKED {
  uint16_t mqtt_flags;
  uint16_t current_chunk_length;
  uint8_t data[];
} sli_si91x_mqtt_client_received_chunk_t;
