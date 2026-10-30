import hashlib
import json
import os
import shlex
import subprocess
import tempfile
from pathlib import Path

from synthetic_identity import generate_identity, write_config


root = Path(__file__).resolve().parents[2]
test_dir = root / "tests" / "app_admission"
app_host = root / "Custom" / "Services" / "AppHost"
common = root / "Custom" / "Common"
cc = shlex.split(os.environ.get("CC", "cc"))
base_flags = [
    "-std=c11",
    "-O1",
    "-g",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-DMBEDTLS_CONFIG_FILE=\"host_user_config.h\"",
    "-DMBEDTLS_ALLOW_PRIVATE_ACCESS",
]
include_dirs = [
    root / "tests" / "sign_spike" / "shim",
    root / "tests" / "storage_gate" / "shim",
    test_dir,
    app_host,
    root / "Custom" / "Services" / "Web",
    root / "Custom" / "Services" / "Web" / "api",
    common / "Inc",
    common / "Utils",
    common / "Lib",
    common / "Lib" / "cJSON",
    common / "Lib" / "MbedTLS" / "include",
    common / "Lib" / "MbedTLS" / "configs",
    root / "tests" / "sign_spike",
    root / "tests" / "app_installer",
]
for include_dir in include_dirs:
    base_flags.extend(["-I", str(include_dir)])

mbedtls_objects = sorted((test_dir / "build").glob("*.o"))
if not mbedtls_objects:
    raise SystemExit("MbedTLS test objects are missing; run make test first")


def compile_case(name, profile, config, expected_status, expected_profile,
                 expected_publisher, expected_digest, expected_provenance,
                 test_package_acceptance, work):
    anchor_object = work / (name + "_anchor.o")
    test_object = work / (name + "_test.o")
    executable = work / name
    anchor_command = cc + base_flags
    if profile == "production":
        anchor_command.append(
            "-DAPP_TRUST_PROFILE=APP_TRUST_PROFILE_PRODUCTION")
        if config is not None:
            anchor_command.append(
                "-DAPP_TRUST_PROVISION_HEADER=\"%s\"" % config)
    anchor_command.extend([
        "-c",
        str(app_host / "app_trust_anchor.c"),
        "-o",
        str(anchor_object),
    ])
    subprocess.run(anchor_command, cwd=root, check=True)
    subprocess.run(
        cc + base_flags + [
            "-c",
            str(test_dir / "test_app_trust_profile.c"),
            "-o",
            str(test_object),
        ],
        cwd=root,
        check=True,
    )
    subprocess.run(
        cc + base_flags + [
            str(test_object),
            str(anchor_object),
            str(test_dir / "app_admission.o"),
            str(test_dir / "generic_math.o"),
        ] + [str(obj) for obj in mbedtls_objects] + ["-o", str(executable)],
        cwd=root,
        check=True,
    )
    result = subprocess.run(
        [
            str(executable),
            str(expected_status),
            expected_profile,
            expected_publisher,
            expected_digest,
            expected_provenance,
            str(test_package_acceptance),
        ],
        cwd=root,
        check=True,
        text=True,
        capture_output=True,
    )
    print("PROFILE PASS %s %s" % (name, result.stdout.splitlines()[0]))


def expect_firmware_compile_rejection(name, config, expected_error, work):
    command = cc + base_flags + [
        "-DAPP_TRUST_PROFILE=APP_TRUST_PROFILE_PRODUCTION",
        "-DAPP_TRUST_FIRMWARE_BUILD=1",
        "-DAPP_TRUST_PROVISION_HEADER=\"%s\"" % config,
        "-c",
        str(app_host / "app_trust_anchor.c"),
        "-o",
        str(work / (name + "_anchor.o")),
    ]
    result = subprocess.run(command, cwd=root, text=True, capture_output=True)
    if result.returncode == 0:
        raise SystemExit("PROFILE FAIL %s firmware compile unexpectedly succeeded" % name)
    if expected_error not in result.stderr:
        raise SystemExit("PROFILE FAIL %s missing compiler rejection: %s" %
                         (name, result.stderr))
    print("PROFILE PASS %s firmware compile rejected" % name)


with tempfile.TemporaryDirectory(prefix="ne301-host-trust-profile-") as temp:
    work = Path(temp)
    _, _, der, digest = generate_identity()
    synthetic_publisher = "synthetic-pub"
    synthetic_provenance = "SYNTHETIC_TEST_ONLY:generated-per-run"
    valid_config = work / "synthetic.h"
    write_config(valid_config, synthetic_publisher, der, digest,
                 synthetic_provenance)

    no_provenance_config = work / "no-provenance.h"
    write_config(no_provenance_config, synthetic_publisher, der, digest,
                 synthetic_provenance,
                 fields={
                     "APP_TRUST_PRODUCTION_PUBLISHER_ID",
                     "APP_TRUST_PRODUCTION_SPKI_DER",
                     "APP_TRUST_PRODUCTION_SPKI_SHA256",
                     "APP_TRUST_PRODUCTION_SPKI_SHA256_HEX",
                 })

    partial_config = work / "partial.h"
    write_config(partial_config, synthetic_publisher, der, digest,
                 synthetic_provenance,
                 fields={"APP_TRUST_PRODUCTION_PUBLISHER_ID"})

    test_publisher_config = work / "test-publisher.h"
    write_config(test_publisher_config, "ne301-exp-pub", der, digest,
                 "PROFILE_TEST_SOURCE",
                 provenance_class="APP_TRUST_PROVENANCE_CLASS_PRODUCTION")

    synthetic_provenance_config = work / "synthetic-provenance.h"
    write_config(synthetic_provenance_config, synthetic_publisher, der, digest,
                 synthetic_provenance,
                 provenance_class="APP_TRUST_PROVENANCE_CLASS_PRODUCTION")

    invalid_publisher_config = work / "invalid-publisher.h"
    write_config(invalid_publisher_config, "Invalid Publisher", der, digest,
                 synthetic_provenance)

    empty_publisher_config = work / "empty-publisher.h"
    write_config(empty_publisher_config, "", der, digest, synthetic_provenance)

    oversized_publisher = "publisher-1234567"
    oversized_publisher_config = work / "oversized-publisher.h"
    write_config(oversized_publisher_config, oversized_publisher, der, digest,
                 synthetic_provenance)

    mismatch_digest = hashlib.sha256(der + b"digest-mismatch").digest()
    mismatch_config = work / "digest-mismatch.h"
    write_config(mismatch_config, synthetic_publisher, der, mismatch_digest,
                 synthetic_provenance)

    malformed_digest_hex_config = work / "malformed-digest-hex.h"
    write_config(malformed_digest_hex_config, synthetic_publisher, der, digest,
                 synthetic_provenance,
                 overrides={"APP_TRUST_PRODUCTION_SPKI_SHA256_HEX":
                            json.dumps(digest.hex().upper())})

    empty_provenance_config = work / "empty-provenance.h"
    write_config(empty_provenance_config, synthetic_publisher, der, digest, "")

    invalid_provenance_class_config = work / "invalid-provenance-class.h"
    write_config(invalid_provenance_class_config, synthetic_publisher, der,
                 digest, synthetic_provenance, provenance_class="3")

    malformed_der = bytes([der[0] ^ 1]) + der[1:]
    malformed_config = work / "malformed-der.h"
    write_config(malformed_config, synthetic_publisher, malformed_der,
                 hashlib.sha256(malformed_der).digest(), synthetic_provenance)

    cases = [
        ("test-default", "test", None, 0, "test", "ne301-exp-pub",
         "06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda",
         "TEST_ONLY:compiled-ne301-exp-pub", 1),
        ("production-unprovisioned", "production", None, -1, "production",
         "unprovisioned", "unprovisioned", "unprovisioned", 0),
        ("production-synthetic", "production", valid_config, 0, "production",
         synthetic_publisher, digest.hex(), synthetic_provenance, 0),
        ("production-test-publisher", "production", test_publisher_config, -2,
         "production", "ne301-exp-pub", digest.hex(), "PROFILE_TEST_SOURCE", 0),
        ("production-invalid-publisher", "production", invalid_publisher_config,
         -2, "production", "Invalid Publisher", digest.hex(),
         synthetic_provenance, 0),
        ("production-empty-publisher", "production", empty_publisher_config,
         -2, "production", "", digest.hex(), synthetic_provenance, 0),
        ("production-oversized-publisher", "production",
         oversized_publisher_config, -2, "production", oversized_publisher,
         digest.hex(), synthetic_provenance, 0),
        ("production-digest-mismatch", "production", mismatch_config, -2,
         "production", synthetic_publisher, mismatch_digest.hex(),
         synthetic_provenance, 0),
        ("production-malformed-digest-hex", "production",
         malformed_digest_hex_config, -2, "production", synthetic_publisher,
         digest.hex().upper(), synthetic_provenance, 0),
        ("production-malformed-der", "production", malformed_config, -2,
         "production", synthetic_publisher, hashlib.sha256(malformed_der).hexdigest(),
         synthetic_provenance, 0),
        ("production-empty-provenance", "production",
         empty_provenance_config, -2, "production", synthetic_publisher,
         digest.hex(), "", 0),
        ("production-invalid-provenance-class", "production",
         invalid_provenance_class_config, -2, "production",
         synthetic_publisher, digest.hex(), synthetic_provenance, 0),
        ("production-no-provenance", "production", no_provenance_config, -1,
         "production", "unprovisioned", "unprovisioned", "unprovisioned", 0),
        ("production-partial-config", "production", partial_config, -1,
         "production", "unprovisioned", "unprovisioned", "unprovisioned", 0),
    ]
    for case in cases:
        compile_case(*case, work=work)
    expect_firmware_compile_rejection(
        "firmware-synthetic-source", valid_config,
        "APP_TRUST_PRODUCTION_PROVENANCE_CLASS must be production in firmware",
        work)
    expect_firmware_compile_rejection(
        "firmware-synthetic-provenance", synthetic_provenance_config,
        "production provenance must not be synthetic", work)
    expect_firmware_compile_rejection(
        "firmware-test-publisher", test_publisher_config,
        "production publisher must not be ne301-exp-pub", work)
    print("trust_profile_build_matrix: %d cases, 0 failures" % len(cases))
