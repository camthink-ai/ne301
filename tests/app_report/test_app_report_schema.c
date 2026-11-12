#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "app_report_schema.h"

static int g_total;
static int g_fail;

#define CHECK(cond) \
    do { \
        g_total++; \
        if (!(cond)) { \
            g_fail++; \
            printf("FAIL app_report_schema:%d:%d %s\n", g_total, __LINE__, #cond); \
        } \
    } while (0)

static jr_member_t make_member(const char *json, const char *key_colon,
                               jr_type_t type)
{
    const char *kp = strstr(json, key_colon);
    const char *vp;
    jr_member_t m;
    size_t key_span = strlen(key_colon);

    memset(&m, 0, sizeof(m));
    CHECK(kp != NULL);
    if (kp == NULL) {
        return m;
    }
    vp = kp + key_span;
    m.raw_off = (size_t)(kp - json);
    m.name = json + m.raw_off + 1u;
    m.name_len = key_span - 3u;
    m.value_off = (size_t)(vp - json);
    m.type = type;
    if (type == JR_STRING) {
        const char *end = strchr(vp + 1, '"');

        CHECK(end != NULL);
        if (end != NULL) {
            m.value_end = (size_t)(end - json) + 1u;
        }
    } else if (type == JR_OBJECT) {
        const char *end = strchr(vp, '}');

        CHECK(end != NULL);
        if (end != NULL) {
            m.value_end = (size_t)(end - json) + 1u;
        }
    } else if (type == JR_ARRAY) {
        const char *end = strchr(vp, ']');

        CHECK(end != NULL);
        if (end != NULL) {
            m.value_end = (size_t)(end - json) + 1u;
        }
    } else {
        const char *p = vp;

        while (*p >= '0' && *p <= '9') {
            p++;
        }
        m.value_end = (size_t)(p - json);
    }
    return m;
}

static void test_type_allowed(void)
{
    const char *json =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1}";
    jr_member_t m = make_member(json, "\"type\":", JR_STRING);

    CHECK(app_report_schema_lc_type_allowed(&m, json) == APP_REPORT_ABI_OK);
}

static void test_type_refused(void)
{
    const char *cases[] = {
        "{\"schema_version\":1,\"type\":\"face_count\",\"report_seq\":1}",
        "{\"schema_version\":1,\"type\":\"line_counting2\",\"report_seq\":1}",
        "{\"schema_version\":1,\"type\":\"line_countin\",\"report_seq\":1}",
        "{\"schema_version\":1,\"type\":\"LINE_COUNTING\",\"report_seq\":1}",
        "{\"schema_version\":1,\"type\":\"line counting\",\"report_seq\":1}",
        "{\"schema_version\":1,\"type\":\"\",\"report_seq\":1}",
        "{\"schema_version\":1,\"type\":\"line_counting\\u0020x\",\"report_seq\":1}"
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        jr_member_t m = make_member(cases[i], "\"type\":", JR_STRING);

        CHECK(app_report_schema_lc_type_allowed(&m, cases[i]) ==
              APP_REPORT_ABI_INCOMPATIBLE);
    }
}

static void test_type_non_string_member(void)
{
    const char *json =
        "{\"schema_version\":1,\"type\":7,\"report_seq\":1}";
    jr_member_t m = make_member(json, "\"type\":", JR_NUMBER);

    CHECK(app_report_schema_lc_type_allowed(&m, json) == APP_REPORT_ABI_INCOMPATIBLE);
}

static void test_fields_valid(void)
{
    const char *json =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"window\":{},\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}";
    jr_member_t members[5];

    members[0] = make_member(json, "\"window\":", JR_OBJECT);
    members[1] = make_member(json, "\"total\":", JR_OBJECT);
    members[2] = make_member(json, "\"counter\":", JR_OBJECT);
    members[3] = make_member(json, "\"target\":", JR_OBJECT);
    members[4] = make_member(json, "\"model\":", JR_OBJECT);
    CHECK(app_report_schema_lc_fields_valid(members, 5u) == APP_REPORT_ABI_OK);
}

static void test_fields_refused(void)
{
    const char *missing_window =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}";
    const char *missing_total =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"window\":{},\"counter\":{},\"target\":{},\"model\":{}}";
    const char *missing_counter =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"window\":{},\"total\":{},\"target\":{},\"model\":{}}";
    const char *missing_target =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"window\":{},\"total\":{},\"counter\":{},\"model\":{}}";
    const char *missing_model =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"window\":{},\"total\":{},\"counter\":{},\"target\":{}}";
    const char *window_array =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"window\":[],\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}";
    const char *window_string =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"window\":\"w\",\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}";
    const char *window_number =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"window\":1,\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}";
    const char *window_true =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"window\":true,\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}";
    const char *window_null =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"window\":null,\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}";
    const char *model_array =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"window\":{},\"total\":{},\"counter\":{},\"target\":{},\"model\":[]}";
    jr_member_t members[5];

    members[0] = make_member(missing_window, "\"total\":", JR_OBJECT);
    members[1] = make_member(missing_window, "\"counter\":", JR_OBJECT);
    members[2] = make_member(missing_window, "\"target\":", JR_OBJECT);
    members[3] = make_member(missing_window, "\"model\":", JR_OBJECT);
    CHECK(app_report_schema_lc_fields_valid(members, 4u) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);

    members[0] = make_member(missing_total, "\"window\":", JR_OBJECT);
    members[1] = make_member(missing_total, "\"counter\":", JR_OBJECT);
    members[2] = make_member(missing_total, "\"target\":", JR_OBJECT);
    members[3] = make_member(missing_total, "\"model\":", JR_OBJECT);
    CHECK(app_report_schema_lc_fields_valid(members, 4u) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);

    members[0] = make_member(missing_counter, "\"window\":", JR_OBJECT);
    members[1] = make_member(missing_counter, "\"total\":", JR_OBJECT);
    members[2] = make_member(missing_counter, "\"target\":", JR_OBJECT);
    members[3] = make_member(missing_counter, "\"model\":", JR_OBJECT);
    CHECK(app_report_schema_lc_fields_valid(members, 4u) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);

    members[0] = make_member(missing_target, "\"window\":", JR_OBJECT);
    members[1] = make_member(missing_target, "\"total\":", JR_OBJECT);
    members[2] = make_member(missing_target, "\"counter\":", JR_OBJECT);
    members[3] = make_member(missing_target, "\"model\":", JR_OBJECT);
    CHECK(app_report_schema_lc_fields_valid(members, 4u) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);

    members[0] = make_member(missing_model, "\"window\":", JR_OBJECT);
    members[1] = make_member(missing_model, "\"total\":", JR_OBJECT);
    members[2] = make_member(missing_model, "\"counter\":", JR_OBJECT);
    members[3] = make_member(missing_model, "\"target\":", JR_OBJECT);
    CHECK(app_report_schema_lc_fields_valid(members, 4u) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);

    members[0] = make_member(window_array, "\"window\":", JR_ARRAY);
    members[1] = make_member(window_array, "\"total\":", JR_OBJECT);
    members[2] = make_member(window_array, "\"counter\":", JR_OBJECT);
    members[3] = make_member(window_array, "\"target\":", JR_OBJECT);
    members[4] = make_member(window_array, "\"model\":", JR_OBJECT);
    CHECK(app_report_schema_lc_fields_valid(members, 5u) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);

    members[0] = make_member(window_string, "\"window\":", JR_STRING);
    members[1] = make_member(window_string, "\"total\":", JR_OBJECT);
    members[2] = make_member(window_string, "\"counter\":", JR_OBJECT);
    members[3] = make_member(window_string, "\"target\":", JR_OBJECT);
    members[4] = make_member(window_string, "\"model\":", JR_OBJECT);
    CHECK(app_report_schema_lc_fields_valid(members, 5u) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);

    members[0] = make_member(window_number, "\"window\":", JR_NUMBER);
    members[1] = make_member(window_number, "\"total\":", JR_OBJECT);
    members[2] = make_member(window_number, "\"counter\":", JR_OBJECT);
    members[3] = make_member(window_number, "\"target\":", JR_OBJECT);
    members[4] = make_member(window_number, "\"model\":", JR_OBJECT);
    CHECK(app_report_schema_lc_fields_valid(members, 5u) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);

    members[0] = make_member(window_true, "\"window\":", JR_TRUE);
    members[1] = make_member(window_true, "\"total\":", JR_OBJECT);
    members[2] = make_member(window_true, "\"counter\":", JR_OBJECT);
    members[3] = make_member(window_true, "\"target\":", JR_OBJECT);
    members[4] = make_member(window_true, "\"model\":", JR_OBJECT);
    CHECK(app_report_schema_lc_fields_valid(members, 5u) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);

    members[0] = make_member(window_null, "\"window\":", JR_NULL);
    members[1] = make_member(window_null, "\"total\":", JR_OBJECT);
    members[2] = make_member(window_null, "\"counter\":", JR_OBJECT);
    members[3] = make_member(window_null, "\"target\":", JR_OBJECT);
    members[4] = make_member(window_null, "\"model\":", JR_OBJECT);
    CHECK(app_report_schema_lc_fields_valid(members, 5u) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);

    members[0] = make_member(model_array, "\"window\":", JR_OBJECT);
    members[1] = make_member(model_array, "\"total\":", JR_OBJECT);
    members[2] = make_member(model_array, "\"counter\":", JR_OBJECT);
    members[3] = make_member(model_array, "\"target\":", JR_OBJECT);
    members[4] = make_member(model_array, "\"model\":", JR_ARRAY);
    CHECK(app_report_schema_lc_fields_valid(members, 5u) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
}

static void test_field_name_must_match_exactly(void)
{
    const char *json =
        "{\"schema_version\":1,\"type\":\"line_counting\",\"report_seq\":1,"
        "\"windowx\":{},\"total\":{},\"counter\":{},\"target\":{},\"model\":{}}";
    jr_member_t members[5];

    members[0] = make_member(json, "\"windowx\":", JR_OBJECT);
    members[1] = make_member(json, "\"total\":", JR_OBJECT);
    members[2] = make_member(json, "\"counter\":", JR_OBJECT);
    members[3] = make_member(json, "\"target\":", JR_OBJECT);
    members[4] = make_member(json, "\"model\":", JR_OBJECT);
    CHECK(app_report_schema_lc_fields_valid(members, 5u) ==
          APP_REPORT_ABI_INVALID_ARGUMENT);
}

int main(void)
{
    test_type_allowed();
    test_type_refused();
    test_type_non_string_member();
    test_fields_valid();
    test_fields_refused();
    test_field_name_must_match_exactly();
    printf("app_report_schema: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
