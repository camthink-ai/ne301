import argparse
import base64
import hashlib
import hmac
import json
import os
import re
import zlib

PUB = "ne301-exp-pub"
APP_ID = "anchor-fixture"
BOARD_ID = 0x00003010
PSRAM_MIB = 64
ABI_VERSION = 0x00010000
CAPS = 0x00000003
EXEC_BYTES = 4
TARGET_ADDR = 0x93E00000
NATIVE_PAYLOAD = bytes([0x00, 0x20, 0x70, 0x47])
IMAGE_FORMAT_VERSION = 1
IMAGE_HEADER_SIZE = 32
MANIFEST_SIZE = 160
CONTAINER_MAGIC = bytes([0x4E, 0x45, 0x41, 0x50, 0x50, 0x01, 0x00, 0x00])
MANIFEST_MAGIC = b"NMF1"
SPKI_PREFIX = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d03010703420004")

P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
GX = 0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296
GY = 0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551

PRESERVED_ARRAYS = (
    "ANCHOR_SUPERSEDED_SPKI_DER",
    "ANCHOR_SUPERSEDED_SPKI_SHA256",
    "ANCHOR_SUPERSEDED_PACKAGE",
    "ANCHOR_FOREIGN_PACKAGE",
)


def inv_mod(x, m):
    return pow(x, -1, m)


def ec_add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return p1
    x1, y1 = p1
    x2, y2 = p2
    if x1 == x2 and (y1 + y2) % P == 0:
        return None
    if p1 == p2:
        lam = (3 * x1 * x1 + A) * inv_mod(2 * y1, P) % P
    else:
        lam = (y2 - y1) * inv_mod(x2 - x1, P) % P
    x3 = (lam * lam - x1 - x2) % P
    y3 = (lam * (x1 - x3) - y1) % P
    return (x3, y3)


def ec_mul(k, point):
    result = None
    addend = point
    while k:
        if k & 1:
            result = ec_add(result, addend)
        addend = ec_add(addend, addend)
        k >>= 1
    return result


def spki_der(q):
    return SPKI_PREFIX + q[0].to_bytes(32, "big") + q[1].to_bytes(32, "big")


def spki_sha256(q):
    return hashlib.sha256(spki_der(q)).digest()


def spki_point(spki):
    if len(spki) != 91 or spki[:27] != SPKI_PREFIX or spki[26] != 0x04:
        raise SystemExit("unexpected SPKI encoding")
    return (int.from_bytes(spki[27:59], "big"), int.from_bytes(spki[59:91], "big"))


def bits2int(data):
    return int.from_bytes(data, "big")


def bits2octets(data):
    return (bits2int(data) % N).to_bytes(32, "big")


def rfc6979_k(d, h1):
    x = d.to_bytes(32, "big")
    h_o = bits2octets(h1)
    v = b"\x01" * 32
    k = b"\x00" * 32
    k = hmac.new(k, v + b"\x00" + x + h_o, hashlib.sha256).digest()
    v = hmac.new(k, v, hashlib.sha256).digest()
    k = hmac.new(k, v + b"\x01" + x + h_o, hashlib.sha256).digest()
    v = hmac.new(k, v, hashlib.sha256).digest()
    while True:
        v = hmac.new(k, v, hashlib.sha256).digest()
        candidate = bits2int(v)
        if 1 <= candidate <= N - 1:
            return candidate
        k = hmac.new(k, v + b"\x00", hashlib.sha256).digest()
        v = hmac.new(k, v, hashlib.sha256).digest()


def der_int(value):
    raw = value.to_bytes((value.bit_length() + 7) // 8 or 1, "big")
    if raw[0] & 0x80:
        raw = b"\x00" + raw
    return raw


def ecdsa_sign(d, tbs):
    h1 = hashlib.sha256(tbs).digest()
    e = bits2int(h1)
    while True:
        k = rfc6979_k(d, h1)
        point = ec_mul(k, (GX, GY))
        r = point[0] % N
        if r != 0:
            s = inv_mod(k, N) * (e + r * d) % N
            if s != 0:
                if s > N // 2:
                    s = N - s
                return r, s
        h1 = hashlib.sha256(h1).digest()
        e = bits2int(h1)


def der_sig_bytes(r, s):
    r_bytes = der_int(r)
    s_bytes = der_int(s)
    body = b"\x02" + bytes([len(r_bytes)]) + r_bytes
    body += b"\x02" + bytes([len(s_bytes)]) + s_bytes
    return b"\x30" + bytes([len(body)]) + body


def ecdsa_verify(q, tbs, sig_der):
    if sig_der[0] != 0x30:
        return False
    seq_len = sig_der[1]
    body = sig_der[2:2 + seq_len]
    if body[0] != 0x02:
        return False
    r_len = body[1]
    r = int.from_bytes(body[2:2 + r_len], "big")
    pos = 2 + r_len
    if body[pos] != 0x02:
        return False
    s_len = body[pos + 1]
    s = int.from_bytes(body[pos + 2:pos + 2 + s_len], "big")
    if not (1 <= r < N and 1 <= s < N):
        return False
    e = bits2int(hashlib.sha256(tbs).digest())
    w = inv_mod(s, N)
    p1 = ec_mul(e * w % N, (GX, GY))
    p2 = ec_mul(r * w % N, q)
    point = ec_add(p1, p2)
    if point is None:
        return False
    return point[0] % N == r


def der_element(data, pos):
    tag = data[pos]
    first = data[pos + 1]
    if first & 0x80:
        n = first & 0x7F
        length = int.from_bytes(data[pos + 2:pos + 2 + n], "big")
        start = pos + 2 + n
    else:
        length = first
        start = pos + 2
    return tag, data[start:start + length], start + length


def der_sequence_body(der):
    tag, body, _ = der_element(der, 0)
    if tag != 0x30:
        raise SystemExit("unsupported key DER")
    return body


def scalar_from_sec1(der):
    body = der_sequence_body(der)
    tag, version, pos = der_element(body, 0)
    if tag != 0x02 or version != b"\x01":
        raise SystemExit("unsupported SEC1 layout")
    tag, d_raw, _ = der_element(body, pos)
    if tag != 0x04:
        raise SystemExit("unsupported SEC1 layout")
    d = int.from_bytes(d_raw, "big")
    if not 1 <= d < N:
        raise SystemExit("key scalar out of range")
    return d


def load_signing_key(path):
    with open(path, "r", encoding="ascii") as handle:
        text = handle.read()
    if "PRIVATE KEY" not in text:
        raise SystemExit("not a private key PEM")
    body = "".join(line.strip() for line in text.splitlines() if "-----" not in line)
    der = base64.b64decode(body)
    seq = der_sequence_body(der)
    tag, version, pos = der_element(seq, 0)
    if tag == 0x02 and version == b"\x01":
        return scalar_from_sec1(der)
    if tag == 0x02 and version == b"\x00":
        tag, value, pos = der_element(seq, pos)
        if tag != 0x30:
            raise SystemExit("unsupported PKCS8 layout")
        tag, value, pos = der_element(seq, pos)
        if tag != 0x04:
            raise SystemExit("unsupported PKCS8 layout")
        return scalar_from_sec1(value)
    raise SystemExit("unsupported key layout")


def native_image(payload):
    header = (
        (0x3141454E).to_bytes(4, "little")
        + IMAGE_HEADER_SIZE.to_bytes(2, "little")
        + IMAGE_FORMAT_VERSION.to_bytes(2, "little")
        + ABI_VERSION.to_bytes(4, "little")
        + TARGET_ADDR.to_bytes(4, "little")
        + len(payload).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
        + (zlib.crc32(payload) & 0xFFFFFFFF).to_bytes(4, "little")
    )
    return header + payload


def build_manifest(signer_spki):
    manifest = bytearray(MANIFEST_SIZE)
    manifest[0:4] = MANIFEST_MAGIC
    pub = PUB.encode("ascii")
    app = APP_ID.encode("ascii")
    manifest[4:4 + len(pub)] = pub
    manifest[20:20 + len(app)] = app
    manifest[52:58] = (1).to_bytes(2, "little") + (0).to_bytes(4, "little")
    manifest[60:64] = BOARD_ID.to_bytes(4, "little")
    manifest[64:68] = PSRAM_MIB.to_bytes(4, "little")
    manifest[68:72] = ABI_VERSION.to_bytes(4, "little")
    manifest[72:76] = CAPS.to_bytes(4, "little")
    manifest[76:80] = EXEC_BYTES.to_bytes(4, "little")
    image = native_image(NATIVE_PAYLOAD)
    manifest[80:84] = len(image).to_bytes(4, "little")
    manifest[84:88] = len(NATIVE_PAYLOAD).to_bytes(4, "little")
    manifest[88:92] = (0).to_bytes(4, "little")
    manifest[92:96] = TARGET_ADDR.to_bytes(4, "little")
    manifest[96:128] = hashlib.sha256(image).digest()
    manifest[128:160] = hashlib.sha256(signer_spki).digest()
    return bytes(manifest), image


def build_signed_package(d, q, fingerprint_spki=None):
    manifest, image = build_manifest(spki_der(q) if fingerprint_spki is None else fingerprint_spki)
    tbs = CONTAINER_MAGIC + len(manifest).to_bytes(4, "little")
    tbs += len(image).to_bytes(4, "little") + manifest + image
    return tbs + der_sig_bytes(*ecdsa_sign(d, tbs)), tbs


def split_sig(package):
    off = 16 + MANIFEST_SIZE + int.from_bytes(package[12:16], "little")
    seq_len = package[off + 1]
    body = package[off + 2:off + 2 + seq_len]
    r_len = body[1]
    pos = 2 + r_len
    s_len = body[pos + 1]
    return package[:off], body[2:2 + r_len], body[pos + 2:pos + 2 + s_len], off


def parse_c_byte_array(text, name):
    match = re.search(name + r"\[\]\s*=\s*\{(.*?)\}", text, re.S)
    if match is None:
        raise SystemExit("anchor constant not found: " + name)
    return bytes(int(tok, 16) for tok in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)))


def parse_c_define(text, name):
    match = re.search(r"#define\s+" + name + r"\s+(\d+)u", text)
    if match is None:
        raise SystemExit("anchor define not found: " + name)
    return int(match.group(1))


def verify_binding(anchor_c_path, vectors_h_path, repo_root):
    with open(anchor_c_path, "r", encoding="utf-8") as handle:
        anchor_text = handle.read()
    with open(vectors_h_path, "r", encoding="utf-8") as handle:
        vector_text = handle.read()
    compiled_spki = parse_c_byte_array(anchor_text, "g_anchor_spki_der")
    compiled_digest = parse_c_byte_array(anchor_text, "g_anchor_spki_sha256")
    vector_spki = parse_c_byte_array(vector_text, "ANCHOR_VECTOR_SPKI_DER")
    vector_digest = parse_c_byte_array(vector_text, "ANCHOR_VECTOR_SPKI_SHA256")
    positive = parse_c_byte_array(vector_text, "ANCHOR_POSITIVE_PACKAGE")
    superseded = parse_c_byte_array(vector_text, "ANCHOR_SUPERSEDED_PACKAGE")
    foreign = parse_c_byte_array(vector_text, "ANCHOR_FOREIGN_PACKAGE")
    recomputed = hashlib.sha256(compiled_spki).digest()
    q = spki_point(compiled_spki)
    tbs, _, _, sig_off = split_sig(positive)
    sup_tbs, _, _, sup_sig_off = split_sig(superseded)
    frn_tbs, _, _, frn_sig_off = split_sig(foreign)
    manifest_fingerprint = positive[16 + 128:16 + 160]
    foreign_fingerprint = foreign[16 + 128:16 + 160]
    report = {
        "anchor_c": os.path.relpath(anchor_c_path, repo_root),
        "vectors_h": os.path.relpath(vectors_h_path, repo_root),
        "compiled_spki_sha256": compiled_digest.hex(),
        "recomputed_compiled_spki_sha256": recomputed.hex(),
        "vector_signer_spki_sha256": vector_digest.hex(),
        "positive_manifest_fingerprint": manifest_fingerprint.hex(),
        "positive_package_sha256": hashlib.sha256(positive).hexdigest(),
        "superseded_package_sha256": hashlib.sha256(superseded).hexdigest(),
        "foreign_package_sha256": hashlib.sha256(foreign).hexdigest(),
        "compiled_spki_equals_vector_spki": compiled_spki == vector_spki,
        "pinned_digest_equals_recomputed": compiled_digest == recomputed,
        "vector_digest_equals_compiled": vector_digest == compiled_digest,
        "manifest_fingerprint_equals_compiled": manifest_fingerprint == compiled_digest,
        "foreign_fingerprint_claims_compiled": foreign_fingerprint == compiled_digest,
        "positive_len_define_matches": parse_c_define(vector_text, "ANCHOR_POSITIVE_PACKAGE_LEN") == len(positive),
        "positive_signature_valid": ecdsa_verify(q, tbs, positive[sig_off:]),
        "superseded_signature_rejected": not ecdsa_verify(q, sup_tbs, superseded[sup_sig_off:]),
        "foreign_signature_rejected": not ecdsa_verify(q, frn_tbs, foreign[frn_sig_off:]),
        "vector_signer_differs_from_superseded": vector_spki != parse_c_byte_array(vector_text, "ANCHOR_SUPERSEDED_SPKI_DER"),
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    required_true = (
        report["compiled_spki_equals_vector_spki"],
        report["pinned_digest_equals_recomputed"],
        report["vector_digest_equals_compiled"],
        report["manifest_fingerprint_equals_compiled"],
        report["positive_len_define_matches"],
        report["positive_signature_valid"],
        report["superseded_signature_rejected"],
        report["foreign_signature_rejected"],
        report["foreign_fingerprint_claims_compiled"],
        report["vector_signer_differs_from_superseded"],
    )
    if not all(required_true):
        raise SystemExit("ANCHOR BINDING GUARD FAILED: compiled anchor and vectors diverge")
    return True


def c_array(name, data, per_line=12):
    lines = []
    for start in range(0, len(data), per_line):
        chunk = data[start:start + per_line]
        lines.append("    " + ", ".join("0x%02x" % b for b in chunk) + ",")
    return "static const uint8_t %s[] = {\n%s\n};\n" % (name, "\n".join(lines))


def replace_c_array(text, name, data):
    pattern = re.compile(r"static const uint8_t " + re.escape(name) + r"\[\] = \{.*?\};", re.S)
    if pattern.search(text) is None:
        raise SystemExit("anchor constant not found: " + name)
    return pattern.sub(lambda _match: c_array(name, data).rstrip("\n"), text, count=1)


def update_header(path, positive, q, foreign_pkg=None):
    with open(path, "r", encoding="utf-8") as handle:
        old = handle.read()
    preserved = {}
    for name in PRESERVED_ARRAYS:
        preserved[name] = parse_c_byte_array(old, name)
    if foreign_pkg is None:
        foreign_pkg = preserved["ANCHOR_FOREIGN_PACKAGE"]
    if preserved["ANCHOR_SUPERSEDED_SPKI_DER"] == spki_der(q):
        raise SystemExit("preserved superseded fixture must differ from anchor key")
    if foreign_pkg[16 + 128:16 + 160] != spki_sha256(q):
        raise SystemExit("foreign fixture must claim the current anchor fingerprint, rerun with --foreign-key")
    image_len = IMAGE_HEADER_SIZE + len(NATIVE_PAYLOAD)
    tamper_offset = 16 + MANIFEST_SIZE + image_len - 1
    sig_offset = 16 + MANIFEST_SIZE + image_len
    parts = []
    parts.append("#ifndef ANCHOR_VECTORS_H\n")
    parts.append("#define ANCHOR_VECTORS_H\n\n")
    parts.append("#include <stddef.h>\n")
    parts.append("#include <stdint.h>\n\n")
    parts.append("#define ANCHOR_VECTOR_SPKI_DER_LEN 91u\n")
    parts.append("#define ANCHOR_VECTOR_SPKI_SHA256_LEN 32u\n")
    parts.append("#define ANCHOR_POSITIVE_PACKAGE_LEN %du\n" % len(positive))
    parts.append("#define ANCHOR_POSITIVE_SIG_OFFSET %du\n" % sig_offset)
    parts.append("#define ANCHOR_POSITIVE_TAMPER_OFFSET %du\n" % tamper_offset)
    parts.append("#define ANCHOR_SUPERSEDED_PACKAGE_LEN %du\n" % len(preserved["ANCHOR_SUPERSEDED_PACKAGE"]))
    parts.append("#define ANCHOR_FOREIGN_PACKAGE_LEN %du\n" % len(foreign_pkg))
    parts.append("\n")
    parts.append(c_array("ANCHOR_VECTOR_SPKI_DER", spki_der(q)))
    parts.append("\n")
    parts.append(c_array("ANCHOR_VECTOR_SPKI_SHA256", spki_sha256(q)))
    parts.append("\n")
    parts.append(c_array("ANCHOR_POSITIVE_PACKAGE", positive))
    parts.append("\n")
    parts.append(c_array("ANCHOR_SUPERSEDED_SPKI_DER", preserved["ANCHOR_SUPERSEDED_SPKI_DER"]))
    parts.append("\n")
    parts.append(c_array("ANCHOR_SUPERSEDED_SPKI_SHA256", preserved["ANCHOR_SUPERSEDED_SPKI_SHA256"]))
    parts.append("\n")
    parts.append(c_array("ANCHOR_SUPERSEDED_PACKAGE", preserved["ANCHOR_SUPERSEDED_PACKAGE"]))
    parts.append("\n")
    parts.append(c_array("ANCHOR_FOREIGN_PACKAGE", foreign_pkg))
    parts.append("\n#endif\n")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("".join(parts))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emit-header", default=None)
    parser.add_argument("--key", default=None)
    parser.add_argument("--foreign-key", default=None)
    parser.add_argument("--anchor-c", default=None)
    args = parser.parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    anchor_c = args.anchor_c or os.path.join(
        repo_root, "Custom", "Services", "AppHost", "app_trust_anchor.c")
    vectors_h = args.emit_header or os.path.join(
        repo_root, "tests", "app_admission", "anchor_vectors.h")

    rotate = args.emit_header is not None or args.key is not None
    if rotate and (args.emit_header is None or args.key is None):
        raise SystemExit("rotation requires both --emit-header and --key")

    if rotate:
        d = load_signing_key(args.key)
        q = ec_mul(d, (GX, GY))
        positive, _ = build_signed_package(d, q)
        tbs, _, _, sig_off = split_sig(positive)
        if not ecdsa_verify(q, tbs, positive[sig_off:]):
            raise SystemExit("self verify FAILED for new positive package")
        foreign_pkg = None
        if args.foreign_key is not None:
            fd = load_signing_key(args.foreign_key)
            fq = ec_mul(fd, (GX, GY))
            foreign_pkg, _ = build_signed_package(fd, fq, fingerprint_spki=spki_der(q))
            ftbs, _, _, fsig_off = split_sig(foreign_pkg)
            if ecdsa_verify(q, ftbs, foreign_pkg[fsig_off:]):
                raise SystemExit("foreign fixture must NOT verify against anchor key")
        update_header(vectors_h, positive, q, foreign_pkg)
        with open(anchor_c, "r", encoding="utf-8") as handle:
            anchor_text = handle.read()
        anchor_text = replace_c_array(anchor_text, "g_anchor_spki_der", spki_der(q))
        anchor_text = replace_c_array(anchor_text, "g_anchor_spki_sha256", spki_sha256(q))
        with open(anchor_c, "w", encoding="utf-8") as handle:
            handle.write(anchor_text)
        print("rotated anchor to controlled issuer spki_sha256=" + spki_sha256(q).hex())
        print("positive_package_sha256=" + hashlib.sha256(positive).hexdigest())

    verify_binding(anchor_c, vectors_h, repo_root)


if __name__ == "__main__":
    main()
