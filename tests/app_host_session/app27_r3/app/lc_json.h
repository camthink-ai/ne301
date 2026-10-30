
#ifndef LC_JSON_H
#define LC_JSON_H

#include <stddef.h>
#include <stdint.h>

typedef struct lc_json {
    char    *buf;
    uint32_t cap;
    uint32_t len;
    uint8_t  overflow;
} lc_json_t;

void lcj_init(lc_json_t *j, char *buf, uint32_t cap);
void lcj_raw(lc_json_t *j, const char *s);
void lcj_string(lc_json_t *j, const char *s);
void lcj_u32(lc_json_t *j, uint32_t v);
void lcj_i32(lc_json_t *j, int32_t v);
void lcj_bool(lc_json_t *j, int v);
void lcj_null(lc_json_t *j);

void lcj_permille(lc_json_t *j, uint32_t permille);

void lcj_coord(lc_json_t *j, float v);

#endif
