import argparse
import hashlib
import os
import struct

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

MAGIC = bytes([0x4E, 0x45, 0x55, 0x49, 0x01, 0x00, 0x00, 0x00])
TEST_PUBLISHER_ID = b"cnt-publisher"
TEST_APP_ID = b"line-counting-ui"
NATIVE_TBS_SEED = b"ne301-native-tbs-fixture-v1"
ANCHOR_SEED = b"ne301-sidecar-host-test-anchor-v1"
FOREIGN_SEED = b"ne301-sidecar-foreign-reject-anchor-v1"
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551

COUNTING_FILES = [
    ("assets/index-BqeWX5wD.js", 260143),
    ("assets/index-DUrZLGu5.css", 45942),
    ("assets/videoWorker-Cfb7ojPI.js", 647),
    ("assets/websocket-init-yiJ-u6et.js", 2000),
    ("index.html", 506),
    ("libs/FileSaver.min.js", 6954),
    ("libs/jmuxer.min.js", 100844),
]
SMALL_FILES = [
    ("assets/app.min.js", 512),
    ("index.html", 128),
    ("libs/lib-a.min.js", 256),
]

OK = "APP_SIDECAR_OK"
BAD_FORMAT = "APP_SIDECAR_ERR_BAD_FORMAT"
LIMIT = "APP_SIDECAR_ERR_LIMIT"
ABI = "APP_SIDECAR_ERR_ABI_INCOMPATIBLE"
IDENTITY = "APP_SIDECAR_ERR_IDENTITY_MISMATCH"
ASSET = "APP_SIDECAR_ERR_ASSET_INVALID"
SIG = "APP_SIDECAR_ERR_SIGNATURE_INVALID"
NO_TRUST = "APP_SIDECAR_ERR_NO_TRUST"


def le16(v):
    return struct.pack("<H", v)


def le32(v):
    return struct.pack("<I", v)


def sha256(data):
    return hashlib.sha256(data).digest()


def hex32(data):
    return data.hex()


def deterministic_content(name, size):
    out = bytearray()
    counter = 0
    while len(out) < size:
        out.extend(sha256(name.encode() + b":" + str(counter).encode()))
        counter += 1
    return bytes(out[:size])


def derive_key(seed):
    scalar = int.from_bytes(sha256(seed), "big") % (N - 1) + 1
    return ec.derive_private_key(scalar, ec.SECP256R1())


def spki_der(key):
    return key.public_key().public_bytes(Encoding.DER, PublicFormat.SubjectPublicKeyInfo)


def sign_tbs(key, tbs):
    return key.sign(tbs, ec.ECDSA(hashes.SHA256()))


def sign_tbs_with_pad(key, tbs, field):
    for _ in range(2048):
        signature = sign_tbs(key, tbs)
        r, s = decode_dss_signature(signature)
        value = r if field == "r" else s
        if value.to_bytes(32, "big")[0] & 0x80:
            return signature
    raise RuntimeError("unable to generate high-bit %s signature integer" % field)


def sign_tbs_with_length(key, tbs, length, excluded):
    for _ in range(2048):
        signature = sign_tbs(key, tbs)
        if (len(signature) == length) and (signature != excluded):
            return signature
    raise RuntimeError("unable to generate a distinct DER signature with matching length")


def remove_integer_sign_pad(signature, field):
    out = bytearray(signature)
    r_len = out[3]
    s_tag = 4 + r_len
    s_len = out[s_tag + 1]
    start = 4 if field == "r" else s_tag + 2
    length_pos = 3 if field == "r" else s_tag + 1
    assert out[0] == 0x30
    assert out[2] == 0x02
    assert out[s_tag] == 0x02
    assert out[start] == 0x00
    assert out[start + 1] & 0x80
    del out[start]
    out[length_pos] -= 1
    out[1] -= 1
    assert out[3] == r_len - (1 if field == "r" else 0)
    assert out[s_tag - (1 if field == "r" else 0) + 1] == s_len - (1 if field == "s" else 0)
    return bytes(out)


def build_manifest(pub, app, key_sha, native_tbs, version, file_count, abi, reserved, records):
    m = bytearray()
    m.extend(pub.ljust(16, b"\x00"))
    m.extend(app.ljust(32, b"\x00"))
    m.extend(key_sha)
    m.extend(native_tbs)
    m.extend(le16(version[0]))
    m.extend(le16(version[1]))
    m.extend(le16(version[2]))
    m.extend(le16(file_count))
    m.extend(le32(abi))
    m.extend(le32(reserved))
    m.extend(records)
    return bytes(m)


def build_records(files):
    out = bytearray()
    for path, data in files:
        out.extend(le16(len(path)))
        out.extend(le32(len(data)))
        out.extend(sha256(data))
        out.extend(path)
    return bytes(out)


def build_from_parts(key, pub, app, key_sha, native_tbs, version, file_count, abi, reserved,
                     records, payload, skip_sign=False):
    manifest = build_manifest(pub, app, key_sha, native_tbs, version, file_count, abi,
                              reserved, records)
    tbs = MAGIC + le32(len(manifest)) + le32(len(payload)) + manifest + payload
    if skip_sign:
        return tbs
    return tbs + sign_tbs(key, tbs)


def build_bundle(key, files, version=(1, 4, 2), abi=1, reserved=0, pub=TEST_PUBLISHER_ID,
                 app=TEST_APP_ID, native_tbs=None, key_sha=None, file_count=None,
                 payload_order=None, records_override=None, payload_override=None):
    if native_tbs is None:
        native_tbs = sha256(NATIVE_TBS_SEED)
    if key_sha is None:
        key_sha = sha256(spki_der(key))
    if file_count is None:
        file_count = len(files)
    records = build_records(files) if records_override is None else records_override
    ordered = files if payload_order is None else payload_order
    payload = b"".join(d for _, d in ordered) if payload_override is None else payload_override
    return build_from_parts(key, pub, app, key_sha, native_tbs, version, file_count, abi,
                            reserved, records, payload)


def tbs_of(bundle, manifest_len, payload_len):
    return bundle[:16 + manifest_len + payload_len]


def make_files(spec):
    return [(p.encode(), deterministic_content(p, n)) for p, n in spec]


def patch_header(bundle, manifest_len=None, payload_len=None):
    b = bytearray(bundle)
    if manifest_len is not None:
        b[8:12] = le32(manifest_len)
    if payload_len is not None:
        b[12:16] = le32(payload_len)
    return bytes(b)


def vec_hex(data, per_line=12):
    chunks = [data[i:i + per_line] for i in range(0, len(data), per_line)]
    lines = ["    " + ", ".join("0x%02Xu" % b for b in c) + "," for c in chunks]
    return "\\\n".join(lines).rstrip(",")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    out = args.output
    os.makedirs(out, exist_ok=True)

    key = derive_key(ANCHOR_SEED)
    foreign = derive_key(FOREIGN_SEED)
    spki = spki_der(key)
    foreign_spki = spki_der(foreign)
    native_tbs = sha256(NATIVE_TBS_SEED)
    key_sha = sha256(spki)
    foreign_sha = sha256(foreign_spki)

    small = make_files(SMALL_FILES)
    counting = make_files(COUNTING_FILES)
    small_ver = (1, 4, 2)
    counting_ver = (2, 0, 1)

    small_manifest_len = 128 + sum(38 + len(p) for p, _ in small)
    small_payload_len = sum(len(d) for _, d in small)
    counting_manifest_len = 128 + sum(38 + len(p) for p, _ in counting)
    counting_payload_len = sum(len(d) for _, d in counting)
    assert counting_payload_len == 417036
    assert counting_manifest_len == 555

    small_bundle = build_bundle(key, small, version=small_ver)
    small_tbs = tbs_of(small_bundle, small_manifest_len, small_payload_len)
    counting_bundle = build_bundle(key, counting, version=counting_ver)
    counting_tbs = tbs_of(counting_bundle, counting_manifest_len, counting_payload_len)

    cases = []

    def emit(name, data, expected):
        with open(os.path.join(out, name), "wb") as f:
            f.write(data)
        cases.append((name, expected))

    emit("positive_small.bin", small_bundle, OK)
    emit("positive_counting.bin", counting_bundle, OK)
    small_signature_len = len(small_bundle) - len(small_tbs)
    emit("positive_resign_same_tbs.bin",
         small_tbs + sign_tbs_with_length(key, small_tbs, small_signature_len,
                                          small_bundle[len(small_tbs):]), OK)

    r_signature = sign_tbs_with_pad(key, small_tbs, "r")
    r_positive = small_tbs + r_signature
    r_negative = small_tbs + remove_integer_sign_pad(r_signature, "r")
    assert r_positive[:len(small_tbs)] == r_negative[:len(small_tbs)]
    emit("positive_strict_der_r_msb.bin", r_positive, OK)
    emit("negative_strict_der_r_msb.bin", r_negative, BAD_FORMAT)

    s_signature = sign_tbs_with_pad(key, small_tbs, "s")
    s_positive = small_tbs + s_signature
    s_negative = small_tbs + remove_integer_sign_pad(s_signature, "s")
    assert s_positive[:len(small_tbs)] == s_negative[:len(small_tbs)]
    emit("positive_strict_der_s_msb.bin", s_positive, OK)
    emit("negative_strict_der_s_msb.bin", s_negative, BAD_FORMAT)

    bad_magic = bytearray(small_bundle)
    bad_magic[0] = 0x58
    emit("neg_bad_magic.bin", bytes(bad_magic), BAD_FORMAT)

    bad_major = bytearray(small_bundle)
    bad_major[4] = 0x02
    emit("neg_bad_major_magic.bin", bytes(bad_major), BAD_FORMAT)

    emit("neg_reserved_nonzero.bin", build_bundle(key, small, reserved=2), BAD_FORMAT)
    emit("neg_version_major_zero.bin", build_bundle(key, small, version=(0, 4, 2)), BAD_FORMAT)
    emit("neg_abi_two.bin", build_bundle(key, small, abi=2), ABI)
    emit("neg_abi_zero.bin", build_bundle(key, small, abi=0), ABI)

    emit("neg_truncated_last.bin", small_bundle[:-1], BAD_FORMAT)
    emit("neg_truncated_header.bin", small_bundle[:40], BAD_FORMAT)
    emit("neg_trailing_byte.bin", small_bundle + b"\x00", BAD_FORMAT)

    sig_flip = bytearray(small_bundle)
    sig_flip[-1] ^= 0x01
    emit("neg_sig_lowbit_flip.bin", bytes(sig_flip), SIG)

    sig_tag = bytearray(small_bundle)
    sig_tag[len(small_tbs)] = 0x31
    emit("neg_sig_tag_corrupt.bin", bytes(sig_tag), BAD_FORMAT)

    emit("neg_sig_foreign_key.bin", small_tbs + sign_tbs(foreign, small_tbs), SIG)

    manifest_mut = bytearray(small_bundle)
    manifest_mut[16 + 114] ^= 0x01
    emit("neg_manifest_mutated.bin", bytes(manifest_mut), SIG)

    payload_flip = bytearray(small_bundle)
    payload_flip[16 + small_manifest_len] ^= 0xFF
    emit("neg_payload_flip.bin", bytes(payload_flip), ASSET)

    emit("neg_payload_reorder.bin",
         build_bundle(key, small, payload_order=[small[1], small[0], small[2]]), ASSET)

    emit("neg_file_len_zero.bin",
         build_bundle(key, [(small[0][0], b""), small[1], small[2]]), LIMIT)

    rec_over = bytearray()
    for i, (p, d) in enumerate(small):
        flen = 1048577 if i == 0 else len(d)
        rec_over.extend(le16(len(p)))
        rec_over.extend(le32(flen))
        rec_over.extend(sha256(d))
        rec_over.extend(p)
    emit("neg_file_len_over.bin",
         build_bundle(key, small, records_override=bytes(rec_over)), LIMIT)

    emit("neg_file_count_zero.bin", build_bundle(key, small, file_count=0), LIMIT)
    emit("neg_file_count_over.bin", build_bundle(key, small, file_count=65), LIMIT)

    emit("neg_manifest_len_over.bin", patch_header(small_bundle, manifest_len=16385), LIMIT)
    emit("neg_manifest_len_under.bin", patch_header(small_bundle, manifest_len=100), LIMIT)
    emit("neg_manifest_len_zero.bin", patch_header(small_bundle, manifest_len=0), LIMIT)
    emit("neg_payload_len_zero.bin", patch_header(small_bundle, payload_len=0), LIMIT)
    emit("neg_payload_len_over.bin", patch_header(small_bundle, payload_len=2097153), LIMIT)
    emit("neg_payload_sum_mismatch.bin",
         patch_header(small_bundle, payload_len=small_payload_len + 1), BAD_FORMAT)

    with_neg = [(b"../escape.html", small[0][1]), small[1], small[2]]
    assert [p for p, _ in with_neg] == sorted(p for p, _ in with_neg)
    emit("neg_path_traversal.bin", build_bundle(key, with_neg), BAD_FORMAT)

    abs_neg = [(b"/etc/passwd", small[0][1]), small[1], small[2]]
    emit("neg_path_absolute.bin", build_bundle(key, abs_neg), BAD_FORMAT)

    bslash_neg = [small[0], small[1], (b"libs\\evil.js", small[2][1])]
    emit("neg_path_backslash.bin", build_bundle(key, bslash_neg), BAD_FORMAT)

    dotdot_neg = [(b"assets/../index.html", small[0][1]), small[1], small[2]]
    emit("neg_path_dotdot_segment.bin", build_bundle(key, dotdot_neg), BAD_FORMAT)

    empty_neg = [(b"assets//x.js", small[0][1]), small[1], small[2]]
    emit("neg_path_empty_segment.bin", build_bundle(key, empty_neg), BAD_FORMAT)

    pct_neg = [(b"a%20b.js", small[0][1]), small[1], small[2]]
    emit("neg_path_percent.bin", build_bundle(key, pct_neg), BAD_FORMAT)

    colon_neg = [(b"https://evil", small[0][1]), small[1], small[2]]
    emit("neg_path_colon.bin", build_bundle(key, colon_neg), BAD_FORMAT)

    query_neg = [small[0], (b"index.html?x=1", small[1][1]), small[2]]
    assert [p for p, _ in query_neg] == sorted(p for p, _ in query_neg)
    emit("neg_path_query.bin", build_bundle(key, query_neg), BAD_FORMAT)

    nul_neg = [(b"index\x00.html", small[0][1]), small[1], small[2]]
    assert [p for p, _ in nul_neg] == sorted(p for p, _ in nul_neg)
    emit("neg_path_nul.bin", build_bundle(key, nul_neg), BAD_FORMAT)

    nonascii_neg = [(b"caf\xc3\xa9.js", small[0][1]), small[1], small[2]]
    emit("neg_path_non_ascii.bin", build_bundle(key, nonascii_neg), BAD_FORMAT)

    hidden_neg = [(b".hidden.js", small[0][1]), small[1], small[2]]
    emit("neg_path_leading_dot.bin", build_bundle(key, hidden_neg), BAD_FORMAT)

    long_neg = [(b"a" * 161, small[0][1]), small[1], small[2]]
    emit("neg_path_too_long.bin", build_bundle(key, long_neg), LIMIT)

    rec_pl0 = bytearray()
    for i, (p, d) in enumerate(small):
        plen = 0 if i == 0 else len(p)
        rec_pl0.extend(le16(plen))
        rec_pl0.extend(le32(len(d)))
        rec_pl0.extend(sha256(d))
        rec_pl0.extend(p)
    emit("neg_path_len_zero.bin", build_bundle(key, small, records_override=bytes(rec_pl0)), LIMIT)

    emit("neg_path_duplicate.bin",
         build_bundle(key, [small[0], small[1], (b"index.html", small[2][1])]), BAD_FORMAT)

    case_neg = [(b"INDEX.html", small[1][1]), small[0], (b"index.html", small[2][1])]
    assert [p for p, _ in case_neg] == sorted(p for p, _ in case_neg)
    emit("neg_path_case_collision.bin", build_bundle(key, case_neg), BAD_FORMAT)

    emit("neg_records_out_of_order.bin",
         build_bundle(key, [small[1], small[0], small[2]]), BAD_FORMAT)

    emit("neg_index_missing.bin",
         build_bundle(key, [(b"main.html", small[1][1]), small[0], small[2]]), BAD_FORMAT)

    emit("neg_identity_publisher.bin",
         build_bundle(key, small, pub=b"other-publisher"), IDENTITY)
    emit("neg_identity_app.bin", build_bundle(key, small, app=b"other-app"), IDENTITY)
    emit("neg_identity_key_claim.bin",
         build_bundle(foreign, small, key_sha=foreign_sha), IDENTITY)
    emit("neg_native_tbs_wrong.bin",
         build_bundle(key, small, native_tbs=sha256(b"other-native")), IDENTITY)
    emit("neg_native_pkg_sha_confusion.bin",
         build_bundle(key, small, native_tbs=sha256(b"whole-native-package-bytes")), IDENTITY)

    vectors_path = os.path.join(out, "sidecar_vectors.h")

    counting_records = build_records(counting)
    line = []
    line.append("#ifndef SIDECAR_VECTORS_H")
    line.append("#define SIDECAR_VECTORS_H")
    line.append("")
    line.append("#include <stdint.h>")
    line.append("")
    line.append('#define SC_VEC_PUBLISHER_ID "cnt-publisher"')
    line.append('#define SC_VEC_APP_ID "line-counting-ui"')
    line.append("#define SC_VEC_PUBLISHER_ID_LEN 13u")
    line.append("#define SC_VEC_APP_ID_LEN 16u")
    line.append("#define SC_VEC_SMALL_MANIFEST_LEN %uu" % small_manifest_len)
    line.append("#define SC_VEC_SMALL_PAYLOAD_LEN %uu" % small_payload_len)
    line.append("#define SC_VEC_COUNTING_MANIFEST_LEN %uu" % counting_manifest_len)
    line.append("#define SC_VEC_COUNTING_PAYLOAD_LEN %uu" % counting_payload_len)
    line.append("#define SC_VEC_COUNTING_FILE_COUNT 7u")
    line.append("static const uint8_t SC_VEC_NATIVE_TBS[] = {")
    line.append(vec_hex(native_tbs))
    line.append("};")
    line.append("static const uint8_t SC_VEC_SPKI_DER[] = {")
    line.append(vec_hex(spki))
    line.append("};")
    line.append("#define SC_VEC_SPKI_LEN %uu" % len(spki))
    line.append("static const uint8_t SC_VEC_SPKI_BAD_POINT[] = {")
    line.append(vec_hex(spki[:26] + bytes(65)))
    line.append("};")
    line.append("static const uint8_t SC_VEC_UI_TBS_SMALL[] = {")
    line.append(vec_hex(sha256(small_tbs)))
    line.append("};")
    line.append("static const uint8_t SC_VEC_UI_TBS_COUNTING[] = {")
    line.append(vec_hex(sha256(counting_tbs)))
    line.append("};")
    line.append("#define SC_VEC_COUNTING_FILES \\")
    rows = []
    for p, d in counting:
        rows.append('    { "%s", %uu, "%s" }' % (p.decode(), len(d), hex32(sha256(d))))
    line.append(",\\\n".join(rows))
    line.append("")
    line.append("typedef struct {")
    line.append("    const char *file;")
    line.append("    int expected;")
    line.append("} sc_vec_neg_t;")
    line.append("")
    line.append("static const sc_vec_neg_t SC_VEC_NEG_CASES[] = {")
    for name, expected in cases:
        line.append('    { "%s", %s },' % (name, expected))
    line.append("};")
    line.append("")
    line.append("#endif")
    with open(vectors_path, "w") as f:
        f.write("\n".join(line) + "\n")

    print("fixtures=%d counting_bytes=%d counting_manifest=%d" %
          (len(cases) + 3, counting_payload_len, counting_manifest_len))


if __name__ == "__main__":
    main()
