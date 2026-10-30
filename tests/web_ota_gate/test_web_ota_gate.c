#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mongoose.h"
#include "web_server.h"
#include "web_api.h"
#include "api_ota_module.h"
#include "app_host_session.h"
#include "ota_header.h"

static app_session_result_t g_stop_rc = APP_SESSION_ERR_BUSY;
static int g_alloc_count = 0;
static int g_free_count = 0;
static int g_mqtt_stop_calls = 0;
static int g_mqtt_start_calls = 0;
static int g_ai_stop_calls = 0;
static int g_ai_start_calls = 0;
static int g_camera_stop_calls = 0;
static int g_camera_start_calls = 0;
static int g_response_count = 0;
static int g_response_error_code = -1;
static char g_response_message[128] = {0};
static uint32_t g_tick = 1000;

void *buffer_calloc(size_t count, size_t size) {
    g_alloc_count++;
    return calloc(count, size);
}

void buffer_free(void *ptr) {
    g_free_count++;
    free(ptr);
}

app_session_result_t app_host_session_stop(uint32_t wait_ms,
                                           app_host_session_stop_out_t *out) {
    (void)wait_ms;
    if (out) memset(out, 0, sizeof(*out));
    return g_stop_rc;
}

aicam_bool_t mqtt_service_is_running(void) { return AICAM_FALSE; }
aicam_result_t mqtt_service_stop(void) { g_mqtt_stop_calls++; return AICAM_OK; }
void mqtt_service_start(void) { g_mqtt_start_calls++; }

aicam_bool_t ai_pipeline_is_running(void) { return AICAM_FALSE; }
aicam_result_t ai_pipeline_stop(void) { g_ai_stop_calls++; return AICAM_OK; }
aicam_result_t ai_pipeline_start(void) { g_ai_start_calls++; return AICAM_OK; }

aicam_result_t device_service_camera_stop(void) { g_camera_stop_calls++; return AICAM_ERROR; }
aicam_result_t device_service_camera_start(void) { g_camera_start_calls++; return AICAM_OK; }

aicam_bool_t system_service_capture_in_progress(void) { return AICAM_FALSE; }

uint32_t osKernelGetTickCount(void) { return g_tick++; }

aicam_result_t api_response_error(http_handler_context_t *ctx,
                                  api_error_code_t error_code,
                                  const char *message) {
    (void)ctx;
    g_response_count++;
    g_response_error_code = (int)error_code;
    snprintf(g_response_message, sizeof(g_response_message), "%s",
             message ? message : "");
    return AICAM_OK;
}

aicam_result_t http_send_response(http_handler_context_t *ctx) {
    (void)ctx;
    return AICAM_OK;
}

static struct mg_http_message make_upload_headers(void) {
    struct mg_http_message hm;
    memset(&hm, 0, sizeof(hm));
    hm.headers[0].name = mg_str("Content-Length");
    hm.headers[0].value = mg_str("4096");
    return hm;
}

static void reset_recorders(void) {
    g_alloc_count = g_free_count = 0;
    g_mqtt_stop_calls = g_mqtt_start_calls = 0;
    g_ai_stop_calls = g_ai_start_calls = 0;
    g_camera_stop_calls = g_camera_start_calls = 0;
    g_response_count = 0;
    g_response_error_code = -1;
    g_response_message[0] = '\0';
}

static int g_failures = 0;

#define CHECK(cond, name)                                                    \
    do {                                                                     \
        if (cond) {                                                          \
            printf("PASS %s\n", name);                                       \
        } else {                                                             \
            printf("FAIL %s\n", name);                                       \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static void test_busy_stop_refuses_upgrade(void) {
    struct mg_http_message hm = make_upload_headers();
    struct mg_connection conn;
    memset(&conn, 0, sizeof(conn));

    g_stop_rc = APP_SESSION_ERR_BUSY;
    reset_recorders();

    ota_upload_stream_processor(&conn, MG_EV_HTTP_HDRS, &hm);

    CHECK(g_response_count == 1, "busy: controlled rejection sent");
    CHECK(g_response_error_code == API_ERROR_SERVICE_UNAVAILABLE,
          "busy: 503 service unavailable");
    CHECK(strcmp(g_response_message, "App session is busy, OTA upgrade refused") == 0,
          "busy: refusal reason in response");
    CHECK(conn.fn_data == NULL, "busy: request context detached");
    CHECK(ota_is_upload_in_progress() == AICAM_FALSE,
          "busy: upgrade latch untouched");
    CHECK(g_mqtt_stop_calls == 0 && g_ai_stop_calls == 0 && g_camera_stop_calls == 0,
          "busy: no services torn down before refusal");
    CHECK(g_alloc_count == g_free_count, "busy: context freed");

    ota_upload_stream_processor(&conn, MG_EV_CLOSE, NULL);
    CHECK(g_free_count == g_alloc_count, "busy: close after refusal is no-op");
    CHECK(ota_is_upload_in_progress() == AICAM_FALSE, "busy: still not upgrading");
}

static void test_not_running_stop_enters_upgrade(void) {
    struct mg_http_message hm = make_upload_headers();
    struct mg_connection conn;
    memset(&conn, 0, sizeof(conn));

    g_stop_rc = APP_SESSION_ERR_NOT_RUNNING;
    reset_recorders();

    ota_upload_stream_processor(&conn, MG_EV_HTTP_HDRS, &hm);

    CHECK(g_response_count == 0, "not-running: gate does not reject");
    CHECK(conn.fn_data != NULL, "not-running: upload context stays attached");
    CHECK(ota_is_upload_in_progress() == AICAM_TRUE,
          "not-running: upgrade proceeds");
    CHECK(g_mqtt_stop_calls == 0 && g_ai_stop_calls == 0, "not-running: idle services untouched");

    ota_upload_stream_processor(&conn, MG_EV_CLOSE, NULL);
    CHECK(conn.fn_data == NULL, "not-running: close frees context");
    CHECK(ota_is_upload_in_progress() == AICAM_FALSE, "not-running: latch reset on close");
    CHECK(g_mqtt_start_calls == 0 && g_ai_start_calls == 0 && g_camera_start_calls == 0,
          "not-running: no service restarts after close");
    CHECK(g_alloc_count == g_free_count, "not-running: alloc/free balanced");
}

int main(void) {
    test_busy_stop_refuses_upgrade();
    test_not_running_stop_enters_upgrade();
    printf("%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILED",
           g_failures);
    return g_failures == 0 ? 0 : 1;
}
