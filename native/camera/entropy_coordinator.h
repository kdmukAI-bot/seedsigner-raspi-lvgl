// entropy_coordinator — the chain+latch state for the native image-entropy flow.
// Python-free, pthread-safe. The Pi analog of the ESP cam_pipeline_entropy consumer:
// a running SHA-256 over preview frames plus a single latched final frame.
//
// Chain semantics (matches the SeedSigner reference so the host finishes the hash
// identically — helpers/mnemonic_generation.generate_mnemonic_from_camera_entropy):
//   h = seed_hash                       (optional 32-byte caller uniqueness seed, else empty)
//   per preview frame: h = SHA256(h ‖ frame_rgb565_bytes)
//   final image: latched RAW and handed to the host; the host computes the entropy as
//                SHA256(chain ‖ final_frame). The final frame is NOT chained here, and the
//                chain digest is frozen at its pre-final value.
//
// Producer (engine blit worker): chain_frame() per preview frame; latch() once, when the
// engine's pinned-exposure final frame is ready. Consumer (Python thread via the
// camera_entropy binding): arm(), get_result(), resume(), frames_chained().
#ifndef SS_ENTROPY_COORDINATOR_H
#define SS_ENTROPY_COORDINATOR_H

#include <cstddef>
#include <cstdint>

// Begin a new session: seed the chain (seed/seed_len, or empty when seed==nullptr /
// seed_len==0), clear the frame counter + latch, unset armed/captured. seed_len must be
// 0 or 32 (32 is the only accepted caller-seed size; other lengths are ignored as empty).
void entropy_coord_reset(const uint8_t *seed, size_t seed_len);

// Chain one preview frame's RGB565 bytes into the running digest (producer). No-op once
// armed or captured — the chain freezes at capture time (the pinned/latched final frame
// is excluded, matching the reference).
void entropy_coord_chain_frame(const uint8_t *rgb565, size_t len);

// Arm capture (consumer, from camera_entropy.capture()): stop chaining and wait for the
// engine to latch the pinned-exposure final frame. Idempotent.
void entropy_coord_arm();

// True once armed and not yet latched — the engine polls this on the blit worker to know
// a latch is pending (so it can pin exposure, let it settle, then latch()).
bool entropy_coord_is_armed();

// Latch the final frame (producer): copy rgb565 (len bytes) as the final image and freeze
// the chain digest at its current value (finalized from a COPY of the ctx, so resume()
// can keep chaining). Marks captured; clears armed. No-op if already captured or not armed.
void entropy_coord_latch(const uint8_t *rgb565, size_t len);

// Snapshot the result (consumer). Returns false until latch() completes. On success sets
// any non-null out: chain -> 32-byte digest, frame -> latched RGB565 bytes, n -> frames
// chained. The chain/frame pointers stay valid until resume() or reset().
bool entropy_coord_get_result(const uint8_t **chain, size_t *chain_len,
                              const uint8_t **frame, size_t *frame_len, uint32_t *n);

// Reshoot (consumer): discard the latch, unset armed/captured, and resume chaining from
// where the digest left off (accumulated preview entropy retained).
void entropy_coord_resume();

// Live count of preview frames chained so far (drives a progress indicator).
uint32_t entropy_coord_frames_chained();

// Zeroize the chain ctx, digest, and latch buffer (called at stop). Idempotent.
void entropy_coord_wipe();

#endif  // SS_ENTROPY_COORDINATOR_H
