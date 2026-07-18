// camera_entropy_engine — native libcamera capture engine for the image-entropy flow.
//
// Sibling of camera_engine (QR scan), sharing the SAME single-CameraManager invariant
// (§4.11 — the two engines are mutually exclusive; each refuses to start while the other
// holds the camera). Simpler than the scan engine: ONE YUV420 stream at the display-sink
// size (centered-square, converged 3A), a blit worker that converts YUV420 -> RGB565 into
// the shared camera_preview sink AND chains each preview frame into the entropy digest
// (entropy_coordinator). Python does nothing per displayed frame.
//
// Capture is the "2B" pinned-exposure path: capture() snapshots the live session's last
// converged exposure/gain/colour-gains, pins them (AeEnable/AwbEnable=false + those
// values), lets the pin settle a few frames, then latches that stabilized frame as the
// final image — so the latched frame is well-exposed and stable rather than a raw preview
// frame mid-AE-adjustment. The chain digest + latched frame are owned by the
// entropy_coordinator; the binding reads results from there.
//
// Python-free AND libcamera-free header (only camera_entropy_engine.cpp includes
// <libcamera/...>), so the camera_entropy binding includes just this.
#ifndef SS_CAMERA_ENTROPY_ENGINE_H
#define SS_CAMERA_ENTROPY_ENGINE_H

#include <cstdint>

// Bring up the entropy preview session and begin capture + chaining. The entropy preview
// screen + sink must already exist (the camera_entropy binding builds them first). rotate
// is 0/90/180/270; target_fps caps the sensor frame duration. Returns nullptr on success,
// or a short static error string on failure (the binding raises OSError with it).
const char *camera_entropy_engine_start(int rotate, int target_fps);

// Stop capture, join the blit worker, release the camera + CameraManager, wipe the
// coordinator. Idempotent.
void camera_entropy_engine_stop();

bool camera_entropy_engine_is_running();

// Pump-path hook (LVGL locus, every lvgl_runtime_pump iteration): publish the latest
// converted frame into the camera_preview sink + invalidate. Fast no-op when idle.
void camera_entropy_engine_pump_consume();

// Arm the pinned-exposure capture (camera_entropy.capture()). No-op if not running or a
// capture is already in progress.
void camera_entropy_engine_capture();

// Reshoot (camera_entropy.resume()): discard the latch, re-enable auto exposure/AWB, and
// resume chaining live frames. No-op if not running.
void camera_entropy_engine_resume();

#endif  // SS_CAMERA_ENTROPY_ENGINE_H
