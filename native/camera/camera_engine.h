// camera_engine — native libcamera capture engine for the Pi Zero (Phase 1).
//
// Owns the process's SINGLE CameraManager (feasibility spec §4.11). Brings up the
// dual-stream session the Phase-0 harness proved (decode 480x480 + display
// sink-sized YUV420, centered-square ScalerCrop), runs libcamera's completion
// handler copy/flag/requeue-only on the manager thread, and converts frames to
// RGB565 on an engine-owned blit worker — so Python does nothing per displayed
// frame. Converted frames reach the LVGL camera_preview sink via the pump-path
// consume hook under a render lock.
//
// This header is deliberately Python-free AND libcamera-free: only camera_engine.cpp
// includes <libcamera/...>, so Python.h and the libcamera C++ headers never share a
// translation unit. Callers (the camera_scanner binding, lvgl_runtime pump) include
// just this.
#ifndef SS_CAMERA_ENGINE_H
#define SS_CAMERA_ENGINE_H

#include <cstddef>
#include <cstdint>

// Bring up a live preview session and begin capture. The camera_preview screen +
// overlay must already exist (camera_scanner.start builds them first) — the engine
// reads its sink dimensions and publishes converted frames into it.
//
// Takes no rotation or fps argument: rotation is a sticky device setting sampled
// here from camera_config (composed with the sensor-mount base), and the frame
// duration cap is the CAMERA_TARGET_FPS constant. That keeps this start() argument-
// identical to the ESP camera_scanner.start().
//
// Returns CAMERA_OK (0) on success, or a camera_error.h code (CAMERA_ERR_*) on
// bring-up failure (no camera, config invalid, allocation/start failure) — the
// binding raises OSError(code, camera_error_str(code)), matching the ESP
// camera_scanner.start() contract (same codes on both platforms).
int camera_engine_start();

// Stop capture, join the blit worker, release the camera + CameraManager, unmap
// buffers. Idempotent (safe to call when not running).
void camera_engine_stop();

bool camera_engine_is_running();

// Pump-path hook, called every lvgl_runtime_pump iteration on the LVGL locus: if
// the blit worker has published a newer converted frame, copy it into the
// camera_preview sink and invalidate (the render lock is the engine's output
// mutex, held only for the copy). Fast no-op when idle or no session is active.
void camera_engine_pump_consume();

// Lifetime frame counters for validation/instrumentation: frames the manager thread
// captured (in), the blit worker converted (conv), and the pump published to the
// sink (pub). All zero when no session has run. Used by camera_scanner._debug_stats.
void camera_engine_stats(uint64_t *frames_in, uint64_t *frames_conv, uint64_t *frames_pub);

// Decode-worker counters: zbar passes run (attempts) and passes that found >=1 QR
// symbol (hits). attempts/elapsed is the decoder's frame rate. Zero when no session.
void camera_engine_decode_stats(uint64_t *attempts, uint64_t *hits);

#endif  // SS_CAMERA_ENGINE_H
