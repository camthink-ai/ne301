from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
enc = (root / "Custom/Hal/enc.c").read_text()
ws = (root / "Custom/Services/Web/websocket_stream_server.c").read_text()

def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]

encode = section(enc, "static int VENC_H264_Encode(enc_t *enc)\n{",
                 "static int ENC_H264_Init(")
process = section(enc, "static void encProcess(void *argument)\n{",
                  "static int enc_start(")
start = section(enc, "static int enc_start(void *priv)\n{",
                "static int enc_ioctl(")
ioctl = section(enc, "static int enc_ioctl(", "static int enc_init(")
join_request = section(ws, "static void ws_request_join_idr(void)\n{",
                       "static void ws_frame_fifo_drain(void)\n{")

checks = {
    "atomic-force-sample-before-venc-call":
        "__atomic_exchange_n(&enc->is_intra_force, 0, __ATOMIC_ACQ_REL)" in encode and
        encode.index("__atomic_exchange_n") < encode.index("VENC_H264_EncodeFrame("),
    "encode-sample-does-not-invert-lock-order": "osMutexAcquire(enc->state_mtx" not in encode,
    "completion-keeps-new-pending-request": "enc->is_intra_force = 0" not in process and
        "__atomic_store_n(&enc->is_intra_force, 1" in process,
    "startup-force-remains-enabled":
        ("__atomic_store_n(&enc->is_intra_force, 1" in start or
         "enc->is_intra_force = 1;" in start),
    "hw-lock-released-before-completion-state-lock":
        process.index("osMutexRelease(enc->hw_mtx)") <
        process.index("osMutexAcquire(enc->state_mtx", process.index("osMutexRelease(enc->hw_mtx)")),
    "force-ioctl-uses-atomic-state":
        "__atomic_store_n(&enc->is_intra_force, 1" in ioctl and
        "osMutexAcquire(enc->state_mtx" in ioctl and
        "osMutexAcquire(enc->hw_mtx" not in ioctl,
    "join-request-keeps-two-second-limit":
        "#define WS_JOIN_IDR_REQUEST_MIN_MS 2000" in ws and
        "now_ms - last_req_ms < WS_JOIN_IDR_REQUEST_MIN_MS" in join_request and
        "device_ioctl(enc_dev, ENC_CMD_FORCE_INTRA" in join_request,
    "join-request-does-not-take-ws-locks":
        "g_websocket_server.mutex" not in join_request and
        "s_ws_fifo_mutex" not in join_request,
}

for name, ok in checks.items():
    print(("PASS" if ok else "FAIL") + " " + name)
if not all(checks.values()):
    sys.exit(1)
