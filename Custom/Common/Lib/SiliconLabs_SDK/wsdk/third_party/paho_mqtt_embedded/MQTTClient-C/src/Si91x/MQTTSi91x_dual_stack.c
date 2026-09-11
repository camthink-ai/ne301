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

/*******************************************************************************
 * @file  MQTTSi91x_dual_stack.c
 * @brief MQTT network implementation for Si91x dual stack using sl_si91x_socket APIs
 ******************************************************************************/

#include "MQTTSi91x_dual_stack.h"
#include "sl_constants.h"
#include <string.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "sl_net.h"
#include "lwip/errno.h"
#include "sl_si91x_socket_types.h"
#include "sl_si91x_socket_constants.h"
#include "sl_si91x_constants.h"
#include "sl_si91x_protocol_types.h"
#include "sl_si91x_socket.h"

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

#define SL_SI91X_SOL_SOCKET 1

char expired(Timer *timer)
{
  long left = timer->end_time - osKernelGetTickCount();
  return (left < 0);
}

void countdown_ms_mqtt(Timer *timer, unsigned int timeout)
{
  timer->end_time = osKernelGetTickCount() + timeout;
}

void countdown(Timer *timer, unsigned int timeout)
{
  timer->end_time = osKernelGetTickCount() + (timeout * 1000);
}

int left_ms_mqtt(Timer *timer)
{
  long left = timer->end_time - osKernelGetTickCount();
  return (left < 0) ? 0 : left;
}

void InitTimer(Timer *timer)
{
  timer->end_time = 0;
}

static int mqtt_tcp_read(Network *n, unsigned char *buffer, int len, int timeout_ms)
{
  sl_si91x_time_value timeout;
  timeout.tv_sec  = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  // Set socket receive timeout using sl_si91x_socket API
  sl_si91x_setsockopt(n->socket, SL_SI91X_SOL_SOCKET, SL_SI91X_SO_RCVTIME, &timeout, sizeof(timeout));

  int total_bytes_read = 0;
  while (total_bytes_read < len) {
    int rc = sl_si91x_recv(n->socket, &buffer[total_bytes_read], (len - total_bytes_read), 0);
    if (rc == -1) {
      if (errno != ENOTCONN && errno != ECONNRESET)
        total_bytes_read = -1;
      break;
    } else if (rc == 0) {
      total_bytes_read = 0;
      break;
    } else
      total_bytes_read += rc;
  }
  return total_bytes_read;
}

static int mqtt_tcp_write(Network *n, unsigned char *buffer, int len, int timeout_ms)
{
  UNUSED_PARAMETER(timeout_ms);
  return sl_si91x_send(n->socket, buffer, len, 0);
}

static void mqtt_tcp_disconnect(Network *n)
{
  if (n->socket >= 0) {
    sl_si91x_shutdown(n->socket, 0);
    n->socket = -1;
  }
}

static int mqtt_tcpconnection_handler(Network *n, uint8_t flags, char *addr, int dst_port, int src_port, bool ssl)
{
  UNUSED_PARAMETER(flags);
  int type = SOCK_STREAM;
  int rc   = -1;
  int status;

#ifdef SLI_SI91X_ENABLE_IPV6
  struct sockaddr_in6 server_address_v6 = { 0 };
  struct sockaddr_in6 clientAddr_v6     = { 0 };
  socklen_t socket_length_v6            = sizeof(struct sockaddr_in6);
#else
  struct sockaddr_in server_address = { 0 };
  struct sockaddr_in client_address = { 0 };
  socklen_t socket_length           = sizeof(struct sockaddr_in);
#endif

#ifdef SLI_SI91X_ENABLE_IPV6
  server_address_v6.sin6_family = AF_INET6;
  server_address_v6.sin6_port   = dst_port;
  memcpy(&server_address_v6.sin6_addr.s6_addr, addr, SL_IPV6_ADDRESS_LENGTH);

  n->socket = sl_si91x_socket(AF_INET6, type, IPPROTO_TCP);
  if (n->socket < 0) {
    printf("\r\nSocket creation failed with error: %d\r\n", errno);
    return -1;
  }

  clientAddr_v6.sin6_family = AF_INET6;
  clientAddr_v6.sin6_port   = src_port;

  if (ssl) {
    uint32_t ssl_enable = SL_SI91X_ENABLE_TLS;
    status =
      sl_si91x_setsockopt(n->socket, SL_SI91X_SOL_SOCKET, SL_SI91X_SO_SSL_ENABLE, &ssl_enable, sizeof(ssl_enable));
    if (status < 0) {
      printf("\r\nSet Socket SSL option failed with error: %d\r\n", errno);
      sl_si91x_shutdown(n->socket, 0);
      return -1;
    }
  }

  status = sl_si91x_bind(n->socket, (struct sockaddr *)&clientAddr_v6, socket_length_v6);
  if (status != 0) {
    printf("\r\nSocket bind failed with error: %d\r\n", errno);
    mqtt_tcp_disconnect(n);
    return status;
  }

  rc = sl_si91x_connect(n->socket, (struct sockaddr *)&server_address_v6, socket_length_v6);
#else
  server_address.sin_family         = AF_INET;
  server_address.sin_port           = dst_port;
  memcpy(&server_address.sin_addr.s_addr, addr, sizeof(server_address.sin_addr.s_addr));

  n->socket = sl_si91x_socket(AF_INET, type, IPPROTO_TCP);
  if (n->socket < 0) {
    printf("\r\nSocket creation failed with error: %d\r\n", errno);
    return -1;
  }

  client_address.sin_family = AF_INET;
  client_address.sin_port   = src_port;

  if (ssl) {
    uint32_t ssl_enable = SL_SI91X_ENABLE_TLS;
    status =
      sl_si91x_setsockopt(n->socket, SL_SI91X_SOL_SOCKET, SL_SI91X_SO_SSL_ENABLE, &ssl_enable, sizeof(ssl_enable));
    if (status < 0) {
      printf("\r\nSet Socket SSL option failed with error: %d\r\n", errno);
      sl_si91x_shutdown(n->socket, 0);
      return -1;
    }
  }

  status = sl_si91x_bind(n->socket, (struct sockaddr *)&client_address, socket_length);
  if (status != 0) {
    printf("\r\nSocket bind failed with error: %d\r\n", errno);
    mqtt_tcp_disconnect(n);
    return status;
  }

  rc = sl_si91x_connect(n->socket, (struct sockaddr *)&server_address, socket_length);
#endif

  if (rc == -1) {
    printf("\r\nSocket Connect failed with error: %d\r\n", errno);
    sl_si91x_shutdown(n->socket, 0);
    n->socket = -1;
    return NETWORK_ERROR_CONNECT_FAILED;
  }
  printf("\nSocket connection success \n");
  return 0;
}

void NetworkInit(Network *n)
{
  if (n == NULL)
    return;

  n->socket     = -1;
  n->mqttread   = mqtt_tcp_read;
  n->mqttwrite  = mqtt_tcp_write;
  n->disconnect = mqtt_tcp_disconnect;
}

int NetworkConnect(Network *n, uint8_t flags, char *addr, int dst_port, int src_port, bool ssl)
{
  if (n == NULL) {
    return NETWORK_ERROR_NULL_STRUCTURE;
  }

  if (addr == NULL) {
    return NETWORK_ERROR_NULL_ADDRESS;
  }

  if (n->transport_type == MQTT_TRANSPORT_TCP) {
    return mqtt_tcpconnection_handler(n, flags, addr, dst_port, src_port, ssl);
  } else {
    return NETWORK_ERROR_INVALID_TYPE; // WebSocket not supported in dual stack MQTT
  }
}

void NetworkDisconnect(Network *n)
{
  if (n && n->disconnect) {
    n->disconnect(n);
  }
}
