// scan_coordinator — the cross-thread hand-off between the native decode worker and
// the Python consumer (the app's scan_consumer.run_scan). Python-free, pthread-safe.
// The Pi analog of esp-board-common's scan_coordinator: a NEW ring of unique decoded
// payloads + a coalesced status cell.
//
// Producer (engine decode worker): scan_coord_on_frame() once per decoded frame.
// Consumer (pump/Python thread via the camera_scanner binding): scan_coord_poll_new()
// drains the NEW ring; scan_coord_read_status() reads the coalesced status.
//
// Frame-status vocabulary matches camera_scanner FRAME_* / DecodeQRStatus dot codes:
//   0 NONE (nothing decoded)  1 NEW (fresh payload -> ring)  2 REPEAT (recently seen)
//   3 MISS (located but undecoded — not produced yet on the Pi; zbar exposes no
//     locate-without-decode pass, so consecutive_misses stays 0. Deferred.)
#ifndef SS_SCAN_COORDINATOR_H
#define SS_SCAN_COORDINATOR_H

#include <cstddef>
#include <cstdint>
#include <vector>

enum {
    SCAN_FRAME_NONE   = 0,
    SCAN_FRAME_NEW    = 1,
    SCAN_FRAME_REPEAT = 2,
    SCAN_FRAME_MISS   = 3,
};

struct ScanStatus {
    int      latest;              // last frame's FRAME_* classification
    uint32_t consecutive_misses;  // MISS run length (always 0 on the Pi for now)
    uint32_t dropped_new;         // NEW payloads dropped on ring overflow (lost progress)
    bool     has_corners;         // reserved; always false on the Pi
};

// Clear the ring + all counters. Called at scan start.
void scan_coord_reset();

// One decoded frame's outcome (decode worker thread). payload==nullptr/len==0 means
// "nothing decoded this frame" (NONE). A payload is deduped against a small recent-set:
// unseen -> NEW (pushed to the ring), seen -> REPEAT. Updates the status cell.
void scan_coord_on_frame(const uint8_t *payload, size_t len);

// Drain one NEW payload into out (consumer thread). Returns false when the ring is
// empty. The payload is copied into out; the ring slot is freed.
bool scan_coord_poll_new(std::vector<uint8_t> &out);

// Snapshot the coalesced status (consumer thread).
void scan_coord_read_status(ScanStatus *st);

#endif  // SS_SCAN_COORDINATOR_H
