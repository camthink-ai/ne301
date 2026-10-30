#define main app_report_existing_main
#include "../app_report/test_app_report.c"
#undef main

static size_t read_report(const char *path, char *out, size_t cap)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 0u;
    size_t len = fread(out, 1u, cap, file);
    int extra = fgetc(file);
    int read_error = ferror(file);
    fclose(file);
    if (extra != EOF || read_error) return 0u;
    return len;
}

int main(int argc, char **argv)
{
    char report[APP_REPORT_INPUT_MAX + 2u];
    char oversized[APP_REPORT_INPUT_MAX + 1u];
    uint32_t boot = 0u;
    app_report_entry_info_t info;
    app_report_stats_t stats;
    app_report_session_t replacement;
    if (argc != 2) return 64;
    size_t len = read_report(argv[1], report, sizeof(report));
    if (len == 0u || len > APP_REPORT_INPUT_MAX) return 1;
    reset_all();
    if (app_report_submit(&g_session, report, (uint32_t)len, 1u, &boot) != APP_REPORT_ABI_OK) return 2;
    if (app_report_service_queue_count() != 1u) return 3;
    if (app_report_service_entry(0u, &info) != AICAM_TRUE) return 4;
    if (info.report_seq != 1u || info.host_boot_id != boot || info.payload_len == 0u) return 5;
    uint32_t mqtt_state = 0u;
    uint32_t webhook_state = 0u;
    if (app_report_status(boot, 1u, &mqtt_state, &webhook_state) != APP_REPORT_ABI_OK) return 6;
    if (mqtt_state != APP_REPORT_MQTT_PENDING || webhook_state != APP_REPORT_WEBHOOK_PENDING) return 7;
    memcpy(oversized, report, len);
    memset(oversized + len, ' ', sizeof(oversized) - len);
    if (app_report_submit(&g_session, oversized, (uint32_t)sizeof(oversized), 2u, &boot) != APP_REPORT_ABI_QUOTA_EXCEEDED) return 8;
    session_fill(&replacement, "line-crossing", 0x5A, APP_REPORT_INPUT_MAX, AICAM_TRUE);
    if (app_report_service_set_session(&replacement) != APP_REPORT_ABI_OK) return 9;
    if (app_report_submit(&g_session, report, (uint32_t)len, 2u, &boot) != APP_REPORT_ABI_UNAUTHORIZED) return 10;
    if (app_report_service_set_session(NULL) != APP_REPORT_ABI_OK) return 11;
    if (app_report_submit(&replacement, report, (uint32_t)len, 2u, &boot) != APP_REPORT_ABI_UNAUTHORIZED) return 12;
    app_report_service_get_stats(&stats);
    printf("host_submit=OK queued=%u seq=%u payload_len=%zu mqtt=%u webhook=%u quota=%u identity_swap=UNAUTHORIZED permission_loss=UNAUTHORIZED\n",
           app_report_service_queue_count(), info.report_seq, info.payload_len,
           mqtt_state, webhook_state, stats.rejected_quota);
    return 0;
}
