import base64
import hashlib
import hmac
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
