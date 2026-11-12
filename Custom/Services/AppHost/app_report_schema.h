#ifndef APP_REPORT_SCHEMA_H
#define APP_REPORT_SCHEMA_H

#include <stddef.h>
#include <stdint.h>

#include "app_report_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JR_OBJECT,
    JR_ARRAY,
    JR_STRING,
    JR_NUMBER,
    JR_TRUE,
    JR_FALSE,
    JR_NULL
} jr_type_t;

typedef struct {
    size_t raw_off;
    const char *name;
    size_t name_len;
    size_t value_off;
    size_t value_end;
    jr_type_t type;
} jr_member_t;

int jr_find(const jr_member_t *members, size_t count, const char *name,
            const jr_member_t **out);

int jr_object_member(const jr_member_t *members, size_t count, const char *name);

int jr_string_equals(const jr_member_t *m, const char *buf, const char *s);

int32_t app_report_schema_lc_type_allowed(const jr_member_t *m_type,
                                          const char *json);

int32_t app_report_schema_lc_fields_valid(const jr_member_t *members,
                                          size_t count);

#ifdef __cplusplus
}
#endif

#endif
