#include <string.h>

#include "app_sidecar_verify.h"

typedef struct {
    uint32_t state[8];
    uint64_t total;
    uint8_t buf[64u];
    size_t buf_len;
} app_sidecar_sha256_t;

static const uint8_t SC_MAGIC[APP_SIDECAR_MAGIC_SIZE] = {
    0x4Eu, 0x45u, 0x55u, 0x49u, 0x01u, 0x00u, 0x00u, 0x00u
};

static const uint8_t SC_UNSIGNED_MAGIC[APP_SIDECAR_MAGIC_SIZE] = {
    0x4Eu, 0x45u, 0x55u, 0x49u, 0x02u, 0x00u, 0x00u, 0x00u
};

static const uint32_t SC_SHA_K[64u] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

static uint32_t sc_ror32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

static uint32_t sc_sha_load32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void sc_sha_store32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void sc_sha_compress(app_sidecar_sha256_t *s, const uint8_t block[64u])
{
    uint32_t w[64u];
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
    size_t i;

    for (i = 0u; i < 16u; i++) {
        w[i] = sc_sha_load32(block + (i * 4u));
    }
    for (i = 16u; i < 64u; i++) {
        uint32_t s0 = sc_ror32(w[i - 15u], 7u) ^ sc_ror32(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
        uint32_t s1 = sc_ror32(w[i - 2u], 17u) ^ sc_ror32(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    a = s->state[0];
    b = s->state[1];
    c = s->state[2];
    d = s->state[3];
    e = s->state[4];
    f = s->state[5];
    g = s->state[6];
    h = s->state[7];

    for (i = 0u; i < 64u; i++) {
        uint32_t s1 = sc_ror32(e, 6u) ^ sc_ror32(e, 11u) ^ sc_ror32(e, 25u);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t s0 = sc_ror32(a, 2u) ^ sc_ror32(a, 13u) ^ sc_ror32(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + s1 + ch + SC_SHA_K[i] + w[i];
        t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    s->state[0] += a;
    s->state[1] += b;
    s->state[2] += c;
    s->state[3] += d;
    s->state[4] += e;
    s->state[5] += f;
    s->state[6] += g;
    s->state[7] += h;
}

static void sc_sha_init(app_sidecar_sha256_t *s)
{
    s->state[0] = 0x6A09E667u;
    s->state[1] = 0xBB67AE85u;
    s->state[2] = 0x3C6EF372u;
    s->state[3] = 0xA54FF53Au;
    s->state[4] = 0x510E527Fu;
    s->state[5] = 0x9B05688Cu;
    s->state[6] = 0x1F83D9ABu;
    s->state[7] = 0x5BE0CD19u;
    s->total = 0u;
    s->buf_len = 0u;
}

static void sc_sha_update(app_sidecar_sha256_t *s, const uint8_t *data, size_t len)
{
    size_t pos = 0u;

    s->total += (uint64_t)len;
    while (len > 0u) {
        size_t take = 64u - s->buf_len;
        if (take > len) {
            take = len;
        }
        (void)memcpy(&s->buf[s->buf_len], data + pos, take);
        s->buf_len += take;
        pos += take;
        len -= take;
        if (s->buf_len == 64u) {
            sc_sha_compress(s, s->buf);
            s->buf_len = 0u;
        }
    }
}

static void sc_sha_final(app_sidecar_sha256_t *s, uint8_t out[APP_SIDECAR_SHA256_LEN])
{
    uint8_t tail[72u];
    uint64_t bits = s->total * 8u;
    size_t pad_len;
    size_t i;

    (void)memset(tail, 0, sizeof(tail));
    tail[0] = 0x80u;
    pad_len = (s->buf_len < 56u) ? (56u - s->buf_len) : (120u - s->buf_len);
    sc_sha_store32(&tail[pad_len], (uint32_t)(bits >> 32));
    sc_sha_store32(&tail[pad_len + 4u], (uint32_t)bits);
    sc_sha_update(s, tail, pad_len + 8u);

    for (i = 0u; i < 8u; i++) {
        sc_sha_store32(&out[i * 4u], s->state[i]);
    }
}

static uint16_t sc_rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t sc_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t sc_fold(uint8_t c)
{
    if ((c >= (uint8_t)'a') && (c <= (uint8_t)'z')) {
        return (uint8_t)(c - 32u);
    }
    return c;
}

static int sc_path_segment_valid(const uint8_t *s, size_t len)
{
    size_t i;

    if (len == 0u) {
        return 0;
    }
    if (!((s[0] >= (uint8_t)'A' && s[0] <= (uint8_t)'Z') ||
          (s[0] >= (uint8_t)'a' && s[0] <= (uint8_t)'z') ||
          (s[0] >= (uint8_t)'0' && s[0] <= (uint8_t)'9'))) {
        return 0;
    }
    for (i = 1u; i < len; i++) {
        uint8_t c = s[i];
        if (!((c >= (uint8_t)'A' && c <= (uint8_t)'Z') ||
              (c >= (uint8_t)'a' && c <= (uint8_t)'z') ||
              (c >= (uint8_t)'0' && c <= (uint8_t)'9') ||
              (c == (uint8_t)'.' || c == (uint8_t)'_' || c == (uint8_t)'-'))) {
            return 0;
        }
    }
    return 1;
}

static int sc_path_valid(const uint8_t *path, size_t len)
{
    size_t seg_start = 0u;
    size_t i;

    if ((len < 1u) || (len > APP_SIDECAR_PATH_MAX)) {
        return 0;
    }
    if ((path[0] == (uint8_t)'/') || (path[len - 1u] == (uint8_t)'/')) {
        return 0;
    }
    for (i = 0u; i < len; i++) {
        uint8_t c = path[i];
        if (c == (uint8_t)'/') {
            if (i == seg_start) {
                return 0;
            }
            if (sc_path_segment_valid(path + seg_start, i - seg_start) == 0) {
                return 0;
            }
            seg_start = i + 1u;
        } else if ((c < 0x21u) || (c > 0x7Eu)) {
            return 0;
        }
    }
    if (len == seg_start) {
        return 0;
    }
    return sc_path_segment_valid(path + seg_start, len - seg_start);
}

static int sc_path_greater(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen)
{
    size_t n = (alen < blen) ? alen : blen;
    size_t i;

    for (i = 0u; i < n; i++) {
        if (a[i] != b[i]) {
            return (a[i] > b[i]) ? 1 : 0;
        }
    }
    return (alen > blen) ? 1 : 0;
}

static int sc_path_fold_eq(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen)
{
    size_t i;

    if (alen != blen) {
        return 0;
    }
    for (i = 0u; i < alen; i++) {
        if (sc_fold(a[i]) != sc_fold(b[i])) {
            return 0;
        }
    }
    return 1;
}

static int sc_identifier_valid(const uint8_t *field, size_t slot_len)
{
    size_t text_len = 0u;
    size_t i;

    if (slot_len == 0u) {
        return 0;
    }
    while ((text_len < slot_len) && (field[text_len] != 0x00u)) {
        text_len++;
    }
    for (i = text_len; i < slot_len; i++) {
        if (field[i] != 0x00u) {
            return 0;
        }
    }
    if (text_len == 0u) {
        return 0;
    }
    if ((field[0] < (uint8_t)'a') || (field[0] > (uint8_t)'z')) {
        return 0;
    }
    for (i = 0u; i < text_len; i++) {
        uint8_t c = field[i];
        if (!((c >= (uint8_t)'a' && c <= (uint8_t)'z') ||
              (c >= (uint8_t)'0' && c <= (uint8_t)'9') ||
              (c == (uint8_t)'-'))) {
            return 0;
        }
    }
    if (text_len > 1u) {
        if (field[text_len - 1u] == (uint8_t)'-') {
            return 0;
        }
    }
    return 1;
}

static int sc_slot_equals(const uint8_t *slot, size_t slot_len, const char *str, size_t str_len)
{
    size_t i;

    if ((str == NULL) || (str_len == 0u) || (str_len > slot_len)) {
        return 0;
    }
    if (memcmp(slot, str, str_len) != 0) {
        return 0;
    }
    for (i = str_len; i < slot_len; i++) {
        if (slot[i] != 0x00u) {
            return 0;
        }
    }
    return 1;
}

static app_sidecar_result_t sc_verify_unsigned_bundle(
    const uint8_t *data,
    size_t len,
    const app_sidecar_trust_t *trust,
    app_sidecar_file_fn file_cb,
    void *user,
    app_sidecar_verdict_t *out)
{
    const uint8_t *manifest;
    const uint8_t *payload;
    uint32_t manifest_len;
    uint32_t payload_len;
    uint16_t file_count;
    uint16_t index_html_count = 0u;
    uint16_t index;
    uint64_t total_len = 0u;
    size_t manifest_offset;
    size_t payload_offset = 0u;
    const uint8_t *previous_path = NULL;
    uint16_t previous_path_len = 0u;
    app_sidecar_sha256_t sha;
    uint8_t digest[APP_SIDECAR_SHA256_LEN];
    uint8_t zero[APP_SIDECAR_SHA256_LEN] = {0};
    char app_id[APP_SIDECAR_VERDICT_APP_SLOT];
    size_t app_id_len;

    if (data == NULL || trust == NULL || out == NULL || trust->publisher_id == NULL ||
        trust->app_id == NULL ||
        trust->native_tbs_sha256 == NULL ||
        len < APP_SIDECAR_HEADER_SIZE + APP_SIDECAR_UNSIGNED_MANIFEST_FIXED_SIZE) {
        return APP_SIDECAR_ERR_PARAM;
    }
    if (memcmp(data, SC_UNSIGNED_MAGIC, APP_SIDECAR_MAGIC_SIZE) != 0) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }
    manifest_len = sc_rd32(data + 8u);
    payload_len = sc_rd32(data + 12u);
    if (manifest_len < APP_SIDECAR_UNSIGNED_MANIFEST_FIXED_SIZE +
                           APP_SIDECAR_RECORD_FIXED_SIZE + 1u ||
        manifest_len > APP_SIDECAR_MANIFEST_LEN_MAX || payload_len == 0u ||
        payload_len > APP_SIDECAR_ASSET_PAYLOAD_MAX ||
        (uint64_t)APP_SIDECAR_HEADER_SIZE + manifest_len + payload_len != len) {
        return APP_SIDECAR_ERR_LIMIT;
    }
    manifest = data + APP_SIDECAR_HEADER_SIZE;
    payload = manifest + manifest_len;
    if (!sc_identifier_valid(manifest + 16u, APP_SIDECAR_APP_ID_SLOT) ||
        !sc_slot_equals(manifest + 16u, APP_SIDECAR_APP_ID_SLOT,
                        trust->app_id, trust->app_id_len) ||
        memcmp(manifest, zero, APP_SIDECAR_PUBLISHER_ID_SLOT) != 0 ||
        memcmp(manifest + 48u, zero, APP_SIDECAR_SHA256_LEN) != 0 ||
        memcmp(manifest + 80u, trust->native_tbs_sha256,
               APP_SIDECAR_SHA256_LEN) != 0 ||
        memcmp(manifest + 130u, zero, 6u) != 0) {
        return APP_SIDECAR_ERR_IDENTITY_MISMATCH;
    }
    if (sc_rd16(manifest + 112u) < APP_SIDECAR_VERSION_MAJOR_MIN ||
        sc_rd16(manifest + 114u) == 0u || sc_rd16(manifest + 116u) == 0u) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }
    file_count = sc_rd16(manifest + 118u);
    if (file_count == 0u || file_count > APP_SIDECAR_FILE_COUNT_MAX) {
        return APP_SIDECAR_ERR_LIMIT;
    }
    if (sc_rd32(manifest + 120u) != APP_SIDECAR_HOST_UI_ABI) {
        return APP_SIDECAR_ERR_ABI_INCOMPATIBLE;
    }
    if (!sc_identifier_valid((const uint8_t *)trust->publisher_id,
                             trust->publisher_id_len) ||
        strcmp(trust->publisher_id, "admin-installed") != 0) {
        return APP_SIDECAR_ERR_IDENTITY_MISMATCH;
    }
    app_id_len = trust->app_id_len;
    if (app_id_len == 0u || app_id_len >= sizeof(app_id)) {
        return APP_SIDECAR_ERR_IDENTITY_MISMATCH;
    }
    (void)memset(app_id, 0, sizeof(app_id));
    (void)memcpy(app_id, trust->app_id, app_id_len);
    manifest_offset = APP_SIDECAR_UNSIGNED_MANIFEST_FIXED_SIZE;
    for (index = 0u; index < file_count; index++) {
        const uint8_t *record;
        const uint8_t *path;
        uint16_t path_len;
        uint32_t file_len;
        uint64_t record_end;
        uint16_t prior;
        app_sidecar_sha256_t file_sha;
        app_sidecar_file_desc_t desc;

        if (manifest_offset > manifest_len ||
            (size_t)manifest_len - manifest_offset < APP_SIDECAR_RECORD_FIXED_SIZE) {
            return APP_SIDECAR_ERR_BAD_FORMAT;
        }
        record = manifest + manifest_offset;
        path_len = sc_rd16(record);
        file_len = sc_rd32(record + 2u);
        record_end = (uint64_t)manifest_offset + APP_SIDECAR_RECORD_FIXED_SIZE + path_len;
        if (path_len == 0u || path_len > APP_SIDECAR_PATH_MAX || file_len == 0u ||
            file_len > APP_SIDECAR_FILE_LEN_MAX || record_end > manifest_len) {
            return APP_SIDECAR_ERR_LIMIT;
        }
        path = record + APP_SIDECAR_RECORD_FIXED_SIZE;
        if (sc_path_valid(path, path_len) == 0) {
            return APP_SIDECAR_ERR_BAD_FORMAT;
        }
        if (previous_path != NULL) {
            if (sc_path_greater(path, path_len, previous_path, previous_path_len) == 0) {
                return APP_SIDECAR_ERR_BAD_FORMAT;
            }
        }
        for (prior = 0u; prior < index; prior++) {
            size_t prior_offset = APP_SIDECAR_UNSIGNED_MANIFEST_FIXED_SIZE;
            uint16_t step;
            for (step = 0u; step < prior; step++) {
                prior_offset += APP_SIDECAR_RECORD_FIXED_SIZE + sc_rd16(manifest + prior_offset);
            }
            if (sc_path_fold_eq(path,
                                path_len,
                                manifest + prior_offset + APP_SIDECAR_RECORD_FIXED_SIZE,
                                sc_rd16(manifest + prior_offset)) != 0) {
                return APP_SIDECAR_ERR_BAD_FORMAT;
            }
        }
        if (path_len == 10u && memcmp(path, "index.html", 10u) == 0) {
            index_html_count++;
        }
        if ((uint64_t)payload_offset + file_len > payload_len) {
            return APP_SIDECAR_ERR_BAD_FORMAT;
        }
        sc_sha_init(&file_sha);
        sc_sha_update(&file_sha, payload + payload_offset, file_len);
        sc_sha_final(&file_sha, digest);
        if (memcmp(record + 6u, digest, sizeof(digest)) != 0) {
            return APP_SIDECAR_ERR_ASSET_INVALID;
        }
        (void)memset(&desc, 0, sizeof(desc));
        desc.file_len = file_len;
        (void)memcpy(desc.sha256, digest, sizeof(desc.sha256));
        (void)memcpy(desc.path, path, path_len);
        desc.path[path_len] = '\0';
        if (file_cb != NULL) {
            file_cb(user, &desc);
        }
        payload_offset += file_len;
        total_len += file_len;
        previous_path = path;
        previous_path_len = path_len;
        manifest_offset = (size_t)record_end;
    }
    if (manifest_offset != manifest_len || total_len != payload_len ||
        payload_offset != payload_len || index_html_count != 1u) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }
    sc_sha_init(&sha);
    sc_sha_update(&sha, data, len);
    sc_sha_final(&sha, digest);
    (void)memset(out, 0, sizeof(*out));
    (void)memcpy(out->publisher_id, "admin-installed", 15u);
    (void)memcpy(out->app_id, app_id, app_id_len);
    out->version_major = sc_rd16(manifest + 112u);
    out->version_minor = sc_rd16(manifest + 114u);
    out->version_patch = sc_rd16(manifest + 116u);
    out->host_ui_abi = sc_rd32(manifest + 120u);
    out->native_version[0] = sc_rd16(manifest + 124u);
    out->native_version[1] = sc_rd16(manifest + 126u);
    out->native_version[2] = sc_rd16(manifest + 128u);
    out->file_count = file_count;
    out->manifest_len = manifest_len;
    out->asset_payload_len = payload_len;
    out->signature_len = 0u;
    (void)memcpy(out->ui_tbs_sha256, digest, sizeof(digest));
    return APP_SIDECAR_OK;
}

app_sidecar_result_t app_sidecar_verify_bundle(const uint8_t *data,
                                               size_t len,
                                               const app_sidecar_trust_t *trust,
                                               app_sidecar_file_fn file_cb,
                                               void *user,
                                               app_sidecar_verdict_t *out)
{
    if (data != NULL && len >= APP_SIDECAR_MAGIC_SIZE &&
        memcmp(data, SC_UNSIGNED_MAGIC, APP_SIDECAR_MAGIC_SIZE) == 0) {
        return sc_verify_unsigned_bundle(data, len, trust, file_cb, user, out);
    }
    if (data != NULL && len >= APP_SIDECAR_MAGIC_SIZE &&
        memcmp(data, SC_MAGIC, APP_SIDECAR_MAGIC_SIZE) == 0) {
        return APP_SIDECAR_ERR_LEGACY_SIGNED_FORMAT;
    }
    if (data == NULL || out == NULL) {
        return APP_SIDECAR_ERR_PARAM;
    }
    if (len < APP_SIDECAR_MAGIC_SIZE) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }
    return APP_SIDECAR_ERR_UNKNOWN_FORMAT;
}

const char *app_sidecar_result_str(app_sidecar_result_t result)
{
    switch (result) {
    case APP_SIDECAR_OK:
        return "ok";
    case APP_SIDECAR_ERR_PARAM:
        return "param";
    case APP_SIDECAR_ERR_NO_TRUST:
        return "no_trust";
    case APP_SIDECAR_ERR_BAD_FORMAT:
        return "bad_format";
    case APP_SIDECAR_ERR_LIMIT:
        return "limit";
    case APP_SIDECAR_ERR_ABI_INCOMPATIBLE:
        return "abi_incompatible";
    case APP_SIDECAR_ERR_IDENTITY_MISMATCH:
        return "identity_mismatch";
    case APP_SIDECAR_ERR_ASSET_INVALID:
        return "asset_invalid";
    case APP_SIDECAR_ERR_SIGNATURE_INVALID:
        return "legacy_signed_format";
    case APP_SIDECAR_ERR_LEGACY_SIGNED_FORMAT:
        return "legacy_signed_format";
    case APP_SIDECAR_ERR_UNKNOWN_FORMAT:
        return "unknown_format";
    default:
        return "unknown";
    }
}
