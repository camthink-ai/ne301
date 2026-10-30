import argparse
import hashlib
import os
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app_admission"))

from anchor_keygen import (ecdsa_sign, der_sig_bytes, ec_mul, spki_der,
                           spki_sha256, load_signing_key, ecdsa_verify, split_sig,
                           GX, GY, PUB, BOARD_ID, PSRAM_MIB, ABI_VERSION, CAPS,
                           TARGET_ADDR, IMAGE_HEADER_SIZE, IMAGE_FORMAT_VERSION,
                           MANIFEST_SIZE, CONTAINER_MAGIC, MANIFEST_MAGIC, N)

PUBLISHER_ID = PUB
APP_ID = "hello-app"
EXEC_BYTES = 64
REQUIRED_CAPS = CAPS

V1_PAYLOAD = bytes([0x00, 0x20, 0x70, 0x47, 0x01])
V2_PAYLOAD = bytes([0x00, 0x20, 0x70, 0x47, 0x02, 0x02])
V2_ALT_PAYLOAD = bytes([0x00, 0x20, 0x70, 0x47, 0x03, 0x03, 0x03])
BADBOARD_PAYLOAD = bytes([0x00, 0x20, 0x70, 0x47, 0x04])
FOREIGN_PAYLOAD = bytes([0x00, 0x20, 0x70, 0x47, 0x09])


def native_image(payload, abi=ABI_VERSION):
    header = (
        (0x3141454E).to_bytes(4, "little")
        + IMAGE_HEADER_SIZE.to_bytes(2, "little")
        + IMAGE_FORMAT_VERSION.to_bytes(2, "little")
        + abi.to_bytes(4, "little")
        + TARGET_ADDR.to_bytes(4, "little")
        + len(payload).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
        + (zlib.crc32(payload) & 0xFFFFFFFF).to_bytes(4, "little")
    )
    return header + payload


def build_manifest(signer_spki, app_id, version, payload, board_id, abi=ABI_VERSION):
    major, minor, patch = version
    manifest = bytearray(MANIFEST_SIZE)
    manifest[0:4] = MANIFEST_MAGIC
    pub = PUBLISHER_ID.encode("ascii")
    app = app_id.encode("ascii")
    manifest[4:4 + len(pub)] = pub
    manifest[20:20 + len(app)] = app
    manifest[52:54] = major.to_bytes(2, "little")
    manifest[54:56] = minor.to_bytes(2, "little")
    manifest[56:58] = patch.to_bytes(2, "little")
    manifest[60:64] = board_id.to_bytes(4, "little")
    manifest[64:68] = PSRAM_MIB.to_bytes(4, "little")
    manifest[68:72] = abi.to_bytes(4, "little")
    manifest[72:76] = REQUIRED_CAPS.to_bytes(4, "little")
    manifest[76:80] = EXEC_BYTES.to_bytes(4, "little")
    image = native_image(payload, abi)
    manifest[80:84] = len(image).to_bytes(4, "little")
    manifest[84:88] = len(payload).to_bytes(4, "little")
    manifest[88:92] = (0).to_bytes(4, "little")
    manifest[92:96] = TARGET_ADDR.to_bytes(4, "little")
    manifest[96:128] = hashlib.sha256(image).digest()
    manifest[128:160] = hashlib.sha256(signer_spki).digest()
    return bytes(manifest), image


def build_signed_package(d, q, app_id, version, payload, board_id=BOARD_ID,
                         fingerprint_spki=None, abi=ABI_VERSION):
    signer_spki = spki_der(q) if fingerprint_spki is None else fingerprint_spki
    manifest, image = build_manifest(signer_spki, app_id, version, payload, board_id,
                                     abi=abi)
    tbs = CONTAINER_MAGIC + len(manifest).to_bytes(4, "little")
    tbs += len(image).to_bytes(4, "little") + manifest + image
    package = tbs + der_sig_bytes(*ecdsa_sign(d, tbs))
    tbs_sha = hashlib.sha256(tbs).digest()
    sig_off = len(tbs)
    if not ecdsa_verify(q, tbs, package[sig_off:]):
        raise SystemExit("self verify FAILED for %s %s" % (app_id, version))
    return package, tbs_sha


def high_s_variant(package, q):
    tbs, r_bytes, s_bytes, sig_off = split_sig(package)
    r = int.from_bytes(r_bytes, "big")
    s = int.from_bytes(s_bytes, "big")
    alt = tbs + der_sig_bytes(r, N - s)
    if alt[:sig_off] != package[:sig_off]:
        raise SystemExit("high-S variant must keep identical TBS bytes")
    if alt == package:
        raise SystemExit("high-S variant must differ in DER signature bytes")
    if not ecdsa_verify(q, alt[:sig_off], alt[sig_off:]):
        raise SystemExit("high-S variant must stay a valid signature")
    return alt


def c_array(name, data, per_line=12):
    lines = []
    for start in range(0, len(data), per_line):
        chunk = data[start:start + per_line]
        lines.append("    " + ", ".join("0x%02x" % b for b in chunk) + ",")
    return "static const uint8_t %s[] = {\n%s\n};\n" % (name, "\n".join(lines))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emit-header", default=None)
    parser.add_argument("--key", required=True)
    parser.add_argument("--foreign-key", default=None)
    args = parser.parse_args()

    d = load_signing_key(args.key)
    q = ec_mul(d, (GX, GY))
    fingerprint = spki_sha256(q)

    expected = "06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda"
    if fingerprint.hex() != expected:
        raise SystemExit("signing key fingerprint %s does not match the current "
                         "firmware anchor %s" % (fingerprint.hex(), expected))

    v1, v1_tbs = build_signed_package(d, q, APP_ID, (1, 0, 0), V1_PAYLOAD)
    v2, v2_tbs = build_signed_package(d, q, APP_ID, (2, 0, 0), V2_PAYLOAD)
    v2_alt, v2_alt_tbs = build_signed_package(d, q, APP_ID, (2, 0, 0), V2_ALT_PAYLOAD)
    v2_hisig = high_s_variant(v2, q)
    v2_hisig_tbs = hashlib.sha256(split_sig(v2_hisig)[0]).digest()
    badboard, badboard_tbs = build_signed_package(d, q, APP_ID, (1, 1, 0),
                                                  BADBOARD_PAYLOAD, board_id=0x00003110)
    badabi, badabi_tbs = build_signed_package(d, q, APP_ID, (1, 2, 0),
                                              BADBOARD_PAYLOAD, abi=ABI_VERSION + 1)

    foreign_pkg = None
    if args.foreign_key is not None:
        fd = load_signing_key(args.foreign_key)
        fq = ec_mul(fd, (GX, GY))
        foreign_pkg, _ = build_signed_package(fd, fq, APP_ID, (9, 9, 9),
                                              FOREIGN_PAYLOAD,
                                              fingerprint_spki=spki_der(q))
        _, _, _, sig_off = split_sig(foreign_pkg)
        if ecdsa_verify(q, foreign_pkg[:sig_off], foreign_pkg[sig_off:]):
            raise SystemExit("foreign package must NOT verify against anchor key")

    if v1_tbs == v2_tbs or v2_tbs == v2_alt_tbs:
        raise SystemExit("TBS collision between versions")
    if v2_tbs == v2_alt_tbs:
        raise SystemExit("alt package must share version but differ in TBS")
    if v2_hisig_tbs != v2_tbs:
        raise SystemExit("high-S variant must share the base package TBS digest")
    if hashlib.sha256(v2_hisig).hexdigest() == hashlib.sha256(v2).hexdigest():
        raise SystemExit("high-S variant must differ from the base package bytes")
    if len(v2_hisig) == len(v2):
        raise SystemExit("high-S variant must differ in DER length for this fixture")

    header_path = args.emit_header or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "installer_vectors.h")

    parts = []
    parts.append("#ifndef INSTALLER_VECTORS_H\n")
    parts.append("#define INSTALLER_VECTORS_H\n\n")
    parts.append("#include <stddef.h>\n")
    parts.append("#include <stdint.h>\n\n")
    parts.append("#define INSTALLER_ANCHOR_SPKI_SHA256_LEN 32u\n")
    parts.append("#define HELLO_V1_PACKAGE_LEN %du\n" % len(v1))
    parts.append("#define HELLO_V2_PACKAGE_LEN %du\n" % len(v2))
    parts.append("#define HELLO_V2_ALT_PACKAGE_LEN %du\n" % len(v2_alt))
    parts.append("#define HELLO_V2_HISIG_PACKAGE_LEN %du\n" % len(v2_hisig))
    parts.append("#define HELLO_BADBOARD_PACKAGE_LEN %du\n" % len(badboard))
    parts.append("#define HELLO_BADABI_PACKAGE_LEN %du\n" % len(badabi))
    if foreign_pkg is not None:
        parts.append("#define HELLO_FOREIGN_PACKAGE_LEN %du\n" % len(foreign_pkg))
    parts.append("\n")
    parts.append(c_array("INSTALLER_VECTOR_SPKI_SHA256", fingerprint))
    parts.append("\n")
    parts.append(c_array("HELLO_V1_PACKAGE", v1))
    parts.append("\n")
    parts.append(c_array("HELLO_V2_PACKAGE", v2))
    parts.append("\n")
    parts.append(c_array("HELLO_V2_ALT_PACKAGE", v2_alt))
    parts.append("\n")
    parts.append(c_array("HELLO_V2_HISIG_PACKAGE", v2_hisig))
    parts.append("\n")
    parts.append(c_array("HELLO_BADBOARD_PACKAGE", badboard))
    parts.append("\n")
    parts.append(c_array("HELLO_BADABI_PACKAGE", badabi))
    if foreign_pkg is not None:
        parts.append("\n")
        parts.append(c_array("HELLO_FOREIGN_PACKAGE", foreign_pkg))
    parts.append("\n#endif\n")

    with open(header_path, "w", encoding="utf-8") as handle:
        handle.write("".join(parts))

    print("anchor_spki_sha256=" + fingerprint.hex())
    print("hello_v1_package_sha256=" + hashlib.sha256(v1).hexdigest())
    print("hello_v1_tbs_sha256=" + v1_tbs.hex())
    print("hello_v2_package_sha256=" + hashlib.sha256(v2).hexdigest())
    print("hello_v2_tbs_sha256=" + v2_tbs.hex())
    print("hello_v2_alt_package_sha256=" + hashlib.sha256(v2_alt).hexdigest())
    print("hello_v2_alt_tbs_sha256=" + v2_alt_tbs.hex())
    print("hello_v2_hisig_package_sha256=" + hashlib.sha256(v2_hisig).hexdigest())
    print("hello_v2_hisig_tbs_sha256=" + v2_hisig_tbs.hex())
    print("hello_badboard_package_sha256=" + hashlib.sha256(badboard).hexdigest())
    print("hello_badabi_package_sha256=" + hashlib.sha256(badabi).hexdigest())
    if foreign_pkg is not None:
        print("hello_foreign_package_sha256=" + hashlib.sha256(foreign_pkg).hexdigest())
    print("wrote " + header_path)


if __name__ == "__main__":
    main()
