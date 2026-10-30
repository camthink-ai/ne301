#include <stdio.h>
#include <string.h>
#include "web_host_policy.h"

static int g_total;
static int g_fail;

#define CHECK(cond) \
    do { \
        g_total++; \
        if (!(cond)) { \
            g_fail++; \
            printf("FAIL web_host_policy:%d %s\n", g_total, #cond); \
        } \
    } while (0)

static void setup(void)
{
    web_host_policy_reset();
    web_host_policy_add_admin_host("192.168.93.111");
    web_host_policy_add_admin_host("NE301-2A1509");
    web_host_policy_set_app_site_host("ne301-app.local");
}

static void test_classify(void)
{
    setup();
    web_host_policy_add_admin_host("[fe80::1]");
    CHECK(web_host_policy_classify("[fe80::1]") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("[fe80::1]:80") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("[fe80::1]evil") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("[fe80::1]:x") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("[fe80::1]:") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("[fe80::1]:80evil") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("[]") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("[fe80::1] evil.com") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("[fe80::1]:99999") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("[fe80::1]:0") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("192.168.93.111:garbage") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("192.168.93.111:") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("192.168.93.111:0") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("192.168.93.111:99999") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("192.168.93.111:8o80") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("192.168.93.111:80") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("192.168.93.111:65535") == WEB_HOST_ADMIN);
    web_host_policy_reset();
    web_host_policy_add_admin_host("192.168.93.111");
    web_host_policy_add_admin_host("NE301-2A1509");
    web_host_policy_set_app_site_host("ne301-app.local");
    CHECK(web_host_policy_classify("192.168.93.111") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("192.168.93.111:80") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("192.168.93.111:8080") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("ne301-2a1509") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("NE301-2A1509.") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("ne301-app.local") == WEB_HOST_APP_SITE);
    CHECK(web_host_policy_classify("NE301-APP.LOCAL:80") == WEB_HOST_APP_SITE);
    CHECK(web_host_policy_classify("evil.example.com") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify(NULL) == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("192.168.93.111 evil.com") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("[fe80::1]:80") == WEB_HOST_UNKNOWN);
    web_host_policy_add_admin_host("[fe80::1]");
    CHECK(web_host_policy_classify("[fe80::1]:80") == WEB_HOST_ADMIN);
}

static void test_admin_refresh(void)
{
    web_host_policy_reset();
    web_host_policy_begin_admin_update();
    web_host_policy_add_admin_host("10.0.0.5");
    web_host_policy_end_admin_update("cam-42");
    CHECK(web_host_policy_classify("10.0.0.5") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("cam-42") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("app.cam-42") == WEB_HOST_APP_SITE);
    web_host_policy_begin_admin_update();
    web_host_policy_add_admin_host("10.0.0.6");
    web_host_policy_end_admin_update("cam-43");
    CHECK(web_host_policy_classify("10.0.0.5") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("app.cam-42") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("app.cam-43") == WEB_HOST_APP_SITE);
    web_host_policy_set_app_site_host("explicit.local");
    web_host_policy_begin_admin_update();
    web_host_policy_add_admin_host("10.0.0.7");
    web_host_policy_end_admin_update(NULL);
    CHECK(web_host_policy_classify("app.cam-43") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("explicit.local") == WEB_HOST_APP_SITE);
    web_host_policy_begin_admin_update();
    web_host_policy_end_admin_update(NULL);
    CHECK(web_host_policy_classify("explicit.local") == WEB_HOST_APP_SITE);
}

static void test_origin_gate(void)
{
    setup();
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111", "192.168.93.111") == AICAM_TRUE);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111:8080", "192.168.93.111:8080") == AICAM_TRUE);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111:80", "192.168.93.111") == AICAM_TRUE);
    CHECK(web_host_policy_origin_allowed("https://192.168.93.111:443", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111:8080", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111", "192.168.93.111:8080") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://ne301-2a1509", "NE301-2A1509:80") == AICAM_TRUE);
    CHECK(web_host_policy_origin_allowed("http://ne301-app.local", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("null", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed(NULL, "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://evil.example.com", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111.evil.com", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111/", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111/x", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111?q=1", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://admin@192.168.93.111", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://admin:pw@192.168.93.111", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111:0", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111:99999", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111:", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("ftp://192.168.93.111", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("192.168.93.111", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://%31%32%37.0.0.1", "127.0.0.1") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111 ", "192.168.93.111") == AICAM_FALSE);
    web_host_policy_add_admin_host("[::1]");
    CHECK(web_host_policy_origin_allowed("http://[::1]:8080", "[::1]:8080") == AICAM_TRUE);
    CHECK(web_host_policy_origin_allowed("http://[::1]", "[::1]:8080") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://[::1%eth0]", "[::1]") == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://[::1", "[::1]") == AICAM_FALSE);
}

static void test_entry(void)
{
    setup();
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/device/info", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/echo", "POST",
                                NULL, AICAM_FALSE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/echo", "POST",
                                "", AICAM_TRUE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/echo", "POST",
                                "null", AICAM_TRUE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/echo", "POST",
                                "http://192.168.93.111", AICAM_TRUE) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry("192.168.93.111:8080", "/api/v1/echo", "POST",
                                "http://192.168.93.111", AICAM_TRUE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/echo", "POST",
                                "http://evil.example.com", AICAM_TRUE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/echo", "POST",
                                "http://evil.example.com", AICAM_TRUE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/system/ota/upload", "POST",
                                NULL, AICAM_FALSE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/system/ota/upload", "POST",
                                "http://evil.example.com", AICAM_TRUE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/system/ota/upload", "POST",
                                NULL, AICAM_FALSE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("evil.example.com", "/api/v1/device/info", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_UNKNOWN_HOST);
    CHECK(web_host_policy_entry(NULL, "/api/v1/device/info", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_UNKNOWN_HOST);
    CHECK(web_host_policy_entry("ne301-app.local", "/api/v1/device/info", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry("ne301-app.local", "/appsite/g1/index.html", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry("192.168.93.111", "/appsite/g1/index.html", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry("192.168.93.111", "/appsite/g1/app.js", "GET",
                                "http://192.168.93.111", AICAM_TRUE) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry("ne301-2a1509", "/appsite/g1/index.html", "OPTIONS",
                                "http://ne301-2a1509", AICAM_TRUE) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry("192.168.93.111", "/appsite", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry("ne301-app.local", "/appsite/g1/api", "POST",
                                "http://ne301-app.local", AICAM_TRUE) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/ai/toggle", "GET",
                                "http://evil.example.com", AICAM_TRUE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/ai/toggle", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/ai/toggle", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/ai/toggle?x=1", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/device/info", "OPTIONS",
                                NULL, AICAM_FALSE) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/echo", "PATCH",
                                "http://192.168.93.111:8080", AICAM_TRUE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", NULL, "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/echo", NULL,
                                NULL, AICAM_FALSE) == WEB_ENTRY_CROSS_SITE);
}

static void test_origin_length_boundaries(void)
{
    char host63[64];
    char host64[65];
    char host100[101];
    char origin63[80];
    char origin64[80];
    char origin100[120];

    memset(host63, 'a', 63); host63[63] = '\0';
    memset(host64, 'a', 64); host64[64] = '\0';
    memset(host100, 'a', 100); host100[100] = '\0';
    snprintf(origin63, sizeof(origin63), "http://%s", host63);
    snprintf(origin64, sizeof(origin64), "http://%s", host64);
    snprintf(origin100, sizeof(origin100), "http://%s", host100);

    web_host_policy_reset();
    web_host_policy_add_admin_host(host63);
    CHECK(web_host_policy_classify(host63) == WEB_HOST_ADMIN);
    CHECK(web_host_policy_origin_allowed(origin63, host63) == AICAM_TRUE);

    CHECK(web_host_policy_origin_allowed(origin64, host64) == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed(origin100, host100) == AICAM_FALSE);
    CHECK(web_host_policy_origin_allowed("http://a", host64) == AICAM_FALSE);
    CHECK(web_host_policy_classify(host64) == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify(host100) == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_origin_allowed(origin64, host63) == AICAM_FALSE);
}

static void test_entry_role_app_port(void)
{
    setup();
    web_host_policy_set_app_site_authority("192.168.93.111:8082");
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/appsite/g1/app.js", "HEAD",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_UNKNOWN) == WEB_ENTRY_UNKNOWN_HOST);
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/api/v1/count", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/api/v1/system/ota/upload", "POST",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/appsite/g1/x", "POST",
                                     "http://192.168.93.111:8082", AICAM_TRUE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/appsite/g1/x", "OPTIONS",
                                     "http://192.168.93.111:8082", AICAM_TRUE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("evil.example.com:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111:80", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111:8081", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111.evil.com:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111:0", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111:8082:9", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111:80:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111:8082:80", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111:8082:", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    web_host_policy_set_app_site_authority("192.168.93.111:80:8082");
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    web_host_policy_set_app_site_authority("192.168.93.111:8082");
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry_role(NULL, "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_ADMIN) == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_role("192.168.93.111", "/api/v1/echo", "POST",
                                     "http://192.168.93.111", AICAM_TRUE, WEB_LISTENER_ADMIN) == WEB_ENTRY_ALLOW);
    web_host_policy_set_app_site_authority(NULL);
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    web_host_policy_set_app_site_authority("192.168.93.111");
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
    web_host_policy_set_app_site_authority("192.168.93.111:0");
    CHECK(web_host_policy_entry_role("192.168.93.111:0", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_APP_HOST_API);
}

static void test_netif_ip_candidate(void)
{
    CHECK(web_host_policy_ip_is_site_candidate((const uint8_t[]){127,0,0,1}) == AICAM_FALSE);
    CHECK(web_host_policy_ip_is_site_candidate((const uint8_t[]){127,1,2,3}) == AICAM_FALSE);
    CHECK(web_host_policy_ip_is_site_candidate((const uint8_t[]){0,0,0,0}) == AICAM_FALSE);
    CHECK(web_host_policy_ip_is_site_candidate((const uint8_t[]){224,0,0,1}) == AICAM_FALSE);
    CHECK(web_host_policy_ip_is_site_candidate((const uint8_t[]){255,255,255,255}) == AICAM_FALSE);
    CHECK(web_host_policy_ip_is_site_candidate((const uint8_t[]){192,168,93,111}) == AICAM_TRUE);
    CHECK(web_host_policy_ip_is_site_candidate((const uint8_t[]){10,0,0,5}) == AICAM_TRUE);
    CHECK(web_host_policy_ip_is_site_candidate((const uint8_t[]){172,16,4,20}) == AICAM_TRUE);
    CHECK(web_host_policy_ip_is_site_candidate(NULL) == AICAM_FALSE);
}

static void test_conn_ip_same_origin(void)
{
    web_host_policy_reset();
    CHECK(web_host_policy_classify_conn("192.168.93.111", "192.168.93.111") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify_conn("192.168.93.111:80", "192.168.93.111") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify_conn("192.168.93.111:65535", "192.168.93.111") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify_conn("192.168.93.111", "10.0.0.7") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify_conn("192.168.93.111", NULL) == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify_conn("192.168.93.111", "") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify_conn("ne301-2a1509", "192.168.93.111") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify_conn("192.168.93.111:80", "192.168.93.111:80") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_entry_conn("192.168.93.111", "/api/v1/device/info", "GET",
                                     NULL, AICAM_FALSE, "192.168.93.111") == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry_conn("192.168.93.111", "/api/v1/echo", "POST",
                                     "http://192.168.93.111", AICAM_TRUE,
                                     "192.168.93.111") == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry_conn("192.168.93.111:8080", "/api/v1/echo", "POST",
                                     "http://192.168.93.111:8080", AICAM_TRUE,
                                     "192.168.93.111") == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry_conn("192.168.93.111", "/api/v1/echo", "POST",
                                     "http://evil.example.com", AICAM_TRUE,
                                     "192.168.93.111") == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry_conn("192.168.93.111", "/api/v1/echo", "POST",
                                     NULL, AICAM_FALSE, "192.168.93.111") == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry_conn("192.168.93.111", "/api/v1/ai/toggle", "GET",
                                     NULL, AICAM_FALSE, "192.168.93.111") == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry_conn("192.168.93.111", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, "192.168.93.111") == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_entry_conn("10.0.0.7", "/api/v1/device/info", "GET",
                                     NULL, AICAM_FALSE, "192.168.93.111") == WEB_ENTRY_UNKNOWN_HOST);
    CHECK(web_host_policy_entry_conn("192.168.93.111", "/api/v1/device/info", "GET",
                                     NULL, AICAM_FALSE, NULL) == WEB_ENTRY_UNKNOWN_HOST);
    CHECK(web_host_policy_entry_role_conn("192.168.93.111", "/api/v1/echo", "POST",
                                          "http://192.168.93.111", AICAM_TRUE,
                                          WEB_LISTENER_ADMIN, "192.168.93.111") == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry_role_conn("192.168.93.111", "/api/v1/echo", "POST",
                                          "http://evil.example.com", AICAM_TRUE,
                                          WEB_LISTENER_ADMIN, "192.168.93.111") == WEB_ENTRY_CROSS_SITE);
    CHECK(web_host_policy_entry_role_conn("10.0.0.7", "/api/v1/device/info", "GET",
                                          NULL, AICAM_FALSE, WEB_LISTENER_ADMIN,
                                          "192.168.93.111") == WEB_ENTRY_UNKNOWN_HOST);
    CHECK(web_host_policy_entry_role_conn("192.168.93.111", "/api/v1/device/info", "GET",
                                          NULL, AICAM_FALSE, WEB_LISTENER_UNKNOWN,
                                          "192.168.93.111") == WEB_ENTRY_UNKNOWN_HOST);
    CHECK(web_host_policy_entry_role_conn("192.168.93.111:8082", "/appsite/g1/index.html", "GET",
                                          NULL, AICAM_FALSE, WEB_LISTENER_APP,
                                          "192.168.93.111") == WEB_ENTRY_APP_HOST_API);
    CHECK(web_host_policy_origin_allowed_conn("http://192.168.93.111:8080",
                                              "192.168.93.111:8080",
                                              "192.168.93.111") == AICAM_TRUE);
    CHECK(web_host_policy_origin_allowed_conn("http://evil.example.com",
                                              "192.168.93.111",
                                              "192.168.93.111") == AICAM_FALSE);
    setup();
    CHECK(web_host_policy_classify_conn("NE301-2A1509", "192.168.93.111") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify_conn("192.168.93.111:80", NULL) == WEB_HOST_ADMIN);
}

static void test_conn_ip_degenerate_addr(void)
{
    web_host_policy_reset();
    CHECK(web_host_policy_classify_conn("0.0.0.0", "0.0.0.0") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify_conn("0.0.0.0:80", "0.0.0.0") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_entry_conn("0.0.0.0", "/api/v1/device/info", "GET",
                                     NULL, AICAM_FALSE, "0.0.0.0") == WEB_ENTRY_UNKNOWN_HOST);
    CHECK(web_host_policy_entry_role_conn("0.0.0.0", "/api/v1/device/info", "GET",
                                          NULL, AICAM_FALSE, WEB_LISTENER_ADMIN,
                                          "0.0.0.0") == WEB_ENTRY_UNKNOWN_HOST);
    CHECK(web_host_policy_origin_allowed_conn("http://0.0.0.0", "0.0.0.0",
                                              "0.0.0.0") == AICAM_FALSE);
    setup();
    CHECK(web_host_policy_classify_conn("192.168.93.111", "0.0.0.0") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify_conn("0.0.0.0", "192.168.93.111") == WEB_HOST_UNKNOWN);
}

static void test_netif_degraded_retention(void)
{
    web_host_policy_netif_entry_t entries[2];
    char origin[80];

    web_host_policy_reset();
    memset(entries, 0, sizeof(entries));
    entries[0].ip_addr[0] = 192u;
    entries[0].ip_addr[1] = 168u;
    entries[0].ip_addr[2] = 10u;
    entries[0].ip_addr[3] = 10u;
    entries[0].is_local = AICAM_FALSE;
    entries[0].is_up = AICAM_TRUE;
    entries[0].host_name = "ne301-2a1509";
    entries[1].ip_addr[0] = 10u;
    entries[1].ip_addr[3] = 5u;
    entries[1].is_local = AICAM_FALSE;
    entries[1].is_up = AICAM_TRUE;

    web_host_policy_apply_netif_entries(entries, 2, 8082u, "http://", origin, sizeof(origin));
    CHECK(strcmp(origin, "http://192.168.10.10:8082") == 0);
    CHECK(web_host_policy_classify("192.168.10.10") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("10.0.0.5") == WEB_HOST_ADMIN);

    entries[0].is_up = AICAM_FALSE;
    entries[1].ip_addr[0] = 0u;
    entries[1].ip_addr[1] = 0u;
    entries[1].ip_addr[2] = 0u;
    entries[1].ip_addr[3] = 0u;
    web_host_policy_apply_netif_entries(entries, 2, 8082u, "http://", origin, sizeof(origin));
    CHECK(origin[0] == '\0');
    CHECK(web_host_policy_classify("192.168.10.10") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("10.0.0.5") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("ne301-2a1509") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_entry("192.168.10.10", "/api/v1/device/info", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry_role("192.168.10.10:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) != WEB_ENTRY_ALLOW);

    entries[0].is_up = AICAM_TRUE;
    entries[0].ip_addr[2] = 77u;
    entries[0].ip_addr[3] = 5u;
    entries[1].is_up = AICAM_FALSE;
    web_host_policy_apply_netif_entries(entries, 2, 8082u, "http://", origin, sizeof(origin));
    CHECK(strcmp(origin, "http://192.168.77.5:8082") == 0);
    CHECK(web_host_policy_classify("192.168.10.10") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("10.0.0.5") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("192.168.77.5") == WEB_HOST_ADMIN);
}

static void test_netif_entry_refresh(void)
{
    web_host_policy_netif_entry_t entries[2];
    char origin[80];

    web_host_policy_reset();
    memset(entries, 0, sizeof(entries));
    entries[0].ip_addr[0] = 127u;
    entries[0].ip_addr[3] = 1u;
    entries[0].is_local = AICAM_TRUE;
    entries[0].is_up = AICAM_TRUE;
    entries[1].ip_addr[0] = 192u;
    entries[1].ip_addr[1] = 168u;
    entries[1].ip_addr[2] = 10u;
    entries[1].ip_addr[3] = 10u;
    entries[1].is_local = AICAM_FALSE;
    entries[1].is_up = AICAM_TRUE;
    entries[1].host_name = "ne301-2a1509";

    web_host_policy_apply_netif_entries(entries, 2, 8082u, "http://", origin, sizeof(origin));
    CHECK(strcmp(origin, "http://192.168.10.10:8082") == 0);
    CHECK(web_host_policy_classify("192.168.10.10") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("192.168.10.10:8080") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("ne301-2a1509") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("127.0.0.1") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_entry_role("192.168.10.10:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry("192.168.10.10", "/api/v1/device/info", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_ALLOW);

    entries[1].is_up = AICAM_FALSE;
    web_host_policy_apply_netif_entries(entries, 2, 8082u, "http://", origin, sizeof(origin));
    CHECK(origin[0] == '\0');
    CHECK(web_host_policy_classify("192.168.10.10") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_classify("ne301-2a1509") == WEB_HOST_ADMIN);
    CHECK(web_host_policy_entry_role("192.168.10.10:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) != WEB_ENTRY_ALLOW);

    entries[1].is_up = AICAM_TRUE;
    web_host_policy_apply_netif_entries(entries, 2, 8082u, "http://", origin, sizeof(origin));
    CHECK(strcmp(origin, "http://192.168.10.10:8082") == 0);

    web_host_policy_apply_netif_entries(NULL, 0, 8082u, "http://", origin, sizeof(origin));
    CHECK(origin[0] == '\0');
    CHECK(web_host_policy_classify("192.168.10.10") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_classify("127.0.0.1") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_entry_role("192.168.10.10:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) != WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry("192.168.10.10", "/api/v1/device/info", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_UNKNOWN_HOST);

    web_host_policy_apply_netif_entries(entries, 2, 8082u, "http://", origin, sizeof(origin));
    CHECK(strcmp(origin, "http://192.168.10.10:8082") == 0);
    CHECK(web_host_policy_entry_role("192.168.10.10:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry("192.168.10.10", "/api/v1/device/info", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_ALLOW);
}

static void test_restart_recovery_sequence(void)
{
    setup();
    web_host_policy_set_app_site_authority("192.168.93.111:8082");
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_classify("192.168.93.111") == WEB_HOST_ADMIN);
    web_host_policy_set_app_site_authority(NULL);
    web_host_policy_begin_admin_update();
    web_host_policy_end_admin_update(NULL);
    CHECK(web_host_policy_classify("192.168.93.111") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/device/info", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_UNKNOWN_HOST);
    web_host_policy_begin_admin_update();
    web_host_policy_add_admin_host("192.168.93.111");
    web_host_policy_end_admin_update(NULL);
    web_host_policy_set_app_site_authority("192.168.93.111:8082");
    CHECK(web_host_policy_entry_role("192.168.93.111:8082", "/appsite/g1/index.html", "GET",
                                     NULL, AICAM_FALSE, WEB_LISTENER_APP) == WEB_ENTRY_ALLOW);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/device/info", "GET",
                                NULL, AICAM_FALSE) == WEB_ENTRY_ALLOW);
}

static void test_default_fail_closed(void)
{
    web_host_policy_reset();
    CHECK(web_host_policy_classify("192.168.93.111") == WEB_HOST_UNKNOWN);
    CHECK(web_host_policy_origin_allowed("http://192.168.93.111", "192.168.93.111") == AICAM_FALSE);
    CHECK(web_host_policy_entry("192.168.93.111", "/api/v1/echo", "POST",
                                NULL, AICAM_FALSE) == WEB_ENTRY_UNKNOWN_HOST);
}

int main(void)
{
    test_classify();
    test_admin_refresh();
    test_origin_gate();
    test_entry();
    test_entry_role_app_port();
    test_origin_length_boundaries();
    test_netif_ip_candidate();
    test_conn_ip_same_origin();
    test_conn_ip_degenerate_addr();
    test_netif_degraded_retention();
    test_netif_entry_refresh();
    test_restart_recovery_sequence();
    test_default_fail_closed();
    printf("web_host_policy: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
