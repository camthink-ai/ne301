import hashlib
import json
import secrets

from anchor_keygen import GX, GY, N, ec_mul, spki_der


def generate_identity():
    private_scalar = secrets.randbelow(N - 1) + 1
    public_point = ec_mul(private_scalar, (GX, GY))
    der = spki_der(public_point)
    return private_scalar, public_point, der, hashlib.sha256(der).digest()


def c_bytes(value):
    return "{ " + ", ".join("0x%02x" % byte for byte in value) + " }"


def write_config(path, publisher_id, der, digest, provenance,
                 fields=None, overrides=None,
                 provenance_class="APP_TRUST_PROVENANCE_CLASS_SYNTHETIC"):
    values = {
        "APP_TRUST_PRODUCTION_PUBLISHER_ID": json.dumps(publisher_id),
        "APP_TRUST_PRODUCTION_SPKI_DER": c_bytes(der),
        "APP_TRUST_PRODUCTION_SPKI_SHA256": c_bytes(digest),
        "APP_TRUST_PRODUCTION_SPKI_SHA256_HEX": json.dumps(digest.hex()),
        "APP_TRUST_PRODUCTION_PROVENANCE": json.dumps(provenance),
        "APP_TRUST_PRODUCTION_PROVENANCE_CLASS": provenance_class,
    }
    if overrides is not None:
        values.update(overrides)
    selected = set(values) if fields is None else set(fields)
    lines = ["#define %s %s" % (name, values[name])
             for name in sorted(selected)]
    path.write_text("\n".join(lines) + "\n", encoding="ascii")
