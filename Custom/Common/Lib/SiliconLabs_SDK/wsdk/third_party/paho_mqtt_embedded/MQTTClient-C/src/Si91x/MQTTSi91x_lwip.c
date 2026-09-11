/*******************************************************************************
 * @file  MQTTSi91x_lwip.c
 * @brief MQTT network implementation for Si91x with LwIP stack
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "MQTTSi91x_lwip.h"
#include "cmsis_os2.h"
#include "mbedtls/debug.h"

// LwIP socket includes (for LwIP stack)
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/sys.h"
#include "lwip/errno.h"

#if MQTT_TLS_ENABLE
#include "mbedtls/platform.h"
#endif

// Configuration
#define MQTT_SOCKET_TIMEOUT_MS 30000
#define MQTT_SEND_TIMEOUT_MS   10000

// Utility macros
#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

#if MQTT_TLS_ENABLE

// mbedTLS error: bad client hello (e.g. SNI/hostname or cipher mismatch); value 0x5D80
#ifndef MBEDTLS_ERR_SSL_BAD_HS_CLIENT_HELLO
#define MBEDTLS_ERR_SSL_BAD_HS_CLIENT_HELLO (-0x5D80)
#endif

// Function prototypes for TLS
static mqtt_tls_context_t *allocate_tls_context(Network *n);
static void free_tls_context(mqtt_tls_context_t *ctx);
static int mqtt_tls_init(mqtt_tls_context_t *tls_ctx, int socket_fd, const char *hostname);
static int mqtt_tls_handshake(mqtt_tls_context_t *tls_ctx);
static void mqtt_tls_cleanup(mqtt_tls_context_t *tls_ctx);
#endif

char expired(Timer *timer)
{
  long left = timer->end_time - sys_now();
  return (left < 0);
}

void countdown_ms_mqtt(Timer *timer, unsigned int timeout)
{
  timer->end_time = sys_now() + timeout;
}

void countdown(Timer *timer, unsigned int timeout)
{
  timer->end_time = sys_now() + (timeout * 1000);
}

int left_ms_mqtt(Timer *timer)
{
  long left = timer->end_time - sys_now();
  return (left < 0) ? 0 : left;
}

void InitTimer(Timer *timer)
{
  timer->end_time = 0;
}

#if MQTT_TLS_ENABLE
// TLS context management functions

static mqtt_tls_context_t *allocate_tls_context(Network *n)
{
  mqtt_tls_context_t *ctx = (mqtt_tls_context_t *)calloc(1, sizeof(mqtt_tls_context_t));
  if (ctx) {
    ctx->in_use   = true;
    ctx->cert_ctx = n->tls->cert_ctx;
    free(n->tls);
  }
  return ctx;
}

static void free_tls_context(mqtt_tls_context_t *ctx)
{
  if (ctx) {
    mqtt_tls_cleanup(ctx);
    free(ctx);
  }
}

// Hardware entropy function for mbedTLS
static int mqtt_mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
  (void)data;
  for (size_t i = 0; i < len; i++) {
    output[i] = rand() % 256;
  }
  *olen = len;
  return 0;
}

// Send function for mbedTLS
static int mqtt_ssl_send(void *ctx, const unsigned char *buf, size_t len)
{
  int fd  = *(int *)ctx;
  int ret = write(fd, buf, len);

  if (ret < 0) {
    int err = errno;
    if (err == EAGAIN || err == EWOULDBLOCK) {
      return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return MBEDTLS_ERR_NET_SEND_FAILED;
  }
  return ret;
}

// Receive function for mbedTLS
static int mqtt_ssl_recv(void *ctx, unsigned char *buf, size_t len)
{
  int fd  = *(int *)ctx;
  int ret = read(fd, buf, len);

  if (ret < 0) {
    int err = errno;
    if (err == EAGAIN || err == EWOULDBLOCK) {
      return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return MBEDTLS_ERR_NET_RECV_FAILED;
  }
  if (ret == 0) {
    return MBEDTLS_ERR_SSL_CONN_EOF;
  }
  return ret;
}

// Receive with timeout function for mbedTLS
static int mqtt_ssl_recv_timeout(void *ctx, unsigned char *buf, size_t len, uint32_t timeout)
{
  int ret;
  struct timeval tv;
  fd_set read_fds;
  int fd = *(int *)ctx;

  // Avoid immediate timeout if mbedTLS passes 0
  if (timeout == 0) {
    timeout = WAIT_TIMEOUT;
  }

  FD_ZERO(&read_fds);
  FD_SET(fd, &read_fds);

  tv.tv_sec  = timeout / 1000;
  tv.tv_usec = (timeout % 1000) * 1000;

  ret = select(fd + 1, &read_fds, NULL, NULL, &tv);
  if (ret == 0) {
    return MBEDTLS_ERR_SSL_TIMEOUT;
  }
  if (ret < 0) {
    return MBEDTLS_ERR_NET_RECV_FAILED;
  }

  return mqtt_ssl_recv(ctx, buf, len);
}

// TLS initialization
static int mqtt_tls_init(mqtt_tls_context_t *tls_ctx, int socket_fd, const char *hostname)
{
  int ret;
  const char *pers = "mqtt_client_lwip";

  // Initialize structures
  mbedtls_ssl_init(&tls_ctx->ssl);
  mbedtls_ssl_config_init(&tls_ctx->conf);
  mbedtls_x509_crt_init(&tls_ctx->cacert);
  mbedtls_entropy_init(&tls_ctx->entropy);
  mbedtls_ctr_drbg_init(&tls_ctx->ctr_drbg);

  tls_ctx->socket_fd   = socket_fd;
  tls_ctx->initialized = false;

  // Add entropy source
  ret = mbedtls_entropy_add_source(&tls_ctx->entropy,
                                   mqtt_mbedtls_hardware_poll,
                                   NULL,
                                   MBEDTLS_ENTROPY_MAX_GATHER,
                                   MBEDTLS_ENTROPY_SOURCE_STRONG);
  if (ret != 0) {
    SL_DEBUG_LOG("mbedtls_entropy_add_source failed: -0x%04x\n", (unsigned int)(-ret));
    return ret;
  }

  // Simplified entropy setup
  ret = mbedtls_ctr_drbg_seed(&tls_ctx->ctr_drbg,
                              mbedtls_entropy_func,
                              &tls_ctx->entropy,
                              (const unsigned char *)pers,
                              strlen(pers));
  if (ret != 0) {
    SL_DEBUG_LOG("mbedtls_ctr_drbg_seed failed: -0x%04x\n", (unsigned int)(-ret));
    return ret;
  }

  // Configure SSL with ultra-minimal memory usage
  ret = mbedtls_ssl_config_defaults(&tls_ctx->conf,
                                    MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0) {
    SL_DEBUG_LOG("mbedtls_ssl_config_defaults failed: -0x%04x\n", (unsigned int)(-ret));
    return ret;
  }

  // Server certificate verification: REQUIRED when CA is provided, else NONE
  if (tls_ctx->cert_ctx.cacert && tls_ctx->cert_ctx.cacert_len > 0) {
    ret = mbedtls_x509_crt_parse(&tls_ctx->cacert,
                                 (const unsigned char *)tls_ctx->cert_ctx.cacert,
                                 tls_ctx->cert_ctx.cacert_len);
    if (ret < 0) {
      SL_DEBUG_LOG("TLS init: CA certificate parse failed: -0x%04x\n", (unsigned int)(-ret));
      mbedtls_x509_crt_free(&tls_ctx->cacert);
      mbedtls_ssl_free(&tls_ctx->ssl);
      mbedtls_ssl_config_free(&tls_ctx->conf);
      mbedtls_entropy_free(&tls_ctx->entropy);
      mbedtls_ctr_drbg_free(&tls_ctx->ctr_drbg);
      return ret;
    }
    mbedtls_ssl_conf_ca_chain(&tls_ctx->conf, &tls_ctx->cacert, NULL);
    mbedtls_ssl_conf_authmode(&tls_ctx->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    SL_DEBUG_LOG("CA certificate loaded successfully\n");
  } else {
    mbedtls_ssl_conf_authmode(&tls_ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
    SL_DEBUG_LOG("TLS init: no CA provided - server verification disabled\n");
  }

  // Configure RNG
  mbedtls_ssl_conf_rng(&tls_ctx->conf, mbedtls_ctr_drbg_random, &tls_ctx->ctr_drbg);

  // Note: Optional SSL feature configuration calls removed due to SiSDK mbedTLS limitations
  // These were used to disable features for memory optimization:
  // - DTLS handshake timeout configuration (requires MBEDTLS_SSL_PROTO_DTLS support)
  // - Session tickets disabled (requires MBEDTLS_SSL_SESSION_TICKETS support)
  // - Renegotiation disabled (requires MBEDTLS_SSL_RENEGOTIATION support)
  // SiSDK mbedTLS components provide these features in default disabled state

  ret = mbedtls_ssl_setup(&tls_ctx->ssl, &tls_ctx->conf);
  if (ret != 0) {
    SL_DEBUG_LOG("mbedtls_ssl_setup failed: -0x%04x\n", (unsigned int)(-ret));
    if (ret == MBEDTLS_ERR_SSL_ALLOC_FAILED) {
      SL_DEBUG_LOG("SSL setup failed due to memory allocation failure\n");
    }
    return ret;
  }

  SL_DEBUG_LOG("SSL setup completed successfully!\n");

  if (hostname) {
    ret = mbedtls_ssl_set_hostname(&tls_ctx->ssl, hostname);
    if (ret != 0) {
      SL_DEBUG_LOG("mbedtls_ssl_set_hostname failed: -0x%04x\n", (unsigned int)(-ret));
      return ret;
    }
    SL_DEBUG_LOG("Hostname set successfully\n");
  } else {
    SL_DEBUG_LOG("No hostname to set\n");
  }

  // Set BIO callbacks
  mbedtls_ssl_set_bio(&tls_ctx->ssl, &tls_ctx->socket_fd, mqtt_ssl_send, mqtt_ssl_recv, mqtt_ssl_recv_timeout);

  tls_ctx->initialized = true;
  SL_DEBUG_LOG("TLS initialization completed successfully\n");
  return 0;
}

// TLS handshake
static int mqtt_tls_handshake(mqtt_tls_context_t *tls_ctx)
{
  int ret;

  while ((ret = mbedtls_ssl_handshake(&tls_ctx->ssl)) != 0) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      SL_DEBUG_LOG("TLS handshake failed: -0x%04x", (unsigned int)(-ret));
      if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
        SL_DEBUG_LOG(" (cert verify failed: CA must sign broker cert; hostname must match cert CN/SAN)");
      } else if (ret == MBEDTLS_ERR_SSL_BAD_HS_CLIENT_HELLO) {
        SL_DEBUG_LOG(" (SSL error: often SNI/hostname or cipher mismatch)");
      }
      SL_DEBUG_LOG("\n");
      return ret;
    }
  }

  SL_DEBUG_LOG("TLS handshake successful\n");
  return 0;
}

// TLS cleanup
static void mqtt_tls_cleanup(mqtt_tls_context_t *tls_ctx)
{
  if (tls_ctx && tls_ctx->initialized) {
    SL_DEBUG_LOG("Cleaning up TLS context...\n");

    mbedtls_ssl_free(&tls_ctx->ssl);
    mbedtls_ssl_config_free(&tls_ctx->conf);
    mbedtls_x509_crt_free(&tls_ctx->cacert);
    mbedtls_entropy_free(&tls_ctx->entropy);
    mbedtls_ctr_drbg_free(&tls_ctx->ctr_drbg);
    tls_ctx->initialized = false;
  }
}

// TLS read function
static int mqtt_tls_read(Network *n, unsigned char *buffer, int len, int timeout_ms)
{
  mqtt_tls_context_t *tls_ctx = n->tls;
  int ret;

  if (!tls_ctx || !tls_ctx->initialized) {
    return -1;
  }

  mbedtls_ssl_conf_read_timeout(&tls_ctx->conf, timeout_ms);
  ret = mbedtls_ssl_read(&tls_ctx->ssl, buffer, len);

  if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
    return 0;
  }
  if (ret == MBEDTLS_ERR_SSL_TIMEOUT) {
    return 0;
  }
  if (ret < 0) {
    SL_DEBUG_LOG("mbedtls_ssl_read failed: -0x%04x\n", (unsigned int)(-ret));
    return -1;
  }

  return ret;
}

static int mqtt_tls_write(Network *n, unsigned char *buffer, int len, int timeout_ms)
{
  mqtt_tls_context_t *tls_ctx = n->tls;
  int ret;

  (void)timeout_ms;

  if (!tls_ctx || !tls_ctx->initialized) {
    return -1;
  }

  ret = mbedtls_ssl_write(&tls_ctx->ssl, buffer, len);

  if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
    return 0;
  }
  if (ret < 0) {
    SL_DEBUG_LOG("mbedtls_ssl_write failed: -0x%04x\n", (unsigned int)(-ret));
    return -1;
  }

  return ret;
}
#endif

// Check if socket is using TLS

static int mqtt_tcp_read(Network *n, unsigned char *buffer, int len, int timeout_ms)
{
#if MQTT_TLS_ENABLE
  // If TLS is enabled for this Network, use TLS read
  if (n->tls && n->tls->initialized) {
    return mqtt_tls_read(n, buffer, len, timeout_ms);
  }
#endif

  fd_set read_fds;
  struct timeval timeout;
  int rc;

  // Set up timeout
  timeout.tv_sec  = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  FD_ZERO(&read_fds);
  FD_SET(n->socket, &read_fds);

  rc = select(n->socket + 1, &read_fds, NULL, NULL, &timeout);
  if (rc == 0) {
    // Timeout occurred - no data available
    return 0;
  }
  if (rc < 0) {
    // Select error
    printf("select() error: %d\n", errno);
    return -1;
  }

  // Data is available, perform the read
  if (FD_ISSET(n->socket, &read_fds)) {
    int total_bytes_read = 0;
    while (total_bytes_read < len) {
      rc = recv(n->socket, &buffer[total_bytes_read], (len - total_bytes_read), MSG_DONTWAIT);
      if (rc == -1) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
          // No more data available right now, but we got some
          break;
        }
        if (err != ENOTCONN && err != ECONNRESET) {
          printf("recv() error: %d\n", err);
          total_bytes_read = -1;
        }
        break;
      } else if (rc == 0) {
        // Connection closed
        total_bytes_read = 0;
        break;
      } else {
        total_bytes_read += rc;
      }
    }
    return total_bytes_read;
  }

  return 0;
}

static int mqtt_tcp_write(Network *n, unsigned char *buffer, int len, int timeout_ms)
{
#if MQTT_TLS_ENABLE
  // If TLS is enabled for this Network, use TLS write
  if (n->tls && n->tls->initialized) {
    return mqtt_tls_write(n, buffer, len, timeout_ms);
  }
#endif

  // Regular TCP write
  UNUSED_PARAMETER(timeout_ms);
  return send(n->socket, buffer, len, 0);
}

static void mqtt_tcp_disconnect(Network *n)
{
#if MQTT_TLS_ENABLE
  if (n->tls) {
    free_tls_context(n->tls);
    n->tls = NULL;
  }
  n->tls_hostname = NULL; // Require NetworkSetTlsHostname() again on reconnect
#endif

  if (n->socket >= 0) {
    close(n->socket);
    n->socket = -1;
  }

  printf("MQTT connection closed\n");
}

static int mqtt_tcpconnection_handler(Network *n,
                                      uint8_t flags,
                                      char *addr,
                                      uint32_t dst_port,
                                      uint32_t src_port,
                                      bool ssl)
{
  UNUSED_PARAMETER(flags);
  UNUSED_PARAMETER(src_port);
  int type = SOCK_STREAM;
  int rc   = -1;

#ifdef SLI_SI91X_ENABLE_IPV6
  struct sockaddr_in6 server_address_v6 = { 0 };
  socklen_t socket_length_v6            = sizeof(struct sockaddr_in6);
#else
  struct sockaddr_in server_address = { 0 };
  socklen_t socket_length           = sizeof(struct sockaddr_in);
#endif

#ifdef SLI_SI91X_ENABLE_IPV6
  server_address_v6.sin6_family = AF_INET6;
  server_address_v6.sin6_port   = htons(dst_port);

  memcpy(&server_address_v6.sin6_addr.un.u8_addr, addr, SL_IPV6_ADDRESS_LENGTH);

  n->socket = socket(AF_INET6, type, IPPROTO_TCP);
  if (n->socket < 0) {
    printf("\r\nSocket creation failed with bsd error: %d\r\n", errno);
    return -1;
  }

#else
  //! Set family type
  server_address.sin_family = AF_INET;
  //! Set local port number
  server_address.sin_port = htons(dst_port);
  SL_DEBUG_LOG("Connecting to MQTT broker on port %ld\n", dst_port);

  memcpy(&server_address.sin_addr.s_addr, addr, sizeof(server_address.sin_addr.s_addr));

  n->socket = socket(AF_INET, type, IPPROTO_TCP);
  if (n->socket < 0) {
    printf("\r\nSocket creation failed with bsd error: %d\r\n", errno);
    return -1;
  }
#endif

#ifdef SLI_SI91X_ENABLE_IPV6
  rc = connect(n->socket, (struct sockaddr *)&server_address_v6, socket_length_v6);
#else
  rc = connect(n->socket, (struct sockaddr *)&server_address, socket_length);
#endif
  if (rc == -1) {
    printf("\r\nSocket Connect failed with bsd error: %d\r\n", errno);
    close(n->socket);
    n->socket = -1;
    return rc;
  }
  printf("\nSocket connection success \n");

  // Handle TLS if requested
  if (ssl) {
#if MQTT_TLS_ENABLE

    // TLS requires a hostname for SNI and server cert verification; brokers typically reject or fail without it.
    // Returns NETWORK_ERROR_TLS_HOSTNAME_REQUIRED (-4) if not set.
    if (!n->tls_hostname || n->tls_hostname[0] == '\0') {
      SL_DEBUG_LOG("Error: TLS requires hostname. Call NetworkSetTlsHostname() before NetworkConnect().\n");
      close(n->socket);
      n->socket = -1;
      return NETWORK_ERROR_TLS_HOSTNAME_REQUIRED;
    }

    // Allocate TLS context for this Network
    n->tls = allocate_tls_context(n);
    if (!n->tls) {
      SL_DEBUG_LOG("Failed to allocate TLS context\n");
      close(n->socket);
      n->socket = -1;
      return -1;
    }

    // Use hostname set by app via NetworkSetTlsHostname(); must match server cert CN/SAN for verification
    const char *hostname = n->tls_hostname;
    if (mqtt_tls_init(n->tls, n->socket, hostname) != 0) {
      SL_DEBUG_LOG("TLS initialization failed\n");
      NetworkDisconnect(n);
      return -1;
    }
    SL_DEBUG_LOG("TLS init completed successfully\n");

    // CA already loaded and ca_chain set in mqtt_tls_init() before ssl_setup
    SL_DEBUG_LOG("CA cert: %p (len=%u), Client cert: %p (len=%u), Client key: %p (len=%u)\n",
                 (void *)n->tls->cert_ctx.cacert,
                 (unsigned int)n->tls->cert_ctx.cacert_len,
                 (void *)n->tls->cert_ctx.client_cert,
                 (unsigned int)n->tls->cert_ctx.client_cert_len,
                 (void *)n->tls->cert_ctx.client_key,
                 (unsigned int)n->tls->cert_ctx.client_key_len);
    SL_DEBUG_LOG("TLS certificates configured\n");

    // Perform TLS handshake
    if (mqtt_tls_handshake(n->tls) != 0) {
      SL_DEBUG_LOG("TLS handshake failed\n");
      NetworkDisconnect(n);
      return -1;
    }

    SL_DEBUG_LOG("TLS connection established\n");
#else
    // TLS requested but not available
    SL_DEBUG_LOG("ERROR: TLS requested but mbedTLS not available!\n");
    SL_DEBUG_LOG("Compile with mbedTLS libraries to enable TLS support\n");
    close(n->socket);
    n->socket = -1;
    return -1;
#endif
  } else {
    printf("TCP connection without SSL established\n");
#ifdef MQTT_TLS_ENABLE
    n->tls = NULL;
#endif
  }

  return 0; // Success
}

// Initialize the network structure
void NetworkInit(Network *n)
{
  if (n == NULL)
    return;

  n->socket     = -1;
  n->mqttread   = mqtt_tcp_read;
  n->mqttwrite  = mqtt_tcp_write;
  n->disconnect = mqtt_tcp_disconnect;
#if MQTT_TLS_ENABLE
  n->tls_hostname = NULL;
#endif
}

#if MQTT_TLS_ENABLE
/* Max hostname length: RFC 1035 (DNS) = 255 octets; TLS SNI (RFC 6066) uses DNS hostnames. */
#define MQTT_TLS_MAX_HOSTNAME_LEN 255u

int NetworkSetTlsHostname(Network *n, const char *hostname)
{
  if (n == NULL) {
    return -1;
  }
  if (hostname == NULL) {
    n->tls_hostname = NULL;
    return -1;
  }
  size_t len = strlen(hostname);
  if (len == 0 || len > MQTT_TLS_MAX_HOSTNAME_LEN) {
    n->tls_hostname = NULL; // Reject empty or too long; connect will fail with hostname required
    return -1;
  }
  n->tls_hostname = hostname;
  return 0;
}
#endif

// Connect to the MQTT broker using TCP
int NetworkConnect(Network *n, uint8_t flags, char *addr, int dst_port, int src_port, bool ssl)
{

  if (n == NULL) {
    return NETWORK_ERROR_NULL_STRUCTURE; // Error: NULL network structure
  }

  if (addr == NULL) {
    return NETWORK_ERROR_NULL_ADDRESS; // Error: NULL address
  }

  if (n->transport_type == MQTT_TRANSPORT_TCP) {
    return mqtt_tcpconnection_handler(n, flags, addr, dst_port, src_port, ssl);
  } else {
    return NETWORK_ERROR_INVALID_TYPE; // Error: invalid transport type
  }
}

// Cleans up and closes the network connection.
void NetworkDisconnect(Network *n)
{
  if (n && n->disconnect) {
    n->disconnect(n);
  }
  return;
}

#if MQTT_TLS_ENABLE && !defined(MBEDTLS_PSA_CRYPTO_C)
// No-op stub for psa_crypto_init to satisfy API linkage when PSA crypto is disabled.
psa_status_t psa_crypto_init(void)
{
  // No-op: PSA crypto core is disabled, so initialization is not needed
  return PSA_SUCCESS;
}

// Stub implementation of psa_generate_random that uses external RNG directly, bypassing PSA crypto core when disabled.
psa_status_t psa_generate_random(uint8_t *output, size_t output_size)
{
  size_t output_length = 0;
  psa_status_t status  = mbedtls_psa_external_get_random(NULL, output, output_size, &output_length);

  if (status != PSA_SUCCESS) {
    return status;
  }

  if (output_length != output_size) {
    return PSA_ERROR_INSUFFICIENT_ENTROPY;
  }

  return PSA_SUCCESS;
}
#endif // MQTT_TLS_ENABLE && !defined(MBEDTLS_PSA_CRYPTO_C)