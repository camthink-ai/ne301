import glob
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
suites = [
    "tests/app_admission",
    "tests/app_unsigned",
    "tests/app_installer",
    "tests/app_host_run",
    "tests/app_host_session",
    "tests/app_host_control",
    "tests/app_sidecar",
    "tests/app-platform-offline",
    "tests/app_sidecar_lifecycle",
    "tests/app_state",
    "tests/app_report",
    "tests/storage_gate",
]


def existing_clean_outputs(suite):
    result = subprocess.run(
        ["make", "-s", "-C", str(root / suite), "-n", "clean"],
        check=True,
        text=True,
        capture_output=True,
    )
    found = []
    for line in result.stdout.splitlines():
        parts = shlex.split(line)
        if not parts or parts[0] != "rm":
            continue
        for target in parts[2:]:
            if target.startswith("-"):
                continue
            matches = glob.glob(str(root / suite / target))
            found.extend(Path(match) for match in matches if Path(match).exists())
    return found


def run(command, label, environment=None, verbose=False):
    print(f"=== {label} ===", flush=True)
    result = subprocess.run(command, cwd=root, env=environment, text=True, capture_output=True)
    if result.returncode != 0:
        if result.stdout:
            print(result.stdout, end="", flush=True)
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr, flush=True)
        raise subprocess.CalledProcessError(result.returncode, command)
    if verbose:
        if result.stdout:
            print(result.stdout, end="", flush=True)
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr, flush=True)
    else:
        summary = re.compile(
            r"^(test_unsigned_admission:|app_install_gate:|test_app_installer_storage:|app_host_run:|app_host_control:|app_control_gate:|app_state_store:|app_state_faults:|app_report:|PASS NEAPP05|fixtures=|checks=)"
        )
        for line in (result.stdout + result.stderr).splitlines():
            if summary.match(line):
                print(line, flush=True)
    print(f"SUITE PASS={label}", flush=True)


def cleanup():
    failed = []
    for suite in reversed(suites):
        result = subprocess.run(
            ["make", "-s", "-C", str(root / suite), "clean"],
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            failed.append(suite)
    return failed


for suite in suites:
    artifacts = existing_clean_outputs(suite)
    if artifacts:
        names = ", ".join(str(path.relative_to(root)) for path in artifacts)
        raise SystemExit(f"refusing to remove pre-existing build outputs in {suite}: {names}")

started = False
try:
    with tempfile.TemporaryDirectory(prefix="ne301-app-platform-offline-") as temp:
        temp_path = Path(temp)
        run(
            [
                "python3",
                "Script/version_header.py",
                "--fsbl-version",
                "1.0.3.1",
                "--stedgeai-version",
                "3.0.0",
                "--output",
                str(temp_path / "version.h"),
                "--fsbl-output",
                str(temp_path / "fsbl_version.h"),
                "--stedgeai-output",
                str(temp_path / "stedgeai_version.h"),
            ],
            "version header compile preflight",
            verbose=True,
        )
        started = True
        environment = os.environ.copy()
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        environment["CPATH"] = os.pathsep.join(
            part for part in [str(temp_path), environment.get("CPATH", "")] if part
        )
        for suite in suites:
            run(["make", "-s", "-C", str(root / suite), "test"], suite, environment)
    print("BROWSER_STUDY=historical #99 Chrome evidence audited; current browser rerun NOT_RUN")
    print("REAL_PURGE_INTEGRATION=BLOCKED/NOT_AVAILABLE")
    print("DEVICE_TESTS=NOT_RUN")
except subprocess.CalledProcessError as error:
    print(f"FAIL command={error.cmd} exit={error.returncode}", file=sys.stderr)
    if started:
        failed = cleanup()
        if failed:
            print("CLEANUP_FAILED=" + ",".join(failed), file=sys.stderr)
    raise SystemExit(error.returncode)
except Exception:
    if started:
        failed = cleanup()
        if failed:
            print("CLEANUP_FAILED=" + ",".join(failed), file=sys.stderr)
    raise
else:
    failed = cleanup()
    if failed:
        print("CLEANUP_FAILED=" + ",".join(failed), file=sys.stderr)
        raise SystemExit(1)
    print("GENERATED_ARTIFACTS=CLEAN")
