// LVGL runtime lifecycle: init/shutdown, tick + pump loop, flush dispatch
// (native ST7789 vs Python callback), resolution switching, and the
// save/restore/clear screen helpers. The pump is host-driven: LVGL only
// advances while Python calls lvgl_pump().
#include "module_internal.h"

#include "gui_constants.h"    // set_display / active_profile
#include "input_profile.h"
#include "overlay_manager.h"  // native screensaver idle-watch dispatcher

#ifdef SS_CAMERA_ENGINE
#include "camera_engine.h"    // camera_engine_pump_consume (Phase-1 native capture)
#include "camera_entropy_engine.h"  // camera_entropy_engine_pump_consume (image-entropy)
#endif

#include <chrono>
#include <stdexcept>
#include <thread>

static bool s_lvgl_inited = false;
static std::vector<uint8_t> s_buf1;  // RGB565: 2 bytes/pixel, sized at runtime
static lv_display_t *s_disp = NULL;
static lv_indev_t *s_input_indev = NULL;
static uint64_t s_last_tick_ms = 0;
static uint32_t s_hor_res = 240;
static uint32_t s_ver_res = 240;
static PyObject *s_flush_cb_py = NULL;

static uint64_t now_ms() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

static void lvgl_tick_update() {
    if (!s_lvgl_inited) {
        return;
    }

    uint64_t now = now_ms();
    if (s_last_tick_ms == 0) {
        s_last_tick_ms = now;
        return;
    }

    uint64_t delta = now - s_last_tick_ms;
    if (delta == 0) {
        return;
    }

    if (delta > 100) {
        delta = 100;
    }

    lv_tick_inc(static_cast<uint32_t>(delta));
    s_last_tick_ms = now;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    size_t nbytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 2;  // RGB565: 2 bytes/pixel

    if (native_flush_active()) {
        native_flush_blit(area, px_map, nbytes);
    } else if (s_flush_cb_py != NULL) {
        PyGILState_STATE gil = PyGILState_Ensure();

        PyObject *payload = PyBytes_FromStringAndSize(reinterpret_cast<const char *>(px_map), static_cast<Py_ssize_t>(nbytes));
        if (payload != NULL) {
            PyObject *ret = PyObject_CallFunction(s_flush_cb_py, "iiiiO", area->x1, area->y1, area->x2, area->y2, payload);
            if (ret == NULL) {
                // Preserve KeyboardInterrupt so the pump loop can exit.
                // Only print/clear non-interrupt exceptions.
                if (!PyErr_ExceptionMatches(PyExc_KeyboardInterrupt)) {
                    PyErr_Print();
                }
            } else {
                Py_DECREF(ret);
            }
            Py_DECREF(payload);
        } else {
            if (!PyErr_ExceptionMatches(PyExc_KeyboardInterrupt)) {
                PyErr_Print();
            }
        }

        PyGILState_Release(gil);
    }

    lv_display_flush_ready(disp);
}

static void ensure_lvgl_runtime() {
    if (s_lvgl_inited) {
        return;
    }

    // lv_init() MUST precede set_display(): the i18n baked floor rasterizes its
    // five translated-text role fonts (OpenSans Western via tiny_ttf) inside
    // set_display(), but only once lv_is_initialized() is true. With the reverse
    // order those role fonts stay null, and the first Fallback-pack load (e.g.
    // ru, which chains under the baseline) dereferences null -> segfault. (The
    // old pre-i18n floor was static bitmap fonts, non-null at init, so order
    // didn't matter then.)
    lv_init();
    set_display(s_hor_res, s_ver_res);

    s_buf1.assign(static_cast<size_t>(s_hor_res) * s_ver_res * 2, 0);

    s_disp = lv_display_create(s_hor_res, s_ver_res);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_buf1.data(), NULL, s_buf1.size(),
                           LV_DISPLAY_RENDER_MODE_FULL);

    s_input_indev = lv_indev_create();
    lv_indev_set_type(s_input_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_input_indev, native_input_read_cb);

    input_profile_set_mode(INPUT_MODE_HARDWARE);

    // Start the native overlay manager (screensaver idle-watch) now that the
    // display + input devices exist — its contract requires both. The dispatcher
    // runs inside lv_timer_handler() (pumped by lvgl_pump). The idle timeout is
    // configured later by the Python runtime via set_screensaver_timeout().
    overlay_manager_init();

    s_last_tick_ms = now_ms();
    s_lvgl_inited = true;
}

static void lvgl_runtime_shutdown() {
    if (!s_lvgl_inited) {
        return;
    }

#if LV_USE_LOG
    LV_LOG_USER("lvgl runtime shutdown");
#endif
    // Tear down any live camera-preview session while its widgets + overlay handle
    // are still valid — after lv_deinit() the statics would dangle into a re-init.
    camera_preview_on_lvgl_shutdown();
#if defined(LVGL_VERSION_MAJOR) && (LVGL_VERSION_MAJOR >= 8)
    lv_deinit();
#endif
    s_disp = NULL;
    s_buf1.clear();
    s_last_tick_ms = 0;
    s_lvgl_inited = false;
}

// --- Cross-unit API (see module_internal.h) --------------------------------

void require_lvgl_runtime() {
    if (!s_lvgl_inited) {
        throw std::runtime_error("LVGL runtime not initialized: call lvgl_init(hor_res=..., ver_res=...) first");
    }
}

bool lvgl_runtime_is_inited() {
    return s_lvgl_inited;
}

int lvgl_runtime_pump(unsigned int duration_ms, unsigned int sleep_ms) {
    if (!s_lvgl_inited) {
        return 0;
    }

    auto start = std::chrono::steady_clock::now();
    while (true) {
        lvgl_tick_update();
#ifdef SS_CAMERA_ENGINE
        // Publish the newest engine-converted camera frame into the preview sink
        // (under the render lock) BEFORE rendering, so this pump paints it. No-op
        // when no capture session is active. This is the ONLY place an engine frame
        // reaches LVGL — invalidate stays on the LVGL locus (spec §4.9). Scan and entropy
        // engines are mutually exclusive, so at most one publishes per pump.
        camera_engine_pump_consume();
        camera_entropy_engine_pump_consume();
#endif
        lv_timer_handler();

        // Check for exceptions raised inside flush callbacks (e.g.
        // KeyboardInterrupt) or new pending signals (Ctrl+C).
        if (PyErr_Occurred() || PyErr_CheckSignals() != 0) {
            return -1;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed >= static_cast<long long>(duration_ms)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    return 0;
}

void lvgl_clear_to_black(bool clean_sys_layer) {
    if (clean_sys_layer) {
        lv_obj_clean(lv_layer_sys());
    }
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_scr_load(scr);
    lvgl_runtime_pump(50, 5);
}

// --- Python entry points ----------------------------------------------------

PyObject *py_lvgl_init(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    static const char *kwlist[] = {"hor_res", "ver_res", NULL};
    unsigned int hor_res = 240;
    unsigned int ver_res = 240;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|II", const_cast<char **>(kwlist), &hor_res, &ver_res)) {
        return NULL;
    }

    if (s_lvgl_inited) {
        Py_RETURN_NONE;
    }

    if (hor_res == 0 || ver_res == 0) {
        PyErr_SetString(PyExc_ValueError, "hor_res and ver_res must be > 0");
        return NULL;
    }

    s_hor_res = hor_res;
    s_ver_res = ver_res;
    ensure_lvgl_runtime();
    Py_RETURN_NONE;
}

PyObject *py_lvgl_shutdown(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    lvgl_runtime_shutdown();
    Py_RETURN_NONE;
}

PyObject *py_set_resolution(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    static const char *kwlist[] = {"width", "height", NULL};
    unsigned int width = 0;
    unsigned int height = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "II", const_cast<char **>(kwlist), &width, &height)) {
        return NULL;
    }

    if (!s_lvgl_inited) {
        PyErr_SetString(PyExc_RuntimeError, "LVGL runtime not initialized: call lvgl_init() first");
        return NULL;
    }

    if (width == s_hor_res && height == s_ver_res) {
        Py_RETURN_NONE;  // already at requested resolution
    }

    // Update the active display profile (aborts if no profile matches).
    set_display(width, height);

    s_hor_res = width;
    s_ver_res = height;

    // Delete the current LVGL display (also deletes all screens).
    if (s_disp) {
        lv_display_delete(s_disp);
        s_disp = NULL;
    }

    // Resize the draw buffer for the new resolution.
    s_buf1.assign(static_cast<size_t>(width) * height * 2, 0);

    // Create a new LVGL display at the new resolution.
    s_disp = lv_display_create(width, height);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_buf1.data(), NULL, s_buf1.size(),
                           LV_DISPLAY_RENDER_MODE_FULL);

    // lv_display_delete sets all associated indevs' display to NULL, which
    // silently disables them.  Reassign every indev to the new display.
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        lv_indev_set_display(indev, s_disp);
    }

    // If native display is active, send the new MADCTL for the target rotation.
    display_update_resolution(width, height);

    Py_RETURN_NONE;
}

// display_size() -> (width, height) of the active display profile.
//
// Gate on the runtime: active_profile() aborts() the process if no profile is set
// (a profile is only installed by lvgl_init()/set_resolution()), so raise a
// catchable RuntimeError for a premature call — same guard as locale_packs.cpp.
// Lets the app read vertical_resolution the same way on Pi and ESP32 (it just
// calls display_size() regardless of target).
PyObject *py_display_size(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    try {
        require_lvgl_runtime();
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }
    const DisplayProfile &p = active_profile();
    return Py_BuildValue("(ii)", p.width, p.height);
}

PyObject *py_lvgl_pump(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    static const char *kwlist[] = {"duration_ms", "sleep_ms", NULL};
    unsigned int duration_ms = 10;
    unsigned int sleep_ms = 1;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|II", const_cast<char **>(kwlist), &duration_ms, &sleep_ms)) {
        return NULL;
    }

    require_lvgl_runtime();
    if (lvgl_runtime_pump(duration_ms, sleep_ms) < 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}

PyObject *py_set_flush_callback(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *cb = Py_None;
    if (!PyArg_ParseTuple(args, "|O", &cb)) {
        return NULL;
    }

    if (cb != Py_None && !PyCallable_Check(cb)) {
        PyErr_SetString(PyExc_TypeError, "flush callback must be callable or None");
        return NULL;
    }

    Py_XINCREF(cb == Py_None ? NULL : cb);
    Py_XDECREF(s_flush_cb_py);
    s_flush_cb_py = (cb == Py_None) ? NULL : cb;
    Py_RETURN_NONE;
}

// --- Screen save / restore ------------------------------------------------
// General-purpose mechanism for preserving the active LVGL screen across
// an overlay (e.g. screensaver, modal dialog). The Python side decides
// when to save and restore; the C side just holds the pointer.

static lv_obj_t  *s_saved_screen = NULL;
static lv_group_t *s_saved_group = NULL;

PyObject *py_save_screen(PyObject *self, PyObject *args) {
    (void)self; (void)args;
    try {
        require_lvgl_runtime();
        s_saved_screen = lv_scr_act();

        // Save the current indev group.
        s_saved_group = NULL;
        lv_indev_t *indev = NULL;
        while ((indev = lv_indev_get_next(indev)) != NULL) {
            if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD ||
                lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
                s_saved_group = lv_indev_get_group(indev);
                break;
            }
        }
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }
    Py_RETURN_NONE;
}

PyObject *py_restore_screen(PyObject *self, PyObject *args) {
    (void)self; (void)args;
    try {
        require_lvgl_runtime();
        if (!s_saved_screen) {
            Py_RETURN_NONE;  // nothing saved — no-op
        }

        lv_obj_t *cur_scr = lv_scr_act();

        // Restore the saved indev group BEFORE deleting the overlay screen,
        // since deletion frees the overlay's group.
        if (s_saved_group) {
            lv_indev_t *indev = NULL;
            while ((indev = lv_indev_get_next(indev)) != NULL) {
                if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD ||
                    lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
                    lv_indev_set_group(indev, s_saved_group);
                }
            }
        }

        if (cur_scr != s_saved_screen) {
            lv_scr_load(s_saved_screen);
            // Synchronously delete the overlay screen now that the saved
            // screen is active and the indev group is restored.
            lv_obj_delete(cur_scr);
        }

        s_saved_screen = NULL;
        s_saved_group = NULL;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }
    Py_RETURN_NONE;
}

PyObject *py_clear_screen(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    try {
        require_lvgl_runtime();
        lvgl_clear_to_black(false);
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }
    Py_RETURN_NONE;
}

// set_screensaver_timeout(ms) -> None
//
// Idle time (ms) before the native overlay manager activates the screensaver;
// 0 disables it. The Python runtime calls this once at init with the configured
// screensaver_activation_ms. Per-screen opt-out is declarative (the cfg's
// allow_screensaver bool), so there is no suspend/resume export.
PyObject *py_set_screensaver_timeout(PyObject *self, PyObject *args) {
    (void)self;
    unsigned int ms = 0;
    if (!PyArg_ParseTuple(args, "I", &ms)) {
        return NULL;
    }
    overlay_manager_set_screensaver_timeout(static_cast<uint32_t>(ms));
    Py_RETURN_NONE;
}
