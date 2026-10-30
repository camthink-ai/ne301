
#include "lc_lvn4.h"
#include <string.h>

typedef char lc_lvn4_assert_header_size[(LC_LVN4_HEADER_SIZE == 48u) ? 1 : -1];
typedef char lc_lvn4_assert_event_batch[(LC_LVN4_EVENT_BATCH_MAX_LEN == 304u) ? 1 : -1];
typedef char lc_lvn4_assert_pkt_max[(LC_LVN4_PKT_MAX == 9008u) ? 1 : -1];
typedef char lc_lvn4_assert_track_record[(LC_LVN4_TRACK_HEADER_SIZE == 12u) ? 1 : -1];
typedef char lc_lvn4_assert_point[(LC_LVN4_TRACK_POINT_SIZE == 8u) ? 1 : -1];

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint8_t q8_from_f(float v)
{
    if (!(v > 0.0f)) return 0u;
    if (v >= 1.0f) return 255u;
    uint32_t q = (uint32_t)(v * 255.0f + 0.5f);
    if (q > 255u) q = 255u;
    return (uint8_t)q;
}

static uint16_t q16_from_f(float v)
{
    if (!(v > 0.0f)) return 0u;
    if (v >= 1.0f) return 65535u;
    uint32_t q = (uint32_t)(v * 65535.0f + 0.5f);
    if (q > 65535u) q = 65535u;
    return (uint16_t)q;
}

static uint8_t ring_k(const lcbus_t *b)
{
    uint8_t k = b->cfg.track_history_k;
    if (k < 4u) k = 4u;
    if (k > 16u) k = 16u;
    return k;
}

static uint32_t bus_config_generation(const lc_lvn4_t *p)
{
    if (!p->bus) return 0u;
    return p->bus->config_generation;
}

static void watch_binding(lc_lvn4_t *p)
{
    const lcbus_t *b = p->bus;
    uint32_t mg = 0u;
    uint32_t cg = 0u;
    int32_t ti = -1;
    if (b && b->binding.bound && b->model_state == LCBUS_MSTATE_RUNNING) {
        mg = b->binding.model_generation;
        cg = b->binding.class_generation;
        ti = b->binding.target_index;
    }
    if (mg != p->bound_model_gen || cg != p->bound_class_gen || ti != p->bound_target_index) {
        p->bound_model_gen = mg;
        p->bound_class_gen = cg;
        p->bound_target_index = ti;
        p->resync_required = 1;
    }
}

static void exhaust(lc_lvn4_t *p)
{
    p->state = LC_LVN4_STATE_UNAVAILABLE_EXHAUSTED;
}

static int add_gap(lc_lvn4_t *p, uint32_t n)
{
    if (n == 0u) return 1;
    if (p->cumulative_gap > 0xFFFFFFFFu - n) {
        exhaust(p);
        return 0;
    }
    p->cumulative_gap += n;
    return 1;
}

static int account(lc_lvn4_t *p, uint32_t delta, uint32_t published)
{
    if (delta == 0u) return 1;
    if (p->through_seq > 0xFFFFFFFFu - delta) {
        exhaust(p);
        return 0;
    }
    if (p->cumulative_total > 0xFFFFFFFFu - delta) {
        exhaust(p);
        return 0;
    }
    p->through_seq += delta;
    p->cumulative_total += delta;
    if (!add_gap(p, delta - published)) return 0;
    return 1;
}

static void fill_header(uint8_t *h, uint16_t kind, uint32_t total_len,
                        const lc_lvn4_t *p, uint32_t ts_ms,
                        uint32_t config_gen, uint32_t record_count, uint32_t flags)
{
    h[LC_LVN4_OFF_MAGIC + 0] = LC_LVN4_MAGIC_0;
    h[LC_LVN4_OFF_MAGIC + 1] = LC_LVN4_MAGIC_1;
    h[LC_LVN4_OFF_MAGIC + 2] = LC_LVN4_MAGIC_2;
    h[LC_LVN4_OFF_MAGIC + 3] = LC_LVN4_MAGIC_3;
    wr16(h + LC_LVN4_OFF_HEADER_VERSION, LC_LVN4_HEADER_VERSION);
    wr16(h + LC_LVN4_OFF_KIND, kind);
    wr32(h + LC_LVN4_OFF_TOTAL_LEN, total_len);
    wr32(h + LC_LVN4_OFF_PACKET_SEQ, p->packet_seq);
    wr32(h + LC_LVN4_OFF_EVENT_THROUGH_SEQ, p->through_seq);
    wr32(h + LC_LVN4_OFF_CUMULATIVE_TOTAL, p->cumulative_total);
    wr32(h + LC_LVN4_OFF_CUMULATIVE_GAP, p->cumulative_gap);
    wr32(h + LC_LVN4_OFF_SOURCE_TS_MS, ts_ms);
    wr32(h + LC_LVN4_OFF_CONFIG_GENERATION, config_gen);
    wr32(h + LC_LVN4_OFF_RECORD_COUNT, record_count);
    wr32(h + LC_LVN4_OFF_FLAGS, flags);
    wr32(h + LC_LVN4_OFF_RESERVED, 0u);
}

static uint32_t current_flags(const lc_lvn4_t *p)
{
    uint32_t flags = 0u;
    if (p->cumulative_gap != p->last_pub_gap) flags |= LC_LVN4_FLAG_SOURCE_GAP_DELTA;
    if (p->loss_unknown_latch) flags |= LC_LVN4_FLAG_SOURCE_LOSS_UNKNOWN;
    if (p->resync_required) flags |= LC_LVN4_FLAG_RESYNC_REQUIRED;
    return flags;
}

static int take_packet_seq(lc_lvn4_t *p)
{
    if (p->packet_seq == 0xFFFFFFFFu) {
        exhaust(p);
        return 0;
    }
    p->packet_seq += 1u;
    return 1;
}

static int publish_packet(lc_lvn4_t *p, uint32_t len)
{
    int32_t rc = p->publish(p->publish_user, p->pkt, len);
    if (rc != 0) {
        p->st.publish_failures++;
        p->state = LC_LVN4_STATE_UNAVAILABLE_PUBLISH;
        p->loss_unknown_latch = 1u;
        p->resync_required = 1u;
        return 0;
    }
    p->st.publish_ok++;
    p->last_pub_gap = p->cumulative_gap;
    p->resync_required = 0;
    return 1;
}

static void publish_event_batch(lc_lvn4_t *p, const lcbus_live_facts_t *f,
                                uint8_t n_enc, uint32_t delta)
{
    uint32_t gen = bus_config_generation(p);
    if (gen == 0u) {
        p->state = LC_LVN4_STATE_UNAVAILABLE_CONFIG_GEN;
        return;
    }
    if (!take_packet_seq(p)) return;

    uint32_t total_len = LC_LVN4_HEADER_SIZE + (uint32_t)n_enc * LC_LVN4_EVENT_RECORD_SIZE;
    uint32_t flags = current_flags(p);
    fill_header(p->pkt, LC_LVN4_KIND_EVENT_BATCH, total_len, p, f->mono_ms,
                gen, (uint32_t)n_enc, flags);

    uint32_t base_seq = p->through_seq - delta;
    uint8_t i;
    for (i = 0; i < n_enc; i++) {
        uint8_t *r = p->pkt + LC_LVN4_HEADER_SIZE + (uint32_t)i * LC_LVN4_EVENT_RECORD_SIZE;
        wr32(r + 0, base_seq + (uint32_t)i + 1u);
        wr32(r + 4, f->evts[i].track_id);
        wr32(r + 8, f->evts[i].ts_ms);
        r[12] = (f->evts[i].direction == LC_CROSS_IN) ? (uint8_t)LC_LVN4_DIR_IN
                                                      : (uint8_t)LC_LVN4_DIR_OUT;
        r[13] = 0u;
        r[14] = q8_from_f(f->evts[i].pos.x);
        r[15] = q8_from_f(f->evts[i].pos.y);
    }

    if (publish_packet(p, total_len)) {
        p->st.packets_event++;
        p->st.encoded_events += n_enc;
    } else {
        if (!add_gap(p, n_enc)) return;
    }
}

typedef struct {
    lc_lvn4_t  *p;
    uint8_t    *cursor;
    uint32_t    remaining;
    uint32_t    count;
    uint8_t     overflow;
} track_enc_ctx_t;

static void encode_track(const lc_track_t *trk, void *user)
{
    track_enc_ctx_t *c = (track_enc_ctx_t *)user;
    lc_lvn4_t *p = c->p;
    uint8_t k = ring_k(p->bus);
    uint8_t used = trk->history_used;
    if (used > LC_LVN4_TRACK_POINTS_MAX) used = LC_LVN4_TRACK_POINTS_MAX;

    uint32_t need = LC_LVN4_TRACK_HEADER_SIZE + (uint32_t)used * LC_LVN4_TRACK_POINT_SIZE;
    if (need > c->remaining) {
        c->overflow = 1;
        return;
    }

    uint8_t *r = c->cursor;
    uint16_t class_index = LC_LVN4_CLASS_UNKNOWN;
    const lcbus_t *b = p->bus;
    if (b && b->binding.bound && b->binding.target_index >= 0 &&
        (uint32_t)b->binding.target_index < b->binding.class_count) {
        class_index = (uint16_t)b->binding.target_index;
    }

    wr32(r + 0, trk->id);
    wr16(r + 4, (uint16_t)used);
    wr16(r + 6, class_index);
    r[8] = LC_LVN4_TRACK_STATE_ACTIVE;
    r[9] = 0u;
    wr16(r + 10, 0u);

    uint8_t *pt = r + LC_LVN4_TRACK_HEADER_SIZE;
    uint8_t i;
    for (i = 0; i < used; i++) {
        uint8_t slot = (uint8_t)((trk->history_head + k - used + i) % k);
        wr32(pt + 0, trk->history_ts[slot]);
        wr16(pt + 4, q16_from_f(trk->history[slot].x));
        wr16(pt + 6, q16_from_f(trk->history[slot].y));
        pt += LC_LVN4_TRACK_POINT_SIZE;
    }

    c->cursor = r + need;
    c->remaining -= need;
    c->count++;
}

static void publish_tracks_snapshot(lc_lvn4_t *p, const lcbus_live_facts_t *f)
{
    if (p->state != LC_LVN4_STATE_ARMED) return;

    uint32_t gen = bus_config_generation(p);
    if (gen == 0u) {
        p->state = LC_LVN4_STATE_UNAVAILABLE_CONFIG_GEN;
        return;
    }
    if (!take_packet_seq(p)) return;

    track_enc_ctx_t c;
    c.p = p;
    c.cursor = p->pkt + LC_LVN4_HEADER_SIZE;
    c.remaining = p->live_publish_max - LC_LVN4_HEADER_SIZE;
    c.count = 0u;
    c.overflow = 0;

    if (p->bus) {
        lc_tracker_for_each_active(p->bus->tracker, encode_track, &c);
    }

    if (c.overflow) {
        p->state = LC_LVN4_STATE_UNAVAILABLE_TRACK_CAPACITY;
        p->packet_seq -= 1u;
        return;
    }

    uint32_t encoded = (uint32_t)(c.cursor - (p->pkt + LC_LVN4_HEADER_SIZE));
    uint32_t total_len = LC_LVN4_HEADER_SIZE + encoded;
    uint32_t flags = current_flags(p);
    fill_header(p->pkt, LC_LVN4_KIND_TRACKS, total_len, p, f->mono_ms,
                gen, c.count, flags);

    if (publish_packet(p, total_len)) {
        p->st.packets_tracks++;
        p->st.track_snapshots++;
        if (c.count == 0u) p->st.track_cleared++;
    }
}

void lc_lvn4_init(lc_lvn4_t *p, lcbus_t *bus,
                  lc_lvn4_publish_fn publish, void *publish_user)
{
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->bus = bus;
    p->publish = publish;
    p->publish_user = publish_user;
    p->state = LC_LVN4_STATE_UNARMED;
    p->gate = LC_LVN4_GATE_UNKNOWN;
    p->bound_target_index = -1;
}

int lc_lvn4_arm(lc_lvn4_t *p, const lc_lvn4_profile_t *profile)
{
    if (!p) return -1;
    if (!p->publish || !p->bus) {
        p->state = LC_LVN4_STATE_UNAVAILABLE_NO_CAPABILITY;
        return -1;
    }
    if (!profile || profile->entry_epoch == 0u ||
        profile->track_max == 0u || profile->track_max > LC_LVN4_TRACKS_MAX) {
        p->state = LC_LVN4_STATE_UNAVAILABLE_BAD_PROFILE;
        return -1;
    }
    uint64_t closure = (uint64_t)LC_LVN4_HEADER_SIZE +
                       (uint64_t)profile->track_max *
                           (LC_LVN4_TRACK_HEADER_SIZE +
                            (uint64_t)LC_LVN4_TRACK_POINTS_MAX * LC_LVN4_TRACK_POINT_SIZE);
    if (profile->publish_max < LC_LVN4_MIN_PUBLISH_MAX ||
        (uint64_t)profile->publish_max < closure ||
        profile->publish_max > LC_LVN4_PKT_MAX) {
        p->state = LC_LVN4_STATE_UNAVAILABLE_BAD_PROFILE;
        return -1;
    }
    if (p->armed_ever && profile->entry_epoch == p->entry_epoch) {
        p->state = LC_LVN4_STATE_UNAVAILABLE_ENTRY_REUSED;
        return -1;
    }

    p->entry_epoch = profile->entry_epoch;
    p->live_publish_max = profile->publish_max;
    p->live_track_max = profile->track_max;
    p->packet_seq = 0u;
    p->through_seq = 0u;
    p->cumulative_total = 0u;
    p->cumulative_gap = 0u;
    p->last_pub_gap = 0u;
    p->loss_unknown_latch = 0u;
    p->resync_required = 1u;
    p->bound_model_gen = 0u;
    p->bound_class_gen = 0u;
    p->bound_target_index = -1;
    memset(&p->st, 0, sizeof(p->st));
    watch_binding(p);
    p->resync_required = 1u;
    p->armed_ever = 1u;
    p->state = LC_LVN4_STATE_ARMED;
    return 0;
}

void lc_lvn4_disarm(lc_lvn4_t *p)
{
    if (!p) return;
    p->state = LC_LVN4_STATE_DISARMED;
}

void lc_lvn4_set_gate(lc_lvn4_t *p, lc_lvn4_gate_t gate)
{
    if (!p) return;
    if (gate != LC_LVN4_GATE_OPEN && p->gate == LC_LVN4_GATE_OPEN) {
        if (gate == LC_LVN4_GATE_UNKNOWN) p->loss_unknown_latch = 1u;
    }
    if (gate == LC_LVN4_GATE_OPEN && p->gate != LC_LVN4_GATE_OPEN) {
        p->resync_required = 1u;
    }
    p->gate = gate;
}

void lc_lvn4_observe(lc_lvn4_t *p, const lcbus_live_facts_t *f)
{
    if (!p || !f) return;
    watch_binding(p);
    if (p->state != LC_LVN4_STATE_ARMED) return;
    if (!f->kind_frame && !f->is_gap) return;

    if (f->is_gap) {
        p->loss_unknown_latch = 1u;
        p->st.gap_events++;
        return;
    }

    if (f->frame_dropped) p->loss_unknown_latch = 1u;

    if (p->bus && lc_tracker_active_count(p->bus->tracker) > p->live_track_max) {
        p->state = LC_LVN4_STATE_UNAVAILABLE_TRACK_CAPACITY;
        return;
    }

    uint32_t delta = f->frame_in + f->frame_out;
    uint8_t n_enc = f->n_evts;
    if (n_enc > LC_LVN4_EVENT_MAX) n_enc = LC_LVN4_EVENT_MAX;
    if ((uint32_t)n_enc > delta) n_enc = (uint8_t)delta;

    if (p->gate != LC_LVN4_GATE_OPEN) {
        if (delta > 0u) {
            if (!account(p, delta, 0u)) return;
            p->st.gate_held_events += delta;
        }
        return;
    }

    if (!account(p, delta, (uint32_t)n_enc)) return;
    if (delta > (uint32_t)n_enc) p->st.truncated_events += delta - (uint32_t)n_enc;

    publish_event_batch(p, f, n_enc, delta);
    publish_tracks_snapshot(p, f);
}

lc_lvn4_state_t lc_lvn4_state(const lc_lvn4_t *p)
{
    return p ? p->state : LC_LVN4_STATE_UNARMED;
}

const lc_lvn4_stats_t *lc_lvn4_stats(const lc_lvn4_t *p)
{
    return p ? &p->st : NULL;
}
