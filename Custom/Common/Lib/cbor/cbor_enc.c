/**
 * @file cbor_enc.c
 * @brief Minimal CBOR encoder (RFC 8949) - see cbor_enc.h
 */

#include "cbor_enc.h"

#include <string.h>

/* CBOR major types (RFC 8949 section 3.1), pre-shifted into the high 3 bits */
#define CBOR_MAJOR_UINT   0x00U
#define CBOR_MAJOR_TEXT   0x60U
#define CBOR_MAJOR_ARRAY  0x80U
#define CBOR_MAJOR_MAP    0xA0U
#define CBOR_SIMPLE_NULL  0xF6U
#define CBOR_FLOAT16      0xF9U

static void cbor_put_byte(cbor_enc_t *enc, uint8_t byte)
{
    if (enc->overflow || enc->len >= enc->cap) {
        enc->overflow = 1;
        return;
    }
    enc->buf[enc->len++] = byte;
}

static void cbor_put_bytes(cbor_enc_t *enc, const uint8_t *bytes, size_t n)
{
    if (enc->overflow || n > enc->cap - enc->len) {
        enc->overflow = 1;
        return;
    }
    memcpy(&enc->buf[enc->len], bytes, n);
    enc->len += n;
}

/**
 * @brief Emit a major-type header with its argument in shortest form
 */
static void cbor_put_head(cbor_enc_t *enc, uint8_t major, uint64_t value)
{
    if (value < 24U) {
        cbor_put_byte(enc, (uint8_t)(major | value));
    } else if (value <= 0xFFU) {
        cbor_put_byte(enc, (uint8_t)(major | 24U));
        cbor_put_byte(enc, (uint8_t)value);
    } else if (value <= 0xFFFFU) {
        cbor_put_byte(enc, (uint8_t)(major | 25U));
        cbor_put_byte(enc, (uint8_t)(value >> 8));
        cbor_put_byte(enc, (uint8_t)value);
    } else if (value <= 0xFFFFFFFFU) {
        cbor_put_byte(enc, (uint8_t)(major | 26U));
        cbor_put_byte(enc, (uint8_t)(value >> 24));
        cbor_put_byte(enc, (uint8_t)(value >> 16));
        cbor_put_byte(enc, (uint8_t)(value >> 8));
        cbor_put_byte(enc, (uint8_t)value);
    } else {
        cbor_put_byte(enc, (uint8_t)(major | 27U));
        for (int shift = 56; shift >= 0; shift -= 8) {
            cbor_put_byte(enc, (uint8_t)(value >> shift));
        }
    }
}

/**
 * @brief Convert float32 to IEEE 754 half with round-to-nearest-even
 * @details Overflow becomes infinity, underflow becomes signed zero; NaN is
 *          preserved as a quiet NaN. Rounding may carry into the exponent,
 *          which yields the correct next-power-of-two encoding.
 */
static uint16_t cbor_float_to_half(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));

    uint32_t sign = (bits >> 16) & 0x8000U;
    uint32_t exp32 = (bits >> 23) & 0xFFU;
    uint32_t mant = bits & 0x7FFFFFU;

    if (exp32 == 0xFFU) {
        return (uint16_t)(sign | 0x7C00U | (mant ? 0x0200U : 0U));
    }

    int32_t exp16 = (int32_t)exp32 - 127 + 15;
    if (exp16 >= 0x1F) {
        return (uint16_t)(sign | 0x7C00U);
    }
    if (exp16 <= 0) {
        if (exp16 < -10) {
            return (uint16_t)sign;
        }
        // Subnormal half: shift the full 24-bit significand into place
        mant |= 0x800000U;
        uint32_t shift = (uint32_t)(14 - exp16);
        uint16_t half = (uint16_t)(mant >> shift);
        uint32_t rem = mant & ((1U << shift) - 1U);
        uint32_t halfway = 1U << (shift - 1U);
        if (rem > halfway || (rem == halfway && (half & 1U))) {
            half++;
        }
        return (uint16_t)(sign | half);
    }

    uint16_t half = (uint16_t)(sign | ((uint32_t)exp16 << 10) | (mant >> 13));
    uint32_t rem = mant & 0x1FFFU;
    if (rem > 0x1000U || (rem == 0x1000U && (half & 1U))) {
        half++;
    }
    return half;
}

void cbor_enc_init(cbor_enc_t *enc, uint8_t *buf, size_t cap)
{
    enc->buf = buf;
    enc->cap = buf ? cap : 0;
    enc->len = 0;
    enc->overflow = 0;
}

void cbor_enc_map(cbor_enc_t *enc, uint64_t pairs)
{
    cbor_put_head(enc, CBOR_MAJOR_MAP, pairs);
}

void cbor_enc_array(cbor_enc_t *enc, uint64_t items)
{
    cbor_put_head(enc, CBOR_MAJOR_ARRAY, items);
}

void cbor_enc_text(cbor_enc_t *enc, const char *s)
{
    cbor_enc_text_n(enc, s, s ? strlen(s) : 0);
}

void cbor_enc_text_n(cbor_enc_t *enc, const char *s, size_t n)
{
    if (!s) {
        n = 0;
    }
    cbor_put_head(enc, CBOR_MAJOR_TEXT, n);
    if (n > 0) {
        cbor_put_bytes(enc, (const uint8_t *)s, n);
    }
}

void cbor_enc_uint(cbor_enc_t *enc, uint64_t value)
{
    cbor_put_head(enc, CBOR_MAJOR_UINT, value);
}

void cbor_enc_null(cbor_enc_t *enc)
{
    cbor_put_byte(enc, CBOR_SIMPLE_NULL);
}

void cbor_enc_f16(cbor_enc_t *enc, float value)
{
    uint16_t half = cbor_float_to_half(value);
    cbor_put_byte(enc, CBOR_FLOAT16);
    cbor_put_byte(enc, (uint8_t)(half >> 8));
    cbor_put_byte(enc, (uint8_t)half);
}

int cbor_enc_finish(const cbor_enc_t *enc, size_t *out_len)
{
    if (enc->overflow) {
        return -1;
    }
    if (out_len) {
        *out_len = enc->len;
    }
    return 0;
}
