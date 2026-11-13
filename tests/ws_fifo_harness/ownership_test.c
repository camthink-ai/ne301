#define _GNU_SOURCE
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIFO_SLOTS 5u
#define SLOT_SIZE 64u
#define FRAME_CAP 256u
#define FRAME_HEADER_SIZE 4u

typedef struct {
    uint8_t *data;
    uint32_t size;
    bool heap;
    bool key;
} slot_t;

static uint8_t s_mem[FIFO_SLOTS][SLOT_SIZE];
static slot_t s_slots[FIFO_SLOTS];
static uint32_t s_head;
static uint32_t s_tail;
static uint32_t s_count;
static uint32_t s_drops;
static bool s_skip_until_key;
static bool s_alloc_fail;
static pthread_mutex_t s_fifo_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_server_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t s_total;
static uint32_t s_failures;

static void check(bool ok, const char *name)
{
    s_total++;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        s_failures++;
    }
}

static void reset_fifo(void)
{
    uint32_t i;
    for (i = 0u; i < FIFO_SLOTS; i++) {
        if (s_slots[i].heap) {
            free(s_slots[i].data);
        }
    }
    memset(s_slots, 0, sizeof(s_slots));
    memset(s_mem, 0, sizeof(s_mem));
    s_head = 0u;
    s_tail = 0u;
    s_count = 0u;
    s_drops = 0u;
    s_skip_until_key = false;
    s_alloc_fail = false;
}

static bool push_frame(const uint8_t *data, uint32_t size, bool key)
{
    slot_t *slot;
    uint8_t *dst;
    bool heap = false;

    pthread_mutex_lock(&s_fifo_lock);
    if (s_skip_until_key && !key) {
        s_drops++;
        pthread_mutex_unlock(&s_fifo_lock);
        return false;
    }
    if (key) {
        s_skip_until_key = false;
    }
    if (s_count == FIFO_SLOTS) {
        s_drops++;
        s_skip_until_key = true;
        pthread_mutex_unlock(&s_fifo_lock);
        return false;
    }
    slot = &s_slots[s_head];
    dst = s_mem[s_head];
    if (size > SLOT_SIZE) {
        dst = s_alloc_fail ? NULL : malloc(size);
        if (dst == NULL) {
            s_drops++;
            s_skip_until_key = true;
            pthread_mutex_unlock(&s_fifo_lock);
            return false;
        }
        heap = true;
    }
    memcpy(dst, data, size);
    slot->data = dst;
    slot->size = size;
    slot->heap = heap;
    slot->key = key;
    s_head = (s_head + 1u) % FIFO_SLOTS;
    s_count++;
    pthread_mutex_unlock(&s_fifo_lock);
    return true;
}

static slot_t pop_frame(bool reserve_until_send)
{
    slot_t frame;
    pthread_mutex_lock(&s_fifo_lock);
    frame = s_slots[s_tail];
    s_tail = (s_tail + 1u) % FIFO_SLOTS;
    if (!reserve_until_send) {
        s_count--;
    }
    pthread_mutex_unlock(&s_fifo_lock);
    return frame;
}

static void release_frame(slot_t *frame, bool reserve_until_send)
{
    uint32_t i;
    pthread_mutex_lock(&s_fifo_lock);
    if (reserve_until_send) {
        s_count--;
    }
    pthread_mutex_unlock(&s_fifo_lock);
    if (frame->heap) {
        free(frame->data);
        frame->heap = false;
        for (i = 0u; i < FIFO_SLOTS; i++) {
            if (s_slots[i].data == frame->data) {
                s_slots[i].heap = false;
            }
        }
    }
}

static bool send_lock_free(void)
{
    bool free_locks = true;
    if (pthread_mutex_trylock(&s_fifo_lock) != 0) {
        free_locks = false;
    } else {
        pthread_mutex_unlock(&s_fifo_lock);
    }
    if (pthread_mutex_trylock(&s_server_lock) != 0) {
        free_locks = false;
    } else {
        pthread_mutex_unlock(&s_server_lock);
    }
    return free_locks;
}

static bool send_frame(const uint8_t *data, uint32_t size,
                       const uint8_t *expected, uint32_t expected_size)
{
    return send_lock_free() && size == expected_size &&
           memcmp(data, expected, expected_size) == 0;
}

static uint32_t build_join(const uint8_t *data, uint32_t size,
                           uint8_t *out, uint32_t cap)
{
    static const uint8_t parameters[8] = { 0, 0, 0, 1, 0, 0, 0, 1 };
    if (size < FRAME_HEADER_SIZE || size + sizeof(parameters) > cap) {
        return 0u;
    }
    memcpy(out, data, FRAME_HEADER_SIZE);
    memcpy(out + FRAME_HEADER_SIZE, parameters, sizeof(parameters));
    memcpy(out + FRAME_HEADER_SIZE + sizeof(parameters),
           data + FRAME_HEADER_SIZE, size - FRAME_HEADER_SIZE);
    return size + sizeof(parameters);
}

static void make_frame(uint8_t *out, uint32_t size, uint8_t tag)
{
    memset(out, tag, size);
    out[0] = tag;
}

static bool run_full_refill(bool reserve_until_send)
{
    uint8_t frame_data[FRAME_CAP];
    uint8_t expected[FRAME_CAP];
    uint8_t expected_join[FRAME_CAP];
    uint8_t join[FRAME_CAP];
    static const uint8_t parameters[8] = { 0, 0, 0, 1, 0, 0, 0, 1 };
    uint32_t expected_size = 12u;
    uint32_t join_size;
    slot_t frame;
    bool client_a_ok;
    bool client_b_ok;
    uint32_t i;

    reset_fifo();
    make_frame(expected, expected_size, 1u);
    for (i = 1u; i <= FIFO_SLOTS; i++) {
        uint32_t size = 11u + i;
        make_frame(frame_data, size, (uint8_t)i);
        push_frame(frame_data, size, i == 1u);
    }
    frame = pop_frame(reserve_until_send);
    make_frame(frame_data, 31u, 6u);
    push_frame(frame_data, 31u, false);
    memcpy(expected_join, expected, FRAME_HEADER_SIZE);
    memcpy(expected_join + FRAME_HEADER_SIZE, parameters, sizeof(parameters));
    memcpy(expected_join + FRAME_HEADER_SIZE + sizeof(parameters),
           expected + FRAME_HEADER_SIZE, expected_size - FRAME_HEADER_SIZE);
    join_size = build_join(frame.data, frame.size, join, sizeof(join));
    client_a_ok = send_frame(frame.data, frame.size, expected, expected_size);
    client_b_ok = join_size == expected_size + sizeof(parameters) &&
                  send_frame(join, join_size, expected_join,
                             expected_size + sizeof(parameters));
    release_frame(&frame, reserve_until_send);
    if (!reserve_until_send) {
        check(!client_a_ok, "baseline-mg-ws-send-corruption-reproduced");
        check(!client_b_ok, "baseline-join-frame-corruption-reproduced");
        return !client_a_ok && !client_b_ok;
    }
    check(client_a_ok, "reserved-slot-mg-ws-send");
    check(client_b_ok, "reserved-slot-join-frame");
    check(s_count == FIFO_SLOTS - 1u && s_drops == 1u && s_skip_until_key,
          "full-queue-bounded-drop");
    make_frame(frame_data, 12u, 7u);
    check(!push_frame(frame_data, 12u, false), "drop-until-keyframe");
    make_frame(frame_data, 13u, 8u);
    check(push_frame(frame_data, 13u, true), "ring-wrap-keyframe-refill");
    check(s_count == FIFO_SLOTS, "ring-wrap-capacity");
    return true;
}

static void test_ring_drain(void)
{
    uint32_t i;
    for (i = 0u; i < FIFO_SLOTS; i++) {
        slot_t frame = pop_frame(true);
        release_frame(&frame, true);
    }
    check(s_count == 0u, "ring-drain-empty");
}

static void test_heap_lifetime_and_failure(void)
{
    uint8_t large[100];
    uint8_t small[12];
    slot_t frame;
    bool stable;

    reset_fifo();
    make_frame(large, sizeof(large), 9u);
    make_frame(small, sizeof(small), 10u);
    check(push_frame(large, sizeof(large), true), "oversize-heap-accepted");
    frame = pop_frame(true);
    check(push_frame(small, sizeof(small), false), "heap-neighbor-push");
    stable = send_frame(frame.data, frame.size, large, sizeof(large));
    check(stable, "oversize-heap-lifetime");
    release_frame(&frame, true);

    reset_fifo();
    s_alloc_fail = true;
    check(!push_frame(large, sizeof(large), true), "oversize-allocation-failure");
    check(s_drops == 1u && s_skip_until_key, "allocation-failure-resync");
    check(!push_frame(small, sizeof(small), false), "allocation-failure-backpressure");
    make_frame(small, sizeof(small), 11u);
    check(push_frame(small, sizeof(small), true), "allocation-failure-next-key");
}

int main(void)
{
    bool baseline_reproduced;
    bool reserved_stable;
    uint8_t frame[12];
    uint32_t i;
    bool bounded = true;

    reset_fifo();
    baseline_reproduced = run_full_refill(false);
    check(baseline_reproduced, "baseline-overwrite-reproduced");
    reserved_stable = run_full_refill(true);
    check(reserved_stable, "reserved-frame-stable");
    test_ring_drain();
    test_heap_lifetime_and_failure();
    reset_fifo();
    make_frame(frame, sizeof(frame), 1u);
    for (i = 0u; i < 1000u; i++) {
        make_frame(frame, sizeof(frame), (uint8_t)i);
        push_frame(frame, sizeof(frame), i == 0u || i % 25u == 0u);
        bounded = bounded && s_count <= FIFO_SLOTS;
    }
    check(bounded, "bounded-fifo-occupancy");
    check(s_drops > 0u, "bounded-fifo-overflow-counted");
    printf("ws_fifo_ownership: total=%u fail=%u\n", s_total, s_failures);
    return s_failures == 0u ? 0 : 1;
}
