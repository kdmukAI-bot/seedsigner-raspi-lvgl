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
// reads its sink dimensions and publishes converted frames into it. rotate is one
// of 0/90/180/270; target_fps caps the sensor frame duration.
//
// Returns nullptr on success, or a short static error string on bring-up failure
// (no camera, config invalid, allocation/start failure) — the binding raises
// OSError with it, matching the ESP camera_scanner.start() contract.
const char *camera_engine_start(int rotate, int target_fps);

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

#endif  // SS_CAMERA_ENGINE_H
