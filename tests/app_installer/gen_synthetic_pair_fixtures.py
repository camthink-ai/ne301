import hashlib
import struct
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
admission_dir = root / "tests" / "app_admission"
sys.path.insert(0, str(admission_dir))

from anchor_keygen import der_sig_bytes, ecdsa_sign, ecdsa_verify
from gen_v4_vectors import signed_package
from synthetic_identity import generate_identity, write_config

SMALL_FILES = (
    ("assets/app.min.js", 512),
    ("index.html", 128),
    ("libs/lib-a.min.js", 256),
)


def sha256(data):
    return hashlib.sha256(data).digest()


def native_tbs_length(package):
    return 16 + int.from_bytes(package[8:12], "little") + int.from_bytes(
        package[12:16], "little")


def make_native(private_scalar, public_point, publisher, version_minor=0,
                key_digest=None):
    package = signed_package(private_scalar, public_point, publisher_id=publisher,
                             version_minor=version_minor)
    if key_digest is None:
        return package
    tbs_len = native_tbs_length(package)
    tbs = bytearray(package[:tbs_len])
    tbs[16 + 128:16 + 160] = key_digest
    tbs = bytes(tbs)
    signature = der_sig_bytes(*ecdsa_sign(private_scalar, tbs))
    return tbs + signature


def deterministic_content(name, size):
    out = bytearray()
    counter = 0
    while len(out) < size:
        out.extend(sha256(name + b":" + str(counter).encode("ascii")))
        counter += 1
    return bytes(out[:size])


def make_files():
    return [(name.encode("ascii"), deterministic_content(name.encode("ascii"), size))
            for name, size in SMALL_FILES]


def make_ui(private_scalar, public_point, publisher, spki, native, version,
            files):
    tbs_len = native_tbs_length(native)
    records = bytearray()
    payload = bytearray()
    for path, data in files:
        records.extend(struct.pack("<HI", len(path), len(data)))
        records.extend(sha256(data))
        records.extend(path)
        payload.extend(data)
    manifest = bytearray()
    manifest.extend(publisher.encode("ascii").ljust(16, b"\x00"))
    manifest.extend(b"hello-app".ljust(32, b"\x00"))
    manifest.extend(sha256(spki))
    manifest.extend(sha256(native[:tbs_len]))
    manifest.extend(struct.pack("<HHHHII", version[0], version[1], version[2],
                                len(files), 1, 0))
    manifest.extend(records)
    tbs = (b"NEUI\x01\x00\x00\x00" + struct.pack("<II", len(manifest),
                                                    len(payload))
           + manifest + payload)
    signature = der_sig_bytes(*ecdsa_sign(private_scalar, tbs))
    if not ecdsa_verify(public_point, tbs, signature):
        raise RuntimeError("UI signature self-verification failed")
    return tbs + signature


def make_pair(private_scalar, public_point, native, ui, release_seq):
    tbs = (b"NEPK" + struct.pack("<HHII", 1, 1, len(native), len(ui))
           + native + ui + b"NEPAIR1\x00" + struct.pack("<Q", release_seq))
    signature = der_sig_bytes(*ecdsa_sign(private_scalar, tbs))
    if not ecdsa_verify(public_point, tbs, signature):
        raise RuntimeError("Pair signature self-verification failed")
    return tbs + signature


def make_signed_pair(private_scalar, public_point, publisher, spki,
                     native_minor, ui_version, ui_files, release_seq,
                     key_digest=None):
    native = make_native(private_scalar, public_point, publisher, native_minor,
                         key_digest)
    native_length = native_tbs_length(native)
    if not ecdsa_verify(public_point, native[:native_length],
                        native[native_length:]):
        raise RuntimeError("native signature self-verification failed")
    ui = make_ui(private_scalar, public_point, publisher, spki, native,
                 ui_version, ui_files)
    return make_pair(private_scalar, public_point, native, ui, release_seq)


def write_bytes(directory, name, value):
    (directory / name).write_bytes(value)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: gen_synthetic_pair_fixtures.py OUTPUT_DIR")
    output = Path(sys.argv[1]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    private_scalar, public_point, spki, digest = generate_identity()
    foreign_scalar, foreign_point, foreign_spki, foreign_digest = generate_identity()
    publisher = "synthetic-pub"
    provenance = "SYNTHETIC_TEST_ONLY:generated-per-run"
    write_config(output / "trust_config.h", publisher, spki, digest, provenance)
    base_files = make_files()
    ui_files_4 = [(path, bytes(value[i] ^ 0x31 for i in range(len(value))))
                  for path, value in base_files]
    ui_files_4_conflict = [
        (path, bytes(value[i] ^ 0x72 for i in range(len(value))))
        for path, value in base_files
    ]
    ui_files_5 = [
        (path, bytes(value[i] ^ 0xA5 for i in range(len(value))))
        for path, value in base_files
    ]
    seq3 = make_signed_pair(private_scalar, public_point, publisher, spki, 0,
                            (1, 0, 0), base_files, 3)
    seq4 = make_signed_pair(private_scalar, public_point, publisher, spki, 0,
                            (1, 1, 0), ui_files_4, 4)
    seq4_conflict = make_signed_pair(private_scalar, public_point, publisher,
                                     spki, 0, (1, 1, 0),
                                     ui_files_4_conflict, 4)
    seq5 = make_signed_pair(private_scalar, public_point, publisher, spki, 1,
                            (1, 2, 0), ui_files_5, 5)
    mixed_publisher = make_signed_pair(private_scalar, public_point, "mixed-pub",
                                       spki, 0, (1, 0, 0), base_files, 9)
    mixed_spki = make_signed_pair(private_scalar, public_point, publisher, spki,
                                  0, (1, 0, 0), base_files, 9, foreign_digest)
    inband_root = make_signed_pair(foreign_scalar, foreign_point, publisher,
                                   foreign_spki, 0, (1, 0, 0), base_files, 9)
    malformed = bytearray(seq3)
    malformed[0] ^= 0x20
    fixtures = {
        "seq3.bin": seq3,
        "seq4.bin": seq4,
        "seq4-conflict.bin": seq4_conflict,
        "seq5.bin": seq5,
        "mixed-publisher.bin": mixed_publisher,
        "mixed-spki.bin": mixed_spki,
        "inband-root-replacement.bin": inband_root,
        "malformed.bin": bytes(malformed),
    }
    for name, value in fixtures.items():
        write_bytes(output, name, value)
    (output / "fixtures.stamp").write_text(
        "synthetic-pair-fixtures-generated\n", encoding="ascii")
    print("synthetic_pair_fixtures=%d signatures=ECDSA_P256_SHA256 "
          "publisher=%s spki_sha256=%s provenance=%s" %
          (len(fixtures), publisher, digest.hex(), provenance))


if __name__ == "__main__":
    main()
