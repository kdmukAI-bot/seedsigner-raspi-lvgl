// camera_preview.cpp — Pi Zero live camera-preview scan surface.
//
// The portable overlay (components/seedsigner/camera_preview_overlay.{h,cpp}) is
// PASSIVE CHROME ONLY: status bar, progress fill, instruction text, back-affordance,
// status dot. It never touches camera pixels — it draws OVER a pixel plane the
// platform owns. On ESP32 that plane is an lv_image fed by the camera pipeline
// (board_pipeline_display_lvgl.c) with the overlay created above it
// (camera_scanner.cpp). This file is the Pi equivalent: it owns the pixel plane (a
// full-screen RGB565 lv_image the host pushes frames into) and creates the same
// portable overlay on top. The screens submodule needs no changes — the overlay is
// called as a black box.
//
// Execution model (Stage 1, host-driven pump): Python captures a frame (picamera),
// converts it to RGB565, calls camera_preview_set_frame(bytes), then lvgl_pump().
// Decode (pyzbar/DecodeQR) stays in Python; camera_preview_set_progress() advances
// the overlay a few times/sec (never per frame). All calls happen under the GIL and,
// in the app, under renderer.lock — single-writer, so no extra locking here. Post
// display-driver cutover a native background task will pump (like ESP32); keep these
// calls cheap + lock-safe so they survive that move (ESP cam_present is the template).
//
// FRAME FORMAT CONTRACT: set_frame() takes LVGL-NATIVE RGB565 (w*h*2 bytes), NEVER
// pre-swapped for the panel. Once a frame is lv_image content the active flush driver
// (python flush -> ST7789.py, or native flush -> display_st7789.cpp) applies its
// panel byte-order/BGR handling uniformly to camera pixels AND overlay widgets.
// Pre-swapping would be correct under one flush driver and double-swapped under the
// other; feeding LVGL-native keeps this binding flush-mode-agnostic (cutover-safe).
#include "module_internal.h"

#include "screen_scaffold.h"          // load_screen_and_cleanup_previous
#include "overlay_manager.h"          // SS_OBJ_FLAG_NO_SCREENSAVER (per-screen saver opt-out)
#include "camera_preview_overlay.h"   // camera_preview_overlay_* (portable, called as a black box)
#include "camera_preview_sink.h"      // the Python-free sink bridge the native engine calls

#include <cstring>                    // memcpy
#include <stdexcept>
#include <string>
#include <vector>

// --- Live session state -----------------------------------------------------
// One camera, one session. The screen is reaped by the app's next
// load_screen_and_cleanup_previous(); the overlay HANDLE is a separate lv_malloc'd
// struct that widget teardown does NOT free, so close() must always destroy it.
static lv_obj_t                 *s_cam_screen = nullptr;
static lv_obj_t                 *s_cam_img    = nullptr;
static lv_image_dsc_t            s_cam_dsc;
static std::vector<uint8_t>      s_cam_buf;     // dsc.data backing; sized once per session
static uint8_t                  *s_cam_data = nullptr;  // == s_cam_buf.data() while a session is live
static size_t                    s_cam_size = 0;        // == w*h*2
static camera_preview_overlay_t *s_overlay  = nullptr;

// Drop the overlay handle + backing buffer. Safe after the screen was reaped
// externally: overlay_destroy() only touches the anim subsystem + frees the handle
// struct (never the already-freed widgets). Does NOT delete the screen object.
static void camera_preview_teardown() {
    if (s_overlay) {
        camera_preview_overlay_destroy(s_overlay);
        s_overlay = nullptr;
    }
    s_cam_screen = nullptr;
    s_cam_img    = nullptr;
    s_cam_data   = nullptr;
    s_cam_size   = 0;
    std::vector<uint8_t>().swap(s_cam_buf);  // release capacity
}

// Called from lvgl_runtime_shutdown() BEFORE lv_deinit(), while the widgets + the
// overlay handle are still valid: destroys the handle and nulls every static so a
// subsequent lvgl_init() + set_frame/build can't dereference a freed lv_obj (the
// build-time tests init/shutdown the runtime repeatedly across modules).
void camera_preview_on_lvgl_shutdown() {
    camera_preview_teardown();
}

// End the live session (shared by py_camera_preview_close and camera_scanner.stop()):
// drop the overlay handle + sink buffer and reset LVGL's idle clock so the successor
// screen gets a full screensaver window. Idempotent.
void camera_preview_close_session() {
    camera_preview_teardown();
    if (lvgl_runtime_is_inited()) {
        lv_display_trigger_activity(NULL);
    }
}

// --- Builder ----------------------------------------------------------------
// Build the live preview screen + portable overlay (the body shared by the Python
// binding and camera_scanner.start(), which owns the screen on the Pi like the ESP
// camera_scanner does). Throws std::runtime_error on failure; the callers translate
// that to a Python exception. `instructions` is the optional bottom-line text.
void camera_preview_build_session(const std::string &instructions) {
    {
        require_lvgl_runtime();

        // A rebuild without close() first must not leak the prior handle.
        if (s_overlay) {
            camera_preview_overlay_destroy(s_overlay);
            s_overlay = nullptr;
        }

        const int32_t w = lv_display_get_horizontal_resolution(NULL);
        const int32_t h = lv_display_get_vertical_resolution(NULL);
        const size_t  buf_size = static_cast<size_t>(w) * static_cast<size_t>(h) * 2;

        // Bare black screen (chrome-free: no top-nav scaffold).
        lv_obj_t *scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

        // Pixel sink: one full-screen RGB565 lv_image the host memcpy's frames into.
        // The dsc.data pointer stays STABLE for the whole session (buffer sized once,
        // never resized on set_frame) so LVGL's cached decode aliases our buffer and
        // a memcpy + invalidate updates what's drawn — the ESP image-widget contract.
        s_cam_buf.assign(buf_size, 0);
        s_cam_data = s_cam_buf.data();
        s_cam_size = buf_size;

        lv_memzero(&s_cam_dsc, sizeof(s_cam_dsc));
        s_cam_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
        s_cam_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
        s_cam_dsc.header.w      = w;
        s_cam_dsc.header.h      = h;
        s_cam_dsc.header.stride = static_cast<uint32_t>(w) * 2;
        s_cam_dsc.data_size     = static_cast<uint32_t>(buf_size);
        s_cam_dsc.data          = s_cam_data;

        s_cam_img = lv_image_create(scr);
        lv_obj_set_size(s_cam_img, w, h);
        lv_obj_set_pos(s_cam_img, 0, 0);
        lv_image_set_src(s_cam_img, &s_cam_dsc);
        lv_obj_remove_flag(s_cam_img,
                           (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

        // Overlay chrome ON TOP (created after the image so it draws above it). On the
        // Pi (short dim <= 240) the preview square fills the whole display: square ==
        // screen, so the overlay adds no gutter fills. Start on the back-affordance
        // state (instruction text shown); the first set_progress() raises the bar
        // (mirrors Python ScanScreen: instructions first, progress bar once decoding).
        camera_preview_overlay_spec_t spec;
        lv_memzero(&spec, sizeof(spec));
        spec.instructions_text = instructions.empty() ? nullptr : instructions.c_str();
        spec.square_x = 0;
        spec.square_y = 0;
        spec.square_w = w;
        spec.square_h = h;
        spec.scanning_active  = false;
        spec.progress_percent = 0;
        spec.frame_status     = CAMERA_OVERLAY_FRAME_NONE;
        s_overlay = camera_preview_overlay_create(scr, &spec);
        if (!s_overlay) {
            lv_obj_delete(scr);
            camera_preview_teardown();
            throw std::runtime_error("camera_preview overlay create failed");
        }

        s_cam_screen = scr;

        // A scan runs with NO user input while the user lines up the QR. Opt this
        // screen out of the idle screensaver (the overlay-manager dispatcher reads
        // this flag off the active screen and skips activation) so the saver never
        // covers the live preview mid-scan. The flag rides on the screen object, so
        // it auto-clears on the next screen swap. camera_preview_close() then resets
        // LVGL's idle clock so the successor screen still gets a full saver window.
        lv_obj_add_flag(scr, SS_OBJ_FLAG_NO_SCREENSAVER);

        load_screen_and_cleanup_previous(scr);
        mark_last_path_compiled();
    }
}

PyObject *py_camera_preview_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = nullptr;
    if (!PyArg_ParseTuple(args, "|O", &cfg)) {
        return nullptr;
    }
    if (cfg && cfg != Py_None && !PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "camera_preview_screen expects cfg_dict as dict");
        return nullptr;
    }

    // Optional hardware/joystick bottom-line text (already localized + composed by the
    // host, e.g. "< back  |  Scan a QR code"). Borrowed ref; copied into std::string so
    // it outlives the overlay build (the overlay copies it into an lv_label).
    std::string instructions;
    if (cfg && cfg != Py_None) {
        PyObject *it = PyDict_GetItemString(cfg, "instructions_text");  // borrowed
        if (it && PyUnicode_Check(it)) {
            const char *s = PyUnicode_AsUTF8(it);
            if (!s) {
                return nullptr;  // encoding error already set
            }
            instructions = s;
        }
    }

    try {
        camera_preview_build_session(instructions);
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
    Py_RETURN_NONE;
}

// --- Native-engine sink bridge (camera_preview_sink.h) ----------------------
// Python-free entry points the native camera engine calls (camera_engine.cpp). All
// three run on the pump/LVGL thread — the engine's consume hook fires inside
// lvgl_runtime_pump — so blit_rgb565's lv_obj_invalidate stays on the LVGL locus.
bool camera_preview_session_active() {
    return s_cam_img != nullptr && s_cam_data != nullptr;
}

void camera_preview_get_sink_dims(int *w, int *h) {
    if (w) {
        *w = static_cast<int>(s_cam_dsc.header.w);
    }
    if (h) {
        *h = static_cast<int>(s_cam_dsc.header.h);
    }
}

void camera_preview_blit_rgb565(const uint8_t *rgb565, size_t nbytes) {
    if (!s_cam_img || !s_cam_data || nbytes != s_cam_size) {
        return;  // no session / size mismatch — silent no-op
    }
    memcpy(s_cam_data, rgb565, s_cam_size);
    lv_obj_invalidate(s_cam_img);
}

// --- Frame push -------------------------------------------------------------
// camera_preview_set_frame(frame: bytes) -> None
// frame is LVGL-native RGB565, exactly w*h*2 bytes (see the FRAME FORMAT CONTRACT
// at the top). Zero-copy read of the buffer, memcpy into the sink, invalidate.
// Safe no-op when no session is active.
PyObject *py_camera_preview_set_frame(PyObject *self, PyObject *args) {
    (void)self;

    Py_buffer view;
    if (!PyArg_ParseTuple(args, "y*", &view)) {  // y* == read-only bytes-like, zero-copy
        return nullptr;
    }

    if (!s_cam_img || !s_cam_data) {
        PyBuffer_Release(&view);
        Py_RETURN_NONE;  // no active session — no-op
    }

    if (static_cast<size_t>(view.len) != s_cam_size) {
        PyErr_Format(PyExc_ValueError,
                     "camera_preview_set_frame: expected %zu RGB565 bytes, got %zd",
                     s_cam_size, (Py_ssize_t)view.len);
        PyBuffer_Release(&view);
        return nullptr;
    }

    memcpy(s_cam_data, view.buf, s_cam_size);
    PyBuffer_Release(&view);
    lv_obj_invalidate(s_cam_img);  // mark dirty; the next pump re-reads the sink buffer

    Py_RETURN_NONE;
}

// camera_preview_set_frame_yuv420(buf, src_w, src_h, y_stride, uv_stride, rotate) -> None
// Convert a planar I420/YUV420 frame (Y plane, then U, then V; U/V at half resolution)
// straight into the LVGL-native RGB565 sink, applying a 0/90/180/270-degree rotation.
// The three planes are contiguous in buf at Y=0, U=y_stride*src_h, V=U+uv_stride*(src_h/2)
// (the RPi vc4 YUV420 layout). No scaling: the ROTATED source dims must equal the sink
// dims. This is the Phase-0 replacement for the old per-frame numpy RGB->RGB565 convert
// (numpy is gone on the libcamera dev image); the platform owns the pixel plane, so the
// host converts here rather than shipping pre-made RGB565. BT.601 studio-range coeffs.
PyObject *py_camera_preview_set_frame_yuv420(PyObject *self, PyObject *args) {
    (void)self;

    Py_buffer view;
    int src_w, src_h, y_stride, uv_stride, rotate;
    if (!PyArg_ParseTuple(args, "y*iiiii", &view, &src_w, &src_h, &y_stride, &uv_stride, &rotate)) {
        return nullptr;
    }

    if (!s_cam_img || !s_cam_data) {
        PyBuffer_Release(&view);
        Py_RETURN_NONE;  // no active session — no-op
    }

    if (src_w <= 0 || src_h <= 0 || (src_w & 1) || (src_h & 1) ||
        y_stride < src_w || uv_stride < (src_w + 1) / 2) {
        PyErr_SetString(PyExc_ValueError, "set_frame_yuv420: bad src dims/strides");
        PyBuffer_Release(&view);
        return nullptr;
    }

    const int  dst_w = static_cast<int>(s_cam_dsc.header.w);
    const int  dst_h = static_cast<int>(s_cam_dsc.header.h);
    const bool swap  = (rotate == 90 || rotate == 270);
    const int  rot_w = swap ? src_h : src_w;
    const int  rot_h = swap ? src_w : src_h;
    if (rot_w != dst_w || rot_h != dst_h) {
        PyErr_Format(PyExc_ValueError,
                     "set_frame_yuv420: rotated src %dx%d != sink %dx%d (no scaling)",
                     rot_w, rot_h, dst_w, dst_h);
        PyBuffer_Release(&view);
        return nullptr;
    }

    const size_t y_size  = static_cast<size_t>(y_stride) * src_h;
    const size_t uv_size = static_cast<size_t>(uv_stride) * (src_h / 2);
    const size_t need    = y_size + 2 * uv_size;
    if (static_cast<size_t>(view.len) < need) {
        PyErr_Format(PyExc_ValueError,
                     "set_frame_yuv420: buffer %zd < needed %zu", (Py_ssize_t)view.len, need);
        PyBuffer_Release(&view);
        return nullptr;
    }

    const uint8_t *base = static_cast<const uint8_t *>(view.buf);
    const uint8_t *Yp = base;
    const uint8_t *Up = base + y_size;
    const uint8_t *Vp = Up + uv_size;
    uint16_t *dst = reinterpret_cast<uint16_t *>(s_cam_data);

    for (int sy = 0; sy < src_h; ++sy) {
        const uint8_t *yrow = Yp + static_cast<size_t>(sy) * y_stride;
        const uint8_t *urow = Up + static_cast<size_t>(sy >> 1) * uv_stride;
        const uint8_t *vrow = Vp + static_cast<size_t>(sy >> 1) * uv_stride;
        for (int sx = 0; sx < src_w; ++sx) {
            const int c = 298 * (static_cast<int>(yrow[sx]) - 16);
            const int u = static_cast<int>(urow[sx >> 1]) - 128;
            const int v = static_cast<int>(vrow[sx >> 1]) - 128;
            int r = (c + 409 * v + 128) >> 8;
            int g = (c - 100 * u - 208 * v + 128) >> 8;
            int b = (c + 516 * u + 128) >> 8;
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            const uint16_t px = static_cast<uint16_t>(
                ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));

            int dx, dy;
            switch (rotate) {
                case 90:  dx = src_h - 1 - sy; dy = sx;             break;
                case 180: dx = src_w - 1 - sx; dy = src_h - 1 - sy; break;
                case 270: dx = sy;             dy = src_w - 1 - sx; break;
                default:  dx = sx;             dy = sy;             break;  // 0
            }
            dst[static_cast<size_t>(dy) * dst_w + dx] = px;
        }
    }

    PyBuffer_Release(&view);
    lv_obj_invalidate(s_cam_img);
    Py_RETURN_NONE;
}

// --- Overlay state (a few updates/sec, never per frame) ---------------------
// camera_preview_set_progress(percent: int, frame_status: int) -> None
// frame_status: 0 none / 1 added(green) / 2 repeated(gray) / 3 miss(hidden),
// matching Python ScanScreen FRAME__*. Implies scanning (raises the bar).
PyObject *py_camera_preview_set_progress(PyObject *self, PyObject *args) {
    (void)self;

    int percent = 0;
    int frame_status = 0;
    if (!PyArg_ParseTuple(args, "ii", &percent, &frame_status)) {
        return nullptr;
    }
    if (s_overlay) {
        camera_preview_overlay_set_progress(
            s_overlay, percent, (camera_overlay_frame_status_t)frame_status);
    }
    Py_RETURN_NONE;
}

// C++ helper shared by the Python binding and camera_scanner.start(): toggle the
// overlay between the back-affordance state and the scanning status-bar state.
void camera_preview_set_scanning_active(bool active) {
    if (s_overlay) {
        camera_preview_overlay_set_scanning(s_overlay, active);
    }
}

// Drive the overlay status bar + dot from camera_scanner.report()/report_complete().
// Arg order (frame_status, percent) matches the ESP camera_scanner.report() contract
// — the REVERSE of set_progress(percent, frame_status), which retires with the rest of
// the Phase-0 surface. No-op when no overlay is active.
void camera_preview_report(int frame_status, int percent) {
    if (s_overlay) {
        camera_preview_overlay_set_progress(
            s_overlay, percent, (camera_overlay_frame_status_t)frame_status);
    }
}

// camera_preview_set_scanning(active: bool) -> None
// Toggle between the back-affordance state and the status-bar state.
PyObject *py_camera_preview_set_scanning(PyObject *self, PyObject *args) {
    (void)self;

    int active = 0;
    if (!PyArg_ParseTuple(args, "p", &active)) {
        return nullptr;
    }
    camera_preview_set_scanning_active(active != 0);
    Py_RETURN_NONE;
}

// --- Teardown ---------------------------------------------------------------
// camera_preview_close() -> None
// End the session: destroy the overlay handle + free the sink buffer. Call this when
// the scan ends, BEFORE loading the next screen (the next build's
// load_screen_and_cleanup_previous reaps our screen object). Idempotent.
PyObject *py_camera_preview_close(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    // Reset LVGL's idle clock so the successor screen gets a full screensaver window:
    // a no-input scan leaves inactive-time large, which would otherwise immediately
    // fire the saver over the next (flag-free) screen.
    camera_preview_close_session();
    Py_RETURN_NONE;
}
