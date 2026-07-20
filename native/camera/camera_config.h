// camera_config — sticky, process-wide camera settings shared by BOTH capture
// engines (scan + entropy).
//
// Rotation is a DEVICE setting: set rarely, and it applies to every camera flow
// alike. Modelling it as a sticky setter rather than a start() argument is what
// lets camera_scanner.start() / camera_entropy.start() stay argument-identical to
// the ESP bindings, so one Python call shape drives both platforms.
//
// Deliberately Python-free and libcamera-free, like the engine headers.
#ifndef SS_CAMERA_CONFIG_H
#define SS_CAMERA_CONFIG_H

// Clockwise degrees the sensor's mount sits off upright. Every frame needs this
// much rotation before it is the right way up, which is why it is applied even when
// the user's delta is 0. Standard Pi board only — SeedSigner+ (landscape) may need a
// different base, which is exactly why this is a native constant and never the app's
// concern.
constexpr int CAMERA_SENSOR_BASE_ROTATION = 90;

// Sensor frame-duration cap. The app never varies this, so it is a constant rather
// than a binding argument.
constexpr int CAMERA_TARGET_FPS = 15;

// The USER's rotation DELTA — the raw SETTING__CAMERA_ROTATION value, passed through
// unmodified (0/90/180/270). The app must NOT pre-add the sensor base; this layer
// composes it. Values are normalized into [0,360); the binding enforces the
// multiple-of-90 rule before calling.
void camera_config_set_rotation(int degrees);
int camera_config_get_rotation();

// What the engines actually rotate by: (base + user delta) % 360, CLOCKWISE.
//
// The retired PIL path rotated COUNTER-clockwise, so the two are opposite-handed and
// the setting values do not carry over unchanged: 90 and 270 mean the same on both,
// but 0 and 180 swap. Hence the app default is 0 (it was 180 under PIL) — both land
// on 90 CW, the correct alignment for the standard build. See the app-side
// camera-rotation contract for the full mapping table.
int camera_config_effective_rotation();

#endif  // SS_CAMERA_CONFIG_H
