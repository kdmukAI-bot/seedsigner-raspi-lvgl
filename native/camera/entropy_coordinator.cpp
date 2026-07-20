// entropy_coordinator — see entropy_coordinator.h. One mutex guards all state; the
// producer (engine blit worker) and consumer (Python thread) are the only actors.
#include "entropy_coordinator.h"

#include "secure_zero.h"
#include "sha256_min.h"

#include <mutex>
#include <vector>

namespace {

struct EntropyState {
    std::mutex           mtx;
    ss_sha256_ctx        chain;               // running digest (seed + preview frames)
    uint32_t             frames    = 0;       // preview frames chained
    bool                 armed     = false;   // capture requested, awaiting latch
    bool                 captured  = false;   // final frame latched
    uint8_t              digest[SS_SHA256_DIGEST_LEN] = {0};  // frozen chain (at latch)
    std::vector<uint8_t> latch;               // latched RGB565 final frame
    int                  latch_w   = 0;       // its display-orientation dims
    int                  latch_h   = 0;
};

EntropyState g;

// Scrub the latched frame before returning it to the allocator, then release it.
// swap-to-empty alone would free the pixels intact, leaving a scene image that is a
// direct seed input sitting in freed heap for the life of the process (g is static).
// Caller must hold g.mtx.
void scrub_latch() {
    if (!g.latch.empty()) {
        ss_secure_zero(g.latch.data(), g.latch.size());
    }
    std::vector<uint8_t>().swap(g.latch);
    g.latch_w = 0;
    g.latch_h = 0;
}

}  // namespace

void entropy_coord_reset(const uint8_t *seed, size_t seed_len) {
    std::lock_guard<std::mutex> lk(g.mtx);
    ss_sha256_init(&g.chain);
    if (seed && seed_len == 32) {
        ss_sha256_update(&g.chain, seed, 32);
    }
    g.frames   = 0;
    g.armed    = false;
    g.captured = false;
    ss_secure_zero(g.digest, sizeof(g.digest));
    scrub_latch();
}

void entropy_coord_chain_frame(const uint8_t *rgb565, size_t len) {
    if (!rgb565 || len == 0) {
        return;
    }
    std::lock_guard<std::mutex> lk(g.mtx);
    if (g.armed || g.captured) {
        return;  // chain frozen at capture — the final frame is excluded
    }
    ss_sha256_update(&g.chain, rgb565, len);
    g.frames++;
}

void entropy_coord_arm() {
    std::lock_guard<std::mutex> lk(g.mtx);
    if (!g.captured) {
        g.armed = true;
    }
}

bool entropy_coord_is_armed() {
    std::lock_guard<std::mutex> lk(g.mtx);
    return g.armed && !g.captured;
}

void entropy_coord_latch(const uint8_t *rgb565, size_t len, int w, int h) {
    if (!rgb565 || len == 0 || w <= 0 || h <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lk(g.mtx);
    if (g.captured || !g.armed) {
        return;
    }
    // Scrub-then-release before assign(): every teardown path leaves the vector empty, so
    // in practice this allocates fresh, but assign() into a live buffer would otherwise be
    // free to reallocate and drop the old pixels unscrubbed.
    scrub_latch();
    g.latch.assign(rgb565, rgb565 + len);
    g.latch_w = w;
    g.latch_h = h;
    // Finalize a COPY so the running ctx survives for resume() (reshoot continues chaining).
    ss_sha256_ctx copy = g.chain;
    ss_sha256_final(&copy, g.digest);
    g.captured = true;
    g.armed    = false;
}

bool entropy_coord_get_result(const uint8_t **chain, size_t *chain_len,
                              const uint8_t **frame, size_t *frame_len,
                              int *frame_w, int *frame_h, uint32_t *n) {
    std::lock_guard<std::mutex> lk(g.mtx);
    if (!g.captured) {
        return false;
    }
    if (chain)     *chain     = g.digest;
    if (chain_len) *chain_len = SS_SHA256_DIGEST_LEN;
    if (frame)     *frame     = g.latch.data();
    if (frame_len) *frame_len = g.latch.size();
    if (frame_w)   *frame_w   = g.latch_w;
    if (frame_h)   *frame_h   = g.latch_h;
    if (n)         *n         = g.frames;
    return true;
}

void entropy_coord_resume() {
    std::lock_guard<std::mutex> lk(g.mtx);
    g.armed    = false;
    g.captured = false;
    ss_secure_zero(g.digest, sizeof(g.digest));
    scrub_latch();
    // g.chain + g.frames retained — chaining continues from where it left off.
}

uint32_t entropy_coord_frames_chained() {
    std::lock_guard<std::mutex> lk(g.mtx);
    return g.frames;
}

void entropy_coord_wipe() {
    std::lock_guard<std::mutex> lk(g.mtx);
    ss_sha256_init(&g.chain);
    g.frames   = 0;
    g.armed    = false;
    g.captured = false;
    ss_secure_zero(g.digest, sizeof(g.digest));
    scrub_latch();
}

