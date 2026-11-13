#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define GOP_FRAMES 30u
#define JOIN_LIMIT_MS 2000u

static int s_pending_force;
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

static void force_ioctl(void)
{
    __atomic_store_n(&s_pending_force, 1, __ATOMIC_RELEASE);
}

static bool encode_sample(uint32_t frame_index)
{
    int forced = __atomic_exchange_n(&s_pending_force, 0, __ATOMIC_ACQ_REL);
    return forced != 0 || frame_index % GOP_FRAMES == 0u;
}

static void encode_complete(bool success)
{
    if (!success) {
        __atomic_store_n(&s_pending_force, 1, __ATOMIC_RELEASE);
    }
}

static bool request_join_idr(uint64_t now, uint64_t *last, uint32_t *requests)
{
    if (*last != 0u && now - *last < JOIN_LIMIT_MS) {
        return false;
    }
    *last = now;
    (*requests)++;
    force_ioctl();
    return true;
}

static void test_request_before_sample(void)
{
    bool first;
    bool second;
    s_pending_force = 0;
    force_ioctl();
    first = encode_sample(7u);
    encode_complete(true);
    second = encode_sample(8u);
    encode_complete(true);
    check(first && !second, "request-before-sample-consumed-once");
}

static void test_request_during_encode(void)
{
    bool current;
    bool next;
    bool after;
    s_pending_force = 0;
    current = encode_sample(7u);
    force_ioctl();
    encode_complete(true);
    next = encode_sample(8u);
    encode_complete(true);
    after = encode_sample(9u);
    encode_complete(true);
    check(!current && next && !after, "inflight-p-frame-preserves-next-idr");
}

static void test_request_after_completion(void)
{
    bool before;
    bool next;
    s_pending_force = 0;
    before = encode_sample(7u);
    encode_complete(true);
    force_ioctl();
    next = encode_sample(8u);
    encode_complete(true);
    check(!before && next, "post-completion-request-applies-next-frame");
}

static void test_error_recovery_and_gop(void)
{
    bool failed_frame;
    bool recovered;
    bool normal_gop;
    s_pending_force = 0;
    failed_frame = encode_sample(7u);
    encode_complete(false);
    recovered = encode_sample(8u);
    encode_complete(true);
    normal_gop = encode_sample(30u);
    encode_complete(true);
    check(!failed_frame && recovered && normal_gop,
          "error-recovery-and-normal-gop");
}

static void test_join_rate_limit(void)
{
    uint64_t last = 0u;
    uint32_t requests = 0u;
    bool first = request_join_idr(100u, &last, &requests);
    bool early = request_join_idr(1100u, &last, &requests);
    bool boundary = request_join_idr(2099u, &last, &requests);
    bool eligible = request_join_idr(2100u, &last, &requests);
    check(first && !early && !boundary && eligible && requests == 2u,
          "join-idr-rate-limited-to-two-seconds");
}

int main(void)
{
    test_request_before_sample();
    test_request_during_encode();
    test_request_after_completion();
    test_error_recovery_and_gop();
    test_join_rate_limit();
    printf("enc_force_idr: total=%u fail=%u\n", s_total, s_failures);
    return s_failures == 0u ? 0 : 1;
}
