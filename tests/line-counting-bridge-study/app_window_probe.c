#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_stub.h"
#include "lc_bus.h"

static lcstub_t *stub;
static lc_app_api_v2_t table;
static lcbus_t bus;

static void setup(int tracks_enabled)
{
    stub = lcstub_new();
    if (stub == NULL) exit(2);
    lcstub_make_table(stub, &table);
    lcstub_set_model(stub, 1, 7, 3, 2, "od-demo", "1.2");
    lcstub_set_class(stub, 0, "person");
    lcstub_set_class(stub, 1, "car");
    lcbus_host_ops_t ops = *lcbus_abi_ops();
    ops.user = &table;
    lcbus_init(&bus, &ops);
    lcbus_restore(&bus);
    lcbus_rebind(&bus, 1);
    lc_bus_config_t config;
    lc_bus_config_defaults(&config);
    config.window_minutes = 1;
    config.tracks_report_enable = (uint8_t)tracks_enabled;
    if (lcbus_apply_config(&bus, &config) != 0) exit(3);
}

static void run_crossing(int down, uint32_t x_center_permille, uint32_t mono0)
{
    static const float ys_down[4] = { 0.30f, 0.44f, 0.58f, 0.72f };
    static const float ys_up[4] = { 0.72f, 0.58f, 0.44f, 0.30f };
    const float *ys = down ? ys_down : ys_up;
    float x = (float)x_center_permille / 1000.0f;
    for (int i = 0; i < 4; i++) {
        lcstub_det_t detection = { x - 0.05f, ys[i] - 0.02f, 0.1f, 0.04f, 0.9f, 0 };
        lcstub_push_frame(stub, (uint32_t)(i + 1), mono0 + (uint32_t)i * 100u,
                          7, 3, 0, 0, &detection, 1);
        uint8_t event[LC_APP_EVENT_BUF_CAP];
        uint32_t event_len = stub->event_len[stub->ev_head];
        memcpy(event, stub->events[stub->ev_head], event_len);
        stub->ev_head = (stub->ev_head + 1) % LCSTUB_MAX_EVENTS;
        stub->ev_count--;
        lcbus_on_event(&bus, event, event_len);
    }
}

static int write_report(const char *path)
{
    uint32_t window_in = bus.window_in;
    uint32_t window_out = bus.window_out;
    lcstub_set_tick(stub, 61000u);
    lcbus_on_idle(&bus);
    const lcstub_report_t *report = lcstub_report_by_seq(stub, bus.report_seq);
    if (report == NULL || report->len == 0u || report->len > LC_APP_REPORT_MAX) return 1;
    FILE *file = fopen(path, "wb");
    if (file == NULL) return 1;
    size_t wrote = fwrite(report->bytes, 1, report->len, file);
    int close_result = fclose(file);
    if (wrote != report->len || close_result != 0) return 1;
    printf("report_seq=%u len=%u window_in=%u window_out=%u tracks_enabled=%u\n",
           bus.report_seq, report->len, window_in, window_out,
           bus.cfg.tracks_report_enable);
    return 0;
}

static int generate(const char *path, int tracks_enabled)
{
    setup(tracks_enabled);
    run_crossing(1, 350u, 100u);
    run_crossing(0, 650u, 1000u);
    int result = write_report(path);
    lcstub_destroy(stub);
    stub = NULL;
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 3) return 64;
    if (generate(argv[1], 1) != 0) return 1;
    if (generate(argv[2], 0) != 0) return 1;
    return 0;
}
