/*******************************************************************************
 * Copyright (c) 2014 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Allan Stockdill-Mander - initial API and implementation and/or initial documentation
 *******************************************************************************/

#ifndef MQTTSI91X_DUAL_STACK_H
#define MQTTSI91X_DUAL_STACK_H

#include <stdint.h>
#include "sl_utility.h"
#include "cmsis_os2.h"

#define MQTT_WITH_WEBSOCKET_HEADER_LEN         3
#define WAIT_TIMEOUT                           5000
#define SLI_917_WAIT_FOREVER                   0
#define WIFI_BLOCKED_TIMEOUT_SF                1
#define DEFAULT_TIMEOUT                        SLI_TX_EVENT_WAIT_TIME
#define SLI_TX_EVENT_WAIT_TIME                 (30000 + TX_WAIT_TIME)
#define TX_WAIT_TIME                           0
#define SLI_SOCKET_RECVFROM_RESPONSE_WAIT_TIME ((SLI_917_WAIT_FOREVER * WIFI_BLOCKED_TIMEOUT_SF) + (DEFAULT_TIMEOUT))
#define SINGLE_PKT_TCP_STREAM_TIMEOUT          WAIT_TIMEOUT + SLI_SOCKET_RECVFROM_RESPONSE_WAIT_TIME
#define NETWORK_ERROR_NULL_STRUCTURE           -1 // Error: NULL network structure
#define NETWORK_ERROR_NULL_ADDRESS             -2 // Error: NULL address
#define NETWORK_ERROR_INVALID_TYPE             -3 // Error: Invalid transport type
#define NETWORK_ERROR_CONNECT_FAILED           -4 // Error: Socket connect failed

// MQTT TLS Configuration
#ifndef MQTT_TLS_AVAILABLE
#define MQTT_TLS_AVAILABLE 1 // Set to 1 to enable TLS support, 0 to disable
#endif

typedef struct Timer Timer;
struct Timer {
  uint32_t systick_period;
  uint32_t end_time;
};

typedef enum { MQTT_TRANSPORT_TCP, MQTT_TRANSPORT_WEBSOCKET } mqtt_transport_t;

/**
 * @brief Represents the network abstraction layer for MQTT communication with dual stack.
 *
 * @details
 * The `Network` structure provides an abstraction for network operations required by the MQTT client.
 * This implementation uses sl_si91x_socket APIs for the offload network stack in dual stack mode.
 * It supports TCP transport over the Si91x offload stack.
 */
typedef struct Network Network;
struct Network {
  int socket;
  int (*mqttread)(Network *, unsigned char *, int, int);
  int (*mqttwrite)(Network *, unsigned char *, int, int);
  void (*disconnect)(Network *);
  mqtt_transport_t transport_type;
};

uint32_t osKernelGetTickCount(void);
void InitTimer(Timer *);
char expired(Timer *);
void countdown_ms_mqtt(Timer *, unsigned int);
void countdown(Timer *, unsigned int);
int left_ms_mqtt(Timer *);

void NetworkInit(Network *n);
int NetworkConnect(Network *n, uint8_t flags, char *addr, int dst_port, int src_port, bool ssl);
void NetworkDisconnect(Network *n);

#endif /* MQTTSI91X_DUAL_STACK_H */
