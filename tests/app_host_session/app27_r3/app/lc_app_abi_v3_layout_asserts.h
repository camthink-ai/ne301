#ifndef LC_APP_ABI_V3_LAYOUT_ASSERTS_H
#define LC_APP_ABI_V3_LAYOUT_ASSERTS_H

_Static_assert(sizeof(void *) == 4, "v3 ABI requires sizeof(void*) == 4");
_Static_assert(sizeof(int32_t) == 4, "v3 ABI requires 32-bit int");
_Static_assert(sizeof(lc_app_api_v3_t) == 60, "v3 function table must be exactly 60 bytes");
_Static_assert(offsetof(lc_app_api_v3_t, table_size) == 0, "table_size offset 0");
_Static_assert(offsetof(lc_app_api_v3_t, abi_version) == 4, "abi_version offset 4");
_Static_assert(offsetof(lc_app_api_v3_t, log) == 8, "log offset 8");
_Static_assert(offsetof(lc_app_api_v3_t, tick_ms) == 12, "tick_ms offset 12");
_Static_assert(offsetof(lc_app_api_v3_t, event_next) == 16, "event_next offset 16");
_Static_assert(offsetof(lc_app_api_v3_t, model_meta) == 20, "model_meta offset 20");
_Static_assert(offsetof(lc_app_api_v3_t, class_name) == 24, "class_name offset 24");
_Static_assert(offsetof(lc_app_api_v3_t, report_submit) == 28, "report_submit offset 28");
_Static_assert(offsetof(lc_app_api_v3_t, report_status) == 32, "report_status offset 32");
_Static_assert(offsetof(lc_app_api_v3_t, state_read) == 36, "state_read offset 36");
_Static_assert(offsetof(lc_app_api_v3_t, state_commit) == 40, "state_commit offset 40");
_Static_assert(offsetof(lc_app_api_v3_t, should_stop) == 44, "should_stop offset 44");
_Static_assert(offsetof(lc_app_api_v3_t, control_next) == 48, "control_next offset 48");
_Static_assert(offsetof(lc_app_api_v3_t, control_reply) == 52, "control_reply offset 52");
_Static_assert(offsetof(lc_app_api_v3_t, snapshot_publish) == 56, "snapshot_publish offset 56");
_Static_assert(sizeof(void (*)(void)) == 4, "v3 ABI requires 32-bit function pointers");

#endif
