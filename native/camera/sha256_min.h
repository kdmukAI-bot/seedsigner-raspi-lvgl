// sha256_min — a small, self-contained SHA-256 for the image-entropy chain.
//
// The native camera_entropy engine chains preview frames into a running SHA-256 in C
// (pixels never enter Python, mirroring the ESP cam_pipeline_entropy consumer). It must
// produce byte-identical digests to Python's hashlib.sha256 so the app can finish the
// hash — final = SHA256(chain ‖ latched_frame) — identically (see
// helpers/mnemonic_generation.generate_mnemonic_from_camera_entropy). Standard FIPS-180-4
// SHA-256, so any correct implementation matches; a build-time test pins it to a known
// vector + hashlib.
//
// Deliberately dependency-free (no OpenSSL/mbedTLS/zbar) so it links into the main
// extension without pulling in another library. C-linkage so the C++ engine and any C TU
// can share it.
#ifndef SS_SHA256_MIN_H
#define SS_SHA256_MIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;      // total message length in bits
    uint8_t  buf[64];     // partial block
    uint32_t buflen;      // bytes currently in buf (0..63)
} ss_sha256_ctx;

#define SS_SHA256_DIGEST_LEN 32

// Standard init/update/final. The ctx is a plain value type: copying it (struct
// assignment) snapshots the running hash — the entropy latch finalizes a COPY so the
// original keeps chaining after a reshoot (resume).
void ss_sha256_init(ss_sha256_ctx *ctx);
void ss_sha256_update(ss_sha256_ctx *ctx, const void *data, size_t len);
// Writes SS_SHA256_DIGEST_LEN bytes into out. Consumes ctx (finalize a copy to keep going).
void ss_sha256_final(ss_sha256_ctx *ctx, uint8_t out[SS_SHA256_DIGEST_LEN]);

#ifdef __cplusplus
}
#endif

#endif  // SS_SHA256_MIN_H
