import json
import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP_SHA = "9a5db17cfba6f8ab52d17b031784e71c66a5ab32"
STUDY = Path(__file__).resolve().parent


def run(command, cwd=None):
    subprocess.run([str(part) for part in command], cwd=cwd, check=True)


def assert_report(path, tracks_enabled):
    raw = path.read_bytes()
    report = json.loads(raw)
    if len(raw) > 6144:
        raise RuntimeError("App report exceeded LC_APP_REPORT_MAX")
    if report["type"] != "line_counting":
        raise RuntimeError("unexpected App report type")
    if report["window"]["start_ms"] != 0 or report["window"]["end_ms"] != 61000:
        raise RuntimeError("report did not close on the one-minute window tick")
    if report["window"]["in"] != 1 or report["window"]["out"] != 1:
        raise RuntimeError("App crossing vector did not produce one IN and one OUT")
    if tracks_enabled:
        tracks = report.get("tracks")
        if not isinstance(tracks, list) or not tracks or len(tracks) > 12:
            raise RuntimeError("tracks were absent or exceeded the producer cap")
        events = {event for track in tracks for event in track.get("events", [])}
        if not {"line_cross_in", "line_cross_out"}.issubset(events):
            raise RuntimeError("window report did not contain both crossing directions")
        if any(len(track.get("points", [])) > 8 for track in tracks):
            raise RuntimeError("track points exceeded the producer cap")
        if any(not track.get("points") for track in tracks):
            raise RuntimeError("crossing track has no points")
    elif "tracks" in report:
        raise RuntimeError("tracks_report_enable=false still emitted tracks")
    return len(raw), report


def copy_host_fixture(root):
    target = root / "repo"
    (target / "tests").mkdir(parents=True)
    (target / "Custom" / "Services").mkdir(parents=True)
    (target / "Custom" / "Common").mkdir(parents=True)
    shutil.copytree(ROOT / "tests" / "app_report", target / "tests" / "app_report",
                    ignore=shutil.ignore_patterns("*.o", "test_app_report", "gen", "gen-stamp"))
    shutil.copytree(ROOT / "tests" / "line-counting-bridge-study",
                    target / "tests" / "line-counting-bridge-study")
    shutil.copytree(ROOT / "Custom" / "Services" / "AppHost",
                    target / "Custom" / "Services" / "AppHost")
    shutil.copytree(ROOT / "Custom" / "Common" / "Inc",
                    target / "Custom" / "Common" / "Inc")
    return target


def main():
    subprocess.run(["git", "cat-file", "-e", APP_SHA + "^{commit}"], cwd=ROOT, check=True)
    with tempfile.TemporaryDirectory(prefix="line-counting-bridge-study-") as temp_name:
        temp = Path(temp_name)
        app_dir = temp / "app"
        app_dir.mkdir()
        archive_path = temp / "app-source.tar"
        with archive_path.open("wb") as archive:
            subprocess.run(["git", "archive", "--format=tar", APP_SHA,
                            "apps/line-crossing", "tests/line-crossing-app"],
                           cwd=ROOT, stdout=archive, check=True)
        with tarfile.open(archive_path) as archive:
            archive.extractall(app_dir)
        app_root = app_dir
        app = app_root / "apps" / "line-crossing"
        app_tests = app_root / "tests" / "line-crossing-app"
        app_probe = STUDY / "app_window_probe.c"
        app_binary = temp / "app_window_probe"
        cc = os.environ.get("CC", "cc")
        app_sources = [
            app / "app" / "lc_app_entry.c",
            app / "app" / "lc_bus.c",
            app / "app" / "lc_bus_config.c",
            app / "app" / "lc_stateblob.c",
            app / "app" / "lc_json.c",
            app / "app" / "lc_arena.c",
            app / "src" / "lc_tracker.c",
            app / "src" / "lc_line_cross.c",
            app_tests / "host_stub.c",
        ]
        run([cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-I" + str(app / "include"), "-I" + str(app / "app"),
             "-I" + str(app_tests), str(app_probe), *app_sources,
             "-o", str(app_binary)])
        on_path = temp / "report-tracks-on.json"
        off_path = temp / "report-tracks-off.json"
        run([app_binary, on_path, off_path])
        on_len, on_report = assert_report(on_path, True)
        off_len, _ = assert_report(off_path, False)
        print("app_window_report_tracks_on_bytes=" + str(on_len))
        print("app_window_report_tracks_off_bytes=" + str(off_len))
        print("app_window_report_tracks=" + str(len(on_report["tracks"])))
        print("app_window_report_points=" + str(sum(len(track["points"]) for track in on_report["tracks"])))

        host_root = copy_host_fixture(temp)
        app_report_dir = host_root / "tests" / "app_report"
        run(["make", "test"], cwd=app_report_dir)
        gen = app_report_dir / "gen"
        apphost = host_root / "Custom" / "Services" / "AppHost"
        common_inc = host_root / "Custom" / "Common" / "Inc"
        bridge_binary = temp / "host_report_bridge"
        bridge_source = host_root / "tests" / "line-counting-bridge-study" / "host_report_bridge.c"
        flags = ["-DAPP_REPORT_DEV_PUMP_PERIOD_MS=20",
                 "-DAPP_REPORT_DEV_ACK_WAIT_MS=60",
                 "-DAPP_REPORT_DEV_ACK_POLL_MS=5",
                 "-DAPP_REPORT_DEV_STOP_JOIN_MS=300",
                 "-DAPP_REPORT_DEV_STOP_POLL_MS=5",
                 "-DAPP_REPORT_DEV_TRANSPORT_MAX_MS=20"]
        run([cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-pthread",
             "-I" + str(apphost), "-I" + str(common_inc), "-I" + str(gen), *flags,
             str(bridge_source), str(app_report_dir / "app_report_service.o"),
             str(app_report_dir / "app_report_service_device.o"), "-o", str(bridge_binary)])
        run([bridge_binary, on_path])
        print("study_result=PASS source_sha=" + APP_SHA)


if __name__ == "__main__":
    main()
