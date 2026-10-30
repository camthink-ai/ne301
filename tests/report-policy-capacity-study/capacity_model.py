import re
import sys

SLOT_CAP = 6240
INPUT_MAX = 6144
SEEN_RING = 16
MAX_ATTEMPTS = 3
BACKOFF_POLLS = 4
QUEUE_SLOTS_AUDITED = 8
PUMP_PERIOD_MS = 500
MQTT_ACK_WAIT_MS = 2000
WEBHOOK_TIMEOUT_MS = 15000
STOP_JOIN_MS = 5000
STOP_JOIN_FLOOR_MS = 2000 + 15000 + 500 + 2 * 10 + 200
PUMP_TASK_STACK = 2048

CAPACITIES = [1, 8, 16, 64, 256]

DEVICE_ENTRY_FIELDS = [
    ("payload", SLOT_CAP, 1),
    ("payload_len", 4, 4),
    ("app_id", 33, 1),
    ("key_sha", 32, 1),
    ("report_seq", 4, 4),
    ("mqtt_state", 4, 4),
    ("webhook_state", 4, 4),
    ("mqtt_attempts", 4, 4),
    ("webhook_attempts", 4, 4),
    ("mqtt_wait", 4, 4),
    ("webhook_wait", 4, 4),
    ("accept_order", 8, 8),
    ("in_flight", 4, 4),
    ("in_use", 4, 4),
]

DEVICE_TX_FIELDS = [
    ("entry", 4, 4),
    ("accept_order", 8, 8),
    ("do_mqtt", 4, 4),
    ("do_webhook", 4, 4),
    ("mqtt_outcome", 4, 4),
    ("webhook_outcome", 4, 4),
]

DEVICE_JR_MEMBER_FIELDS = [
    ("raw_off", 4, 4),
    ("name", 4, 4),
    ("name_len", 4, 4),
    ("value_off", 4, 4),
    ("value_end", 4, 4),
    ("type", 4, 4),
]

DEVICE_SESSION_FIELDS = [
    ("active", 4, 4),
    ("publisher_id", 17, 1),
    ("app_id", 33, 1),
    ("publisher_key_sha256", 32, 1),
    ("report_max", 4, 4),
]

DEVICE_STATS_BYTES = 10 * 4

HOST_DEFAULTS = {
    "sizeof_report_entry": 6360,
    "sizeof_tx_item": 32,
    "sizeof_jr_member": 48,
    "sizeof_seen_ring": 16 * (4 + 32 + 33 + 4 + 4),
    "sizeof_session": 92,
    "sizeof_stats": 40,
}

PER_ENTRY_WORST_SEND_S = (MQTT_ACK_WAIT_MS + WEBHOOK_TIMEOUT_MS) / 1000.0
PER_ENTRY_FAST_SEND_S = 0.11


def align_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def sizeof(fields):
    total = 0
    max_alignment = 1
    for _, size, alignment in fields:
        max_alignment = max(max_alignment, alignment)
        total = align_up(total, alignment) + size
    return align_up(total, max_alignment)


def parse_measured(path):
    values = dict(HOST_DEFAULTS)
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            match = re.match(r"CAPACITY_STUDY (\w+)=(\d+)", line)
            if match:
                values[match.group(1)] = int(match.group(2))
    return values


def seen_ring_bytes(ring):
    return ring * (4 + 32 + 33 + 4 + 4)


DEVICE_SESSION_MAX = sizeof(DEVICE_SESSION_FIELDS)


def main():
    measured = dict(HOST_DEFAULTS)
    if len(sys.argv) > 1:
        measured = parse_measured(sys.argv[1])
    host_entry = measured["sizeof_report_entry"]
    host_tx = measured["sizeof_tx_item"]
    host_jr = measured["sizeof_jr_member"]
    host_seen = measured["sizeof_seen_ring"]
    host_session = measured["sizeof_session"]
    host_stats = measured["sizeof_stats"]
    device_entry = sizeof(DEVICE_ENTRY_FIELDS)
    device_tx = sizeof(DEVICE_TX_FIELDS)
    device_jr = sizeof(DEVICE_JR_MEMBER_FIELDS)

    print("capacity_model.py offline output")
    print("measurement_class: theoretical_estimate host_structs_only")
    print("board_measurement: NOT_RUN")
    print()
    print("source constants")
    print(f"slot_payload_bytes={SLOT_CAP}")
    print(f"input_max_bytes={INPUT_MAX}")
    print(f"audited_queue_slots={QUEUE_SLOTS_AUDITED}")
    print(f"seen_ring={SEEN_RING}")
    print(f"max_attempts={MAX_ATTEMPTS}")
    print(f"backoff_polls={BACKOFF_POLLS}")
    print(f"pump_period_ms={PUMP_PERIOD_MS}")
    print(f"mqtt_ack_wait_ms={MQTT_ACK_WAIT_MS}")
    print(f"webhook_timeout_ms={WEBHOOK_TIMEOUT_MS}")
    print(f"stop_join_ms={STOP_JOIN_MS}")
    print(f"stop_join_floor_ms={STOP_JOIN_FLOOR_MS}")
    print(f"pump_task_stack_bytes={PUMP_TASK_STACK}")
    print()
    print("struct sizes")
    print(f"report_entry_host_measured={host_entry}")
    print(f"report_entry_device_model={device_entry}")
    print(f"tx_item_host_measured={host_tx}")
    print(f"tx_item_device_model={device_tx}")
    print(f"jr_member_host_measured={host_jr}")
    print(f"jr_member_device_model={device_jr}")
    print(f"submit_stack_members_256_host={host_jr * 256}")
    print(f"submit_stack_members_256_device={device_jr * 256}")
    print()
    print("audited static footprint (8 slots, host measured)")
    host_queue = host_entry * QUEUE_SLOTS_AUDITED
    host_tx_total = host_tx * QUEUE_SLOTS_AUDITED
    host_total = (
        host_queue
        + host_tx_total
        + host_seen
        + host_session
        + host_stats
        + SLOT_CAP
    )
    print(f"queue_8={host_queue}")
    print(f"tx_8={host_tx_total}")
    print(f"seen_ring={host_seen}")
    print(f"session={host_session}")
    print(f"stats={host_stats}")
    print(f"submit_scratch={SLOT_CAP}")
    print(f"audited_static_subtotal={host_total}")
    print()
    print("scaled RAM floor (device 32-bit model)")
    print("| capacity | payload floor | struct overhead above payload | seen fixed16 | seen dedup-parity | fixed scratch+stack | total fixed-seen | total dedup-parity |")
    print("|---|---|---|---|---|---|---|---|")
    for capacity in CAPACITIES:
        payload_floor = capacity * SLOT_CAP
        overhead = capacity * ((device_entry - SLOT_CAP) + device_tx)
        seen_fixed = seen_ring_bytes(SEEN_RING)
        seen_parity = seen_ring_bytes(max(capacity, SEEN_RING))
        fixed_rest = (
            DEVICE_SESSION_MAX
            + DEVICE_STATS_BYTES
            + SLOT_CAP
            + device_jr * 256
            + PUMP_TASK_STACK
        )
        total_fixed = payload_floor + overhead + seen_fixed + fixed_rest
        total_parity = payload_floor + overhead + seen_parity + fixed_rest
        print(
            f"| {capacity} | {payload_floor} | {overhead} | {seen_fixed} | "
            f"{seen_parity} | {fixed_rest} | {total_fixed} | {total_parity} |"
        )
    print()
    print("worst-case drain and saturation model (theory)")
    print("| capacity | worst batch drain s | sustained drain rate/min | fill time at 1 report/min |")
    print("|---|---|---|---|")
    for capacity in CAPACITIES:
        batch_s = capacity * PER_ENTRY_WORST_SEND_S
        cycle_s = batch_s + PUMP_PERIOD_MS / 1000.0
        sustained_per_min = capacity / cycle_s * 60.0
        fill_min = capacity
        print(
            f"| {capacity} | {batch_s:.1f} | {sustained_per_min:.2f} | "
            f"{fill_min} min |"
        )
    print()
    print(f"per_entry_worst_send_s={PER_ENTRY_WORST_SEND_S:.1f}")
    print(f"per_entry_fast_send_s={PER_ENTRY_FAST_SEND_S:.2f}")
    fast_batch_8 = QUEUE_SLOTS_AUDITED * PER_ENTRY_FAST_SEND_S
    print(f"fast_batch_8_s={fast_batch_8:.2f}")
    print("saturation_rule: arrival_rate_gt_sustained_drain_or_full_queue_gives_BUSY_drop")
    print()
    print("board LittleFS/NVS/PSRAM headroom: NOT_RUN (no device in this study)")


if __name__ == "__main__":
    main()
