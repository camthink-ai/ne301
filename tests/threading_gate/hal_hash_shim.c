#include <string.h>
#include "stm32n6xx_hal.h"

static int g_fail_arming;

static const uint32_t sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t sha_ror(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

static void sha256_compress(uint32_t *st, const uint8_t *p)
{
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t t1;
    uint32_t t2;
    uint32_t i;

    for (i = 0u; i < 16u; i++) {
        w[i] = ((uint32_t)p[i * 4u] << 24) | ((uint32_t)p[i * 4u + 1u] << 16) |
               ((uint32_t)p[i * 4u + 2u] << 8) | (uint32_t)p[i * 4u + 3u];
    }
    for (i = 16u; i < 64u; i++) {
        uint32_t s0 = sha_ror(w[i - 15u], 7) ^ sha_ror(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
        uint32_t s1 = sha_ror(w[i - 2u], 17) ^ sha_ror(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    a = st[0]; b = st[1]; c = st[2]; d = st[3];
    e = st[4]; f = st[5]; g = st[6]; h = st[7];

    for (i = 0u; i < 64u; i++) {
        uint32_t s1 = sha_ror(e, 6) ^ sha_ror(e, 11) ^ sha_ror(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        t1 = h + s1 + ch + sha256_k[i] + w[i];
        uint32_t s0 = sha_ror(a, 2) ^ sha_ror(a, 13) ^ sha_ror(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        t2 = s0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
    st[4] += e; st[5] += f; st[6] += g; st[7] += h;
}

static void sha256_reset(HASH_HandleTypeDef *hhash)
{
    if (hhash->Init.Algorithm == HASH_ALGOSELECTION_SHA224) {
        hhash->state[0] = 0xc1059ed8u; hhash->state[1] = 0x367cd507u;
        hhash->state[2] = 0x3070dd17u; hhash->state[3] = 0xf70e5939u;
        hhash->state[4] = 0xffc00b31u; hhash->state[5] = 0x68581511u;
        hhash->state[6] = 0x64f98fa7u; hhash->state[7] = 0xbefa4fa4u;
    } else {
        hhash->state[0] = 0x6a09e667u; hhash->state[1] = 0xbb67ae85u;
        hhash->state[2] = 0x3c6ef372u; hhash->state[3] = 0xa54ff53au;
        hhash->state[4] = 0x510e527fu; hhash->state[5] = 0x9b05688cu;
        hhash->state[6] = 0x1f83d9abu; hhash->state[7] = 0x5be0cd19u;
    }
    hhash->block_len = 0u;
    hhash->total_len = 0u;
}

static void sha256_feed(HASH_HandleTypeDef *hhash, const uint8_t *data, size_t len)
{
    hhash->total_len += len;
    while (len > 0u) {
        size_t take = 64u - hhash->block_len;
        if (take > len) {
            take = len;
        }
        memcpy(hhash->block + hhash->block_len, data, take);
        hhash->block_len += take;
        data += take;
        len -= take;
        if (hhash->block_len == 64u) {
            sha256_compress(hhash->state, hhash->block);
            hhash->block_len = 0u;
        }
    }
}

static void sha256_digest(HASH_HandleTypeDef *hhash, uint8_t *out)
{
    uint8_t pad[72];
    size_t pad_len;
    uint64_t bits = hhash->total_len * 8u;
    uint32_t digest_words = (hhash->Init.Algorithm == HASH_ALGOSELECTION_SHA224) ? 7u : 8u;
    uint32_t i;

    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80u;
    pad_len = (hhash->block_len < 56u) ? (56u - hhash->block_len) : (120u - hhash->block_len);
    for (i = 0u; i < 8u; i++) {
        pad[pad_len + i] = (uint8_t)(bits >> (56u - i * 8u));
    }
    sha256_feed(hhash, pad, pad_len + 8u);

    for (i = 0u; i < digest_words; i++) {
        out[i * 4u] = (uint8_t)(hhash->state[i] >> 24);
        out[i * 4u + 1u] = (uint8_t)(hhash->state[i] >> 16);
        out[i * 4u + 2u] = (uint8_t)(hhash->state[i] >> 8);
        out[i * 4u + 3u] = (uint8_t)hhash->state[i];
    }
}

void hal_hash_shim_arm_fail(void)
{
    g_fail_arming = 1;
}

void hal_hash_shim_disarm_fail(void)
{
    g_fail_arming = 0;
}

int HAL_HASH_Init(HASH_HandleTypeDef *hhash)
{
    if (g_fail_arming) {
        return -1;
    }
    sha256_reset(hhash);
    return 0;
}

int HAL_HASH_DeInit(HASH_HandleTypeDef *hhash)
{
    sha256_reset(hhash);
    return 0;
}

int HAL_HASH_Suspend(HASH_HandleTypeDef *hhash, uint8_t *regs)
{
    (void)hhash;
    (void)regs;
    return 0;
}

int HAL_HASH_Resume(HASH_HandleTypeDef *hhash, const uint8_t *regs)
{
    (void)hhash;
    (void)regs;
    return 0;
}

int HAL_HASH_Accumulate(HASH_HandleTypeDef *hhash, uint8_t *data, size_t len, uint32_t timeout)
{
    (void)timeout;
    if (g_fail_arming) {
        return -1;
    }
    sha256_feed(hhash, data, len);
    return 0;
}

int HAL_HASH_AccumulateLast(HASH_HandleTypeDef *hhash, uint8_t *data, size_t len, uint8_t *out, uint32_t timeout)
{
    (void)timeout;
    if (g_fail_arming) {
        return -1;
    }
    sha256_feed(hhash, data, len);
    sha256_digest(hhash, out);
    return 0;
}

void __HAL_RCC_HASH_CLK_ENABLE(void)
{
}

void __disable_irq(void)
{
}

void __enable_irq(void)
{
}
