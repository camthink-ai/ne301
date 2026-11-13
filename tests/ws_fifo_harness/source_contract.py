from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
source = (root / "Custom/Services/Web/websocket_stream_server.c").read_text()
start = source.index("static void ws_frame_fifo_drain(void)\n{")
end = source.index("static void ws_stream_event_handler", start)
drain = source[start:end]
send_positions = [i for i in range(len(drain)) if drain.startswith("mg_ws_send(", i)]
release_pos = drain.find("s_ws_fifo_count--")

def lock_free_at_sends(lock_name):
    acquire = "osMutexAcquire(" + lock_name
    release = "osMutexRelease(" + lock_name
    for send_pos in send_positions:
        last_acquire = drain.rfind(acquire, 0, send_pos)
        last_release = drain.rfind(release, 0, send_pos)
        if last_acquire < 0 or last_release <= last_acquire:
            return False
    return bool(send_positions)

checks = {
    "fifo-slot-release-after-every-consumer": bool(send_positions) and
        release_pos > max(send_positions),
    "fifo-unlocked-during-every-send": lock_free_at_sends("s_ws_fifo_mutex"),
    "server-unlocked-during-every-send":
        lock_free_at_sends("g_websocket_server.mutex"),
    "producer-does-not-take-server-lock":
        "g_websocket_server.mutex" not in source[source.index("static void ws_frame_fifo_push(const void *packet, size_t size)\n{"):source.index("static uint8_t *ws_build_join_frame(")],
}

for name, ok in checks.items():
    print(("PASS" if ok else "FAIL") + " " + name)
if not all(checks.values()):
    sys.exit(1)
