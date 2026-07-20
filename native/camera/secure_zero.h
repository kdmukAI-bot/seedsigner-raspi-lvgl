// secure_zero — buffer scrub the optimizer is not allowed to elide.
//
// A plain std::memset immediately before free()/deallocate is a dead store, and the
// compiler is entitled to remove it at the -O2 the ARMv6 build uses. Routing the call
// through a volatile function pointer makes the indirect call opaque to the optimizer,
// so the stores must be emitted. This is the same guarantee mbedtls_platform_zeroize
// gives the ESP32 sibling (cam_pipeline_entropy scrubs both the chain digest and the
// latched frame before freeing them); the Pi path matches it rather than relying on
// explicit_bzero/memset_s, neither of which the cross-build should depend on.
//
// Used for the image-entropy chain digest + latched final frame, both of which are
// direct inputs to BIP-39 seed derivation.
#ifndef SS_SECURE_ZERO_H
#define SS_SECURE_ZERO_H

#include <stddef.h>
#include <string.h>

inline void ss_secure_zero(void *p, size_t len) {
    if (!p || len == 0) {
        return;
    }
    static void *(*volatile const memset_v)(void *, int, size_t) = &::memset;
    memset_v(p, 0, len);
}

#endif  // SS_SECURE_ZERO_H
