#ifndef SHIM_MONGOOSE_H
#define SHIM_MONGOOSE_H
struct mg_mgr {
    int dummy;
};
struct mg_connection;
struct mg_http_message;
struct mg_timer;
struct mg_str {
    const char *buf;
    size_t len;
};
struct mg_str mg_str(const char *s);
struct mg_str *mg_http_get_header(struct mg_http_message *hm, const char *name);
struct mg_str mg_http_var(struct mg_str buf, struct mg_str name);
size_t mg_base64_encode(const unsigned char *src, size_t src_len, char *dst, size_t dst_len);
int mg_base64_decode(const char *src, size_t src_len, char *dst, size_t dst_len);
unsigned long mg_crc32(uint32_t crc, const char *buf, size_t len);
#endif
