#!/usr/bin/env python3
"""Verify installed stedgeai matches STEDGEAI_VARIANT / MODEL_STEDGEAI_VERSION."""

from __future__ import annotations

import os
import re
import subprocess
import sys
from typing import Optional

# Keep in sync with stedgeai.mk
VARIANT_TO_EXPECTED = {
    '2.2': 'v2.2',
    '3.0': 'v3.0.0',
    '4.0': 'v4.0.1',
}


def get_stedgeai_executable() -> str:
    core_dir = os.environ.get('STEDGEAI_CORE_DIR')
    if core_dir:
        candidates = [
            os.path.join(core_dir, 'Utilities', 'windows', 'stedgeai.exe'),
            os.path.join(core_dir, 'Utilities', 'windows', 'stedgeai'),
            os.path.join(core_dir, 'Utilities', 'linux', 'stedgeai'),
        ]
        for candidate in candidates:
            if os.path.isfile(candidate):
                return candidate
    return 'stedgeai'


def get_stedgeai_version_line() -> tuple[Optional[str], str]:
    """Return (parsed version token, full first line of stedgeai --version)."""
    stedgeai_bin = get_stedgeai_executable()
    try:
        result = subprocess.run(
            [stedgeai_bin, '--version'],
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as exc:
        return None, f'{stedgeai_bin}: {exc}'

    if result.returncode != 0:
        err = (result.stderr or result.stdout or '').strip()
        return None, f'{stedgeai_bin}: exit {result.returncode} {err}'

    first_line = (result.stdout or '').strip().split('\n')[0]
    match = re.search(r'(v\d+\.\d+\.\d+(?:-[\w]+)?(?:\s+\d+)?)', first_line)
    version = match.group(1).strip() if match else None
    return version, first_line or stedgeai_bin


def expected_version_token() -> tuple[str, str]:
    variant = os.environ.get('STEDGEAI_VARIANT', '4.0')
    expected = os.environ.get('MODEL_STEDGEAI_VERSION') or VARIANT_TO_EXPECTED.get(variant)
    if not expected:
        print(
            f'ERROR: Unsupported STEDGEAI_VARIANT={variant!r}. Use 2.2, 3.0, or 4.0.',
            file=sys.stderr,
        )
        sys.exit(1)
    return variant, expected


def version_matches(installed: str, expected: str) -> bool:
    return expected in installed


def main() -> int:
    variant, expected = expected_version_token()
    installed, detail = get_stedgeai_version_line()
    stedgeai_bin = get_stedgeai_executable()

    if not installed:
        print('ERROR: Failed to read stedgeai --version', file=sys.stderr)
        print(f'  Tool: {stedgeai_bin}', file=sys.stderr)
        print(f'  Detail: {detail}', file=sys.stderr)
        return 1

    if not version_matches(installed, expected):
        print('ERROR: ST Edge AI toolchain does not match build variant', file=sys.stderr)
        print(f'  STEDGEAI_VARIANT={variant} (requires tool version containing {expected!r})', file=sys.stderr)
        print(f'  Installed: {detail}', file=sys.stderr)
        print(f'  Tool path: {stedgeai_bin}', file=sys.stderr)
        if os.environ.get('STEDGEAI_CORE_DIR'):
            print(f'  STEDGEAI_CORE_DIR={os.environ["STEDGEAI_CORE_DIR"]}', file=sys.stderr)
        print(
            f'  Fix: point STEDGEAI_CORE_DIR to ST Edge AI {variant}, '
            f'or rebuild with STEDGEAI_VARIANT matching your installation.',
            file=sys.stderr,
        )
        return 1

    print(f'OK: stedgeai {installed} matches STEDGEAI_VARIANT={variant}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
