
#include "lc_json.h"
#include "lc_compat.h"

static void lcj_put(lc_json_t *j, char c)
{
    if (j->overflow) return;
    if (j->len + 1u >= j->cap) {
        j->overflow = 1u;
        if (j->cap > 0u) j->buf[j->len] = '\0';
        return;
    }
    j->buf[j->len++] = c;
    j->buf[j->len] = '\0';
}

static void lcj_puts(lc_json_t *j, const char *s)
{
    while (*s) {
        lcj_put(j, *s);
        s++;
    }
}

void lcj_init(lc_json_t *j, char *buf, uint32_t cap)
{
    j->buf = buf;
    j->cap = cap;
    j->len = 0u;
    j->overflow = 0u;
    if (cap > 0u) buf[0] = '\0';
}

void lcj_raw(lc_json_t *j, const char *s) { lcj_puts(j, s); }

void lcj_string(lc_json_t *j, const char *s)
{
    lcj_put(j, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            lcj_put(j, '\\');
            lcj_put(j, (char)c);
        } else if (c < 0x20u) {
            static const char hex[] = "0123456789abcdef";
            lcj_put(j, '\\');
            lcj_put(j, 'u');
            lcj_put(j, '0');
            lcj_put(j, '0');
            lcj_put(j, hex[(c >> 4) & 0xFu]);
            lcj_put(j, hex[c & 0xFu]);
        } else {
            lcj_put(j, (char)c);
        }
    }
    lcj_put(j, '"');
}

void lcj_u32(lc_json_t *j, uint32_t v)
{
    char tmp[10];
    uint32_t n = 0u;
    do {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    while (n) lcj_put(j, tmp[--n]);
}

void lcj_i32(lc_json_t *j, int32_t v)
{
    uint32_t m;
    if (v < 0) {
        lcj_put(j, '-');
        m = (uint32_t)(-(int64_t)v);
    } else {
        m = (uint32_t)v;
    }
    lcj_u32(j, m);
}

void lcj_bool(lc_json_t *j, int v) { lcj_puts(j, v ? "true" : "false"); }
void lcj_null(lc_json_t *j) { lcj_puts(j, "null"); }

void lcj_permille(lc_json_t *j, uint32_t permille)
{
    lcj_u32(j, permille / 1000u);
    lcj_put(j, '.');
    uint32_t f = permille % 1000u;
    lcj_put(j, (char)('0' + (f / 100u)));
    lcj_put(j, (char)('0' + ((f / 10u) % 10u)));
    lcj_put(j, (char)('0' + (f % 10u)));
}

void lcj_coord(lc_json_t *j, float v)
{

    if (!(v >= 0.0f)) v = 0.0f;
    if (v > 2.0f) v = 2.0f;
    uint32_t scaled = (uint32_t)(v * 1000000.0f + 0.5f);
    lcj_u32(j, scaled / 1000000u);
    lcj_put(j, '.');
    uint32_t f = scaled % 1000000u;
    uint32_t d = 100000u;
    for (int i = 0; i < 6; i++) {
        lcj_put(j, (char)('0' + (f / d)));
        f %= d;
        d /= 10u;
    }
}
