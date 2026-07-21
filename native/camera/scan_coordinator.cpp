// scan_coordinator — see scan_coordinator.h. One mutex guards the ring + status; the
// decode worker holds it only to classify + push, the consumer only to pop + read.
#include "scan_coordinator.h"

#include <cstring>
#include <deque>
#include <mutex>

namespace {

constexpr size_t kRingCap = 24;   // unique NEW payloads awaiting the consumer

std::mutex                         s_mtx;
std::deque<std::vector<uint8_t>>   s_ring;         // NEW payloads, FIFO
std::vector<uint8_t>               s_last_fwd;      // last forwarded payload (consecutive-dedup)
bool                               s_have_last_fwd = false;
ScanStatus                         s_status = {SCAN_FRAME_NONE, 0, 0, false};

}  // namespace

void scan_coord_reset() {
    std::lock_guard<std::mutex> lk(s_mtx);
    s_ring.clear();
    s_last_fwd.clear();
    s_have_last_fwd = false;
    s_status = {SCAN_FRAME_NONE, 0, 0, false};
}

void scan_coord_on_frame(const uint8_t *payload, size_t len) {
    std::lock_guard<std::mutex> lk(s_mtx);

    if (!payload || len == 0) {
        s_status.latest = SCAN_FRAME_NONE;
        // No MISS detection on the Pi yet: a non-decode does not increment
        // consecutive_misses (it stays 0), so the consumer's sustained-MISS warning
        // simply never fires — graceful degradation vs the ESP quirc locate pass.
        return;
    }

    // Consecutive-only dedup (mirrors esp-board-common's scan_coordinator last_fwd):
    // REPEAT iff byte-identical to the last forwarded payload — a QR held still on one
    // part. A cycled-back part differs from the previous frame, so it flows through as
    // NEW and the consumer's DecodeQR reclassifies it (PART_EXISTING) with a real index.
    // Set-membership would suppress those index-bearing domain-repeats — wrong layer.
    bool same = s_have_last_fwd && len == s_last_fwd.size() &&
                memcmp(payload, s_last_fwd.data(), len) == 0;
    if (same) {
        s_status.latest = SCAN_FRAME_REPEAT;
        return;
    }

    // Fresh vs the last frame: record it and push to the NEW ring.
    s_last_fwd.assign(payload, payload + len);
    s_have_last_fwd = true;
    if (s_ring.size() >= kRingCap) {
        s_ring.pop_front();          // drop oldest un-drained NEW = lost progress
        s_status.dropped_new++;
    }
    s_ring.emplace_back(payload, payload + len);
    s_status.latest = SCAN_FRAME_NEW;
}

bool scan_coord_poll_new(std::vector<uint8_t> &out) {
    std::lock_guard<std::mutex> lk(s_mtx);
    if (s_ring.empty()) {
        return false;
    }
    out.swap(s_ring.front());
    s_ring.pop_front();
    return true;
}

void scan_coord_read_status(ScanStatus *st) {
    if (!st) {
        return;
    }
    std::lock_guard<std::mutex> lk(s_mtx);
    *st = s_status;
}
