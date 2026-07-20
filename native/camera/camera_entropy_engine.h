// camera_entropy_engine — native libcamera capture engine for the image-entropy flow.
//
// Sibling of camera_engine (QR scan), sharing the SAME single-CameraManager invariant
// (§4.11 — the two engines are mutually exclusive; each refuses to start while the other
// holds the camera). Like the scan engine it runs TWO YUV420 streams: a preview at the
// display-sink size (which a blit worker converts to RGB565 into the shared camera_preview
// sink AND chains into the entropy digest), plus a higher-resolution still stream that
// supplies the one final frame. Python does nothing per displayed frame.
//
// Capture is the "2B" pinned-exposure path plus a real still. capture() snapshots the live
// session's last converged exposure/gain/colour-gains, pins them (AeEnable/AwbEnable=false
// + those values), lets the pin settle a few frames, then latches a frame off the STILL
// stream. So the final image is well-exposed and stable rather than a raw preview frame
// mid-AE-adjustment, AND it is a genuine high-resolution still rather than a copy of the
// 240x240 preview — which is what the PIL flow's start_single_frame_mode() used to provide
// and the first native cut dropped.
//
// Two things to know before touching this file:
//
//   - The still is the preview's geometry at kStillScale x resolution: SAME aspect, SAME
//     ScalerCrop, SAME field of view. That is load-bearing, not incidental. libcamera 0.3.2
//     exposes ONE global ScalerCrop shared by all streams (no per-stream ScalerCrops), so a
//     differently-shaped still would require swapping the crop mid-session — which renders
//     the preview stream visibly squeezed for the frames the reframe needs to work through
//     the pipeline, at capture AND on the first frames after a reshoot. Matching the preview
//     means the crop is set once and never touched. See kStillScale for why a wider still
//     also gains nothing on this sensor.
//   - still_w/still_h are POST-rotation dims and are derived, not assumed equal to the
//     sensor-space size. On the square Pi Zero sink they coincide; on a non-square sink they
//     do not, and this path stays correct where the preview path does not (see
//     docs/nonsquare-panel-preview-rotation-todo.md).
//
// The chain digest + latched frame are owned by the entropy_coordinator; the binding reads
// results from there, including the latched frame's dimensions — they are NOT the sink
// dims and must never be inferred as such.
//
// Python-free AND libcamera-free header (only camera_entropy_engine.cpp includes
// <libcamera/...>), so the camera_entropy binding includes just this.
#ifndef SS_CAMERA_ENTROPY_ENGINE_H
#define SS_CAMERA_ENTROPY_ENGINE_H

#include <cstdint>

// Bring up the entropy preview session and begin capture + chaining. The entropy preview
// screen + sink must already exist (the camera_entropy binding builds them first).
// Takes no rotation/fps argument: rotation is the sticky camera_config device setting
// (composed with the sensor-mount base) and the frame cap is CAMERA_TARGET_FPS, which
// keeps this argument-identical to the ESP camera_entropy.start(). Returns CAMERA_OK (0)
// on success, or a camera_error.h code (CAMERA_ERR_*) on failure — the binding raises
// OSError(code, camera_error_str(code)) (same codes as the ESP contract).
int camera_entropy_engine_start();

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
