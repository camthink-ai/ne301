/**
 * @file cbor_enc.h
 * @brief Minimal CBOR encoder (RFC 8949)
 * @details Encode-only, definite-length subset used for compact telemetry
 *          payloads: maps, arrays, text strings, unsigned integers, null,
 *          and IEEE 754 half-precision floats. Writes are bounds-checked
 *          against a caller-owned buffer with a sticky overflow flag, so a
 *          whole document can be emitted before a single error check.
 *          No dynamic allocation, no decoder.
 */

#ifndef CBOR_ENC_H
#define CBOR_ENC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encoder state over a caller-owned output buffer
 */
typedef struct {
    uint8_t *buf;                  // Output buffer (caller-owned)
    size_t cap;                    // Buffer capacity in bytes
    size_t len;                    // Bytes written so far
    uint8_t overflow;              // Sticky flag: a write did not fit
} cbor_enc_t;

/**
 * @brief Initialize an encoder over a buffer
 * @param enc Encoder state
 * @param buf Output buffer (may be NULL only if cap is 0)
 * @param cap Buffer capacity in bytes
 */
void cbor_enc_init(cbor_enc_t *enc, uint8_t *buf, size_t cap);

/**
 * @brief Emit a definite-length map header
 * @param pairs Number of key/value pairs that will follow
 */
void cbor_enc_map(cbor_enc_t *enc, uint64_t pairs);

/**
 * @brief Emit a definite-length array header
 * @param items Number of items that will follow
 */
void cbor_enc_array(cbor_enc_t *enc, uint64_t items);

/**
 * @brief Emit a text string (UTF-8 expected, not validated)
 */
void cbor_enc_text(cbor_enc_t *enc, const char *s);

/**
 * @brief Emit the first n bytes of a text string
 */
void cbor_enc_text_n(cbor_enc_t *enc, const char *s, size_t n);

/**
 * @brief Emit an unsigned integer (shortest form)
 */
void cbor_enc_uint(cbor_enc_t *enc, uint64_t value);

/**
 * @brief Emit null
 */
void cbor_enc_null(cbor_enc_t *enc);

/**
 * @brief Emit a float as IEEE 754 half-precision (0xF9 + 2 bytes)
 * @details Converts with round-to-nearest-even; values exceeding half range
 *          become infinity. Intended for values in [0,1], where the maximum
 *          conversion error is 2^-12.
 */
void cbor_enc_f16(cbor_enc_t *enc, float value);

/**
 * @brief Check the document for overflow and get its length
 * @param enc Encoder state
 * @param out_len Receives the total bytes written (valid only on success)
 * @return 0 when the document fit the buffer, -1 when any write overflowed
 */
int cbor_enc_finish(const cbor_enc_t *enc, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // CBOR_ENC_H
