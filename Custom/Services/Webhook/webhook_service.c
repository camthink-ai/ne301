/**
 * @file webhook_service.c
 * @brief Webhook service implementation — HTTP(S) POST of capture JPEG images
 *        Payload format matches MQTT JSON structure (metadata + device_info + ai_result + base64 image)
 */

#include "webhook_service.h"
#include "json_config_mgr.h"
#include "json_config_internal.h"
#include "http_client.h"
#include "ms_network.h"
#include "debug.h"
#include "cmsis_os2.h"
#include "tx_api.h"
#include "buffer_mgr.h"
#include "common_utils.h"
#include "mqtt_service.h"
#include "nn.h"
#include "cJSON.h"
#include "device_service.h"
#include "communication_service.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Verbose [WEBHOOK] diagnostic logs. Default OFF — set to 1 to trace service
 * start and push task lifecycle. Operational errors use LOG_SVC_* regardless. */
#define WEBHOOK_DEBUG 0
#if WEBHOOK_DEBUG
#define WEBHOOK_LOG(...) printf("[WEBHOOK] " __VA_ARGS__)
#else
#define WEBHOOK_LOG(...) ((void)0)
#endif

/* ==================== Configuration ==================== */

#define WEBHOOK_TASK_STACK      (4096 * 4)
#define WEBHOOK_TASK_PRIORITY   osPriorityBelowNormal
#define WEBHOOK_TIMEOUT_MS      15000
#define WEBHOOK_BOUNDARY        "----NE301WebhookBoundary"

/* ISRG Root X1 (Let's Encrypt) — https://letsencrypt.org/certs/isrgrootx1.pem */
#define WEBHOOK_CA_ISRG_X1 \
"-----BEGIN CERTIFICATE-----\n" \
"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\n" \
"-----END CERTIFICATE-----\n"

/* GlobalSign Root CA - R3 */
#define WEBHOOK_CA_GLOBALSIGN_R3 \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDXzCCAkegAwIBAgILBAAAAAABIVhTCKIwDQYJKoZIhvcNAQELBQAwTDEgMB4G\n" \
"A1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNp\n" \
"Z24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDkwMzE4MTAwMDAwWhcNMjkwMzE4\n" \
"MTAwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEG\n" \
"A1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI\n" \
"hvcNAQEBBQADggEPADCCAQoCggEBAMwldpB5BngiFvXAg7aEyiie/QV2EcWtiHL8\n" \
"RgJDx7KKnQRfJMsuS+FggkbhUqsMgUdwbN1k0ev1LKMPgj0MK66X17YUhhB5uzsT\n" \
"gHeMCOFJ0mpiLx9e+pZo34knlTifBtc+ycsmWQ1z3rDI6SYOgxXG71uL0gRgykmm\n" \
"KPZpO/bLyCiR5Z2KYVc3rHQU3HTgOu5yLy6c+9C7v/U9AOEGM+iCK65TpjoWc4zd\n" \
"QQ4gOsC0p6Hpsk+QLjJg6VfLuQSSaGjlOCZgdbKfd/+RFO+uIEn8rUAVSNECMWEZ\n" \
"XriX7613t2Saer9fwRPvm2L7DWzgVGkWqQPabumDk3F2xmmFghcCAwEAAaNCMEAw\n" \
"DgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFI/wS3+o\n" \
"LkUkrk1Q+mOai97i3Ru8MA0GCSqGSIb3DQEBCwUAA4IBAQBLQNvAUKr+yAzv95ZU\n" \
"RUm7lgAJQayzE4aGKAczymvmdLm6AC2upArT9fHxD4q/c2dKg8dEe3jgr25sbwMp\n" \
"jjM5RcOO5LlXbKr8EpbsU8Yt5CRsuZRj+9xTaGdWPoO4zzUhw8lo/s7awlOqzJCK\n" \
"6fBdRoyV3XpYKBovHd7NADdBj+1EbddTKJd+82cEHhXXipa0095MJ6RMG3NzdvQX\n" \
"mcIfeg7jLQitChws/zyrVQ4PkX4268NXSb7hLi18YIvDQVETI53O9zJrlAGomecs\n" \
"Mx86OyXShkDOOyyGeMlhLxS67ttVb9+E7gUJTb0o2HLO02JQZR7rkpeDMdmztcpH\n" \
"WD9f\n" \
"-----END CERTIFICATE-----\n"

/* DigiCert Global Root G2 (DigiCert/GeoTrust, many CDNs and enterprises) */
#define WEBHOOK_CA_DIGICERT_G2 \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n" \
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n" \
"MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n" \
"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n" \
"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n" \
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n" \
"2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n" \
"1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n" \
"q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n" \
"tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n" \
"vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n" \
"BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n" \
"5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n" \
"1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n" \
"NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1N\n" \
"GFdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n" \
"8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n" \
"pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n" \
"MrY=\n" \
"-----END CERTIFICATE-----\n"

/* CA bundle: Let's Encrypt + GlobalSign R3 + DigiCert G2 (mbedTLS parses multiple PEMs) */
#define WEBHOOK_CA_BUNDLE \
    WEBHOOK_CA_ISRG_X1 \
    WEBHOOK_CA_GLOBALSIGN_R3 \
    WEBHOOK_CA_DIGICERT_G2

#define WEBHOOK_CA_CERT_MAX_SIZE  (8 * 1024)

static aicam_result_t webhook_build_tls_config(network_tls_config_t *tls_config, char **custom_ca)
{
    if (!tls_config || !custom_ca) return AICAM_ERROR_INVALID_PARAM;

    memset(tls_config, 0, sizeof(*tls_config));
    tls_config->is_verify_hostname = 0;
    *custom_ca = NULL;

    size_t cert_len = 0;
    char *ca_data = NULL;

    if (json_config_get_webhook_ca_cert(&ca_data, &cert_len) == AICAM_OK && ca_data && cert_len > 0) {
        tls_config->ca_data = ca_data;
        tls_config->ca_len = cert_len + 1;
        *custom_ca = ca_data;
        LOG_SVC_DEBUG("Webhook: using custom CA cert (%zu bytes)", cert_len);
    } else {
        tls_config->ca_data = WEBHOOK_CA_BUNDLE;
        tls_config->ca_len = 0;
        if (ca_data) buffer_free(ca_data);
        LOG_SVC_DEBUG("Webhook: using built-in CA bundle");
    }

    return AICAM_OK;
}

/* ==================== Types ==================== */

typedef struct {
    uint8_t *jpeg_data;
    uint32_t jpeg_size;
    mqtt_image_metadata_t metadata;
    char *ai_result_json;   /* pre-serialized JSON string (heap), NULL if no AI result */
} webhook_push_msg_t;

/* ==================== State ==================== */

static struct {
    aicam_bool_t initialized;
    aicam_bool_t running;
    service_state_t state;
    osMutexId_t mutex;
    osMessageQueueId_t queue;
    osThreadId_t task_handle;
    uint32_t push_count;
    uint32_t fail_count;
    volatile aicam_bool_t push_in_progress;
} g_webhook;

static uint8_t webhook_task_stack[WEBHOOK_TASK_STACK] __attribute__((aligned(32))) IN_PSRAM;

#define WEBHOOK_QUEUE_COUNT 8
/* Queue passes heap-allocated pointers (1 ULONG each) — well within TX_16_ULONG limit */
static TX_QUEUE webhook_queue_cb __attribute__((aligned(8)));
static uint8_t webhook_queue_mem[WEBHOOK_QUEUE_COUNT * sizeof(ULONG)] __attribute__((aligned(8)));
static const osMessageQueueAttr_t webhook_queue_attr = {
    .name = "webhook_q",
    .cb_mem = &webhook_queue_cb,
    .cb_size = sizeof(webhook_queue_cb),
    .mq_mem = webhook_queue_mem,
    .mq_size = sizeof(webhook_queue_mem),
};

/* ==================== Forward Declarations ==================== */

static void webhook_push_task(void *arg);
static aicam_result_t webhook_do_push(const uint8_t *jpeg_data, uint32_t jpeg_size,
                                       const mqtt_image_metadata_t *metadata,
                                       const char *ai_result_json);
static aicam_result_t webhook_build_auth_header(const webhook_config_t *cfg,
                                                  char *header, uint32_t size);

/* ==================== Service Lifecycle ==================== */

aicam_result_t webhook_service_init(void *config)
{
    (void)config;
    memset(&g_webhook, 0, sizeof(g_webhook));
    g_webhook.state = SERVICE_STATE_UNINITIALIZED;

    g_webhook.mutex = osMutexNew(NULL);
    if (!g_webhook.mutex) {
        LOG_SVC_ERROR("Webhook: Failed to create mutex");
        return AICAM_ERROR;
    }

    g_webhook.queue = osMessageQueueNew(WEBHOOK_QUEUE_COUNT, sizeof(webhook_push_msg_t *), &webhook_queue_attr);
    if (!g_webhook.queue) {
        LOG_SVC_ERROR("Webhook: Failed to create queue");
        return AICAM_ERROR;
    }

    g_webhook.initialized = AICAM_TRUE;
    g_webhook.state = SERVICE_STATE_INITIALIZED;
    LOG_SVC_INFO("Webhook service initialized");
    return AICAM_OK;
}

aicam_result_t webhook_service_start(void)
{
    if (!g_webhook.initialized) return AICAM_ERROR_NOT_INITIALIZED;
    if (g_webhook.running) return AICAM_OK;

    g_webhook.running = AICAM_TRUE;
    g_webhook.state = SERVICE_STATE_RUNNING;

    osThreadAttr_t attr = {
        .name = "webhook_push",
        .stack_size = WEBHOOK_TASK_STACK,
        .stack_mem = webhook_task_stack,
        .priority = WEBHOOK_TASK_PRIORITY,
    };
    g_webhook.task_handle = osThreadNew(webhook_push_task, NULL, &attr);
    if (!g_webhook.task_handle) {
        LOG_SVC_ERROR("Webhook: Failed to create push task");
        WEBHOOK_LOG("ERROR: Failed to create push task\r\n");
        g_webhook.running = AICAM_FALSE;
        return AICAM_ERROR;
    }

    WEBHOOK_LOG("service started, task handle=%p\r\n", (void*)g_webhook.task_handle);
    LOG_SVC_INFO("Webhook service started");
    return AICAM_OK;
}

aicam_result_t webhook_service_stop(void)
{
    if (!g_webhook.running) return AICAM_OK;

    g_webhook.running = AICAM_FALSE;

    /* Send a NULL pointer to wake up the task so it can exit */
    webhook_push_msg_t *stop_ptr = NULL;
    osMessageQueuePut(g_webhook.queue, &stop_ptr, 0, 0);

    if (g_webhook.task_handle) {
        osThreadJoin(g_webhook.task_handle);
        /* osThreadJoin only waits but does NOT release the ThreadX TCB.
         * osThreadTerminate calls tx_thread_delete to free kernel resources. */
        osThreadTerminate(g_webhook.task_handle);
        g_webhook.task_handle = NULL;
    }

    g_webhook.state = SERVICE_STATE_INITIALIZED;
    LOG_SVC_INFO("Webhook service stopped");
    return AICAM_OK;
}

aicam_result_t webhook_service_deinit(void)
{
    if (g_webhook.running) webhook_service_stop();

    if (g_webhook.queue) {
        osMessageQueueDelete(g_webhook.queue);
        g_webhook.queue = NULL;
    }
    if (g_webhook.mutex) {
        osMutexDelete(g_webhook.mutex);
        g_webhook.mutex = NULL;
    }

    g_webhook.initialized = AICAM_FALSE;
    g_webhook.state = SERVICE_STATE_UNINITIALIZED;
    return AICAM_OK;
}

service_state_t webhook_service_get_state(void)
{
    return g_webhook.state;
}

aicam_bool_t webhook_service_is_enabled(void)
{
    webhook_config_t cfg;
    if (json_config_get_webhook_config(&cfg) == AICAM_OK) {
        return cfg.enable;
    }
    return AICAM_FALSE;
}

/* ==================== Public API ==================== */

aicam_result_t webhook_service_push_capture(
    const uint8_t *jpeg_data, uint32_t jpeg_size,
    const mqtt_image_metadata_t *metadata,
    const mqtt_ai_result_t *ai_result)
{
    if (!jpeg_data || jpeg_size == 0) {
        LOG_SVC_WARN("Webhook push_capture: invalid params (data=%p, size=%lu)",
                     (const void *)jpeg_data, (unsigned long)jpeg_size);
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_webhook.running) {
        LOG_SVC_ERROR("Webhook push_capture: service not running!");
        return AICAM_ERROR;
    }

    webhook_config_t cfg;
    if (json_config_get_webhook_config(&cfg) != AICAM_OK || !cfg.enable) {
        LOG_SVC_WARN("Webhook push_capture: disabled or config read failed (enable=%d)", cfg.enable);
        return AICAM_ERROR;
    }

    LOG_SVC_INFO("Webhook push_capture: enqueuing %lu bytes, queue count=%lu",
                 (unsigned long)jpeg_size,
                 (unsigned long)osMessageQueueGetCount(g_webhook.queue));

    webhook_push_msg_t *msg = buffer_calloc(1, sizeof(webhook_push_msg_t));
    if (!msg) {
        LOG_SVC_WARN("Webhook: failed to allocate push message");
        return AICAM_ERROR;
    }
    msg->jpeg_data = (uint8_t *)jpeg_data;
    msg->jpeg_size = jpeg_size;

    if (metadata) {
        memcpy(&msg->metadata, metadata, sizeof(*metadata));
    }

    /* Serialize AI result to JSON now — the nn_result_t pointers won't be valid later */
    if (ai_result && ai_result->ai_result.is_valid) {
        cJSON *ai_root = cJSON_CreateObject();
        if (ai_root) {
            cJSON_AddStringToObject(ai_root, "model_name", ai_result->model_name);
            cJSON_AddStringToObject(ai_root, "model_version", ai_result->model_version);
            cJSON_AddNumberToObject(ai_root, "inference_time_ms", ai_result->inference_time_ms);
            cJSON_AddNumberToObject(ai_root, "confidence_threshold", ai_result->confidence_threshold);
            cJSON_AddNumberToObject(ai_root, "nms_threshold", ai_result->nms_threshold);
            cJSON *ai_res = nn_create_ai_result_json(&ai_result->ai_result);
            if (ai_res) cJSON_AddItemToObject(ai_root, "ai_result", ai_res);
            msg->ai_result_json = cJSON_PrintUnformatted(ai_root);
            cJSON_Delete(ai_root);
        }
    }

    /* Mark push in progress BEFORE enqueuing — prevents wait_pending from
     * seeing queue=0 + push_in_progress=0 if the task consumes the message
     * before wait_pending gets a chance to poll. */
    g_webhook.push_in_progress = AICAM_TRUE;

    osStatus_t status = osMessageQueuePut(g_webhook.queue, &msg, 0, 0);
    if (status != osOK) {
        LOG_SVC_WARN("Webhook: queue put failed (status=%d, count=%lu)",
                     status, (unsigned long)osMessageQueueGetCount(g_webhook.queue));
        g_webhook.push_in_progress = AICAM_FALSE;
        if (msg->ai_result_json) cJSON_free(msg->ai_result_json);
        buffer_free(msg);
        return AICAM_ERROR;
    }

    LOG_SVC_INFO("Webhook push_capture: enqueued OK (queue count=%lu)",
                 (unsigned long)osMessageQueueGetCount(g_webhook.queue));
    return AICAM_OK;
}

aicam_result_t webhook_service_test_push(void)
{
    /* Build a minimal 1x1 white JPEG for testing */
    static const uint8_t test_jpeg[] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01,
        0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43,
        0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08, 0x07, 0x07, 0x07, 0x09,
        0x09, 0x08, 0x0A, 0x0C, 0x14, 0x0D, 0x0C, 0x0B, 0x0B, 0x0C, 0x19, 0x12,
        0x13, 0x0F, 0x14, 0x1D, 0x1A, 0x1F, 0x1E, 0x1D, 0x1A, 0x1C, 0x1C, 0x20,
        0x24, 0x2E, 0x27, 0x20, 0x22, 0x2C, 0x23, 0x1C, 0x1C, 0x28, 0x37, 0x29,
        0x2C, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1F, 0x27, 0x39, 0x3D, 0x38, 0x32,
        0x3C, 0x2E, 0x33, 0x34, 0x32, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01,
        0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00,
        0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03,
        0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D,
        0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
        0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
        0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
        0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
        0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45,
        0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
        0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75,
        0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
        0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3,
        0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
        0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
        0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
        0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4,
        0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01,
        0x00, 0x00, 0x3F, 0x00, 0x7B, 0x94, 0x11, 0x00, 0x00, 0x00, 0x00, 0xFF,
        0xD9
    };

    return webhook_do_push(test_jpeg, sizeof(test_jpeg), NULL, NULL);
}

aicam_result_t webhook_service_wait_pending(uint32_t timeout_ms)
{
    if (!g_webhook.running || !g_webhook.queue) {
        LOG_SVC_WARN("Webhook wait_pending: service not running (running=%d, queue=%p)",
                     g_webhook.running, (void *)g_webhook.queue);
        return AICAM_OK;
    }

    LOG_SVC_INFO("Webhook wait_pending: queue=%lu, push_in_progress=%d, timeout=%lu ms",
                 (unsigned long)osMessageQueueGetCount(g_webhook.queue),
                 g_webhook.push_in_progress, (unsigned long)timeout_ms);

    uint32_t start = osKernelGetTickCount();
    while (osMessageQueueGetCount(g_webhook.queue) > 0 || g_webhook.push_in_progress) {
        if ((osKernelGetTickCount() - start) >= timeout_ms) {
            LOG_SVC_WARN("Webhook: wait pending timed out after %lu ms (queue=%lu, push_in_progress=%d)",
                         (unsigned long)timeout_ms,
                         (unsigned long)osMessageQueueGetCount(g_webhook.queue),
                         g_webhook.push_in_progress);
            return AICAM_ERROR_TIMEOUT;
        }
        osDelay(50);
    }
    return AICAM_OK;
}

/* ==================== Push Task ==================== */

static void webhook_push_task(void *arg)
{
    (void)arg;
    WEBHOOK_LOG("push task started (stack=%d)\r\n", WEBHOOK_TASK_STACK);
    LOG_SVC_INFO("Webhook push task started (stack=%d)", WEBHOOK_TASK_STACK);

    while (g_webhook.running) {
        webhook_push_msg_t *msg = NULL;
        osStatus_t status = osMessageQueueGet(g_webhook.queue, &msg, NULL, osWaitForever);
        if (status != osOK) continue;

        /* NULL pointer = stop signal */
        if (!msg) break;

        LOG_SVC_INFO("Webhook task: processing %lu bytes",
                     (unsigned long)msg->jpeg_size);

        g_webhook.push_in_progress = AICAM_TRUE;

        /* Wait for network connectivity before push.
         * After sleep wake-up the interface may not be up yet. */
        communication_type_t net_type = communication_get_current_type();
        LOG_SVC_INFO("Webhook: network type=%d", net_type);
        if (net_type == COMM_TYPE_NONE) {
            LOG_SVC_INFO("Webhook: network not ready, waiting up to 10s...");
            uint32_t wait_start = osKernelGetTickCount();
            while (g_webhook.running &&
                   communication_get_current_type() == COMM_TYPE_NONE) {
                if ((osKernelGetTickCount() - wait_start) >= 10000) {
                    LOG_SVC_WARN("Webhook: network wait timed out (10s)");
                    break;
                }
                osDelay(500);
            }
            LOG_SVC_INFO("Webhook: network ready after %lu ms (type=%d)",
                         (unsigned long)(osKernelGetTickCount() - wait_start),
                         communication_get_current_type());
        }

        aicam_result_t ret = webhook_do_push(msg->jpeg_data, msg->jpeg_size,
                                              &msg->metadata, msg->ai_result_json);
        LOG_SVC_INFO("Webhook: push result=%d", ret);
        g_webhook.push_in_progress = AICAM_FALSE;
        LOG_SVC_INFO("Webhook: push done (total=%lu, fail=%lu)",
                     (unsigned long)g_webhook.push_count, (unsigned long)g_webhook.fail_count);

        /* Free the JPEG buffer (caller transferred ownership) */
        buffer_free(msg->jpeg_data);
        /* Free the AI result JSON string */
        if (msg->ai_result_json) cJSON_free(msg->ai_result_json);
        /* Free the message struct */
        buffer_free(msg);

        if (ret == AICAM_OK) {
            g_webhook.push_count++;
        } else {
            g_webhook.fail_count++;
        }
        LOG_SVC_INFO("Webhook task: done (total=%lu, fail=%lu)",
                     (unsigned long)g_webhook.push_count, (unsigned long)g_webhook.fail_count);
    }

    LOG_SVC_WARN("Webhook push task exited");
}

/* ==================== HTTP Push Implementation ==================== */

static const char *trigger_type_str(aicam_capture_trigger_t t)
{
    switch (t) {
        case AICAM_CAPTURE_TRIGGER_RTC:      return "rtc";
        case AICAM_CAPTURE_TRIGGER_PIR:      return "pir";
        case AICAM_CAPTURE_TRIGGER_WEB:      return "web";
        case AICAM_CAPTURE_TRIGGER_REMOTE:   return "remote";
        case AICAM_CAPTURE_TRIGGER_GPIO:     return "gpio";
        case AICAM_CAPTURE_TRIGGER_BUTTON:   return "button";
        case AICAM_CAPTURE_TRIGGER_SCHEDULE: return "schedule";
        default:                             return "unknown";
    }
}

static const char *image_format_str(mqtt_image_format_t f)
{
    switch (f) {
        case MQTT_IMAGE_FORMAT_JPEG: return "jpeg";
        case MQTT_IMAGE_FORMAT_PNG:  return "png";
        case MQTT_IMAGE_FORMAT_BMP:  return "bmp";
        case MQTT_IMAGE_FORMAT_RAW:  return "raw";
        default:                     return "unknown";
    }
}

static aicam_result_t webhook_do_push(const uint8_t *jpeg_data, uint32_t jpeg_size,
                                       const mqtt_image_metadata_t *metadata,
                                       const char *ai_result_json)
{
    webhook_config_t cfg;
    if (json_config_get_webhook_config(&cfg) != AICAM_OK || !cfg.enable) {
        LOG_SVC_DEBUG("Webhook: disabled, skipping push");
        return AICAM_OK;
    }

    if (!cfg.url[0]) {
        LOG_SVC_WARN("Webhook: no URL configured");
        return AICAM_ERROR;
    }

    LOG_SVC_INFO("Webhook: pushing %lu bytes to %s",
                 (unsigned long)jpeg_size, cfg.url);

    /* Build JSON part matching MQTT structure (metadata + device_info + ai_result) */
    cJSON *root = cJSON_CreateObject();
    if (!root) return AICAM_ERROR_NO_MEMORY;

    if (metadata) {
        cJSON *meta = cJSON_CreateObject();
        if (meta) {
            cJSON_AddStringToObject(meta, "image_id", metadata->image_id);
            cJSON_AddNumberToObject(meta, "timestamp", (double)metadata->timestamp);
            cJSON_AddStringToObject(meta, "format", image_format_str(metadata->format));
            cJSON_AddNumberToObject(meta, "width", metadata->width);
            cJSON_AddNumberToObject(meta, "height", metadata->height);
            cJSON_AddNumberToObject(meta, "size", metadata->size);
            cJSON_AddNumberToObject(meta, "quality", metadata->quality);
            cJSON_AddStringToObject(meta, "trigger_type", trigger_type_str(metadata->trigger_type));
            cJSON_AddItemToObject(root, "metadata", meta);
        }
    }

    device_info_config_t *dev_info = (device_info_config_t *)buffer_calloc(1, sizeof(device_info_config_t));
    if (dev_info) {
        if (device_service_get_cached_info(dev_info) == AICAM_OK) {
            cJSON *dev = cJSON_CreateObject();
            if (dev) {
                cJSON_AddStringToObject(dev, "device_name", dev_info->device_name);
                cJSON_AddStringToObject(dev, "serial_number", dev_info->serial_number);
                cJSON_AddItemToObject(root, "device_info", dev);
            }
        }
        buffer_free(dev_info);
    }

    if (ai_result_json) {
        cJSON *ai = cJSON_Parse(ai_result_json);
        cJSON_AddItemToObject(root, "ai_result", ai ? ai : cJSON_CreateNull());
    } else {
        cJSON_AddItemToObject(root, "ai_result", cJSON_CreateNull());
    }

    char *meta_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!meta_json) return AICAM_ERROR_NO_MEMORY;

    /* Build multipart/form-data body:
       Part 1: metadata (JSON matching MQTT structure)
       Part 2: image (raw JPEG binary) */
    uint32_t meta_json_len = strlen(meta_json);
    uint32_t body_size = 512 + meta_json_len + jpeg_size + 128;
    uint8_t *body = (uint8_t *)buffer_calloc(1, body_size);
    if (!body) {
        LOG_SVC_ERROR("Webhook: failed to alloc body (%lu)", (unsigned long)body_size);
        cJSON_free(meta_json);
        return AICAM_ERROR_NO_MEMORY;
    }

    int pos = 0;

    /* Metadata JSON part */
    pos += snprintf((char *)body + pos, body_size - pos,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"metadata\"\r\n"
        "Content-Type: application/json\r\n\r\n",
        WEBHOOK_BOUNDARY);
    memcpy(body + pos, meta_json, meta_json_len);
    pos += meta_json_len;
    cJSON_free(meta_json);
    pos += snprintf((char *)body + pos, body_size - pos, "\r\n");

    /* Image binary part */
    pos += snprintf((char *)body + pos, body_size - pos,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n",
        WEBHOOK_BOUNDARY);

    if ((uint32_t)pos + jpeg_size + 64 > body_size) {
        LOG_SVC_ERROR("Webhook: body buffer too small");
        buffer_free(body);
        return AICAM_ERROR_NO_MEMORY;
    }
    memcpy(body + pos, jpeg_data, jpeg_size);
    pos += jpeg_size;
    pos += snprintf((char *)body + pos, body_size - pos,
        "\r\n--%s--\r\n", WEBHOOK_BOUNDARY);

    /* Determine if HTTPS */
    aicam_bool_t use_https = (strncmp(cfg.url, "https://", 8) == 0);

    /* Build TLS config */
    network_tls_config_t tls_config;
    char *custom_ca_cert = NULL;

    if (use_https) {
        if (webhook_build_tls_config(&tls_config, &custom_ca_cert) != AICAM_OK) {
            LOG_SVC_ERROR("Webhook: failed to build TLS config");
            if (custom_ca_cert) buffer_free(custom_ca_cert);
            buffer_free(body);
            return AICAM_ERROR;
        }
    }

    char content_type[64];
    snprintf(content_type, sizeof(content_type),
             "multipart/form-data; boundary=%s", WEBHOOK_BOUNDARY);

    /* Create HTTP client */
    http_client_config_t http_cfg = {
        .url = cfg.url,
        .method = HTTP_METHOD_POST,
        .post_data = (const char *)body,
        .post_len = pos,
        .content_type = content_type,
        .timeout_ms = WEBHOOK_TIMEOUT_MS,
        .tls_config = use_https ? (const http_client_tls_config_t *)&tls_config : NULL,
    };

    http_client_handle_t client = http_client_init(&http_cfg);
    if (!client) {
        LOG_SVC_ERROR("Webhook: failed to create HTTP client");
        if (custom_ca_cert) buffer_free(custom_ca_cert);
        buffer_free(body);
        return AICAM_ERROR;
    }

    /* Set auth header if configured */
    char auth_header[384];
    if (webhook_build_auth_header(&cfg, auth_header, sizeof(auth_header)) == AICAM_OK) {
        http_client_set_header(client, "Authorization", auth_header);
    }

    int ret = http_client_perform(client);
    int status_code = http_client_get_status_code(client);

    http_client_cleanup(client);
    if (custom_ca_cert) buffer_free(custom_ca_cert);
    buffer_free(body);

    if (ret != 0) {
        LOG_SVC_ERROR("Webhook: HTTP request failed (ret=%d)", ret);
        return AICAM_ERROR;
    }

    if (status_code < 200 || status_code >= 300) {
        LOG_SVC_WARN("Webhook: server returned %d", status_code);
        return AICAM_ERROR;
    }

    LOG_SVC_INFO("Webhook: push OK (status=%d, size=%lu)", status_code, (unsigned long)jpeg_size);
    return AICAM_OK;
}

/* ==================== Auth Header ==================== */

static aicam_result_t webhook_build_auth_header(const webhook_config_t *cfg,
                                                  char *header, uint32_t size)
{
    if (!cfg || !header) return AICAM_ERROR;

    if (strcmp(cfg->auth_type, "bearer") == 0 && cfg->secret[0]) {
        snprintf(header, size, "Bearer %s", cfg->secret);
        return AICAM_OK;
    }

    if (strcmp(cfg->auth_type, "basic") == 0 && cfg->secret[0]) {
        /* secret is expected to be base64-encoded "user:pass" */
        snprintf(header, size, "Basic %s", cfg->secret);
        return AICAM_OK;
    }

    if (strcmp(cfg->auth_type, "custom") == 0 && cfg->secret[0]) {
        strncpy(header, cfg->secret, size - 1);
        header[size - 1] = '\0';
        return AICAM_OK;
    }

    /* "none" or empty — no auth header */
    return AICAM_ERROR;
}
