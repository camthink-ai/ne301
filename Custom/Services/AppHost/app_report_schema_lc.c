#include "app_report_schema.h"

int32_t app_report_schema_lc_type_allowed(const jr_member_t *m_type,
                                          const char *json)
{
    if (jr_string_equals(m_type, json, APP_REPORT_TYPE_LINE_COUNTING) == 0) {
        return APP_REPORT_ABI_INCOMPATIBLE;
    }
    return APP_REPORT_ABI_OK;
}

int32_t app_report_schema_lc_fields_valid(const jr_member_t *members,
                                          size_t count)
{
    if (jr_object_member(members, count, "window") != 0 ||
        jr_object_member(members, count, "total") != 0 ||
        jr_object_member(members, count, "counter") != 0 ||
        jr_object_member(members, count, "target") != 0 ||
        jr_object_member(members, count, "model") != 0) {
        return APP_REPORT_ABI_INVALID_ARGUMENT;
    }
    return APP_REPORT_ABI_OK;
}
