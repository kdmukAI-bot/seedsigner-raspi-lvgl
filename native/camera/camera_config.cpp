#include "camera_config.h"

#include <atomic>

namespace {

// Written from Python (runtime init, and again whenever the user changes the
// setting) and read by whichever engine is starting. Those are not guaranteed to be
// the same thread, so it is atomic — relaxed ordering is sufficient: the value is a
// standalone scalar guarding no other state, and engines sample it once at start()
// rather than per frame.
std::atomic<int> g_user_rotation{0};

}  // namespace

void camera_config_set_rotation(int degrees) {
    // Normalize into [0,360) so a negative or out-of-range delta cannot poison the
    // composition in camera_config_effective_rotation(). C's % keeps the sign of the
    // dividend, hence the explicit correction.
    int d = degrees % 360;
    if (d < 0) {
        d += 360;
    }
    g_user_rotation.store(d, std::memory_order_relaxed);
}

int camera_config_get_rotation() {
    return g_user_rotation.load(std::memory_order_relaxed);
}

int camera_config_effective_rotation() {
    return (CAMERA_SENSOR_BASE_ROTATION + camera_config_get_rotation()) % 360;
}
