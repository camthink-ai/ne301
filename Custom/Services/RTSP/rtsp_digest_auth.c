/**
 * @file rtsp_digest_auth.c
 * @brief RTSP Digest authentication implementation with built-in MD5
 */

#include "rtsp_digest_auth.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ==================== Minimal MD5 Implementation ==================== */

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t buffer[64];
} md5_ctx_t;

static const uint32_t md5_T[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const int md5_shift[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))
#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t M[16];

    for (int i = 0; i < 16; i++) {
        M[i] = (uint32_t)block[i * 4]
             | ((uint32_t)block[i * 4 + 1] << 8)
             | ((uint32_t)block[i * 4 + 2] << 16)
             | ((uint32_t)block[i * 4 + 3] << 24);
    }

    for (int i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16)      { f = F(b, c, d); g = (uint32_t)i; }
        else if (i < 32) { f = G(b, c, d); g = (uint32_t)((5 * i + 1) % 16); }
        else if (i < 48) { f = H(b, c, d); g = (uint32_t)((3 * i + 5) % 16); }
        else              { f = I(b, c, d); g = (uint32_t)((7 * i) % 16); }

        uint32_t temp = d;
        d = c;
        c = b;
        b = b + ROTL(a + f + md5_T[i] + M[g], md5_shift[i]);
        a = temp;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

static void md5_init(md5_ctx_t *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count = 0;
}

static void md5_update(md5_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t index = (uint32_t)(ctx->count & 0x3F);
    ctx->count += len;

    uint32_t i = 0;
    if (index) {
        uint32_t part = 64 - index;
        if (len >= part) {
            memcpy(ctx->buffer + index, data, part);
            md5_transform(ctx->state, ctx->buffer);
            i = part;
        } else {
            memcpy(ctx->buffer + index, data, len);
            return;
        }
    }

    for (; i + 64 <= len; i += 64) {
        md5_transform(ctx->state, data + i);
    }

    if (i < len) {
        memcpy(ctx->buffer, data + i, len - i);
    }
}

static void md5_final(md5_ctx_t *ctx, uint8_t digest[16])
{
    static const uint8_t padding[64] = {0x80, 0};
    uint64_t bits = ctx->count * 8;

    uint32_t padlen = (ctx->count & 0x3F) < 56 ? 56 - (ctx->count & 0x3F) : 120 - (ctx->count & 0x3F);
    md5_update(ctx, padding, padlen);

    uint8_t bits_buf[8];
    for (int i = 0; i < 8; i++) {
        bits_buf[i] = (uint8_t)(bits >> (i * 8));
    }
    md5_update(ctx, bits_buf, 8);

    for (int i = 0; i < 4; i++) {
        digest[i * 4]     = (uint8_t)(ctx->state[i]);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i] >> 24);
    }
}

/* ==================== Helper ==================== */

static void md5_hex(const char *input, uint32_t input_len, char *output)
{
    uint8_t digest[16];
    md5_ctx_t ctx;
    md5_init(&ctx);
    md5_update(&ctx, (const uint8_t *)input, input_len);
    md5_final(&ctx, digest);

    for (int i = 0; i < 16; i++) {
        snprintf(output + i * 2, 3, "%02x", digest[i]);
    }
    output[32] = '\0';
}

/* ==================== Public API ==================== */

void rtsp_digest_generate_nonce(char *nonce, size_t size)
{
    if (size < 65) {
        nonce[0] = '\0';
        return;
    }

    uint32_t t1 = (uint32_t)rand();
    uint32_t t2 = (uint32_t)rand();
    uint32_t t3 = (uint32_t)rand();
    uint32_t t4 = (uint32_t)rand();

    char raw[64];
    snprintf(raw, sizeof(raw), "%08lX%08lX%08lX%08lX",
             (unsigned long)t1, (unsigned long)t2,
             (unsigned long)t3, (unsigned long)t4);

    md5_hex(raw, strlen(raw), nonce);
}

void rtsp_digest_build_challenge(const char *nonce, const char *realm,
                                  char *header, size_t size)
{
    snprintf(header, size,
             "Digest realm=\"%s\", nonce=\"%s\"",
             realm ? realm : "NE301", nonce ? nonce : "");
}

static aicam_bool_t parse_quoted_value(const char *str, const char *key,
                                        char *value, uint32_t value_size)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=\"", key);

    const char *p = strstr(str, search);
    if (!p) return AICAM_FALSE;
    p += strlen(search);

    const char *end = strchr(p, '"');
    if (!end) return AICAM_FALSE;

    uint32_t len = end - p;
    if (len >= value_size) len = value_size - 1;
    memcpy(value, p, len);
    value[len] = '\0';
    return AICAM_TRUE;
}

aicam_bool_t rtsp_digest_verify(const char *auth_header, const char *method,
                                 const char *username, const char *password,
                                 const char *nonce, const char *realm)
{
    if (!auth_header || !method || !username || !password || !nonce) {
        return AICAM_FALSE;
    }

    if (strncasecmp(auth_header, "Digest ", 7) != 0) {
        return AICAM_FALSE;
    }
    const char *params = auth_header + 7;

    /* Static buffers - safe because RTSP is single-threaded */
    static char recv_username[128];
    static char recv_realm[128];
    static char recv_nonce[128];
    static char recv_uri[256];
    static char recv_response[64];

    memset(recv_username, 0, sizeof(recv_username));
    memset(recv_realm, 0, sizeof(recv_realm));
    memset(recv_nonce, 0, sizeof(recv_nonce));
    memset(recv_uri, 0, sizeof(recv_uri));
    memset(recv_response, 0, sizeof(recv_response));

    if (!parse_quoted_value(params, "username", recv_username, sizeof(recv_username)) ||
        !parse_quoted_value(params, "realm", recv_realm, sizeof(recv_realm)) ||
        !parse_quoted_value(params, "nonce", recv_nonce, sizeof(recv_nonce)) ||
        !parse_quoted_value(params, "uri", recv_uri, sizeof(recv_uri)) ||
        !parse_quoted_value(params, "response", recv_response, sizeof(recv_response))) {
        return AICAM_FALSE;
    }

    if (strcmp(recv_username, username) != 0) return AICAM_FALSE;
    if (strcmp(recv_nonce, nonce) != 0) return AICAM_FALSE;
    if (realm && strcmp(recv_realm, realm) != 0) return AICAM_FALSE;

    static char ha1_input[512], ha2_input[512], response_input[512];
    static char ha1[33], ha2[33], expected_response[33];

    snprintf(ha1_input, sizeof(ha1_input), "%s:%s:%s", username, realm ? realm : "NE301", password);
    md5_hex(ha1_input, strlen(ha1_input), ha1);

    snprintf(ha2_input, sizeof(ha2_input), "%s:%s", method, recv_uri);
    md5_hex(ha2_input, strlen(ha2_input), ha2);

    snprintf(response_input, sizeof(response_input), "%s:%s:%s", ha1, nonce, ha2);
    md5_hex(response_input, strlen(response_input), expected_response);

    return (strcmp(recv_response, expected_response) == 0) ? AICAM_TRUE : AICAM_FALSE;
}
