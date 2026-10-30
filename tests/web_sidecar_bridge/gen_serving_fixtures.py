import argparse
import hashlib
import importlib.util
import os
import struct

SOURCE = os.path.join(os.path.dirname(__file__), "..", "app_sidecar", "gen_sidecar_fixtures.py")
SPEC = importlib.util.spec_from_file_location("sidecar_fixture_source", SOURCE)
FIXTURES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FIXTURES)


def build_unsigned_bundle(files, version=(1, 4, 2), app=FIXTURES.TEST_APP_ID,
                          native_tbs=None, bound_native_version=(1, 0, 0)):
    if native_tbs is None:
        native_tbs = hashlib.sha256(FIXTURES.NATIVE_TBS_SEED).digest()
    records = bytearray()
    payload = bytearray()
    for path, data in files:
        records.extend(struct.pack("<HI", len(path), len(data)))
        records.extend(hashlib.sha256(data).digest())
        records.extend(path)
        payload.extend(data)
    manifest = bytearray(136)
    manifest[16:16 + len(app)] = app
    manifest[80:112] = native_tbs
    for index, part in enumerate(version):
        struct.pack_into("<H", manifest, 112 + index * 2, part)
    struct.pack_into("<H", manifest, 118, len(files))
    struct.pack_into("<I", manifest, 120, 1)
    for index, part in enumerate(bound_native_version):
        struct.pack_into("<H", manifest, 124 + index * 2, part)
    manifest.extend(records)
    return (b"NEUI\x02\x00\x00\x00" +
            struct.pack("<II", len(manifest), len(payload)) +
            bytes(manifest) + bytes(payload))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    os.makedirs(args.output, exist_ok=True)
    files = FIXTURES.make_files(FIXTURES.SMALL_FILES)
    bundle = build_unsigned_bundle(files)
    with open(os.path.join(args.output, "serving_small.bin"), "wb") as stream:
        stream.write(bundle)
    upgraded = build_unsigned_bundle(files, version=(1, 5, 1))
    with open(os.path.join(args.output, "serving_upgrade.bin"), "wb") as stream:
        stream.write(upgraded)
    print("unsigned serving fixtures written to", args.output)


if __name__ == "__main__":
    main()
