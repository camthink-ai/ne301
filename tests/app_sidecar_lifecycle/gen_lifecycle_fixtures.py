import argparse
import importlib.util
import os

SOURCE = os.path.join(os.path.dirname(__file__), "..", "app_sidecar", "gen_sidecar_fixtures.py")
SPEC = importlib.util.spec_from_file_location("sidecar_fixture_source", SOURCE)
FIXTURES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FIXTURES)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    os.makedirs(args.output, exist_ok=True)
    key = FIXTURES.derive_key(FIXTURES.ANCHOR_SEED)
    small = FIXTURES.make_files(FIXTURES.SMALL_FILES)
    paths = [("assets/%03d.js" % i).encode() for i in range(63)]
    remaining = 1048576
    files = [(b"index.html", FIXTURES.deterministic_content("index.html", 1048576))]
    for i, path in enumerate(paths):
        size = remaining // 63 + (1 if i < remaining % 63 else 0)
        files.append((path, FIXTURES.deterministic_content(path.decode(), size)))
    files.sort(key=lambda item: item[0])
    bundle = FIXTURES.build_bundle(key, files, version=(3, 0, 0))
    output = os.path.join(args.output, "positive_limit_64.bin")
    with open(output, "wb") as stream:
        stream.write(bundle)
    changed = [(path, bytes([data[0] ^ 1]) + data[1:])
               if path == b"assets/app.min.js" else (path, data)
               for path, data in small]
    upgrade = FIXTURES.build_bundle(key, changed, version=(1, 5, 0))
    conflict = FIXTURES.build_bundle(key, changed, version=(1, 4, 2))
    downgrade = FIXTURES.build_bundle(key, small, version=(1, 4, 1))
    nested = FIXTURES.build_bundle(
        key,
        sorted(small + [(b"docs/index.html", FIXTURES.deterministic_content("docs/index.html", 128))],
               key=lambda item: item[0]))
    duplicate_root = FIXTURES.build_bundle(
        key,
        sorted(small + [(b"index.html", FIXTURES.deterministic_content("duplicate-index", 128))],
               key=lambda item: item[0]))
    for name, data in (("positive_upgrade.bin", upgrade),
                       ("positive_version_conflict.bin", conflict),
                       ("positive_downgrade.bin", downgrade),
                       ("positive_nested_index.bin", nested),
                       ("negative_duplicate_root_index.bin", duplicate_root)):
        with open(os.path.join(args.output, name), "wb") as stream:
            stream.write(data)
    interoperability_key = FIXTURES.ec.derive_private_key(1, FIXTURES.ec.SECP256R1())
    interoperability_spki = FIXTURES.spki_der(interoperability_key)
    interoperability_native_tbs = bytes.fromhex(
        "d793a455e3ccda715c8ee9ae95a718cf0fc2627c0ff20969f23e7ea9349b349e")
    interoperability_files = [
        (b"assets/host-controlled.js", b"window.sidecarInterop='host86';"),
        (b"index.html", b"<!doctype html><title>Host 86 fixture</title>"),
    ]
    interoperability_files.sort(key=lambda item: item[0])
    interoperability_tbs = FIXTURES.build_from_parts(
        interoperability_key,
        b"test-publisher",
        b"line-crossing",
        FIXTURES.sha256(interoperability_spki),
        interoperability_native_tbs,
        (1, 0, 1),
        len(interoperability_files),
        1,
        0,
        FIXTURES.build_records(interoperability_files),
        b"".join(data for _, data in interoperability_files),
        skip_sign=True)
    interoperability_signature = interoperability_key.sign(
        interoperability_tbs,
        FIXTURES.ec.ECDSA(FIXTURES.hashes.SHA256(), deterministic_signing=True))
    interoperability_bundle = interoperability_tbs + interoperability_signature
    with open(os.path.join(args.output, "host_controlled.neui"), "wb") as stream:
        stream.write(interoperability_bundle)
    payload_len = sum(len(data) for _, data in files)
    manifest_len = 128 + sum(38 + len(path) for path, _ in files)
    print("bundle=%d manifest=%d payload=%d files=%d" %
          (len(bundle), manifest_len, payload_len, len(files)))


if __name__ == "__main__":
    main()
