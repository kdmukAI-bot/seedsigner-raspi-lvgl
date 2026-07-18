// scan_coordinator — see scan_coordinator.h. One mutex guards the ring + status; the
// decode worker holds it only to classify + push, the consumer only to pop + read.
#include "scan_coordinator.h"

#include <cstring>
#include <deque>
#include <mutex>

namespace {

constexpr size_t kRingCap = 24;   // unique NEW payloads awaiting the consumer
constexpr size_t kSeenCap = 16;   // recent payload hashes for NEW/REPEAT dedup

std::mutex                         s_mtx;
std::deque<std::vector<uint8_t>>   s_ring;      // NEW payloads, FIFO
std::deque<uint64_t>               s_seen;      // recent payload hashes (dedup window)
ScanStatus                         s_status = {SCAN_FRAME_NONE, 0, 0, false};

// FNV-1a over the payload — a cheap dedup key. The consumer's DecodeQR is the
// authoritative dedup; this only keeps the same held QR from flooding the ring.
uint64_t hash_payload(const uint8_t *p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

bool seen_recently(uint64_t h) {
    for (uint64_t v : s_seen) {
        if (v == h) {
            return true;
        }
    }
    return false;
}

}  // namespace

void scan_coord_reset() {
    std::lock_guard<std::mutex> lk(s_mtx);
    s_ring.clear();
    s_seen.clear();
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

    uint64_t h = hash_payload(payload, len);
    if (seen_recently(h)) {
        s_status.latest = SCAN_FRAME_REPEAT;
        return;
    }

    // Fresh payload: record it in the dedup window and push to the NEW ring.
    s_seen.push_back(h);
    if (s_seen.size() > kSeenCap) {
        s_seen.pop_front();
    }
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
